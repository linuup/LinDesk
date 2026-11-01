#include "desktop/widgets/linwidget/WidgetManager.h"
#include "desktop/widgets/cef/LinWidgetCanvasWidget.h"
#include "desktop/widgets/cef/CefOsrBrowserInstance.h"
#include "desktop/widgets/cef/CefManager.h"
#include "desktop/widgets/linwidget/ConfigManager.h"
#include "desktop/widgets/linwidget/PermissionManager.h"
#include "desktop/widgets/linwidget/WidgetMessageTypes.h"
#include "desktop/widgets/linwidget/WidgetPackage.h"
#include "desktop/widgets/linwidget/WidgetRegistryManager.h"
#include "services/AppPaths.h"
#include "services/DpiCoordinateHelper.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>
#include <QDebug>
#include <QGuiApplication>
#include <QScreen>
#include <QSet>
#include <QStringList>
#include <QSettings>
#include <QFileDialog>
#include <QDirIterator>
#include <QSaveFile>
#include <QDesktopServices>
#include <QUrl>
#include <QRegularExpression>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace LinWidget {
namespace {
constexpr int kRuntimeHealthCheckIntervalMs = 2500;
constexpr int kMaxWidgetInstancesPerRegistry = 3;
constexpr qint64 kWidgetStorageQuotaBytes = 16LL * 1024LL * 1024LL;

qint64 calculateDirectorySize(const QString& dirPath)
{
    if (dirPath.isEmpty() || !QFileInfo::exists(dirPath)) {
        return 0;
    }
    const QFileInfo rootInfo(dirPath);
    if (rootInfo.isFile()) {
        return rootInfo.size();
    }
    qint64 total = 0;
    QDirIterator it(dirPath, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

QString canonicalPackagePath(const QString& packagePath)
{
    const QFileInfo info(packagePath);
    const QString path = info.exists() ? info.absoluteFilePath() : packagePath;
    return QDir::cleanPath(path).toLower();
}

QString widgetIdentityKey(const WidgetManifest& manifest, const QString& packagePath)
{
    const QString manifestId = manifest.id.trimmed().toLower();
    return manifestId.isEmpty() ? canonicalPackagePath(packagePath) : manifestId;
}

QString instanceLayoutId(const WidgetManifest& manifest, bool isDevelopment)
{
    return QStringLiteral("%1:%2").arg(isDevelopment ? QStringLiteral("dev") : QStringLiteral("installed"),
                                       manifest.id.trimmed().toLower());
}

QString registryIdForManifest(const WidgetManifest& manifest)
{
    return manifest.id.trimmed().toLower();
}

QString newMultiInstanceId(const WidgetManifest& manifest, bool isDevelopment)
{
    const QString prefix = instanceLayoutId(manifest, isDevelopment);
    const QString stamp = QString::number(QDateTime::currentMSecsSinceEpoch(), 36).right(8);
    return QStringLiteral("%1:%2").arg(prefix, stamp);
}

bool isMultiInstanceId(const QString& instanceId)
{
    return instanceId.count(QLatin1Char(':')) >= 2;
}

QString stateRegistryId(const WidgetLayoutState& state)
{
    if (!state.registryId.trimmed().isEmpty()) {
        return state.registryId.trimmed().toLower();
    }
    WidgetManifest manifest;
    if (WidgetPackage::loadManifest(state.packagePath, &manifest, nullptr)) {
        return registryIdForManifest(manifest);
    }
    const QStringList parts = state.instanceId.split(QLatin1Char(':'));
    return parts.size() >= 2 ? parts.at(1).trimmed().toLower() : QString();
}

bool stateIsDevelopment(const WidgetLayoutState& state)
{
    return state.isDevelopment || state.instanceId.startsWith(QStringLiteral("dev:"));
}

bool sameWidgetFamily(const WidgetInstance& source, const WidgetLayoutState& target)
{
    if (source.instanceId.isEmpty() || target.instanceId.isEmpty()) {
        return false;
    }
    const QString sourceRegistryId = registryIdForManifest(source.manifest);
    return stateRegistryId(target) == sourceRegistryId && stateIsDevelopment(target) == source.isDevelopment;
}

QRect screenClampedGeometry(const QRect& geometry)
{
    QScreen* screen = QGuiApplication::screenAt(geometry.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    const QRect bounds = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
    QRect next = geometry;
    if (next.width() > bounds.width()) {
        next.setWidth(bounds.width());
    }
    if (next.height() > bounds.height()) {
        next.setHeight(bounds.height());
    }
    if (next.right() > bounds.right()) {
        next.moveRight(bounds.right());
    }
    if (next.bottom() > bounds.bottom()) {
        next.moveBottom(bounds.bottom());
    }
    if (next.left() < bounds.left()) {
        next.moveLeft(bounds.left());
    }
    if (next.top() < bounds.top()) {
        next.moveTop(bounds.top());
    }
    return next;
}

QStringList grantedPermissionsForManifest(const WidgetManifest& manifest, const QString& packagePath, bool isDevelopment)
{
    PermissionManager permissions;
    if (isDevelopment) {
        return permissions.normalizePermissions({ QStringLiteral("network"), QStringLiteral("storage"), QStringLiteral("desktop") });
    }

    auto& registry = WidgetRegistryManager::instance();
    registry.load();
    registry.upsertFromPackage(packagePath);
    registry.save();

    const WidgetRegistryEntry entry = registry.entryById(manifest.id);
    QStringList granted = entry.permissionsGranted;
    // Bundled (built-in) widgets ship with LinDesk itself, so their declared permissions are implicitly granted.
    const QString normalizedPath = QDir::fromNativeSeparators(packagePath);
    if (entry.trusted || normalizedPath.contains(QStringLiteral("resources/LinWidgets"), Qt::CaseInsensitive)) {
        granted += manifest.permissions;
    }
    return permissions.normalizePermissions(granted);
}

QSize clampedWidgetSize(const WidgetManifest& manifest, const QSize& requestedSize)
{
    QSize size = requestedSize.isValid()
        ? requestedSize
        : QSize(manifest.width, manifest.height);
    if (!manifest.resizable) {
        return QSize(manifest.width, manifest.height);
    }

    size.setWidth(qMax(size.width(), manifest.minWidth));
    size.setHeight(qMax(size.height(), manifest.minHeight));
    if (manifest.maxWidth > 0) {
        size.setWidth(qMin(size.width(), manifest.maxWidth));
    }
    if (manifest.maxHeight > 0) {
        size.setHeight(qMin(size.height(), manifest.maxHeight));
    }
    return size;
}

QRect clampedWidgetGeometry(const WidgetManifest& manifest, const QRect& requestedGeometry)
{
    QRect geometry = requestedGeometry;
    if (geometry.isEmpty()) {
        geometry = QRect(QPoint(0, 0), QSize(manifest.width, manifest.height));
    }
    geometry.setSize(clampedWidgetSize(manifest, geometry.size()));
    return geometry;
}

QRect resolvedWidgetGeometry(const WidgetManifest& manifest, const QRect& requestedGeometry, bool isDevelopment)
{
    QRect geometry = requestedGeometry;
    if (geometry.isEmpty()) {
        geometry = QRect(QPoint(0, 0), QSize(manifest.width, manifest.height));
    }
    if (isDevelopment) {
        geometry.setSize(clampedWidgetSize(manifest, QSize(manifest.width, manifest.height)));
        return geometry;
    }
    geometry.setSize(clampedWidgetSize(manifest, geometry.size()));
    return geometry;
}

QVector<WidgetLayoutState> deduplicatedLayoutStates(const QVector<WidgetLayoutState>& states)
{
    QVector<WidgetLayoutState> result;
    QHash<QString, int> indexByPackage;
    for (const WidgetLayoutState& state : states) {
        if (isMultiInstanceId(state.instanceId)) {
            result.append(state);
            continue;
        }
        const QString key = canonicalPackagePath(state.packagePath);
        if (key.isEmpty()) {
            continue;
        }

        const auto existing = indexByPackage.find(key);
        if (existing == indexByPackage.end()) {
            indexByPackage.insert(key, result.size());
            result.append(state);
            continue;
        }

        WidgetLayoutState& kept = result[*existing];
        if (state.visible || !kept.visible) {
            kept = state;
        }
    }
    return result;
}

#ifdef Q_OS_WIN
QRect windowRect(HWND window)
{
    RECT rect = {};
    if (!window || !GetWindowRect(window, &rect)) {
        return QRect();
    }
    return QRect(rect.left,
                 rect.top,
                 qMax(0L, rect.right - rect.left),
                 qMax(0L, rect.bottom - rect.top));
}

QRect bestLogicalLayerRect(HWND layerWindow, const RECT& layerNativeRect)
{
    const QRect rawRect = windowRect(layerWindow);
    const QRect convertedRect = DpiCoordinateHelper::nativeRectToQtGlobalRect(layerWindow, layerNativeRect);
    QScreen* screen = QGuiApplication::screenAt(rawRect.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return convertedRect.isValid() ? convertedRect : rawRect;
    }

    const QRect screenGeometry = screen->geometry();
    const int rawDelta = qAbs(rawRect.width() - screenGeometry.width()) +
        qAbs(rawRect.height() - screenGeometry.height());
    const int convertedDelta = qAbs(convertedRect.width() - screenGeometry.width()) +
        qAbs(convertedRect.height() - screenGeometry.height());
    return rawDelta <= convertedDelta ? rawRect : convertedRect;
}

#endif

QString sharedLogFilePath()
{
    return QDir(AppPaths::linWidgetLogsDir()).filePath(QStringLiteral("__shared__.log"));
}

QString safeStorageSegment(QString value)
{
    value = value.trimmed().toLower();
    QString safe;
    safe.reserve(value.size());
    for (const QChar ch : value) {
        if (ch.isLetterOrNumber() || ch == QLatin1Char('.') || ch == QLatin1Char('_') || ch == QLatin1Char('-')) {
            safe.append(ch);
        } else if (!safe.endsWith(QLatin1Char('_'))) {
            safe.append(QLatin1Char('_'));
        }
    }
    while (safe.startsWith(QLatin1Char('.')) || safe.startsWith(QLatin1Char('_')) || safe.startsWith(QLatin1Char('-'))) {
        safe.remove(0, 1);
    }
    while (safe.endsWith(QLatin1Char('.')) || safe.endsWith(QLatin1Char('_')) || safe.endsWith(QLatin1Char('-'))) {
        safe.chop(1);
    }
    return safe.isEmpty() ? QStringLiteral("unknown") : safe.left(128);
}

QString storageFilePathForInstance(const WidgetInstance& instance, const QString& scope)
{
    const QString mode = instance.isDevelopment ? QStringLiteral("dev") : QStringLiteral("installed");
    const QString registryId = safeStorageSegment(instance.manifest.id);
    QDir root(AppPaths::linWidgetDataDir());
    root.mkpath(mode + QLatin1Char('/') + registryId);
    if (scope == QStringLiteral("instance")) {
        root.mkpath(mode + QLatin1Char('/') + registryId + QStringLiteral("/instances"));
        return root.filePath(mode + QLatin1Char('/') + registryId + QStringLiteral("/instances/") + safeStorageSegment(instance.instanceId) + QStringLiteral(".json"));
    }
    return root.filePath(mode + QLatin1Char('/') + registryId + QStringLiteral("/widget.json"));
}

bool loadStorageObject(const QString& path, QJsonObject* object, QString* errorMessage)
{
    if (!object) {
        return false;
    }
    *object = QJsonObject();
    QFile file(path);
    if (!file.exists()) {
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Storage file is invalid.");
        }
        return false;
    }
    *object = document.object();
    return true;
}

bool saveStorageObject(const QString& path, const QJsonObject& object, QString* errorMessage)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    return true;
}

void appendSharedLog(const QString& level, const QString& message)
{
    const QString line = QStringLiteral("[%1] [%2] %3\n")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
             level,
             message);
    QFile file(sharedLogFilePath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        file.write(line.toUtf8());
    }
}

#ifdef LINDESK_SOURCE_DIR
bool copyDirRecursively(const QString& sourcePath, const QString& targetPath)
{
    QDir sourceDir(sourcePath);
    if (!sourceDir.exists()) {
        return false;
    }
    QDir targetDir(targetPath);
    if (!targetDir.exists()) {
        if (!targetDir.mkpath(QStringLiteral("."))) {
            return false;
        }
    }

    const QFileInfoList entries = sourceDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries, QDir::Name);
    for (const QFileInfo& entry : entries) {
        const QString name = entry.fileName();
        if (name == QStringLiteral(".git") ||
            name == QStringLiteral("node_modules") ||
            name.compare(QStringLiteral("cache"), Qt::CaseInsensitive) == 0 ||
            name.compare(QStringLiteral("tmp"), Qt::CaseInsensitive) == 0 ||
            name.compare(QStringLiteral("temp"), Qt::CaseInsensitive) == 0) {
            continue;
        }

        const QString targetEntryPath = targetDir.filePath(name);
        if (entry.isDir()) {
            if (!copyDirRecursively(entry.absoluteFilePath(), targetEntryPath)) {
                return false;
            }
        } else {
            if (QFile::exists(targetEntryPath)) {
                const QFileInfo targetInfo(targetEntryPath);
                if (entry.lastModified() <= targetInfo.lastModified() && entry.size() == targetInfo.size()) {
                    continue;
                }
                QFile::remove(targetEntryPath);
            }
            QFile::copy(entry.absoluteFilePath(), targetEntryPath);
        }
    }
    return true;
}

void ensureDevelopmentResources()
{
    const QString sourceDir = QString::fromUtf8(LINDESK_SOURCE_DIR) + QStringLiteral("/src/resources");
    const QString targetDir = QCoreApplication::applicationDirPath() + QStringLiteral("/resources");
    if (QDir(sourceDir).exists()) {
        copyDirRecursively(sourceDir, targetDir);
    }
}
#endif
}

static WidgetManager* s_widgetManagerInstance = nullptr;

WidgetManager* WidgetManager::instance()
{
    return s_widgetManagerInstance;
}

WidgetManager::WidgetManager(QObject* parent)
    : QObject(parent)
{
    s_widgetManagerInstance = this;
#ifdef LINDESK_SOURCE_DIR
    ensureDevelopmentResources();
#endif

    QSettings cefSettings(AppPaths::configFilePath(), QSettings::IniFormat);
    m_cefOsrEnabled = cefSettings.value("LinWidgets/CefOsrEnabled", true).toBool();

    m_runtimeHealthTimer.setInterval(kRuntimeHealthCheckIntervalMs);
    connect(&m_runtimeHealthTimer, &QTimer::timeout, this, &WidgetManager::checkRuntimeHealth);

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(350);
    connect(&m_saveTimer, &QTimer::timeout, this, &WidgetManager::saveToDefaultConfig);

    m_canvasWidget = new LinCef::LinWidgetCanvasWidget();
}

WidgetManager::~WidgetManager()
{
    if (s_widgetManagerInstance == this) {
        s_widgetManagerInstance = nullptr;
    }
    if (m_saveTimer.isActive()) {
        m_saveTimer.stop();
        saveToDefaultConfig();
    }
    qDeleteAll(m_systemInfoWatchTimers);
    m_systemInfoWatchTimers.clear();
    m_enabled = false;
    stopAll();
    stopWallpaper();
    if (m_canvasWidget) {
        delete m_canvasWidget;
        m_canvasWidget = nullptr;
    }
}

bool WidgetManager::start()
{
    if (m_enabled && !m_runtimeHealthTimer.isActive()) {
        m_runtimeHealthTimer.start();
    }
    return m_enabled;
}

bool WidgetManager::launchWidgetPackage(const QString& packagePath,
                                        const QRect& geometry,
                                        QString* instanceId,
                                        QString* errorMessage)
{
    return launchWidgetPackage(packagePath, geometry, false, false, instanceId, errorMessage);
}

bool WidgetManager::launchWidgetPackage(const QString& packagePath,
                                        const QRect& geometry,
                                        bool isDevelopment,
                                        bool autoReload,
                                        QString* instanceId,
                                        QString* errorMessage)
{
    if (!m_enabled) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("LinWidgets runtime is disabled.");
        }
        return false;
    }

    if (!start()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to start LinWidget runtime.");
        }
        return false;
    }

    WidgetManifest manifest;
    if (!WidgetPackage::loadManifest(packagePath, &manifest, errorMessage)) {
        return false;
    }

    if (manifest.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0) {
        if (instanceId) {
            *instanceId = QStringLiteral("wallpaper:") + manifest.id;
        }
        return startWallpaper(packagePath, isDevelopment, false, errorMessage);
    }

    const QString requestedPackageKey = canonicalPackagePath(packagePath);
    const QString requestedIdentityKey = widgetIdentityKey(manifest, packagePath);
    const QString requestedLayoutId = instanceLayoutId(manifest, isDevelopment);
    for (const WidgetLayoutState& state : m_layout.states()) {
        QString existingIdentityKey;
        WidgetManifest existingManifest;
        if (WidgetPackage::loadManifest(state.packagePath, &existingManifest, nullptr)) {
            existingIdentityKey = widgetIdentityKey(existingManifest, state.packagePath);
        }
        if (state.instanceId == requestedLayoutId ||
            (canonicalPackagePath(state.packagePath) == requestedPackageKey && state.instanceId.startsWith(isDevelopment ? QStringLiteral("dev:") : QStringLiteral("installed:"))) ||
            (!requestedIdentityKey.isEmpty() && existingIdentityKey == requestedIdentityKey && state.instanceId.startsWith(isDevelopment ? QStringLiteral("dev:") : QStringLiteral("installed:")))) {
            if (instanceId) {
                *instanceId = state.instanceId;
            }
            if (errorMessage) {
                errorMessage->clear();
            }
            WidgetLayoutState nextState = state;
            nextState.visible = true;
            if (!m_runningWidgets.contains(state.instanceId)) {
                WidgetInstance instance;
                instance.instanceId = state.instanceId;
                instance.packagePath = packagePath;
                instance.manifest = manifest;
                instance.grantedPermissions = grantedPermissionsForManifest(manifest, packagePath, isDevelopment);
                instance.isDevelopment = isDevelopment;
                instance.autoReload = autoReload;
                instance.geometry = resolvedWidgetGeometry(manifest, state.geometry, isDevelopment);
                nextState.geometry = instance.geometry;
                if (!launchWidgetInstance(instance, nextState, errorMessage)) {
                    return false;
                }
                scheduleSaveToConfig();
                return true;
            }
            m_layout.addOrUpdate(nextState);
            scheduleSaveToConfig();
            return true;
        }
    }

    WidgetInstance instance;
    instance.instanceId = requestedLayoutId;
    instance.packagePath = packagePath;
    instance.manifest = manifest;
    instance.grantedPermissions = grantedPermissionsForManifest(manifest, packagePath, isDevelopment);
    instance.isDevelopment = isDevelopment;
    instance.autoReload = autoReload;
    instance.geometry = resolvedWidgetGeometry(manifest, geometry, isDevelopment);

    WidgetLayoutState layoutState;
    layoutState.instanceId = instance.instanceId;
    layoutState.packagePath = packagePath;
    layoutState.registryId = registryIdForManifest(manifest);
    layoutState.geometry = instance.geometry;
    layoutState.zOrder = m_layout.nextZOrder();
    layoutState.visible = true;
    layoutState.isDevelopment = isDevelopment;
    layoutState.createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    if (!launchWidgetInstance(instance, layoutState, errorMessage)) {
        return false;
    }

    if (instanceId) {
        *instanceId = instance.instanceId;
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    scheduleSaveToConfig();
    emit widgetLaunched(instance.instanceId);
    return true;
}

