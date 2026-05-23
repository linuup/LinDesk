#pragma once

#include "desktop/widgets/linwidget/LinWidgetTypes.h"

#include <QJsonObject>
#include <QMap>
#include <QPoint>
#include <QVector>
#include <QWidget>

class QLabel;
class QLocalSocket;
class QTimer;

namespace LinWidget {

class WidgetHostRuntime : public QWidget {
    Q_OBJECT

public:
    explicit WidgetHostRuntime(QString widgetId,
                               QString packagePath,
                               QString ipcServerName,
                               QRect initialGeometry,
                               quint32 parentProcessId = 0,
                               quint64 parentWindowId = 0,
                               QStringList grantedPermissions = QStringList(),
                               bool isDevelopment = false,
                               bool autoReload = false,
                               bool sharedRuntime = false,
                               QWidget* parent = nullptr);
    ~WidgetHostRuntime() override;

    bool initialize(QString* errorMessage = nullptr);

private:
    bool nativeEvent(const QByteArray& eventType, void* message, long* result) override;
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

    void setupWindow();
    void attachToParentWindow();
    QRect parentClientRect() const;
    QPoint clampToParent(const QPoint& requestedTopLeft) const;
    void connectIpc();
    void startParentMonitor();
    bool isParentProcessAlive() const;
    void handleIpcReadyRead();
    void handleManagerMessage(const QJsonObject& message);
    void sendIpcMessage(const QString& type, const QJsonObject& extra = QJsonObject());
    void renderNativePlaceholder();
    void renderHtmlWidget();
    void renderHtmlPlaceholder(const QString& reason = QString());
    void renderSharedHtmlRuntime();
    QString sharedRuntimeEntryPath() const;
    void installSharedWidgetApiBridge();
    void sendHostPageMessage(const QJsonObject& payload);
    void queueOrSendHostPageMessage(const QJsonObject& payload);
    void flushPendingHostPageMessages();
    QJsonObject widgetInstancePayload(const QString& instanceId,
                                      const QString& packagePath,
                                      const WidgetManifest& manifest,
                                      const QRect& geometry,
                                      const QStringList& permissions,
                                      bool isDevelopment,
                                      bool autoReload,
                                      const QString& entryUrl) const;
    QString virtualHostNameForInstance(const QString& instanceId,
                                       const QStringList& permissions,
                                       bool isDevelopment) const;
    bool mapWidgetVirtualHost(const QString& hostName, const QString& packagePath);
    void installSharedWebResourceGuards(void* environment);
    void openDevToolsWhenReady();
    void setSharedWindowGeometry();
    void updateSharedWindowRegion();
    void syncSharedRegion(const QString& reason);
    void updateNativeWindowRegion(void* windowHandle);
    void installWebViewDragBridge();
    void installWebConsoleLogging();
    void installWebPermissionGuards();
    void handleWebMessageJson(const QString& json);
    void appendLog(const QString& level, const QString& message) const;
    void appendInstanceLog(const QString& instanceId, const QString& level, const QString& message) const;
    QString logFilePath() const;
    QString logFilePathForId(const QString& id) const;
    void focusWebView();
    void moveByDelta(const QPoint& delta);
    QSize clampWidgetSize(const QSize& requestedSize) const;
    void applyHostPosition(const QPoint& topLeft);
    void applyHostGeometry(const QRect& geometry);
    void updateWebViewBounds();

    QString m_widgetId;
    QString m_packagePath;
    QString m_ipcServerName;
    QRect m_initialGeometry;
    quint32 m_parentProcessId = 0;
    quint64 m_parentWindowId = 0;
    QStringList m_grantedPermissions;
    bool m_isDevelopment = false;
    bool m_autoReload = false;
    bool m_sharedRuntime = false;
    WidgetManifest m_manifest;
    QLocalSocket* m_socket = nullptr;
    QTimer* m_heartbeatTimer = nullptr;
    QTimer* m_parentMonitorTimer = nullptr;
    QLabel* m_placeholderLabel = nullptr;
    void* m_webViewController = nullptr;
    void* m_webView = nullptr;
    bool m_comInitialized = false;
    bool m_embedded = false;
    bool m_dragging = false;
    bool m_applyingManagerGeometry = false;
    QPoint m_dragOffset;
    QPoint m_lastDragGlobalPos;
    QPoint m_dragStartScreenPos;
    QPoint m_dragStartTopLeft;
    bool m_dragMoveStarted = false;
    QMap<QString, QRect> m_sharedWidgetGeometries;
    QMap<QString, QString> m_sharedWidgetHosts;
    QMap<QString, QStringList> m_sharedWidgetPermissions;
    QVector<QJsonObject> m_pendingHostPageMessages;
    bool m_hostPageReady = false;
    bool m_showWebViewBounds = false;
    bool m_pendingOpenDevTools = false;
    quint64 m_lastReportedWebViewWindow = 0;
};

}
