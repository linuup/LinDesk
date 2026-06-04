#include "ai/AIRequestManager.h"

#include <QTimer>
#include <QUuid>

namespace AI {

AIRequestManager::AIRequestManager(QObject* parent)
    : QObject(parent)
    , m_client(this)
{
    connect(&m_client, &AIClient::replyFinished, this, [this](const AI::ChatResponse& response) {
        finishRequest(response.requestId);
        emit replyFinished(response);
    });
    connect(&m_client, &AIClient::streamDelta, this, [this](const QString& requestId, const QString& delta) {
        resetStreamIdleTimeout(requestId);
        emit streamDelta(requestId, delta);
    });
    connect(&m_client, &AIClient::streamFinished, this, [this](const AI::ChatResponse& response) {
        finishRequest(response.requestId);
        emit streamFinished(response);
    });
    connect(&m_client, &AIClient::requestFailed, this, [this](const AI::ChatResponse& response) {
        if (!m_requests.contains(response.requestId)) {
            return;
        }
        finishRequest(response.requestId);
        emit requestFailed(response);
    });
}

void AIRequestManager::setProvider(AIClient::ProviderKind providerKind)
{
    m_client.setProvider(providerKind);
}

AIClient::ProviderKind AIRequestManager::provider() const
{
    return m_client.provider();
}

void AIRequestManager::sendMessage(ChatRequest request, const AIRequestTimeouts& timeouts)
{
    startRequest(std::move(request), false, timeouts);
}

void AIRequestManager::streamReply(ChatRequest request, const AIRequestTimeouts& timeouts)
{
    startRequest(std::move(request), true, timeouts);
}

void AIRequestManager::cancelRequest(const QString& requestId)
{
    if (!m_requests.contains(requestId)) {
        return;
    }
    m_client.cancelRequest(requestId);
    finishRequest(requestId);
    emit requestCanceled(requestId);
}

void AIRequestManager::cancelAll()
{
    const QStringList requestIds = m_requests.keys();
    for (const QString& requestId : requestIds) {
        cancelRequest(requestId);
    }
}

void AIRequestManager::startRequest(ChatRequest request, bool stream, const AIRequestTimeouts& timeouts)
{
    if (request.requestId.isEmpty()) {
        request.requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    RequestState state;
    state.requestId = request.requestId;
    state.model = request.model;
    state.stream = stream;
    state.timeouts = timeouts;
    state.timeoutTimer = new QTimer(this);
    state.timeoutTimer->setSingleShot(true);
    state.timeoutTimer->setInterval(qMax(1000, stream ? timeouts.firstChunkTimeoutMs : timeouts.totalTimeoutMs));
    connect(state.timeoutTimer, &QTimer::timeout, this, [this, requestId = request.requestId, stream]() {
        failWithTimeout(requestId, stream
            ? QStringLiteral("AI stream timed out before the first chunk.")
            : QStringLiteral("AI request timed out."));
    });
    state.timeoutTimer->start();

    if (stream && timeouts.maxStreamDurationMs > 0) {
        state.maxDurationTimer = new QTimer(this);
        state.maxDurationTimer->setSingleShot(true);
        state.maxDurationTimer->setInterval(qMax(1000, timeouts.maxStreamDurationMs));
        connect(state.maxDurationTimer, &QTimer::timeout, this, [this, requestId = request.requestId]() {
            failWithTimeout(requestId, QStringLiteral("AI stream reached the maximum duration."));
        });
        state.maxDurationTimer->start();
    }
    m_requests.insert(request.requestId, state);

    if (stream) {
        m_client.streamReply(request);
    } else {
        m_client.sendMessage(request);
    }
}

void AIRequestManager::resetStreamIdleTimeout(const QString& requestId)
{
    auto it = m_requests.find(requestId);
    if (it == m_requests.end() || !it->stream || !it->timeoutTimer) {
        return;
    }

    it->firstChunkReceived = true;
    it->timeoutTimer->stop();
    it->timeoutTimer->setInterval(qMax(1000, it->timeouts.idleTimeoutMs));
    disconnect(it->timeoutTimer, &QTimer::timeout, this, nullptr);
    connect(it->timeoutTimer, &QTimer::timeout, this, [this, requestId]() {
        failWithTimeout(requestId, QStringLiteral("AI stream idle timed out."));
    });
    it->timeoutTimer->start();
}

void AIRequestManager::finishRequest(const QString& requestId)
{
    auto it = m_requests.find(requestId);
    if (it == m_requests.end()) {
        return;
    }
    if (it->timeoutTimer) {
        it->timeoutTimer->stop();
        it->timeoutTimer->deleteLater();
    }
    if (it->maxDurationTimer) {
        it->maxDurationTimer->stop();
        it->maxDurationTimer->deleteLater();
    }
    m_requests.erase(it);
}

void AIRequestManager::failWithTimeout(const QString& requestId, const QString& reason)
{
    auto it = m_requests.find(requestId);
    if (it == m_requests.end()) {
        return;
    }

    const RequestState state = *it;
    m_client.cancelRequest(requestId);
    finishRequest(requestId);

    ChatResponse response;
    response.ok = false;
    response.requestId = requestId;
    response.model = state.model;
    response.error = reason;
    emit requestTimedOut(requestId);
    emit requestFailed(response);
}

}