bool WidgetManager::createWidgetInstance(const QString& sourceInstanceId,
                                         const QRect& requestedGeometry,
                                         QString* newInstanceId,
                                         QString* errorMessage)
{
    const WidgetInstance source = m_instances.value(sourceInstanceId);
    if (source.instanceId.isEmpty() || !m_runningWidgets.contains(sourceInstanceId)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Source widget instance is not running.");
        }
        return false;
    }
    if (!source.manifest.multiInstance) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("This widget does not support multiple instances.");
        }
        return false;
    }
    if (!PermissionManager().canUseDesktopApi(source.grantedPermissions)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Desktop permission is required.");
        }
        appendSharedLog(QStringLiteral("permission"),
                        QStringLiteral("%1 permission denied: desktop").arg(sourceInstanceId));
        return false;
    }

    QRect geometry = requestedGeometry;
    if (!geometry.isValid()) {
        const WidgetLayoutState sourceState = m_layout.state(sourceInstanceId);
        geometry = sourceState.geometry.isValid() ? sourceState.geometry : source.geometry;
        const QString registryId = registryIdForManifest(source.manifest);
        int siblingCount = 0;
        for (const WidgetLayoutState& state : m_layout.states()) {
            if (stateRegistryId(state) == registryId && stateIsDevelopment(state) == source.isDevelopment) {
                ++siblingCount;
            }
        }
        const int offset = qMax(1, siblingCount) * 30;
        geometry.translate(offset, offset);
    }
    return createWidgetInstanceFromPackageInternal(source.packagePath,
                                                  source.isDevelopment,
                                                  source.autoReload,
                                                  geometry,
                                                  sourceInstanceId,
                                                  newInstanceId,
                                                  errorMessage);
}

bool WidgetManager::createWidgetInstanceFromPackage(const QString& packagePath,
                                                    bool isDevelopment,
                                                    bool autoReload,
                                                    const QRect& requestedGeometry,
                                                    QString* newInstanceId,
                                                    QString* errorMessage)
{
    return createWidgetInstanceFromPackageInternal(packagePath,
                                                  isDevelopment,
                                                  autoReload,
                                                  requestedGeometry,
                                                  QString(),
                                                  newInstanceId,
                                                  errorMessage);
}

bool WidgetManager::createWidgetInstanceFromPackageInternal(const QString& packagePath,
                                                           bool isDevelopment,
                                                           bool autoReload,
                                                           const QRect& requestedGeometry,
                                                           const QString& parentInstanceId,
                                                           QString* newInstanceId,
                                                           QString* errorMessage)
{
    if (!m_enabled) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("LinWidgets runtime is disabled.");
        }
        return false;
    }
    if (!start()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to start LinWidget runtime.");
        }
        return false;
    }

    WidgetManifest manifest;
    if (!WidgetPackage::loadManifest(packagePath, &manifest, errorMessage)) {
        return false;
    }
    if (!manifest.multiInstance) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("This widget does not support multiple instances.");
        }
        return false;
    }

    const QString registryId = registryIdForManifest(manifest);
    int instanceCount = 0;
    for (const WidgetLayoutState& state : m_layout.states()) {
        if (stateRegistryId(state) == registryId && stateIsDevelopment(state) == isDevelopment) {
            ++instanceCount;
        }
    }
    if (instanceCount >= kMaxWidgetInstancesPerRegistry) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("This widget has reached the 3 instance limit.");
        }
        return false;
    }

    WidgetInstance instance;
    instance.instanceId = newMultiInstanceId(manifest, isDevelopment);
    instance.packagePath = packagePath;
    instance.manifest = manifest;
    instance.grantedPermissions = grantedPermissionsForManifest(manifest, packagePath, isDevelopment);
    instance.isDevelopment = isDevelopment;
    instance.autoReload = autoReload;
    QRect geometry = requestedGeometry.isValid()
        ? requestedGeometry
        : QRect(QPoint(260 + instanceCount * 24, 220 + instanceCount * 24), QSize(manifest.width, manifest.height));
    instance.geometry = screenClampedGeometry(resolvedWidgetGeometry(manifest, geometry, isDevelopment));

    WidgetLayoutState layoutState;
    layoutState.instanceId = instance.instanceId;
    layoutState.packagePath = packagePath;
    layoutState.registryId = registryId;
    layoutState.geometry = instance.geometry;
    layoutState.zOrder = m_layout.nextZOrder();
    layoutState.visible = true;
    layoutState.isDevelopment = isDevelopment;
    layoutState.createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    layoutState.parentInstanceId = parentInstanceId;

    if (!launchWidgetInstance(instance, layoutState, errorMessage)) {
        return false;
    }
    if (newInstanceId) {
        *newInstanceId = instance.instanceId;
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    scheduleSaveToConfig();
    emit widgetLaunched(instance.instanceId);
    return true;
}

bool WidgetManager::launchWidgetInstance(const WidgetInstance& instance,
                                         const WidgetLayoutState& layoutState,
                                         QString* errorMessage)
{
    m_layout.addOrUpdate(layoutState);
    m_instances.insert(instance.instanceId, instance);
    m_runningWidgets.insert(instance.instanceId);
    configureDevelopmentWatcher(instance);

    if (m_cefOsrEnabled && LinCef::CefManager::instance()->isCefAvailable() && LinCef::CefManager::instance()->initialize()) {
        auto* osr = new LinCef::CefOsrBrowserInstance(instance.instanceId, this);
        osr->setPackagePath(instance.packagePath);
        osr->setAllowNetwork(m_permissions.canUseNetwork(instance.grantedPermissions) &&
                             instance.manifest.permissions.contains(QStringLiteral("network")));

        connect(osr, &LinCef::CefOsrBrowserInstance::frameReady, this,
                [this](const QString& wid, const QRect& geom, const QImage& img, const QVector<QRect>& dirty) {
                    if (m_canvasWidget) {
                        m_canvasWidget->updateWidgetFrame(wid, geom, img, dirty);
                    }
                });
        connect(osr, &LinCef::CefOsrBrowserInstance::messageReceived, this,
                &WidgetManager::handleRuntimeMessage);
        connect(osr, &LinCef::CefOsrBrowserInstance::browserCrashed, this,
                &WidgetManager::handleBrowserCrashed);
        connect(osr, &LinCef::CefOsrBrowserInstance::imeCompositionRangeChanged, this,
                [this](const QString& wid, const QRect& cursorRect) {
                    if (wid == m_focusedInstanceId) {
                        if (cursorRect.isValid()) {
                            const QRect geom = m_layout.state(wid).geometry;
                            m_imeCursorRect = QRect(geom.topLeft() + cursorRect.topLeft(), cursorRect.size());
                        } else {
                            m_imeCursorRect = QRect();
                        }
                        emit imeCursorRectChanged(m_imeCursorRect);
                    }
                });

        const QString entryFile = instance.manifest.entry.isEmpty() ? QStringLiteral("index.html") : instance.manifest.entry;
        const QString entryUrl = QStringLiteral("linwidget://%1/%2").arg(instance.instanceId, entryFile);
        osr->create(entryUrl, layoutState.geometry);
        m_osrBrowsers.insert(instance.instanceId, osr);
        updateDefaultCefPage();
    } else {
        if (errorMessage) {
            *errorMessage = QStringLiteral("CEF OSR runtime is not available.");
        }
        return false;
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool WidgetManager::restartWidget(const QString& instanceId)
{
    if (!m_enabled) {
        return false;
    }

    m_crashTimestamps.remove(instanceId);
    m_crashTrippedWidgets.remove(instanceId);

    if (instanceId.startsWith(QStringLiteral("wallpaper:"))) {
        if (m_activeWallpaperId == instanceId && !m_activeWallpaperPackagePath.isEmpty()) {
            const QString pkgPath = m_activeWallpaperPackagePath;
            const bool isDev = m_activeWallpaperIsDevelopment;
            const bool interactive = m_activeWallpaperInteractive;
            return startWallpaper(pkgPath, isDev, interactive, nullptr);
        }
        return false;
    }

    WidgetLayoutState state = m_layout.state(instanceId);
    if (state.instanceId.isEmpty()) {
        return false;
    }

    WidgetInstance instance = m_instances.value(instanceId);
    bool wasRunning = !instance.instanceId.isEmpty() && m_runningWidgets.contains(instanceId);

    if (!wasRunning) {
        QString manifestError;
        if (!WidgetPackage::loadManifest(state.packagePath, &instance.manifest, &manifestError)) {
            emit widgetError(instanceId, QStringLiteral("Load blocked: %1").arg(manifestError));
            return false;
        }
        instance.instanceId = state.instanceId;
        instance.packagePath = state.packagePath;
        instance.isDevelopment = stateIsDevelopment(state);
        instance.autoReload = instance.isDevelopment;
    } else {
        QString manifestError;
        if (!WidgetPackage::loadManifest(instance.packagePath, &instance.manifest, &manifestError)) {
            emit widgetError(instanceId, QStringLiteral("Reload blocked: %1").arg(manifestError));
            return false;
        }
    }

    const QString regId = registryIdForManifest(instance.manifest);
    if (!regId.isEmpty()) {
        m_crashTimestamps.remove(regId);
        m_crashTrippedWidgets.remove(regId);
    }

    instance.geometry = state.geometry;
    instance.grantedPermissions = grantedPermissionsForManifest(instance.manifest, instance.packagePath, instance.isDevelopment);

    state.visible = true;

    if (!wasRunning) {
        QString launchError;
        if (!launchWidgetInstance(instance, state, &launchError)) {
            emit widgetError(instanceId, launchError);
            return false;
        }
        emit widgetLaunched(instanceId);
    } else {
        m_instances.insert(instanceId, instance);
        stopSystemInfoWatchesForInstance(instanceId);
        clearDevelopmentWatcher(instanceId);
        configureDevelopmentWatcher(instance);
        auto* osr = m_osrBrowsers.value(instanceId);
        if (osr) {
            osr->setAllowNetwork(m_permissions.canUseNetwork(instance.grantedPermissions) &&
                                 instance.manifest.permissions.contains(QStringLiteral("network")));
            const QString entryFile = instance.manifest.entry.isEmpty() ? QStringLiteral("index.html") : instance.manifest.entry;
            const QString entryUrl = QStringLiteral("linwidget://%1/%2").arg(instance.instanceId, entryFile);
            osr->loadUrl(entryUrl);
        }
    }
    scheduleSaveToConfig();
    return true;
}

bool WidgetManager::restoreFromConfig(const QString& configPath, QString* errorMessage)
{
    if (!m_enabled) {
        if (errorMessage) {
            errorMessage->clear();
        }
        return true;
    }

    if (!start()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to start LinWidget runtime.");
        }
        return false;
    }

    const bool wasStoppingAll = m_stoppingAll;
    m_stoppingAll = true;
    stopAll(false);
    m_stoppingAll = wasStoppingAll;

    QVector<WidgetLayoutState> states;
    const QString path = configPath.isEmpty() ? ConfigManager::defaultConfigPath() : configPath;
    if (!ConfigManager::loadLayout(path, &states, errorMessage)) {
        updateDefaultCefPage();
        return false;
    }

    if (configPath.isEmpty()) {
        auto& registry = WidgetRegistryManager::instance();
        QString registryError;
        if (registry.load(&registryError)) {
            QSet<QString> knownInstanceIds;
            for (const WidgetLayoutState& state : states) {
                knownInstanceIds.insert(state.instanceId);
            }
            for (const WidgetRegistryEntry& entry : registry.entries()) {
                if (!entry.enabled || !entry.autoLoad || !QFileInfo::exists(entry.packagePath)) {
                    continue;
                }
                WidgetManifest manifest;
                if (!WidgetPackage::loadManifest(entry.packagePath, &manifest, nullptr)) {
                    continue;
                }
                if (manifest.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0) {
                    continue;
                }
                const QString instanceId = instanceLayoutId(manifest, entry.isDevelopment);
                if (knownInstanceIds.contains(instanceId)) {
                    continue;
                }
                WidgetLayoutState state;
                state.instanceId = instanceId;
                state.packagePath = entry.packagePath;
                state.registryId = registryIdForManifest(manifest);
                state.geometry = QRect(QPoint(260, 220), QSize(manifest.width, manifest.height));
                state.zOrder = m_layout.nextZOrder();
                state.visible = true;
                state.isDevelopment = entry.isDevelopment;
                state.createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
                states.append(state);
                knownInstanceIds.insert(instanceId);
            }
        } else if (!registryError.isEmpty()) {
            emit widgetError(QString(), registryError);
        }
    }

    bool ok = true;
    const QVector<WidgetLayoutState> uniqueStates = deduplicatedLayoutStates(states);
    m_layout.setStates(QVector<WidgetLayoutState>());
    for (const WidgetLayoutState& state : uniqueStates) {
        if (!state.visible) {
            m_layout.addOrUpdate(state);
            continue;
        }

        WidgetManifest manifest;
        QString launchError;
        if (!WidgetPackage::loadManifest(state.packagePath, &manifest, &launchError)) {
            ok = false;
            emit widgetError(state.instanceId, launchError);
            continue;
        }

        if (manifest.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0) {
            continue;
        }

        WidgetInstance instance;
        instance.instanceId = state.instanceId;
        instance.packagePath = state.packagePath;
        instance.manifest = manifest;
        instance.isDevelopment = stateIsDevelopment(state);
        instance.autoReload = instance.isDevelopment;
        instance.grantedPermissions = grantedPermissionsForManifest(manifest, state.packagePath, instance.isDevelopment);
        instance.geometry = resolvedWidgetGeometry(manifest, state.geometry, instance.isDevelopment);
        if (instance.instanceId.isEmpty()) {
            instance.instanceId = makeInstanceId(manifest);
        }

        WidgetLayoutState launchState = state;
        launchState.registryId = state.registryId.isEmpty() ? registryIdForManifest(manifest) : state.registryId;
        launchState.isDevelopment = instance.isDevelopment;
        if (launchState.createdAt.isEmpty()) {
            launchState.createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        }
        launchState.geometry = instance.geometry;
        if (!launchWidgetInstance(instance, launchState, &launchError)) {
            ok = false;
            emit widgetError(instance.instanceId, launchError);
            continue;
        }
        emit widgetLaunched(instance.instanceId);
    }

    updateDefaultCefPage();

    if (errorMessage && ok) {
        errorMessage->clear();
    }
    if (configPath.isEmpty()) {
        loadWallpaperConfig();
        scheduleSaveToConfig();
    }
    return ok;
}

QVector<WidgetLayoutState> WidgetManager::filterWallpapers(const QVector<WidgetLayoutState>& states) const
{
    QVector<WidgetLayoutState> filtered;
    for (const WidgetLayoutState& state : states) {
        WidgetInstance inst = m_instances.value(state.instanceId);
        if (inst.instanceId.isEmpty()) {
            WidgetManifest manifest;
            if (WidgetPackage::loadManifest(state.packagePath, &manifest)) {
                if (manifest.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0) {
                    continue;
                }
            }
        } else {
            if (inst.manifest.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0) {
                continue;
            }
        }
        filtered.append(state);
    }
    return filtered;
}

bool WidgetManager::saveToConfig(const QString& configPath, QString* errorMessage) const
{
    const QString path = configPath.isEmpty() ? ConfigManager::defaultConfigPath() : configPath;
    return ConfigManager::saveLayout(path, deduplicatedLayoutStates(filterWallpapers(m_layout.states())), errorMessage);
}

QVector<WidgetLayoutState> WidgetManager::layoutStates() const
{
    QVector<WidgetLayoutState> states = deduplicatedLayoutStates(filterWallpapers(m_layout.states()));
    if (!m_enabled) {
        for (WidgetLayoutState& state : states) {
            state.visible = false;
        }
    }
    return states;
}

bool WidgetManager::hasRunningWidget(const QString& instanceId) const
{
    return m_runningWidgets.contains(instanceId);
}

QRect WidgetManager::clampGeometryToHostBounds(const QRect& geometry) const
{
    QRect bounds;
    if (LinCef::LinWidgetCanvasWidget::hostWidget()) {
        const QRect hostRect = LinCef::LinWidgetCanvasWidget::hostWidget()->rect();
        if (hostRect.isValid() && hostRect.width() > 100 && hostRect.height() > 100) {
            bounds = hostRect;
        }
    }
    if (!bounds.isValid() && m_canvasWidget) {
        const QRect canvasRect = m_canvasWidget->rect();
        if (canvasRect.isValid() && canvasRect.width() > 100 && canvasRect.height() > 100) {
            bounds = canvasRect;
        }
    }
    if (!bounds.isValid()) {
        return screenClampedGeometry(geometry);
    }

    QRect next = geometry;
    if (next.width() > bounds.width()) {
        next.setWidth(bounds.width());
    }
    if (next.height() > bounds.height()) {
        next.setHeight(bounds.height());
    }
    if (next.right() > bounds.right()) {
        next.moveRight(bounds.right());
    }
    if (next.bottom() > bounds.bottom()) {
        next.moveBottom(bounds.bottom());
    }
    if (next.left() < bounds.left()) {
        next.moveLeft(bounds.left());
    }
    if (next.top() < bounds.top()) {
        next.moveTop(bounds.top());
    }
    return next;
}

QRect WidgetManager::widgetGeometry(const QString& instanceId) const
{
    if (instanceId.isEmpty()) {
        return QRect();
    }
    if (m_layout.contains(instanceId)) {
        const WidgetLayoutState state = m_layout.state(instanceId);
        if (state.geometry.isValid()) {
            return state.geometry;
        }
    }
    if (m_instances.contains(instanceId)) {
        const WidgetInstance inst = m_instances.value(instanceId);
        if (inst.geometry.isValid()) {
            return inst.geometry;
        }
    }
    if (m_canvasWidget) {
        return m_canvasWidget->widgetGeometry(instanceId);
    }
    return QRect();
}

bool WidgetManager::setWidgetGeometry(const QString& instanceId, const QRect& geometry)
{
    if (!m_enabled || instanceId.isEmpty() || !geometry.isValid()) {
        return false;
    }
    if (instanceId.startsWith(QStringLiteral("wallpaper:")) ||
        instanceId == QStringLiteral("__default_page__")) {
        qDebug() << "[LinWidget][WidgetManager] Rejecting setWidgetGeometry on non-draggable target:" << instanceId;
        return false;
    }

    WidgetLayoutState state = m_layout.state(instanceId);
    if (state.instanceId.isEmpty()) {
        return false;
    }

    WidgetManifest manifest;
    QString manifestError;
    bool hasManifest = WidgetPackage::loadManifest(state.packagePath, &manifest, &manifestError);
    if (!hasManifest && m_instances.contains(instanceId)) {
        // Fall back to the manifest cached at launch time so that a manifest reload failure
        // does not silently freeze the widget size.
        manifest = m_instances.value(instanceId).manifest;
        hasManifest = !manifest.id.isEmpty();
        qWarning() << "[LinWidget][WidgetManager] setWidgetGeometry: manifest reload failed for" << instanceId
                   << "package:" << state.packagePath << "reason:" << manifestError
                   << "- using cached manifest:" << hasManifest;
    }
    if (hasManifest && manifest.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0) {
        qDebug() << "[LinWidget][WidgetManager] Rejecting setWidgetGeometry on wallpaper manifest:" << instanceId;
        return false;
    }
    if (m_instances.contains(instanceId) &&
        m_instances.value(instanceId).manifest.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0) {
        return false;
    }

    QRect nextGeometry = geometry;
    if (hasManifest && nextGeometry.size() != state.geometry.size()) {
        const QSize clamped = clampedWidgetSize(manifest, nextGeometry.size());
        const bool rightAnchored = (geometry.right() == state.geometry.right() && geometry.left() != state.geometry.left());
        const bool bottomAnchored = (geometry.bottom() == state.geometry.bottom() && geometry.top() != state.geometry.top());
        if (rightAnchored) {
            nextGeometry.setLeft(geometry.right() - clamped.width() + 1);
            nextGeometry.setRight(geometry.right());
        } else {
            nextGeometry.setWidth(clamped.width());
        }
        if (bottomAnchored) {
            nextGeometry.setTop(geometry.bottom() - clamped.height() + 1);
            nextGeometry.setBottom(geometry.bottom());
        } else {
            nextGeometry.setHeight(clamped.height());
        }
    } else if (!hasManifest && nextGeometry.size() != state.geometry.size()) {
        nextGeometry.setSize(state.geometry.size());
    }
    nextGeometry = clampGeometryToHostBounds(nextGeometry);

    state.geometry = nextGeometry;
    m_layout.setGeometry(instanceId, state.geometry);
    if (m_instances.contains(instanceId)) {
        WidgetInstance instance = m_instances.value(instanceId);
        instance.geometry = state.geometry;
        m_instances.insert(instanceId, instance);
    }
    if (auto* osr = m_osrBrowsers.value(instanceId)) {
        osr->setGeometry(state.geometry);
    }
    if (m_canvasWidget) {
        m_canvasWidget->updateWidgetGeometry(instanceId, state.geometry);
    }
    scheduleSaveToConfig();
    return true;
}

bool WidgetManager::moveWidget(const QString& instanceId, const QPoint& pos)
{
    if (!m_enabled || instanceId.isEmpty()) {
        return false;
    }
    if (instanceId.startsWith(QStringLiteral("wallpaper:")) ||
        instanceId == QStringLiteral("__default_page__")) {
        qDebug() << "[LinWidget][WidgetManager] Rejecting moveWidget on non-draggable target:" << instanceId;
        return false;
    }

    WidgetLayoutState state = m_layout.state(instanceId);
    if (state.instanceId.isEmpty()) {
        return false;
    }
    if (m_instances.contains(instanceId) &&
        m_instances.value(instanceId).manifest.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0) {
        return false;
    }

    QRect nextRect = state.geometry;
    nextRect.moveTopLeft(pos);
    return setWidgetGeometry(instanceId, nextRect);
}

bool WidgetManager::resizeWidget(const QString& instanceId, const QSize& size)
{
    if (!m_enabled || instanceId.isEmpty() || !size.isValid()) {
        return false;
    }
    if (instanceId.startsWith(QStringLiteral("wallpaper:")) ||
        instanceId == QStringLiteral("__default_page__")) {
        return false;
    }

    WidgetLayoutState state = m_layout.state(instanceId);
    if (state.instanceId.isEmpty()) {
        return false;
    }
    WidgetManifest manifest;
    QString manifestError;
    if (!WidgetPackage::loadManifest(state.packagePath, &manifest, &manifestError)) {
        if (!m_instances.contains(instanceId)) {
            qWarning() << "[LinWidget][WidgetManager] resizeWidget failed: manifest unavailable for" << instanceId
                       << "package:" << state.packagePath << "reason:" << manifestError;
            return false;
        }
        manifest = m_instances.value(instanceId).manifest;
        qWarning() << "[LinWidget][WidgetManager] resizeWidget: manifest reload failed for" << instanceId
                   << "reason:" << manifestError << "- using cached manifest";
    }
    if (manifest.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0) {
        return false;
    }
    QRect nextRect = state.geometry;
    nextRect.setSize(clampedWidgetSize(manifest, size));
    return setWidgetGeometry(instanceId, nextRect);
}

bool WidgetManager::removeWidgetState(const QString& instanceId)
{
    if (instanceId.isEmpty()) {
        return false;
    }
    if (m_runningWidgets.contains(instanceId)) {
        stopWidget(instanceId);
    }
    const bool hadState = m_layout.contains(instanceId);
    m_layout.remove(instanceId);
    if (hadState) {
        saveToDefaultConfig();
    }
    return hadState;
}

void WidgetManager::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }

    if (!enabled && m_saveTimer.isActive()) {
        m_saveTimer.stop();
        saveToDefaultConfig();
    }

    m_enabled = enabled;
    if (!m_enabled) {
        if (m_runtimeHealthTimer.isActive()) {
            m_runtimeHealthTimer.stop();
        }
        stopAll();
    } else {
        if (!m_runtimeHealthTimer.isActive()) {
            m_runtimeHealthTimer.start();
        }
    }
}

