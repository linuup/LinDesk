// SystemApi.cpp : Defines the exported functions for the DLL.
#include "SystemApi.h"
#include "Tool.h"
#include <Shlwapi.h>
#include <fstream>
#include <cstdio>
#include <QDebug>

namespace {
constexpr size_t kShellLinkHeaderSize = 0x4C;
constexpr DWORD kHasLinkTargetIDListFlag = 0x00000001;

std::wstring NormalizeShellPath(std::wstring path)
{
    if (path.empty()) {
        return path;
    }

    for (wchar_t& ch : path) {
        if (ch == L'/') {
            ch = L'\\';
        } else {
            ch = static_cast<wchar_t>(ch);
        }
    }

    while (path.length() > 3 && (path.back() == L'\\' || path.back() == L'/')) {
        path.pop_back();
    }

    return path;
}

std::wstring ResolveKnownFolderPath(REFKNOWNFOLDERID folderId)
{
    wil::unique_cotaskmem_string knownFolderPath;
    if (SUCCEEDED(SHGetKnownFolderPath(folderId, 0, nullptr, wil::out_param(knownFolderPath))) && knownFolderPath) {
        return NormalizeShellPath(knownFolderPath.get());
    }

    return std::wstring();
}

std::vector<std::wstring> CurrentUserFolderPaths()
{
    std::vector<std::wstring> paths;

    const std::wstring usersFilesPath = ResolveKnownFolderPath(FOLDERID_UsersFiles);
    if (!usersFilesPath.empty()) {
        paths.push_back(usersFilesPath);
    }

    const std::wstring profilePath = ResolveKnownFolderPath(FOLDERID_Profile);
    bool hasProfilePath = false;
    for (const std::wstring& existing : paths) {
        if (_wcsicmp(existing.c_str(), profilePath.c_str()) == 0) {
            hasProfilePath = true;
            break;
        }
    }
    if (!profilePath.empty() && !hasProfilePath) {
        paths.push_back(profilePath);
    }

    wchar_t userProfileValue[MAX_PATH] = { 0 };
    const DWORD userProfileLength = GetEnvironmentVariableW(L"USERPROFILE", userProfileValue, MAX_PATH);
    const std::wstring userProfilePath = (userProfileLength > 0 && userProfileLength < MAX_PATH && userProfileValue[0] != L'\0')
        ? NormalizeShellPath(userProfileValue)
        : std::wstring();
    bool hasUserProfilePath = false;
    for (const std::wstring& existing : paths) {
        if (_wcsicmp(existing.c_str(), userProfilePath.c_str()) == 0) {
            hasUserProfilePath = true;
            break;
        }
    }
    if (!userProfilePath.empty() && !hasUserProfilePath) {
        paths.push_back(userProfilePath);
    }

    if (paths.empty()) {
        wchar_t homeDriveValue[MAX_PATH] = { 0 };
        wchar_t homePathValue[MAX_PATH] = { 0 };
        const DWORD homeDriveLength = GetEnvironmentVariableW(L"HOMEDRIVE", homeDriveValue, MAX_PATH);
        const DWORD homePathLength = GetEnvironmentVariableW(L"HOMEPATH", homePathValue, MAX_PATH);
        if (homeDriveLength > 0 && homeDriveLength < MAX_PATH && homePathLength > 0 && homePathLength < MAX_PATH &&
            homeDriveValue[0] != L'\0' && homePathValue[0] != L'\0') {
            paths.push_back(NormalizeShellPath(std::wstring(homeDriveValue) + homePathValue));
        }
    }

    return paths;
}

std::wstring ResolveComparableShellPath(const wchar_t* name)
{
    if (!name || name[0] == L'\0') {
        return std::wstring();
    }

    GUID parsedGuid{};
    if (SUCCEEDED(CLSIDFromString(const_cast<LPOLESTR>(name), &parsedGuid)) ||
        wcsncmp(name, L"::", 2) == 0 ||
        _wcsnicmp(name, L"shell:", 6) == 0) {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHParseDisplayName(name, nullptr, &pidl, 0, nullptr)) && pidl) {
            wil::unique_cotaskmem_string fileSystemPath;
            if (SUCCEEDED(SHGetNameFromIDList(pidl, SIGDN_FILESYSPATH, wil::out_param(fileSystemPath))) && fileSystemPath) {
                CoTaskMemFree(pidl);
                return NormalizeShellPath(fileSystemPath.get());
            }
            CoTaskMemFree(pidl);
        }
    }

    return NormalizeShellPath(name);
}

bool IsCurrentUserFolderPath(const wchar_t* name)
{
    if (!name || name[0] == L'\0') {
        return false;
    }

    const std::vector<std::wstring> currentUserFolderPaths = CurrentUserFolderPaths();
    if (currentUserFolderPaths.empty()) {
        return false;
    }

    const std::wstring normalizedPath = ResolveComparableShellPath(name);
    for (const std::wstring& candidatePath : currentUserFolderPaths) {
        if (_wcsicmp(normalizedPath.c_str(), candidatePath.c_str()) == 0) {
            return true;
        }
    }

    return false;
}

bool FillTargetNamesFromPidl(PCIDLIST_ABSOLUTE pidl, std::wstring& targetParsingPath,
                             std::wstring& targetDisplayName)
{
    if (!pidl) {
        return false;
    }

    // Try multiple SIGDN formats to handle both filesystem and virtual folders
    // SIGDN_DESKTOPABSOLUTEPARSING works for most cases but may fail for virtual folders
    // SIGDN_FILESYSPATH works for filesystem items
    // SIGDN_PARENTRELATIVEFORADDRESSBAR works for some shell namespace items

    // First try DESKTOPABSOLUTEPARSING (works for most shortcuts)
    wil::unique_cotaskmem_string parsingName;
    HRESULT hr = SHGetNameFromIDList(pidl, SIGDN_DESKTOPABSOLUTEPARSING, wil::out_param(parsingName));
    
    // If that fails, try FILESYSPATH for filesystem items
    if (FAILED(hr) || !parsingName) {
        wil::unique_cotaskmem_string fsPath;
        hr = SHGetNameFromIDList(pidl, SIGDN_FILESYSPATH, wil::out_param(fsPath));
        if (SUCCEEDED(hr) && fsPath) {
            parsingName = std::move(fsPath);
        }
    }

    // If still no parsing name, try PARENTRELATIVEFORADDRESSBAR
    if (!parsingName) {
        wil::unique_cotaskmem_string parentRelPath;
        hr = SHGetNameFromIDList(pidl, SIGDN_PARENTRELATIVEFORADDRESSBAR, wil::out_param(parentRelPath));
        if (SUCCEEDED(hr) && parentRelPath) {
            parsingName = std::move(parentRelPath);
        }
    }

    if (parsingName) {
        targetParsingPath = parsingName.get();
    }

    wil::unique_cotaskmem_string displayName;
    if (SUCCEEDED(SHGetNameFromIDList(pidl, SIGDN_NORMALDISPLAY, wil::out_param(displayName))) && displayName) {
        targetDisplayName = displayName.get();
    }

    return !targetParsingPath.empty() || !targetDisplayName.empty();
}

