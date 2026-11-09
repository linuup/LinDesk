#pragma once

namespace CrashHandler {
    // 注册全局异常过滤器，启动本地崩溃转储 (Minidump) 监控
    void initialize();

    // 注册系统托盘底层消息窗口句柄，以便在异常退出时立即清理系统托盘图标
    void registerTrayWindow(void* hwnd);
}
