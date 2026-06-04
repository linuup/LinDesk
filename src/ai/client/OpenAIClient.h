#pragma once

#include "ai/client/AIProviderBase.h"

#include <QHash>
#include <QUrl>

class QNetworkReply;

namespace AI {

class OpenAIClient : public AIProviderBase {
    Q_OBJECT

public:
    explicit OpenAIClient(QObject* parent = nullptr);
    OpenAIClient(const QString& providerId, const QUrl& defaultBaseUrl, QObject* parent = nullptr);

    QString providerId() const override;
    void sendMessage(const ChatRequest& request) override;
    void streamReply(const ChatRequest& request) override;
    void cancelRequest(const QString& requestId) override;

private:
    struct StreamState {
        ChatRequest request;
        QByteArray buffer;
        QString accumulatedText;
        bool done = false;
    };

    QHash<QNetworkReply*, StreamState> m_streams;
    QHash<QString, QNetworkReply*> m_repliesByRequestId;
    QString m_providerId;
    QUrl m_defaultBaseUrl;
};

}
