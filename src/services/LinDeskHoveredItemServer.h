#pragma once

#include <QObject>

class QLocalServer;
class QLocalSocket;

class LinDeskHoveredItemServer : public QObject
{
    Q_OBJECT

public:
    explicit LinDeskHoveredItemServer(QObject* parent = nullptr);
    bool startListening();

private:
    void handleNewConnection();
    void handleReadyRead(QLocalSocket* socket);
    void handleSocketDisconnected(QLocalSocket* socket);

    QLocalServer* m_server = nullptr;
};
