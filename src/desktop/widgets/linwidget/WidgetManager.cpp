#include "desktop/widgets/linwidget/WidgetManager.h"

#include "desktop/widgets/linwidget/ConfigManager.h"
#include "desktop/widgets/linwidget/PermissionManager.h"
#include "desktop/widgets/linwidget/WidgetMessageTypes.h"
#include "desktop/widgets/linwidget/WidgetPackage.h"
#include "desktop/widgets/linwidget/WidgetRegistryManager.h"
#include "desktop/widgets/linwidget/LinWidgetWebViewRuntime.h"
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

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace LinWidget {
namespace {
constexpr int kRuntimeHealthCheckIntervalMs = 2500;
constexpr int kMaxWidgetInstancesPerRegistry = 3;
constexpr const char* kSharedHostId = "__shared__";

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
    return permissions.normalizePermissions(entry.permissionsGranted);
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
            name == QStringLiteral(".webview2") ||
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

WidgetManager::WidgetManager(QObject* parent)
    : QObject(parent)
{
#ifdef LINDESK_SOURCE_DIR
    ensureDevelopmentResources();
#endif

    m_runtimeHealthTimer.setInterval(kRuntimeHealthCheckIntervalMs);
    connect(&m_runtimeHealthTimer, &QTimer::timeout, this, &WidgetManager::checkRuntimeHealth);

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(350);
    connect(&m_saveTimer, &QTimer::timeout, this, &WidgetManager::saveToDefaultConfig);
}

WidgetManager::~WidgetManager()
{
    if (m_saveTimer.isActive()) {
        m_saveTimer.stop();
        saveToDefaultConfig();
    }
    qDeleteAll(m_systemInfoWatchTimers);
    m_systemInfoWatchTimers.clear();
    stopAll();
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
    if (!ensureSharedHost(errorMessage)) {
        return false;
    }

    m_layout.addOrUpdate(layoutState);
    m_instances.insert(instance.instanceId, instance);
    m_runningWidgets.insert(instance.instanceId);
    configureDevelopmentWatcher(instance);
    sendAddWidgetToSharedHost(instance, layoutState);
    syncAllWidgetWindowLayers();
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

    WidgetLayoutState state = m_layout.state(instanceId);
    if (state.instanceId.isEmpty()) {
        return false;
    }

    WidgetInstance instance = m_instances.value(instanceId);
    bool wasRunning = !instance.instanceId.isEmpty();

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
        QJsonObject removeMessage = makeWidgetMessage(QString::fromLatin1(WidgetMessageType::RemoveWidget), QString::fromLatin1(kSharedHostId));
        removeMessage.insert(QStringLiteral("instanceId"), instanceId);
        removeMessage.insert(QStringLiteral("widgetId"), instanceId);
        sendMessageToSharedRuntime(removeMessage);
        sendAddWidgetToSharedHost(instance, state);
        syncAllWidgetWindowLayers();
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

    stopAll();

    QVector<WidgetLayoutState> states;
    const QString path = configPath.isEmpty() ? ConfigManager::defaultConfigPath() : configPath;
    if (!ConfigManager::loadLayout(path, &states, errorMessage)) {
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
                const QString instanceId = instanceLayoutId(manifest, entry.isDevelopment);
                if (knownInstanceIds.contains(instanceId)) {
                    for (WidgetLayoutState& state : states) {
                        if (state.instanceId == instanceId) {
                            state.visible = true;
                            break;
                        }
                    }
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

    if (errorMessage && ok) {
        errorMessage->clear();
    }
    scheduleSaveToConfig();
    return ok;
}

bool WidgetManager::saveToConfig(const QString& configPath, QString* errorMessage) const
{
    const QString path = configPath.isEmpty() ? ConfigManager::defaultConfigPath() : configPath;
    return ConfigManager::saveLayout(path, deduplicatedLayoutStates(m_layout.states()), errorMessage);
}

QVector<WidgetLayoutState> WidgetManager::layoutStates() const
{
    QVector<WidgetLayoutState> states = deduplicatedLayoutStates(m_layout.states());
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

bool WidgetManager::moveWidget(const QString& instanceId, const QPoint& pos)
{
    if (!m_enabled) {
        return false;
    }

    WidgetLayoutState state = m_layout.state(instanceId);
    if (state.instanceId.isEmpty()) {
        return false;
    }
    state.geometry.moveTopLeft(pos);
    m_layout.setGeometry(instanceId, state.geometry);
    if (m_instances.contains(instanceId)) {
        WidgetInstance instance = m_instances.value(instanceId);
        instance.geometry = state.geometry;
        m_instances.insert(instanceId, instance);
    }
    sendGeometryToHost(instanceId, state.geometry);
    scheduleSaveToConfig();
    return true;
}

bool WidgetManager::resizeWidget(const QString& instanceId, const QSize& size)
{
    if (!m_enabled) {
        return false;
    }

    if (!size.isValid()) {
        return false;
    }
    WidgetLayoutState state = m_layout.state(instanceId);
    if (state.instanceId.isEmpty()) {
        return false;
    }
    WidgetManifest manifest;
    if (!WidgetPackage::loadManifest(state.packagePath, &manifest, nullptr)) {
        return false;
    }
    state.geometry.setSize(clampedWidgetSize(manifest, size));
    m_layout.setGeometry(instanceId, state.geometry);
    if (m_instances.contains(instanceId)) {
        WidgetInstance instance = m_instances.value(instanceId);
        instance.geometry = state.geometry;
        m_instances.insert(instanceId, instance);
    }
    sendGeometryToHost(instanceId, state.geometry);
    scheduleSaveToConfig();
    return true;
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
        m_widgetWindows.clear();
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
    m_desktopLayerWindow = layerWindow;
    syncAllWidgetWindowLayers();
}

WId WidgetManager::desktopLayerWindow() const
{
    return m_desktopLayerWindow;
}

void WidgetManager::setShowWebViewBounds(bool enabled)
{
    if (m_showWebViewBounds == enabled) {
        return;
    }
    m_showWebViewBounds = enabled;
    sendHostOptionsToSharedHost();
}

bool WidgetManager::showWebViewBounds() const
{
    return m_showWebViewBounds;
}

void WidgetManager::syncWidgetWindowLayer(const QString& instanceId)
{
#ifdef Q_OS_WIN
    const WId widgetWindowId = m_widgetWindows.value(QString::fromLatin1(kSharedHostId), m_widgetWindows.value(instanceId, 0));
    if (widgetWindowId == 0 || m_desktopLayerWindow == 0) {
        return;
    }

    HWND widgetWindow = reinterpret_cast<HWND>(static_cast<quintptr>(widgetWindowId));
    HWND layerWindow = reinterpret_cast<HWND>(static_cast<quintptr>(m_desktopLayerWindow));
    if (!IsWindow(widgetWindow) || !IsWindow(layerWindow)) {
        return;
    }

    if (IsIconic(widgetWindow)) {
        ShowWindow(widgetWindow, SW_RESTORE);
    }
    if (!IsWindowVisible(widgetWindow)) {
        ShowWindow(widgetWindow, SW_SHOWNOACTIVATE);
    }

    RECT layerNativeRect = {};
    if (!GetWindowRect(layerWindow, &layerNativeRect)) {
        return;
    }
    RECT layerClientRect = {};
    GetClientRect(layerWindow, &layerClientRect);
    const bool inProcessSharedRuntime = m_webViewRuntime != nullptr;
    const QRect nativeLayerGeometry = inProcessSharedRuntime
        ? QRect(0,
                0,
                qMax(0L, layerClientRect.right - layerClientRect.left),
                qMax(0L, layerClientRect.bottom - layerClientRect.top))
        : windowRect(layerWindow);
    QRect logicalLayerGeometry = bestLogicalLayerRect(layerWindow, layerNativeRect);
    if (inProcessSharedRuntime) {
        logicalLayerGeometry.moveTopLeft(QPoint(0, 0));
    }

    ShowWindow(widgetWindow, SW_SHOWNOACTIVATE);

    UINT setWindowFlags = SWP_SHOWWINDOW | SWP_FRAMECHANGED | SWP_NOACTIVATE;

    const BOOL ok = SetWindowPos(widgetWindow,
                                 inProcessSharedRuntime ? HWND_TOP : layerWindow,
                                 nativeLayerGeometry.x(),
                                 nativeLayerGeometry.y(),
                                 nativeLayerGeometry.width(),
                                 nativeLayerGeometry.height(),
                                 setWindowFlags);
    if (ok) {
        QJsonObject hostGeometryMessage = makeWidgetMessage(QString::fromLatin1(WidgetMessageType::UpdateHostGeometry), QString::fromLatin1(kSharedHostId));
        hostGeometryMessage.insert(QStringLiteral("x"), logicalLayerGeometry.x());
        hostGeometryMessage.insert(QStringLiteral("y"), logicalLayerGeometry.y());
        hostGeometryMessage.insert(QStringLiteral("width"), logicalLayerGeometry.width());
        hostGeometryMessage.insert(QStringLiteral("height"), logicalLayerGeometry.height());
        sendMessageToSharedRuntime(hostGeometryMessage);
    }
    if (!ok) {
        appendSharedLog(QStringLiteral("layer"),
                        QStringLiteral("sync failed id=%1 ok=%2")
                            .arg(instanceId,
                                 ok ? QStringLiteral("true") : QStringLiteral("false")));
        qDebug() << "[LinWidgetLayer] sync"
                 << "id=" << instanceId
                 << "ok=" << ok;
    }
#else
#endif
}

void WidgetManager::syncAllWidgetWindowLayers()
{
    syncWidgetWindowLayer(QString::fromLatin1(kSharedHostId));
}

bool WidgetManager::ensureSharedHost(QString* errorMessage)
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
    if (m_webViewRuntime) {
        return true;
    }

    WidgetManifest manifest;
    manifest.id = QString::fromLatin1(kSharedHostId);
    manifest.name = QStringLiteral("LinWidget Shared Runtime");
    manifest.width = 1200;
    manifest.height = 760;

    WidgetInstance instance;
    instance.instanceId = QString::fromLatin1(kSharedHostId);
    instance.manifest = manifest;
    instance.geometry = QRect(0, 0, manifest.width, manifest.height);

    WidgetInstance sharedInstance = instance;
    sharedInstance.geometry = QRect(80, 80, manifest.width, manifest.height);
    const WId parentWindow = m_desktopLayerWindow != 0 ? m_desktopLayerWindow : m_hostParentWindow;
    auto* runtime = new LinWidgetWebViewRuntime(QString::fromLatin1(kSharedHostId),
                                                QString(),
                                                sharedInstance.geometry,
                                                static_cast<quint64>(parentWindow),
                                                QStringList(),
                                                false,
                                                false,
                                                true,
                                                nullptr);
    m_webViewRuntime = runtime;
    connect(runtime, &LinWidgetWebViewRuntime::messageToManager,
            this, &WidgetManager::handleRuntimeMessage);

    QString runtimeError;
    if (!runtime->initialize(&runtimeError)) {
        m_webViewRuntime = nullptr;
        runtime->deleteLater();
        if (errorMessage) {
            *errorMessage = runtimeError.isEmpty()
                ? QStringLiteral("Failed to start LinWidget WebView runtime.")
                : runtimeError;
        }
        return false;
    }
    sendHostOptionsToSharedHost();
    return true;
}

void WidgetManager::restartSharedHost()
{
    if (!m_enabled) {
        return;
    }

    if (m_webViewRuntime) {
        LinWidgetWebViewRuntime* runtime = m_webViewRuntime;
        m_webViewRuntime = nullptr;
        m_widgetWindows.remove(QString::fromLatin1(kSharedHostId));
        m_sharedWebViewWindow = 0;
        m_sharedWebViewWindowClass.clear();
        runtime->close();
        runtime->deleteLater();
    }
    QString errorMessage;
    if (!ensureSharedHost(&errorMessage)) {
        emit widgetError(QString(), errorMessage);
        return;
    }
    const QVector<WidgetLayoutState> states = m_layout.states();
    for (const WidgetLayoutState& state : states) {
        if (!state.visible || !m_instances.contains(state.instanceId)) {
            continue;
        }
        WidgetInstance instance = m_instances.value(state.instanceId);
        QString manifestError;
        if (!WidgetPackage::loadManifest(instance.packagePath, &instance.manifest, &manifestError)) {
            emit widgetError(instance.instanceId, QStringLiteral("Restore blocked: %1").arg(manifestError));
            continue;
        }
        m_instances.insert(instance.instanceId, instance);
        m_runningWidgets.insert(state.instanceId);
        sendAddWidgetToSharedHost(instance, state);
    }
}

void WidgetManager::sendHostOptionsToSharedHost()
{
    if (!m_webViewRuntime) {
        return;
    }

    QJsonObject message = makeWidgetMessage(QString::fromLatin1(WidgetMessageType::UpdateHostOptions), QString::fromLatin1(kSharedHostId));
    message.insert(QStringLiteral("showWebViewBounds"), m_showWebViewBounds);
    sendMessageToSharedRuntime(message);
}

bool WidgetManager::sendAddWidgetToSharedHost(const WidgetInstance& instance, const WidgetLayoutState& layoutState)
{
    QJsonObject geometry;
    geometry.insert(QStringLiteral("x"), layoutState.geometry.x());
    geometry.insert(QStringLiteral("y"), layoutState.geometry.y());
    geometry.insert(QStringLiteral("width"), layoutState.geometry.width());
    geometry.insert(QStringLiteral("height"), layoutState.geometry.height());

    QJsonArray permissions;
    for (const QString& permission : instance.grantedPermissions) {
        permissions.append(permission);
    }

    QJsonObject message = makeWidgetMessage(QString::fromLatin1(WidgetMessageType::AddWidget), QString::fromLatin1(kSharedHostId));
    message.insert(QStringLiteral("instanceId"), instance.instanceId);
    message.insert(QStringLiteral("packagePath"), instance.packagePath);
    message.insert(QStringLiteral("geometry"), geometry);
    message.insert(QStringLiteral("permissions"), permissions);
    message.insert(QStringLiteral("registryId"), instance.manifest.id.trimmed().toLower());
    message.insert(QStringLiteral("multiInstance"), instance.manifest.multiInstance);
    message.insert(QStringLiteral("parentInstanceId"), layoutState.parentInstanceId);
    message.insert(QStringLiteral("development"), instance.isDevelopment);
    message.insert(QStringLiteral("autoReload"), instance.autoReload);
    message.insert(QStringLiteral("zOrder"), layoutState.zOrder);
    return sendMessageToSharedRuntime(message);
}

void WidgetManager::stopWidget(const QString& instanceId)
{
    if (!m_runningWidgets.contains(instanceId)) {
        return;
    }
    m_runningWidgets.remove(instanceId);
    stopSystemInfoWatchesForInstance(instanceId);
    clearDevelopmentWatcher(instanceId);
    m_instances.remove(instanceId);
    QJsonObject message = makeWidgetMessage(QString::fromLatin1(WidgetMessageType::RemoveWidget), QString::fromLatin1(kSharedHostId));
    message.insert(QStringLiteral("instanceId"), instanceId);
    message.insert(QStringLiteral("widgetId"), instanceId);
    sendMessageToSharedRuntime(message);
    syncAllWidgetWindowLayers();
    if (!m_stoppingAll) {
        m_layout.setVisible(instanceId, false);
    }
    if (!m_stoppingAll) {
        saveToDefaultConfig();
    }
    m_widgetWindows.remove(instanceId);
    emit widgetStopped(instanceId);
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
            name == QStringLiteral(".webview2") ||
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

void WidgetManager::stopAll()
{
    const bool wasStoppingAll = m_stoppingAll;
    m_stoppingAll = true;
    const QStringList ids = m_runningWidgets.values();
    for (const QString& id : ids) {
        stopWidget(id);
    }
    m_runningWidgets.clear();
    m_instances.clear();
    qDeleteAll(m_systemInfoWatchTimers);
    m_systemInfoWatchTimers.clear();
    if (m_webViewRuntime) {
        LinWidgetWebViewRuntime* runtime = m_webViewRuntime;
        m_webViewRuntime = nullptr;
        runtime->close();
        runtime->deleteLater();
    }
    m_widgetWindows.remove(QString::fromLatin1(kSharedHostId));
    m_sharedWebViewWindow = 0;
    m_sharedWebViewWindowClass.clear();
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
        sendSystemInfoResponse(instanceId, requestId, action, false, QJsonObject(), QStringLiteral("Widget instance is not running."));
        return;
    }
    if (!m_permissions.canUseDesktopApi(instance.grantedPermissions)) {
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
    QJsonObject message = makeWidgetMessage(QString::fromLatin1(WidgetMessageType::SystemInfoResponse), QString::fromLatin1(kSharedHostId));
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
    sendMessageToSharedRuntime(message);
}

void WidgetManager::sendSystemInfoUpdate(const QString& instanceId,
                                         const QString& watchId,
                                         const QJsonObject& data)
{
    QJsonObject message = makeWidgetMessage(QString::fromLatin1(WidgetMessageType::SystemInfoUpdate), QString::fromLatin1(kSharedHostId));
    message.insert(QStringLiteral("instanceId"), instanceId);
    message.insert(QStringLiteral("widgetId"), instanceId);
    message.insert(QStringLiteral("watchId"), watchId);
    message.insert(QStringLiteral("data"), data);
    sendMessageToSharedRuntime(message);
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
    QJsonObject message = makeWidgetMessage(QString::fromLatin1(WidgetMessageType::StorageResponse), QString::fromLatin1(kSharedHostId));
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
    sendMessageToSharedRuntime(message);
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

void WidgetManager::handleRuntimeMessage(const QString& widgetId, const QJsonObject& message)
{
    const QString type = message.value(QStringLiteral("type")).toString();
    if (widgetId == QString::fromLatin1(kSharedHostId)) {
        if (m_webViewRuntime && type == QString::fromLatin1(WidgetMessageType::Ready)) {
            sendHostOptionsToSharedHost();
            const QVector<WidgetLayoutState> states = m_layout.states();
            for (const WidgetLayoutState& state : states) {
                if (state.visible && m_instances.contains(state.instanceId)) {
                    sendAddWidgetToSharedHost(m_instances.value(state.instanceId), state);
                }
            }
            return;
        }
        if (type == QString::fromLatin1(WidgetMessageType::HostWindowReady)) {
            const WId windowId = static_cast<WId>(message.value(QStringLiteral("hwnd")).toString().toULongLong());
            if (windowId != 0) {
                m_widgetWindows.insert(QString::fromLatin1(kSharedHostId), windowId);
                syncWidgetWindowLayer(QString::fromLatin1(kSharedHostId));
            }
            return;
        }
        if (type == QString::fromLatin1(WidgetMessageType::WebViewWindowReady)) {
            const WId windowId = static_cast<WId>(message.value(QStringLiteral("hwnd")).toString().toULongLong());
            if (windowId != 0) {
                m_sharedWebViewWindow = windowId;
                m_sharedWebViewWindowClass = message.value(QStringLiteral("className")).toString();
                syncWidgetWindowLayer(QString::fromLatin1(kSharedHostId));
            }
            return;
        }
        if (type == QString::fromLatin1(WidgetMessageType::WebViewWindowLost)) {
            m_sharedWebViewWindow = 0;
            m_sharedWebViewWindowClass.clear();
            qDebug() << "[LinWidgetLayer] WebView child window lost"
                     << "reason=" << message.value(QStringLiteral("reason")).toString();
            if (visibleWidgetCount() > 0) {
                emit widgetError(QString(), QStringLiteral("Shared WebView child window was lost. Restarting widgets."));
                restartSharedHost();
            }
            return;
        }
        if (type == QString::fromLatin1(WidgetMessageType::WidgetGeometryChanged)) {
            const QString instanceId = message.value(QStringLiteral("instanceId")).toString(message.value(QStringLiteral("widgetId")).toString());
            WidgetLayoutState state = m_layout.state(instanceId);
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
            }
            state.geometry = nextGeometry;
            m_layout.setGeometry(instanceId, state.geometry);
            if (m_instances.contains(instanceId)) {
                WidgetInstance instance = m_instances.value(instanceId);
                instance.geometry = state.geometry;
                m_instances.insert(instanceId, instance);
            }
            sendGeometryToHost(instanceId, state.geometry);
            scheduleSaveToConfig();
            return;
        }
        if (type == QString::fromLatin1(WidgetMessageType::WidgetError)) {
            emit widgetError(message.value(QStringLiteral("instanceId")).toString(), message.value(QStringLiteral("message")).toString());
            return;
        }
        if (type == QString::fromLatin1(WidgetMessageType::OpenWidgetSettings)) {
            const QString instanceId = message.value(QStringLiteral("instanceId")).toString(message.value(QStringLiteral("widgetId")).toString()).trimmed();
            if (!instanceId.isEmpty() && m_instances.contains(instanceId)) {
                emit widgetSettingsRequested(instanceId);
            }
            return;
        }
        if (type == QString::fromLatin1(WidgetMessageType::SystemInfoRequest)) {
            handleSystemInfoRequest(message);
            return;
        }
        if (type == QString::fromLatin1(WidgetMessageType::StorageRequest)) {
            handleStorageRequest(message);
            return;
        }
        if (type == QString::fromLatin1(WidgetMessageType::CreateWidgetInstanceRequest)) {
            const QString sourceInstanceId = message.value(QStringLiteral("instanceId")).toString(message.value(QStringLiteral("widgetId")).toString()).trimmed();
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
            QJsonObject response = makeWidgetMessage(QString::fromLatin1(WidgetMessageType::CreateWidgetInstanceResponse), QString::fromLatin1(kSharedHostId));
            response.insert(QStringLiteral("targetInstanceId"), sourceInstanceId);
            response.insert(QStringLiteral("requestId"), message.value(QStringLiteral("requestId")).toString());
            response.insert(QStringLiteral("ok"), ok);
            response.insert(QStringLiteral("instanceId"), newInstanceId);
            response.insert(QStringLiteral("error"), errorMessage);
            sendMessageToSharedRuntime(response);
            return;
        }
        if (type == QString::fromLatin1(WidgetMessageType::ListWidgetInstancesRequest)) {
            const QString sourceInstanceId = message.value(QStringLiteral("instanceId")).toString(message.value(QStringLiteral("widgetId")).toString()).trimmed();
            const QString requestId = message.value(QStringLiteral("requestId")).toString();
            QJsonObject response = makeWidgetMessage(QString::fromLatin1(WidgetMessageType::ListWidgetInstancesResponse), QString::fromLatin1(kSharedHostId));
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
            sendMessageToSharedRuntime(response);
            return;
        }
        if (type == QString::fromLatin1(WidgetMessageType::CloseWidgetInstanceRequest)) {
            const QString sourceInstanceId = message.value(QStringLiteral("instanceId")).toString(message.value(QStringLiteral("widgetId")).toString()).trimmed();
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
            QJsonObject response = makeWidgetMessage(QString::fromLatin1(WidgetMessageType::CloseWidgetInstanceResponse), QString::fromLatin1(kSharedHostId));
            response.insert(QStringLiteral("targetInstanceId"), sourceInstanceId);
            response.insert(QStringLiteral("requestId"), requestId);
            response.insert(QStringLiteral("ok"), allowed);
            response.insert(QStringLiteral("instanceId"), targetInstanceId);
            if (!allowed) {
                response.insert(QStringLiteral("error"), QStringLiteral("Target widget instance is outside the caller scope."));
            }
            sendMessageToSharedRuntime(response);
            return;
        }
        if (type == QString::fromLatin1(WidgetMessageType::RemoveWidgetInstanceRequest)) {
            const QString sourceInstanceId = message.value(QStringLiteral("instanceId")).toString(message.value(QStringLiteral("widgetId")).toString()).trimmed();
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
            QJsonObject response = makeWidgetMessage(QString::fromLatin1(WidgetMessageType::RemoveWidgetInstanceResponse), QString::fromLatin1(kSharedHostId));
            response.insert(QStringLiteral("targetInstanceId"), sourceInstanceId);
            response.insert(QStringLiteral("requestId"), requestId);
            response.insert(QStringLiteral("ok"), allowed);
            response.insert(QStringLiteral("instanceId"), targetInstanceId);
            if (!allowed) {
                response.insert(QStringLiteral("error"), QStringLiteral("Target widget instance is outside the caller scope."));
            }
            sendMessageToSharedRuntime(response);
            return;
        }
    }

    if (!m_runningWidgets.contains(widgetId)) {
        return;
    }

    if (type == QString::fromLatin1(WidgetMessageType::MoveWidget)) {
        const WidgetInstance instance = m_instances.value(widgetId);
        const QPoint pos(
            message.value(QStringLiteral("x")).toInt(instance.geometry.x()),
            message.value(QStringLiteral("y")).toInt(instance.geometry.y()));
        moveWidget(widgetId, pos);
    } else if (type == QString::fromLatin1(WidgetMessageType::ResizeWidget)) {
        const WidgetInstance instance = m_instances.value(widgetId);
        const QSize size(
            message.value(QStringLiteral("width")).toInt(instance.geometry.width()),
            message.value(QStringLiteral("height")).toInt(instance.geometry.height()));
        resizeWidget(widgetId, size);
    } else if (type == QString::fromLatin1(WidgetMessageType::HostWindowReady)) {
        const WId windowId = static_cast<WId>(message.value(QStringLiteral("hwnd")).toString().toULongLong());
        if (windowId != 0) {
            qDebug() << "[LinWidgetLayer] host window ready"
                     << "id=" << widgetId
                     << "hwnd=" << QString::number(static_cast<quintptr>(windowId), 16);
            m_widgetWindows.insert(widgetId, windowId);
            syncWidgetWindowLayer(widgetId);
        }
    }
}

void WidgetManager::sendGeometryToHost(const QString& instanceId, const QRect& geometry)
{
    QJsonObject message = makeWidgetMessage(QString::fromLatin1(WidgetMessageType::UpdateWidgetGeometry), QString::fromLatin1(kSharedHostId));
    message.insert(QStringLiteral("instanceId"), instanceId);
    message.insert(QStringLiteral("x"), geometry.x());
    message.insert(QStringLiteral("y"), geometry.y());
    message.insert(QStringLiteral("width"), geometry.width());
    message.insert(QStringLiteral("height"), geometry.height());
    sendMessageToSharedRuntime(message);
}

void WidgetManager::broadcastToWidgetInstances(const QString& widgetRegistryId, const QJsonObject& message)
{
    const QString targetId = widgetRegistryId.trimmed().toLower();
    for (const WidgetLayoutState& state : m_layout.states()) {
        if (state.registryId.trimmed().toLower() == targetId && m_runningWidgets.contains(state.instanceId)) {
            QJsonObject instanceMessage = message;
            instanceMessage.insert(QStringLiteral("instanceId"), state.instanceId);
            instanceMessage.insert(QStringLiteral("widgetId"), state.instanceId);
            sendMessageToSharedRuntime(instanceMessage);
        }
    }
}

bool WidgetManager::sendMessageToSharedRuntime(const QJsonObject& message)
{
    if (!m_webViewRuntime) {
        return false;
    }
    m_webViewRuntime->handleManagerMessage(message);
    return true;
}

void WidgetManager::resendVisibleWidgetsToSharedHost()
{
    if (!m_webViewRuntime) {
        return;
    }
    const QVector<WidgetLayoutState> states = m_layout.states();
    for (const WidgetLayoutState& state : states) {
        if (!state.visible || !m_instances.contains(state.instanceId)) {
            continue;
        }
        sendAddWidgetToSharedHost(m_instances.value(state.instanceId), state);
    }
}

int WidgetManager::visibleWidgetCount() const
{
    int count = 0;
    const QVector<WidgetLayoutState> states = m_layout.states();
    for (const WidgetLayoutState& state : states) {
        if (state.visible && state.geometry.isValid()) {
            ++count;
        }
    }
    return count;
}

bool WidgetManager::sharedWebViewMissingForVisibleWidgets() const
{
#ifdef Q_OS_WIN
    if (visibleWidgetCount() == 0) {
        return false;
    }
    HWND hostWindow = reinterpret_cast<HWND>(static_cast<quintptr>(m_widgetWindows.value(QString::fromLatin1(kSharedHostId), 0)));
    HWND webViewWindow = reinterpret_cast<HWND>(static_cast<quintptr>(m_sharedWebViewWindow));
    return hostWindow && IsWindow(hostWindow) && (!webViewWindow || !IsWindow(webViewWindow));
#else
    return false;
#endif
}

bool WidgetManager::sharedHostLayerNeedsRecovery() const
{
#ifdef Q_OS_WIN
    HWND hostWindow = reinterpret_cast<HWND>(static_cast<quintptr>(m_widgetWindows.value(QString::fromLatin1(kSharedHostId), 0)));
    HWND layerWindow = reinterpret_cast<HWND>(static_cast<quintptr>(m_desktopLayerWindow));
    if (!hostWindow || !layerWindow || !IsWindow(hostWindow) || !IsWindow(layerWindow)) {
        return false;
    }
    if (IsIconic(hostWindow) || !IsWindowVisible(hostWindow)) {
        return true;
    }
    const bool inProcessSharedRuntime = m_webViewRuntime != nullptr;
    if (inProcessSharedRuntime) {
        RECT layerClientRect = {};
        RECT hostWindowRect = {};
        if (!GetClientRect(layerWindow, &layerClientRect) || !GetWindowRect(hostWindow, &hostWindowRect)) {
            return true;
        }
        POINT hostTopLeft { hostWindowRect.left, hostWindowRect.top };
        ScreenToClient(layerWindow, &hostTopLeft);
        const QRect expectedRect(0,
                                 0,
                                 qMax(0L, layerClientRect.right - layerClientRect.left),
                                 qMax(0L, layerClientRect.bottom - layerClientRect.top));
        const QRect actualRect(hostTopLeft.x,
                               hostTopLeft.y,
                               qMax(0L, hostWindowRect.right - hostWindowRect.left),
                               qMax(0L, hostWindowRect.bottom - hostWindowRect.top));
        return actualRect != expectedRect || sharedWebViewMissingForVisibleWidgets();
    }
    const bool layerMismatch = GetWindow(layerWindow, GW_HWNDPREV) != hostWindow;
    return layerMismatch ||
        windowRect(hostWindow) != windowRect(layerWindow) ||
        sharedWebViewMissingForVisibleWidgets();
#else
    return false;
#endif
}

void WidgetManager::checkRuntimeHealth()
{
    if (m_webViewRuntime && sharedHostLayerNeedsRecovery()) {
        if (sharedWebViewMissingForVisibleWidgets()) {
            qDebug() << "[LinWidgetLayer] runtime recovery restarting shared host because WebView child is missing";
            emit widgetError(QString(), QStringLiteral("Shared WebView child window is missing. Restarting widgets."));
            restartSharedHost();
            return;
        }
        syncAllWidgetWindowLayers();
    }
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

}


