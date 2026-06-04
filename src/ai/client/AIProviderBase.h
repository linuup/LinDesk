#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>

namespace AI {

struct ChatMessage {
    QString role;
    QString content;
    QJsonObject extra;
};

struct ChatRequest {
    QString requestId;
    QString model;
    QList<ChatMessage> messages;
    QString systemPrompt;
    QString apiKey;
    QUrl baseUrl;
    double temperature = -1.0;
    int maxTokens = 0;
    QJsonObject options;
};

struct ChatResponse {
    bool ok = false;
    QString requestId;
    QString provider;
    QString model;
    QString text;
    QString error;
    int statusCode = 0;
    QJsonObject rawObject;
    QJsonArray rawArray;
};

class AIProviderBase : public QObject {
    Q_OBJECT

public:
    explicit AIProviderBase(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    ~AIProviderBase() override = default;

    virtual QString providerId() const = 0;
    virtual void sendMessage(const ChatRequest& request) = 0;
    virtual void streamReply(const ChatRequest& request) = 0;
    virtual void cancelRequest(const QString& requestId)
    {
        Q_UNUSED(requestId);
    }

signals:
    void replyFinished(const AI::ChatResponse& response);
    void streamDelta(const QString& requestId, const QString& delta);
    void streamFinished(const AI::ChatResponse& response);
    void requestFailed(const AI::ChatResponse& response);

protected:
    QNetworkAccessManager m_network;
};

}

Q_DECLARE_METATYPE(AI::ChatMessage)
Q_DECLARE_METATYPE(AI::ChatRequest)
Q_DECLARE_METATYPE(AI::ChatResponse)
