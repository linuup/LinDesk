#pragma once

#include "desktop/widgets/linwidget/LayoutManager.h"
#include "desktop/widgets/linwidget/PermissionManager.h"
#include "services/SystemInfo/SystemInfoService.h"

#include <QHash>
#include <QObject>
#include <QRect>
#include <QTimer>
#include <QFileSystemWatcher>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <qwindowdefs.h>

namespace LinCef {
class LinWidgetCanvasWidget;
class CefOsrBrowserInstance;
}

namespace LinWidget {

class WidgetManager : public QObject {
    Q_OBJECT

public:
    static WidgetManager* instance();
    explicit WidgetManager(QObject* parent = nullptr);
    ~WidgetManager() override;

    bool start();
    bool startWallpaper(const QString& packagePath, bool isDevelopment, bool interactive = false, QString* errorMessage = nullptr);
    void stopWallpaper(bool clearConfig = false);
    void syncWallpaperWindowLayer();
    void loadWallpaperConfig();
    void saveWallpaperConfig(const QString& packagePath, bool isDevelopment, bool interactive);
    QString activeWallpaperId() const { return m_activeWallpaperId; }
    QString activeWallpaperPackagePath() const { return m_activeWallpaperPackagePath; }
    bool activeWallpaperIsDevelopment() const { return m_activeWallpaperIsDevelopment; }
    bool launchWidgetPackage(const QString& packagePath,
                             const QRect& geometry,
                             QString* instanceId = nullptr,
                             QString* errorMessage = nullptr);
    bool launchWidgetPackage(const QString& packagePath,
                             const QRect& geometry,
                             bool isDevelopment,
                             bool autoReload,
                             QString* instanceId = nullptr,
                             QString* errorMessage = nullptr);
    bool createWidgetInstance(const QString& sourceInstanceId,
                              const QRect& requestedGeometry,
                              QString* newInstanceId = nullptr,
                              QString* errorMessage = nullptr);
    bool createWidgetInstanceFromPackage(const QString& packagePath,
                                         bool isDevelopment,
                                         bool autoReload,
                                         const QRect& requestedGeometry,
                                         QString* newInstanceId = nullptr,
                                         QString* errorMessage = nullptr);
    bool restartWidget(const QString& instanceId);
    bool restoreFromConfig(const QString& configPath = QString(), QString* errorMessage = nullptr);
    bool saveToConfig(const QString& configPath = QString(), QString* errorMessage = nullptr) const;
    QVector<WidgetLayoutState> layoutStates() const;
    bool hasRunningWidget(const QString& instanceId) const;
    bool moveWidget(const QString& instanceId, const QPoint& pos);
    bool resizeWidget(const QString& instanceId, const QSize& size);
    bool removeWidgetState(const QString& instanceId);
    void setEnabled(bool enabled);
    bool isEnabled() const;
    void setHostParentWindow(WId parentWindow);
    WId hostParentWindow() const;
    void setDesktopLayerWindow(WId layerWindow);
    WId desktopLayerWindow() const;
    void setShowWidgetBounds(bool enabled);
    bool showWidgetBounds() const;
    void syncWidgetWindowLayer(const QString& instanceId);
    void syncAllWidgetWindowLayers();
    void stopWidget(const QString& instanceId);
    void stopAll(bool includeWallpaper = true);
    WidgetInstance widgetInstance(const QString& instanceId) const;
    void broadcastToWidgetInstances(const QString& widgetRegistryId, const QJsonObject& message);

    bool hasActiveWallpaper() const { return !m_activeWallpaperId.isEmpty() && m_osrBrowsers.contains(m_activeWallpaperId); }
    QWidget* activeWallpaperWidget() const { return nullptr; }
    bool isWidgetAt(const QPoint& clientPos) const;
    QString widgetAt(const QPoint& clientPos) const;
    bool isWallpaperInteractive() const { return m_activeWallpaperInteractive; }

    void setCanvasWidget(::LinCef::LinWidgetCanvasWidget* canvas);
    ::LinCef::LinWidgetCanvasWidget* canvasWidget() const { return m_canvasWidget; }

    void setFocusedWidget(const QString& instanceId);
    void clearWidgetFocus();
    QString focusedWidget() const { return m_focusedInstanceId; }
    QRect focusedWidgetCursorRect() const { return m_imeCursorRect; }

    bool forwardMouseEvent(QMouseEvent* event);
    bool forwardWheelEvent(QWheelEvent* event);
    bool forwardKeyEvent(QKeyEvent* event);
    bool forwardInputMethodEvent(QInputMethodEvent* event);