bool WidgetManager::isEnabled() const
{
    return m_enabled;
}

void WidgetManager::setHostParentWindow(WId parentWindow)
{
    m_hostParentWindow = parentWindow;
}

WId WidgetManager::hostParentWindow() const
{
    return m_hostParentWindow;
}

void WidgetManager::setDesktopLayerWindow(WId layerWindow)
{
    if (m_desktopLayerWindow == layerWindow) {
        return;
    }
    m_desktopLayerWindow = layerWindow;

    QWidget* lv = QWidget::find(layerWindow);
    if (lv) {
        m_linDesktopParent = lv->parentWidget();
    }

    if (m_enabled && m_activeWallpaperId.isEmpty()) {
        loadWallpaperConfig();
    }
    syncAllWidgetWindowLayers();
}

WId WidgetManager::desktopLayerWindow() const
{
    return m_desktopLayerWindow;
}

void WidgetManager::setShowWidgetBounds(bool enabled)
{
    if (m_showWidgetBounds == enabled) {
        return;
    }
    m_showWidgetBounds = enabled;
    if (m_canvasWidget) {
        m_canvasWidget->setShowBounds(enabled);
    }
}

bool WidgetManager::showWidgetBounds() const
{
    return m_showWidgetBounds;
}

void WidgetManager::setCanvasWidget(::LinCef::LinWidgetCanvasWidget* canvas)
{
    m_canvasWidget = canvas;
    if (m_canvasWidget) {
        m_canvasWidget->setShowBounds(m_showWidgetBounds);
        updateDefaultCefPage();
    }
}

void WidgetManager::setCefOsrEnabled(bool enabled)
{
    m_cefOsrEnabled = enabled;

    QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
    settings.setValue("LinWidgets/CefOsrEnabled", enabled);
    if (enabled && !m_enabled) {
        settings.setValue("LinWidgets/Enabled", true);
    }
    settings.sync();

    if (enabled && !m_enabled) {
        setEnabled(true);
    }

    if (!m_enabled) {
        return;
    }

    if (enabled) {
        if (m_activeWallpaperId.isEmpty()) {
            loadWallpaperConfig();
        }

        // 启动 CEF OSR 浏览器
        if (LinCef::CefManager::instance()->isCefAvailable() && LinCef::CefManager::instance()->initialize()) {
            for (const QString& id : m_runningWidgets) {
                if (m_osrBrowsers.contains(id)) continue;
                const WidgetInstance inst = m_instances.value(id);
                const WidgetLayoutState state = m_layout.state(id);
                auto* osr = new LinCef::CefOsrBrowserInstance(id, this);
                osr->setPackagePath(inst.packagePath);
                osr->setAllowNetwork(inst.manifest.permissions.contains(QStringLiteral("network")));

                connect(osr, &LinCef::CefOsrBrowserInstance::frameReady, this,
                        [this](const QString& wid, const QRect& geom, const QImage& img, const QVector<QRect>& dirty) {
                            if (m_canvasWidget) {
                                m_canvasWidget->updateWidgetFrame(wid, geom, img, dirty);
                            }
                        });
                connect(osr, &LinCef::CefOsrBrowserInstance::messageReceived, this,
                        &WidgetManager::handleRuntimeMessage);
                connect(osr, &LinCef::CefOsrBrowserInstance::browserCrashed, this,
                        &WidgetManager::handleBrowserCrashed);
                connect(osr, &LinCef::CefOsrBrowserInstance::imeCompositionRangeChanged, this,
                        [this](const QString& wid, const QRect& cursorRect) {
                            if (wid == m_focusedInstanceId) {
                                if (cursorRect.isValid()) {
                                    const QRect geom = m_layout.state(wid).geometry;
                                    m_imeCursorRect = QRect(geom.topLeft() + cursorRect.topLeft(), cursorRect.size());
                                } else {
                                    m_imeCursorRect = QRect();
                                }
                                emit imeCursorRectChanged(m_imeCursorRect);
                            }
                        });

                const QString entryFile = inst.manifest.entry.isEmpty() ? QStringLiteral("index.html") : inst.manifest.entry;
                const QString entryUrl = QStringLiteral("linwidget://%1/%2").arg(id, entryFile);
                osr->create(entryUrl, state.geometry);
                m_osrBrowsers.insert(id, osr);
            }
            updateDefaultCefPage();
        }
    } else {
        // 禁用并卸载全部 CEF OSR 浏览器
        for (auto* osr : m_osrBrowsers) {
            if (osr) {
                osr->close();
                osr->deleteLater();
            }
        }
        m_osrBrowsers.clear();
        updateDefaultCefPage();

        if (m_canvasWidget) {
            m_canvasWidget->clearAllFrames();
        }

        // 停止当前正在播放的壁纸，但保留配置以便恢复
        stopWallpaper(false);
    }
}

bool WidgetManager::isWidgetAt(const QPoint& clientPos) const
{
    return !widgetAt(clientPos).isEmpty();
}

QString WidgetManager::widgetAt(const QPoint& clientPos) const
{
    if (!m_enabled || !m_cefOsrEnabled) {
        return QString();
    }

    if (m_canvasWidget) {
        const QString canvasHit = m_canvasWidget->widgetAt(clientPos);
        if (!canvasHit.isEmpty()) {
            return canvasHit;
        }
    }
    for (const WidgetInstance& instance : m_instances) {
        if (!m_runningWidgets.contains(instance.instanceId)) {
            continue;
        }
        // 壁纸属于底层全屏背景，不作为局部小组件返回，由专属壁纸管道处理
        if (instance.instanceId.startsWith(QStringLiteral("wallpaper:"))) {
            continue;
        }
        if (m_layout.contains(instance.instanceId)) {
            WidgetLayoutState state = m_layout.state(instance.instanceId);
            if (!state.visible) {
                continue;
            }
        }
        if (instance.geometry.contains(clientPos)) {
            return instance.instanceId;
        }
    }
    return QString();
}

bool WidgetManager::forwardMouseEvent(QMouseEvent* event)
{
    if (!event) {
        return false;
    }
    const QString wid = widgetAt(event->pos());
    if (wid.isEmpty()) {
        return false;
    }

    auto* osr = m_osrBrowsers.value(wid, nullptr);
    if (!osr && wid == QStringLiteral("__default_page__")) {
        osr = m_defaultOsrBrowser;
    }

    if (osr) {
        const QPoint localPos = event->pos() - osr->geometry().topLeft();
        if (event->type() == QEvent::MouseMove) {
            osr->sendMouseMove(localPos.x(), localPos.y());
        } else if (event->type() == QEvent::MouseButtonPress) {
            if (wid != QStringLiteral("__default_page__")) {
                setFocusedWidget(wid);
            } else {
                clearWidgetFocus();
            }
            osr->sendMouseClick(localPos.x(), localPos.y(), event->button(), false, 1);
        } else if (event->type() == QEvent::MouseButtonRelease) {
            osr->sendMouseClick(localPos.x(), localPos.y(), event->button(), true, 1);
        } else if (event->type() == QEvent::MouseButtonDblClick) {
            osr->sendMouseClick(localPos.x(), localPos.y(), event->button(), false, 2);
        }
        return true;
    }
    return false;
}

bool WidgetManager::forwardWheelEvent(QWheelEvent* event)
{
    if (!event) {
        return false;
    }
    const QString wid = widgetAt(event->pos());
    if (wid.isEmpty()) {
        return false;
    }

    auto* osr = m_osrBrowsers.value(wid, nullptr);
    if (!osr && wid == QStringLiteral("__default_page__")) {
        osr = m_defaultOsrBrowser;
    }

    if (osr) {
        const QPoint localPos = event->pos() - osr->geometry().topLeft();
        const QPoint angleDelta = event->angleDelta();
        osr->sendMouseWheel(localPos.x(), localPos.y(), angleDelta.x(), angleDelta.y());
        return true;
    }
    return false;
}

bool WidgetManager::forwardMouseMoveToWallpaper(const QPoint& clientPos)
{
    if (!m_enabled || !m_cefOsrEnabled || m_activeWallpaperId.isEmpty() || !m_activeWallpaperInteractive) {
        return false;
    }
    auto* osr = m_osrBrowsers.value(m_activeWallpaperId, nullptr);
    if (!osr) {
        return false;
    }

    // 60fps 频率限制 (16ms 间隔)，平滑广播光标位置，避免高频 IPC 洪峰
    static qint64 s_lastMoveMs = 0;
    static QPoint s_lastPos;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (clientPos == s_lastPos || (now - s_lastMoveMs < 16 && (clientPos - s_lastPos).manhattanLength() < 4)) {
        return true;
    }
    s_lastMoveMs = now;
    s_lastPos = clientPos;

    const QPoint localPos = clientPos - osr->geometry().topLeft();
    osr->sendMouseMove(localPos.x(), localPos.y());
    return true;
}

bool WidgetManager::forwardMouseClickToWallpaper(const QPoint& clientPos, Qt::MouseButton button, bool isRelease, int clickCount)
{
    if (!m_enabled || !m_cefOsrEnabled || m_activeWallpaperId.isEmpty() || !m_activeWallpaperInteractive) {
        return false;
    }
    auto* osr = m_osrBrowsers.value(m_activeWallpaperId, nullptr);
    if (!osr) {
        return false;
    }

    const QPoint localPos = clientPos - osr->geometry().topLeft();
    osr->sendMouseClick(localPos.x(), localPos.y(), button, isRelease, clickCount);
    return true;
}

bool WidgetManager::forwardWheelToWallpaper(const QPoint& clientPos, const QPoint& angleDelta)
{
    if (!m_enabled || !m_cefOsrEnabled || m_activeWallpaperId.isEmpty() || !m_activeWallpaperInteractive) {
        return false;
    }
    auto* osr = m_osrBrowsers.value(m_activeWallpaperId, nullptr);
    if (!osr) {
        return false;
    }

    const QPoint localPos = clientPos - osr->geometry().topLeft();
    osr->sendMouseWheel(localPos.x(), localPos.y(), angleDelta.x(), angleDelta.y());
    return true;
}

