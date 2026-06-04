#include "services/CrashHandler.h"
#include "services/AppPaths.h"
#include <windows.h>
#include <dbghelp.h>
#include <QDateTime>
#include <QDir>
#include <QDebug>

#pragma comment(lib, "dbghelp.lib")

namespace {
LONG WINAPI CustomUnhandledExceptionFilter(PEXCEPTION_POINTERS pExceptionInfo)
{
    // 创建崩溃转储目录
    QString crashDir = AppPaths::userLocalConfigDir() + "/crashes";
    QDir().mkpath(crashDir);

    QString timeStr = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    QString dumpPath = crashDir + "/crash_" + timeStr + ".dmp";

    HANDLE hFile = CreateFileW(
        reinterpret_cast<LPCWSTR>(dumpPath.utf16()),
        GENERIC_WRITE,
        0,
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

        // 生成 Minidump 崩溃转储文件
        BOOL success = MiniDumpWriteDump(
            GetCurrentProcess(),
            GetCurrentProcessId(),
            hFile,
            MiniDumpNormal,
            &dumpInfo,
            nullptr,
            nullptr
        );
        CloseHandle(hFile);
        if (success) {
            qCritical() << "Application crashed! Minidump written to:" << dumpPath;
        } else {
            qCritical() << "Failed to write minidump. Error:" << GetLastError();
        }
    } else {
        qCritical() << "Failed to create dump file:" << dumpPath;
    }

    return EXCEPTION_EXECUTE_HANDLER;
}
} // namespace

namespace CrashHandler {
void initialize()
{
    SetUnhandledExceptionFilter(CustomUnhandledExceptionFilter);
}
}