    bool isCefOsrEnabled() const { return m_cefOsrEnabled; }
    void setCefOsrEnabled(bool enabled);
    int cefOsrBrowserCount() const { return m_osrBrowsers.size(); }

    void setWidgetLayerEmbedded(bool embedded);
    bool isWidgetLayerEmbedded() const;

signals:
    void widgetLaunched(const QString& instanceId);
    void widgetStopped(const QString& instanceId);
    void widgetError(const QString& instanceId, const QString& errorMessage);
    void widgetSettingsRequested(const QString& instanceId);
    void focusedWidgetChanged(const QString& instanceId);
    void imeCursorRectChanged(const QRect& cursorRect);

private:
    QString makeInstanceId(const WidgetManifest& manifest) const;
    bool createWidgetInstanceFromPackageInternal(const QString& packagePath,
                                                bool isDevelopment,
                                                bool autoReload,
                                                const QRect& requestedGeometry,
                                                const QString& parentInstanceId,
                                                QString* newInstanceId,
                                                QString* errorMessage);
    bool launchWidgetInstance(const WidgetInstance& instance,
                              const WidgetLayoutState& layoutState,
                              QString* errorMessage = nullptr);
    void handleRuntimeMessage(const QString& widgetId, const QJsonObject& message);
    void handleSystemInfoRequest(const QJsonObject& message);
    void handleStorageRequest(const QJsonObject& message);
    void sendSystemInfoResponse(const QString& instanceId,
                                const QString& requestId,
                                const QString& action,
                                bool ok,
                                const QJsonObject& data = QJsonObject(),
                                const QString& errorMessage = QString());
    void sendSystemInfoUpdate(const QString& instanceId,
                              const QString& watchId,
                              const QJsonObject& data);
    void sendStorageResponse(const QString& instanceId,
                             const QString& requestId,
                             bool ok,
                             const QJsonValue& value = QJsonValue(),
                             const QString& errorMessage = QString());
    void stopSystemInfoWatchesForInstance(const QString& instanceId);
    bool sendMessageToWidget(const QString& instanceId, const QJsonObject& message);
    void checkRuntimeHealth();
    void scheduleSaveToConfig();
    void saveToDefaultConfig();
    void configureDevelopmentWatcher(const WidgetInstance& instance);
    void clearDevelopmentWatcher(const QString& instanceId);
    QStringList watchedFilesForPackage(const QString& packagePath) const;
    QStringList watchedDirectoriesForPackage(const QString& packagePath) const;

    QVector<WidgetLayoutState> filterWallpapers(const QVector<WidgetLayoutState>& states) const;

    LayoutManager m_layout;
    PermissionManager m_permissions;
    WId m_hostParentWindow = 0;
    WId m_desktopLayerWindow = 0;
    QSet<QString> m_runningWidgets;
    QTimer m_runtimeHealthTimer;
    QTimer m_saveTimer;
    QHash<QString, QFileSystemWatcher*> m_developmentWatchers;
    QHash<QString, QTimer*> m_developmentReloadTimers;
    QHash<QString, QTimer*> m_systemInfoWatchTimers;
    SystemInfo::SystemInfoService m_systemInfoService;
    QHash<QString, WidgetInstance> m_instances;
    bool m_stoppingAll = false;
    bool m_showWidgetBounds = false;
    bool m_enabled = false;
    bool m_widgetLayerEmbedded = true; // true = widget host below ListView; false = HWND_TOP (above)
    QWidget* m_linDesktopParent = nullptr;
    QString m_activeWallpaperId;
    QString m_activeWallpaperPackagePath;
    bool m_activeWallpaperIsDevelopment = false;
    bool m_activeWallpaperInteractive = false;
    ::LinCef::LinWidgetCanvasWidget* m_canvasWidget = nullptr;
    QHash<QString, ::LinCef::CefOsrBrowserInstance*> m_osrBrowsers;
    ::LinCef::CefOsrBrowserInstance* m_defaultOsrBrowser = nullptr;
    bool m_cefOsrEnabled = true;
    QString m_focusedInstanceId;
    QRect m_imeCursorRect;
    QMap<QString, QVector<qint64>> m_crashTimestamps;

    void handleBrowserCrashed(const QString& widgetId, int status, int errorCode, const QString& errorString);
    void updateDefaultCefPage();
    QString defaultCefPagePath() const;
};

}

