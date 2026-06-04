#include "ai/client/OpenAIClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDebug>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace AI {
namespace {

QUrl endpointUrl(const QUrl& baseUrl, const QUrl& defaultBaseUrl)
{
    QUrl url = baseUrl.isValid() && !baseUrl.isEmpty()
        ? baseUrl
        : defaultBaseUrl;
    QString path = url.path();
    if (!path.endsWith(QStringLiteral("/chat/completions"))) {
        if (!path.endsWith(QLatin1Char('/'))) {
            path += QLatin1Char('/');
        }
        path += QStringLiteral("chat/completions");
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

QString contentToString(const QJsonValue& value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (!value.isArray()) {
        return QString();
    }
    QStringList parts;
    const QJsonArray array = value.toArray();
    for (const QJsonValue& item : array) {
        if (item.isString()) {
            parts.append(item.toString());
            continue;
        }
        const QJsonObject object = item.toObject();
        QString text = object.value(QStringLiteral("text")).toString();
        if (text.isEmpty()) {
            text = object.value(QStringLiteral("content")).toString();
        }
        if (!text.isEmpty()) {
            parts.append(text);
        }
    }
    return parts.join(QString());
}

QString firstNonEmpty(const QStringList& values)
{
    for (const QString& value : values) {
        if (!value.trimmed().isEmpty()) {
            return value;
        }
    }
    return QString();
}

QString compactJsonPreview(const QJsonDocument& document, const QByteArray& rawBytes)
{
    QByteArray preview = document.isNull()
        ? rawBytes.left(1200)
        : document.toJson(QJsonDocument::Compact).left(1200);
    return QString::fromUtf8(preview);
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
                           const QJsonDocument& document,
                           const QByteArray& rawBytes,
                           const QJsonParseError& parseError)
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
        const QJsonArray choices = response.rawObject.value(QStringLiteral("choices")).toArray();
        if (!choices.isEmpty()) {
            for (const QJsonValue& choiceValue : choices) {
                const QJsonObject choice = choiceValue.toObject();
                const QJsonObject message = choice.value(QStringLiteral("message")).toObject();
                response.text = firstNonEmpty({
                    contentToString(message.value(QStringLiteral("content"))),
                    contentToString(message.value(QStringLiteral("reasoning_content"))),
                    contentToString(message.value(QStringLiteral("reasoning"))),
                    contentToString(message.value(QStringLiteral("refusal"))),
                    contentToString(choice.value(QStringLiteral("text")))
                });
                if (!response.text.isEmpty()) {
                    break;
                }
            }
        }
        if (response.text.isEmpty()) {
            const QJsonValue errorValue = response.rawObject.value(QStringLiteral("error"));
            if (errorValue.isObject()) {
                const QJsonObject error = errorValue.toObject();
                response.error = firstNonEmpty({
                    error.value(QStringLiteral("message")).toString(),
                    error.value(QStringLiteral("type")).toString(),
                    error.value(QStringLiteral("code")).toString()
                });
            } else if (errorValue.isString()) {
                response.error = errorValue.toString();
            }
        }
    } else if (document.isArray()) {
        response.rawArray = document.array();
    }
    response.ok = !response.text.isEmpty() && response.error.isEmpty();
    if (!response.ok && response.error.isEmpty()) {
        if (parseError.error != QJsonParseError::NoError) {
            response.error = QStringLiteral("%1 response is not valid JSON. status=%2 parse=%3 raw=%4")
                .arg(provider)
                .arg(response.statusCode)
                .arg(parseError.errorString(), QString::fromUtf8(rawBytes.left(1200)));
        } else {
            response.error = QStringLiteral("%1 response did not contain assistant text. status=%2 raw=%3")
                .arg(provider)
                .arg(response.statusCode)
                .arg(compactJsonPreview(document, rawBytes));
        }
    }
    return response;
}

QNetworkRequest makeRequest(const QUrl& url, const QString& apiKey)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey.toUtf8());
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
    headers.insert(QStringLiteral("Authorization"), QStringLiteral("Bearer %1").arg(maskedApiKey(apiKey)));

    qDebug().noquote() << QStringLiteral("[AIRequest][%1] url=%2")
        .arg(provider, request.url().toString());
    qDebug().noquote() << QStringLiteral("[AIRequest][%1] headers=%2")
        .arg(provider, QString::fromUtf8(QJsonDocument(headers).toJson(QJsonDocument::Compact)));
    qDebug().noquote() << QStringLiteral("[AIRequest][%1] payload=%2")
        .arg(provider, QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
}

}

