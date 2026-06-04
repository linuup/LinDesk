#include "ai/AIWorker.h"

namespace AI {

AIWorker::AIWorker(QObject* parent)
    : QObject(parent)
    , m_requestManager(this)
{
    connect(&m_requestManager, &AIRequestManager::replyFinished,
            this, &AIWorker::replyFinished);
    connect(&m_requestManager, &AIRequestManager::streamDelta,
            this, &AIWorker::streamDelta);
    connect(&m_requestManager, &AIRequestManager::streamFinished,
            this, &AIWorker::streamFinished);
    connect(&m_requestManager, &AIRequestManager::requestFailed,
            this, &AIWorker::requestFailed);
    connect(&m_requestManager, &AIRequestManager::requestTimedOut,
            this, &AIWorker::requestTimedOut);
    connect(&m_requestManager, &AIRequestManager::requestCanceled,
            this, &AIWorker::requestCanceled);
}

void AIWorker::setProvider(AIClient::ProviderKind providerKind)
{
    m_requestManager.setProvider(providerKind);
}

void AIWorker::sendMessage(const ChatRequest& request, const AIRequestTimeouts& timeouts)
{
    m_requestManager.sendMessage(request, timeouts);
}

void AIWorker::streamReply(const ChatRequest& request, const AIRequestTimeouts& timeouts)
{
    m_requestManager.streamReply(request, timeouts);
}

void AIWorker::cancelRequest(const QString& requestId)
{
    m_requestManager.cancelRequest(requestId);
}

void AIWorker::cancelAll()
{
    m_requestManager.cancelAll();
}

}
