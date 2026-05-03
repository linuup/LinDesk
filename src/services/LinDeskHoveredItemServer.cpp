#include "services/LinDeskHoveredItemServer.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>

#include "desktop/ListView.h"

namespace {

const char kLinDeskHoveredItemServerName[] = "LinDesk.HoveredItem";

QByteArray buildResponsePayload()
{
    QJsonObject payload;
    payload.insert(QStringLiteral("ok"), false);
    payload.insert(QStringLiteral("path"), QString());
    payload.insert(QStringLiteral("timestamp_ms"), QString::number(QDateTime::currentMSecsSinceEpoch()));

    ListView* listView = ListView::instance();
    if (!listView) {
        payload.insert(QStringLiteral("error"), QStringLiteral("ListView instance is unavailable"));
        return QJsonDocument(payload).toJson(QJsonDocument::Compact) + '\n';
    }

    const QString hoveredPath = QDir::toNativeSeparators(listView->currentSpaceLookHoveredPath().trimmed());
    if (hoveredPath.isEmpty()) {
        payload.insert(QStringLiteral("error"), QStringLiteral("No hovered item is available"));
        return QJsonDocument(payload).toJson(QJsonDocument::Compact) + '\n';
    }

    payload.insert(QStringLiteral("ok"), true);
    payload.insert(QStringLiteral("path"), hoveredPath);
    payload.insert(QStringLiteral("error"), QString());
    return QJsonDocument(payload).toJson(QJsonDocument::Compact) + '\n';
}

}

LinDeskHoveredItemServer::LinDeskHoveredItemServer(QObject* parent)
    : QObject(parent)
    , m_server(new QLocalServer(this))
{
    connect(m_server, &QLocalServer::newConnection, this, &LinDeskHoveredItemServer::handleNewConnection);
}

bool LinDeskHoveredItemServer::startListening()
{
    if (m_server->isListening()) {
        return true;
    }

    const QString serverName = QString::fromLatin1(kLinDeskHoveredItemServerName);
    if (m_server->listen(serverName)) {
        qDebug().noquote() << QStringLiteral("[LinDesk Hover IPC] listening on \"%1\"").arg(serverName);
        return true;
    }

    QLocalServer::removeServer(serverName);
    if (m_server->listen(serverName)) {
        qDebug().noquote() << QStringLiteral("[LinDesk Hover IPC] listening on \"%1\" after cleanup").arg(serverName);
        return true;
    }

    qWarning().noquote() << QStringLiteral("[LinDesk Hover IPC] failed to start listener: error=\"%1\"")
        .arg(m_server->errorString());
    return false;
}

void LinDeskHoveredItemServer::handleNewConnection()
{
    while (QLocalSocket* socket = m_server->nextPendingConnection()) {
        connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
            handleReadyRead(socket);
        });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
            handleSocketDisconnected(socket);
        });
    }
}

void LinDeskHoveredItemServer::handleReadyRead(QLocalSocket* socket)
{
    if (!socket) {
        return;
    }

    while (socket->canReadLine()) {
        const QByteArray rawLine = socket->readLine().trimmed();
        if (rawLine.isEmpty()) {
            continue;
        }

        const QJsonDocument jsonDocument = QJsonDocument::fromJson(rawLine);
        if (!jsonDocument.isObject()) {
            qWarning().noquote() << QStringLiteral("[LinDesk Hover IPC] ignored invalid request payload=%1")
                .arg(QString::fromUtf8(rawLine));
            socket->write(buildResponsePayload());
            socket->flush();
            socket->disconnectFromServer();
            continue;
        }

        const QJsonObject request = jsonDocument.object();
        const QString command = request.value(QStringLiteral("cmd")).toString().trimmed();
        if (command != QStringLiteral("get_hovered_item")) {
            qWarning().noquote() << QStringLiteral("[LinDesk Hover IPC] ignored unsupported command: cmd=\"%1\" payload=%2")
                .arg(command, QString::fromUtf8(rawLine));
            socket->write(buildResponsePayload());
            socket->flush();
            socket->disconnectFromServer();
            continue;
        }

        const QByteArray response = buildResponsePayload();
        qDebug().noquote() << QStringLiteral("[LinDesk Hover IPC] responding with payload=%1")
            .arg(QString::fromUtf8(response).trimmed());
        socket->write(response);
        socket->flush();
        socket->disconnectFromServer();
    }
}

void LinDeskHoveredItemServer::handleSocketDisconnected(QLocalSocket* socket)
{
    if (!socket) {
        return;
    }

    socket->deleteLater();
}
