#pragma once

namespace LogManager {
    // 初始化日志系统，拦截并重定向所有 qInfo/qWarning/qDebug 至 desktop.log 并在控制台/IDE输出
    void initialize();
    // 释放日志系统
    void shutdown();
}