bool TryReadShortcutTargetFromFile(const wchar_t* shortcutPath, std::wstring& targetParsingPath,
                                   std::wstring& targetDisplayName)
{
    if (!shortcutPath) {
        return false;
    }

    FILE* file = _wfopen(shortcutPath, L"rb");
    if (!file) {
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }

    const long fileSize = ftell(file);
    if (fileSize <= 0) {
        fclose(file);
        return false;
    }

    rewind(file);
    std::vector<char> bytes(static_cast<size_t>(fileSize));
    const size_t readSize = fread(bytes.data(), 1, bytes.size(), file);
    fclose(file);
    if (readSize != bytes.size()) {
        return false;
    }

    if (bytes.size() < kShellLinkHeaderSize + sizeof(WORD)) {
        return false;
    }

    const DWORD linkFlags = *reinterpret_cast<const DWORD*>(bytes.data() + 0x14);
    if ((linkFlags & kHasLinkTargetIDListFlag) == 0) {
        return false;
    }

    const WORD idListSize = *reinterpret_cast<const WORD*>(bytes.data() + kShellLinkHeaderSize);
    const size_t idListOffset = kShellLinkHeaderSize + sizeof(WORD);
    if (idListSize == 0 || bytes.size() < idListOffset + idListSize) {
        return false;
    }

    unique_pidl_absolute pidl(reinterpret_cast<PIDLIST_ABSOLUTE>(CoTaskMemAlloc(idListSize)));
    if (!pidl) {
        return false;
    }

    memcpy(pidl.get(), bytes.data() + idListOffset, idListSize);
    return FillTargetNamesFromPidl(pidl.get(), targetParsingPath, targetDisplayName);
}

bool ResolveShortcutTarget(const wchar_t* shortcutPath, std::wstring& targetParsingPath,
                           std::wstring& targetDisplayName)
{
    if (!shortcutPath) {
        return false;
    }
    
    std::wstring pathStr(shortcutPath);
    std::wstring extension;
    size_t dotPos = pathStr.find_last_of(L"./\\");
    if (dotPos != std::wstring::npos && dotPos + 1 < pathStr.length()) {
        extension = pathStr.substr(dotPos + 1);
        for (auto& c : extension) c = towlower(c);
    }
    if (extension != L"lnk") {
        qDebug() << "[ResolveShortcutTarget] Skipping non-lnk file:" << QString::fromWCharArray(shortcutPath) << "(ext:" << QString::fromStdWString(extension) << ")";
        return false;
    }

    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninit = SUCCEEDED(initHr);

    wil::com_ptr_nothrow<IShellLinkW> shellLink;
    wil::com_ptr_nothrow<IPersistFile> persistFile;
    unique_pidl_absolute targetPidl;
    bool resolved = false;

    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&shellLink));
    if (FAILED(hr) || !shellLink) {
        qWarning() << "[ResolveShortcutTarget] CoCreateInstance failed: hr=" << QString("0x%1").arg(hr, 8, 16, QChar('0'));
        if (shouldUninit) CoUninitialize();
        return false;
    }

    hr = shellLink->QueryInterface(IID_PPV_ARGS(&persistFile));
    if (FAILED(hr) || !persistFile) {
        qWarning() << "[ResolveShortcutTarget] QueryInterface(IPersistFile) failed: hr=" << QString("0x%1").arg(hr, 8, 16, QChar('0'));
        if (shouldUninit) CoUninitialize();
        return false;
    }

    hr = persistFile->Load(shortcutPath, STGM_READ);
    if (FAILED(hr)) {
        qWarning() << "[ResolveShortcutTarget] IPersistFile::Load failed: hr=" << QString("0x%1").arg(hr, 8, 16, QChar('0')) << "path=" << QString::fromWCharArray(shortcutPath);
        if (shouldUninit) CoUninitialize();
        return false;
    }

    hr = shellLink->GetIDList(wil::out_param(targetPidl));
    if (SUCCEEDED(hr) && targetPidl) {
        resolved = FillTargetNamesFromPidl(targetPidl.get(), targetParsingPath, targetDisplayName);
    }

    if (!resolved) {
        WIN32_FIND_DATAW findData{};
        wchar_t resolvedPath[MAX_PATH] = { 0 };
        hr = shellLink->GetPath(resolvedPath, MAX_PATH, &findData, SLGP_RAWPATH);
        if (SUCCEEDED(hr) && resolvedPath[0] != L'\0') {
            targetParsingPath = resolvedPath;
            targetDisplayName = PathFindFileNameW(resolvedPath);
            resolved = true;
            qDebug() << "[ResolveShortcutTarget] GetPath succeeded:" << QString::fromWCharArray(shortcutPath) << "->" << QString::fromStdWString(targetParsingPath);
        }
    }

    if (!resolved) {
        resolved = TryReadShortcutTargetFromFile(shortcutPath, targetParsingPath, targetDisplayName);
        if (resolved) {
            qDebug() << "[ResolveShortcutTarget] TryReadShortcutTargetFromFile succeeded:" << QString::fromWCharArray(shortcutPath) << "->" << QString::fromStdWString(targetParsingPath);
        }
    }

    if (!resolved) {
        qWarning() << "[ResolveShortcutTarget] All methods failed for:" << QString::fromWCharArray(shortcutPath);
    }

    if (shouldUninit) {
        CoUninitialize();
    }
    return resolved;
}
}

// 闂堟瑦鈧焦鍨氶崨妯哄綁闁插繐鐣炬稊澶涚礄韫囧懘銆忛崷銊ц婢舵牠鍎存潻娑滎攽閿?
HWND CSystemApi::global_SysListView32 = nullptr;
HWND CSystemApi::global_realDesktop = nullptr;
//reference
//https://github.com/Puellaquae/Desktop-Snake
void CSystemApi::GetSpecifyItemString(HANDLE MMEListViewProcess, HWND hwnd, int row, int column, TCHAR* buffer, int bufferSize)
{
    if (!MMEListViewProcess || !hwnd || !buffer || bufferSize <= 0) {
        return;
    }
    buffer[0] = 0;

    LVITEM curItem{};
    curItem.cchTextMax = bufferSize;
    curItem.iSubItem = column;

    LVITEM* tempItem = (LVITEM*)VirtualAllocEx(MMEListViewProcess, NULL, sizeof(LVITEM), MEM_COMMIT, PAGE_READWRITE);
    TCHAR* stringBuffer = (TCHAR*)VirtualAllocEx(MMEListViewProcess, NULL, bufferSize, MEM_COMMIT, PAGE_READWRITE);
    if (tempItem != nullptr && stringBuffer != nullptr) {
        curItem.pszText = (LPTSTR)stringBuffer;
        if (WriteProcessMemory(MMEListViewProcess, tempItem, &curItem, sizeof(LVITEM), nullptr)) {
            bool res = SendMessage(hwnd, LVM_GETITEMTEXTW, row, (LPARAM)tempItem);
            if (res) {
                // Read the text from the remote process
                if (ReadProcessMemory(MMEListViewProcess, stringBuffer, buffer, bufferSize, NULL)) {
                    // Read successful, 'buffer' now holds the retrieved text
                }
                else {
                    qWarning() << "Failed to read memory";
                }
            }
            else {
                qWarning() << "SendMessage failed";
                // Handle the error accordingly
            }
        }
    }
    if (tempItem) {
        VirtualFreeEx(MMEListViewProcess, tempItem, 0, MEM_RELEASE);
    }
    if (stringBuffer) {
        VirtualFreeEx(MMEListViewProcess, stringBuffer, 0, MEM_RELEASE);
    }
}



