#include "desktop/widgets/linwidget/WidgetManager_Internal.h"

namespace LinWidget {

// ============================================================================
// WidgetManager Core Singleton, Lifecycle & Geometry Implementation
// ============================================================================

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
    if (enabled) {
        QSettings runtimeSettings(AppPaths::configFilePath(), QSettings::IniFormat);
        if (runtimeSettings.value(QStringLiteral("LinWidgets/RuntimeUninstalled"), false).toBool()) {
            return;
        }
    }
    if (m_enabled == enabled) {
        return;
    }

    if (!enabled) {
        if (m_saveTimer.isActive()) {
            m_saveTimer.stop();
        }
        saveToDefaultConfig();
    }

    m_enabled = enabled;
    if (!m_enabled) {
        if (m_runtimeHealthTimer.isActive()) {
            m_runtimeHealthTimer.stop();
        }
        // 主系统总开关关闭：同时停止桌面小组件与动态壁纸（保留壁纸配置以便开启时恢复）
        stopAll(true);
        if (LinCef::LinWidgetCanvasWidget::hostWidget()) {
            LinCef::LinWidgetCanvasWidget::hostWidget()->update();
        }
    } else {
        if (!m_runtimeHealthTimer.isActive()) {
            m_runtimeHealthTimer.start();
        }
    }
    emit runtimeEnabledChanged(m_enabled);
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


QString WidgetManager::makeInstanceId(const WidgetManifest& manifest) const
{
    return QStringLiteral("%1.%2").arg(manifest.id, QString::number(QDateTime::currentMSecsSinceEpoch()));
}


void WidgetManager::checkRuntimeHealth()
{
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


