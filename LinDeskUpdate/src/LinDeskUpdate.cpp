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

bool hasArgument(const std::vector<std::wstring>& args, const std::wstring& name)
{
    for (const std::wstring& arg : args) {
        if (arg == name) {
            return true;
        }
    }
    return false;
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

std::wstring currentModulePath()
{
    std::wstring path(MAX_PATH, L'\0');
    DWORD size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    while (size == path.size()) {
        path.resize(path.size() * 2);
        size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    }
    if (size == 0) {
        return {};
    }
    path.resize(size);
    return path;
}

std::filesystem::path updaterRuntimeDir()
{
    return std::filesystem::path(localAppDataPath()) / L"LinDesk" / L"UpdaterRuntime";
}

void initLog()
{
    const std::filesystem::path logDir = std::filesystem::path(localAppDataPath()) / L"LinDesk" / L"logs";
    std::error_code ec;
    std::filesystem::create_directories(logDir, ec);
    g_log.open(logDir / L"updater.log", std::ios::app);
}

void logLine(const std::wstring& text)
{
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    if (g_log.is_open()) {
        wchar_t prefix[128] = {};
        swprintf_s(prefix, L"[%04u-%02u-%02u %02u:%02u:%02u.%03u] [info] [updater] [%lu] [%lu] ",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            GetCurrentProcessId(), GetCurrentThreadId());
        g_log << prefix << text << std::endl;
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

bool samePath(const std::filesystem::path& left, const std::filesystem::path& right)
{
    std::error_code ec;
    return std::filesystem::equivalent(left, right, ec);
}

void removeDirectoryIfEmpty(const std::filesystem::path& dirPath)
{
    std::error_code ec;
    if (std::filesystem::exists(dirPath, ec) && std::filesystem::is_empty(dirPath, ec)) {
        std::filesystem::remove(dirPath, ec);
    }
}

void cleanupOldUpdaterRuntimeFiles()
{
    const std::filesystem::path runtimeDir = updaterRuntimeDir();
    std::error_code ec;
    if (!std::filesystem::exists(runtimeDir, ec)) {
        return;
    }

    const std::filesystem::path currentPath = currentModulePath();
    for (const auto& entry : std::filesystem::directory_iterator(runtimeDir, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const std::filesystem::path filePath = entry.path();
        if (samePath(filePath, currentPath)) {
            continue;
        }
        if (filePath.filename().wstring().find(L"LinDeskUpdate_") == 0 && filePath.extension() == L".exe") {
            std::filesystem::remove(filePath, ec);
        }
    }
}

void scheduleCurrentUpdaterSelfDelete()
{
    const std::filesystem::path currentPath = currentModulePath();
    const std::filesystem::path runtimeDir = updaterRuntimeDir();
    if (currentPath.empty() || !samePath(currentPath.parent_path(), runtimeDir)) {
        return;
    }

    DWORD errorCode = 0;
    const std::wstring command = L"cmd.exe /C timeout /T 2 /NOBREAK >NUL & del /F /Q "
        + quote(currentPath.wstring())
        + L" >NUL 2>NUL & rmdir "
        + quote(runtimeDir.wstring())
        + L" >NUL 2>NUL";
    runDetached(command, errorCode);
}

std::wstring buildWorkerCommandLine(const std::filesystem::path& workerPath, const std::vector<std::wstring>& args)
{
    std::wstring command = quote(workerPath.wstring());
    for (size_t i = 1; i < args.size(); ++i) {
        command += L" " + quote(args[i]);
    }
    command += L" --worker";
    return command;
}

int relaunchFromUserWritableLocation(const std::vector<std::wstring>& args)
{
    if (hasArgument(args, L"--worker")) {
        return 0;
    }

    const std::wstring modulePath = currentModulePath();
    if (modulePath.empty()) {
        logLine(L"Failed to get current updater path.");
        return 2;
    }

    const std::filesystem::path runtimeDir = updaterRuntimeDir();
    std::error_code ec;
    std::filesystem::create_directories(runtimeDir, ec);
    if (ec) {
        logLine(L"Failed to create updater runtime directory.");
        return 2;
    }

    const std::filesystem::path workerPath = runtimeDir / (L"LinDeskUpdate_" + std::to_wstring(GetCurrentProcessId()) + L".exe");
    std::filesystem::copy_file(modulePath, workerPath, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        const std::string message = ec.message();
        logLine(L"Failed to copy updater worker: " + std::wstring(message.begin(), message.end()));
        return 2;
    }

    DWORD errorCode = 0;
    const std::wstring command = buildWorkerCommandLine(workerPath, args);
    logLine(L"Relaunching updater from user writable location: " + command);
    if (!runDetached(command, errorCode)) {
        logLine(L"Failed to start updater worker. Error: " + std::to_wstring(errorCode));
        return 2;
    }
    return 1;
}

bool deleteFileIfExists(const std::wstring& filePath)
{
    if (filePath.empty()) {
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::exists(filePath, ec)) {
        return true;
    }

    return std::filesystem::remove(filePath, ec);
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

    const int relaunchState = relaunchFromUserWritableLocation(args);
    if (relaunchState == 1) {
        logLine(L"Original updater process finished after relaunch.");
        return 0;
    }
    if (relaunchState == 2) {
        logLine(L"Failed to relaunch updater outside the install directory.");
        return 6;
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
    logLine(L"Running installer and waiting: " + command);
    if (!runAndWait(command, installerErrorCode)) {
        logLine(L"Failed to start installer. Error: " + std::to_wstring(installerErrorCode));
        return 4;
    }
    logLine(L"Installer finished. Exit code: " + std::to_wstring(installerErrorCode));

    if (installerErrorCode == 0) {
        if (deleteFileIfExists(installer)) {
            logLine(L"Deleted downloaded installer: " + installer);
        } else {
            logLine(L"Failed to delete downloaded installer: " + installer);
        }
        removeDirectoryIfEmpty(std::filesystem::path(installer).parent_path());
        cleanupOldUpdaterRuntimeFiles();
        scheduleCurrentUpdaterSelfDelete();
    } else {
        logLine(L"Installer did not finish successfully. Keep downloaded installer for retry.");
    }

    if (restart) {
        logLine(L"Restart is delegated to the visible installer flow.");
    }
    logLine(L"LinDeskUpdate finished.");
    return installerErrorCode == 0 ? 0 : 5;
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