void WidgetManager::setFocusedWidget(const QString& instanceId)
{
    if (instanceId == QStringLiteral("__default_page__")) {
        if (!m_focusedInstanceId.isEmpty()) {
            clearWidgetFocus();
        }
        return;
    }
    if (m_focusedInstanceId == instanceId) {
        return;
    }
    const QString oldId = m_focusedInstanceId;
    m_focusedInstanceId = instanceId;

    if (!oldId.isEmpty()) {
        if (auto* oldBrowser = m_osrBrowsers.value(oldId, nullptr)) {
            oldBrowser->setFocus(false);
        } else if (oldId == QStringLiteral("__default_page__") && m_defaultOsrBrowser) {
            m_defaultOsrBrowser->setFocus(false);
        }
    }
    if (!m_focusedInstanceId.isEmpty()) {
        if (auto* newBrowser = m_osrBrowsers.value(m_focusedInstanceId, nullptr)) {
            newBrowser->setFocus(true);
        } else if (m_focusedInstanceId == QStringLiteral("__default_page__") && m_defaultOsrBrowser) {
            m_defaultOsrBrowser->setFocus(true);
        }
    }
    emit focusedWidgetChanged(m_focusedInstanceId);
}

void WidgetManager::clearWidgetFocus()
{
    setFocusedWidget(QString());
}

bool WidgetManager::forwardKeyEvent(QKeyEvent* event)
{
    if (!event || m_focusedInstanceId.isEmpty() || m_focusedInstanceId == QStringLiteral("__default_page__")) {
        return false;
    }
    auto* osr = m_osrBrowsers.value(m_focusedInstanceId, nullptr);
    if (!osr && m_focusedInstanceId == QStringLiteral("__default_page__")) {
        osr = m_defaultOsrBrowser;
    }
    if (osr) {
        osr->sendKeyEvent(event);
        return true;
    }
    return false;
}

bool WidgetManager::forwardInputMethodEvent(QInputMethodEvent* event)
{
    if (!event || m_focusedInstanceId.isEmpty()) {
        return false;
    }
    auto* osr = m_osrBrowsers.value(m_focusedInstanceId, nullptr);
    if (!osr && m_focusedInstanceId == QStringLiteral("__default_page__")) {
        osr = m_defaultOsrBrowser;
    }
    if (osr) {
        osr->sendInputMethodEvent(event);
        return true;
    }
    return false;
}

void WidgetManager::handleBrowserCrashed(const QString& widgetId, int status, int errorCode, const QString& errorString)
{
    qWarning() << "[LinWidget][WidgetManager] Widget render process crashed:" << widgetId
               << "status:" << status << "code:" << errorCode << "reason:" << errorString;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QVector<qint64>& timestamps = m_crashTimestamps[widgetId];
    timestamps.erase(std::remove_if(timestamps.begin(), timestamps.end(), [now](qint64 t) {
        return (now - t) > 60000;
    }), timestamps.end());
    timestamps.append(now);

    m_totalCrashCounts[widgetId] = m_totalCrashCounts.value(widgetId, 0) + 1;
    QString regId;
    if (m_instances.contains(widgetId)) {
        regId = registryIdForManifest(m_instances.value(widgetId).manifest);
        if (!regId.isEmpty() && regId != widgetId) {
            m_totalCrashCounts[regId] = m_totalCrashCounts.value(regId, 0) + 1;
        }
    }

    if (m_focusedInstanceId == widgetId) {
        clearWidgetFocus();
    }

    if (timestamps.size() <= 3) {
        qInfo() << "[LinWidget][WidgetManager] Auto-recovering crashed widget:" << widgetId
                << "(attempt" << timestamps.size() << "of 3 in last 60s)";
        QTimer::singleShot(500, this, [this, widgetId]() {
            if (m_runningWidgets.contains(widgetId)) {
                restartWidget(widgetId);
            }
        });
    } else {
        qCritical() << "[LinWidget][WidgetManager] Widget exceeded crash limit (3 in 60s), stopping:" << widgetId;
        m_crashTrippedWidgets.insert(widgetId);
        if (!regId.isEmpty()) {
            m_crashTrippedWidgets.insert(regId);
        }
        stopWidget(widgetId);
        emit widgetError(widgetId, QStringLiteral("Widget crashed repeatedly and was stopped."));
    }
}

QString WidgetManager::defaultCefPagePath() const
{
    const QString relativePath = QStringLiteral("resources/LinWidgetRuntime/default_page.html");
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString entryPath = QDir::cleanPath(QDir(appDir).filePath(relativePath));
    if (QFileInfo::exists(entryPath)) {
        return entryPath;
    }
#ifdef LINDESK_SOURCE_DIR
    const QString srcPath = QDir::cleanPath(QDir(QStringLiteral(LINDESK_SOURCE_DIR)).filePath(QStringLiteral("src/") + relativePath));
    if (QFileInfo::exists(srcPath)) {
        return srcPath;
    }
#endif
    return entryPath;
}

void WidgetManager::updateDefaultCefPage()
{
    qDebug() << "[LinWidget][WidgetManager] updateDefaultCefPage called. enabled=" << m_enabled
             << "cefOsrEnabled=" << m_cefOsrEnabled
             << "runningWidgetsCount=" << m_runningWidgets.size()
             << "osrBrowsersCount=" << m_osrBrowsers.size();

    if (!m_cefOsrEnabled || !m_enabled) {
        if (m_defaultOsrBrowser) {
            qDebug() << "[LinWidget][WidgetManager] Closing default OSR browser...";
            m_defaultOsrBrowser->close();
            m_defaultOsrBrowser->deleteLater();
            m_defaultOsrBrowser = nullptr;
        }
        if (m_canvasWidget) {
            m_canvasWidget->removeWidgetFrame(QStringLiteral("__default_page__"));
        }
        return;
    }

    // 检查是否有真实用户小组件正在运行
    const bool hasUserWidgets = !m_osrBrowsers.isEmpty();
    if (hasUserWidgets) {
        if (m_defaultOsrBrowser) {
            qDebug() << "[LinWidget][WidgetManager] User widget running, removing default page.";
            m_defaultOsrBrowser->close();
            m_defaultOsrBrowser->deleteLater();
            m_defaultOsrBrowser = nullptr;
            if (m_canvasWidget) {
                m_canvasWidget->removeWidgetFrame(QStringLiteral("__default_page__"));
            }
        }
        return;
    }

    // 无任何组件：启动并加载默认居中页面
    if (!LinCef::CefManager::instance()->isCefAvailable()) {
        qWarning() << "[LinWidget][WidgetManager] CEF is not available!";
        return;
    }
    if (!LinCef::CefManager::instance()->initialize()) {
        qWarning() << "[LinWidget][WidgetManager] Failed to initialize CEF!";
        return;
    }

    QRect canvasRect;
    if (LinCef::LinWidgetCanvasWidget::hostWidget() && LinCef::LinWidgetCanvasWidget::hostWidget()->rect().isValid() && LinCef::LinWidgetCanvasWidget::hostWidget()->rect().width() > 100) {
        canvasRect = LinCef::LinWidgetCanvasWidget::hostWidget()->rect();
    } else if (m_canvasWidget && m_canvasWidget->rect().isValid() && m_canvasWidget->rect().width() > 100) {
        canvasRect = m_canvasWidget->rect();
    } else if (QGuiApplication::primaryScreen()) {
        canvasRect = QGuiApplication::primaryScreen()->geometry();
        canvasRect.moveTo(0, 0);
    } else {
        canvasRect = QRect(0, 0, 1920, 1080);
    }

    if (m_defaultOsrBrowser) {
        if (m_defaultOsrBrowser->geometry() != canvasRect) {
            m_defaultOsrBrowser->setGeometry(canvasRect);
            if (m_canvasWidget) {
                m_canvasWidget->updateWidgetGeometry(QStringLiteral("__default_page__"), canvasRect);
            }
        }
        return;
    }

    const QString defaultId = QStringLiteral("__default_page__");
    m_defaultOsrBrowser = new LinCef::CefOsrBrowserInstance(defaultId, this);
    connect(m_defaultOsrBrowser, &LinCef::CefOsrBrowserInstance::frameReady, this,
            [this](const QString& wid, const QRect& geom, const QImage& img, const QVector<QRect>& dirty) {
                if (m_canvasWidget) {
                    m_canvasWidget->updateWidgetFrame(wid, geom, img, dirty);
                }
            });

    const QString path = defaultCefPagePath();
    const bool exists = QFileInfo::exists(path);
    const QString url = QUrl::fromLocalFile(path).toString();
    qDebug() << "[LinWidget][WidgetManager] Loading default page. path=" << path << "exists=" << exists << "url=" << url << "geom=" << canvasRect;
    m_defaultOsrBrowser->create(url, canvasRect);
}

void WidgetManager::setWidgetLayerEmbedded(bool embedded)
{
    if (m_widgetLayerEmbedded != embedded) {
        m_widgetLayerEmbedded = embedded;
        syncAllWidgetWindowLayers();
    }
}

bool WidgetManager::isWidgetLayerEmbedded() const
{
    return m_widgetLayerEmbedded;
}

void WidgetManager::syncWidgetWindowLayer(const QString& instanceId)
{
    Q_UNUSED(instanceId);
}

void WidgetManager::syncAllWidgetWindowLayers()
{
    syncWallpaperWindowLayer();
    if (m_cefOsrEnabled && m_defaultOsrBrowser) {
        updateDefaultCefPage();
    }
}

QString WidgetManager::resolveWidgetId(const QString& rawId) const
{
    QString target = rawId.trimmed();
    if (target.isEmpty()) {
        return QString();
    }
    if (m_instances.contains(target)) {
        return registryIdForManifest(m_instances.value(target).manifest);
    }
    if (m_layout.contains(target)) {
        return stateRegistryId(m_layout.state(target));
    }
    if (target.startsWith(QStringLiteral("installed:")) ||
        target.startsWith(QStringLiteral("dev:")) ||
        target.startsWith(QStringLiteral("wallpaper:"))) {
        const QStringList parts = target.split(QLatin1Char(':'));
        if (parts.size() >= 2) {
            return parts.at(1).trimmed().toLower();
        }
    }
    return target.toLower();
}

void WidgetManager::stopWidget(const QString& instanceId)
{
    if (instanceId.startsWith(QStringLiteral("wallpaper:"))) {
        stopWallpaper(false);
        emit widgetStopped(instanceId);
        return;
    }

    if (m_canvasWidget) {
        m_canvasWidget->removeWidgetFrame(instanceId);
    }

    if (!m_runningWidgets.contains(instanceId)) {
        const QString resolvedId = resolveWidgetId(instanceId);
        QStringList toStop;
        for (const QString& runningId : m_runningWidgets) {
            if (runningId == instanceId ||
                runningId.startsWith(instanceId + QLatin1Char(':')) ||
                (!resolvedId.isEmpty() && (runningId == resolvedId || runningId.contains(QLatin1Char(':') + resolvedId)))) {
                toStop.append(runningId);
            }
        }
        for (const WidgetLayoutState& state : m_layout.states()) {
            if (state.instanceId == instanceId ||
                (!resolvedId.isEmpty() && stateRegistryId(state) == resolvedId)) {
                if (m_runningWidgets.contains(state.instanceId) && !toStop.contains(state.instanceId)) {
                    toStop.append(state.instanceId);
                }
            }
        }
        if (!toStop.isEmpty()) {
            for (const QString& id : toStop) {
                stopWidget(id);
            }
            return;
        }

        if (m_layout.contains(instanceId)) {
            m_layout.setVisible(instanceId, false);
            saveToDefaultConfig();
        }
        return;
    }

    if (m_focusedInstanceId == instanceId) {
        clearWidgetFocus();
    }
    if (auto* osr = m_osrBrowsers.take(instanceId)) {
        osr->close();
        osr->deleteLater();
    }
    m_runningWidgets.remove(instanceId);
    stopSystemInfoWatchesForInstance(instanceId);
    clearDevelopmentWatcher(instanceId);
    m_instances.remove(instanceId);
    if (!m_stoppingAll) {
        m_layout.setVisible(instanceId, false);
        saveToDefaultConfig();
    }
    emit widgetStopped(instanceId);
    if (!m_stoppingAll) {
        updateDefaultCefPage();
    }
}

void WidgetManager::configureDevelopmentWatcher(const WidgetInstance& instance)
{
    if (!instance.isDevelopment || !instance.autoReload) {
        return;
    }

    clearDevelopmentWatcher(instance.instanceId);
    QStringList files = watchedFilesForPackage(instance.packagePath);
    QStringList directories = watchedDirectoriesForPackage(instance.packagePath);
    if (files.isEmpty() && directories.isEmpty()) {
        return;
    }

    auto* watcher = new QFileSystemWatcher(this);
    if (!files.isEmpty()) {
        watcher->addPaths(files);
    }
    if (!directories.isEmpty()) {
        watcher->addPaths(directories);
    }
    auto* timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(500);
    connect(watcher, &QFileSystemWatcher::fileChanged, this, [this, id = instance.instanceId](const QString& path) {
        if (QFileInfo::exists(path)) {
            QFileSystemWatcher* watcher = m_developmentWatchers.value(id, nullptr);
            if (watcher && !watcher->files().contains(path)) {
                watcher->addPath(path);
            }
        }
        QTimer* reloadTimer = m_developmentReloadTimers.value(id, nullptr);
        if (reloadTimer) {
            reloadTimer->start();
        }
    });
    connect(watcher, &QFileSystemWatcher::directoryChanged, this, [this, id = instance.instanceId](const QString&) {
        const WidgetInstance instance = m_instances.value(id);
        QFileSystemWatcher* watcher = m_developmentWatchers.value(id, nullptr);
        if (watcher && !instance.instanceId.isEmpty()) {
            const QStringList nextFiles = watchedFilesForPackage(instance.packagePath);
            for (const QString& file : nextFiles) {
                if (!watcher->files().contains(file)) {
                    watcher->addPath(file);
                }
            }
            const QStringList nextDirectories = watchedDirectoriesForPackage(instance.packagePath);
            for (const QString& directory : nextDirectories) {
                if (!watcher->directories().contains(directory)) {
                    watcher->addPath(directory);
                }
            }
        }
        QTimer* reloadTimer = m_developmentReloadTimers.value(id, nullptr);
        if (reloadTimer) {
            reloadTimer->start();
        }
    });
    connect(timer, &QTimer::timeout, this, [this, id = instance.instanceId]() {
        restartWidget(id);
    });
    m_developmentWatchers.insert(instance.instanceId, watcher);
    m_developmentReloadTimers.insert(instance.instanceId, timer);
}

void WidgetManager::clearDevelopmentWatcher(const QString& instanceId)
{
    if (QFileSystemWatcher* watcher = m_developmentWatchers.take(instanceId)) {
        watcher->deleteLater();
    }
    if (QTimer* timer = m_developmentReloadTimers.take(instanceId)) {
        timer->deleteLater();
    }
}

QStringList WidgetManager::watchedFilesForPackage(const QString& packagePath) const
{
    QStringList result;
    const QDir dir(packagePath);
    const QStringList filters {
        QStringLiteral("manifest.json"),
        QStringLiteral("*.html"),
        QStringLiteral("*.css"),
        QStringLiteral("*.js")
    };
    const QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Name);
    for (const QFileInfo& file : files) {
        result.append(file.absoluteFilePath());
    }
    return result;
}

QStringList WidgetManager::watchedDirectoriesForPackage(const QString& packagePath) const
{
    QStringList result;
    const QDir root(packagePath);
    if (!root.exists()) {
        return result;
    }

    result.append(root.absolutePath());
    const QFileInfoList directories = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& directory : directories) {
        const QString name = directory.fileName();
        if (name == QStringLiteral(".git") ||
            name == QStringLiteral("node_modules") ||
            name.compare(QStringLiteral("cache"), Qt::CaseInsensitive) == 0 ||
            name.compare(QStringLiteral("tmp"), Qt::CaseInsensitive) == 0 ||
            name.compare(QStringLiteral("temp"), Qt::CaseInsensitive) == 0) {
            continue;
        }
        result.append(directory.absoluteFilePath());
    }
    return result;
}

void WidgetManager::stopAll(bool includeWallpaper)
{
    const bool wasStoppingAll = m_stoppingAll;
    m_stoppingAll = true;
    const QStringList ids = m_runningWidgets.values();
    for (const QString& id : ids) {
        WidgetInstance inst = m_instances.value(id);
        if (!includeWallpaper && inst.manifest.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0) {
            continue;
        }
        stopWidget(id);
    }

    if (includeWallpaper) {
        for (auto* osr : m_osrBrowsers) {
            if (osr) {
                osr->close();
                osr->deleteLater();
            }
        }
        m_osrBrowsers.clear();
        if (m_defaultOsrBrowser) {
            m_defaultOsrBrowser->close();
            m_defaultOsrBrowser->deleteLater();
            m_defaultOsrBrowser = nullptr;
        }
        if (m_canvasWidget) {
            m_canvasWidget->clearAllFrames();
        }
        m_runningWidgets.clear();
        m_instances.clear();
        qDeleteAll(m_systemInfoWatchTimers);
        m_systemInfoWatchTimers.clear();
        stopWallpaper(false);
    } else {
        const QStringList osrKeys = m_osrBrowsers.keys();
        for (const QString& key : osrKeys) {
            if (key == m_activeWallpaperId || key.startsWith(QStringLiteral("wallpaper:"))) {
                continue;
            }
            if (auto* osr = m_osrBrowsers.take(key)) {
                if (m_canvasWidget) {
                    m_canvasWidget->removeWidgetFrame(key);
                }
                osr->close();
                osr->deleteLater();
            }
        }
        if (!wasStoppingAll) {
            updateDefaultCefPage();
        }
    }
    m_stoppingAll = wasStoppingAll;
}

WidgetInstance WidgetManager::widgetInstance(const QString& instanceId) const
{
    return m_instances.value(instanceId);
}

