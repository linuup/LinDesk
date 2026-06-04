#include "ai/client/OllamaClient.h"

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
        return QUrl(QStringLiteral("http://127.0.0.1:11434/api/chat"));
    }
    QUrl url(baseUrl);
    QString path = url.path();
    if (!path.endsWith(QStringLiteral("/api/chat"))) {
        if (!path.endsWith(QLatin1Char('/'))) {
            path += QLatin1Char('/');
        }
        path += QStringLiteral("api/chat");
        url.setPath(path);
    }
    return url;
}

QJsonArray buildMessages(const ChatRequest& request)
{
    QJsonArray array;
    if (!request.systemPrompt.trimmed().isEmpty()) {
        QJsonObject systemMessage;
        systemMessage.insert(QStringLiteral("role"), QStringLiteral("system"));
        systemMessage.insert(QStringLiteral("content"), request.systemPrompt);
        array.append(systemMessage);
    }
    for (const ChatMessage& message : request.messages) {
        QJsonObject object = message.extra;
        object.insert(QStringLiteral("role"), message.role);
        object.insert(QStringLiteral("content"), message.content);
        array.append(object);
    }
    return array;
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
        response.text = response.rawObject.value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
        if (response.text.isEmpty()) {
            response.error = response.rawObject.value(QStringLiteral("error")).toString();
        }
    }
    response.ok = !response.text.isEmpty() && response.error.isEmpty();
    if (!response.ok && response.error.isEmpty()) {
        response.error = QStringLiteral("Ollama response did not contain assistant text.");
    }
    return response;
}

QNetworkRequest makeRequest(const QUrl& url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    return request;
}

void logRequest(const QString& provider, const QNetworkRequest& request, const QJsonObject& payload)
{
    QJsonObject headers;
    headers.insert(QStringLiteral("Content-Type"), QStringLiteral("application/json"));

    qDebug().noquote() << QStringLiteral("[AIRequest][%1] url=%2")
        .arg(provider, request.url().toString());
    qDebug().noquote() << QStringLiteral("[AIRequest][%1] headers=%2")
        .arg(provider, QString::fromUtf8(QJsonDocument(headers).toJson(QJsonDocument::Compact)));
    qDebug().noquote() << QStringLiteral("[AIRequest][%1] payload=%2")
        .arg(provider, QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
}

}

OllamaClient::OllamaClient(QObject* parent)
    : AIProviderBase(parent)
{
}

QString OllamaClient::providerId() const
{
    return QStringLiteral("ollama");
}

void OllamaClient::sendMessage(const ChatRequest& request)
{
    if (request.model.trimmed().isEmpty()) {
        emit requestFailed(invalidRequest(providerId(), request, QStringLiteral("Missing model.")));
        return;
    }

    QJsonObject payload = request.options;
    payload.insert(QStringLiteral("model"), request.model);
    payload.insert(QStringLiteral("messages"), buildMessages(request));
    payload.insert(QStringLiteral("stream"), false);
    if (request.temperature >= 0.0 || request.maxTokens > 0) {
        QJsonObject options = payload.value(QStringLiteral("options")).toObject();
        if (request.temperature >= 0.0) {
            options.insert(QStringLiteral("temperature"), request.temperature);
        }
        if (request.maxTokens > 0) {
            options.insert(QStringLiteral("num_predict"), request.maxTokens);
        }
        payload.insert(QStringLiteral("options"), options);
    }

    const QNetworkRequest networkRequest = makeRequest(endpointUrl(request.baseUrl));
    logRequest(providerId(), networkRequest, payload);
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

void OllamaClient::streamReply(const ChatRequest& request)
{
    if (request.model.trimmed().isEmpty()) {
        emit requestFailed(invalidRequest(providerId(), request, QStringLiteral("Missing model.")));
        return;
    }

    QJsonObject payload = request.options;
    payload.insert(QStringLiteral("model"), request.model);
    payload.insert(QStringLiteral("messages"), buildMessages(request));
    payload.insert(QStringLiteral("stream"), true);
    if (request.temperature >= 0.0 || request.maxTokens > 0) {
        QJsonObject options = payload.value(QStringLiteral("options")).toObject();
        if (request.temperature >= 0.0) {
            options.insert(QStringLiteral("temperature"), request.temperature);
        }
        if (request.maxTokens > 0) {
            options.insert(QStringLiteral("num_predict"), request.maxTokens);
        }
        payload.insert(QStringLiteral("options"), options);
    }

    const QNetworkRequest networkRequest = makeRequest(endpointUrl(request.baseUrl));
    logRequest(providerId(), networkRequest, payload);
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
            if (line.isEmpty()) {
                continue;
            }
            const QJsonDocument document = QJsonDocument::fromJson(line);
            const QJsonObject root = document.object();
            const QString text = root.value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
            if (!text.isEmpty()) {
                it->accumulatedText += text;
                emit streamDelta(it->request.requestId, text);
            }
            if (root.value(QStringLiteral("done")).toBool(false)) {
                it->done = true;
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
                response.error = QStringLiteral("Ollama stream finished without text.");
            }
            qDebug().noquote() << QStringLiteral("[AIResponse][%1] %2=%3")
                .arg(providerId(), response.ok ? QStringLiteral("text") : QStringLiteral("error"), response.ok ? response.text : response.error);
            emit streamFinished(response);
        }
        reply->deleteLater();
    });
}

void OllamaClient::cancelRequest(const QString& requestId)
{
    QNetworkReply* reply = m_repliesByRequestId.take(requestId);
    if (reply) {
        m_streams.remove(reply);
        reply->abort();
    }
}

}
