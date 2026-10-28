#include <gtest/gtest.h>
#include <QDateTime>
#include <QDir>
#include <QFile>
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

    const QString token = QStringLiteral("token_%1").arg(QDateTime::currentMSecsSinceEpoch());
    const QString desktopInfoMsg = QStringLiteral("Test desktop info %1").arg(token);
    const QString desktopWarnMsg = QStringLiteral("Test desktop warn %1").arg(token);
    const QString widgetInfoMsg = QStringLiteral("[LinWidget][CefManager] Test widget info %1").arg(token);
    const QString widgetWarnMsg = QStringLiteral("[LinWidget][WidgetManager] Test widget warn %1").arg(token);
    const QString widgetCritMsg = QStringLiteral("[LinWidget][CefOsrBrowserInstance] Test widget error %1").arg(token);

    // 记录测试日志
    qInfo().noquote() << desktopInfoMsg;
    qWarning().noquote() << desktopWarnMsg;
    qInfo().noquote() << widgetInfoMsg;
    qWarning().noquote() << widgetWarnMsg;
    qCritical().noquote() << widgetCritMsg;

    // 验证桌面与组件日志文件存在
    const QString desktopLogPath = QDir(AppPaths::userLogsDir()).filePath(QStringLiteral("desktop.log"));
    const QString errorLogPath = QDir(AppPaths::userLogsDir()).filePath(QStringLiteral("desktop-error.log"));
    const QString linwidgetLogPath = QDir(AppPaths::userLogsDir()).filePath(QStringLiteral("linwidget.log"));

    // 正常刷新与关闭
    LogManager::shutdown();

    EXPECT_TRUE(QFileInfo::exists(desktopLogPath));
    EXPECT_TRUE(QFileInfo::exists(errorLogPath));
    EXPECT_TRUE(QFileInfo::exists(linwidgetLogPath));

    auto readFileUtf8 = [](const QString& path) -> QString {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return QString();
        }
        return QString::fromUtf8(file.readAll());
    };

    const QString desktopLogContent = readFileUtf8(desktopLogPath);
    const QString errorLogContent = readFileUtf8(errorLogPath);
    const QString linwidgetLogContent = readFileUtf8(linwidgetLogPath);

    // 1. desktop.log 包含主程序日志（且行内带 [desktop] 标记），不含本次写入的 [LinWidget] 日志
    EXPECT_TRUE(desktopLogContent.contains(desktopInfoMsg));
    EXPECT_TRUE(desktopLogContent.contains(desktopWarnMsg));
    EXPECT_FALSE(desktopLogContent.contains(widgetInfoMsg));
    EXPECT_FALSE(desktopLogContent.contains(widgetWarnMsg));
    EXPECT_FALSE(desktopLogContent.contains(widgetCritMsg));

    for (const QString& line : desktopLogContent.split(QLatin1Char('\n'))) {
        if (line.contains(desktopInfoMsg)) {
            EXPECT_TRUE(line.contains(QStringLiteral("[desktop]")));
            EXPECT_TRUE(line.contains(QStringLiteral("[info]")));
        }
        if (line.contains(desktopWarnMsg)) {
            EXPECT_TRUE(line.contains(QStringLiteral("[desktop]")));
            EXPECT_TRUE(line.contains(QStringLiteral("[warning]")));
        }
    }

    // 2. desktop-error.log 包含主程序 warn+，不含组件 warn/err
    EXPECT_TRUE(errorLogContent.contains(desktopWarnMsg));
    EXPECT_FALSE(errorLogContent.contains(widgetWarnMsg));
    EXPECT_FALSE(errorLogContent.contains(widgetCritMsg));

    // 3. linwidget.log 包含组件 info/warn/err 且对应行带有 [linwidget] 及级别标记，不含主程序日志
    EXPECT_TRUE(linwidgetLogContent.contains(widgetInfoMsg));
    EXPECT_TRUE(linwidgetLogContent.contains(widgetWarnMsg));
    EXPECT_TRUE(linwidgetLogContent.contains(widgetCritMsg));
    EXPECT_FALSE(linwidgetLogContent.contains(desktopInfoMsg));
    EXPECT_FALSE(linwidgetLogContent.contains(desktopWarnMsg));

    for (const QString& line : linwidgetLogContent.split(QLatin1Char('\n'))) {
        if (line.contains(widgetInfoMsg)) {
            EXPECT_TRUE(line.contains(QStringLiteral("[linwidget]")));
            EXPECT_TRUE(line.contains(QStringLiteral("[info]")));
        }
        if (line.contains(widgetWarnMsg)) {
            EXPECT_TRUE(line.contains(QStringLiteral("[linwidget]")));
            EXPECT_TRUE(line.contains(QStringLiteral("[warning]")));
        }
        if (line.contains(widgetCritMsg)) {
            EXPECT_TRUE(line.contains(QStringLiteral("[linwidget]")));
            EXPECT_TRUE(line.contains(QStringLiteral("[error]")));
        }
    }
}

#ifdef Q_OS_WIN
#include <windows.h>

TEST(LogManagerTests, FallbackToDesktopLoggerWhenLinwidgetLogLocked)
{
    LogManager::shutdown();

    const QString logsDir = AppPaths::userLogsDir();
    QDir().mkpath(logsDir);
    const QString desktopLogPath = QDir(logsDir).filePath(QStringLiteral("desktop.log"));
    const QString linwidgetLogPath = QDir(logsDir).filePath(QStringLiteral("linwidget.log"));

    // 独占锁住 linwidget.log，模拟 rotating_file_sink_mt 打开失败
    const std::wstring linwidgetPathW = QDir::toNativeSeparators(linwidgetLogPath).toStdWString();
    HANDLE hLock = CreateFileW(
        linwidgetPathW.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0, // 禁止共享读写，使 spdlog rotating_file_sink_mt 抛出异常
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    ASSERT_NE(hLock, INVALID_HANDLE_VALUE);

    LogManager::initialize();

    const QString fallbackToken = QStringLiteral("fallback_%1").arg(QDateTime::currentMSecsSinceEpoch());
    const QString fallbackWidgetMsg = QStringLiteral("[LinWidget][WidgetManager] Fallback widget log %1").arg(fallbackToken);
    qInfo().noquote() << fallbackWidgetMsg;

    LogManager::shutdown();
    CloseHandle(hLock);

    QFile desktopFile(desktopLogPath);
    ASSERT_TRUE(desktopFile.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString desktopLogContent = QString::fromUtf8(desktopFile.readAll());
    EXPECT_TRUE(desktopLogContent.contains(fallbackWidgetMsg));
}
#endif

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


