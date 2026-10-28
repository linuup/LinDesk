#include <gtest/gtest.h>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include "services/AppPaths.h"
#include "services/LogManager.h"

TEST(LogManagerTests, AppPathsValidation)
{
    const QString logsDir = AppPaths::userLogsDir();
    EXPECT_FALSE(logsDir.isEmpty());
    EXPECT_TRUE(QDir(logsDir).exists());
    EXPECT_TRUE(logsDir.endsWith(QStringLiteral("logs")));

    const QString crashDir = AppPaths::userCrashDumpsDir();
    EXPECT_FALSE(crashDir.isEmpty());
    EXPECT_TRUE(QDir(crashDir).exists());
    EXPECT_TRUE(crashDir.endsWith(QStringLiteral("crash")));

    const QString widgetLogsDir = AppPaths::linWidgetLogsDir();
    EXPECT_EQ(widgetLogsDir, logsDir);
}

TEST(LogManagerTests, InitializeAndShutdown)
{
    // 初始化日志管理器
    LogManager::initialize();

    // 记录测试日志
    qInfo() << "Test informational message from LinDesk test suite";
    qWarning() << "Test warning message from LinDesk test suite";

    // 验证桌面日志文件存在
    const QString desktopLogPath = QDir(AppPaths::userLogsDir()).filePath(QStringLiteral("desktop.log"));
    const QString errorLogPath = QDir(AppPaths::userLogsDir()).filePath(QStringLiteral("desktop-error.log"));

    // 正常刷新与关闭
    LogManager::shutdown();

    EXPECT_TRUE(QFileInfo::exists(desktopLogPath));
    EXPECT_TRUE(QFileInfo::exists(errorLogPath));
}

TEST(LogManagerTests, LegacyLogMigration)
{
    const QString legacyDir = AppPaths::userLocalConfigDir();
    const QString logsDir = AppPaths::userLogsDir();

    // 创建虚拟遗留日志
    const QString dummyLegacyLog = QDir(legacyDir).filePath(QStringLiteral("test_legacy_widget_999.log"));
    QFile f(dummyLegacyLog);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("legacy widget log content");
    f.close();

    // 执行迁移
    LogManager::migrateLegacyLogs();

    // 验证遗留文件已从根目录移动到 logs 目录
    EXPECT_FALSE(QFile::exists(dummyLegacyLog));
    const QString migratedPath = QDir(logsDir).filePath(QStringLiteral("test_legacy_widget_999.log"));
    EXPECT_TRUE(QFile::exists(migratedPath));

    // 清理测试文件
    QFile::remove(migratedPath);
}

