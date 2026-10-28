#pragma once

#include "desktop/widgets/linwidget/LinWidgetTypes.h"

#include <QJsonObject>
#include <QMap>
#include <QPoint>
#include <QVector>
#include <QWidget>

class QLabel;
class QTimer;

namespace LinWidget {

class LinWidgetWebViewRuntime : public QWidget {
    Q_OBJECT

public:
    explicit LinWidgetWebViewRuntime(QString widgetId,
                               QString packagePath,
                               QRect initialGeometry,
                               quint64 parentWindowId = 0,
                               QStringList grantedPermissions = QStringList(),
                               bool isDevelopment = false,
                               bool autoReload = false,
                               bool sharedRuntime = false,
                               QWidget* parent = nullptr);
    ~LinWidgetWebViewRuntime() override;

    bool initialize(QString* errorMessage = nullptr);
    void handleManagerMessage(const QJsonObject& message);
    bool isCompositionMode() const { return m_compositionController != nullptr; }

signals:
    void messageToManager(const QString& widgetId, const QJsonObject& message);

private:
    bool nativeEvent(const QByteArray& eventType, void* message, long* result) override;
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void forwardMouseInput(QMouseEvent* event, int eventKind);

    void setupWindow();
    void attachToParentWindow();
    QRect parentClientRect() const;
    QPoint clampToParent(const QPoint& requestedTopLeft) const;
    void sendManagerMessage(const QString& type, const QJsonObject& extra = QJsonObject());
    void renderNativePlaceholder();
    void renderHtmlWidget();
    void renderHtmlPlaceholder(const QString& reason = QString());
    void renderSharedHtmlRuntime();
    QString sharedRuntimeEntryPath() const;
    void installSharedWidgetApiBridge();
    void sendHostPageMessage(const QJsonObject& payload);
    void queueOrSendHostPageMessage(const QJsonObject& payload);
    void flushPendingHostPageMessages();
    void queueOrReplacePendingSharedManagerMessage(const QJsonObject& message);
    void flushPendingSharedManagerMessages();
    QJsonObject widgetInstancePayload(const QString& instanceId,
                                      const QString& packagePath,
                                      const WidgetManifest& manifest,
                                      const QRect& geometry,
                                      const QStringList& permissions,
                                      bool isDevelopment,
                                      bool autoReload,
                                      bool multiInstance,
                                      const QString& accessToken,
                                      const QString& entryUrl) const;
    QString virtualHostNameForInstance(const QString& instanceId,
                                       const QStringList& permissions,
                                       bool isDevelopment) const;
    bool mapWidgetVirtualHost(const QString& hostName, const QString& packagePath);
    QString webViewErrorString(long result) const;
    void installSharedWebResourceGuards(void* environment);
    void openDevToolsWhenReady();
    void setSharedWindowGeometry();
    void updateSharedWindowRegion();
    void syncSharedRegion(const QString& reason);
    void updateNativeWindowRegion(void* windowHandle);
    void installWebViewDragBridge();
    void installWebConsoleLogging();
    void installWebPermissionGuards();
    void installWebZoomGuards();
    void handleWebMessageJson(const QString& json);
    void appendLog(const QString& level, const QString& message) const;
    void appendInstanceLog(const QString& instanceId, const QString& level, const QString& message) const;
    QString logFilePath() const;
    QString logFilePathForId(const QString& id) const;
    void focusWebView(const QString& source = QString());
    void moveByDelta(const QPoint& delta);
    QSize clampWidgetSize(const QSize& requestedSize) const;
    void applyHostPosition(const QPoint& topLeft);
    void applyHostGeometry(const QRect& geometry);
    void updateWebViewBounds();

    QString m_widgetId;
    QString m_packagePath;
    QRect m_initialGeometry;
    quint64 m_parentWindowId = 0;
    QStringList m_grantedPermissions;
    bool m_isDevelopment = false;
    bool m_autoReload = false;
    bool m_sharedRuntime = false;
    WidgetManifest m_manifest;
    QLabel* m_placeholderLabel = nullptr;
    void* m_webViewController = nullptr;
    void* m_compositionController = nullptr;
    void* m_webView = nullptr;
    void* m_d3d11Device = nullptr;
    void* m_dcompDevice = nullptr;
    void* m_dcompTarget = nullptr;
    void* m_rootVisual = nullptr;
    void* m_webViewVisual = nullptr;
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
    QMap<QString, QString> m_sharedWidgetAccessTokens;
    QMap<QString, QString> m_sharedWidgetRoles;
    QMap<QString, QString> m_virtualHostMappings;
    QVector<QJsonObject> m_pendingHostPageMessages;
    QVector<QJsonObject> m_pendingSharedManagerMessages;
    bool m_hostPageReady = false;
    bool m_showWebViewBounds = false;
    bool m_forceShowSharedBackground = false;
    bool m_pendingOpenDevTools = false;
    quint64 m_lastReportedWebViewWindow = 0;
};

}

