#include "services/SystemApi/ShellMenuWorker.h"

#include <QDir>

#include <QFileInfo>

#include <QStandardPaths>

#include <QElapsedTimer>

#include <QDebug>

#include <Shlwapi.h>

#include <shellapi.h>

#include <algorithm>

#include <iostream>

#include <unordered_set>

#pragma comment(lib, "gdi32.lib")

using unique_pidl_absolute = wil::unique_cotaskmem_ptr<std::remove_pointer_t<PIDLIST_ABSOLUTE>>;

namespace {

class WorkerMenuSite : public IServiceProvider, public IOleWindow

{

public:

    HWND m_hwndOwner = nullptr;

    explicit WorkerMenuSite(HWND hwnd = nullptr) : m_hwndOwner(hwnd) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {

        if (!ppv) return E_POINTER;

        if (riid == IID_IUnknown || riid == IID_IServiceProvider) {

            *ppv = static_cast<IServiceProvider*>(this);

            return S_OK;

        }

        if (riid == IID_IOleWindow) {

            *ppv = static_cast<IOleWindow*>(this);

            return S_OK;

        }

        *ppv = nullptr;

        return E_NOINTERFACE;

    }

    STDMETHODIMP_(ULONG) AddRef() override { return 1; }

    STDMETHODIMP_(ULONG) Release() override { return 1; }

    STDMETHODIMP QueryService(REFGUID guidService, REFIID riid, void** ppv) override {

        if (!ppv) return E_POINTER;

        if (riid == IID_IOleWindow || guidService == SID_SShellBrowser) {

            if (riid == IID_IOleWindow) {

                *ppv = static_cast<IOleWindow*>(this);

                return S_OK;

            }

        }

        *ppv = nullptr;

        return E_NOINTERFACE;

    }

    STDMETHODIMP GetWindow(HWND* phwnd) override {

        if (!phwnd) return E_POINTER;

        *phwnd = m_hwndOwner;

        return S_OK;

    }

    STDMETHODIMP ContextSensitiveHelp(BOOL) override {

        return E_NOTIMPL;

    }

};

}

// ============================================================================

// Leaf SEH Functions (namespace ShellMenuSeh)

// Pure C-style functions with zero C++ object unwinding to avoid MSVC C2712

// ============================================================================

namespace ShellMenuSeh {

HRESULT SafeQueryContextMenu(

    IContextMenu* pContextMenu,

    HMENU hMenu,

    UINT indexMenu,

    UINT idCmdFirst,

    UINT idCmdLast,

    UINT uFlags)

{

    if (!pContextMenu) return E_POINTER;

    __try {

        return pContextMenu->QueryContextMenu(hMenu, indexMenu, idCmdFirst, idCmdLast, uFlags);

    }

    __except (EXCEPTION_EXECUTE_HANDLER) {

        return E_FAIL;

    }

}

HRESULT SafeInvokeCommand(

    IContextMenu* pContextMenu,

    CMINVOKECOMMANDINFO* pCmi)

{

    if (!pContextMenu || !pCmi) return E_POINTER;

    __try {

        return pContextMenu->InvokeCommand(pCmi);

    }

    __except (EXCEPTION_EXECUTE_HANDLER) {

        return E_FAIL;

    }

}

HRESULT SafeHandleMenuMsg2(

    IContextMenu3* pContextMenu3,

    UINT uMsg,

    WPARAM wParam,

    LPARAM lParam,

    LRESULT* plResult)

{

    if (!pContextMenu3) return E_POINTER;

    __try {

        return pContextMenu3->HandleMenuMsg2(uMsg, wParam, lParam, plResult);

    }

    __except (EXCEPTION_EXECUTE_HANDLER) {

        return E_FAIL;

    }

}

HRESULT SafeHandleMenuMsg(

    IContextMenu2* pContextMenu2,

    UINT uMsg,

    WPARAM wParam,

    LPARAM lParam)

{

    if (!pContextMenu2) return E_POINTER;

    __try {

        return pContextMenu2->HandleMenuMsg(uMsg, wParam, lParam);

    }

    __except (EXCEPTION_EXECUTE_HANDLER) {

        return E_FAIL;

    }

}

HRESULT SafeGetCommandString(

    IContextMenu* pContextMenu,

    UINT_PTR idCmd,

    UINT uType,

    UINT* pReserved,

    LPSTR pszName,

    UINT cchMax)

{

    if (!pContextMenu) return E_POINTER;

    __try {

        return pContextMenu->GetCommandString(idCmd, uType, pReserved, pszName, cchMax);

    }

    __except (EXCEPTION_EXECUTE_HANDLER) {

        return E_FAIL;

    }

}

} // namespace ShellMenuSeh

// ============================================================================

// RAII COM Apartment Scope

// ============================================================================

struct ComApartmentScope {

    HRESULT hr = E_FAIL;

    explicit ComApartmentScope(DWORD flags = COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE) {

        hr = CoInitializeEx(nullptr, flags);

    }

    ~ComApartmentScope() {

        if (SUCCEEDED(hr)) {

            CoUninitialize();

        }

    }

    bool succeeded() const { return SUCCEEDED(hr); }

};

// ============================================================================

// Dark Menu Theme Helper

// ============================================================================

namespace {

void EnableWindowDarkMenuTheme(HWND hwnd)

{

    if (!hwnd) return;

    static HMODULE hUxtheme = LoadLibraryW(L"uxtheme.dll");

    if (!hUxtheme) return;

    using fnSetPreferredAppMode = int(WINAPI*)(int appMode);

    using fnAllowDarkModeForWindow = bool(WINAPI*)(HWND hWnd, bool allow);

    using fnFlushMenuThemes = void(WINAPI*)();

    static auto pSetPreferredAppMode = reinterpret_cast<fnSetPreferredAppMode>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135)));

    static auto pAllowDarkModeForWindow = reinterpret_cast<fnAllowDarkModeForWindow>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(133)));

    static auto pFlushMenuThemes = reinterpret_cast<fnFlushMenuThemes>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(136)));

    if (pSetPreferredAppMode) {

        pSetPreferredAppMode(1);

    }

    if (pAllowDarkModeForWindow) {

        pAllowDarkModeForWindow(hwnd, true);

    }

    if (pFlushMenuThemes) {

        pFlushMenuThemes();

    }

}

