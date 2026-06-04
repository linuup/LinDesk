#include "ai/client/ClaudeClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace AI {
namespace {

QUrl endpointUrl(const QUrl& baseUrl)
{
    if (!baseUrl.isValid() || baseUrl.isEmpty()) {
        return QUrl(QStringLiteral("https://api.anthropic.com/v1/messages"));
    }
    QUrl url(baseUrl);
    QString path = url.path();
    if (!path.endsWith(QStringLiteral("/messages"))) {
        if (!path.endsWith(QLatin1Char('/'))) {
            path += QLatin1Char('/');
        }
        path += QStringLiteral("messages");
        url.setPath(path);
    }
    return url;
}

QJsonArray buildMessages(const ChatRequest& request)
{
    QJsonArray array;
    for (const ChatMessage& message : request.messages) {
        if (message.role == QStringLiteral("system")) {
            continue;
        }
        QJsonObject object = message.extra;
        object.insert(QStringLiteral("role"), message.role);
        object.insert(QStringLiteral("content"), message.content);
        array.append(object);
    }
    return array;
}

QString contentArrayToString(const QJsonArray& content)
{
    QStringList parts;
    for (const QJsonValue& value : content) {
        const QJsonObject object = value.toObject();
        const QString text = object.value(QStringLiteral("text")).toString();
        if (!text.isEmpty()) {
            parts.append(text);
        }
    }
    return parts.join(QString());
}

ChatResponse invalidRequest(const QString& provider, const ChatRequest& request, const QString& error)
{
    ChatResponse response;
    response.provider = provider;
    response.requestId = request.requestId;
    response.model = request.model;
    response.error = error;
    return response;
}

QNetworkRequest makeRequest(const QUrl& url, const QString& apiKey)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("x-api-key", apiKey.toUtf8());
    request.setRawHeader("anthropic-version", QByteArray("2023-06-01"));
    return request;
}

QString maskedApiKey(const QString& apiKey)
{
    if (apiKey.size() <= 8) {
        return QStringLiteral("***");
    }
    return apiKey.left(4) + QStringLiteral("...") + apiKey.right(4);
}

void logRequest(const QString& provider, const QNetworkRequest& request, const QJsonObject& payload, const QString& apiKey)
{
    QJsonObject headers;
    headers.insert(QStringLiteral("Content-Type"), QStringLiteral("application/json"));
    headers.insert(QStringLiteral("x-api-key"), maskedApiKey(apiKey));
    headers.insert(QStringLiteral("anthropic-version"), QStringLiteral("2023-06-01"));

    qDebug().noquote() << QStringLiteral("[AIRequest][%1] url=%2")
        .arg(provider, request.url().toString());
    qDebug().noquote() << QStringLiteral("[AIRequest][%1] headers=%2")
        .arg(provider, QString::fromUtf8(QJsonDocument(headers).toJson(QJsonDocument::Compact)));
    qDebug().noquote() << QStringLiteral("[AIRequest][%1] payload=%2")
        .arg(provider, QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
}

ChatResponse buildResponse(const QString& provider,
                           const ChatRequest& request,
                           QNetworkReply* reply,
                           const QJsonDocument& document)
{
    ChatResponse response;
    response.provider = provider;
    response.requestId = request.requestId;
    response.model = request.model;
    response.statusCode = reply
        ? reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
        : 0;
    if (document.isObject()) {
        response.rawObject = document.object();
        response.text = contentArrayToString(response.rawObject.value(QStringLiteral("content")).toArray());
        if (response.text.isEmpty()) {
            response.error = response.rawObject.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
        }
    }
    response.ok = !response.text.isEmpty() && response.error.isEmpty();
    if (!response.ok && response.error.isEmpty()) {
        response.error = QStringLiteral("Claude response did not contain assistant text.");
    }
    return response;
}

}

ClaudeClient::ClaudeClient(QObject* parent)
    : AIProviderBase(parent)
{
}

QString ClaudeClient::providerId() const
{
    return QStringLiteral("claude");
}