void WidgetManager::handleSystemInfoRequest(const QJsonObject& message)
{
    const QString instanceId = message.value(QStringLiteral("instanceId")).toString(message.value(QStringLiteral("widgetId")).toString()).trimmed();
    const QString requestId = message.value(QStringLiteral("requestId")).toString();
    const QString action = message.value(QStringLiteral("action")).toString(QStringLiteral("snapshot"));
    const QString watchId = message.value(QStringLiteral("watchId")).toString(requestId);

    const WidgetInstance instance = m_instances.value(instanceId);
    if (instance.instanceId.isEmpty()) {
        qWarning() << "[LinWidget][WidgetManager] system_info_request rejected: instance not running:" << instanceId
                   << "action:" << action;
        sendSystemInfoResponse(instanceId, requestId, action, false, QJsonObject(), QStringLiteral("Widget instance is not running."));
        return;
    }
    if (!m_permissions.canUseDesktopApi(instance.grantedPermissions)) {
        qWarning() << "[LinWidget][WidgetManager] system_info_request rejected: 'desktop' permission not granted for"
                   << instanceId << "granted:" << instance.grantedPermissions << "action:" << action;
        sendSystemInfoResponse(instanceId, requestId, action, false, QJsonObject(), QStringLiteral("Desktop permission is required."));
        return;
    }

    if (action == QStringLiteral("snapshot")) {
        sendSystemInfoResponse(instanceId, requestId, action, true, m_systemInfoService.snapshot());
        return;
    }
    const QStringList categories {
        QStringLiteral("cpu"),
        QStringLiteral("memory"),
        QStringLiteral("gpu"),
        QStringLiteral("disk"),
        QStringLiteral("network"),
        QStringLiteral("battery"),
        QStringLiteral("os"),
        QStringLiteral("uptime")
    };
    if (categories.contains(action)) {
        sendSystemInfoResponse(instanceId, requestId, action, true, m_systemInfoService.categorySnapshot(action));
        return;
    }

    if (action == QStringLiteral("watch")) {
        if (watchId.isEmpty()) {
            sendSystemInfoResponse(instanceId, requestId, action, false, QJsonObject(), QStringLiteral("Missing watch id."));
            return;
        }
        const QString timerKey = instanceId + QLatin1Char('\n') + watchId;
        if (QTimer* oldTimer = m_systemInfoWatchTimers.take(timerKey)) {
            oldTimer->deleteLater();
        }
        const int intervalMs = qMax(500, message.value(QStringLiteral("intervalMs")).toInt(1000));
        auto* timer = new QTimer(this);
        timer->setInterval(intervalMs);
        connect(timer, &QTimer::timeout, this, [this, instanceId, watchId]() {
            if (!m_instances.contains(instanceId)) {
                stopSystemInfoWatchesForInstance(instanceId);
                return;
            }
            sendSystemInfoUpdate(instanceId, watchId, m_systemInfoService.snapshot());
        });
        m_systemInfoWatchTimers.insert(timerKey, timer);
        timer->start();
        sendSystemInfoResponse(instanceId, requestId, action, true, m_systemInfoService.snapshot());
        return;
    }

    if (action == QStringLiteral("unwatch")) {
        const QString timerKey = instanceId + QLatin1Char('\n') + watchId;
        if (QTimer* timer = m_systemInfoWatchTimers.take(timerKey)) {
            timer->deleteLater();
        }
        sendSystemInfoResponse(instanceId, requestId, action, true);
        return;
    }

    sendSystemInfoResponse(instanceId, requestId, action, false, QJsonObject(), QStringLiteral("Unknown system info action."));
}

void WidgetManager::sendSystemInfoResponse(const QString& instanceId,
                                           const QString& requestId,
                                           const QString& action,
                                           bool ok,
                                           const QJsonObject& data,
                                           const QString& errorMessage)
{
    QJsonObject message = makeWidgetMessage(QString::fromLatin1(WidgetMessageType::SystemInfoResponse), instanceId);
    message.insert(QStringLiteral("instanceId"), instanceId);
    message.insert(QStringLiteral("widgetId"), instanceId);
    message.insert(QStringLiteral("requestId"), requestId);
    message.insert(QStringLiteral("action"), action);
    message.insert(QStringLiteral("ok"), ok);
    if (!data.isEmpty()) {
        message.insert(QStringLiteral("data"), data);
    }
    if (!errorMessage.isEmpty()) {
        message.insert(QStringLiteral("error"), errorMessage);
    }
    sendMessageToWidget(instanceId, message);
}

void WidgetManager::sendSystemInfoUpdate(const QString& instanceId,
                                         const QString& watchId,
                                         const QJsonObject& data)
{
    QJsonObject message = makeWidgetMessage(QString::fromLatin1(WidgetMessageType::SystemInfoUpdate), instanceId);
    message.insert(QStringLiteral("instanceId"), instanceId);
    message.insert(QStringLiteral("widgetId"), instanceId);
    message.insert(QStringLiteral("watchId"), watchId);
    message.insert(QStringLiteral("data"), data);
    sendMessageToWidget(instanceId, message);
}

void WidgetManager::handleStorageRequest(const QJsonObject& message)
{
    const QString instanceId = message.value(QStringLiteral("instanceId")).toString(message.value(QStringLiteral("widgetId")).toString()).trimmed();
    const QString requestId = message.value(QStringLiteral("requestId")).toString();
    const QString scope = message.value(QStringLiteral("scope")).toString(QStringLiteral("instance")).trimmed().toLower();
    const QString action = message.value(QStringLiteral("action")).toString(QStringLiteral("get")).trimmed().toLower();
    const QString key = message.value(QStringLiteral("key")).toString().trimmed();

    const WidgetInstance instance = m_instances.value(instanceId);
    if (instance.instanceId.isEmpty()) {
        sendStorageResponse(instanceId, requestId, false, QJsonValue(), QStringLiteral("Widget instance is not running."));
        return;
    }
    if (!m_permissions.canUseStorage(instance.grantedPermissions)) {
        appendSharedLog(QStringLiteral("permission"),
                        QStringLiteral("%1 permission denied: storage").arg(instanceId));
        sendStorageResponse(instanceId, requestId, false, QJsonValue(), QStringLiteral("Storage permission is required."));
        return;
    }
    if (scope != QStringLiteral("instance") && scope != QStringLiteral("widget")) {
        sendStorageResponse(instanceId, requestId, false, QJsonValue(), QStringLiteral("Storage scope must be instance or widget."));
        return;
    }
    if ((action == QStringLiteral("get") || action == QStringLiteral("set") || action == QStringLiteral("remove")) && key.isEmpty()) {
        sendStorageResponse(instanceId, requestId, false, QJsonValue(), QStringLiteral("Storage key is required."));
        return;
    }

    const QString path = storageFilePathForInstance(instance, scope);
    QJsonObject object;
    QString errorMessage;
    if (!loadStorageObject(path, &object, &errorMessage)) {
        sendStorageResponse(instanceId, requestId, false, QJsonValue(), errorMessage);
        return;
    }

    if (action == QStringLiteral("get")) {
        sendStorageResponse(instanceId, requestId, true, object.value(key));
        return;
    }
    if (action == QStringLiteral("all")) {
        sendStorageResponse(instanceId, requestId, true, object);
        return;
    }
    if (action == QStringLiteral("set")) {
        object.insert(key, message.value(QStringLiteral("value")));
        const QByteArray encoded = QJsonDocument(object).toJson(QJsonDocument::Compact);
        const QString widgetDataRoot = QFileInfo(storageFilePathForInstance(instance, QStringLiteral("widget"))).absolutePath();
        const qint64 currentDirBytes = calculateDirectorySize(widgetDataRoot);
        const qint64 oldFileBytes = QFileInfo(path).exists() ? QFileInfo(path).size() : 0;
        if ((currentDirBytes - oldFileBytes + encoded.size()) > kWidgetStorageQuotaBytes) {
            sendStorageResponse(instanceId, requestId, false, QJsonValue(), QStringLiteral("Storage quota (16MB) exceeded."));
            return;
        }
    } else if (action == QStringLiteral("remove")) {
        object.remove(key);
    } else if (action == QStringLiteral("clear")) {
        object = QJsonObject();
    } else {
        sendStorageResponse(instanceId, requestId, false, QJsonValue(), QStringLiteral("Unknown storage action."));
        return;
    }

    if (!saveStorageObject(path, object, &errorMessage)) {
        sendStorageResponse(instanceId, requestId, false, QJsonValue(), errorMessage);
        return;
    }
    sendStorageResponse(instanceId, requestId, true, action == QStringLiteral("set") ? message.value(QStringLiteral("value")) : QJsonValue(true));
}

void WidgetManager::sendStorageResponse(const QString& instanceId,
                                        const QString& requestId,
                                        bool ok,
                                        const QJsonValue& value,
                                        const QString& errorMessage)
{
    QJsonObject message = makeWidgetMessage(QString::fromLatin1(WidgetMessageType::StorageResponse), instanceId);
    message.insert(QStringLiteral("instanceId"), instanceId);
    message.insert(QStringLiteral("widgetId"), instanceId);
    message.insert(QStringLiteral("requestId"), requestId);
    message.insert(QStringLiteral("ok"), ok);
    if (!value.isUndefined()) {
        message.insert(QStringLiteral("value"), value);
    }
    if (!errorMessage.isEmpty()) {
        message.insert(QStringLiteral("error"), errorMessage);
    }
    sendMessageToWidget(instanceId, message);
}

void WidgetManager::stopSystemInfoWatchesForInstance(const QString& instanceId)
{
    const QString prefix = instanceId + QLatin1Char('\n');
    const QStringList keys = m_systemInfoWatchTimers.keys();
    for (const QString& key : keys) {
        if (!key.startsWith(prefix)) {
            continue;
        }
        if (QTimer* timer = m_systemInfoWatchTimers.take(key)) {
            timer->deleteLater();
        }
    }
}

QString WidgetManager::makeInstanceId(const WidgetManifest& manifest) const
{
    return QStringLiteral("%1.%2").arg(manifest.id, QString::number(QDateTime::currentMSecsSinceEpoch()));
}