inline bool IsDeletableCustomBitmap(HBITMAP hbmp)

{

    if (!hbmp) {

        return false;

    }

    // Predefined Win32 system menu bitmap pseudo-handles:

    // HBMMENU_CALLBACK = ((HBITMAP) -1)

    // HBMMENU_SYSTEM = ((HBITMAP) 1) through HBMMENU_POPUP_MINIMIZE = ((HBITMAP) 11)

    if (hbmp == HBMMENU_CALLBACK) {

        return false;

    }

    const auto handleVal = reinterpret_cast<uintptr_t>(hbmp);

    if (handleVal <= static_cast<uintptr_t>(11)) {

        return false;

    }

    return true;

}

void CollectAndDeleteMenuBitmaps(HMENU hMenu, std::unordered_set<HBITMAP>& deletedBitmaps)

{

    if (!hMenu || !IsMenu(hMenu)) {

        return;

    }

    const int itemCount = GetMenuItemCount(hMenu);

    if (itemCount <= 0) {

        return;

    }

    for (int i = 0; i < itemCount; ++i) {

        MENUITEMINFOW mii = {};

        mii.cbSize = sizeof(mii);

        mii.fMask = MIIM_SUBMENU | MIIM_BITMAP | MIIM_CHECKMARKS;

        if (!GetMenuItemInfoW(hMenu, i, TRUE, &mii)) {

            continue;

        }

        // Recursively clean bitmaps in submenus first

        if (mii.hSubMenu) {

            CollectAndDeleteMenuBitmaps(mii.hSubMenu, deletedBitmaps);

        }

        // Clean item bitmap (hbmpItem)

        if (IsDeletableCustomBitmap(mii.hbmpItem)) {

            if (deletedBitmaps.insert(mii.hbmpItem).second) {

                DeleteObject(mii.hbmpItem);

            }

        }

        // Clean custom checkmark bitmaps (hbmpChecked / hbmpUnchecked)

        if (IsDeletableCustomBitmap(mii.hbmpChecked)) {

            if (deletedBitmaps.insert(mii.hbmpChecked).second) {

                DeleteObject(mii.hbmpChecked);

            }

        }

        if (IsDeletableCustomBitmap(mii.hbmpUnchecked)) {

            if (deletedBitmaps.insert(mii.hbmpUnchecked).second) {

                DeleteObject(mii.hbmpUnchecked);

            }

        }

    }

}

} // namespace

#if defined(_WIN64)

namespace {

struct GdiCell {

    void* pKernelInfo;

    USHORT ProcessId;

    USHORT Count;

    USHORT Upper;

    USHORT Type;

    void* UserData;

};

inline void CollectProcessGdiBitmaps(DWORD pid, std::unordered_set<HBITMAP>& outBitmaps)

{

    char* peb = reinterpret_cast<char*>(__readgsqword(0x60));

    if (!peb) return;

    GdiCell* cells = *reinterpret_cast<GdiCell**>(peb + 0xF8);

    if (!cells) return;

    for (int i = 0; i < 65536; ++i) {

        if (cells[i].ProcessId == static_cast<USHORT>(pid)) {

            if ((cells[i].Type & 0x1F) == 5) {

                HGDIOBJ h = reinterpret_cast<HGDIOBJ>(static_cast<LONG_PTR>(static_cast<int32_t>((cells[i].Upper << 16) | i)));

                outBitmaps.insert(static_cast<HBITMAP>(h));

            }

        }

    }

}

inline void CleanOrphanedGdiBitmaps(DWORD pid, const std::unordered_set<HBITMAP>& preBitmaps)

{

    std::unordered_set<HBITMAP> postBitmaps;

    CollectProcessGdiBitmaps(pid, postBitmaps);

    for (HBITMAP hbmp : postBitmaps) {

        if (preBitmaps.find(hbmp) == preBitmaps.end()) {

            if (IsDeletableCustomBitmap(hbmp)) {

                DeleteObject(hbmp);

            }

        }

    }

}

} // namespace

#endif

BOOL WINAPI ShellMenuWorker::DestroyMenuAndBitmaps(HMENU hMenu)

{

    if (!hMenu || !IsMenu(hMenu)) {

        return FALSE;

    }

    std::unordered_set<HBITMAP> deletedBitmaps;

    CollectAndDeleteMenuBitmaps(hMenu, deletedBitmaps);

    return DestroyMenu(hMenu);

}

// ============================================================================

// ShellMenuWorker Implementation

// ============================================================================

ShellMenuWorker::ShellMenuWorker(QObject* parent)

    : QThread(parent)

{

    registerMetaTypes();

}

ShellMenuWorker::~ShellMenuWorker()

{

    stopWorker();

    unregisterWindowClass();

}

void ShellMenuWorker::registerMetaTypes()

{

    static bool registered = false;

    if (!registered) {

        qRegisterMetaType<ShellMenuRequest>("ShellMenuRequest");

        qRegisterMetaType<ShellMenuResult>("ShellMenuResult");

        registered = true;

    }

}

bool ShellMenuWorker::startWorker()

{

    if (isRunning()) {

        return true;

    }

    m_stopRequested.store(false);

    start(QThread::HighPriority);

    std::unique_lock<std::mutex> lock(m_initMutex);

    bool ok = m_initCv.wait_for(lock, std::chrono::seconds(3), [this]() {

        return m_hwnd != nullptr && IsWindow(m_hwnd);

    });

    return ok && isRunning();

}

void ShellMenuWorker::stopWorker()

{

    if (!isRunning()) {

        m_hwnd = nullptr;

        m_workerThreadId = 0;

        return;

    }

    m_stopRequested.store(true);

    cancelActiveMenu();

    if (m_hwnd && IsWindow(m_hwnd)) {

        PostMessageW(m_hwnd, WM_SHELL_QUIT, 0, 0);

    }

    if (!wait(3000)) {

        terminate();

        wait(1000);

    }

    m_hwnd = nullptr;

    m_workerThreadId = 0;

}

void ShellMenuWorker::postTask(std::function<void()> task)

