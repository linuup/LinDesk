#include "ai/client/GoogleClient.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

namespace AI {
namespace {

QUrl endpointUrl(const QUrl& baseUrl, const QString& model, bool stream)
{
    QUrl url = baseUrl.isValid() && !baseUrl.isEmpty()
        ? baseUrl
        : QUrl(QStringLiteral("https://generativelanguage.googleapis.com/v1beta"));

    QString path = url.path();
    path.replace(QStringLiteral("/openai"), QString());
    const QString action = stream ? QStringLiteral("streamGenerateContent") : QStringLiteral("generateContent");
    if (!path.contains(QStringLiteral("/models/"))) {
        if (!path.endsWith(QLatin1Char('/'))) {
            path += QLatin1Char('/');
        }
        path += QStringLiteral("models/%1:%2").arg(model, action);
    }
    url.setPath(path);
    if (stream) {
        QUrlQuery query(url);
        query.addQueryItem(QStringLiteral("alt"), QStringLiteral("sse"));
        url.setQuery(query);
    }
    return url;
}

QJsonObject textPart(const QString& text)
{
    QJsonObject part;
    part.insert(QStringLiteral("text"), text);
    return part;
}

QJsonArray buildContents(const ChatRequest& request)
{
    QJsonArray contents;
    for (const ChatMessage& message : request.messages) {
        if (message.content.trimmed().isEmpty()) {
            continue;
        }
        QJsonObject content;
        QJsonArray parts;
        parts.append(textPart(message.content));
        content.insert(QStringLiteral("parts"), parts);
        contents.append(content);
    }
    return contents;
}

QString partsToString(const QJsonArray& parts)
{
    QStringList textParts;
    for (const QJsonValue& value : parts) {
        const QString text = value.toObject().value(QStringLiteral("text")).toString();
        if (!text.isEmpty()) {
            textParts.append(text);
        }
    }
    return textParts.join(QString());
}

QString responseText(const QJsonObject& root)
{
    QStringList chunks;
    const QJsonArray candidates = root.value(QStringLiteral("candidates")).toArray();
    for (const QJsonValue& value : candidates) {
        const QJsonObject content = value.toObject().value(QStringLiteral("content")).toObject();
        const QString text = partsToString(content.value(QStringLiteral("parts")).toArray());
        if (!text.isEmpty()) {
            chunks.append(text);
        }
    }
    return chunks.join(QString());
}

QString responseError(const QJsonObject& root)
{
    const QJsonValue errorValue = root.value(QStringLiteral("error"));
    if (errorValue.isObject()) {
        return errorValue.toObject().value(QStringLiteral("message")).toString();
    }
    if (errorValue.isString()) {
        return errorValue.toString();
    }
    return QString();
}

QString maskedApiKey(const QString& apiKey)
{
    if (apiKey.size() <= 8) {
        return QStringLiteral("***");
    }
    return apiKey.left(4) + QStringLiteral("...") + apiKey.right(4);
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

QJsonObject buildPayload(const ChatRequest& request)
{
    QJsonObject payload = request.options;
    payload.insert(QStringLiteral("contents"), buildContents(request));

    if (!request.systemPrompt.trimmed().isEmpty()) {
        QJsonObject systemInstruction;
        QJsonArray parts;
        parts.append(textPart(request.systemPrompt));
        systemInstruction.insert(QStringLiteral("parts"), parts);
        payload.insert(QStringLiteral("system_instruction"), systemInstruction);
    }

    if (request.temperature >= 0.0 || request.maxTokens > 0) {
        QJsonObject generationConfig = payload.value(QStringLiteral("generationConfig")).toObject();
        if (request.temperature >= 0.0) {
            generationConfig.insert(QStringLiteral("temperature"), request.temperature);
        }
        if (request.maxTokens > 0) {
            generationConfig.insert(QStringLiteral("maxOutputTokens"), request.maxTokens);
        }
        payload.insert(QStringLiteral("generationConfig"), generationConfig);
    }
    return payload;
}

QNetworkRequest makeRequest(const QUrl& url, const QString& apiKey)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "text/event-stream");
    request.setRawHeader("Cache-Control", "no-cache");
    request.setRawHeader("x-goog-api-key", apiKey.toUtf8());
    return request;
}

void logRequest(const QString& provider, const QNetworkRequest& request, const QJsonObject& payload, const QString& apiKey)
{
    QJsonObject headers;
    headers.insert(QStringLiteral("Content-Type"), QStringLiteral("application/json"));
    headers.insert(QStringLiteral("x-goog-api-key"), maskedApiKey(apiKey));

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
        response.text = responseText(response.rawObject);
        response.error = responseError(response.rawObject);
    } else if (document.isArray()) {
        response.rawArray = document.array();
        QStringList chunks;
        for (const QJsonValue& value : response.rawArray) {
            const QString text = responseText(value.toObject());
            if (!text.isEmpty()) {
                chunks.append(text);
            }
        }
        response.text = chunks.join(QString());
    }

