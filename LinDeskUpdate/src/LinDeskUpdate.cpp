#include <Windows.h>
#include <TlHelp32.h>
#include <Shellapi.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
std::wofstream g_log;

std::wstring argumentValue(const std::vector<std::wstring>& args, const std::wstring& name)
{
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == name) {
            return args[i + 1];
        }
    }
    return {};
}

std::wstring localAppDataPath()
{
    wchar_t buffer[MAX_PATH] = {};
    const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (size == 0 || size >= MAX_PATH) {
        return L".";
    }
    return buffer;
}

void initLog()
{
    const std::filesystem::path logDir = std::filesystem::path(localAppDataPath()) / L"LinDesk" / L"Logs";
    std::error_code ec;
    std::filesystem::create_directories(logDir, ec);
    g_log.open(logDir / L"updater.log", std::ios::app);
}

void logLine(const std::wstring& text)
{
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    if (g_log.is_open()) {
        g_log << L"[" << st.wYear << L"-" << st.wMonth << L"-" << st.wDay
              << L" " << st.wHour << L":" << st.wMinute << L":" << st.wSecond
              << L"] " << text << std::endl;
    }
}

std::wstring quote(const std::wstring& value)
{
    std::wstring escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back(L'"');
    for (wchar_t ch : value) {
        if (ch == L'"') {
            escaped.push_back(L'\\');
        }
        escaped.push_back(ch);
    }
    escaped.push_back(L'"');
    return escaped;
}

bool runAndWait(const std::wstring& commandLine, DWORD& exitCode)
{
    std::wstring mutableCommand = commandLine;
    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo = {};

    if (!CreateProcessW(nullptr,
                        mutableCommand.data(),
                        nullptr,
                        nullptr,
                        FALSE,
                        CREATE_NO_WINDOW,
                        nullptr,
                        nullptr,
                        &startupInfo,
                        &processInfo)) {
        exitCode = GetLastError();
        return false;
    }

    WaitForSingleObject(processInfo.hProcess, INFINITE);
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
}

bool runDetached(const std::wstring& commandLine, DWORD& errorCode)
{
    std::wstring mutableCommand = commandLine;
    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo = {};

    if (!CreateProcessW(nullptr,
                        mutableCommand.data(),
                        nullptr,
                        nullptr,
                        FALSE,
                        0,
                        nullptr,
                        nullptr,
                        &startupInfo,
                        &processInfo)) {
        errorCode = GetLastError();
        return false;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    errorCode = 0;
    return true;
}

bool isProcessRunning(const std::wstring& processName)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, processName.c_str()) == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

bool waitForProcessExit(const std::wstring& processName, int timeoutMs)
{
    const DWORD start = GetTickCount();
    while (isProcessRunning(processName)) {
        if (static_cast<int>(GetTickCount() - start) > timeoutMs) {
            return false;
        }
        Sleep(500);
    }
    return true;
}

bool closeMainProcess(const std::wstring& processName)
{
    if (processName.empty() || !isProcessRunning(processName)) {
        return true;
    }

    DWORD exitCode = 0;
    const std::wstring graceful = L"taskkill.exe /IM " + quote(processName) + L" /T";
    logLine(L"Closing main process: " + graceful);
    runAndWait(graceful, exitCode);

    if (waitForProcessExit(processName, 15000)) {
        return true;
    }

    const std::wstring forced = L"taskkill.exe /F /IM " + quote(processName) + L" /T";
    logLine(L"Force closing main process: " + forced);
    runAndWait(forced, exitCode);
    return waitForProcessExit(processName, 10000);
}

bool toBool(const std::wstring& value)
{
    return _wcsicmp(value.c_str(), L"true") == 0 || value == L"1";
}

std::wstring normalizedInstallerLanguage(const std::wstring& value)
{
    if (_wcsicmp(value.c_str(), L"zh_CN") == 0 || _wcsicmp(value.c_str(), L"zh") == 0) {
        return L"zh";
    }
    if (_wcsicmp(value.c_str(), L"en_US") == 0 || _wcsicmp(value.c_str(), L"en") == 0) {
        return L"en";
    }
    return {};
}
}

int runUpdater(int argc, wchar_t* argv[])
{
    initLog();
    logLine(L"LinDeskUpdate started.");

    std::vector<std::wstring> args;
    args.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    const std::wstring installer = argumentValue(args, L"--installer");
    const std::wstring app = argumentValue(args, L"--app");
    const std::wstring processName = argumentValue(args, L"--process");
    const std::wstring language = normalizedInstallerLanguage(argumentValue(args, L"--language"));
    const bool restart = toBool(argumentValue(args, L"--restart"));

    if (installer.empty() || app.empty() || processName.empty()) {
        logLine(L"Invalid arguments.");
        return 2;
    }

    if (!closeMainProcess(processName)) {
        logLine(L"Failed to close main process.");
        return 3;
    }

    DWORD installerErrorCode = 0;
    std::wstring command = quote(installer) + L" /NORESTART";
    if (!language.empty()) {
        command += L" /LANG=" + language;
    }
    command += L" /LINDESKUPDATE=1";
    logLine(L"Running installer: " + command);
    if (!runDetached(command, installerErrorCode)) {
        logLine(L"Failed to start installer. Error: " + std::to_wstring(installerErrorCode));
        return 4;
    }

    if (restart) {
        logLine(L"Restart is delegated to the visible installer flow.");
    }
    logLine(L"Installer started. LinDeskUpdate finished.");
    return 0;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        return 2;
    }
    const int result = runUpdater(argc, argv);
    LocalFree(argv);
    return result;
}

int wmain(int argc, wchar_t* argv[])
{
    return runUpdater(argc, argv);
}