// Helper function to convert wide string (wchar_t*) to UTF-8 std::string
std::string WideToUtf8(const wchar_t* wideStr)
{
    if (!wideStr) return std::string();

    // 鐠侊紕鐣?UTF-8 缂傛挸鍟块崠鍝勩亣鐏?
    int utf8_length = WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, nullptr, 0, nullptr, nullptr);
    if (utf8_length == 0) return std::string();

    // 閸掓稑缂?std::string 楠炶泛鍨庨柊宥団敄闂?
    std::string utf8_string(utf8_length - 1, 0); // 娑撳秹娓剁憰浣规汞鐏?\0

    // 閹笛嗩攽鏉烆剚宕?
    WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, utf8_string.data(), utf8_length, nullptr, nullptr);

    return utf8_string;
}

CSystemApi::CSystemApi()
{
#ifdef _DEBUG
    qDebug() << "SystemApi_DLL_Init";
#endif
}

CSystemApi::~CSystemApi()
{
}

const wchar_t* L_MY_COMPUTER(TEXT(MY_COMPUTER_CLS));
const wchar_t* L_RECYCLE_BIN(TEXT(RECYCLE_BIN_CLS));
const wchar_t* L_CONTROL_PANEL(TEXT(CONTROL_PANEL_CLS));
const wchar_t* L_CONTROL_PANEL2(TEXT(CONTROL_PANEL_CLS2));
const wchar_t* L_NETWORK(TEXT(NETWORK_CLS));
const wchar_t* L_USER_FOLDER(TEXT(USER_FOLDER_CLS));
const wchar_t* L_LIARIES(TEXT(LIBRARIES_CLS));
const wchar_t* L_LINUX(TEXT(LINUX_CLS));
const wchar_t* L_ONE_DRIVE(TEXT(ONE_DRIVE_CLS));
bool TryExtractShellGuid(const wchar_t* name, GUID& guid)
{
    if (!name || name[0] == L'\0') {
        return false;
    }

    const wchar_t* guidStart = wcschr(name, L'{');
    if (!guidStart) {
        return false;
    }

    const wchar_t* guidEnd = wcschr(guidStart, L'}');
    if (!guidEnd || guidEnd <= guidStart) {
        return false;
    }

    std::wstring guidText(guidStart, guidEnd - guidStart + 1);
    return SUCCEEDED(CLSIDFromString(const_cast<LPOLESTR>(guidText.c_str()), &guid));
}

SpecialFolders ResolveShellSpecialFolderType(const wchar_t* name)
{
    GUID parsedGuid{};
    if (TryExtractShellGuid(name, parsedGuid)) {
        if (IsEqualGUID(parsedGuid, MyComputerGUID)) {
            return SpecialFolders::ThisPc;
        }
        if (IsEqualGUID(parsedGuid, RecycleBinGUID)) {
            return SpecialFolders::RecycleBin;
        }
        if (IsEqualGUID(parsedGuid, ControlPanelGUID)) {
            return SpecialFolders::ControlPanel;
        }
        if (IsEqualGUID(parsedGuid, ControlPanel2GUID)) {
            return SpecialFolders::ControlPanel2;
        }
        if (IsEqualGUID(parsedGuid, NetworkGUID)) {
            return SpecialFolders::Network;
        }
        if (IsEqualGUID(parsedGuid, UserFolderGUID)) {
            return SpecialFolders::UserFolder;
        }
        if (IsEqualGUID(parsedGuid, LibrariesGUID)) {
            return SpecialFolders::Libraries;
        }
        if (IsEqualGUID(parsedGuid, LinuxGUID)) {
            return SpecialFolders::Linux;
        }
    }

    if (wcsstr(name, L"OneDrive") != nullptr ||
        wcsstr(name, L"Microsoft OneDrive") != nullptr ||
        wcsstr(name, L"ONEDRIVE") != nullptr ||
        wcsstr(name, L_ONE_DRIVE)) {
        return SpecialFolders::OneDrive;
    }

    if (IsCurrentUserFolderPath(name)) {
        return SpecialFolders::UserFolder;
    }

    return SpecialFolders::UnknownFolder;
}

SpecialFolders CSystemApi::getFolderTypeFromName(const wchar_t* name)
{
    if (!name) {
        return SpecialFolders::Empty;
    }

    GUID parsedGuid{};
    if (TryExtractShellGuid(name, parsedGuid) || _wcsnicmp(name, L"shell:", 6) == 0) {
        return ResolveShellSpecialFolderType(name);
    }

    if (IsCurrentUserFolderPath(name)) {
        return SpecialFolders::UserFolder;
    }

    return SpecialFolders::UnknownFolder;
}

IconItem CSystemApi::CreateIconItem(const wchar_t* filePath)
{
    IconItem item;
    if (!filePath || wcslen(filePath) == 0) {
        return item;
    }
    PathType pathType = CSystemApi::GetPathType(filePath);
    if (pathType == PathType::InvalidPath) {
        return item;
    }

    LPITEMIDLIST pidl = nullptr;
    HRESULT hr;
    hr = SHParseDisplayName((PCWSTR)filePath, nullptr, &pidl, 0, nullptr);
    
    item.path = Tool::WstrToStr(filePath);
    item.pidl_p = pidl;

    if (pidl) {
        PWSTR pszName = nullptr;
        if (SUCCEEDED(SHGetNameFromIDList(pidl, SIGDN_NORMALDISPLAY, &pszName))) {
            item.name = Tool::WstrToStr(pszName);
            CoTaskMemFree(pszName);
        }
    }

    if (item.name.empty()) {
        size_t lastSlash = item.path.find_last_of("\\/");
        item.name = (lastSlash != std::string::npos) ? item.path.substr(lastSlash + 1) : item.path;
    }

    item.specialFolder = getFolderTypeFromName(filePath);
    
    std::wstring shortcutTargetPath;
    std::wstring shortcutTargetDisplayName;
    if (ResolveShortcutTarget(filePath, shortcutTargetPath, shortcutTargetDisplayName)) {
        item.shortcutTargetPath = Tool::WstrToStr(shortcutTargetPath.c_str());
        item.shortcutTargetDisplayName = Tool::WstrToStr(shortcutTargetDisplayName.c_str());
        const wchar_t* targetSpecialSource = !shortcutTargetPath.empty()
            ? shortcutTargetPath.c_str()
            : (!shortcutTargetDisplayName.empty() ? shortcutTargetDisplayName.c_str() : nullptr);
        item.shortcutTargetSpecialFolder = getFolderTypeFromName(targetSpecialSource);
    }

    return item;
}
//鐎电粯澹樺宀勬桨閸ョ偓鐖ｇ粣妤€褰?
BOOL CALLBACK CSystemApi::FindSysListView32Internal(HWND hwnd, LPARAM lparam)
{
    TCHAR name[255];
    if (GetClassName(hwnd, name, 255) && lstrcmp(name, _T("WorkerW")) == 0) {
        HWND hSHELLDLL_DefView = ::FindWindowEx(hwnd, NULL, _T("SHELLDLL_DefView"), NULL);
        if (hSHELLDLL_DefView != 0) {
            global_SysListView32 = ::FindWindowEx(hSHELLDLL_DefView, NULL, _T("SysListView32"), _T("FolderView"));
            return FALSE;
        }
    }
    return TRUE;
}