{

    if (!task) return;

    {

        std::lock_guard<std::mutex> lock(m_queueMutex);

        m_taskQueue.push_back(std::move(task));

    }

    if (m_hwnd && IsWindow(m_hwnd)) {

        PostMessageW(m_hwnd, WM_SHELL_PROCESS_QUEUE, 0, 0);

    }

}

void ShellMenuWorker::postRequest(const ShellMenuRequest& request,

                                 std::function<void(const ShellMenuResult&)> callback)

{

    if (m_stopRequested.load() || !isRunning()) {

        ShellMenuResult result;

        result.requestId = request.requestId;

        result.executed = false;

        if (callback) {

            callback(result);

        }

        emit menuDismissed(request.requestId);

        return;

    }

    {

        std::lock_guard<std::mutex> lock(m_queueMutex);

        m_requestQueue.push_back({ request, std::move(callback) });

    }

    if (m_hwnd && IsWindow(m_hwnd)) {

        PostMessageW(m_hwnd, WM_SHELL_PROCESS_QUEUE, 0, 0);

    }

}

void ShellMenuWorker::cancelRequest(uint64_t requestId)

{

    std::vector<std::pair<ShellMenuRequest, std::function<void(const ShellMenuResult&)>>> cancelledItems;

    bool isActive = false;

    {

        std::lock_guard<std::mutex> lock(m_queueMutex);

        for (auto it = m_requestQueue.begin(); it != m_requestQueue.end(); ) {

            if (it->first.requestId == requestId) {

                cancelledItems.push_back(std::move(*it));

                it = m_requestQueue.erase(it);

            } else {

                ++it;

            }

        }

        const uint64_t currentActive = m_activeRequestId.load();

        const uint64_t trackingId = m_trackingRequestId.load();

        const bool isTracking = m_isTrackingMenu.load();

        // Match active request ID or tracking menu ID, or apply defensive fallback if tracking menu is active

        if (currentActive == requestId || trackingId == requestId ||

            (isTracking && (currentActive == 0 || currentActive == requestId || trackingId == 0))) {

            isActive = true;

        }

        if (cancelledItems.empty() && !isActive) {

            m_cancelledRequestIds.insert(requestId);

        }

    }

    for (const auto& item : cancelledItems) {

        ShellMenuResult result;

        result.requestId = item.first.requestId;

        result.executed = false;

        if (item.second) {

            item.second(result);

        }

        emit menuDismissed(item.first.requestId);

    }

    if (isActive) {

        cancelActiveMenu();

    }

}

void ShellMenuWorker::cancelActiveMenu()

{

    m_cancelActivePending.store(true);

    if (m_workerThreadId != 0 && GetCurrentThreadId() == m_workerThreadId) {

        if (m_isTrackingMenu.load()) {

            EndMenu();

        }

    }

    if (m_hwnd && IsWindow(m_hwnd)) {

        PostMessageW(m_hwnd, WM_CANCELMODE, 0, 0);

        PostMessageW(m_hwnd, WM_SHELL_CANCEL_ACTIVE, 0, 0);

    }

}

bool ShellMenuWorker::isWorkerRunning() const

{

    return isRunning() && m_hwnd != nullptr && IsWindow(m_hwnd);

}

bool ShellMenuWorker::isTrackingMenu() const

{

    return m_isTrackingMenu.load();

}

void ShellMenuWorker::run()

{

    m_workerThreadId = GetCurrentThreadId();

    ComApartmentScope comScope;

    if (!comScope.succeeded() && comScope.hr != RPC_E_CHANGED_MODE) {

        qCritical() << "[ShellMenuWorker] CoInitializeEx failed: hr=" << Qt::hex << comScope.hr;

        {

            std::lock_guard<std::mutex> lock(m_initMutex);

            m_initCv.notify_all();

        }

        return;

    }

    {

        if (!createMessageWindow()) {

            qCritical() << "[ShellMenuWorker] Failed to create worker host HWND.";

            {

                std::lock_guard<std::mutex> lock(m_initMutex);

                m_initCv.notify_all();

            }

            return;

        }

        {

            std::lock_guard<std::mutex> lock(m_initMutex);

            m_initCv.notify_all();

        }

        emit workerStarted();

        MSG msg;

        while (GetMessageW(&msg, nullptr, 0, 0)) {

            TranslateMessage(&msg);

            DispatchMessageW(&msg);

        }

        // Strict RAII cleanup: reset all COM smart pointers before CoUninitialize

        m_currentContextMenu3.reset();

        m_currentContextMenu2.reset();

        m_currentContextMenu.reset();

        destroyMessageWindow();

    }

    m_workerThreadId = 0;

    emit workerStopped();

}

bool ShellMenuWorker::registerWindowClass()

{

    WNDCLASSEXW wc = {};

    wc.cbSize = sizeof(wc);

    wc.lpfnWndProc = StaticWindowProc;

    wc.hInstance = GetModuleHandleW(nullptr);

    wc.lpszClassName = WORKER_WINDOW_CLASS;

    ATOM atom = RegisterClassExW(&wc);

    return (atom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);

}

void ShellMenuWorker::unregisterWindowClass()

{

    UnregisterClassW(WORKER_WINDOW_CLASS, GetModuleHandleW(nullptr));

}

bool ShellMenuWorker::createMessageWindow()

{

    if (!registerWindowClass()) {

        return false;

    }

    m_hwnd = CreateWindowExW(

        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,

        WORKER_WINDOW_CLASS,

        L"LinDesk_ShellMenuWorkerHost",

        WS_POPUP,

        0, 0, 0, 0,

        nullptr,

        nullptr,

        GetModuleHandleW(nullptr),

        this

    );

    return (m_hwnd != nullptr);

}

void ShellMenuWorker::destroyMessageWindow()

{

    if (m_hwnd && IsWindow(m_hwnd)) {

        DestroyWindow(m_hwnd);

    }

    m_hwnd = nullptr;

}

LRESULT CALLBACK ShellMenuWorker::StaticWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)

{

    if (msg == WM_NCCREATE) {

        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);

        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));

        return DefWindowProcW(hwnd, msg, wParam, lParam);

    }

    auto* worker = reinterpret_cast<ShellMenuWorker*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (worker) {

        return worker->handleWindowMessage(hwnd, msg, wParam, lParam);

    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);

}

LRESULT ShellMenuWorker::handleWindowMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)

