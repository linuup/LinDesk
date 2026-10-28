#pragma once

#include <QString>

namespace LogManager {
    // 初始化日志系统，拦截并重定向所有 qInfo/qWarning/qDebug 至 desktop.log 并在控制台/IDE输出
    void initialize();
    // 释放日志系统并刷新所有缓冲区
    void shutdown();
    // 获取日志目录路径 (%LOCALAPPDATA%/LinDesk/logs)
    QString logsDirectory();
    // 获取崩溃转储目录路径 (%LOCALAPPDATA%/LinDesk/logs/crash)
    QString crashDumpsDirectory();
    // 是否启用了调试日志输出
    bool isDebugEnabled();
    // 扫描并迁移历史根目录中的遗留日志文件至统一 logs 目录
    void migrateLegacyLogs();
}