//鐎电粯澹樺宀勬桨缁愭褰?
BOOL CALLBACK CSystemApi::FindRealDesktopInternal(HWND hwnd, LPARAM lparam)
{
    TCHAR name[255];
    if (GetClassName(hwnd, name, 255) && lstrcmp(name, _T("WorkerW")) == 0) {
        HWND hSHELLDLL_DefView = ::FindWindowEx(hwnd, NULL, _T("SHELLDLL_DefView"), NULL);
        if (hSHELLDLL_DefView != 0) {
            global_realDesktop = GetParent(hSHELLDLL_DefView);
            return TRUE;
        }
    }
    return TRUE;
}

HWND CSystemApi::FindSysListView32()
{
    HWND hparent = ::FindWindow(_T("progman"), _T("program manager"));
    SendMessageTimeout(hparent, 0x52c, 0, 0, 0, 100, 0);
    HWND hshelldll_defview = ::FindWindowEx(hparent, NULL, _T("shelldll_defview"), NULL);
    HWND syslistview32 = ::FindWindowEx(hshelldll_defview, NULL, _T("syslistview32"), _T("folderview"));
    //鐎电粯澹樼粭顑跨癌娑撶崄orkerw
    if (syslistview32 == 0)
    {
        EnumWindows(FindSysListView32Internal, 0);
        syslistview32 = global_SysListView32;

    }
#ifdef _DEBUG
    qDebug() << "syslistview32 hwnd:" << syslistview32;
#endif
    return syslistview32;
}

HWND CSystemApi::FindRealDesktop()
{
    HWND hparent = ::FindWindow(_T("progman"), _T("program manager"));
    SendMessageTimeout(hparent, 0x52c, 0, 0, 0, 100, 0);
    HWND hshelldll_defview = ::FindWindowEx(hparent, NULL, _T("shelldll_defview"), NULL);
    //鐎电粯澹樼粭顑跨癌娑撶崄orkerw
    if (hshelldll_defview == 0)
    {
        EnumWindows(FindRealDesktopInternal, 0);
        hshelldll_defview = global_realDesktop;
    }
    return hshelldll_defview;
}

HWND CSystemApi::FindDesktop()
{
    HWND desktopHandle = GetDesktopWindow();
    return desktopHandle;
}

vector<IconItem> CSystemApi::GetDesktopIconsItems(HWND sysListView32)
{
    vector<IconItem> iconItemList;
    TCHAR szClassName[256];
    GetClassName(sysListView32, szClassName, sizeof(szClassName));
    //_tprintf(TEXT("Class name: %s\n"), szClassName);
    DWORD processId;
    GetWindowThreadProcessId(sysListView32, &processId);
    HANDLE hProcess = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION, FALSE, processId);
    if (!hProcess) {
        return iconItemList;
    }
    LPVOID ori = VirtualAllocEx(hProcess, NULL, sizeof(POINT), MEM_COMMIT, PAGE_READWRITE);
    if (!ori) {
        CloseHandle(hProcess);
        return iconItemList;
    }
    POINT point = {};
    for (int i = 0; i < ListView_GetItemCount(sysListView32); i++)
    {
        //閸ョ偓鐖ｉ崥宥囆?
        TCHAR Object[512] = {};
        //閸ョ偓鐖ｆ担宥囩枂
        ListView_GetItemPosition(sysListView32, i, ori);
        ReadProcessMemory(hProcess, ori, &point, sizeof(POINT), NULL);
        //閼惧嘲褰囬崶鐐垼閸氬秶袨
        GetSpecifyItemString(hProcess, sysListView32, i, 0, Object, sizeof(Object));

        // 鏉╂瑩鍣烽惄瀛樺复閹?TCHAR 瑜版挻鍨?wchar_t 婢跺嫮鎮?
        const wchar_t* wideStr = Object;
        std::string utf8_string = WideToUtf8(wideStr);

        // 閸樼粯甯€閺堫偄鐔惃鍑琔LL鐎涙顑?
        //size_t length = wcslen(Object);
        // 閸掑棝鍘ょ搾鍐差檮閻ㄥ嫮鈹栭梻瀛樻降鐎涙ê鍋?UTF-8 鐎涙顑佹稉?
        //int utf8_length = WideCharToMultiByte(CP_UTF8, 0, Object, -1, nullptr, 0, nullptr, nullptr);
        //char* utf8_buffer = new char[utf8_length];
        // 閹笛嗩攽鏉烆剚宕?
        //WideCharToMultiByte(CP_UTF8, 0, Object, -1, utf8_buffer, utf8_length, nullptr, nullptr);
        //std::string utf8_string(utf8_buffer);
        // 鏉烆剚宕?
        //WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, utf8_string.data(), utf8_length, nullptr, nullptr);
        //std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        ////TCHAR缁鐎锋潪顑胯礋娑撶皧td::wstring
        //std::wstring utf16_string((wchar_t*)Object, sizeof(Object) / sizeof(wchar_t) - 1); // 閸樼粯甯€閺堫偄鐔惃鍑琔LL鐎涙顑?
        //std::string utf8_string = converter.to_bytes(utf16_string);
        
        //濞撳懐鎮婇張顐㈢啲閻ㄥ嫭澧嶉張濉?
        std::string cleanedString;
        // 閺屻儲澹樼粭顑跨娑撶審0閻ㄥ嫪缍呯純?
        size_t nullPos = utf8_string.find('\0');

        // 婵″倹鐏夐幍鎯у煂娴滃摫0閿涘苯鍨幋顏勫絿缁楊兛绔存稉鐚?娑斿澧犻惃鍕劥閸?
        if (nullPos != std::string::npos) {
            cleanedString = utf8_string.substr(0, nullPos).c_str();
        }
        else {
            // 婵″倹鐏夊▽鈩冩箒閹垫儳鍩孿0閿涘苯鍨崢鐔风摟缁楋缚瑕嗘稉宥呭綁
            cleanedString = utf8_string;
        }
        IconItem iItem;
        iItem.name = cleanedString;
        iItem.position = point;
        iconItemList.push_back(iItem);
        qDebug() << QString("Name :%1").arg(QString::fromStdString(utf8_string));
        qDebug() << QString("Item(%1) 0 Position:(%2, %3)").arg(i).arg(point.x).arg(point.y);
    }
    VirtualFreeEx(hProcess, ori, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    return iconItemList;
}