QJsonObject WidgetManager::handleWidgetManagerRequest(const QJsonObject& request)
{
    const QString action = request.value(QStringLiteral("action")).toString(QStringLiteral("list")).trimmed().toLower();
    const QString requestId = request.value(QStringLiteral("requestId")).toString();
    QJsonObject response;
    response.insert(QStringLiteral("type"), QString::fromLatin1(WidgetMessageType::WidgetManagerResponse));
    response.insert(QStringLiteral("action"), action);
    if (!requestId.isEmpty()) {
        response.insert(QStringLiteral("requestId"), requestId);
    }

    auto& registry = WidgetRegistryManager::instance();
    registry.load();

    auto buildWidgetDetailJson = [&](const WidgetRegistryEntry& entry) -> QJsonObject {
        WidgetManifest manifest;
        const bool manifestLoaded = WidgetPackage::loadManifest(entry.packagePath, &manifest, nullptr);
        const QString regId = entry.id.trimmed().toLower();
        const QString safeId = safeStorageSegment(regId.isEmpty() ? manifest.id : regId);
        const QString mode = entry.isDevelopment ? QStringLiteral("dev") : QStringLiteral("installed");
        const QString dataDirPath = QDir(AppPaths::linWidgetDataDir()).filePath(mode + QLatin1Char('/') + safeId);
        const QString altMode = entry.isDevelopment ? QStringLiteral("installed") : QStringLiteral("dev");
        const QString altDataDirPath = QDir(AppPaths::linWidgetDataDir()).filePath(altMode + QLatin1Char('/') + safeId);
        const qint64 storageUsedBytes = calculateDirectorySize(dataDirPath) + calculateDirectorySize(altDataDirPath);
        const qint64 packageSizeBytes = calculateDirectorySize(entry.packagePath);

        QJsonArray runningInstanceIds;
        QJsonArray instancesArray;
        int totalCrashes = m_totalCrashCounts.value(regId, 0);
        bool isCrashed = m_crashTrippedWidgets.contains(regId);

        for (const WidgetLayoutState& state : m_layout.states()) {
            if (stateRegistryId(state) == regId) {
                const bool instRunning = m_runningWidgets.contains(state.instanceId);
                if (instRunning) {
                    runningInstanceIds.append(state.instanceId);
                }
                totalCrashes += m_totalCrashCounts.value(state.instanceId, 0);
                if (m_crashTrippedWidgets.contains(state.instanceId)) {
                    isCrashed = true;
                }
                QJsonObject instObj;
                instObj.insert(QStringLiteral("instanceId"), state.instanceId);
                instObj.insert(QStringLiteral("running"), instRunning);
                instObj.insert(QStringLiteral("visible"), state.visible);
                QJsonObject geomObj;
                geomObj.insert(QStringLiteral("x"), state.geometry.x());
                geomObj.insert(QStringLiteral("y"), state.geometry.y());
                geomObj.insert(QStringLiteral("width"), state.geometry.width());
                geomObj.insert(QStringLiteral("height"), state.geometry.height());
                instObj.insert(QStringLiteral("geometry"), geomObj);
                instancesArray.append(instObj);
            }
        }
        const QString wallpaperInstId = QStringLiteral("wallpaper:") + regId;
        if (!m_activeWallpaperId.isEmpty() && m_activeWallpaperId.compare(wallpaperInstId, Qt::CaseInsensitive) == 0) {
            runningInstanceIds.append(m_activeWallpaperId);
        }

        const bool isRunning = !runningInstanceIds.isEmpty();
        QString statusText = QStringLiteral("stopped");
        if (isCrashed && !isRunning) {
            statusText = QStringLiteral("crashed");
        } else if (isRunning) {
            statusText = QStringLiteral("running");
        } else if (!entry.enabled) {
            statusText = QStringLiteral("disabled");
        }

        QString sourceLabel = QStringLiteral("组件包安装");
        if (entry.isDevelopment) {
            sourceLabel = QStringLiteral("开发目录");
        } else if (entry.trusted || entry.packagePath.contains(QStringLiteral("resources/LinWidgets"), Qt::CaseInsensitive)) {
            sourceLabel = QStringLiteral("内置组件");
        }

        QJsonArray declaredPerms;
        if (manifestLoaded) {
            for (const QString& p : manifest.permissions) {
                declaredPerms.append(p);
            }
        }
        const QStringList normalizedGranted = entry.isDevelopment
            ? m_permissions.normalizePermissions(manifestLoaded ? manifest.permissions : entry.permissionsGranted)
            : m_permissions.normalizePermissions(entry.permissionsGranted);
        QJsonArray grantedPerms;
        for (const QString& p : normalizedGranted) {
            grantedPerms.append(p);
        }

        QJsonObject item;
        item.insert(QStringLiteral("id"), entry.id);
        item.insert(QStringLiteral("name"), entry.name.isEmpty() ? (manifestLoaded ? manifest.name : entry.id) : entry.name);
        item.insert(QStringLiteral("version"), entry.version.isEmpty() ? (manifestLoaded ? manifest.version : QStringLiteral("1.0.0")) : entry.version);
        item.insert(QStringLiteral("author"), entry.author.isEmpty() ? manifest.author : entry.author);
        item.insert(QStringLiteral("description"), entry.description.isEmpty() ? manifest.description : entry.description);
        item.insert(QStringLiteral("role"), entry.role.isEmpty() ? manifest.role : entry.role);
        item.insert(QStringLiteral("packagePath"), entry.packagePath);
        item.insert(QStringLiteral("source"), sourceLabel);
        item.insert(QStringLiteral("isDevelopment"), entry.isDevelopment);
        item.insert(QStringLiteral("trusted"), entry.trusted);
        item.insert(QStringLiteral("enabled"), entry.enabled);
        item.insert(QStringLiteral("running"), isRunning);
        item.insert(QStringLiteral("status"), statusText);
        item.insert(QStringLiteral("crashCount"), totalCrashes);
        item.insert(QStringLiteral("packageSizeBytes"), packageSizeBytes);
        item.insert(QStringLiteral("storageUsedBytes"), storageUsedBytes);
        item.insert(QStringLiteral("storageQuotaBytes"), kWidgetStorageQuotaBytes);
        item.insert(QStringLiteral("declaredPermissions"), declaredPerms);
        item.insert(QStringLiteral("grantedPermissions"), grantedPerms);
        item.insert(QStringLiteral("hasSettings"), manifestLoaded && (!manifest.settings.isEmpty() || !manifest.settingsSchema.isEmpty()));
        item.insert(QStringLiteral("installedAt"), entry.installedAt);
        item.insert(QStringLiteral("updatedAt"), entry.updatedAt);
        item.insert(QStringLiteral("dataDirPath"), dataDirPath);
        item.insert(QStringLiteral("logFilePath"), sharedLogFilePath());
        item.insert(QStringLiteral("runningInstanceIds"), runningInstanceIds);
        item.insert(QStringLiteral("instances"), instancesArray);
        return item;
    };

    auto resolveWidgetId = [this](const QString& rawId) -> QString {
        return this->resolveWidgetId(rawId);
    };

    if (action == QStringLiteral("list")) {
        QJsonArray widgetsArray;
        QSet<QString> seenIds;
        for (const WidgetRegistryEntry& entry : registry.entries()) {
            seenIds.insert(entry.id.trimmed().toLower());
            widgetsArray.append(buildWidgetDetailJson(entry));
        }
        for (const WidgetInstance& inst : m_instances) {
            const QString regId = registryIdForManifest(inst.manifest);
            if (!regId.isEmpty() && !seenIds.contains(regId)) {
                seenIds.insert(regId);
                WidgetRegistryEntry synth;
                synth.id = inst.manifest.id;
                synth.name = inst.manifest.name;
                synth.version = inst.manifest.version;
                synth.author = inst.manifest.author;
                synth.description = inst.manifest.description;
                synth.packagePath = inst.packagePath;
                synth.role = inst.manifest.role;
                synth.isDevelopment = inst.isDevelopment;
                synth.enabled = true;
                synth.permissionsGranted = inst.grantedPermissions;
                widgetsArray.append(buildWidgetDetailJson(synth));
            }
        }
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("widgets"), widgetsArray);
        response.insert(QStringLiteral("runtimeEnabled"), m_enabled);
        response.insert(QStringLiteral("showBounds"), showWidgetBounds());
        response.insert(QStringLiteral("activeWallpaperId"), m_activeWallpaperId);
        response.insert(QStringLiteral("activeWallpaperPackagePath"), m_activeWallpaperPackagePath);
        response.insert(QStringLiteral("activeWallpaperInteractive"), m_activeWallpaperInteractive);
        response.insert(QStringLiteral("userWidgetsDir"), AppPaths::userLinWidgetsDir());
        response.insert(QStringLiteral("logDir"), AppPaths::userLogsDir());
        return response;
    }

    // --- Global Actions (do not require widgetId) ---
    if (action == QStringLiteral("open_widgets_dir")) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::userLinWidgetsDir()));
        response.insert(QStringLiteral("ok"), true);
        return response;
    }

    if (action == QStringLiteral("open_log_folder")) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::userLogsDir()));
        response.insert(QStringLiteral("ok"), true);
        return response;
    }

    if (action == QStringLiteral("set_master_enabled")) {
        const bool enabled = request.value(QStringLiteral("enabled")).toBool(true);
        setEnabled(enabled);
        QSettings s(AppPaths::configFilePath(), QSettings::IniFormat);
        s.setValue(QStringLiteral("LinWidgets/Enabled"), enabled);
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("enabled"), enabled);
        return response;
    }

    if (action == QStringLiteral("set_show_bounds")) {
        const bool showBounds = request.value(QStringLiteral("showBounds")).toBool(false);
        setShowWidgetBounds(showBounds);
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("showBounds"), showBounds);
        return response;
    }

    if (action == QStringLiteral("stop_wallpaper")) {
        stopWallpaper(true);
        response.insert(QStringLiteral("ok"), true);
        return response;
    }

    if (action == QStringLiteral("create_project")) {
        const QString rawName = request.value(QStringLiteral("name")).toString(QStringLiteral("MyWidget")).trimmed();
        QString safeName = rawName;
        safeName.replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_-]")), QStringLiteral("_"));
        if (safeName.isEmpty()) safeName = QStringLiteral("MyWidget");
        const QString targetDir = QDir(AppPaths::userLinWidgetsDir()).filePath(safeName);
        QDir().mkpath(targetDir);

        QJsonObject manifestObj;
        manifestObj.insert(QStringLiteral("id"), safeName.toLower());
        manifestObj.insert(QStringLiteral("name"), rawName);
        manifestObj.insert(QStringLiteral("version"), QStringLiteral("1.0.0"));
        manifestObj.insert(QStringLiteral("description"), QStringLiteral("用户自定义 LinWidget 桌面组件"));
        manifestObj.insert(QStringLiteral("entry"), QStringLiteral("index.html"));
        manifestObj.insert(QStringLiteral("width"), 320);
        manifestObj.insert(QStringLiteral("height"), 220);
        manifestObj.insert(QStringLiteral("role"), QStringLiteral("widget"));
        QJsonArray perms; perms.append(QStringLiteral("storage")); perms.append(QStringLiteral("systemInfo"));
        manifestObj.insert(QStringLiteral("permissions"), perms);

        QFile mFile(QDir(targetDir).filePath(QStringLiteral("manifest.json")));
        if (mFile.open(QIODevice::WriteOnly)) {
            mFile.write(QJsonDocument(manifestObj).toJson(QJsonDocument::Indented));
            mFile.close();
        }

        QFile hFile(QDir(targetDir).filePath(QStringLiteral("index.html")));
        if (hFile.open(QIODevice::WriteOnly)) {
            const QString htmlContent = QStringLiteral(
                "<!DOCTYPE html>\n<html>\n<head>\n"
                "  <meta charset=\"utf-8\">\n"
                "  <style>\n"
                "    body { margin: 0; padding: 16px; font-family: system-ui; background: rgba(15,23,42,0.88); color: #fff; border-radius: 12px; border: 1px solid rgba(255,255,255,0.12); backdrop-filter: blur(16px); box-sizing: border-box; height: 100vh; display: flex; flex-direction: column; justify-content: center; align-items: center; text-align: center; }\n"
                "    h2 { margin: 0 0 6px 0; font-size: 16px; font-weight: 600; }\n"
                "    p { margin: 0; font-size: 11px; color: #94a3b8; }\n"
                "    .time { font-size: 26px; font-weight: 700; margin: 10px 0; color: #38bdf8; font-variant-numeric: tabular-nums; }\n"
                "  </style>\n</head>\n<body>\n"
                "  <h2>%1</h2>\n"
                "  <div class=\"time\" id=\"clock\">00:00:00</div>\n"
                "  <p>LinWidget 桌面小组件已就绪</p>\n"
                "  <script>\n"
                "    function update() { document.getElementById('clock').textContent = new Date().toLocaleTimeString(); }\n"
                "    setInterval(update, 1000); update();\n"
                "  </script>\n</body>\n</html>\n"
            ).arg(rawName);
            hFile.write(htmlContent.toUtf8());
            hFile.close();
        }

        registry.addDevelopmentProject(targetDir);
        registry.save();
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("packagePath"), targetDir);
        response.insert(QStringLiteral("id"), safeName.toLower());
        return response;
    }

    if (action == QStringLiteral("add_development")) {
        const QString path = request.value(QStringLiteral("path")).toString().trimmed();
        if (path.isEmpty() || !QDir(path).exists()) {
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"), QStringLiteral("Directory does not exist."));
            return response;
        }
        WidgetManifest devManifest;
        QString manifestErr;
        if (!WidgetPackage::loadManifest(path, &devManifest, &manifestErr)) {
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"), manifestErr);
            return response;
        }
        registry.addDevelopmentProject(path);
        registry.save();
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("id"), devManifest.id);
        return response;
    }

    const QString rawTargetId = request.value(QStringLiteral("widgetId")).toString(
        request.value(QStringLiteral("id")).toString(
            request.value(QStringLiteral("instanceId")).toString()));
    const QString resolvedId = resolveWidgetId(rawTargetId);
    if (resolvedId.isEmpty()) {
        response.insert(QStringLiteral("ok"), false);
        response.insert(QStringLiteral("error"), QStringLiteral("Missing widgetId."));
        return response;
    }

    if (action == QStringLiteral("info")) {
        WidgetRegistryEntry entry = registry.entryById(resolvedId);
        if (entry.id.isEmpty()) {
            for (const WidgetInstance& inst : m_instances) {
                if (registryIdForManifest(inst.manifest) == resolvedId) {
                    entry.id = inst.manifest.id;
                    entry.name = inst.manifest.name;
                    entry.version = inst.manifest.version;
                    entry.author = inst.manifest.author;
                    entry.description = inst.manifest.description;
                    entry.packagePath = inst.packagePath;
                    entry.role = inst.manifest.role;
                    entry.isDevelopment = inst.isDevelopment;
                    entry.enabled = true;
                    entry.permissionsGranted = inst.grantedPermissions;
                    break;
                }
            }
        }
        if (entry.id.isEmpty()) {
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"), QStringLiteral("Widget not found: %1").arg(resolvedId));
            return response;
        }
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("widget"), buildWidgetDetailJson(entry));
        return response;
    }

    if (action == QStringLiteral("set_permission")) {
        const QString perm = request.value(QStringLiteral("permission")).toString().trimmed().toLower();
        const bool granted = request.value(QStringLiteral("granted")).toBool(false);
        if (!m_permissions.isPermissionSupported(perm)) {
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"), QStringLiteral("Unsupported permission: %1").arg(perm));
            return response;
        }
        WidgetRegistryEntry entry = registry.entryById(resolvedId);
        if (entry.id.isEmpty()) {
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"), QStringLiteral("Widget not found in registry: %1").arg(resolvedId));
            return response;
        }
        QStringList nextPerms = m_permissions.normalizePermissions(entry.permissionsGranted);
        if (granted) {
            if (!nextPerms.contains(perm)) {
                nextPerms.append(perm);
            }
        } else {
            nextPerms.removeAll(perm);
        }
        registry.setPermissionsGranted(entry.id, nextPerms);
        registry.save();

        // Update all running instances immediately
        for (auto it = m_instances.begin(); it != m_instances.end(); ++it) {
            if (registryIdForManifest(it.value().manifest) == resolvedId) {
                it.value().grantedPermissions = nextPerms;
                if (perm == QStringLiteral("network")) {
                    if (auto* osr = m_osrBrowsers.value(it.key(), nullptr)) {
                        osr->setAllowNetwork(granted && it.value().manifest.permissions.contains(QStringLiteral("network")));
                    }
                }
            }
        }
        qInfo() << "[LinWidget][WidgetManager] Updated permission for widget:" << resolvedId
                << "permission:" << perm << "granted:" << granted;
        response.insert(QStringLiteral("ok"), true);
        QJsonArray grantedArr;
        for (const QString& p : nextPerms) {
            grantedArr.append(p);
        }
        response.insert(QStringLiteral("grantedPermissions"), grantedArr);
        return response;
    }

    if (action == QStringLiteral("clear_storage")) {
        const QString safeId = safeStorageSegment(resolvedId);
        const QStringList modes { QStringLiteral("installed"), QStringLiteral("dev") };
        for (const QString& mode : modes) {
            QDir widgetDataDir(QDir(AppPaths::linWidgetDataDir()).filePath(mode + QLatin1Char('/') + safeId));
            if (widgetDataDir.exists()) {
                widgetDataDir.removeRecursively();
                QDir(AppPaths::linWidgetDataDir()).mkpath(mode + QLatin1Char('/') + safeId);
            }
        }
        QJsonObject notifyMsg = makeWidgetMessage(QStringLiteral("storage_cleared"), resolvedId);
        broadcastToWidgetInstances(resolvedId, notifyMsg);
        qInfo() << "[LinWidget][WidgetManager] Cleared isolated storage data for widget:" << resolvedId;
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("storageUsedBytes"), 0);
        return response;
    }

    if (action == QStringLiteral("export_data")) {
        const QString safeId = safeStorageSegment(resolvedId);
        QJsonObject exportBundle;
        exportBundle.insert(QStringLiteral("widgetId"), resolvedId);
        exportBundle.insert(QStringLiteral("exportedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

        QJsonObject modesBundle;
        const QStringList modes { QStringLiteral("installed"), QStringLiteral("dev") };
        for (const QString& mode : modes) {
            const QDir modeDir(QDir(AppPaths::linWidgetDataDir()).filePath(mode + QLatin1Char('/') + safeId));
            if (!modeDir.exists()) {
                continue;
            }
            QJsonObject modeObj;
            QJsonObject widgetJson;
            if (loadStorageObject(modeDir.filePath(QStringLiteral("widget.json")), &widgetJson, nullptr) && !widgetJson.isEmpty()) {
                modeObj.insert(QStringLiteral("widget"), widgetJson);
            }
            const QDir instDir(modeDir.filePath(QStringLiteral("instances")));
            if (instDir.exists()) {
                QJsonObject instancesObj;
                const QFileInfoList files = instDir.entryInfoList({ QStringLiteral("*.json") }, QDir::Files);
                for (const QFileInfo& fi : files) {
                    QJsonObject instData;
                    if (loadStorageObject(fi.absoluteFilePath(), &instData, nullptr)) {
                        instancesObj.insert(fi.completeBaseName(), instData);
                    }
                }
                if (!instancesObj.isEmpty()) {
                    modeObj.insert(QStringLiteral("instances"), instancesObj);
                }
            }
            if (!modeObj.isEmpty()) {
                modesBundle.insert(mode, modeObj);
            }
        }
        exportBundle.insert(QStringLiteral("storage"), modesBundle);

        const QString targetPath = request.value(QStringLiteral("targetPath")).toString().trimmed();
        if (!targetPath.isEmpty()) {
            QDir().mkpath(QFileInfo(targetPath).absolutePath());
            QSaveFile saveFile(targetPath);
            if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                response.insert(QStringLiteral("ok"), false);
                response.insert(QStringLiteral("error"), saveFile.errorString());
                return response;
            }
            saveFile.write(QJsonDocument(exportBundle).toJson(QJsonDocument::Indented));
            if (!saveFile.commit()) {
                response.insert(QStringLiteral("ok"), false);
                response.insert(QStringLiteral("error"), saveFile.errorString());
                return response;
            }
            response.insert(QStringLiteral("exportPath"), targetPath);
        }
        qInfo() << "[LinWidget][WidgetManager] Exported widget storage data for:" << resolvedId << "targetPath:" << targetPath;
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("data"), exportBundle);
        return response;
    }

    if (action == QStringLiteral("restart")) {
        m_crashTimestamps.remove(resolvedId);
        m_crashTrippedWidgets.remove(resolvedId);
        int restartedCount = 0;

        if (!m_activeWallpaperId.isEmpty() &&
            m_activeWallpaperId.compare(QStringLiteral("wallpaper:") + resolvedId, Qt::CaseInsensitive) == 0) {
            if (restartWidget(m_activeWallpaperId)) {
                ++restartedCount;
            }
        }

        for (const WidgetLayoutState& state : m_layout.states()) {
            if (stateRegistryId(state) == resolvedId) {
                m_crashTimestamps.remove(state.instanceId);
                m_crashTrippedWidgets.remove(state.instanceId);
                if (m_runningWidgets.contains(state.instanceId) || state.visible) {
                    if (restartWidget(state.instanceId)) {
                        ++restartedCount;
                    }
                }
            }
        }

        if (restartedCount == 0 && m_enabled) {
            const WidgetRegistryEntry entry = registry.entryById(resolvedId);
            if (!entry.id.isEmpty() && QFileInfo::exists(entry.packagePath)) {
                QString launchedId;
                if (launchWidgetPackage(entry.packagePath, QRect(QPoint(260, 220), entry.defaultSize), entry.isDevelopment, entry.autoReload, &launchedId, nullptr)) {
                    ++restartedCount;
                }
            }
        }
        qInfo() << "[LinWidget][WidgetManager] Restarted widget:" << resolvedId << "restartedCount:" << restartedCount;
        response.insert(QStringLiteral("ok"), restartedCount > 0);
        response.insert(QStringLiteral("restartedCount"), restartedCount);
        if (restartedCount == 0) {
            response.insert(QStringLiteral("error"), QStringLiteral("Failed to restart widget %1.").arg(resolvedId));
        }
        return response;
    }

    if (action == QStringLiteral("set_enabled")) {
        const bool targetEnabled = request.value(QStringLiteral("enabled")).toBool(true);
        WidgetRegistryEntry entry = registry.entryById(resolvedId);
        if (entry.id.isEmpty()) {
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"), QStringLiteral("Widget not found in registry: %1").arg(resolvedId));
            return response;
        }
        registry.setEnabled(entry.id, targetEnabled);
        registry.save();

        if (!targetEnabled) {
            if (!m_activeWallpaperId.isEmpty() &&
                m_activeWallpaperId.compare(QStringLiteral("wallpaper:") + resolvedId, Qt::CaseInsensitive) == 0) {
                stopWallpaper(true);
            }
            const QVector<WidgetLayoutState> states = m_layout.states();
            for (const WidgetLayoutState& state : states) {
                if (stateRegistryId(state) == resolvedId && m_runningWidgets.contains(state.instanceId)) {
                    stopWidget(state.instanceId);
                }
            }
        } else if (m_enabled) {
            m_crashTimestamps.remove(resolvedId);
            m_crashTrippedWidgets.remove(resolvedId);
            if (entry.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0) {
                startWallpaper(entry.packagePath, entry.isDevelopment, false, nullptr);
            } else {
                bool launchedAny = false;
                for (const WidgetLayoutState& state : m_layout.states()) {
                    if (stateRegistryId(state) == resolvedId) {
                        if (restartWidget(state.instanceId)) {
                            launchedAny = true;
                        }
                    }
                }
                if (!launchedAny && QFileInfo::exists(entry.packagePath)) {
                    launchWidgetPackage(entry.packagePath, QRect(QPoint(260, 220), entry.defaultSize), entry.isDevelopment, entry.autoReload, nullptr, nullptr);
                }
            }
        }

        qInfo() << "[LinWidget][WidgetManager] Set widget enabled state:" << resolvedId << "enabled:" << targetEnabled;
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("enabled"), targetEnabled);
        return response;
    }

    if (action == QStringLiteral("launch") || action == QStringLiteral("load") || action == QStringLiteral("start")) {
        if (!m_enabled) {
            setEnabled(true);
            QSettings s(AppPaths::configFilePath(), QSettings::IniFormat);
            s.setValue(QStringLiteral("LinWidgets/Enabled"), true);
        }
        m_crashTimestamps.remove(resolvedId);
        m_crashTrippedWidgets.remove(resolvedId);

        WidgetRegistryEntry entry = registry.entryById(resolvedId);
        if (entry.id.isEmpty()) {
            for (const WidgetRegistryEntry& e : registry.entries()) {
                if (e.id.compare(resolvedId, Qt::CaseInsensitive) == 0) {
                    entry = e;
                    break;
                }
            }
        }

        QString pkgPath = entry.packagePath;
        if (pkgPath.isEmpty()) {
            pkgPath = request.value(QStringLiteral("packagePath")).toString();
        }
        if (pkgPath.isEmpty() || !QDir(pkgPath).exists()) {
            pkgPath = QDir(AppPaths::userLinWidgetsDir()).filePath(resolvedId);
            if (!QDir(pkgPath).exists()) {
                pkgPath = QStringLiteral(LINDESK_SOURCE_DIR) + QStringLiteral("/src/resources/LinWidgets/") + resolvedId;
            }
        }

        QString newInstId;
        QString launchErr;
        const QSize defSize = (entry.defaultSize.isValid() && entry.defaultSize.width() > 0) ? entry.defaultSize : QSize(340, 240);
        const QPoint launchPos(240 + (m_runningWidgets.size() * 32) % 320, 180 + (m_runningWidgets.size() * 32) % 240);
        const bool ok = launchWidgetPackage(pkgPath, QRect(launchPos, defSize), entry.isDevelopment, entry.autoReload, &newInstId, &launchErr);
        if (ok) {
            registry.setLastRunState(entry.id.isEmpty() ? resolvedId : entry.id, entry.isDevelopment, QStringLiteral("running"));
            registry.save();
        }
        response.insert(QStringLiteral("ok"), ok);
        response.insert(QStringLiteral("instanceId"), newInstId);
        if (!ok) response.insert(QStringLiteral("error"), launchErr.isEmpty() ? QStringLiteral("Failed to launch widget.") : launchErr);
        return response;
    }

    if (action == QStringLiteral("stop") || action == QStringLiteral("unload")) {
        int stoppedCount = 0;
        const QString targetInstId = request.value(QStringLiteral("instanceId")).toString().trimmed();
        if (!targetInstId.isEmpty()) {
            stopWidget(targetInstId);
            ++stoppedCount;
        } else {
            for (const WidgetLayoutState& state : m_layout.states()) {
                if (stateRegistryId(state) == resolvedId && m_runningWidgets.contains(state.instanceId)) {
                    stopWidget(state.instanceId);
                    ++stoppedCount;
                }
            }
            if (!m_activeWallpaperId.isEmpty() && m_activeWallpaperId.contains(resolvedId, Qt::CaseInsensitive)) {
                stopWallpaper(true);
                ++stoppedCount;
            }
        }
        WidgetRegistryEntry entry = registry.entryById(resolvedId);
        registry.setLastRunState(resolvedId, entry.isDevelopment, QStringLiteral("stopped"));
        registry.save();
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("stoppedCount"), stoppedCount);
        return response;
    }

    if (action == QStringLiteral("show_devtools") || action == QStringLiteral("devtools")) {
        QString targetInst = request.value(QStringLiteral("instanceId")).toString().trimmed();
        if (targetInst.isEmpty()) {
            for (const WidgetLayoutState& state : m_layout.states()) {
                if (stateRegistryId(state) == resolvedId && m_runningWidgets.contains(state.instanceId)) {
                    targetInst = state.instanceId;
                    break;
                }
            }
            if (targetInst.isEmpty() && !m_activeWallpaperId.isEmpty() && m_activeWallpaperId.contains(resolvedId, Qt::CaseInsensitive)) {
                targetInst = m_activeWallpaperId;
            }
        }
        bool devOk = false;
        if (!targetInst.isEmpty()) {
            devOk = showDevTools(targetInst);
        }
        response.insert(QStringLiteral("ok"), devOk);
        if (!devOk) response.insert(QStringLiteral("error"), QStringLiteral("Widget instance is not running or DevTools unavailable."));
        return response;
    }

    if (action == QStringLiteral("open_settings") || action == QStringLiteral("settings")) {
        QString targetInst = request.value(QStringLiteral("instanceId")).toString().trimmed();
        if (targetInst.isEmpty()) {
            for (const WidgetLayoutState& state : m_layout.states()) {
                if (stateRegistryId(state) == resolvedId) {
                    targetInst = state.instanceId;
                    break;
                }
            }
        }
        emit widgetSettingsRequested(targetInst.isEmpty() ? resolvedId : targetInst);
        response.insert(QStringLiteral("ok"), true);
        return response;
    }

    if (action == QStringLiteral("open_folder")) {
        QString folderPath = request.value(QStringLiteral("path")).toString().trimmed();
        if (folderPath.isEmpty()) {
            WidgetRegistryEntry entry = registry.entryById(resolvedId);
            folderPath = entry.packagePath;
        }
        if (folderPath.isEmpty() || !QDir(folderPath).exists()) {
            folderPath = AppPaths::userLinWidgetsDir();
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(folderPath));
        response.insert(QStringLiteral("ok"), true);
        return response;
    }

    if (action == QStringLiteral("apply_wallpaper")) {
        QString wallPath = request.value(QStringLiteral("packagePath")).toString().trimmed();
        if (wallPath.isEmpty()) {
            WidgetRegistryEntry entry = registry.entryById(resolvedId);
            wallPath = entry.packagePath;
        }
        if (wallPath.isEmpty()) {
            wallPath = QDir(AppPaths::userLinWidgetsDir()).filePath(resolvedId);
        }
        QString wallErr;
        const bool wOk = startWallpaper(wallPath, false, false, &wallErr);
        response.insert(QStringLiteral("ok"), wOk);
        if (!wOk) response.insert(QStringLiteral("error"), wallErr);
        return response;
    }

    if (action == QStringLiteral("set_wallpaper_prop")) {
        if (!m_activeWallpaperId.isEmpty()) {
            QJsonObject propMsg;
            propMsg.insert(QStringLiteral("type"), QStringLiteral("wallpaper_property"));
            propMsg.insert(QStringLiteral("property"), request.value(QStringLiteral("property")).toString());
            propMsg.insert(QStringLiteral("value"), request.value(QStringLiteral("value")));
            sendMessageToWidget(m_activeWallpaperId, propMsg);
            response.insert(QStringLiteral("ok"), true);
        } else {
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"), QStringLiteral("No active wallpaper running."));
        }
        return response;
    }

    if (action == QStringLiteral("uninstall") || action == QStringLiteral("delete")) {
        for (const WidgetLayoutState& state : m_layout.states()) {
            if (stateRegistryId(state) == resolvedId) {
                stopWidget(state.instanceId);
            }
        }
        WidgetRegistryEntry entry = registry.entryById(resolvedId);
        if (entry.isDevelopment) {
            registry.removeDevelopmentProject(resolvedId);
        } else {
            registry.removeById(resolvedId);
        }
        registry.save();
        response.insert(QStringLiteral("ok"), true);
        return response;
    }

    response.insert(QStringLiteral("ok"), false);
    response.insert(QStringLiteral("error"), QStringLiteral("Unsupported widget_manager action: %1").arg(action));
    return response;
}

