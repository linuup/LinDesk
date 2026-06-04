#pragma once

#include "ai/AIClient.h"

#include <QHash>
#include <QObject>

class QTimer;

namespace AI {

struct AIRequestTimeouts {
    int totalTimeoutMs = 180000;
    int firstChunkTimeoutMs = 120000;
    int idleTimeoutMs = 60000;
    int maxStreamDurationMs = 0;
};

class AIRequestManager : public QObject {
    Q_OBJECT

public:
    explicit AIRequestManager(QObject* parent = nullptr);

    void setProvider(AIClient::ProviderKind providerKind);
    AIClient::ProviderKind provider() const;

    void sendMessage(ChatRequest request, const AIRequestTimeouts& timeouts = AIRequestTimeouts());
    void streamReply(ChatRequest request, const AIRequestTimeouts& timeouts = AIRequestTimeouts());
    void cancelRequest(const QString& requestId);
    void cancelAll();

signals:
    void replyFinished(const AI::ChatResponse& response);
    void streamDelta(const QString& requestId, const QString& delta);
    void streamFinished(const AI::ChatResponse& response);
    void requestFailed(const AI::ChatResponse& response);
    void requestTimedOut(const QString& requestId);
    void requestCanceled(const QString& requestId);

private:
    struct RequestState {
        QString requestId;
        QString model;
        bool stream = false;
        bool firstChunkReceived = false;
        AIRequestTimeouts timeouts;
        QTimer* timeoutTimer = nullptr;
        QTimer* maxDurationTimer = nullptr;
    };

    void startRequest(ChatRequest request, bool stream, const AIRequestTimeouts& timeouts);
    void resetStreamIdleTimeout(const QString& requestId);
    void finishRequest(const QString& requestId);
    void failWithTimeout(const QString& requestId, const QString& reason);

    AIClient m_client;
    QHash<QString, RequestState> m_requests;
};

}

Q_DECLARE_METATYPE(AI::AIRequestTimeouts)
