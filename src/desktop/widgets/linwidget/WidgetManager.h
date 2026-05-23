#pragma once

#include "desktop/widgets/linwidget/WidgetHostController.h"
#include "desktop/widgets/linwidget/WidgetIpcManager.h"
#include "desktop/widgets/linwidget/LayoutManager.h"
#include "desktop/widgets/linwidget/PermissionManager.h"
#include "services/SystemInfo/SystemInfoService.h"

#include <QHash>
#include <QObject>
#include <QRect>
#include <QTimer>
#include <QFileSystemWatcher>
#include <qwindowdefs.h>

namespace LinWidget {

class WidgetManager : public QObject {
    Q_OBJECT

public:
    struct LayerDiagnostics {
        bool hasListViewWindow = false;
        bool hasHostWindow = false;
        bool hasWebViewWindow = false;
        bool syncSucceeded = false;
        bool hostAboveListView = false;
        bool hostRectMatchesListView = false;
        bool webViewRectMatchesHost = false;
        bool emptyPointPassesThrough = false;
        bool widgetPointHitsHost = false;
        bool widgetPointHitsWebView = false;
        WId listViewWindow = 0;
        WId hostWindow = 0;
        WId webViewWindow = 0;
        WId pointWindow = 0;
        QRect listViewRect;
        QRect hostRect;
        QRect webViewRect;
        int visibleWidgetCount = 0;
        int regionRectCount = 0;
        QString reason;
    };

    explicit WidgetManager(QObject* parent = nullptr);
    ~WidgetManager() override;

    bool start();
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
    bool restartWidget(const QString& instanceId);
    bool restoreFromConfig(const QString& configPath = QString(), QString* errorMessage = nullptr);
    bool saveToConfig(const QString& configPath = QString(), QString* errorMessage = nullptr) const;
    QVector<WidgetLayoutState> layoutStates() const;
    bool moveWidget(const QString& instanceId, const QPoint& pos);
    bool resizeWidget(const QString& instanceId, const QSize& size);
    bool removeWidgetState(const QString& instanceId);
    void setEnabled(bool enabled);
    bool isEnabled() const;
    void setHostParentWindow(WId parentWindow);
    WId hostParentWindow() const;
    void setDesktopLayerWindow(WId layerWindow);
    WId desktopLayerWindow() const;
    void setShowWebViewBounds(bool enabled);
    bool showWebViewBounds() const;
    void syncWidgetWindowLayer(const QString& instanceId);
    void syncAllWidgetWindowLayers();
    LayerDiagnostics layerDiagnostics() const;
    LayerDiagnostics verifyWidgetLayerAndClickThrough();
    QString layerDiagnosticsText(const LayerDiagnostics& diagnostics) const;
    void stopWidget(const QString& instanceId);
    void stopAll();
    WidgetInstance widgetInstance(const QString& instanceId) const;

signals:
    void widgetLaunched(const QString& instanceId);
    void widgetStopped(const QString& instanceId);
    void widgetError(const QString& instanceId, const QString& errorMessage);
    void widgetSettingsRequested(const QString& instanceId);

private:
    QString makeInstanceId(const WidgetManifest& manifest) const;
    QString ipcServerName() const;
    bool ensureSharedHost(QString* errorMessage = nullptr);
    void restartSharedHost();
    void sendHostOptionsToSharedHost();
    bool sendAddWidgetToSharedHost(const WidgetInstance& instance, const WidgetLayoutState& layoutState);
    bool launchWidgetInstance(const WidgetInstance& instance,
                              const WidgetLayoutState& layoutState,
                              QString* errorMessage = nullptr);
    void handleIpcMessage(const QString& widgetId, const QJsonObject& message);
    void handleSystemInfoRequest(const QJsonObject& message);
    void sendSystemInfoResponse(const QString& instanceId,
                                const QString& requestId,
                                const QString& action,
                                bool ok,
                                const QJsonObject& data = QJsonObject(),
                                const QString& errorMessage = QString());
    void sendSystemInfoUpdate(const QString& instanceId,
                              const QString& watchId,
                              const QJsonObject& data);
    void stopSystemInfoWatchesForInstance(const QString& instanceId);
    int visibleWidgetCount() const;
    bool sharedWebViewMissingForVisibleWidgets() const;
    bool sendGeometryToHost(const QString& instanceId, const QRect& geometry);
    void resendVisibleWidgetsToSharedHost();
    bool sharedHostLayerNeedsRecovery() const;
    void checkHeartbeats();
    void scheduleSaveToConfig();
    void saveToDefaultConfig();
    void configureDevelopmentWatcher(const WidgetInstance& instance);
    void clearDevelopmentWatcher(const QString& instanceId);
    QStringList watchedFilesForPackage(const QString& packagePath) const;
    QStringList watchedDirectoriesForPackage(const QString& packagePath) const;

    WidgetIpcManager m_ipc;
    LayoutManager m_layout;
    PermissionManager m_permissions;
    WId m_hostParentWindow = 0;
    WId m_desktopLayerWindow = 0;
    WId m_sharedWebViewWindow = 0;
    QString m_sharedWebViewWindowClass;
    QHash<QString, WId> m_widgetWindows;
    QHash<QString, WidgetHostController*> m_hosts;
    QTimer m_heartbeatTimer;
    QTimer m_saveTimer;
    QHash<QString, QFileSystemWatcher*> m_developmentWatchers;
    QHash<QString, QTimer*> m_developmentReloadTimers;
    QHash<QString, QTimer*> m_systemInfoWatchTimers;
    SystemInfo::SystemInfoService m_systemInfoService;
    WidgetHostController* m_sharedHost = nullptr;
    QHash<QString, WidgetInstance> m_instances;
    bool m_stoppingAll = false;
    bool m_showWebViewBounds = false;
    bool m_enabled = true;
    LayerDiagnostics m_lastLayerDiagnostics;
};

}
