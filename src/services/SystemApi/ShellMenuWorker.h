#pragma once

#include <QThread>
#include <QPoint>
#include <QString>
#include <QVector>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include <Windows.h>
#include <ShlObj.h>
#include <wil/com.h>

#include "CommonEnum.h"
#include "ShellMenuTypes.h"

// ============================================================================
// ShellMenuWorker: Dedicated STA Thread & Message Host Window
// ============================================================================
class ShellMenuWorker : public QThread {
    Q_OBJECT

public:
    static constexpr const wchar_t* WORKER_WINDOW_CLASS = L"LinDesk_ShellMenuWorkerHost";

    static constexpr UINT WORKER_MIN_SHELL_ID = 1;
    static constexpr UINT WORKER_MAX_SHELL_ID = 9999;
    static constexpr UINT WORKER_CUSTOM_MENU_ID_BASE = 10001;

    enum WorkerMessage : UINT {
        WM_SHELL_PROCESS_QUEUE = WM_APP + 101,
        WM_SHELL_CANCEL_ACTIVE = WM_APP + 102,
        WM_SHELL_QUIT          = WM_APP + 103,
        WM_SHELL_EXECUTE_TASK  = WM_APP + 104
    };

    explicit ShellMenuWorker(QObject* parent = nullptr);
    ~ShellMenuWorker() override;

    // Disallow copy/move
    ShellMenuWorker(const ShellMenuWorker&) = delete;
    ShellMenuWorker& operator=(const ShellMenuWorker&) = delete;

    // ------------------------------------------------------------------------
    // Public Thread Management Interface (Called from Qt UI Thread)
    // ------------------------------------------------------------------------
    bool startWorker();
    void stopWorker();

    void postTask(std::function<void()> task);
    void postRequest(const ShellMenuRequest& request, std::function<void(const ShellMenuResult&)> callback = nullptr);
    void cancelRequest(uint64_t requestId);
    void cancelActiveMenu();

    bool isWorkerRunning() const;
    bool isTrackingMenu() const;
    HWND hostHwnd() const { return m_hwnd; }
    HWND workerHwnd() const { return m_hwnd; }
    DWORD workerThreadId() const { return m_workerThreadId; }
    DWORD threadId() const { return m_workerThreadId; }

    static void registerMetaTypes();

    // GDI resource cleanup helper for Win32 menus and nested submenus
    static BOOL WINAPI DestroyMenuAndBitmaps(HMENU hMenu);

    // Filter and remove excluded menu items, safely destroying submenus and bitmaps
    static void removeExcludedMenuItems(HMENU menu, const std::vector<std::wstring>& exclusionKeys);

    // RAII wrapper for HMENU that automatically cleans up attached GDI bitmaps
    using unique_menu = wil::unique_any<HMENU, decltype(&ShellMenuWorker::DestroyMenuAndBitmaps), ShellMenuWorker::DestroyMenuAndBitmaps>;

signals:
    // ------------------------------------------------------------------------
    // Signals Emitted to Qt Main Thread (Delivered via Qt::QueuedConnection)
    // ------------------------------------------------------------------------
    void workerStarted();
    void workerStopped();
    void menuCompleted(const ShellMenuResult& result);
    void menuDismissed(uint64_t requestId);
    void backgroundSnapshotReady(uint64_t requestId);

protected:
    // ------------------------------------------------------------------------
    // QThread Override: Runs on the Dedicated STA Background Thread
    // ------------------------------------------------------------------------
    void run() override;

private:
    // ------------------------------------------------------------------------
    // Internal Binding Structure
    // ------------------------------------------------------------------------
    struct TargetComBinding {
        wil::com_ptr_nothrow<IContextMenu> contextMenu;
        wil::com_ptr_nothrow<IContextMenu2> contextMenu2;
        wil::com_ptr_nothrow<IContextMenu3> contextMenu3;
        std::string parsingDirectory;
        bool success = false;
    };

    // ------------------------------------------------------------------------
    // Win32 Window Procedure & Host Window Methods
    // ------------------------------------------------------------------------
    static LRESULT CALLBACK StaticWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handleWindowMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    bool registerWindowClass();
    void unregisterWindowClass();
    bool createMessageWindow();
    void destroyMessageWindow();

    // ------------------------------------------------------------------------
    // Menu Processing Internals (Worker Thread Only)
    // ------------------------------------------------------------------------
    void processQueuedItems();
    void cancelRemainingQueuedRequests();
    void processSingleRequest(const ShellMenuRequest& request,
                             const std::function<void(const ShellMenuResult&)>& completionCallback);
    TargetComBinding bindTargetContextMenu(const ShellMenuRequest& request);
    void handleMenuSelection(const ShellMenuRequest& request,
                            const TargetComBinding& binding,
                            UINT cmd,
                            const std::function<void(const ShellMenuResult&)>& completionCallback);

    void appendCustomMenuItems(HMENU hMenu,
                               const std::vector<ShellCustomMenuItem>& items,
                               UINT& nextId,
                               std::map<UINT, std::function<void()>>& callbackMap);

    // ------------------------------------------------------------------------
    // Member State
    // ------------------------------------------------------------------------
    HWND m_hwnd = nullptr;
    DWORD m_workerThreadId = 0;
    std::atomic<bool> m_stopRequested{ false };
    std::atomic<bool> m_isTrackingMenu{ false };
    std::atomic<bool> m_cancelActivePending{ false };
    std::atomic<uint64_t> m_activeRequestId{ 0 };
    std::atomic<uint64_t> m_trackingRequestId{ 0 };

    mutable std::mutex m_initMutex;
    std::condition_variable m_initCv;

    // Queues protected by m_queueMutex
    mutable std::mutex m_queueMutex;
    std::deque<std::function<void()>> m_taskQueue;
    std::deque<std::pair<ShellMenuRequest, std::function<void(const ShellMenuResult&)>>> m_requestQueue;
    std::unordered_set<uint64_t> m_cancelledRequestIds;

    // Active menu session COM objects (strictly confined to STA thread)
    wil::com_ptr_nothrow<IContextMenu> m_currentContextMenu;
    wil::com_ptr_nothrow<IContextMenu2> m_currentContextMenu2;
    wil::com_ptr_nothrow<IContextMenu3> m_currentContextMenu3;

    // Custom items and exclusions for active session
    std::vector<std::wstring> m_activeExclusionKeys;
    std::map<UINT, std::function<void()>> m_customCallbackMap;
};
