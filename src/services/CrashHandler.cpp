#include "services/CrashHandler.h"
#include "services/LogManager.h"

#include <windows.h>
#include <shellapi.h>
#include <dbghelp.h>
#include <cstdio>

#pragma comment(lib, "dbghelp.lib")

namespace {

static volatile HWND g_trayMessageHwnd = nullptr;

static void cleanupTrayIconOnCrash()
{
    HWND hwnd = g_trayMessageHwnd;
    if (!hwnd) {
        return;
    }
    for (UINT uid = 0; uid <= 2; ++uid) {
        NOTIFYICONDATAW nid = {};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd;
        nid.uID = uid;
        Shell_NotifyIconW(NIM_DELETE, &nid);
    }
}

LONG WINAPI CustomUnhandledExceptionFilter(PEXCEPTION_POINTERS pExceptionInfo)
{
    // 第一时间移除系统托盘图标，避免异常退出后在任务栏右下角残留幽灵图标
    cleanupTrayIconOnCrash();

    // SEH 回调保持最小安全实现：不使用 Qt 对象、不申请堆内存，纯 Win32 API
    wchar_t localAppData[MAX_PATH] = {0};
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        // 兜底回退到当前目录
        wcscpy_s(localAppData, L".");
    }

    wchar_t crashDir[MAX_PATH * 2] = {0};
    swprintf_s(crashDir, L"%s\\LinDesk", localAppData);
    CreateDirectoryW(crashDir, nullptr);
    swprintf_s(crashDir, L"%s\\LinDesk\\logs", localAppData);
    CreateDirectoryW(crashDir, nullptr);
    swprintf_s(crashDir, L"%s\\LinDesk\\logs\\crash", localAppData);
    CreateDirectoryW(crashDir, nullptr);

    SYSTEMTIME st;
    GetLocalTime(&st);

    wchar_t dumpPath[MAX_PATH * 2] = {0};
    swprintf_s(dumpPath, L"%s\\%04d%02d%02d-%02d%02d%02d.dmp",
               crashDir,
               st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond);

    HANDLE hFile = CreateFileW(
        dumpPath,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION dumpInfo;
        dumpInfo.ThreadId = GetCurrentThreadId();
        dumpInfo.ExceptionPointers = pExceptionInfo;
        dumpInfo.ClientPointers = TRUE;

        MiniDumpWriteDump(
            GetCurrentProcess(),
            GetCurrentProcessId(),
            hFile,
            MiniDumpNormal,
            &dumpInfo,
            nullptr,
            nullptr
        );
        CloseHandle(hFile);
    }

    // 崩溃时确保日志缓冲区刷新落盘
    LogManager::shutdown();

    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

namespace CrashHandler {

void initialize()
{
    SetUnhandledExceptionFilter(CustomUnhandledExceptionFilter);
}

void registerTrayWindow(void* hwnd)
{
    g_trayMessageHwnd = reinterpret_cast<HWND>(hwnd);
}

} // namespace CrashHandler
