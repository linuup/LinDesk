#pragma once
#include <windows.h>
#include <QString>
#include <QObject>
#include <thread>
#include <atomic>
#include "monitor/IDirectoryMonitorInterface.h"

// 缓冲区大小
const int LIN_BUFFER_SIZE = 64 * 1024;

class LinDirectoryMonitor : public IDirectoryMonitorListener
{
    Q_OBJECT
public:
    LinDirectoryMonitor(const QString& path, bool recursive, QObject* parent = nullptr);
    virtual ~LinDirectoryMonitor();
    
    // ========== IListenerInterface 接口实现 ==========
    bool startListening() override;
    void stopListening() override;

    // ========== 事件转换工具 ==========
    static FileSystemEvent fromWinEvent(DWORD action);
    QString eventToString(FileSystemEvent event);

signals:
    void fileSystemChanged(const FileSystemEventInfo& eventInfo);
    void errorOccurred(const QString& errorMsg);

private:
    QString m_path;
    bool m_recursiveMonitor = false;
    HANDLE m_hDirectory = INVALID_HANDLE_VALUE;
    HANDLE m_hStopEvent = NULL;
    
    std::unique_ptr<std::thread> m_thread;
    std::atomic<bool> m_running;
    
    // 缓冲区
    alignas(alignof(DWORD)) char m_buffer[LIN_BUFFER_SIZE];
    
    // 暂存重命名的旧路径
    QString m_lastOldPath;
    bool m_lastOldPathIsDir = false;
    
    // 监控线程
    void monitorThread();
    
    // 处理通知
    void processNotification(FILE_NOTIFY_INFORMATION* info);
};
