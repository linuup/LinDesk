#include "services/SpaceLookIpcClient.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTimer>

namespace {

constexpr int kConnectTimeoutMs = 500;
const char kSpaceLookServerName[] = "LinDesk.SpaceLook";

class SpaceLookIpcRequest : public QObject
{
public:
    SpaceLookIpcRequest(const QString& filePath, const QByteArray& payload, QObject* parent = nullptr)
        : QObject(parent)
        , m_filePath(filePath)
        , m_payload(payload)
    {
        m_timeoutTimer.setParent(this);
        m_timeoutTimer.setSingleShot(true);

        connect(&m_socket, &QLocalSocket::connected, this, [this]() {
            const qint64 queuedBytes = m_socket.write(m_payload);
            if (queuedBytes != m_payload.size()) {
                fail(QStringLiteral("Failed to queue payload for write"));
                return;
            }
        });

        connect(&m_socket, &QLocalSocket::bytesWritten, this, [this](qint64 bytesWritten) {
            if (m_finished) {
                return;
            }

            m_totalBytesWritten += bytesWritten;
            if (m_totalBytesWritten < m_payload.size()) {
                return;
            }

            m_timeoutTimer.stop();
            m_finished = true;
            qDebug().noquote() << QStringLiteral("[SpaceLook IPC] preview request sent successfully: path=\"%1\"")
                .arg(m_filePath);
            m_socket.flush();
            m_socket.disconnectFromServer();
            deleteLater();
        });

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        connect(&m_socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
            fail(m_socket.errorString());
        });
#else
        connect(&m_socket,
                QOverload<QLocalSocket::LocalSocketError>::of(&QLocalSocket::error),
                this,
                [this](QLocalSocket::LocalSocketError) {
                    fail(m_socket.errorString());
                });
#endif

        connect(&m_socket, &QLocalSocket::disconnected, this, [this]() {
            if (!m_finished) {
                fail(QStringLiteral("Disconnected before request completed"));
                return;
            }
            deleteLater();
        });

        connect(&m_timeoutTimer, &QTimer::timeout, this, [this]() {
            fail(QStringLiteral("Timed out while sending preview request"));
        });
    }

    void start()
    {
        qDebug().noquote() << QStringLiteral("[SpaceLook IPC] sending preview request: path=\"%1\" payload=%2")
            .arg(m_filePath, QString::fromUtf8(m_payload));
        m_timeoutTimer.start(kConnectTimeoutMs);
        m_socket.connectToServer(QString::fromLatin1(kSpaceLookServerName));
    }

private:
    void fail(const QString& errorMessage)
    {
        if (m_finished) {
            return;
        }

        m_finished = true;
        m_timeoutTimer.stop();
        qWarning().noquote() << QStringLiteral("[SpaceLook IPC] failed to send preview request: path=\"%1\" error=\"%2\"")
            .arg(m_filePath, errorMessage);
        if (m_socket.state() != QLocalSocket::UnconnectedState) {
            m_socket.abort();
        }
        deleteLater();
    }

    QString m_filePath;
    QByteArray m_payload;
    QLocalSocket m_socket;
    QTimer m_timeoutTimer;
    qint64 m_totalBytesWritten = 0;
    bool m_finished = false;
};

}

bool SpaceLookIpcClient::sendPreviewRequest(const QString& filePath, QString* errorMessage)
{
    const QString normalizedPath = QDir::toNativeSeparators(filePath.trimmed());
    if (normalizedPath.isEmpty()) {
        qWarning().noquote() << "[SpaceLook IPC] preview request rejected: empty file path";
        if (errorMessage) {
            *errorMessage = QStringLiteral("Empty file path");
        }
        return false;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("cmd"), QStringLiteral("preview"));
    payload.insert(QStringLiteral("path"), normalizedPath);

    QByteArray bytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    bytes.append('\n');

    auto* request = new SpaceLookIpcRequest(normalizedPath, bytes, QCoreApplication::instance());
    request->start();
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}