vector<IconItem> CSystemApi::GetDesktopPIDLItems()
{
	//vector<IconItem>* iconItems = new vector<IconItem>();
	vector <IconItem> iconItems;
    
    // 閹绘劕澧犻幎鎾冲絿閸欘垵顫嬮崸鎰垼 (闁俺绻冩潻婊呭殠缁嬪鏁為崗銉﹀絹閸?SysListView32)
    HWND sysListView32 = FindSysListView32();
    std::unordered_map<std::string, POINT> posMap;
    if (sysListView32) {
        vector<IconItem> visualItems = GetDesktopIconsItems(sysListView32);
        for (const auto& vi : visualItems) {
            // 鐎涙ê鍋嶆稉鍝勭毈閸愭瑤绗栭崢鑽も敄閺嶈偐娈戦崥宥呯摟閿涘瞼鏁ゆ禍搴⒛佺化濠傚爱闁?
            std::string normalizedName = vi.name;
            while(!normalizedName.empty() && (normalizedName.back() == ' ' || normalizedName.back() == '\0')) normalizedName.pop_back();
            while(!normalizedName.empty() && (normalizedName.front() == ' ')) normalizedName.erase(0, 1);
            for(auto &c : normalizedName) c = tolower(c);
            
            posMap[normalizedName] = vi.position;
        }
    }

    // 閼惧嘲褰囧宀勬桨閺傚洣娆㈡径鍦畱 PIDL
    unique_pidl_absolute pidl = Tool::GetRootPidl();

    // 閼惧嘲褰囧宀勬桨閺傚洣娆㈡径鍦畱 ShellFolder
    wil::com_ptr_nothrow<IShellFolder> deskISF;
    HRESULT hr = SHGetDesktopFolder(&deskISF);
    if (FAILED(hr)) {
        DWORD error = GetLastError();
        qWarning() << "Failed to get desktop folder interface: Error" << error;
        return iconItems;
    }
    // 閸掓稑缂撻弸姘閸ｃ劋浜掗弸姘濡楀矂娼版稉濠勬畱閹碘偓閺堝銆?
    wil::com_ptr_nothrow<IEnumIDList> enumIDList;
    /*SHCONTF_CHECKING_FOR_CHILDREN |
        SHCONTF_FOLDERS |
        SHCONTF_NONFOLDERS |
        SHCONTF_INCLUDEHIDDEN |
        SHCONTF_INIT_ON_FIRST_NEXT |
        SHCONTF_NETPRINTERSRCH |
        SHCONTF_SHAREABLE |
        SHCONTF_STORAGE |
        SHCONTF_NAVIGATION_ENUM |
        SHCONTF_FASTITEMS |
        SHCONTF_FLATLIST |
        SHCONTF_ENABLE_ASYNC |
        SHCONTF_INCLUDESUPERHIDDEN*/
    // Get desktop root PIDL for absolute combination
    unique_pidl_absolute rootPidl = Tool::GetRootPidl();
    
    hr = deskISF->EnumObjects(NULL,
        SHCONTF_FOLDERS |
        SHCONTF_NONFOLDERS |
        SHCONTF_INCLUDEHIDDEN |
        SHCONTF_INCLUDESUPERHIDDEN |
        SHCONTF_ENABLE_ASYNC
        , &enumIDList);

    if (FAILED(hr) || !enumIDList) {
        DWORD error = GetLastError();
        qWarning() << "Failed to get enumeration of desktop items: Error" << error;
        return iconItems;
    }

    // 閺嬫矮濡囧В蹇庨嚋閺傚洣娆㈡い?
    LPITEMIDLIST pidlItem = nullptr;  // 娑撳瓨妞傜€涙ê鍋峆IDL

    while (enumIDList->Next(1, &pidlItem, NULL) == S_OK) {
        // 闁俺绻冮弲楦垮厴閹稿洭鎷℃担璺ㄦ暏 pidlItem
        STRRET strret;
        IconItem i;
        wchar_t szPath[MAX_PATH];
        wchar_t szName[MAX_PATH];
        wchar_t szParsingPath[MAX_PATH];
        
        // 閼惧嘲褰囬崥宥囆為敍鍦獺GDN_NORMAL閿?
        hr = deskISF->GetDisplayNameOf(pidlItem, SHGDN_NORMAL, &strret);
        if (SUCCEEDED(hr)) {
            StrRetToBufW(&strret, pidlItem, szName, MAX_PATH);
            i.name = Tool::WstrToStr(szName);
        }
        else {
            DWORD error = GetLastError();
            qWarning() << "Failed to get name: Error" << error;
        }

        // 閼惧嘲褰囩憴锝嗙€界捄顖氱窞閿涘湯HGDN_FORPARSING閿?
        hr = deskISF->GetDisplayNameOf(pidlItem, SHGDN_FORPARSING, &strret);
        if (SUCCEEDED(hr)) {
            StrRetToBufW(&strret, pidlItem, szParsingPath, MAX_PATH);
            if (Tool::WstrToStr(szName) == Tool::getWindowsUsername()) {
                i.path = "::{59031a47-3f72-44a7-89c5-5595fe6b30ee}";
            }else {
                i.path = Tool::WstrToStr(szParsingPath);
            }
        }
        else {
            DWORD error = GetLastError();
        }

        // 閼惧嘲褰囩€圭偤妾弬鍥︽缁崵绮虹捄顖氱窞閿涘牏鏁ゆ禍搴″灲閺傤厽妲搁崥锔胯礋 .lnk 閺傚洣娆㈤敍?
        hr = deskISF->GetDisplayNameOf(pidlItem, SHGDN_NORMAL | SHGDN_INFOLDER, &strret);
        if (SUCCEEDED(hr)) {
            StrRetToBufW(&strret, pidlItem, szPath, MAX_PATH);
        }

        std::wstring shortcutTargetPath;
        std::wstring shortcutTargetDisplayName;
        // 娴ｈ法鏁?szParsingPath閿?lnk 閺傚洣娆㈤惃鍕卜鐎电鐭惧鍕剁礆閺夈儴袙閺嬫劕鎻╅幑閿嬫煙瀵繒娲伴弽?
        if (ResolveShortcutTarget(szParsingPath, shortcutTargetPath, shortcutTargetDisplayName)) {
            i.shortcutTargetPath = Tool::WstrToStr(shortcutTargetPath.c_str());
            i.shortcutTargetDisplayName = Tool::WstrToStr(shortcutTargetDisplayName.c_str());
            const wchar_t* targetSpecialSource = !shortcutTargetPath.empty()
                ? shortcutTargetPath.c_str()
                : (!shortcutTargetDisplayName.empty() ? shortcutTargetDisplayName.c_str() : nullptr);
            i.shortcutTargetSpecialFolder = getFolderTypeFromName(targetSpecialSource);
        }

		// 閸掓稑缂撶紒婵嗩嚠 PIDL 娴犮儲鏁幐渚€鐝拹銊╁櫤閸ョ偓鐖ｉ幓鎰絿閸滃苯鍙炬禒?Shell 閹垮秳缍?
        if (rootPidl) {
            i.pidl_p = ILCombine(rootPidl.get(), pidlItem);
        } else {
            i.pidl_p = ILClone(pidlItem);
        }
        ILFree(pidlItem); // 闁插﹥鏂?EnumIDList 鏉╂柨娲栭惃鍕祲鐎?PIDL

        if (i.pidl_p && Tool::WstrToStr(szName) != Tool::getWindowsUsername()) {
            std::wstring resolvedItemPath;
            std::wstring resolvedItemDisplayName;
            if (FillTargetNamesFromPidl(i.pidl_p, resolvedItemPath, resolvedItemDisplayName) && !resolvedItemPath.empty()) {
                i.path = Tool::WstrToStr(resolvedItemPath.c_str());
            }
        }

		i.specialFolder = getFolderTypeFromName(IsCurrentUserFolderPath(szParsingPath) ? L_USER_FOLDER : szParsingPath);
        
        // 鐏忔繆鐦禒搴″讲鐟欏棙濮勯崣鏍ㄦ殶缂佸嫪鑵戦崠褰掑帳閸ф劖鐖?(閸氬本鐗辨潻娑滎攽瑜版帊绔撮崠鏍ь槱閻?
        std::string searchName = i.name;
        while(!searchName.empty() && (searchName.back() == ' ' || searchName.back() == '\0')) searchName.pop_back();
        while(!searchName.empty() && (searchName.front() == ' ')) searchName.erase(0, 1);
        for(auto &c : searchName) c = tolower(c);

        if (posMap.count(searchName)) {
            i.position = posMap[searchName];
        }

        iconItems.push_back(i);
#ifndef NDEBUG
		//std::wcout << L"\nName: " << szName << L", Path: " << szPath << std::endl;
#endif
        
    }
    // 闁插﹥鏂侀弸姘閸?
    return iconItems;
}