{

    switch (msg) {

    case WM_SHELL_PROCESS_QUEUE:

        processQueuedItems();

        return 0;

    case WM_CANCELMODE:

        if (m_isTrackingMenu.load()) {

            EndMenu();

        }

        DefWindowProcW(hwnd, msg, wParam, lParam);

        return 0;

    case WM_SHELL_CANCEL_ACTIVE:

        if (m_isTrackingMenu.load()) {

            EndMenu();

        }

        return 0;

    case WM_SHELL_QUIT:

        if (m_isTrackingMenu.load()) {

            EndMenu();

        }

        DestroyWindow(hwnd);

        PostQuitMessage(0);

        return 0;

    case WM_ENTERMENULOOP:

        m_isTrackingMenu.store(true);

        if (m_cancelActivePending.load() || m_stopRequested.load()) {

            EndMenu();

        }

        return 0;

    case WM_EXITMENULOOP:

        m_isTrackingMenu.store(false);

        m_trackingRequestId.store(0);

        return 0;

    case WM_INITMENUPOPUP: {

        if (m_currentContextMenu3) {

            LRESULT res = 0;

            ShellMenuSeh::SafeHandleMenuMsg2(m_currentContextMenu3.get(), msg, wParam, lParam, &res);

        } else if (m_currentContextMenu2) {

            ShellMenuSeh::SafeHandleMenuMsg(m_currentContextMenu2.get(), msg, wParam, lParam);

        }

        HMENU hSubMenu = reinterpret_cast<HMENU>(wParam);

        if (hSubMenu && !m_activeExclusionKeys.empty()) {

            removeExcludedMenuItems(hSubMenu, m_activeExclusionKeys);

        }

        return 0;

    }

    case WM_DRAWITEM: {

        if (wParam == 0 && lParam != 0) {

            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);

            if (dis && dis->CtlType == ODT_MENU) {

                if (m_currentContextMenu3) {

                    LRESULT res = 0;

                    if (SUCCEEDED(ShellMenuSeh::SafeHandleMenuMsg2(m_currentContextMenu3.get(), msg, wParam, lParam, &res))) {

                        return res;

                    }

                } else if (m_currentContextMenu2) {

                    if (SUCCEEDED(ShellMenuSeh::SafeHandleMenuMsg(m_currentContextMenu2.get(), msg, wParam, lParam))) {

                        return TRUE;

                    }

                }

            }

        }

        break;

    }

    case WM_MEASUREITEM: {

        if (wParam == 0 && lParam != 0) {

            auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);

            if (mis && mis->CtlType == ODT_MENU) {

                if (m_currentContextMenu3) {

                    LRESULT res = 0;

                    if (SUCCEEDED(ShellMenuSeh::SafeHandleMenuMsg2(m_currentContextMenu3.get(), msg, wParam, lParam, &res))) {

                        return res;

                    }

                } else if (m_currentContextMenu2) {

                    if (SUCCEEDED(ShellMenuSeh::SafeHandleMenuMsg(m_currentContextMenu2.get(), msg, wParam, lParam))) {

                        return TRUE;

                    }

                }

            }

        }

        break;

    }

    case WM_MENUCHAR: {

        if (m_currentContextMenu3) {

            LRESULT res = 0;

            if (SUCCEEDED(ShellMenuSeh::SafeHandleMenuMsg2(m_currentContextMenu3.get(), msg, wParam, lParam, &res))) {

                return res;

            }

        } else if (m_currentContextMenu2) {

            if (SUCCEEDED(ShellMenuSeh::SafeHandleMenuMsg(m_currentContextMenu2.get(), msg, wParam, lParam))) {

                return 0;

            }

        }

        break;

    }

    case WM_USER + 102:

        return wParam + lParam;

    case WM_DESTROY:

        m_hwnd = nullptr;

        return 0;

    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);

}

void ShellMenuWorker::cancelRemainingQueuedRequests()

{

    std::deque<std::pair<ShellMenuRequest, std::function<void(const ShellMenuResult&)>>> remaining;

    {

        std::lock_guard<std::mutex> lock(m_queueMutex);

        remaining.swap(m_requestQueue);

        m_cancelledRequestIds.clear();

    }

    for (const auto& reqPair : remaining) {

        ShellMenuResult result;

        result.requestId = reqPair.first.requestId;

        result.executed = false;

        if (reqPair.second) {

            reqPair.second(result);

        }

        emit menuDismissed(reqPair.first.requestId);

    }

}

void ShellMenuWorker::processQueuedItems()

{

    // 1. Drain and execute generic posted tasks

    std::deque<std::function<void()>> tasks;

    {

        std::lock_guard<std::mutex> lock(m_queueMutex);

        tasks.swap(m_taskQueue);

    }

    for (auto& task : tasks) {

        if (m_stopRequested.load()) {

            break;

        }

        if (task) {

            task();

        }

    }

    if (m_stopRequested.load()) {

        cancelRemainingQueuedRequests();

        return;

    }

    // 2. Drain and execute context menu requests

    std::deque<std::pair<ShellMenuRequest, std::function<void(const ShellMenuResult&)>>> requests;

    {

        std::lock_guard<std::mutex> lock(m_queueMutex);

        if (m_isTrackingMenu.load()) {

            // While a modal menu is tracking, only process non-tracking requests (showMenu == false)

            // Any request that requires modal tracking (showMenu == true) MUST be deferred!

            for (auto it = m_requestQueue.begin(); it != m_requestQueue.end(); ) {

                const bool willTrack = it->first.showMenu ||

                    (!it->first.globalPos.isNull() && (it->first.globalPos.x() != 0 || it->first.globalPos.y() != 0));

                if (!willTrack) {

                    requests.push_back(std::move(*it));

                    it = m_requestQueue.erase(it);

                } else {

                    ++it;

                }

            }

            if (!m_requestQueue.empty()) {

                // Re-post message to process deferred modal requests once tracking completes

                PostMessageW(m_hwnd, WM_SHELL_PROCESS_QUEUE, 0, 0);

            }

        } else {

            requests.swap(m_requestQueue);

        }

    }

    for (size_t i = 0; i < requests.size(); ++i) {

        if (m_stopRequested.load()) {

            for (size_t j = i; j < requests.size(); ++j) {

                const auto& remPair = requests[j];

                ShellMenuResult res;

                res.requestId = remPair.first.requestId;

                res.executed = false;

                if (remPair.second) {

                    remPair.second(res);

                }

                emit menuDismissed(remPair.first.requestId);

            }

            cancelRemainingQueuedRequests();

            break;

        }

        const auto& reqPair = requests[i];

        bool isCancelled = false;

        {

            std::lock_guard<std::mutex> lock(m_queueMutex);

            auto it = m_cancelledRequestIds.find(reqPair.first.requestId);

            if (it != m_cancelledRequestIds.end()) {

                isCancelled = true;

                m_cancelledRequestIds.erase(it);

            }

        }

        if (isCancelled) {

            ShellMenuResult res;

            res.requestId = reqPair.first.requestId;

            res.executed = false;

            if (reqPair.second) {

                reqPair.second(res);

            }

            emit menuDismissed(reqPair.first.requestId);

            continue;

        }

        processSingleRequest(reqPair.first, reqPair.second);

    }

    {

        std::lock_guard<std::mutex> lock(m_queueMutex);

        if (m_requestQueue.empty()) {

            m_cancelledRequestIds.clear();

        }

    }

}

