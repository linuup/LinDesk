#include "services/LogManager.h"
#include "services/AppPaths.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/msvc_sink.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
std::shared_ptr<spdlog::logger> g_logger;
std::atomic<bool> g_debugEnabled{false};
std::mutex g_initMutex;

void customMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    if (!g_logger) {
        return;
    }

    spdlog::level::level_enum lvl = spdlog::level::info;
    switch (type) {
    case QtDebugMsg:
        if (!g_debugEnabled.load()) {
            return;
        }
        lvl = spdlog::level::debug;
        break;
    case QtInfoMsg:
        lvl = spdlog::level::info;
        break;
    case QtWarningMsg:
        lvl = spdlog::level::warn;
        break;
    case QtCriticalMsg:
        lvl = spdlog::level::err;
        break;
    case QtFatalMsg:
        lvl = spdlog::level::critical;
        break;
    }

    spdlog::source_loc loc{
        context.file ? context.file : "",
        context.line,
        context.function ? context.function : ""
    };

    const std::string utf8Msg = msg.toStdString();
    g_logger->log(loc, lvl, utf8Msg);

    if (type == QtFatalMsg) {
        g_logger->flush();
        spdlog::shutdown();
        abort();
    }
}
} // namespace

namespace LogManager {

void initialize()
{
    std::lock_guard<std::mutex> lock(g_initMutex);
    if (g_logger) {
        return;
    }

    // 1. 检测是否开启 debug 级别
#ifdef QT_DEBUG
    g_debugEnabled.store(true);
#else
    bool debugFlag = false;
#ifdef Q_OS_WIN
    int numArgs = 0;
    LPWSTR* argList = CommandLineToArgvW(GetCommandLineW(), &numArgs);
    if (argList) {
        for (int i = 0; i < numArgs; ++i) {
            if (_wcsicmp(argList[i], L"-debug") == 0 || _wcsicmp(argList[i], L"--debug") == 0) {
                debugFlag = true;
                break;
            }
        }
        LocalFree(argList);
    }
#endif
    g_debugEnabled.store(debugFlag);
#endif

    // 2. 准备日志目录并扫描迁移遗留历史日志
    const QString logsDir = AppPaths::userLogsDir();
    QDir().mkpath(logsDir);
    migrateLegacyLogs();

    const QString desktopLogPath = QDir(logsDir).filePath(QStringLiteral("desktop.log"));
    const QString errorLogPath = QDir(logsDir).filePath(QStringLiteral("desktop-error.log"));

    std::vector<spdlog::sink_ptr> sinks;

    try {
        // 全量主日志：单文件 20MB，保留 7 份
        auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            QDir::toNativeSeparators(desktopLogPath).toStdString(),
            20 * 1024 * 1024,
            7
        );
        sinks.push_back(fileSink);

        // 错误日志双写：仅 WARN / CRIT / FATAL
        auto errorSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            QDir::toNativeSeparators(errorLogPath).toStdString(),
            20 * 1024 * 1024,
            7
        );
        errorSink->set_level(spdlog::level::warn);
        sinks.push_back(errorSink);

        // 控制台带颜色输出
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        sinks.push_back(consoleSink);

#ifdef Q_OS_WIN
        // Windows 调试器输出
        auto msvcSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
        sinks.push_back(msvcSink);
#endif

        g_logger = std::make_shared<spdlog::logger>("desktop", sinks.begin(), sinks.end());
        g_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [desktop] [%P] [%t] [%s:%#] %v");

        if (g_debugEnabled.load()) {
            g_logger->set_level(spdlog::level::debug);
        } else {
            g_logger->set_level(spdlog::level::info);
        }

        g_logger->flush_on(spdlog::level::warn);
        spdlog::flush_every(std::chrono::seconds(2));
        spdlog::set_default_logger(g_logger);

        qInstallMessageHandler(customMessageHandler);
    } catch (const std::exception& e) {
        // 兜底降级处理
        qInstallMessageHandler(nullptr);
    }
}

void shutdown()
{
    std::lock_guard<std::mutex> lock(g_initMutex);
    qInstallMessageHandler(nullptr);
    if (g_logger) {
        g_logger->flush();
    }
    spdlog::shutdown();
    g_logger.reset();
}

QString logsDirectory()
{
    return AppPaths::userLogsDir();
}

QString crashDumpsDirectory()
{
    return AppPaths::userCrashDumpsDir();
}

bool isDebugEnabled()
{
    return g_debugEnabled.load();
}

void migrateLegacyLogs()
{
    const QString legacyDir = AppPaths::userLocalConfigDir();
    const QString targetLogsDir = AppPaths::userLogsDir();
    if (legacyDir.isEmpty() || targetLogsDir.isEmpty() || legacyDir == targetLogsDir) {
        return;
    }

    QDir source(legacyDir);
    const QStringList legacyLogs = source.entryList(QStringList() << QStringLiteral("*.log"), QDir::Files);
    for (const QString& fileName : legacyLogs) {
        const QString oldPath = source.filePath(fileName);
        QString newFileName = fileName;
        if (fileName.compare(QStringLiteral("desktop.log"), Qt::CaseInsensitive) == 0) {
            newFileName = QStringLiteral("desktop-legacy.log");
        }
        const QString targetPath = QDir(targetLogsDir).filePath(newFileName);
        if (!QFile::exists(targetPath)) {
            QFile::rename(oldPath, targetPath);
        } else if (fileName.compare(QStringLiteral("desktop.log"), Qt::CaseInsensitive) == 0) {
            const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
            const QString stampedPath = QDir(targetLogsDir).filePath(QStringLiteral("desktop-legacy-%1.log").arg(ts));
            QFile::rename(oldPath, stampedPath);
        } else {
            QFile::remove(targetPath);
            QFile::rename(oldPath, targetPath);
        }
    }
}

} // namespace LogManager