    response.ok = !response.text.isEmpty() && response.error.isEmpty();
    if (!response.ok && response.error.isEmpty()) {
        if (parseError.error != QJsonParseError::NoError) {
            response.error = QStringLiteral("Google response is not valid JSON. status=%1 parse=%2 raw=%3")
                .arg(response.statusCode)
                .arg(parseError.errorString(), QString::fromUtf8(rawBytes.left(1200)));
        } else {
            response.error = QStringLiteral("Google response did not contain text. status=%1 raw=%2")
                .arg(response.statusCode)
                .arg(QString::fromUtf8(document.toJson(QJsonDocument::Compact).left(1200)));
        }
    }
    return response;
}

}

GoogleClient::GoogleClient(QObject* parent)
    : AIProviderBase(parent)
{
}

QString GoogleClient::providerId() const
{
    return QStringLiteral("google");
}

void GoogleClient::sendMessage(const ChatRequest& request)
{
    if (request.apiKey.trimmed().isEmpty()) {
        emit requestFailed(invalidRequest(providerId(), request, QStringLiteral("Missing API key.")));
        return;
    }
    if (request.model.trimmed().isEmpty()) {
        emit requestFailed(invalidRequest(providerId(), request, QStringLiteral("Missing model.")));
        return;
    }

    const QJsonObject payload = buildPayload(request);
    const QNetworkRequest networkRequest = makeRequest(endpointUrl(request.baseUrl, request.model, false), request.apiKey);
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

void GoogleClient::streamReply(const ChatRequest& request)
{
    if (request.apiKey.trimmed().isEmpty()) {
        emit requestFailed(invalidRequest(providerId(), request, QStringLiteral("Missing API key.")));
        return;
    }
    if (request.model.trimmed().isEmpty()) {
        emit requestFailed(invalidRequest(providerId(), request, QStringLiteral("Missing model.")));
        return;
    }

    const QJsonObject payload = buildPayload(request);
    const QNetworkRequest networkRequest = makeRequest(endpointUrl(request.baseUrl, request.model, true), request.apiKey);
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
            const QString text = responseText(document.object());
            if (text.isEmpty()) {
                continue;
            }
            it->accumulatedText += text;
            qDebug().noquote() << QStringLiteral("[AIStream][%1] delta=%2")
                .arg(providerId(), text);
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
                const QString text = responseText(document.object());
                if (!text.isEmpty()) {
                    state.accumulatedText += text;
                    qDebug().noquote() << QStringLiteral("[AIStream][%1] late-delta=%2")
                        .arg(providerId(), text);
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
        }
        if (response.ok) {
            qDebug().noquote() << QStringLiteral("[AIResponse][%1] text=%2")
                .arg(providerId(), response.text);
            emit streamFinished(response);
        } else {
            if (response.error.isEmpty()) {
                response.error = QStringLiteral("Google stream finished without text. status=%1 raw=%2")
                    .arg(response.statusCode)
                    .arg(QString::fromUtf8(state.buffer.left(1200)));
            }
            qDebug().noquote() << QStringLiteral("[AIResponse][%1] error=%2")
                .arg(providerId(), response.error);
            emit requestFailed(response);
        }
        reply->deleteLater();
    });
}

void GoogleClient::cancelRequest(const QString& requestId)
{
    QNetworkReply* reply = m_repliesByRequestId.take(requestId);
    if (reply) {
        m_streams.remove(reply);
        reply->abort();
    }
}

}
