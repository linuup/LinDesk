#include "services/LogManager.h"
#include "services/AppPaths.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QTextStream>
#include <QThread>
#include <iostream>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
QMutex g_logMutex;
QFile* g_logFile = nullptr;
bool g_debugEnabled = false;

void customMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    QMutexLocker locker(&g_logMutex);
    
    // 如果不是 debug 模式，忽略常规调试输出（仅保留 INFO, WARNING, CRITICAL, FATAL）
    if (type == QtDebugMsg && !g_debugEnabled) {
        return;
    }

    QString levelStr;
    switch (type) {
    case QtDebugMsg:
        levelStr = "DEBUG";
        break;
    case QtInfoMsg:
        levelStr = "INFO";
        break;
    case QtWarningMsg:
        levelStr = "WARN";
        break;
    case QtCriticalMsg:
        levelStr = "CRIT";
        break;
    case QtFatalMsg:
        levelStr = "FATAL";
        break;
    }

    QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    QString threadIdStr = QString("0x%1").arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16);
    
    QString fileStr = context.file ? QFileInfo(context.file).fileName() : "unknown";
    int line = context.line;

    QString formattedMessage = QString("[%1] [%2] [Thread %3] [%4:%5] - %6\n")
        .arg(timeStr)
        .arg(levelStr)
        .arg(threadIdStr)
        .arg(fileStr)
        .arg(line)
        .arg(msg);

    // 1. 写入本地日志文件
    if (g_logFile && g_logFile->isOpen()) {
        QTextStream stream(g_logFile);
        stream.setCodec("UTF-8");
        stream << formattedMessage;
        stream.flush();
    }

    // 2. 输出至控制台与 Windows 调试器
#ifdef Q_OS_WIN
    OutputDebugStringW(reinterpret_cast<const wchar_t*>(formattedMessage.utf16()));
#endif
    if (type == QtCriticalMsg || type == QtFatalMsg) {
        std::cerr << formattedMessage.toStdString();
    } else {
        std::cout << formattedMessage.toStdString();
    }
}
} // namespace

namespace LogManager {
void initialize()
{
    QMutexLocker locker(&g_logMutex);
    if (g_logFile) {
        return;
    }

    // 检测当前是否是调试运行状态 (Debug build 或者命令行带 -debug)
#ifdef QT_DEBUG
    g_debugEnabled = true;
#else
    QStringList args = QCoreApplication::arguments();
    g_debugEnabled = args.contains("-debug", Qt::CaseInsensitive);
#endif

    // 获取并创建 Local AppData 日志文件夹路径
    QString logDir = AppPaths::userLocalConfigDir();
    QDir().mkpath(logDir);
    QString logPath = logDir + "/desktop.log";

    g_logFile = new QFile(logPath);
    
    // 如果单个日志文件大于 10MB，重置/清理
    if (g_logFile->size() > 10 * 1024 * 1024) {
        g_logFile->remove();
    }

    if (g_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qInstallMessageHandler(customMessageHandler);
    }
}

void shutdown()
{
    QMutexLocker locker(&g_logMutex);
    qInstallMessageHandler(nullptr);
    if (g_logFile) {
        g_logFile->close();
        delete g_logFile;
        g_logFile = nullptr;
    }
}
}