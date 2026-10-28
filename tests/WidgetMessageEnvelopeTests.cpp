#include <gtest/gtest.h>
#include <QJsonObject>
#include <QJsonDocument>
#include <QString>
#include "desktop/widgets/linwidget/WidgetMessageTypes.h"

using namespace LinWidget;

TEST(WidgetMessageEnvelopeTests, ErrorCodeConstants)
{
    EXPECT_STREQ(WidgetErrorCode::Ok, "ok");
    EXPECT_STREQ(WidgetErrorCode::Timeout, "timeout");
    EXPECT_STREQ(WidgetErrorCode::NotFound, "not_found");
    EXPECT_STREQ(WidgetErrorCode::PermissionDenied, "permission_denied");
    EXPECT_STREQ(WidgetErrorCode::Internal, "internal");
    EXPECT_STREQ(WidgetErrorCode::NotImplemented, "not_implemented");
}

TEST(WidgetMessageEnvelopeTests, EnvelopeToJsonAndFromJson)
{
    WidgetEnvelope env;
    env.id = QStringLiteral("req_123");
    env.type = QStringLiteral("storage.get");
    env.widgetId = QStringLiteral("demo.widget");
    env.payload[QStringLiteral("key")] = QStringLiteral("theme");

    QJsonObject json = env.toJson();
    EXPECT_TRUE(json.value(QStringLiteral("id")).toString() == QStringLiteral("req_123"));
    EXPECT_TRUE(json.value(QStringLiteral("type")).toString() == QStringLiteral("storage.get"));
    EXPECT_TRUE(json.value(QStringLiteral("widgetId")).toString() == QStringLiteral("demo.widget"));
    EXPECT_TRUE(json.value(QStringLiteral("payload")).isObject());
    EXPECT_FALSE(json.contains(QStringLiteral("error")));

    WidgetEnvelope parsed = WidgetEnvelope::fromJson(json);
    EXPECT_TRUE(parsed.id == QStringLiteral("req_123"));
    EXPECT_TRUE(parsed.type == QStringLiteral("storage.get"));
    EXPECT_TRUE(parsed.widgetId == QStringLiteral("demo.widget"));
    EXPECT_TRUE(parsed.payload.value(QStringLiteral("key")).toString() == QStringLiteral("theme"));
    EXPECT_TRUE(parsed.error.isEmpty());
}

TEST(WidgetMessageEnvelopeTests, EnvelopeWithError)
{
    WidgetEnvelope env;
    env.id = QStringLiteral("req_456");
    env.type = QStringLiteral("fs.read");
    env.widgetId = QStringLiteral("notes.widget");
    
    QJsonObject err;
    err[QStringLiteral("code")] = QString::fromUtf8(WidgetErrorCode::PermissionDenied);
    err[QStringLiteral("message")] = QStringLiteral("Read access not permitted");
    env.error = err;

    QJsonObject json = env.toJson();
    EXPECT_TRUE(json.contains(QStringLiteral("error")));
    QJsonObject errObj = json.value(QStringLiteral("error")).toObject();
    EXPECT_TRUE(errObj.value(QStringLiteral("code")).toString() == QString::fromUtf8(WidgetErrorCode::PermissionDenied));
    EXPECT_TRUE(errObj.value(QStringLiteral("message")).toString() == QStringLiteral("Read access not permitted"));

    WidgetEnvelope parsed = WidgetEnvelope::fromJson(json);
    EXPECT_FALSE(parsed.error.isEmpty());
    EXPECT_TRUE(parsed.error.value(QStringLiteral("code")).toString() == QString::fromUtf8(WidgetErrorCode::PermissionDenied));
    EXPECT_TRUE(parsed.error.value(QStringLiteral("message")).toString() == QStringLiteral("Read access not permitted"));
}

TEST(WidgetMessageEnvelopeTests, MakeWidgetEnvelopeMessage)
{
    QJsonObject payload;
    payload[QStringLiteral("count")] = 42;

    QJsonObject successMsg = makeWidgetEnvelopeMessage(
        QStringLiteral("counter.update"),
        QStringLiteral("widget_a"),
        QStringLiteral("msg_1"),
        payload
    );

    EXPECT_TRUE(successMsg.value(QStringLiteral("id")).toString() == QStringLiteral("msg_1"));
    EXPECT_TRUE(successMsg.value(QStringLiteral("type")).toString() == QStringLiteral("counter.update"));
    EXPECT_TRUE(successMsg.value(QStringLiteral("widgetId")).toString() == QStringLiteral("widget_a"));
    EXPECT_FALSE(successMsg.contains(QStringLiteral("error")));

    QJsonObject errorMsg = makeWidgetEnvelopeMessage(
        QStringLiteral("counter.update"),
        QStringLiteral("widget_a"),
        QStringLiteral("msg_2"),
        payload,
        QString::fromUtf8(WidgetErrorCode::Timeout),
        QStringLiteral("Request timed out")
    );

    EXPECT_TRUE(errorMsg.contains(QStringLiteral("error")));
    EXPECT_TRUE(errorMsg.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString() == QString::fromUtf8(WidgetErrorCode::Timeout));
}
