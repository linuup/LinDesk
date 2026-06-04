#pragma once

#include "ai/AIRequestManager.h"

#include <QObject>

namespace AI {

class AIWorker : public QObject {
    Q_OBJECT

public:
    explicit AIWorker(QObject* parent = nullptr);

public slots:
    void setProvider(AI::AIClient::ProviderKind providerKind);
    void sendMessage(const AI::ChatRequest& request, const AI::AIRequestTimeouts& timeouts);
    void streamReply(const AI::ChatRequest& request, const AI::AIRequestTimeouts& timeouts);
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
    AIRequestManager m_requestManager;
};

}
