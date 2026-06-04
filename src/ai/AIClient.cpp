#include "ai/AIClient.h"

#include "ai/client/ClaudeClient.h"
#include "ai/client/GoogleClient.h"
#include "ai/client/OllamaClient.h"
#include "ai/client/OpenAIClient.h"

namespace AI {

AIClient::AIClient(QObject* parent)
    : QObject(parent)
{
    rebuildProvider();
}

AIClient::~AIClient() = default;

void AIClient::setProvider(ProviderKind providerKind)
{
    if (m_providerKind == providerKind && m_provider) {
        return;
    }
    m_providerKind = providerKind;
    rebuildProvider();
}

AIClient::ProviderKind AIClient::provider() const
{
    return m_providerKind;
}

void AIClient::sendMessage(const ChatRequest& request)
{
    if (!m_provider) {
        ChatResponse response;
        response.requestId = request.requestId;
        response.error = QStringLiteral("AI provider is not initialized.");
        emit requestFailed(response);
        return;
    }
    m_provider->sendMessage(request);
}

void AIClient::streamReply(const ChatRequest& request)
{
    if (!m_provider) {
        ChatResponse response;
        response.requestId = request.requestId;
        response.error = QStringLiteral("AI provider is not initialized.");
        emit requestFailed(response);
        return;
    }
    m_provider->streamReply(request);
}

void AIClient::cancelRequest(const QString& requestId)
{
    if (m_provider) {
        m_provider->cancelRequest(requestId);
    }
}

void AIClient::rebuildProvider()
{
    switch (m_providerKind) {
    case ProviderKind::OpenAI:
        m_provider = std::make_unique<OpenAIClient>();
        break;
    case ProviderKind::Claude:
        m_provider = std::make_unique<ClaudeClient>();
        break;
    case ProviderKind::Ollama:
        m_provider = std::make_unique<OllamaClient>();
        break;
    case ProviderKind::Google:
        m_provider = std::make_unique<GoogleClient>();
        break;
    case ProviderKind::DeepSeek:
        m_provider = std::make_unique<OpenAIClient>(
            QStringLiteral("deepseek"),
            QUrl(QStringLiteral("https://api.deepseek.com")));
        break;
    case ProviderKind::Qwen:
        m_provider = std::make_unique<OpenAIClient>(
            QStringLiteral("qwen"),
            QUrl(QStringLiteral("https://dashscope.aliyuncs.com/compatible-mode/v1")));
        break;
    }
    bindProviderSignals();
}

void AIClient::bindProviderSignals()
{
    if (!m_provider) {
        return;
    }
    connect(m_provider.get(), &AIProviderBase::replyFinished,
            this, &AIClient::replyFinished);
    connect(m_provider.get(), &AIProviderBase::streamDelta,
            this, &AIClient::streamDelta);
    connect(m_provider.get(), &AIProviderBase::streamFinished,
            this, &AIClient::streamFinished);
    connect(m_provider.get(), &AIProviderBase::requestFailed,
            this, &AIClient::requestFailed);
}

}
