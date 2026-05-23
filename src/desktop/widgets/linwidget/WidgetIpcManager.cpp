#include "desktop/widgets/linwidget/WidgetIpcManager.h"

#include "desktop/widgets/linwidget/WidgetIpcProtocol.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QList>

namespace LinWidget {

WidgetIpcManager::WidgetIpcManager(QObject* parent)
    : QObject(parent)
    , m_server(new QLocalServer(this))
{
    connect(m_server, &QLocalServer::newConnection, this, &WidgetIpcManager::handleNewConnection);
}

bool WidgetIpcManager::start(const QString& serverName)
{
    if (m_server->isListening()) {
        return true;
    }

    m_serverName = serverName;
    if (m_server->listen(m_serverName)) {
        return true;
    }

    QLocalServer::removeServer(m_serverName);
    if (m_server->listen(m_serverName)) {
        return true;
    }

    emit errorOccurred(m_server->errorString());
    return false;
}

void WidgetIpcManager::stop()
{
    if (m_server->isListening()) {
        m_server->close();
    }

    const QList<QLocalSocket*> sockets = m_socketToWidget.keys();
    m_socketToWidget.clear();
    m_widgetToSocket.clear();
    for (QLocalSocket* socket : sockets) {
        if (!socket) {
            continue;
        }
        socket->disconnect(this);
        socket->disconnectFromServer();
        socket->deleteLater();
    }
}

bool WidgetIpcManager::sendToWidget(const QString& widgetId, const QJsonObject& message)
{
    QLocalSocket* socket = m_widgetToSocket.value(widgetId, nullptr);
    if (!socket || socket->state() != QLocalSocket::ConnectedState) {
        return false;
    }

    const QByteArray payload = encodeIpcMessage(message);
    return socket->write(payload) == payload.size();
}

void WidgetIpcManager::handleNewConnection()
{
    while (QLocalSocket* socket = m_server->nextPendingConnection()) {
        connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
            handleReadyRead(socket);
        });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
            handleDisconnected(socket);
        });
    }
}

void WidgetIpcManager::handleReadyRead(QLocalSocket* socket)
{
    if (!socket) {
        return;
    }

    while (socket->canReadLine()) {
        QJsonObject message;
        QString errorMessage;
        if (!decodeIpcMessage(socket->readLine(), &message, &errorMessage)) {
            emit errorOccurred(errorMessage);
            continue;
        }

        const QString widgetId = message.value(QStringLiteral("widgetId")).toString().trimmed();
        if (!widgetId.isEmpty() && !m_socketToWidget.contains(socket)) {
            registerWidgetSocket(widgetId, socket);
        }

        emit messageReceived(widgetId, message);
    }
}

void WidgetIpcManager::handleDisconnected(QLocalSocket* socket)
{
    if (!socket) {
        return;
    }

    const QString widgetId = m_socketToWidget.take(socket);
    if (!widgetId.isEmpty()) {
        m_widgetToSocket.remove(widgetId);
        emit widgetDisconnected(widgetId);
    }
    socket->deleteLater();
}

void WidgetIpcManager::registerWidgetSocket(const QString& widgetId, QLocalSocket* socket)
{
    if (widgetId.isEmpty() || !socket) {
        return;
    }
    m_socketToWidget.insert(socket, widgetId);
    m_widgetToSocket.insert(widgetId, socket);
    emit widgetConnected(widgetId);
}

}
