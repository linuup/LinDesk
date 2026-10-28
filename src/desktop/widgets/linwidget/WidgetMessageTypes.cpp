#include "desktop/widgets/linwidget/WidgetMessageTypes.h"

namespace LinWidget {

QJsonObject makeWidgetMessage(const QString& type, const QString& widgetId)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), type);
    object.insert(QStringLiteral("widgetId"), widgetId);
    return object;
}

QJsonObject WidgetEnvelope::toJson() const
{
    QJsonObject obj;
    if (!id.isEmpty()) {
        obj.insert(QStringLiteral("id"), id);
        obj.insert(QStringLiteral("requestId"), id);
    }
    obj.insert(QStringLiteral("type"), type);
    obj.insert(QStringLiteral("widgetId"), widgetId);
    obj.insert(QStringLiteral("instanceId"), widgetId);
    if (!payload.isEmpty()) {
        obj.insert(QStringLiteral("payload"), payload);
    }
    if (!error.isEmpty()) {
        obj.insert(QStringLiteral("error"), error);
        obj.insert(QStringLiteral("ok"), false);
    } else {
        obj.insert(QStringLiteral("ok"), true);
    }
    return obj;
}

WidgetEnvelope WidgetEnvelope::fromJson(const QJsonObject& obj)
{
    WidgetEnvelope env;
    env.id = obj.value(QStringLiteral("id")).toString(obj.value(QStringLiteral("requestId")).toString());
    env.type = obj.value(QStringLiteral("type")).toString();
    env.widgetId = obj.value(QStringLiteral("widgetId")).toString(obj.value(QStringLiteral("instanceId")).toString());
    if (obj.contains(QStringLiteral("payload")) && obj.value(QStringLiteral("payload")).isObject()) {
        env.payload = obj.value(QStringLiteral("payload")).toObject();
    } else {
        env.payload = obj;
    }
    if (obj.value(QStringLiteral("error")).isObject()) {
        env.error = obj.value(QStringLiteral("error")).toObject();
    } else if (obj.value(QStringLiteral("error")).isString()) {
        QJsonObject errObj;
        errObj.insert(QStringLiteral("code"), QStringLiteral("error"));
        errObj.insert(QStringLiteral("message"), obj.value(QStringLiteral("error")).toString());
        env.error = errObj;
    }
    return env;
}

QJsonObject makeWidgetEnvelopeMessage(const QString& type,
                                      const QString& widgetId,
                                      const QString& requestId,
                                      const QJsonObject& payload,
                                      const QString& errorCode,
                                      const QString& errorMessage)
{
    WidgetEnvelope env;
    env.id = requestId;
    env.type = type;
    env.widgetId = widgetId;
    env.payload = payload;
    if (!errorCode.isEmpty() || !errorMessage.isEmpty()) {
        QJsonObject err;
        err.insert(QStringLiteral("code"), errorCode.isEmpty() ? QString::fromLatin1(WidgetErrorCode::Internal) : errorCode);
        err.insert(QStringLiteral("message"), errorMessage);
        env.error = err;
    }
    QJsonObject result = env.toJson();
    for (auto it = payload.constBegin(); it != payload.constEnd(); ++it) {
        if (!result.contains(it.key())) {
            result.insert(it.key(), it.value());
        }
    }
    return result;
}

}
