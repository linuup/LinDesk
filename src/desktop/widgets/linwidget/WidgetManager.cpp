#include "desktop/widgets/linwidget/WidgetManager.h"

#include "desktop/widgets/linwidget/ConfigManager.h"
#include "desktop/widgets/linwidget/PermissionManager.h"
#include "desktop/widgets/linwidget/WidgetIpcProtocol.h"
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
#include <QJsonObject>
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
constexpr qint64 kHeartbeatTimeoutMs = 10 * 1000;
constexpr int kHeartbeatCheckIntervalMs = 2500;
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

QStringList grantedPermissionsForManifest(const WidgetManifest& manifest, const QString& packagePath, bool isDevelopment)
{
    PermissionManager permissions;
    if (isDevelopment) {
        return permissions.normalizePermissions({ QStringLiteral("network"), QStringLiteral("storage"), QStringLiteral("desktop") });
    }

    WidgetRegistryManager registry;
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
QString windowClassName(HWND window)
{
    if (!window) {
        return QString();
    }
    wchar_t className[256] = {};
    GetClassNameW(window, className, 256);
    return QString::fromWCharArray(className);
}

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

QString hwndString(HWND window)
{
    return QString::number(reinterpret_cast<quintptr>(window), 16);
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
}

WidgetManager::WidgetManager(QObject* parent)
    : QObject(parent)
{
    connect(&m_ipc, &WidgetIpcManager::messageReceived,
            this, &WidgetManager::handleIpcMessage);
    connect(&m_ipc, &WidgetIpcManager::errorOccurred,
            this, [this](const QString& message) {
        emit widgetError(QString(), message);
    });

    m_heartbeatTimer.setInterval(kHeartbeatCheckIntervalMs);
    connect(&m_heartbeatTimer, &QTimer::timeout, this, &WidgetManager::checkHeartbeats);

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
    if (!m_enabled) {
        return true;
    }
    const bool ok = m_ipc.start(ipcServerName());
    if (ok && !m_heartbeatTimer.isActive()) {
        m_heartbeatTimer.start();
    }
    return ok;
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
            *errorMessage = QStringLiteral("Failed to start LinWidget IPC server.");
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
            if (!m_hosts.contains(state.instanceId)) {
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
    layoutState.geometry = instance.geometry;
    layoutState.zOrder = m_layout.nextZOrder();
    layoutState.visible = true;

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

bool WidgetManager::launchWidgetInstance(const WidgetInstance& instance,
                                         const WidgetLayoutState& layoutState,
                                         QString* errorMessage)
{
    if (!ensureSharedHost(errorMessage)) {
        return false;
    }

    m_layout.addOrUpdate(layoutState);
    m_instances.insert(instance.instanceId, instance);
    m_hosts.insert(instance.instanceId, m_sharedHost);
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
    WidgetInstance instance = m_instances.value(instanceId);
    if (instance.instanceId.isEmpty() || state.instanceId.isEmpty()) {
        return false;
    }

    QString manifestError;
    if (!WidgetPackage::loadManifest(instance.packagePath, &instance.manifest, &manifestError)) {
        emit widgetError(instanceId, QStringLiteral("Reload blocked: %1").arg(manifestError));
        return false;
    }
    instance.geometry = state.geometry;
    instance.grantedPermissions = grantedPermissionsForManifest(instance.manifest, instance.packagePath, instance.isDevelopment);
    m_instances.insert(instanceId, instance);
    stopSystemInfoWatchesForInstance(instanceId);
    clearDevelopmentWatcher(instanceId);
    configureDevelopmentWatcher(instance);
    QJsonObject removeMessage = makeIpcMessage(QString::fromLatin1(IpcType::RemoveWidget), QString::fromLatin1(kSharedHostId));
    removeMessage.insert(QStringLiteral("instanceId"), instanceId);
    removeMessage.insert(QStringLiteral("widgetId"), instanceId);
    m_ipc.sendToWidget(QString::fromLatin1(kSharedHostId), removeMessage);
    sendAddWidgetToSharedHost(instance, state);
    QJsonObject message = makeIpcMessage(QString::fromLatin1(IpcType::ReloadWidget), QString::fromLatin1(kSharedHostId));
    message.insert(QStringLiteral("instanceId"), instanceId);
    message.insert(QStringLiteral("widgetId"), instanceId);
    m_ipc.sendToWidget(QString::fromLatin1(kSharedHostId), message);
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
            *errorMessage = QStringLiteral("Failed to start LinWidget IPC server.");
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
        WidgetRegistryManager registry;
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
                state.geometry = QRect(QPoint(260, 220), QSize(manifest.width, manifest.height));
                state.zOrder = m_layout.nextZOrder();
                state.visible = true;
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
        instance.isDevelopment = state.instanceId.startsWith(QStringLiteral("dev:"));
        instance.autoReload = instance.isDevelopment;
        instance.grantedPermissions = grantedPermissionsForManifest(manifest, state.packagePath, instance.isDevelopment);
        instance.geometry = resolvedWidgetGeometry(manifest, state.geometry, instance.isDevelopment);
        if (instance.instanceId.isEmpty()) {
            instance.instanceId = makeInstanceId(manifest);
        }

        WidgetLayoutState launchState = state;
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
    if (m_hosts.contains(instanceId)) {
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
        if (m_heartbeatTimer.isActive()) {
            m_heartbeatTimer.stop();
        }
        stopAll();
        m_widgetWindows.clear();
        m_ipc.stop();
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

void WidgetManager::setEmbedInDesktopLayer(bool enabled)
{
    if (m_embedInDesktopLayer == enabled) {
        return;
    }
    m_embedInDesktopLayer = enabled;
    appendSharedLog(QStringLiteral("layer"),
                    enabled
                        ? QStringLiteral("embed host enabled, restarting shared host")
                        : QStringLiteral("embed host disabled, restarting shared host as top level window"));
    if (m_sharedHost) {
        restartSharedHost();
    } else {
        syncAllWidgetWindowLayers();
    }
}

bool WidgetManager::embedInDesktopLayer() const
{
    return m_embedInDesktopLayer;
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

    RECT layerNativeRect = {};
    if (!GetWindowRect(layerWindow, &layerNativeRect)) {
        return;
    }
    const QRect nativeLayerGeometry = windowRect(layerWindow);
    const QRect logicalLayerGeometry = bestLogicalLayerRect(layerWindow, layerNativeRect);
    RECT clientRect = {};
    GetClientRect(layerWindow, &clientRect);
    const int clientWidth = clientRect.right - clientRect.left;
    const int clientHeight = clientRect.bottom - clientRect.top;
    const QSize logicalClientSize = logicalLayerGeometry.size();

    const bool childLayer = GetParent(widgetWindow) == layerWindow;
    HWND insertAfter = childLayer
        ? HWND_TOP
        : layerWindow;

    if (IsIconic(widgetWindow)) {
        ShowWindow(widgetWindow, SW_RESTORE);
    }
    ShowWindow(widgetWindow, SW_SHOWNOACTIVATE);

    UINT setWindowFlags = SWP_SHOWWINDOW | SWP_FRAMECHANGED;
    setWindowFlags |= SWP_NOACTIVATE;

    const BOOL ok = SetWindowPos(widgetWindow,
                                 insertAfter,
                                 childLayer ? 0 : nativeLayerGeometry.x(),
                                 childLayer ? 0 : nativeLayerGeometry.y(),
                                 childLayer ? clientWidth : nativeLayerGeometry.width(),
                                 childLayer ? clientHeight : nativeLayerGeometry.height(),
                                 setWindowFlags);
    if (ok) {
        QJsonObject hostGeometryMessage = makeIpcMessage(QString::fromLatin1(IpcType::UpdateHostGeometry), QString::fromLatin1(kSharedHostId));
        hostGeometryMessage.insert(QStringLiteral("x"), childLayer ? 0 : logicalLayerGeometry.x());
        hostGeometryMessage.insert(QStringLiteral("y"), childLayer ? 0 : logicalLayerGeometry.y());
        hostGeometryMessage.insert(QStringLiteral("width"), childLayer ? logicalClientSize.width() : logicalLayerGeometry.width());
        hostGeometryMessage.insert(QStringLiteral("height"), childLayer ? logicalClientSize.height() : logicalLayerGeometry.height());
        m_ipc.sendToWidget(QString::fromLatin1(kSharedHostId), hostGeometryMessage);
    }
    const QRect hostRect = windowRect(widgetWindow);
    const bool hostRectMatchesListView = hostRect == nativeLayerGeometry;
    const bool hostAboveListView = !m_embedInDesktopLayer || (childLayer
        ? GetWindow(layerWindow, GW_CHILD) == widgetWindow
        : GetWindow(layerWindow, GW_HWNDPREV) == widgetWindow);
    const POINT center {
        hostRect.center().x(),
        hostRect.center().y()
    };
    HWND pointWindow = WindowFromPoint(center);
    if (!ok || !hostAboveListView || !hostRectMatchesListView) {
        appendSharedLog(QStringLiteral("layer"),
                        QStringLiteral("sync mismatch id=%1 ok=%2 widget=%3 widgetClass=%4 layer=%5 layerClass=%6 insertAfter=%7 rect=%8,%9 %10x%11 logicalRect=%12,%13 %14x%15 client=%16x%17 aboveListView=%18 pointWindow=%19 pointClass=%20")
                            .arg(instanceId,
                                 ok ? QStringLiteral("true") : QStringLiteral("false"),
                                 hwndString(widgetWindow),
                                 windowClassName(widgetWindow),
                                 hwndString(layerWindow),
                                 windowClassName(layerWindow),
                                 hwndString(insertAfter),
                                 QString::number(hostRect.x()),
                                 QString::number(hostRect.y()),
                                 QString::number(hostRect.width()),
                                 QString::number(hostRect.height()),
                                 QString::number(logicalLayerGeometry.x()),
                                 QString::number(logicalLayerGeometry.y()),
                                 QString::number(logicalLayerGeometry.width()),
                                 QString::number(logicalLayerGeometry.height()),
                                 QString::number(clientWidth),
                                 QString::number(clientHeight),
                                 hostAboveListView ? QStringLiteral("true") : QStringLiteral("false"),
                                 hwndString(pointWindow),
                                 windowClassName(pointWindow)));
        qDebug() << "[LinWidgetLayer] sync"
                 << "id=" << instanceId
                 << "ok=" << ok
                 << "widget=" << hwndString(widgetWindow)
                 << "widgetClass=" << windowClassName(widgetWindow)
                 << "layer=" << hwndString(layerWindow)
                 << "layerClass=" << windowClassName(layerWindow)
                 << "insertAfter=" << hwndString(insertAfter)
                 << "insertAfterClass=" << windowClassName(insertAfter)
                 << "rect=" << hostRect
                 << "logicalRect=" << logicalLayerGeometry
                 << "client=" << QSize(clientWidth, clientHeight)
                 << "aboveListView=" << hostAboveListView
                 << "pointWindow=" << hwndString(pointWindow)
                 << "pointClass=" << windowClassName(pointWindow);
    }
#else
    Q_UNUSED(instanceId);
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
            *errorMessage = QStringLiteral("Failed to start LinWidget IPC server.");
        }
        return false;
    }
    if (m_sharedHost) {
        return true;
    }

    WidgetManifest manifest;
    manifest.id = QString::fromLatin1(kSharedHostId);
    manifest.name = QStringLiteral("LinWidget Shared Runtime");
    manifest.width = 1920;
    manifest.height = 1080;

    WidgetInstance instance;
    instance.instanceId = QString::fromLatin1(kSharedHostId);
    instance.manifest = manifest;
    instance.geometry = QRect(0, 0, manifest.width, manifest.height);

    const quintptr parentWindow = m_embedInDesktopLayer ? static_cast<quintptr>(m_desktopLayerWindow) : 0;
    auto* host = new WidgetHostController(instance, ipcServerName(), parentWindow, this);
    connect(host, &WidgetHostController::exited, this, [this](const QString&, int, QProcess::ExitStatus status) {
        WidgetHostController* controller = m_sharedHost;
        m_sharedHost = nullptr;
        if (controller) {
            controller->deleteLater();
        }
        if (m_stoppingAll || status == QProcess::NormalExit) {
            return;
        }
        emit widgetError(QString(), QStringLiteral("Shared WidgetHost crashed. Restarting widgets."));
        restartSharedHost();
    });
    connect(host, &WidgetHostController::failed, this, [this](const QString&, const QString& message) {
        emit widgetError(QString(), message);
    });

    if (!host->start()) {
        host->deleteLater();
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to start shared WidgetHost.exe.");
        }
        return false;
    }
    m_sharedHost = host;
    sendHostOptionsToSharedHost();
    return true;
}

void WidgetManager::restartSharedHost()
{
    if (!m_enabled) {
        return;
    }

    if (m_sharedHost) {
        WidgetHostController* host = m_sharedHost;
        m_sharedHost = nullptr;
        m_widgetWindows.remove(QString::fromLatin1(kSharedHostId));
        m_sharedWebViewWindow = 0;
        m_sharedWebViewWindowClass.clear();
        host->stop();
        host->deleteLater();
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
        m_hosts.insert(state.instanceId, m_sharedHost);
        sendAddWidgetToSharedHost(instance, state);
    }
}

void WidgetManager::sendHostOptionsToSharedHost()
{
    if (!m_sharedHost) {
        return;
    }

    QJsonObject message = makeIpcMessage(QString::fromLatin1(IpcType::UpdateHostOptions), QString::fromLatin1(kSharedHostId));
    message.insert(QStringLiteral("showWebViewBounds"), m_showWebViewBounds);
    m_ipc.sendToWidget(QString::fromLatin1(kSharedHostId), message);
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

    QJsonObject message = makeIpcMessage(QString::fromLatin1(IpcType::AddWidget), QString::fromLatin1(kSharedHostId));
    message.insert(QStringLiteral("instanceId"), instance.instanceId);
    message.insert(QStringLiteral("packagePath"), instance.packagePath);
    message.insert(QStringLiteral("geometry"), geometry);
    message.insert(QStringLiteral("permissions"), permissions);
    message.insert(QStringLiteral("development"), instance.isDevelopment);
    message.insert(QStringLiteral("autoReload"), instance.autoReload);
    message.insert(QStringLiteral("zOrder"), layoutState.zOrder);
    return m_ipc.sendToWidget(QString::fromLatin1(kSharedHostId), message);
}

void WidgetManager::stopWidget(const QString& instanceId)
{
    if (!m_hosts.contains(instanceId)) {
        return;
    }
    m_hosts.remove(instanceId);
    stopSystemInfoWatchesForInstance(instanceId);
    clearDevelopmentWatcher(instanceId);
    m_instances.remove(instanceId);
    QJsonObject message = makeIpcMessage(QString::fromLatin1(IpcType::RemoveWidget), QString::fromLatin1(kSharedHostId));
    message.insert(QStringLiteral("instanceId"), instanceId);
    message.insert(QStringLiteral("widgetId"), instanceId);
    m_ipc.sendToWidget(QString::fromLatin1(kSharedHostId), message);
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
    const QStringList ids = m_hosts.keys();
    for (const QString& id : ids) {
        stopWidget(id);
    }
    m_hosts.clear();
    m_instances.clear();
    qDeleteAll(m_systemInfoWatchTimers);
    m_systemInfoWatchTimers.clear();
    if (m_sharedHost) {
        WidgetHostController* host = m_sharedHost;
        m_sharedHost = nullptr;
        host->stop();
        host->deleteLater();
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
    QJsonObject message = makeIpcMessage(QString::fromLatin1(IpcType::SystemInfoResponse), QString::fromLatin1(kSharedHostId));
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
    m_ipc.sendToWidget(QString::fromLatin1(kSharedHostId), message);
}

void WidgetManager::sendSystemInfoUpdate(const QString& instanceId,
                                         const QString& watchId,
                                         const QJsonObject& data)
{
    QJsonObject message = makeIpcMessage(QString::fromLatin1(IpcType::SystemInfoUpdate), QString::fromLatin1(kSharedHostId));
    message.insert(QStringLiteral("instanceId"), instanceId);
    message.insert(QStringLiteral("widgetId"), instanceId);
    message.insert(QStringLiteral("watchId"), watchId);
    message.insert(QStringLiteral("data"), data);
    m_ipc.sendToWidget(QString::fromLatin1(kSharedHostId), message);
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

QString WidgetManager::ipcServerName() const
{
    return QStringLiteral("LinDesk.LinWidget.%1").arg(QCoreApplication::applicationPid());
}

void WidgetManager::handleIpcMessage(const QString& widgetId, const QJsonObject& message)
{
    const QString type = message.value(QStringLiteral("type")).toString();
    if (widgetId == QString::fromLatin1(kSharedHostId)) {
        if (m_sharedHost && (type == QString::fromLatin1(IpcType::Heartbeat) ||
            type == QString::fromLatin1(IpcType::Ready))) {
            m_sharedHost->markHeartbeat();
            if (type == QString::fromLatin1(IpcType::Ready)) {
                sendHostOptionsToSharedHost();
                const QVector<WidgetLayoutState> states = m_layout.states();
                for (const WidgetLayoutState& state : states) {
                    if (state.visible && m_instances.contains(state.instanceId)) {
                        sendAddWidgetToSharedHost(m_instances.value(state.instanceId), state);
                    }
                }
            }
            return;
        }
        if (type == QString::fromLatin1(IpcType::HostWindowReady)) {
            const WId windowId = static_cast<WId>(message.value(QStringLiteral("hwnd")).toString().toULongLong());
            if (windowId != 0) {
                m_widgetWindows.insert(QString::fromLatin1(kSharedHostId), windowId);
                syncWidgetWindowLayer(QString::fromLatin1(kSharedHostId));
            }
            return;
        }
        if (type == QString::fromLatin1(IpcType::WebViewWindowReady)) {
            const WId windowId = static_cast<WId>(message.value(QStringLiteral("hwnd")).toString().toULongLong());
            if (windowId != 0) {
                m_sharedWebViewWindow = windowId;
                m_sharedWebViewWindowClass = message.value(QStringLiteral("className")).toString();
                syncWidgetWindowLayer(QString::fromLatin1(kSharedHostId));
            }
            return;
        }
        if (type == QString::fromLatin1(IpcType::WebViewWindowLost)) {
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
        if (type == QString::fromLatin1(IpcType::WidgetGeometryChanged)) {
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
        if (type == QString::fromLatin1(IpcType::WidgetError)) {
            emit widgetError(message.value(QStringLiteral("instanceId")).toString(), message.value(QStringLiteral("message")).toString());
            return;
        }
        if (type == QString::fromLatin1(IpcType::OpenWidgetSettings)) {
            const QString instanceId = message.value(QStringLiteral("instanceId")).toString(message.value(QStringLiteral("widgetId")).toString()).trimmed();
            if (!instanceId.isEmpty() && m_instances.contains(instanceId)) {
                emit widgetSettingsRequested(instanceId);
            }
            return;
        }
        if (type == QString::fromLatin1(IpcType::SystemInfoRequest)) {
            handleSystemInfoRequest(message);
            return;
        }
    }

    WidgetHostController* host = m_hosts.value(widgetId, nullptr);
    if (!host) {
        return;
    }

    if (type == QString::fromLatin1(IpcType::Heartbeat) ||
        type == QString::fromLatin1(IpcType::Ready)) {
        host->markHeartbeat();
    } else if (type == QString::fromLatin1(IpcType::MoveWidget)) {
        const QPoint pos(
            message.value(QStringLiteral("x")).toInt(host->instance().geometry.x()),
            message.value(QStringLiteral("y")).toInt(host->instance().geometry.y()));
        moveWidget(widgetId, pos);
    } else if (type == QString::fromLatin1(IpcType::ResizeWidget)) {
        const QSize size(
            message.value(QStringLiteral("width")).toInt(host->instance().geometry.width()),
            message.value(QStringLiteral("height")).toInt(host->instance().geometry.height()));
        resizeWidget(widgetId, size);
    } else if (type == QString::fromLatin1(IpcType::HostWindowReady)) {
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

bool WidgetManager::sendGeometryToHost(const QString& instanceId, const QRect& geometry)
{
    QJsonObject message = makeIpcMessage(QString::fromLatin1(IpcType::UpdateWidgetGeometry), QString::fromLatin1(kSharedHostId));
    message.insert(QStringLiteral("instanceId"), instanceId);
    message.insert(QStringLiteral("x"), geometry.x());
    message.insert(QStringLiteral("y"), geometry.y());
    message.insert(QStringLiteral("width"), geometry.width());
    message.insert(QStringLiteral("height"), geometry.height());
    return m_ipc.sendToWidget(QString::fromLatin1(kSharedHostId), message);
}

void WidgetManager::resendVisibleWidgetsToSharedHost()
{
    if (!m_sharedHost) {
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
    const bool childLayer = GetParent(hostWindow) == layerWindow;
    const bool layerMismatch = m_embedInDesktopLayer &&
        (childLayer ? GetWindow(layerWindow, GW_CHILD) != hostWindow : GetWindow(layerWindow, GW_HWNDPREV) != hostWindow);
    return layerMismatch ||
        windowRect(hostWindow) != windowRect(layerWindow) ||
        sharedWebViewMissingForVisibleWidgets();
#else
    return false;
#endif
}

void WidgetManager::checkHeartbeats()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_sharedHost && m_sharedHost->heartbeatTimedOut(now, kHeartbeatTimeoutMs)) {
        emit widgetError(QString(), QStringLiteral("Shared widget heartbeat timed out. Restarting widgets."));
        restartSharedHost();
        return;
    }
    if (m_sharedHost && sharedHostLayerNeedsRecovery()) {
        if (sharedWebViewMissingForVisibleWidgets()) {
            qDebug() << "[LinWidgetLayer] heartbeat recovery restarting shared host because WebView child is missing";
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