ShellMenuWorker::TargetComBinding ShellMenuWorker::bindTargetContextMenu(const ShellMenuRequest& request)

{

    TargetComBinding binding;

    wil::com_ptr_nothrow<IContextMenu> baseContextMenu;

    switch (request.targetType) {

    case ShellTargetType::DesktopBackground: {

        unique_pidl_absolute pidlDesktop;

        if (SUCCEEDED(SHGetKnownFolderIDList(FOLDERID_Desktop, 0, NULL, wil::out_param(pidlDesktop)))) {

            wil::com_ptr_nothrow<IShellFolder> desktopFolder;

            if (SUCCEEDED(SHGetDesktopFolder(&desktopFolder)) && desktopFolder) {

                desktopFolder->CreateViewObject(m_hwnd, IID_PPV_ARGS(&baseContextMenu));

            }

        }

        binding.parsingDirectory = QDir::toNativeSeparators(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)).toStdString();

        break;

    }

    case ShellTargetType::BoxBackground: {

        const QString folderPath = request.targetPath.isEmpty()

            ? QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)

            : request.targetPath;

        unique_pidl_absolute folderPidl;

        HRESULT hr = SHParseDisplayName(reinterpret_cast<const wchar_t*>(folderPath.utf16()), nullptr, wil::out_param(folderPidl), 0, nullptr);

        if (SUCCEEDED(hr) && folderPidl) {

            wil::com_ptr_nothrow<IShellFolder> boxFolder;

            if (SUCCEEDED(SHBindToObject(nullptr, folderPidl.get(), nullptr, IID_PPV_ARGS(&boxFolder))) && boxFolder) {

                boxFolder->CreateViewObject(m_hwnd, IID_PPV_ARGS(&baseContextMenu));

            }

        }

        binding.parsingDirectory = folderPath.toStdString();

        break;

    }

    case ShellTargetType::SpecialFolder: {

        KNOWNFOLDERID kfid;

        switch (request.specialFolder) {

        case SpecialFolders::ThisPc:        kfid = FOLDERID_ComputerFolder; break;

        case SpecialFolders::RecycleBin:     kfid = FOLDERID_RecycleBinFolder; break;

        case SpecialFolders::ControlPanel:

        case SpecialFolders::ControlPanel2:  kfid = FOLDERID_ControlPanelFolder; break;

        case SpecialFolders::Network:        kfid = FOLDERID_NetworkFolder; break;

        case SpecialFolders::UserFolder:     kfid = FOLDERID_UsersFiles; break;

        default: return binding;

        }

        unique_pidl_absolute sfPidl;

        if (SUCCEEDED(SHGetKnownFolderIDList(kfid, 0, NULL, wil::out_param(sfPidl))) && sfPidl) {

            LPCITEMIDLIST relativePidl = nullptr;

            wil::com_ptr_nothrow<IShellFolder> parentFolder;

            if (SUCCEEDED(SHBindToParent(sfPidl.get(), IID_PPV_ARGS(&parentFolder), &relativePidl)) && parentFolder) {

                parentFolder->GetUIObjectOf(m_hwnd, 1, &relativePidl, IID_IContextMenu, nullptr, baseContextMenu.put_void());

            }

        }

        break;

    }

    case ShellTargetType::File:

    case ShellTargetType::Folder:

    case ShellTargetType::Shortcut:

    case ShellTargetType::DiskLetter: {

        if (request.targetPath.isEmpty()) return binding;

        const std::wstring nativePath = QDir::toNativeSeparators(request.targetPath).toStdWString();

        unique_pidl_absolute itemPidl;

        HRESULT hr = SHParseDisplayName(nativePath.c_str(), nullptr, wil::out_param(itemPidl), 0, nullptr);

        if (FAILED(hr) || !itemPidl) {

            itemPidl.reset(ILCreateFromPathW(nativePath.c_str()));

        }

        if (itemPidl) {

            LPCITEMIDLIST relativePidl = nullptr;

            wil::com_ptr_nothrow<IShellFolder> parentFolder;

            if (SUCCEEDED(SHBindToParent(itemPidl.get(), IID_PPV_ARGS(&parentFolder), &relativePidl)) && parentFolder) {

                parentFolder->GetUIObjectOf(m_hwnd, 1, &relativePidl, IID_IContextMenu, nullptr, baseContextMenu.put_void());

            }

        }

        QFileInfo fi(request.targetPath);

        binding.parsingDirectory = fi.isDir() ? request.targetPath.toStdString() : fi.absolutePath().toStdString();

        break;

    }

    case ShellTargetType::MultiObject: {

        if (request.multiPaths.isEmpty()) return binding;

        std::vector<unique_pidl_absolute> absPidls;

        absPidls.reserve(request.multiPaths.size());

        std::vector<LPCITEMIDLIST> relativePidls;

        relativePidls.reserve(request.multiPaths.size());

        wil::com_ptr_nothrow<IShellFolder> commonParentFolder;

        for (const QString& path : request.multiPaths) {

            const std::wstring wpath = QDir::toNativeSeparators(path).toStdWString();

            unique_pidl_absolute pidl;

            if (SUCCEEDED(SHParseDisplayName(wpath.c_str(), nullptr, wil::out_param(pidl), 0, nullptr)) && pidl) {

                LPCITEMIDLIST rel = nullptr;

                wil::com_ptr_nothrow<IShellFolder> parent;

                if (SUCCEEDED(SHBindToParent(pidl.get(), IID_PPV_ARGS(&parent), &rel))) {

                    if (!commonParentFolder) {

                        commonParentFolder = parent;

                    }

                    absPidls.push_back(std::move(pidl));

                    relativePidls.push_back(rel);

                }

            }

        }

        if (commonParentFolder && !relativePidls.empty()) {

            commonParentFolder->GetUIObjectOf(

                m_hwnd,

                static_cast<UINT>(relativePidls.size()),

                relativePidls.data(),

                IID_IContextMenu,

                nullptr,

                baseContextMenu.put_void()

            );

        }

        break;

    }

    }

    if (baseContextMenu) {

        binding.contextMenu = baseContextMenu;

        baseContextMenu->QueryInterface(IID_PPV_ARGS(&binding.contextMenu3));

        if (!binding.contextMenu3) {

            baseContextMenu->QueryInterface(IID_PPV_ARGS(&binding.contextMenu2));

        }

        binding.success = true;

    }

    return binding;

}