void WidgetManager::handleRuntimeMessage(const QString& widgetId, const QJsonObject& message)
{
    const QString type = message.value(QStringLiteral("type")).toString();
    const QString effectiveInstanceId = message.value(QStringLiteral("instanceId")).toString(
        message.value(QStringLiteral("widgetId")).toString(widgetId)).trimmed();

    if (type == QString::fromLatin1(WidgetMessageType::WidgetManagerRequest)) {
        const QJsonObject response = handleWidgetManagerRequest(message);
        if (!effectiveInstanceId.isEmpty()) {
            sendMessageToWidget(effectiveInstanceId, response);
        }
        return;
    }

    if (type == QString::fromLatin1(WidgetMessageType::WidgetGeometryChanged)) {
        WidgetLayoutState state = m_layout.state(effectiveInstanceId);
        if (state.instanceId.isEmpty()) {
            return;
        }
        QRect nextGeometry(message.value(QStringLiteral("x")).toInt(state.geometry.x()),
                           message.value(QStringLiteral("y")).toInt(state.geometry.y()),
                           message.value(QStringLiteral("width")).toInt(state.geometry.width()),
                           message.value(QStringLiteral("height")).toInt(state.geometry.height()));
        WidgetManifest manifest;
        if (WidgetPackage::loadManifest(state.packagePath, &manifest, nullptr)) {
            nextGeometry.setSize(clampedWidgetSize(manifest, nextGeometry.size()));
        } else if (m_instances.contains(effectiveInstanceId)) {
            nextGeometry.setSize(clampedWidgetSize(m_instances.value(effectiveInstanceId).manifest, nextGeometry.size()));
        }
        state.geometry = nextGeometry;
        m_layout.setGeometry(effectiveInstanceId, state.geometry);
        if (m_instances.contains(effectiveInstanceId)) {
            WidgetInstance instance = m_instances.value(effectiveInstanceId);
            instance.geometry = state.geometry;
            m_instances.insert(effectiveInstanceId, instance);
        }
        if (auto* osr = m_osrBrowsers.value(effectiveInstanceId)) {
            osr->setGeometry(state.geometry);
        }
        scheduleSaveToConfig();
        return;
    }
    if (type == QString::fromLatin1(WidgetMessageType::WidgetError)) {
        emit widgetError(effectiveInstanceId, message.value(QStringLiteral("message")).toString());
        return;
    }
    if (type == QString::fromLatin1(WidgetMessageType::OpenWidgetSettings)) {
        if (!effectiveInstanceId.isEmpty() && m_instances.contains(effectiveInstanceId)) {
            emit widgetSettingsRequested(effectiveInstanceId);
        }
        return;
    }
    if (type == QString::fromLatin1(WidgetMessageType::SystemInfoRequest)) {
        handleSystemInfoRequest(message);
        return;
    }
    if (type == QStringLiteral("set_resize_ui_visible")) {
        // The host draws resize affordances itself (ListView edge hit-test); the flag is accepted for API compatibility.
        qDebug() << "[LinWidget][WidgetManager] set_resize_ui_visible from" << effectiveInstanceId
                 << "visible:" << message.value(QStringLiteral("visible")).toBool(true);
        return;
    }
    if (type == QString::fromLatin1(WidgetMessageType::StorageRequest)) {
        handleStorageRequest(message);
        return;
    }
    if (type == QString::fromLatin1(WidgetMessageType::CreateWidgetInstanceRequest)) {
        const QString sourceInstanceId = effectiveInstanceId;
        QRect requestedGeometry;
        const QJsonObject geometry = message.value(QStringLiteral("geometry")).toObject();
        if (!geometry.isEmpty()) {
            requestedGeometry = QRect(geometry.value(QStringLiteral("x")).toInt(),
                                      geometry.value(QStringLiteral("y")).toInt(),
                                      geometry.value(QStringLiteral("width")).toInt(),
                                      geometry.value(QStringLiteral("height")).toInt());
        }
        QString newInstanceId;
        QString errorMessage;
        const bool ok = createWidgetInstance(sourceInstanceId, requestedGeometry, &newInstanceId, &errorMessage);
        QJsonObject response = makeWidgetMessage(QString::fromLatin1(WidgetMessageType::CreateWidgetInstanceResponse), sourceInstanceId);
        response.insert(QStringLiteral("targetInstanceId"), sourceInstanceId);
        response.insert(QStringLiteral("requestId"), message.value(QStringLiteral("requestId")).toString());
        response.insert(QStringLiteral("ok"), ok);
        response.insert(QStringLiteral("instanceId"), newInstanceId);
        response.insert(QStringLiteral("error"), errorMessage);
        sendMessageToWidget(sourceInstanceId, response);
        return;
    }
    if (type == QString::fromLatin1(WidgetMessageType::ListWidgetInstancesRequest)) {
        const QString sourceInstanceId = effectiveInstanceId;
        const QString requestId = message.value(QStringLiteral("requestId")).toString();
        QJsonObject response = makeWidgetMessage(QString::fromLatin1(WidgetMessageType::ListWidgetInstancesResponse), sourceInstanceId);
        response.insert(QStringLiteral("targetInstanceId"), sourceInstanceId);
        response.insert(QStringLiteral("requestId"), requestId);
        QJsonArray instances;
        const WidgetInstance source = m_instances.value(sourceInstanceId);
        if (!source.instanceId.isEmpty()) {
            for (const WidgetLayoutState& state : m_layout.states()) {
                if (!sameWidgetFamily(source, state)) {
                    continue;
                }
                QJsonObject item;
                item.insert(QStringLiteral("instanceId"), state.instanceId);
                item.insert(QStringLiteral("id"), state.instanceId);
                item.insert(QStringLiteral("visible"), state.visible);
                item.insert(QStringLiteral("running"), m_runningWidgets.contains(state.instanceId));
                item.insert(QStringLiteral("parentInstanceId"), state.parentInstanceId);
                item.insert(QStringLiteral("createdAt"), state.createdAt);
                QJsonObject geometry;
                geometry.insert(QStringLiteral("x"), state.geometry.x());
                geometry.insert(QStringLiteral("y"), state.geometry.y());
                geometry.insert(QStringLiteral("width"), state.geometry.width());
                geometry.insert(QStringLiteral("height"), state.geometry.height());
                item.insert(QStringLiteral("geometry"), geometry);
                instances.append(item);
            }
            response.insert(QStringLiteral("ok"), true);
        } else {
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"), QStringLiteral("Source widget instance is not running."));
        }
        response.insert(QStringLiteral("instances"), instances);
        sendMessageToWidget(sourceInstanceId, response);
        return;
    }
    if (type == QString::fromLatin1(WidgetMessageType::CloseWidgetInstanceRequest)) {
        const QString sourceInstanceId = effectiveInstanceId;
        QString targetInstanceId = message.value(QStringLiteral("targetInstanceId")).toString().trimmed();
        if (targetInstanceId.isEmpty()) {
            targetInstanceId = sourceInstanceId;
        }
        const QString requestId = message.value(QStringLiteral("requestId")).toString();
        const WidgetInstance source = m_instances.value(sourceInstanceId);
        const WidgetLayoutState targetState = m_layout.state(targetInstanceId);
        const bool allowed = sameWidgetFamily(source, targetState);
        if (allowed && m_runningWidgets.contains(targetInstanceId)) {
            stopWidget(targetInstanceId);
        }
        QJsonObject response = makeWidgetMessage(QString::fromLatin1(WidgetMessageType::CloseWidgetInstanceResponse), sourceInstanceId);
        response.insert(QStringLiteral("targetInstanceId"), sourceInstanceId);
        response.insert(QStringLiteral("requestId"), requestId);
        response.insert(QStringLiteral("ok"), allowed);
        response.insert(QStringLiteral("instanceId"), targetInstanceId);
        if (!allowed) {
            response.insert(QStringLiteral("error"), QStringLiteral("Target widget instance is outside the caller scope."));
        }
        sendMessageToWidget(sourceInstanceId, response);
        return;
    }
    if (type == QString::fromLatin1(WidgetMessageType::RemoveWidgetInstanceRequest)) {
        const QString sourceInstanceId = effectiveInstanceId;
        QString targetInstanceId = message.value(QStringLiteral("targetInstanceId")).toString().trimmed();
        if (targetInstanceId.isEmpty()) {
            targetInstanceId = sourceInstanceId;
        }
        const QString requestId = message.value(QStringLiteral("requestId")).toString();
        const WidgetInstance source = m_instances.value(sourceInstanceId);
        const WidgetLayoutState targetState = m_layout.state(targetInstanceId);
        const bool allowed = sameWidgetFamily(source, targetState);
        if (allowed && (m_layout.contains(targetInstanceId) || m_runningWidgets.contains(targetInstanceId))) {
            removeWidgetState(targetInstanceId);
        }
        QJsonObject response = makeWidgetMessage(QString::fromLatin1(WidgetMessageType::RemoveWidgetInstanceResponse), sourceInstanceId);
        response.insert(QStringLiteral("targetInstanceId"), sourceInstanceId);
        response.insert(QStringLiteral("requestId"), requestId);
        response.insert(QStringLiteral("ok"), allowed);
        response.insert(QStringLiteral("instanceId"), targetInstanceId);
        if (!allowed) {
            response.insert(QStringLiteral("error"), QStringLiteral("Target widget instance is outside the caller scope."));
        }
        sendMessageToWidget(sourceInstanceId, response);
        return;
    }

    if (!m_runningWidgets.contains(effectiveInstanceId)) {
        return;
    }

    if (type == QString::fromLatin1(WidgetMessageType::MoveWidget)) {
        const WidgetInstance instance = m_instances.value(effectiveInstanceId);
        const QPoint pos(
            message.value(QStringLiteral("x")).toInt(instance.geometry.x()),
            message.value(QStringLiteral("y")).toInt(instance.geometry.y()));
        moveWidget(effectiveInstanceId, pos);
    } else if (type == QString::fromLatin1(WidgetMessageType::ResizeWidget)) {
        const WidgetInstance instance = m_instances.value(effectiveInstanceId);
        const QSize size(
            message.value(QStringLiteral("width")).toInt(instance.geometry.width()),
            message.value(QStringLiteral("height")).toInt(instance.geometry.height()));
        resizeWidget(effectiveInstanceId, size);
    } else if (type == QStringLiteral("wallpaper_set_interactive")) {
        const QString requestId = message.value(QStringLiteral("requestId")).toString();
        const bool interactive = message.value(QStringLiteral("interactive")).toBool(true);
        setWallpaperInteractive(interactive);

        QJsonObject response = makeWidgetMessage(QStringLiteral("wallpaper_interactive_changed"), effectiveInstanceId);
        response.insert(QStringLiteral("requestId"), requestId);
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("interactive"), interactive);
        sendMessageToWidget(effectiveInstanceId, response);
        return;
    } else if (type == QStringLiteral("wallpaper_get_interactive")) {
        const QString requestId = message.value(QStringLiteral("requestId")).toString();
        QJsonObject response = makeWidgetMessage(QStringLiteral("wallpaper_interactive_response"), effectiveInstanceId);
        response.insert(QStringLiteral("requestId"), requestId);
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("interactive"), m_activeWallpaperInteractive);
        sendMessageToWidget(effectiveInstanceId, response);
        return;
    } else if (type == QStringLiteral("wallpaper_set_source")) {
        const QString requestId = message.value(QStringLiteral("requestId")).toString();
        const bool isClear = message.value(QStringLiteral("clear")).toBool(false) ||
                             (message.contains(QStringLiteral("filePath")) &&
                              message.value(QStringLiteral("filePath")).toString().trimmed().isEmpty() &&
                              !message.value(QStringLiteral("dialog")).toBool(false));
        if (isClear) {
            QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
            settings.remove(QStringLiteral("LinWallpapers/%1/SourcePath").arg(effectiveInstanceId));
            settings.sync();

            QJsonObject response = makeWidgetMessage(QStringLiteral("wallpaper_source_changed"), effectiveInstanceId);
            response.insert(QStringLiteral("requestId"), requestId);
            response.insert(QStringLiteral("ok"), true);
            response.insert(QStringLiteral("sourcePath"), QString());
            sendMessageToWidget(effectiveInstanceId, response);
            return;
        }

        QString selectedPath = message.value(QStringLiteral("filePath")).toString().trimmed();
        if (selectedPath.isEmpty() && message.value(QStringLiteral("dialog")).toBool(false)) {
            QWidget* parentWidget = LinCef::LinWidgetCanvasWidget::hostWidget();
            selectedPath = QFileDialog::getOpenFileName(
                parentWidget,
                QStringLiteral("Select Wallpaper Media"),
                QString(),
                QStringLiteral("Media Files (*.mp4 *.webm *.png *.jpg *.jpeg *.bmp *.webp *.gif);;All Files (*.*)"));
        }
        if (!selectedPath.isEmpty()) {
            QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
            settings.setValue(QStringLiteral("LinWallpapers/%1/SourcePath").arg(effectiveInstanceId), selectedPath);
            settings.sync();

            QJsonObject response = makeWidgetMessage(QStringLiteral("wallpaper_source_changed"), effectiveInstanceId);
            response.insert(QStringLiteral("requestId"), requestId);
            response.insert(QStringLiteral("ok"), true);
            response.insert(QStringLiteral("sourcePath"), selectedPath);
            sendMessageToWidget(effectiveInstanceId, response);
        } else {
            QJsonObject response = makeWidgetMessage(QStringLiteral("wallpaper_source_changed"), effectiveInstanceId);
            response.insert(QStringLiteral("requestId"), requestId);
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"), QStringLiteral("No file selected."));
            sendMessageToWidget(effectiveInstanceId, response);
        }
        return;
    } else if (type == QStringLiteral("wallpaper_get_source")) {
        const QString requestId = message.value(QStringLiteral("requestId")).toString();
        QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
        const QString savedPath = settings.value(QStringLiteral("LinWallpapers/%1/SourcePath").arg(effectiveInstanceId)).toString();
        QJsonObject response = makeWidgetMessage(QStringLiteral("wallpaper_source_response"), effectiveInstanceId);
        response.insert(QStringLiteral("requestId"), requestId);
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("sourcePath"), savedPath);
        sendMessageToWidget(effectiveInstanceId, response);
    } else if (type == QStringLiteral("wallpaper_set_preset")) {
        const QString requestId = message.value(QStringLiteral("requestId")).toString();
        const QString preset = message.value(QStringLiteral("preset")).toString().trimmed();
        QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("LinWallpapers/%1/Preset").arg(effectiveInstanceId), preset);
        settings.sync();
        QJsonObject response = makeWidgetMessage(QStringLiteral("wallpaper_preset_changed"), effectiveInstanceId);
        response.insert(QStringLiteral("requestId"), requestId);
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("preset"), preset);
        sendMessageToWidget(effectiveInstanceId, response);
    } else if (type == QStringLiteral("wallpaper_get_preset")) {
        const QString requestId = message.value(QStringLiteral("requestId")).toString();
        QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
        const QString saved = settings.value(QStringLiteral("LinWallpapers/%1/Preset").arg(effectiveInstanceId), QStringLiteral("aurora")).toString();
        QJsonObject response = makeWidgetMessage(QStringLiteral("wallpaper_preset_response"), effectiveInstanceId);
        response.insert(QStringLiteral("requestId"), requestId);
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("preset"), saved);
        sendMessageToWidget(effectiveInstanceId, response);
    }
}

