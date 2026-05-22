#pragma once

#include "desktop/widgets/linwidget/WidgetHostController.h"
#include "desktop/widgets/linwidget/WidgetIpcManager.h"
#include "desktop/widgets/linwidget/LayoutManager.h"
#include "desktop/widgets/linwidget/PermissionManager.h"

#include <QHash>
#include <QObject>
#include <QTimer>
#include <QFileSystemWatcher>
#include <qwindowdefs.h>

namespace LinWidget {

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

signals:
    void widgetLaunched(const QString& instanceId);
    void widgetStopped(const QString& instanceId);
    void widgetError(const QString& instanceId, const QString& errorMessage);

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
    bool sendGeometryToHost(const QString& instanceId, const QRect& geometry);
    void checkHeartbeats();
    void scheduleSaveToConfig();
    void saveToDefaultConfig();
    void configureDevelopmentWatcher(const WidgetInstance& instance);
    void clearDevelopmentWatcher(const QString& instanceId);
    QStringList watchedFilesForPackage(const QString& packagePath) const;

    WidgetIpcManager m_ipc;
    LayoutManager m_layout;
    PermissionManager m_permissions;
    WId m_hostParentWindow = 0;
    WId m_desktopLayerWindow = 0;
    QHash<QString, WId> m_widgetWindows;
    QHash<QString, WidgetHostController*> m_hosts;
    QTimer m_heartbeatTimer;
    QTimer m_saveTimer;
    QHash<QString, QFileSystemWatcher*> m_developmentWatchers;
    QHash<QString, QTimer*> m_developmentReloadTimers;
    WidgetHostController* m_sharedHost = nullptr;
    QHash<QString, WidgetInstance> m_instances;
    bool m_stoppingAll = false;
    bool m_showWebViewBounds = false;
};

}
