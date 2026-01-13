#pragma once
#include <windows.h>
#include <QString>
#include "ShObjIdl_core.h"
#include "IListenerInterface.h"

class IDirectoryMonitorListener: public IListenerInterface
{
    Q_OBJECT
public:
    IDirectoryMonitorListener(const QString& name = "DirectoryMonitor", QObject* parent = nullptr)
        : IListenerInterface(name, "DirectoryMonitor", parent) {}
    // ========== 必须实现的纯虚函数 ==========

    // 监听控制
    virtual bool startListening() override = 0;
    virtual void stopListening() override = 0;
//    IDirectoryMonitorInterface(QObject* parent = nullptr);s
//    ~IDirectoryMonitorInterface();
    virtual void startMonitoring() = 0;
//    void stopMonitoring();
//    void run();
//    // ========== IListenerInterface 接口实现 ==========
//    bool startListening() override;
//    void stopListening() override;
//
//    // ========== IRegistryListener 接口实现 ==========
//private:
//    HWND m_hwnd = nullptr;
//    ULONG m_regId = 0;
//
//
//    // Windows message window procedure
//    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
//
//    // Shell event handler
//    void handleShellEvent(LONG lEvent, LPITEMIDLIST* ppidl);
//
//    // Shell notification message
//    static const UINT WM_SHELLNOTIFY = WM_USER + 100;
};
