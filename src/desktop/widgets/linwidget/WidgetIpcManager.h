#pragma once

#include <QObject>
#include <QHash>
#include <QJsonObject>

class QLocalServer;
class QLocalSocket;

namespace LinWidget {

class WidgetIpcManager : public QObject {
    Q_OBJECT

public:
    explicit WidgetIpcManager(QObject* parent = nullptr);

    bool start(const QString& serverName);
    QString serverName() const { return m_serverName; }
    bool sendToWidget(const QString& widgetId, const QJsonObject& message);

signals:
    void widgetConnected(const QString& widgetId);
    void widgetDisconnected(const QString& widgetId);
    void messageReceived(const QString& widgetId, const QJsonObject& message);
    void errorOccurred(const QString& message);

private:
    void handleNewConnection();
    void handleReadyRead(QLocalSocket* socket);
    void handleDisconnected(QLocalSocket* socket);
    void registerWidgetSocket(const QString& widgetId, QLocalSocket* socket);

    QLocalServer* m_server = nullptr;
    QString m_serverName;
    QHash<QLocalSocket*, QString> m_socketToWidget;
    QHash<QString, QLocalSocket*> m_widgetToSocket;
};

}