void ShellMenuWorker::appendCustomMenuItems(

    HMENU hMenu,

    const std::vector<ShellCustomMenuItem>& items,

    UINT& nextId,

    std::map<UINT, std::function<void()>>& callbackMap)

{

    if (!hMenu || items.empty()) return;

    if (GetMenuItemCount(hMenu) > 0) {

        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    }

    std::function<void(HMENU, const std::vector<ShellCustomMenuItem>&)> addItemsRec;

    addItemsRec = [&](HMENU targetMenu, const std::vector<ShellCustomMenuItem>& menuItems) {

        for (const auto& item : menuItems) {

            if (item.isSeparator) {

                AppendMenuW(targetMenu, MF_SEPARATOR, 0, nullptr);

                continue;

            }

            UINT flags = MF_STRING;

            if (!item.enabled) flags |= MF_GRAYED | MF_DISABLED;

            if (item.checked) flags |= MF_CHECKED;

            if (!item.children.empty()) {

                HMENU hSub = CreatePopupMenu();

                addItemsRec(hSub, item.children);

                AppendMenuW(targetMenu, flags | MF_POPUP, reinterpret_cast<UINT_PTR>(hSub), item.text.c_str());

            } else {

                UINT id = nextId++;

                if (item.callback) {

                    callbackMap[id] = item.callback;

                }

                AppendMenuW(targetMenu, flags, id, item.text.c_str());

                if (item.hBitmap) {

                    SetMenuItemBitmaps(targetMenu, id, MF_BYCOMMAND, item.hBitmap, item.hBitmap);

                }

            }

        }

    };

    addItemsRec(hMenu, items);

}

void ShellMenuWorker::removeExcludedMenuItems(HMENU menu, const std::vector<std::wstring>& exclusionKeys)

{

    if (!menu || exclusionKeys.empty()) return;

    int count = GetMenuItemCount(menu);

    for (int i = count - 1; i >= 0; --i) {

        MENUITEMINFOW mii = {};

        mii.cbSize = sizeof(mii);

        mii.fMask = MIIM_STRING | MIIM_SUBMENU;

        GetMenuItemInfoW(menu, i, TRUE, &mii);

        if (mii.cch > 0) {

            std::wstring text(mii.cch + 1, L'\0');

            mii.dwTypeData = text.data();

            mii.cch++;

            if (GetMenuItemInfoW(menu, i, TRUE, &mii)) {

                text.resize(mii.cch);

                if (mii.hSubMenu) {

                    removeExcludedMenuItems(mii.hSubMenu, exclusionKeys);

                }

                bool match = false;

                for (const auto& key : exclusionKeys) {

                    if (!key.empty() && text.find(key) != std::wstring::npos) {

                        match = true;

                        break;

                    }

                }

                if (match) {

                    if (mii.hSubMenu) {

                        DestroyMenuAndBitmaps(mii.hSubMenu);

                    }

                    MENUITEMINFOW itemBmpInfo = {};

                    itemBmpInfo.cbSize = sizeof(itemBmpInfo);

                    itemBmpInfo.fMask = MIIM_BITMAP | MIIM_CHECKMARKS;

                    if (GetMenuItemInfoW(menu, i, TRUE, &itemBmpInfo)) {

                        if (IsDeletableCustomBitmap(itemBmpInfo.hbmpItem)) {

                            DeleteObject(itemBmpInfo.hbmpItem);

                        }

                        if (IsDeletableCustomBitmap(itemBmpInfo.hbmpChecked)) {

                            DeleteObject(itemBmpInfo.hbmpChecked);

                        }

                        if (IsDeletableCustomBitmap(itemBmpInfo.hbmpUnchecked) &&

                            itemBmpInfo.hbmpUnchecked != itemBmpInfo.hbmpChecked) {

                            DeleteObject(itemBmpInfo.hbmpUnchecked);

                        }

                    }

                    DeleteMenu(menu, i, MF_BYPOSITION);

                }

            }

        } else if (mii.hSubMenu) {

            removeExcludedMenuItems(mii.hSubMenu, exclusionKeys);

        }

    }

}

void ShellMenuWorker::processSingleRequest(

    const ShellMenuRequest& request,

    const std::function<void(const ShellMenuResult&)>& completionCallback)

