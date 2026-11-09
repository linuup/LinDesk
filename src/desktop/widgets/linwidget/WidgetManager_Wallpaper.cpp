#include "desktop/widgets/linwidget/WidgetManager_Internal.h"

namespace LinWidget {

// ============================================================================
// WidgetManager Dynamic Wallpaper Subsystem Implementation
// ============================================================================

bool WidgetManager::forwardMouseMoveToWallpaper(const QPoint& clientPos)
{
    if (!m_cefOsrEnabled || m_activeWallpaperId.isEmpty() || !m_activeWallpaperInteractive) {
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
    if (!m_cefOsrEnabled || m_activeWallpaperId.isEmpty() || !m_activeWallpaperInteractive) {
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
    if (!m_cefOsrEnabled || m_activeWallpaperId.isEmpty() || !m_activeWallpaperInteractive) {
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


bool WidgetManager::startWallpaper(const QString& packagePath, bool isDevelopment, bool interactive, QString* errorMessage)
{
    QSettings runtimeSettings(AppPaths::configFilePath(), QSettings::IniFormat);
    if (runtimeSettings.value(QStringLiteral("LinWidgets/RuntimeUninstalled"), false).toBool() || !m_enabled) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("运行支持组件未安装或系统总开关已关闭。");
        }
        return false;
    }

    if (!m_cefOsrEnabled) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("CEF OSR rendering is disabled.");
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
    // 继承能效状态机当前的帧率与休眠状态，避免切换壁纸后绕过电源策略
    osr->setFramerate(m_wallpaperFramerate);
    osr->setVisible(!m_wallpaperSuspended);

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
    connect(osr, &LinCef::CefOsrBrowserInstance::consoleMessageReceived, this,
            [](const QString&, int, const QString&, const QString&, int) {});

    // 当壁纸页面首次加载完成时，自动回放持久化保存在 QSettings 中的该壁纸参数
    const QString manifestIdForReplay = manifest.id;
    QTimer::singleShot(350, this, [this, instanceId, manifestIdForReplay]() {
        if (m_activeWallpaperId != instanceId || !m_osrBrowsers.contains(instanceId)) {
            return;
        }
        QSettings s(AppPaths::configFilePath(), QSettings::IniFormat);
        const QString savedJson = s.value(QStringLiteral("LinWallpapers/%1/Properties").arg(manifestIdForReplay)).toString();
        if (savedJson.isEmpty()) {
            return;
        }
        const QJsonObject savedProps = QJsonDocument::fromJson(savedJson.toUtf8()).object();
        for (auto it = savedProps.begin(); it != savedProps.end(); ++it) {
            QJsonObject propMsg;
            propMsg.insert(QStringLiteral("type"), QStringLiteral("wallpaper_property_change"));
            propMsg.insert(QStringLiteral("key"), it.key());
            propMsg.insert(QStringLiteral("property"), it.key());
            propMsg.insert(QStringLiteral("value"), it.value());
            sendMessageToWidget(instanceId, propMsg);
        }
    });

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

void WidgetManager::setWallpaperSuspended(bool suspended)
{
    if (m_wallpaperSuspended == suspended) {
        return;
    }
    m_wallpaperSuspended = suspended;
    if (!m_activeWallpaperId.isEmpty()) {
        auto* osr = m_osrBrowsers.value(m_activeWallpaperId, nullptr);
        if (osr) {
            osr->setVisible(!suspended);
        }
    }
}

void WidgetManager::setWallpaperFramerate(int fps)
{
    m_wallpaperFramerate = qBound(1, fps, 144);
    if (!m_activeWallpaperId.isEmpty()) {
        auto* osr = m_osrBrowsers.value(m_activeWallpaperId, nullptr);
        if (osr) {
            osr->setFramerate(fps);
        }
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



} // namespace LinWidget