void CSystemApi::GetDesktopConfigFlags(bool& outAutoArrange, bool& outSnapToGrid)
{
    // Default fallback values (Switch to false if detection fails to avoid intrusive snapping)
    outAutoArrange = false;
    outSnapToGrid = false;
    
    // 1. First choice: Check the actual active Desktop window style (Most high-fidelity)
    HWND sysListView = FindSysListView32();
    if (sysListView) {
        DWORD style = GetWindowLong(sysListView, GWL_STYLE);
        outAutoArrange = (style & LVS_AUTOARRANGE) != 0;

        // LVM_GETEXTENDEDLISTVIEWSTYLE is the direct property for "Snap to grid"
        DWORD exStyle = (DWORD)SendMessage(sysListView, LVM_GETEXTENDEDLISTVIEWSTYLE, 0, 0);
        outSnapToGrid = (exStyle & LVS_EX_SNAPTOGRID) != 0;

        qDebug() << "[SystemApi] Found Desktop Window. AutoArrange=" << outAutoArrange << "SnapToGrid=" << outSnapToGrid;
        return; // Found live state, no need for registry backup
    }

    // 2. Second choice: Registry (Fall back here if Desktop window is not found / startup)
    const wchar_t* regPaths[] = {
        L"Software\\Microsoft\\Windows\\Shell\\Bags\\1\\Desktop",
        L"Software\\Microsoft\\Windows\\Shell\\Bags\\AllFolders\\Shell\\Desktop",
        L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\Shell\\Bags\\AllFolders\\Shell\\Desktop"
    };

    for (auto path : regPaths) {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            DWORD fFlags = 0;
            DWORD size = sizeof(fFlags);
            if (RegQueryValueExW(hKey, L"FFlags", nullptr, nullptr, (LPBYTE)&fFlags, &size) == ERROR_SUCCESS) {
                // FFlags mask: 0x01 = Auto Arrange, 0x04 = Snap to Grid
                outAutoArrange = (fFlags & 0x01) != 0;
                outSnapToGrid = (fFlags & 0x04) != 0;
                RegCloseKey(hKey);
                qDebug() << "[SystemApi] Registry Path Success (" << QString::fromWCharArray(path) << "): AutoArrange=" << outAutoArrange << "SnapToGrid=" << outSnapToGrid;
                return;
            }
            RegCloseKey(hKey);
        }
    }
}


const char* CSystemApi::GetTextFromDll(const char* filePath,int index)
{
    if (!filePath || filePath[0] == '\0') {
        return nullptr;
    }

    HMODULE hModule = LoadLibraryA(filePath);
    if (hModule == NULL) {
        DWORD error = GetLastError();
        qWarning() << "Failed to load DLL:" << filePath << "Error:" << error;
        return nullptr;
    }

    // 娴ｈ法鏁?LoadString 娴?DLL 娑擃厼濮炴潪鑺ュ瘹鐎规氨鍌ㄥ鏇犳畱鐎涙顑佹稉鑼剁カ濠?
    char* strBuffer = new char[MAX_PATH]; // 閸嬪洩顔?MAX_LOADSTRING 閺勵垵鍐绘径鐔枫亣閻ㄥ嫮绱﹂崘鎻掑隘婢堆冪毈
    int result = LoadStringA(hModule, index, strBuffer, MAX_PATH);
    if (result == 0) {
        DWORD error = GetLastError();
        qWarning() << "Failed to load string from DLL:" << filePath << "Index:" << index << "Error:" << error;
        FreeLibrary(hModule);
        delete[] strBuffer;
        return nullptr;
    }

    // 鐎涙顑佹稉鎻掑鏉炶姤鍨氶崝鐕傜礉绾喕绻氱€涙顑佹稉韫簰 null 缂佹挸鐔敍鍦爋adString 閸欘垵鍏橀張顏勶綖閸忓懏鏆ｆ稉顏嗙处閸愭彃灏敍?
    strBuffer[result] = '\0';

    // 闁插﹥鏂?DLL閿涘苯鑻熸潻鏂挎礀閸旂姾娴囬惃鍕摟缁楋缚瑕?
    FreeLibrary(hModule);
    return strBuffer;
}

const char* CSystemApi::GetTextFromDll(const char* input)
{
    if (!input || input[0] == '\0') {
        return nullptr;
    }
    
    const char* str = nullptr;
    ResourceInfo ri = Tool::GetResourceInfo(input);
    if (&ri) {
        str = CSystemApi::GetTextFromDll(ri.path.c_str(), ri.index);
    }
    return str;
}