{

    if (m_stopRequested.load()) {

        ShellMenuResult result;

        result.requestId = request.requestId;

        result.executed = false;

        if (completionCallback) completionCallback(result);

        emit menuDismissed(request.requestId);

        return;

    }

    struct ActiveRequestGuard {

        std::atomic<uint64_t>& target;

        uint64_t previousId;

        bool active = true;

        explicit ActiveRequestGuard(std::atomic<uint64_t>& var, uint64_t id)

            : target(var), previousId(var.load()) {

            target.store(id);

        }

        void restore() {

            if (active) {

                target.store(previousId);

                active = false;

            }

        }

        ~ActiveRequestGuard() {

            restore();

        }

    } activeGuard(m_activeRequestId, request.requestId);

    // Do not wipe cancellation flag if a modal menu is currently tracking

    if (!m_isTrackingMenu.load()) {

        m_cancelActivePending.store(false);

    }

    QElapsedTimer totalStepTimer;

    totalStepTimer.start();

    TargetComBinding binding = bindTargetContextMenu(request);

    const qint64 debugBindMs = totalStepTimer.restart();

    if (!binding.success || !binding.contextMenu || m_cancelActivePending.load() || m_stopRequested.load()) {

        if (!binding.success || !binding.contextMenu) {

            qWarning() << "[ShellMenuWorker] Failed to bind context menu for request" << request.requestId;

        }

        if (!m_isTrackingMenu.load()) {

            m_cancelActivePending.store(false);

        }

        ShellMenuResult result;

        result.requestId = request.requestId;

        result.executed = false;

        if (completionCallback) completionCallback(result);

        emit menuDismissed(request.requestId);

        return;

    }

    ShellMenuWorker::unique_menu hMenu(CreatePopupMenu());

    if (!hMenu || m_cancelActivePending.load() || m_stopRequested.load()) {

        if (!m_isTrackingMenu.load()) {

            m_cancelActivePending.store(false);

        }

        ShellMenuResult result;

        result.requestId = request.requestId;

        result.executed = false;

        if (completionCallback) completionCallback(result);

        emit menuDismissed(request.requestId);

        return;

    }

    UINT queryFlags = CMF_NORMAL | CMF_CANRENAME;

    if (request.extendedVerbs) {

        queryFlags |= CMF_EXTENDEDVERBS;

    }

    const DWORD currentPid = GetCurrentProcessId();

    std::unordered_set<HBITMAP> preQueryBitmaps;

#if defined(_WIN64)

    CollectProcessGdiBitmaps(currentPid, preQueryBitmaps);

#endif

    WorkerMenuSite workerSite(m_hwnd);

    wil::com_ptr_nothrow<IObjectWithSite> ows;

    if (binding.contextMenu) {

        ows = binding.contextMenu.try_query<IObjectWithSite>();

        if (ows) {

            ows->SetSite(static_cast<IServiceProvider*>(&workerSite));

        }

    }

    QElapsedTimer queryTimer;

    queryTimer.start();

    HRESULT queryHr = ShellMenuSeh::SafeQueryContextMenu(

        binding.contextMenu.get(),

        hMenu.get(),

        0,

        WORKER_MIN_SHELL_ID,

        WORKER_MAX_SHELL_ID,

        queryFlags

    );

    const qint64 queryMenuMs = queryTimer.elapsed();

    const qint64 debugQueryMs = totalStepTimer.restart();

    std::cout << "[Worker Debug Timers] bindMs=" << debugBindMs << " queryMenuMs=" << queryMenuMs << std::endl;

    if (FAILED(queryHr) || m_cancelActivePending.load() || m_stopRequested.load()) {

        if (ows) {

            ows->SetSite(nullptr);

        }

        if (FAILED(queryHr)) {

            qWarning() << "[ShellMenuWorker] QueryContextMenu failed: hr=" << Qt::hex << queryHr

                       << "queryMenuMs=" << queryMenuMs;

        } else {

            qInfo() << "[ShellMenuWorker] Menu cancelled during query for request" << request.requestId;

        }

        if (!m_isTrackingMenu.load()) {

            m_cancelActivePending.store(false);

        }

        hMenu.reset();

#if defined(_WIN64)

        CleanOrphanedGdiBitmaps(currentPid, preQueryBitmaps);

#endif

        binding.contextMenu3.reset();

        binding.contextMenu2.reset();

        binding.contextMenu.reset();

        ShellMenuResult result;

        result.requestId = request.requestId;

        result.executed = false;

        if (completionCallback) completionCallback(result);

        emit menuDismissed(request.requestId);

        return;

    }

    // Insert LinDesk custom items (ID >= 10001)

    std::map<UINT, std::function<void()>> localCustomCallbacks;

    if (!request.customItems.empty()) {

        UINT nextCustomId = WORKER_CUSTOM_MENU_ID_BASE;

        appendCustomMenuItems(hMenu.get(), request.customItems, nextCustomId, localCustomCallbacks);

    }

    // Apply exclusion filtering

    QElapsedTimer filterTimer;

    filterTimer.start();

    if (!request.exclusionKeys.empty()) {

        removeExcludedMenuItems(hMenu.get(), request.exclusionKeys);

    }

    const qint64 removeItemsMs = filterTimer.elapsed();

    // Preserve exact diagnostic log format for acceptance criteria verification

    qInfo() << "[ShellContextMenu] ShowMenu ready:"

            << "operationType=" << static_cast<int>(request.targetType)

            << "exclusionsActive=" << !request.exclusionKeys.empty()

            << "bindMs=" << debugBindMs << " queryContextMenuMs=" << queryMenuMs

            << "removeExcludedItemsMs=" << removeItemsMs

            << "menuItemCount=" << GetMenuItemCount(hMenu.get());

    if (m_cancelActivePending.load() || m_stopRequested.load()) {

        qInfo() << "[ShellMenuWorker] Menu cancelled before tracking for request" << request.requestId;

        if (!m_isTrackingMenu.load()) {

            m_cancelActivePending.store(false);

        }

        hMenu.reset();

#if defined(_WIN64)

        CleanOrphanedGdiBitmaps(currentPid, preQueryBitmaps);

#endif

        binding.contextMenu3.reset();

        binding.contextMenu2.reset();

        binding.contextMenu.reset();

        ShellMenuResult result;

        result.requestId = request.requestId;

        result.executed = false;

        if (completionCallback) completionCallback(result);

        emit menuDismissed(request.requestId);

        return;

    }

    const bool shouldTrack = request.showMenu ||

        (!request.globalPos.isNull() && (request.globalPos.x() != 0 || request.globalPos.y() != 0));

    if (!shouldTrack) {

        hMenu.reset();

#if defined(_WIN64)

        CleanOrphanedGdiBitmaps(currentPid, preQueryBitmaps);

#endif

        binding.contextMenu3.reset();

        binding.contextMenu2.reset();

        binding.contextMenu.reset();

        activeGuard.restore();

        ShellMenuResult result;

        result.requestId = request.requestId;

        result.executed = true;

        if (completionCallback) completionCallback(result);

        emit menuCompleted(result);

        return;

    }

    // Prepare active context for window proc message routing

    m_currentContextMenu3 = binding.contextMenu3;

    m_currentContextMenu2 = binding.contextMenu2;

    m_currentContextMenu = binding.contextMenu;

    m_activeExclusionKeys = request.exclusionKeys;

    m_customCallbackMap = std::move(localCustomCallbacks);

    m_trackingRequestId.store(request.requestId);

    // Track popup menu on worker thread

    EnableWindowDarkMenuTheme(m_hwnd);

    SetForegroundWindow(m_hwnd);

    SetActiveWindow(m_hwnd);

    // Defense-in-depth: abort immediately if cancellation occurred during focus/activation

    if (m_cancelActivePending.load() || m_stopRequested.load()) {

        qInfo() << "[ShellMenuWorker] Menu cancelled immediately prior to TrackPopupMenu for request" << request.requestId;

        m_trackingRequestId.store(0);

        m_cancelActivePending.store(false);

        m_currentContextMenu3.reset();

        m_currentContextMenu2.reset();

        m_currentContextMenu.reset();

        m_activeExclusionKeys.clear();

        m_customCallbackMap.clear();

        hMenu.reset();

#if defined(_WIN64)

        CleanOrphanedGdiBitmaps(currentPid, preQueryBitmaps);

#endif

        binding.contextMenu3.reset();

        binding.contextMenu2.reset();

        binding.contextMenu.reset();

        ShellMenuResult result;

        result.requestId = request.requestId;

        result.executed = false;

        if (completionCallback) completionCallback(result);

        emit menuDismissed(request.requestId);

        return;

    }

    m_isTrackingMenu.store(true);

    UINT cmd = TrackPopupMenu(

        hMenu.get(),

        TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,

        request.globalPos.x(),

        request.globalPos.y(),

        0,

        m_hwnd,

        nullptr

    );

    PostMessageW(m_hwnd, WM_NULL, 0, 0);

    m_isTrackingMenu.store(false);

    m_trackingRequestId.store(0);

    const bool wasCancelled = m_cancelActivePending.exchange(false);

    if (ows) {

        ows->SetSite(nullptr);

    }

    // Free menu and attached bitmaps immediately after tracking completes

    hMenu.reset();

#if defined(_WIN64)

    CleanOrphanedGdiBitmaps(currentPid, preQueryBitmaps);

#endif

    // Clear active references

    m_currentContextMenu3.reset();

    m_currentContextMenu2.reset();

    m_currentContextMenu.reset();

    m_activeExclusionKeys.clear();

    if (wasCancelled || cmd == 0) {

        m_customCallbackMap.clear();

        ShellMenuResult result;

        result.requestId = request.requestId;

        result.executed = false;

        if (completionCallback) completionCallback(result);

        emit menuDismissed(request.requestId);

        return;

    }

    // Handle command selection

    handleMenuSelection(request, binding, cmd, completionCallback);

    m_customCallbackMap.clear();

}

