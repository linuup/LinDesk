#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QStandardPaths>

#include "services/DesktopItemLoader.h"

int ListItemData::s_targetIconSize = 96;

class DisplayNameOverrideTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        if (!qApp) {
            static int argc = 1;
            static char appName[] = "DisplayNameOverrideTest";
            static char* argv[] = { appName, nullptr };
            new QCoreApplication(argc, argv);
        }
    }
};

TEST_F(DisplayNameOverrideTest, DisplayNameOverridesListContainsWpsDrive)
{
    const auto overrides = DesktopItemLoader::displayNameOverrides();
    EXPECT_FALSE(overrides.isEmpty());

    bool foundWps = false;
    for (const auto& o : overrides) {
        if (o.normalizedPath.contains("wpsdrive\\notloginpage") ||
            o.normalizedPath.contains("wpsdrive/notloginpage")) {
            foundWps = true;
            EXPECT_EQ(o.zhName, QStringLiteral("WPS云盘"));
            EXPECT_EQ(o.enName, QStringLiteral("WPS Drive"));
        }
    }
    EXPECT_TRUE(foundWps);
}

TEST_F(DisplayNameOverrideTest, NonMatchingPathReturnsOriginalName)
{
    const QString originalName = QStringLiteral("MyNormalFolder");
    const QString nonMatchingPath = QStringLiteral("D:/SomeOtherFolder/NotLoginPage");
    EXPECT_EQ(DesktopItemLoader::applyDisplayNameOverride(nonMatchingPath, originalName), originalName);

    const QString emptyPath;
    EXPECT_EQ(DesktopItemLoader::applyDisplayNameOverride(emptyPath, originalName), originalName);
}

TEST_F(DisplayNameOverrideTest, MatchingPathReturnsOverride)
{
    const QString originalName = QStringLiteral("NotLoginPage");
    const QString standardPath = QStringLiteral("D:/Documents/WPSDrive/NotLoginPage");
    const QString result = DesktopItemLoader::applyDisplayNameOverride(standardPath, originalName);

    const bool isZh = QLocale::system().name().startsWith(QStringLiteral("zh"), Qt::CaseInsensitive);
    if (isZh) {
        EXPECT_EQ(result, QStringLiteral("WPS云盘"));
    } else {
        EXPECT_EQ(result, QStringLiteral("WPS Drive"));
    }
}

TEST_F(DisplayNameOverrideTest, CaseInsensitiveAndSeparatorNormalization)
{
    const QString originalName = QStringLiteral("NotLoginPage");

    // Test backslash vs slash, lowercase, uppercase, trailing slash
    const QString testPath1 = QStringLiteral("d:\\documents\\wpsdrive\\notloginpage");
    const QString testPath2 = QStringLiteral("D:/DOCUMENTS/WPSDRIVE/NOTLOGINPAGE/");
    const QString testPath3 = QStringLiteral("D:\\Documents\\WPSDrive\\NotLoginPage\\");

    const QString res1 = DesktopItemLoader::applyDisplayNameOverride(testPath1, originalName);
    const QString res2 = DesktopItemLoader::applyDisplayNameOverride(testPath2, originalName);
    const QString res3 = DesktopItemLoader::applyDisplayNameOverride(testPath3, originalName);

    EXPECT_NE(res1, originalName);
    EXPECT_EQ(res1, res2);
    EXPECT_EQ(res2, res3);
}