bool WidgetManager::sendMessageToWidget(const QString& instanceId, const QJsonObject& message)
{
    if (instanceId.isEmpty()) {
        return false;
    }
    if (auto* osr = m_osrBrowsers.value(instanceId)) {
        osr->sendMessageToWeb(message);
        return true;
    }
    qWarning() << "[LinWidget][WidgetManager] sendMessageToWidget dropped: no OSR browser for" << instanceId
               << "type:" << message.value(QStringLiteral("type")).toString();
    return false;
}

void WidgetManager::broadcastToWidgetInstances(const QString& widgetRegistryId, const QJsonObject& message)
{
    const QString targetId = widgetRegistryId.trimmed().toLower();
    for (const WidgetLayoutState& state : m_layout.states()) {
        if (state.registryId.trimmed().toLower() == targetId && m_runningWidgets.contains(state.instanceId)) {
            QJsonObject instanceMessage = message;
            instanceMessage.insert(QStringLiteral("instanceId"), state.instanceId);
            instanceMessage.insert(QStringLiteral("widgetId"), state.instanceId);
            sendMessageToWidget(state.instanceId, instanceMessage);
        }
    }
}

void WidgetManager::checkRuntimeHealth()
{
}

void WidgetManager::scheduleSaveToConfig()
{
    if (m_stoppingAll) {
        return;
    }
    m_saveTimer.start();
}

void WidgetManager::saveToDefaultConfig()
{
    if (m_stoppingAll) {
        return;
    }

    QString errorMessage;
    if (!saveToConfig(QString(), &errorMessage) && !errorMessage.isEmpty()) {
        emit widgetError(QString(), errorMessage);
    }
}

QString resolveOptimalPackagePath(const QString& inputPath, bool* isDevOut, bool isWallpaper = false)
{
    QString resolved = inputPath;
    bool isDev = isDevOut ? *isDevOut : false;

#ifdef LINDESK_SOURCE_DIR
    const QString sourceRoot = QStringLiteral(LINDESK_SOURCE_DIR);
    if (!sourceRoot.isEmpty() && QDir(sourceRoot).exists()) {
        WidgetManifest manifest;
        WidgetPackage::loadManifest(inputPath, &manifest, nullptr);
        const QString baseName = QFileInfo(inputPath).fileName();
        QStringList candidates;
        if (!baseName.isEmpty() && baseName != QStringLiteral(".")) {
            candidates.append(baseName);
        }
        if (!manifest.id.isEmpty() && !candidates.contains(manifest.id, Qt::CaseInsensitive)) {
            candidates.append(manifest.id);
            const QString lastPart = manifest.id.section(QLatin1Char('.'), -1);
            if (!lastPart.isEmpty() && !candidates.contains(lastPart, Qt::CaseInsensitive)) {
                candidates.append(lastPart);
            }
        }
        if (inputPath.contains(QStringLiteral("wallpaper"), Qt::CaseInsensitive) && !candidates.contains(QStringLiteral("Wallpaper"), Qt::CaseInsensitive)) {
            candidates.append(QStringLiteral("Wallpaper"));
        }

        QString foundSource;
        for (const QString& cand : candidates) {
            const QString sourceCandidate = QDir(sourceRoot).filePath(QStringLiteral("src/resources/LinWidgets/") + cand);
            if (QFileInfo::exists(sourceCandidate + QStringLiteral("/manifest.json"))) {
                foundSource = sourceCandidate;
                break;
            }
        }

        const QString appDir = QDir::cleanPath(QCoreApplication::applicationDirPath());
        const bool runningFromBuild = appDir.startsWith(QDir::cleanPath(sourceRoot), Qt::CaseInsensitive);
        const bool devModeEnv = qEnvironmentVariableIsSet("LINDESK_DEV_MODE");
#ifdef QT_DEBUG
        const bool isDebugBuild = true;
#else
        const bool isDebugBuild = false;
#endif

        if (!foundSource.isEmpty() && (devModeEnv || isDebugBuild || runningFromBuild)) {
            resolved = foundSource;
            isDev = true;
            qInfo() << "[LinWidget] Resolved package to development source tree path:" << resolved;
        }
    }
#endif

    if (!isDev && resolved.contains(QStringLiteral("AppData"), Qt::CaseInsensitive)) {
        WidgetManifest manifest;
        WidgetPackage::loadManifest(resolved, &manifest, nullptr);
        const QString baseName = QFileInfo(resolved).fileName();
        QStringList candidates;
        if (!baseName.isEmpty() && baseName != QStringLiteral(".")) {
            candidates.append(baseName);
        }
        if (!manifest.id.isEmpty() && !candidates.contains(manifest.id, Qt::CaseInsensitive)) {
            candidates.append(manifest.id);
            const QString lastPart = manifest.id.section(QLatin1Char('.'), -1);
            if (!lastPart.isEmpty() && !candidates.contains(lastPart, Qt::CaseInsensitive)) {
                candidates.append(lastPart);
            }
        }
        if (resolved.contains(QStringLiteral("wallpaper"), Qt::CaseInsensitive) && !candidates.contains(QStringLiteral("Wallpaper"), Qt::CaseInsensitive)) {
            candidates.append(QStringLiteral("Wallpaper"));
        }

        for (const QString& cand : candidates) {
            const QString bundledCandidate = QDir(QCoreApplication::applicationDirPath()).filePath(
                QStringLiteral("resources/LinWidgets/") + cand);
            if (QFileInfo::exists(bundledCandidate + QStringLiteral("/manifest.json"))) {
                if (isWallpaper) {
                    resolved = bundledCandidate;
                }
                break;
            }
        }
    }

    if (isDevOut) {
        *isDevOut = isDev;
    }
    return resolved;
}

bool WidgetManager::startWallpaper(const QString& packagePath, bool isDevelopment, bool interactive, QString* errorMessage)
{
    if (!m_enabled) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("LinWidgets runtime is disabled.");
        }
        return false;
    }

    if (!LinCef::CefManager::instance()->isCefAvailable() || !LinCef::CefManager::instance()->initialize()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("CEF OSR runtime is not available.");
        }
        return false;
    }

    bool resolvedIsDev = isDevelopment;
    const QString resolvedPackagePath = resolveOptimalPackagePath(packagePath, &resolvedIsDev, true);

    WidgetManifest manifest;
    if (!WidgetPackage::loadManifest(resolvedPackagePath, &manifest, errorMessage)) {
        return false;
    }

    if (manifest.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) != 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Package manifest role is not 'wallpaper'.");
        }
        return false;
    }

    const QString instanceId = QStringLiteral("wallpaper:") + manifest.id;
    if (m_activeWallpaperId == instanceId && m_osrBrowsers.contains(instanceId) && m_activeWallpaperPackagePath == resolvedPackagePath) {
        if (m_activeWallpaperInteractive != interactive) {
            setWallpaperInteractive(interactive);
        }
        return true;
    }

    // 单例管理：先关闭已有活动壁纸（不重置配置）
    if (!m_activeWallpaperId.isEmpty()) {
        stopWallpaper(false);
    }

    QRect viewportRect;
    if (LinCef::LinWidgetCanvasWidget::hostWidget() && LinCef::LinWidgetCanvasWidget::hostWidget()->rect().isValid()) {
        viewportRect = LinCef::LinWidgetCanvasWidget::hostWidget()->rect();
    } else if (m_canvasWidget && m_canvasWidget->rect().isValid() && m_canvasWidget->rect().width() > 100) {
        viewportRect = m_canvasWidget->rect();
    } else if (m_desktopLayerWindow) {
#ifdef Q_OS_WIN
        viewportRect = windowRect((HWND)m_desktopLayerWindow);
#endif
    }
    if (!viewportRect.isValid() || viewportRect.width() <= 100) {
        if (QGuiApplication::primaryScreen()) {
            viewportRect = QGuiApplication::primaryScreen()->geometry();
        } else {
            viewportRect = QRect(0, 0, 1920, 1080);
        }
    }

    QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
    const QVariant perWallpaperInteractive = settings.value(QStringLiteral("LinWallpapers/%1/Interactive").arg(manifest.id));
    const bool resolvedInteractive = perWallpaperInteractive.isValid()
        ? perWallpaperInteractive.toBool()
        : manifest.interactive;

    m_activeWallpaperId = instanceId;
    m_activeWallpaperPackagePath = resolvedPackagePath;
    m_activeWallpaperIsDevelopment = resolvedIsDev;
    m_activeWallpaperInteractive = resolvedInteractive;

    WidgetInstance instance;
    instance.instanceId = instanceId;
    instance.packagePath = resolvedPackagePath;
    instance.manifest = manifest;
    instance.isDevelopment = resolvedIsDev;
    instance.autoReload = resolvedIsDev;
    instance.grantedPermissions = grantedPermissionsForManifest(manifest, resolvedPackagePath, resolvedIsDev);
    instance.geometry = viewportRect;

    m_instances.insert(instanceId, instance);
    m_runningWidgets.insert(instanceId);
    configureDevelopmentWatcher(instance);

    auto* osr = new LinCef::CefOsrBrowserInstance(instanceId, this);
    osr->setPackagePath(resolvedPackagePath);
    osr->setAllowNetwork(manifest.permissions.contains(QStringLiteral("network")));

    connect(osr, &LinCef::CefOsrBrowserInstance::frameReady, this,
            [this](const QString& wid, const QRect& geom, const QImage& img, const QVector<QRect>& dirty) {
                if (m_canvasWidget) {
                    m_canvasWidget->updateWidgetFrame(wid, geom, img, dirty);
                }
            });
    connect(osr, &LinCef::CefOsrBrowserInstance::messageReceived, this,
            &WidgetManager::handleRuntimeMessage);
    connect(osr, &LinCef::CefOsrBrowserInstance::browserCrashed, this,
            &WidgetManager::handleBrowserCrashed);

    const QString entryFile = manifest.entry.isEmpty() ? QStringLiteral("index.html") : manifest.entry;
    const QString entryUrl = QStringLiteral("linwidget://%1/%2").arg(instanceId, entryFile);
    osr->create(entryUrl, viewportRect);
    m_osrBrowsers.insert(instanceId, osr);

    saveWallpaperConfig(resolvedPackagePath, resolvedIsDev, resolvedInteractive);
    updateDefaultCefPage();

    if (LinCef::LinWidgetCanvasWidget::hostWidget()) {
        LinCef::LinWidgetCanvasWidget::hostWidget()->update();
    }

    emit widgetLaunched(instanceId);
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

void WidgetManager::stopWallpaper(bool clearConfig)
{
    if (!m_activeWallpaperId.isEmpty()) {
        const QString oldId = m_activeWallpaperId;
        if (auto* osr = m_osrBrowsers.take(oldId)) {
            if (m_canvasWidget) {
                m_canvasWidget->removeWidgetFrame(oldId);
            }
            osr->close();
            osr->deleteLater();
        }
        m_runningWidgets.remove(oldId);
        stopSystemInfoWatchesForInstance(oldId);
        clearDevelopmentWatcher(oldId);
        m_instances.remove(oldId);
        emit widgetStopped(oldId);
    }

    m_activeWallpaperId.clear();
    m_activeWallpaperPackagePath.clear();
    m_activeWallpaperIsDevelopment = false;
    m_activeWallpaperInteractive = false;

    if (clearConfig) {
        saveWallpaperConfig(QString(), false, false);
    }

    if (LinCef::LinWidgetCanvasWidget::hostWidget()) {
        LinCef::LinWidgetCanvasWidget::hostWidget()->update();
    }

    updateDefaultCefPage();
}

void WidgetManager::setWallpaperInteractive(bool interactive)
{
    m_activeWallpaperInteractive = interactive;
    if (!m_activeWallpaperId.isEmpty()) {
        const WidgetInstance instance = m_instances.value(m_activeWallpaperId);
        const QString manifestId = !instance.manifest.id.isEmpty() ? instance.manifest.id : m_activeWallpaperId;
        QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("LinWallpapers/%1/Interactive").arg(manifestId), interactive);
        settings.sync();
    }
    if (!m_activeWallpaperPackagePath.isEmpty()) {
        saveWallpaperConfig(m_activeWallpaperPackagePath, m_activeWallpaperIsDevelopment, interactive);
    }
}

void WidgetManager::syncWallpaperWindowLayer()
{
    if (m_activeWallpaperId.isEmpty()) {
        return;
    }
    auto* osr = m_osrBrowsers.value(m_activeWallpaperId, nullptr);
    if (!osr) {
        return;
    }
    QRect viewportRect;
    if (LinCef::LinWidgetCanvasWidget::hostWidget() && LinCef::LinWidgetCanvasWidget::hostWidget()->rect().isValid()) {
        viewportRect = LinCef::LinWidgetCanvasWidget::hostWidget()->rect();
    } else if (m_canvasWidget && m_canvasWidget->rect().isValid() && m_canvasWidget->rect().width() > 100) {
        viewportRect = m_canvasWidget->rect();
    } else if (m_desktopLayerWindow) {
#ifdef Q_OS_WIN
        viewportRect = windowRect((HWND)m_desktopLayerWindow);
#endif
    }
    if (!viewportRect.isValid() || viewportRect.width() <= 100) {
        if (QGuiApplication::primaryScreen()) {
            viewportRect = QGuiApplication::primaryScreen()->geometry();
        } else {
            viewportRect = QRect(0, 0, 1920, 1080);
        }
    }

    if (viewportRect.isValid() && osr->geometry() != viewportRect) {
        osr->setGeometry(viewportRect);
        if (m_instances.contains(m_activeWallpaperId)) {
            m_instances[m_activeWallpaperId].geometry = viewportRect;
        }
    }
}

void WidgetManager::loadWallpaperConfig()
{
    QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
    const bool enabled = settings.value(QStringLiteral("LinWallpapers/Enabled"), false).toBool();
    if (!enabled) {
        return;
    }
    QString packagePath = settings.value(QStringLiteral("LinWallpapers/PackagePath")).toString();
    bool isDev = settings.value(QStringLiteral("LinWallpapers/IsDevelopment"), false).toBool();
    const bool interactive = settings.value(QStringLiteral("LinWallpapers/Interactive"), false).toBool();

    packagePath = resolveOptimalPackagePath(packagePath, &isDev, true);

    if (!packagePath.isEmpty() && QFileInfo::exists(packagePath)) {
        startWallpaper(packagePath, isDev, interactive, nullptr);
    }
}

void WidgetManager::saveWallpaperConfig(const QString& packagePath, bool isDevelopment, bool interactive)
{
    QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
    if (packagePath.isEmpty()) {
        settings.setValue(QStringLiteral("LinWallpapers/Enabled"), false);
        settings.remove(QStringLiteral("LinWallpapers/PackagePath"));
        settings.remove(QStringLiteral("LinWallpapers/IsDevelopment"));
        settings.remove(QStringLiteral("LinWallpapers/Interactive"));
    } else {
        settings.setValue(QStringLiteral("LinWallpapers/Enabled"), true);
        settings.setValue(QStringLiteral("LinWallpapers/PackagePath"), packagePath);
        settings.setValue(QStringLiteral("LinWallpapers/IsDevelopment"), isDevelopment);
        settings.setValue(QStringLiteral("LinWallpapers/Interactive"), interactive);
    }
    settings.sync();
}


bool WidgetManager::showDevTools(const QString& instanceId)
{
    auto* osr = m_osrBrowsers.value(instanceId, nullptr);
    if (!osr) {
        return false;
    }
    osr->showDevTools();
    return true;
}

} // namespace LinWidget


