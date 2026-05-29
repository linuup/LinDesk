#pragma once

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

class LinWidgetWebViewRuntime;

class WidgetManager : public QObject {
    Q_OBJECT

public:
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
    bool ensureSharedHost(QString* errorMessage = nullptr);
    void restartSharedHost();
    void sendHostOptionsToSharedHost();
    bool sendAddWidgetToSharedHost(const WidgetInstance& instance, const WidgetLayoutState& layoutState);
    bool launchWidgetInstance(const WidgetInstance& instance,
                              const WidgetLayoutState& layoutState,
                              QString* errorMessage = nullptr);
    void handleRuntimeMessage(const QString& widgetId, const QJsonObject& message);
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
    void sendGeometryToHost(const QString& instanceId, const QRect& geometry);
    bool sendMessageToSharedRuntime(const QJsonObject& message);
    void resendVisibleWidgetsToSharedHost();
    bool sharedHostLayerNeedsRecovery() const;
    void checkRuntimeHealth();
    void scheduleSaveToConfig();
    void saveToDefaultConfig();
    void configureDevelopmentWatcher(const WidgetInstance& instance);
    void clearDevelopmentWatcher(const QString& instanceId);
    QStringList watchedFilesForPackage(const QString& packagePath) const;
    QStringList watchedDirectoriesForPackage(const QString& packagePath) const;

    LayoutManager m_layout;
    PermissionManager m_permissions;
    WId m_hostParentWindow = 0;
    WId m_desktopLayerWindow = 0;
    WId m_sharedWebViewWindow = 0;
    QString m_sharedWebViewWindowClass;
    QHash<QString, WId> m_widgetWindows;
    QSet<QString> m_runningWidgets;
    QTimer m_runtimeHealthTimer;
    QTimer m_saveTimer;
    QHash<QString, QFileSystemWatcher*> m_developmentWatchers;
    QHash<QString, QTimer*> m_developmentReloadTimers;
    QHash<QString, QTimer*> m_systemInfoWatchTimers;
    SystemInfo::SystemInfoService m_systemInfoService;
    LinWidgetWebViewRuntime* m_webViewRuntime = nullptr;
    QHash<QString, WidgetInstance> m_instances;
    bool m_stoppingAll = false;
    bool m_showWebViewBounds = false;
    bool m_enabled = true;
};

}

