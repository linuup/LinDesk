#include "ai/AICoreService.h"

#include <QMetaObject>
#include <QMetaType>

namespace AI {

AICoreService::AICoreService(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<AI::AIClient::ProviderKind>("AI::AIClient::ProviderKind");
    qRegisterMetaType<AI::ChatRequest>("AI::ChatRequest");
    qRegisterMetaType<AI::ChatResponse>("AI::ChatResponse");
    qRegisterMetaType<AI::AIRequestTimeouts>("AI::AIRequestTimeouts");

    m_worker = new AIWorker();
    m_worker->moveToThread(&m_workerThread);

    connect(&m_workerThread, &QThread::finished,
            m_worker, &QObject::deleteLater);
    connect(m_worker, &AIWorker::replyFinished,
            this, &AICoreService::replyFinished);
    connect(m_worker, &AIWorker::streamDelta,
            this, &AICoreService::streamDelta);
    connect(m_worker, &AIWorker::streamFinished,
            this, &AICoreService::streamFinished);
    connect(m_worker, &AIWorker::requestFailed,
            this, &AICoreService::requestFailed);
    connect(m_worker, &AIWorker::requestTimedOut,
            this, &AICoreService::requestTimedOut);
    connect(m_worker, &AIWorker::requestCanceled,
            this, &AICoreService::requestCanceled);

    m_workerThread.setObjectName(QStringLiteral("LinDeskAIWorker"));
    m_workerThread.start();
}

AICoreService::~AICoreService()
{
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "cancelAll", Qt::BlockingQueuedConnection);
    }
    m_workerThread.quit();
    m_workerThread.wait(3000);
    m_worker = nullptr;
}

void AICoreService::setProvider(AIClient::ProviderKind providerKind)
{
    m_providerKind = providerKind;
    if (!m_worker) {
        return;
    }
    QMetaObject::invokeMethod(m_worker, "setProvider",
                              Qt::QueuedConnection,
                              Q_ARG(AI::AIClient::ProviderKind, providerKind));
}

AIClient::ProviderKind AICoreService::provider() const
{
    return m_providerKind;
}

void AICoreService::sendMessage(const ChatRequest& request)
{
    if (!m_worker) {
        return;
    }
    QMetaObject::invokeMethod(m_worker, "sendMessage",
                              Qt::QueuedConnection,
                              Q_ARG(AI::ChatRequest, request),
                              Q_ARG(AI::AIRequestTimeouts, m_defaultTimeouts));
}

void AICoreService::streamReply(const ChatRequest& request)
{
    if (!m_worker) {
        return;
    }
    QMetaObject::invokeMethod(m_worker, "streamReply",
                              Qt::QueuedConnection,
                              Q_ARG(AI::ChatRequest, request),
                              Q_ARG(AI::AIRequestTimeouts, m_defaultTimeouts));
}

void AICoreService::cancelRequest(const QString& requestId)
{
    if (!m_worker) {
        return;
    }
    QMetaObject::invokeMethod(m_worker, "cancelRequest",
                              Qt::QueuedConnection,
                              Q_ARG(QString, requestId));
}

void AICoreService::cancelAll()
{
    if (!m_worker) {
        return;
    }
    QMetaObject::invokeMethod(m_worker, "cancelAll", Qt::QueuedConnection);
}

void AICoreService::setDefaultTimeouts(const AIRequestTimeouts& timeouts)
{
    m_defaultTimeouts = timeouts;
}

AIRequestTimeouts AICoreService::defaultTimeouts() const
{
    return m_defaultTimeouts;
}

}