bool CSystemApi::IsLinkFile(const wchar_t* filePath)
{
    DWORD attributes = GetFileAttributes(filePath);
    if (attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
        !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        //std::cout << "鐠囥儴鐭惧鍕讲閼宠姤妲歌箛顐ｅ祹閺傜懓绱￠妴? << std::endl;
        return true;
    }
    return false;
}

/**
 * 閸掔娀娅庨弬鍥︽閻ㄥ嫬甯悽鐑漃I閸戣姤鏆?
*/
bool CSystemApi::DeleteFileNative(const wchar_t* filePath)
{
    if (::DeleteFile(filePath) == TRUE)
    {
        // 閺傚洣娆㈤崚鐘绘珟閹存劕濮?
        return true;
    }
    else
    {
        // 閼惧嘲褰囬柨娆掝嚖娴狅絿鐖?
        DWORD error = GetLastError();
        // 婢跺嫮鎮婇柨娆掝嚖閿涘奔绶ユ俊鍌濈翻閸戞椽鏁婄拠顖欎繆閹垰鍩岀拫鍐槸閸ｃ劍鍨ㄩ弮銉ョ箶
        SetLastError(error);
        return false;
    }
}

bool CSystemApi::DeleteFiles(const std::vector<std::wstring>& filePaths, bool toRecycleBin, bool confirm)
{
    if (filePaths.empty()) return true;

    // SHFileOperation 閻?pFrom 鐟曚焦鐪版禒銉ュ蓟 null 缂佹挸鐔敍灞藉敶闁劌鎮囩捄顖氱窞娴犮儱宕?null 閸掑棝娈?
    std::vector<wchar_t> pathsBuffer;
    for (const auto& path : filePaths) {
        for (wchar_t c : path) pathsBuffer.push_back(c);
        pathsBuffer.push_back(0); // 閸?null 閸掑棝娈?
    }
    pathsBuffer.push_back(0); // 閸?null 缂佹挸鐔?

    SHFILEOPSTRUCTW fileOp = { 0 };
    fileOp.hwnd = GetForegroundWindow();
    fileOp.wFunc = FO_DELETE;
    fileOp.pFrom = pathsBuffer.data();
    
    fileOp.fFlags = 0;
    if (toRecycleBin) fileOp.fFlags |= FOF_ALLOWUNDO;
    if (!toRecycleBin) fileOp.fFlags |= FOF_WANTNUKEWARNING;
    if (!confirm) fileOp.fFlags |= FOF_NOCONFIRMATION;

    int result = SHFileOperationW(&fileOp);
    return (result == 0) && !fileOp.fAnyOperationsAborted;
}

bool CSystemApi::CopyFiles(const std::vector<std::wstring>& filePaths, const std::wstring& destDir)
{
    if (filePaths.empty()) return true;

    std::vector<wchar_t> srcBuffer;
    for (const auto& path : filePaths) {
        for (wchar_t c : path) srcBuffer.push_back(c);
        srcBuffer.push_back(0);
    }
    srcBuffer.push_back(0);

    std::vector<wchar_t> destBuffer;
    for (wchar_t c : destDir) destBuffer.push_back(c);
    destBuffer.push_back(0);
    destBuffer.push_back(0);

    SHFILEOPSTRUCTW fileOp = { 0 };
    fileOp.wFunc = FO_COPY;
    fileOp.pFrom = srcBuffer.data();
    fileOp.pTo = destBuffer.data();
    fileOp.fFlags = FOF_ALLOWUNDO;

    return (SHFileOperationW(&fileOp) == 0);
}

bool CSystemApi::MoveFiles(const std::vector<std::wstring>& filePaths, const std::wstring& destDir)
{
    if (filePaths.empty()) return true;
    if (destDir.empty()) return false;

    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE) {
        return false;
    }
    const bool shouldUninitialize = SUCCEEDED(initHr);

    wil::com_ptr_nothrow<IFileOperation> fileOperation;
    HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&fileOperation));
    if (FAILED(hr) || !fileOperation) {
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return false;
    }

    hr = fileOperation->SetOperationFlags(FOF_ALLOWUNDO | FOF_NOCONFIRMMKDIR);
    if (FAILED(hr)) {
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return false;
    }

    wil::com_ptr_nothrow<IShellItem> destinationItem;
    hr = SHCreateItemFromParsingName(destDir.c_str(), nullptr, IID_PPV_ARGS(&destinationItem));
    if (FAILED(hr) || !destinationItem) {
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return false;
    }

    for (const auto& path : filePaths) {
        if (path.empty()) {
            continue;
        }

        wil::com_ptr_nothrow<IShellItem> sourceItem;
        hr = SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&sourceItem));
        if (FAILED(hr) || !sourceItem) {
            if (shouldUninitialize) {
                CoUninitialize();
            }
            return false;
        }

        hr = fileOperation->MoveItem(sourceItem.get(), destinationItem.get(), nullptr, nullptr);
        if (FAILED(hr)) {
            if (shouldUninitialize) {
                CoUninitialize();
            }
            return false;
        }
    }

    hr = fileOperation->PerformOperations();
    if (FAILED(hr)) {
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return false;
    }

    BOOL operationsAborted = FALSE;
    hr = fileOperation->GetAnyOperationsAborted(&operationsAborted);

    if (shouldUninitialize) {
        CoUninitialize();
    }

    return SUCCEEDED(hr) && !operationsAborted;
}

bool CSystemApi::DeleteFileToRecycleBin(const wchar_t* filePath, bool comfirm)
{
    SHFILEOPSTRUCTW fileOp = { 0 };
    fileOp.hwnd = NULL; // 閸欘垯浜掗幐鍥х暰娑撯偓娑擃亞鐛ラ崣锝呭綖閺屽嫮鏁ゆ禍搴㈡▔缁€楦跨箻鎼达附鍨ㄩ柨娆掝嚖娣団剝浼?
    fileOp.wFunc = FO_DELETE; // 閹稿洤鐣鹃幙宥勭稊缁鐎锋稉鍝勫灩闂?
    fileOp.pFrom = filePath; // 閹稿洤鎮滃鍛灩闂勩倖鏋冩禒鍓佹畱鐠侯垰绶為敍宀冪熅瀵板嫬鐡х粭锔胯韫囧懘銆忔禒銉ュ蓟null鐎涙顑佺紒鎾崇啲閿涘湢"...\0\0"閿?

    //閸氼垳鏁ょ涵顔款吇鐎电鐦藉?
    if (comfirm)
    {
        fileOp.fFlags = FOF_ALLOWUNDO;
    }
    else {
        fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION; // 閸忎浇顔忛弨鎯у弳閸ョ偞鏁圭粩娆忚嫙缁備胶鏁ょ涵顔款吇鐎电鐦藉?
    }
    // 閹笛嗩攽閺傚洣娆㈤幙宥勭稊
    int result = SHFileOperationW(&fileOp);
    if (result == 0)
    {
        // 閹垮秳缍旈幋鎰
        return true;
    }
    else
    {
        // 閼惧嘲褰囬柨娆掝嚖娴狅絿鐖?
        DWORD error = GetLastError();
        SetLastError(error);
        // 婢跺嫮鎮婇柨娆掝嚖
        return false;
    }
}