void ClaudeClient::sendMessage(const ChatRequest& request)
{
    if (request.apiKey.trimmed().isEmpty()) {
        emit requestFailed(invalidRequest(providerId(), request, QStringLiteral("Missing API key.")));
        return;
    }
    if (request.model.trimmed().isEmpty()) {
        emit requestFailed(invalidRequest(providerId(), request, QStringLiteral("Missing model.")));
        return;
    }

    QJsonObject payload = request.options;
    payload.insert(QStringLiteral("model"), request.model);
    payload.insert(QStringLiteral("messages"), buildMessages(request));
    payload.insert(QStringLiteral("max_tokens"), request.maxTokens > 0 ? request.maxTokens : 1024);
    if (!request.systemPrompt.trimmed().isEmpty()) {
        payload.insert(QStringLiteral("system"), request.systemPrompt);
    }
    if (request.temperature >= 0.0) {
        payload.insert(QStringLiteral("temperature"), request.temperature);
    }

    const QNetworkRequest networkRequest = makeRequest(endpointUrl(request.baseUrl), request.apiKey);
    logRequest(providerId(), networkRequest, payload, request.apiKey);
    QNetworkReply* reply = m_network.post(
        networkRequest,
        QJsonDocument(payload).toJson(QJsonDocument::Compact));
    m_repliesByRequestId.insert(request.requestId, reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, request]() {
        m_repliesByRequestId.remove(request.requestId);
        const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
        ChatResponse response = buildResponse(providerId(), request, reply, document);
        if (reply->error() != QNetworkReply::NoError && response.error.isEmpty()) {
            response.error = reply->errorString();
            response.ok = false;
        }
        if (response.ok) {
            qDebug().noquote() << QStringLiteral("[AIResponse][%1] text=%2")
                .arg(providerId(), response.text);
            emit replyFinished(response);
        } else {
            qDebug().noquote() << QStringLiteral("[AIResponse][%1] error=%2")
                .arg(providerId(), response.error);
            emit requestFailed(response);
        }
        reply->deleteLater();
    });
}

void ClaudeClient::streamReply(const ChatRequest& request)
{
    if (request.apiKey.trimmed().isEmpty()) {
        emit requestFailed(invalidRequest(providerId(), request, QStringLiteral("Missing API key.")));
        return;
    }
    if (request.model.trimmed().isEmpty()) {
        emit requestFailed(invalidRequest(providerId(), request, QStringLiteral("Missing model.")));
        return;
    }

    QJsonObject payload = request.options;
    payload.insert(QStringLiteral("model"), request.model);
    payload.insert(QStringLiteral("messages"), buildMessages(request));
    payload.insert(QStringLiteral("max_tokens"), request.maxTokens > 0 ? request.maxTokens : 1024);
    payload.insert(QStringLiteral("stream"), true);
    if (!request.systemPrompt.trimmed().isEmpty()) {
        payload.insert(QStringLiteral("system"), request.systemPrompt);
    }
    if (request.temperature >= 0.0) {
        payload.insert(QStringLiteral("temperature"), request.temperature);
    }

    const QNetworkRequest networkRequest = makeRequest(endpointUrl(request.baseUrl), request.apiKey);
    logRequest(providerId(), networkRequest, payload, request.apiKey);
    QNetworkReply* reply = m_network.post(
        networkRequest,
        QJsonDocument(payload).toJson(QJsonDocument::Compact));
    m_streams.insert(reply, StreamState { request });
    m_repliesByRequestId.insert(request.requestId, reply);

    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        auto it = m_streams.find(reply);
        if (it == m_streams.end()) {
            return;
        }
        it->buffer.append(reply->readAll());
        while (true) {
            const int newlineIndex = it->buffer.indexOf('\n');
            if (newlineIndex < 0) {
                break;
            }
            const QByteArray line = it->buffer.left(newlineIndex).trimmed();
            it->buffer.remove(0, newlineIndex + 1);
            if (!line.startsWith("data:")) {
                continue;
            }
            const QByteArray data = line.mid(5).trimmed();
            if (data.isEmpty()) {
                continue;
            }
            const QJsonDocument document = QJsonDocument::fromJson(data);
            const QJsonObject root = document.object();
            const QString eventType = root.value(QStringLiteral("type")).toString();
            if (eventType == QStringLiteral("content_block_delta")) {
                const QString text = root.value(QStringLiteral("delta")).toObject().value(QStringLiteral("text")).toString();
                if (!text.isEmpty()) {
                    it->accumulatedText += text;
                    emit streamDelta(it->request.requestId, text);
                }
            } else if (eventType == QStringLiteral("message_stop")) {
                it->done = true;
            } else if (eventType == QStringLiteral("error")) {
                it->done = false;
            }
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const StreamState state = m_streams.take(reply);
        m_repliesByRequestId.remove(state.request.requestId);
        ChatResponse response;
        response.ok = state.done || !state.accumulatedText.isEmpty();
        response.provider = providerId();
        response.requestId = state.request.requestId;
        response.model = state.request.model;
        response.text = state.accumulatedText;
        response.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            response.ok = false;
            response.error = reply->errorString();
            qDebug().noquote() << QStringLiteral("[AIResponse][%1] error=%2")
                .arg(providerId(), response.error);
            emit requestFailed(response);
        } else {
            if (!response.ok) {
                response.error = QStringLiteral("Claude stream finished without text.");
            }
            qDebug().noquote() << QStringLiteral("[AIResponse][%1] %2=%3")
                .arg(providerId(), response.ok ? QStringLiteral("text") : QStringLiteral("error"), response.ok ? response.text : response.error);
            emit streamFinished(response);
        }
        reply->deleteLater();
    });
}

void ClaudeClient::cancelRequest(const QString& requestId)
{
    QNetworkReply* reply = m_repliesByRequestId.take(requestId);
    if (reply) {
        m_streams.remove(reply);
        reply->abort();
    }
}

}
