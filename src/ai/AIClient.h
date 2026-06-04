#pragma once

#include "ai/client/AIProviderBase.h"

#include <QObject>
#include <memory>

namespace AI {

class AIClient : public QObject {
    Q_OBJECT

public:
    enum class ProviderKind {
        OpenAI,
        Claude,
        Ollama,
        Google,
        DeepSeek,
        Qwen
    };

    explicit AIClient(QObject* parent = nullptr);
    ~AIClient() override;

    void setProvider(ProviderKind providerKind);
    ProviderKind provider() const;

    void sendMessage(const ChatRequest& request);
    void streamReply(const ChatRequest& request);
    void cancelRequest(const QString& requestId);

signals:
    void replyFinished(const AI::ChatResponse& response);
    void streamDelta(const QString& requestId, const QString& delta);
    void streamFinished(const AI::ChatResponse& response);
    void requestFailed(const AI::ChatResponse& response);

private:
    void rebuildProvider();
    void bindProviderSignals();

    ProviderKind m_providerKind = ProviderKind::OpenAI;
    std::unique_ptr<AIProviderBase> m_provider;
};

}