OpenAIClient::OpenAIClient(QObject* parent)
    : AIProviderBase(parent)
    , m_providerId(QStringLiteral("openai"))
    , m_defaultBaseUrl(QStringLiteral("https://api.openai.com/v1"))
{
}

OpenAIClient::OpenAIClient(const QString& providerId, const QUrl& defaultBaseUrl, QObject* parent)
    : AIProviderBase(parent)
    , m_providerId(providerId)
    , m_defaultBaseUrl(defaultBaseUrl)
{
}

QString OpenAIClient::providerId() const
{
    return m_providerId;
}

void OpenAIClient::sendMessage(const ChatRequest& request)
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
    if (request.temperature >= 0.0) {
        payload.insert(QStringLiteral("temperature"), request.temperature);
    }
    if (request.maxTokens > 0) {
        payload.insert(QStringLiteral("max_tokens"), request.maxTokens);
    }

    const QNetworkRequest networkRequest = makeRequest(endpointUrl(request.baseUrl, m_defaultBaseUrl), request.apiKey);
    logRequest(providerId(), networkRequest, payload, request.apiKey);
    QNetworkReply* reply = m_network.post(
        networkRequest,
        QJsonDocument(payload).toJson(QJsonDocument::Compact));
    m_repliesByRequestId.insert(request.requestId, reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, request]() {
        m_repliesByRequestId.remove(request.requestId);
        const QByteArray bytes = reply->readAll();
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
        ChatResponse response = buildResponse(providerId(), request, reply, document, bytes, parseError);
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

void OpenAIClient::streamReply(const ChatRequest& request)
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
    payload.insert(QStringLiteral("stream"), true);
    if (request.temperature >= 0.0) {
        payload.insert(QStringLiteral("temperature"), request.temperature);
    }
    if (request.maxTokens > 0) {
        payload.insert(QStringLiteral("max_tokens"), request.maxTokens);
    }

    const QNetworkRequest networkRequest = makeRequest(endpointUrl(request.baseUrl, m_defaultBaseUrl), request.apiKey);
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
            if (data == "[DONE]") {
                it->done = true;
                continue;
            }
            const QJsonDocument document = QJsonDocument::fromJson(data);
            const QJsonObject root = document.object();
            const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
            if (choices.isEmpty()) {
                continue;
            }
            const QJsonObject delta = choices.first().toObject().value(QStringLiteral("delta")).toObject();
            const QString text = contentToString(delta.value(QStringLiteral("content")));
            if (text.isEmpty()) {
                continue;
            }
            it->accumulatedText += text;
            emit streamDelta(it->request.requestId, text);
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        StreamState state = m_streams.take(reply);
        m_repliesByRequestId.remove(state.request.requestId);
        if (!state.buffer.isEmpty()) {
            const QList<QByteArray> lines = state.buffer.split('\n');
            for (const QByteArray& rawLine : lines) {
                const QByteArray line = rawLine.trimmed();
                if (!line.startsWith("data:")) {
                    continue;
                }
                const QByteArray data = line.mid(5).trimmed();
                if (data == "[DONE]") {
                    state.done = true;
                    continue;
                }
                const QJsonDocument document = QJsonDocument::fromJson(data);
                const QJsonArray choices = document.object().value(QStringLiteral("choices")).toArray();
                if (choices.isEmpty()) {
                    continue;
                }
                const QJsonObject delta = choices.first().toObject().value(QStringLiteral("delta")).toObject();
                const QString text = firstNonEmpty({
                    contentToString(delta.value(QStringLiteral("content"))),
                    contentToString(delta.value(QStringLiteral("reasoning_content"))),
                    contentToString(delta.value(QStringLiteral("reasoning")))
                });
                if (!text.isEmpty()) {
                    state.accumulatedText += text;
                    emit streamDelta(state.request.requestId, text);
                }
            }
        }
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
                response.error = QStringLiteral("%1 stream finished without text. status=%2")
                    .arg(providerId())
                    .arg(response.statusCode);
            }
            qDebug().noquote() << QStringLiteral("[AIResponse][%1] %2=%3")
                .arg(providerId(), response.ok ? QStringLiteral("text") : QStringLiteral("error"), response.ok ? response.text : response.error);
            emit streamFinished(response);
        }
        reply->deleteLater();
    });
}

void OpenAIClient::cancelRequest(const QString& requestId)
{
    QNetworkReply* reply = m_repliesByRequestId.take(requestId);
    if (reply) {
        m_streams.remove(reply);
        reply->abort();
    }
}

}