PathType CSystemApi::GetPathType(const wchar_t* filePath)
{
    if (!filePath || filePath[0] == L'\0') {
        return InvalidPath;
    }

    // Check for virtual Shell CLSID paths (e.g., ::{20D04FE0-3AEA-1069-A2D8-08002B30309D})
    if (filePath && wcsncmp(filePath, L"::", 2) == 0) {
        return DirectoryPath; // Virtual folders behave like directories for most Shell operations
    }

    DWORD attributes = GetFileAttributesW(filePath);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        // 閺冪姵鏅ョ捄顖氱窞
        return InvalidPath;
    }

    if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
        // 閻╊喖缍嶇捄顖氱窞
        return DirectoryPath;
    }
    else {
        // 閺傚洣娆㈢捄顖氱窞
        return FilePath;
    }
}

/**
lpVerb 閸欏倹鏆熸稉?ShellExecute 閻?lpOperation 閸欏倹鏆熸稉鈧懛杈剧窗
edit 閻劎绱潏鎴濇珤閹垫挸绱?lpFile 閹稿洤鐣鹃惃鍕瀮濡楋綇绱濇俊鍌涚亯 lpFile 娑撳秵妲搁弬鍥ㄣ€傞敍灞藉灟娴兼艾銇戠拹?
explore 濞村繗顫?lpFile 閹稿洤鐣鹃惃鍕瀮娴犺泛銇?
find 閹兼粎鍌?lpDirectory 閹稿洤鐣鹃惃鍕窗瑜?
open 閹垫挸绱?lpFile 閺傚洣娆㈤敍瀹璸File 閸欘垯浜掗弰顖涙瀮娴犺埖鍨ㄩ弬鍥︽婢?
print 閹垫挸宓?lpFile閿涘苯顩ч弸?lpFile 娑撳秵妲搁弬鍥ㄣ€傞敍灞藉灟閸戣姤鏆熸径杈Е
properties 閺勫墽銇氱仦鐐粹偓?
runas 鐠囬攱鐪版禒銉ь吀閻炲棗鎲抽弶鍐鏉╂劘顢戦敍灞剧槷婵″倷浜掔粻锛勬倞閸涙ɑ娼堥梽鎰箥鐞涘本鐓囨稉鐚爔e
NULL 閹笛嗩攽姒涙顓?open"閸斻劋缍?
**/
void CSystemApi::File_RightMenuOperations(const TCHAR* filePath, File_RightMenu_Operations operation)
{
    SHELLEXECUTEINFO shell{};
    shell.hwnd = NULL;
    switch (operation)
    {
    case Z_OPEN:
        shell.lpVerb = TEXT("open");
        break;
    case Z_EDIT:
        shell.lpVerb = TEXT("edit");
        break;
    case Z_PRINT:
        shell.lpVerb = TEXT("print");
        break;
    case Z_COPY:
        shell.lpVerb = TEXT("copy");
        break;
    case Z_CREATE_SHORTCUT:
        shell.lpVerb = TEXT("create shortcut");
        break;
    case Z_DELETE:
        shell.lpVerb = TEXT("delete");
        break;
    case Z_PROPERTIES:
        shell.lpVerb = TEXT("properties");
        break;
    default:
        return;
    }
    shell.lpFile = filePath;
    shell.lpDirectory = NULL;
    shell.lpParameters = NULL;
    shell.nShow = SW_SHOWNORMAL;
    shell.fMask = SEE_MASK_INVOKEIDLIST;
    shell.lpIDList = NULL;
    shell.cbSize = sizeof(SHELLEXECUTEINFO);
    ShellExecuteEx(&shell);
}

HBITMAP CSystemApi::GetShellThumbnail(const wchar_t* path, int width, int height)
{
    if (!path || wcslen(path) == 0) return NULL;

    wil::com_ptr_nothrow<IShellItem> shellItem;
    // Create a shell item for the given path (works for files and folders)
    HRESULT hr = SHCreateItemFromParsingName(path, nullptr, IID_PPV_ARGS(&shellItem));
    if (FAILED(hr)) return NULL;

    wil::com_ptr_nothrow<IShellItemImageFactory> imageFactory;
    hr = shellItem->QueryInterface(IID_PPV_ARGS(&imageFactory));
    if (FAILED(hr)) {
        return NULL;
    }

    SIZE size = { width, height };
    HBITMAP hBitmap = NULL;
    // GetImage parameters:
    // SIIGBF_RESIZETOFIT: Aspect ratio is maintained while fitting into the box.
    // This flag is crucial for folders as it triggers the composite content preview render.
    hr = imageFactory->GetImage(size, SIIGBF_RESIZETOFIT, &hBitmap);

    if (SUCCEEDED(hr) && hBitmap) {
        return hBitmap;
    }
    return NULL;
}
int OpenCLSID(const wchar_t* clsidStr)
{
    // 閺囨寧宕叉稉瀣桨閻?CLSID_String 鐎涙顑佹稉韫礋娴ｇ姵鍏傜憰浣瑰ⅵ瀵偓閻?CLSID 鐎电懓绨查惃鍕柤鎼村繒娈?CLSID 鐎涙顑佹稉?
    LPCWSTR CLSID_String = clsidStr;

    SHELLEXECUTEINFO sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_CLASSNAME;
    sei.lpClass = CLSID_String;
    sei.lpVerb = L"open";
    sei.nShow = SW_SHOWNORMAL;

    if (ShellExecuteEx(&sei))
    {
        // 閹笛嗩攽閹存劕濮?
        return 0;
    }
    else
    {
        // 閹笛嗩攽婢惰精瑙?
        DWORD dwError = GetLastError();
        // 婢跺嫮鎮婇柨娆掝嚖
        return dwError;
    }
    return 0;
}
//濞村鐦禒锝囩垳=========================================================
#ifdef _DEBUG

void WriteLocalFile(const vector<IconItem>* items) {
    // 閹垫挸绱戠憰浣稿晸閸忋儳娈戦弬鍥︽
    std::ofstream outputFile("items_content.txt");

    if (outputFile.is_open()) {
        // 闁秴宸婚梼鐔峰灙楠炶泛鐨㈤崘鍛啇閸愭瑥鍙嗛弬鍥︽
        for (const auto& item : *items) {
            //std::cout << item.name << std::endl;
            outputFile << item.name << std::endl;
        }
        // 閸忔娊妫撮弬鍥︽濞?
        outputFile.close();
        std::cout << "List content has been written to 'items_content.txt'." << std::endl;
    }
    else {
        std::cout << "Unable to open file for writing." << std::endl;
    }
}

#endif // DEBUG
