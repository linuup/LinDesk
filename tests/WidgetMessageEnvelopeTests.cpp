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

#include "desktop/widgets/linwidget/LinWidgetTypes.h"
#include "desktop/widgets/linwidget/WidgetPackage.h"
#include "services/AppPaths.h"
#include <QDir>
#include <QFile>

TEST(WidgetManifestTests, ModernTypeWidgetAndWallpaperParsing)
{
    // 1. Modern manifest with type: "widget"
    QJsonObject widgetObj;
    widgetObj[QStringLiteral("id")] = QStringLiteral("test.modern.clock");
    widgetObj[QStringLiteral("name")] = QStringLiteral("Modern Clock");
    widgetObj[QStringLiteral("type")] = QStringLiteral("widget");
    widgetObj[QStringLiteral("entry")] = QStringLiteral("index.html");

    WidgetManifest widgetManifest;
    QString errorMsg;
    EXPECT_TRUE(manifestFromJson(widgetObj, &widgetManifest, &errorMsg));
    EXPECT_EQ(widgetManifest.role, QStringLiteral("widget"));
    EXPECT_EQ(widgetManifest.type, RuntimeType::Html);

    // 2. Modern manifest with type: "wallpaper" and wallpaperType: "video"
    QJsonObject wallpaperObj;
    wallpaperObj[QStringLiteral("id")] = QStringLiteral("test.modern.video.wallpaper");
    wallpaperObj[QStringLiteral("name")] = QStringLiteral("Video Wallpaper");
    wallpaperObj[QStringLiteral("type")] = QStringLiteral("wallpaper");
    wallpaperObj[QStringLiteral("wallpaperType")] = QStringLiteral("video");
    wallpaperObj[QStringLiteral("interactive")] = false;

    WidgetManifest wallpaperManifest;
    EXPECT_TRUE(manifestFromJson(wallpaperObj, &wallpaperManifest, &errorMsg));
    EXPECT_EQ(wallpaperManifest.role, QStringLiteral("wallpaper"));
    EXPECT_EQ(wallpaperManifest.type, RuntimeType::Html);
    EXPECT_EQ(wallpaperManifest.wallpaperType, QStringLiteral("video"));
    EXPECT_FALSE(wallpaperManifest.interactive);

    // 3. Serializing manifest preserves wallpaperType
    QJsonObject serializedWp = manifestToJson(wallpaperManifest);
    EXPECT_EQ(serializedWp.value(QStringLiteral("wallpaperType")).toString(), QStringLiteral("video"));
    EXPECT_EQ(serializedWp.value(QStringLiteral("role")).toString(), QStringLiteral("wallpaper"));
}

TEST(WidgetPackageTests, DirectoryAndPackageInstallPhysicalSeparation)
{
    // Create temporary source directory with a wallpaper manifest
    const QString tempSrcDir = QDir(AppPaths::userLocalConfigDir()).filePath(QStringLiteral("test_src_wallpaper_123"));
    QDir().mkpath(tempSrcDir);
    const QString manifestFile = QDir(tempSrcDir).filePath(QStringLiteral("manifest.json"));
    const QString entryFile = QDir(tempSrcDir).filePath(QStringLiteral("index.html"));

    QFile entryF(entryFile);
    ASSERT_TRUE(entryF.open(QIODevice::WriteOnly));
    entryF.write("<html><body>Wallpaper Test</body></html>");
    entryF.close();

    QJsonObject wpObj;
    wpObj[QStringLiteral("id")] = QStringLiteral("test.wp.separation");
    wpObj[QStringLiteral("name")] = QStringLiteral("Separation Wallpaper");
    wpObj[QStringLiteral("type")] = QStringLiteral("wallpaper");
    wpObj[QStringLiteral("wallpaperType")] = QStringLiteral("web");

    QFile manifestF(manifestFile);
    ASSERT_TRUE(manifestF.open(QIODevice::WriteOnly));
    manifestF.write(QJsonDocument(wpObj).toJson());
    manifestF.close();

    QString installedTargetDir;
    QString detectedType;
    QString errorMsg;
    bool ok = WidgetPackage::installPackage(tempSrcDir, &installedTargetDir, &detectedType, &errorMsg);
    EXPECT_TRUE(ok);
    EXPECT_EQ(detectedType, QStringLiteral("wallpaper"));
    EXPECT_TRUE(installedTargetDir.contains(QStringLiteral("LinWallpapers")));
    EXPECT_TRUE(QDir(installedTargetDir).exists());
    EXPECT_TRUE(QFile::exists(QDir(installedTargetDir).filePath(QStringLiteral("manifest.json"))));

    // Clean up
    QDir(tempSrcDir).removeRecursively();
    QDir(installedTargetDir).removeRecursively();
}