void ShellMenuWorker::handleMenuSelection(

    const ShellMenuRequest& request,

    const TargetComBinding& binding,

    UINT cmd,

    const std::function<void(const ShellMenuResult&)>& completionCallback)

{

    ShellMenuResult result;

    result.requestId = request.requestId;

    if (cmd == 0) {

        result.executed = false;

        if (completionCallback) completionCallback(result);

        emit menuDismissed(request.requestId);

        return;

    }

    // 1. Custom LinDesk Action (ID >= 10001)

    if (cmd >= WORKER_CUSTOM_MENU_ID_BASE) {

        auto it = m_customCallbackMap.find(cmd);

        if (it != m_customCallbackMap.end() && it->second) {

            result.executed = true;

            result.commandId = cmd;

            result.isCustomAction = true;

            result.customCallback = it->second;

            if (completionCallback) completionCallback(result);

            emit menuCompleted(result);

        } else {

            result.executed = false;

            if (completionCallback) completionCallback(result);

            emit menuDismissed(request.requestId);

        }

        return;

    }

    // 2. Native Windows Shell Verb (ID 1 - 9999)

    if (cmd >= WORKER_MIN_SHELL_ID && cmd <= WORKER_MAX_SHELL_ID && binding.contextMenu) {

        char verbBuf[64] = "";

        HRESULT hrVerb = ShellMenuSeh::SafeGetCommandString(

            binding.contextMenu.get(),

            cmd - WORKER_MIN_SHELL_ID,

            GCS_VERBA,

            nullptr,

            verbBuf,

            sizeof(verbBuf)

        );

        std::string verb = SUCCEEDED(hrVerb) ? verbBuf : "";

        // Intercept "rename" verb for Qt inline editor

        if (_stricmp(verb.c_str(), "rename") == 0) {

            result.executed = true;

            result.verb = "rename";

            result.commandId = cmd;

            result.isCustomAction = false;

            if (request.ownerHwnd) {

                PostMessageW(request.ownerHwnd, WM_LINDESK_START_INLINE_RENAME, 0, 0);

            }

            if (completionCallback) completionCallback(result);

            emit menuCompleted(result);

            return;

        }

        // Execute native shell verb on worker STA thread

        CMINVOKECOMMANDINFO cmi = {};

        cmi.cbSize = sizeof(cmi);

        cmi.fMask = 0;

        cmi.hwnd = request.ownerHwnd ? request.ownerHwnd : m_hwnd;

        cmi.lpVerb = MAKEINTRESOURCEA(cmd - WORKER_MIN_SHELL_ID);

        cmi.lpDirectory = binding.parsingDirectory.empty() ? nullptr : binding.parsingDirectory.c_str();

        cmi.nShow = SW_SHOWNORMAL;

        HRESULT hrInvoke = ShellMenuSeh::SafeInvokeCommand(binding.contextMenu.get(), &cmi);

        result.executed = SUCCEEDED(hrInvoke);

        result.verb = verb;

        result.commandId = cmd;

        result.isCustomAction = false;

        if (completionCallback) completionCallback(result);

        emit menuCompleted(result);

        return;

    }

    result.executed = false;

    if (completionCallback) completionCallback(result);

    emit menuDismissed(request.requestId);

}
