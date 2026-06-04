#pragma once

#include "ai/AIClient.h"
#include "ai/AIRequestManager.h"
#include "ai/AIWorker.h"

#include <QObject>
#include <QThread>

namespace AI {

class AICoreService : public QObject {
    Q_OBJECT

public:
    explicit AICoreService(QObject* parent = nullptr);
    ~AICoreService() override;

    void setProvider(AIClient::ProviderKind providerKind);
    AIClient::ProviderKind provider() const;

    void sendMessage(const ChatRequest& request);
    void streamReply(const ChatRequest& request);
    void cancelRequest(const QString& requestId);
    void cancelAll();
    void setDefaultTimeouts(const AIRequestTimeouts& timeouts);
    AIRequestTimeouts defaultTimeouts() const;

signals:
    void replyFinished(const AI::ChatResponse& response);
    void streamDelta(const QString& requestId, const QString& delta);
    void streamFinished(const AI::ChatResponse& response);
    void requestFailed(const AI::ChatResponse& response);
    void requestTimedOut(const QString& requestId);
    void requestCanceled(const QString& requestId);

private:
    QThread m_workerThread;
    AIWorker* m_worker = nullptr;
    AIClient::ProviderKind m_providerKind = AIClient::ProviderKind::OpenAI;
    AIRequestTimeouts m_defaultTimeouts;
};

}
