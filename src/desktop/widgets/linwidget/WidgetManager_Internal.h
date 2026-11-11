#pragma once

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

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPainter>
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
#include <QUuid>
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

inline QString resolveOptimalPackagePath(const QString& inputPath, bool* isDevOut, bool isWallpaper = false)
{
    QString resolved = inputPath;
    bool isDev = isDevOut ? *isDevOut : false;

    WidgetManifest manifest;
    const bool manifestLoaded = WidgetPackage::loadManifest(inputPath, &manifest, nullptr);

#ifdef LINDESK_SOURCE_DIR
    const QString sourceRoot = QStringLiteral(LINDESK_SOURCE_DIR);
    if (!sourceRoot.isEmpty() && QDir(sourceRoot).exists()) {
        const QString baseName = QFileInfo(inputPath).fileName();
        QStringList candidates;
        if (!baseName.isEmpty() && baseName != QStringLiteral(".")) {
            candidates.append(baseName);
        }
        if (manifestLoaded && !manifest.id.isEmpty()) {
            if (!candidates.contains(manifest.id, Qt::CaseInsensitive)) {
                candidates.append(manifest.id);
            }
            const QString lastPart = manifest.id.section(QLatin1Char('.'), -1);
            if (!lastPart.isEmpty() && !candidates.contains(lastPart, Qt::CaseInsensitive)) {
                candidates.append(lastPart);
            }
        }

        QString foundSource;
        for (const QString& cand : candidates) {
            if (cand.isEmpty()) continue;
            if (isWallpaper) {
                const QString wallCandidate = QDir(sourceRoot).filePath(QStringLiteral("src/resources/wallpapers/") + cand);
                if (QFileInfo::exists(wallCandidate + QStringLiteral("/manifest.json"))) {
                    foundSource = wallCandidate;
                    break;
                }
            }
            const QString rootCandidate = QDir(sourceRoot).filePath(QStringLiteral("widgets/") + cand);
            if (QFileInfo::exists(rootCandidate + QStringLiteral("/manifest.json"))) {
                foundSource = rootCandidate;
                break;
            }
            const QString sourceCandidate = QDir(sourceRoot).filePath(QStringLiteral("src/resources/LinWidgets/") + cand);
            if (QFileInfo::exists(sourceCandidate + QStringLiteral("/manifest.json"))) {
                foundSource = sourceCandidate;
                break;
            }
            if (!isWallpaper) {
                const QString wallCandidate = QDir(sourceRoot).filePath(QStringLiteral("src/resources/wallpapers/") + cand);
                if (QFileInfo::exists(wallCandidate + QStringLiteral("/manifest.json"))) {
                    foundSource = wallCandidate;
                    break;
                }
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
        const QString baseName = QFileInfo(resolved).fileName();
        QStringList candidates;
        if (!baseName.isEmpty() && baseName != QStringLiteral(".")) {
            candidates.append(baseName);
        }
        if (manifestLoaded && !manifest.id.isEmpty()) {
            if (!candidates.contains(manifest.id, Qt::CaseInsensitive)) {
                candidates.append(manifest.id);
            }
            const QString lastPart = manifest.id.section(QLatin1Char('.'), -1);
            if (!lastPart.isEmpty() && !candidates.contains(lastPart, Qt::CaseInsensitive)) {
                candidates.append(lastPart);
            }
        }

        for (const QString& cand : candidates) {
            if (cand.isEmpty()) continue;
            if (isWallpaper) {
                const QString bundledWall = QDir(QCoreApplication::applicationDirPath()).filePath(
                    QStringLiteral("resources/wallpapers/") + cand);
                if (QFileInfo::exists(bundledWall + QStringLiteral("/manifest.json"))) {
                    resolved = bundledWall;
                    break;
                }
            }
            const QString bundledCandidate = QDir(QCoreApplication::applicationDirPath()).filePath(
                QStringLiteral("resources/LinWidgets/") + cand);
            if (QFileInfo::exists(bundledCandidate + QStringLiteral("/manifest.json"))) {
                resolved = bundledCandidate;
                break;
            }
        }
    }

    if (isDevOut) {
        *isDevOut = isDev;
    }
    return resolved;
}

inline QString customWallpapersRegistryPath() {
    return QDir(AppPaths::userWallpapersDir()).filePath(QStringLiteral("custom_wallpapers.json"));
}

inline QString customWallpapersImagesDir() {
    const QString d = QDir(AppPaths::userWallpapersDir()).filePath(QStringLiteral("images"));
    QDir().mkpath(d);
    return d;
}

inline QString customWallpapersVideosDir() {
    const QString d = QDir(AppPaths::userWallpapersDir()).filePath(QStringLiteral("videos"));
    QDir().mkpath(d);
    return d;
}

inline QString customWallpapersPreviewsDir() {
    const QString d = QDir(AppPaths::userWallpapersDir()).filePath(QStringLiteral("previews"));
    QDir().mkpath(d);
    return d;
}

inline QJsonArray loadCustomWallpapersArray() {
    QFile f(customWallpapersRegistryPath());
    if (!f.open(QIODevice::ReadOnly)) {
        return QJsonArray();
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    return doc.isArray() ? doc.array() : QJsonArray();
}

inline void saveCustomWallpapersArray(const QJsonArray& arr) {
    QFile f(customWallpapersRegistryPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
        f.close();
    }
}

inline QJsonObject findCustomWallpaper(const QString& idOrPath) {
    if (idOrPath.isEmpty()) return QJsonObject();
    const QString clean = idOrPath.startsWith(QStringLiteral("wallpaper:"), Qt::CaseInsensitive)
        ? idOrPath.mid(10).trimmed() : idOrPath.trimmed();
    const QJsonArray arr = loadCustomWallpapersArray();
    for (const QJsonValue& v : arr) {
        const QJsonObject obj = v.toObject();
        if (obj.value(QStringLiteral("id")).toString().compare(clean, Qt::CaseInsensitive) == 0 ||
            obj.value(QStringLiteral("sourcePath")).toString().compare(idOrPath, Qt::CaseInsensitive) == 0 ||
            obj.value(QStringLiteral("packagePath")).toString().compare(idOrPath, Qt::CaseInsensitive) == 0) {
            return obj;
        }
    }
    return QJsonObject();
} 

} // namespace LinWidget
