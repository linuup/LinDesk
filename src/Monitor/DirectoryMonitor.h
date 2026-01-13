#pragma once
#include <windows.h>
#include <QString>
#include "ShObjIdl_core.h"
#include <QObject>
#include "Monitor/IDirectoryMonitorInterface.h"

class DirectoryMonitor : public IDirectoryMonitorListener
{
public:
    DirectoryMonitor(HWND hwnd,QObject* parent = nullptr);
    virtual ~DirectoryMonitor();
    void stopMonitoring();
    void startMonitoring();
    void run();
    // ========== IListenerInterface 接口实现 ==========
    bool startListening() override;
    void stopListening() override;

    // ========== IRegistryListener 接口实现 ==========
private:
    HWND m_hwnd = nullptr;
    ULONG m_regId = 0;


    // Windows message window procedure
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Shell event handler
    void handleShellEvent(LONG lEvent, LPITEMIDLIST* ppidl);

    // Shell notification message
    static const UINT WM_SHELLNOTIFY = WM_USER + 52;
};
