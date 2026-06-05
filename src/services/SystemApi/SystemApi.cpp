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
void CSystemApi::GetSpecifyItemString(HANDLE MMEListViewProcess, HWND hwnd, int row, int column, TCHAR* buffer, int bufferSizeChars)
{
    if (!MMEListViewProcess || !hwnd || !buffer || bufferSizeChars <= 0) {
        return;
    }
    buffer[0] = 0;

    LVITEM curItem{};
    curItem.cchTextMax = bufferSizeChars;
    curItem.iSubItem = column;

    const DWORD stringBufferSizeBytes = bufferSizeChars * sizeof(TCHAR);

    LVITEM* tempItem = (LVITEM*)VirtualAllocEx(MMEListViewProcess, NULL, sizeof(LVITEM), MEM_COMMIT, PAGE_READWRITE);
    TCHAR* stringBuffer = (TCHAR*)VirtualAllocEx(MMEListViewProcess, NULL, stringBufferSizeBytes, MEM_COMMIT, PAGE_READWRITE);
    if (tempItem != nullptr && stringBuffer != nullptr) {
        curItem.pszText = (LPTSTR)stringBuffer;
        if (WriteProcessMemory(MMEListViewProcess, tempItem, &curItem, sizeof(LVITEM), nullptr)) {
            bool res = SendMessage(hwnd, LVM_GETITEMTEXTW, row, (LPARAM)tempItem);
            if (res) {
                // Read the text from the remote process
                if (ReadProcessMemory(MMEListViewProcess, stringBuffer, buffer, stringBufferSizeBytes, NULL)) {
                    buffer[bufferSizeChars - 1] = 0;
                }
                else {
                    qWarning() << "Failed to read memory";
                }
            }
            else {
                qWarning() << "SendMessage failed";
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

IconItem CSystemApi::CreateIconItem(const QString& filePath)
{
    IconItem item;
    if (filePath.isEmpty()) {
        return item;
    }
    PathType pathType = CSystemApi::GetPathType(filePath);
    if (pathType == PathType::InvalidPath) {
        return item;
    }

    std::wstring wFilePath = filePath.toStdWString();
    LPITEMIDLIST pidl = nullptr;
    HRESULT hr;
    hr = SHParseDisplayName((PCWSTR)wFilePath.c_str(), nullptr, &pidl, 0, nullptr);
    
    item.path = filePath;
    item.pidl_p = pidl;

    if (pidl) {
        PWSTR pszName = nullptr;
        if (SUCCEEDED(SHGetNameFromIDList(pidl, SIGDN_NORMALDISPLAY, &pszName))) {
            item.name = QString::fromWCharArray(pszName);
            CoTaskMemFree(pszName);
        }
    }

    if (item.name.isEmpty()) {
        int lastSlash = item.path.lastIndexOf("\\");
        int lastSlash2 = item.path.lastIndexOf("/");
        int maxSlash = (std::max)(lastSlash, lastSlash2);
        item.name = (maxSlash != -1) ? item.path.mid(maxSlash + 1) : item.path;
    }

    item.specialFolder = getFolderTypeFromName(wFilePath.c_str());
    
    std::wstring shortcutTargetPath;
    std::wstring shortcutTargetDisplayName;
    if (ResolveShortcutTarget(wFilePath.c_str(), shortcutTargetPath, shortcutTargetDisplayName)) {
        item.shortcutTargetPath = QString::fromStdWString(shortcutTargetPath);
        item.shortcutTargetDisplayName = QString::fromStdWString(shortcutTargetDisplayName);
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


QVector<IconItem> CSystemApi::GetDesktopIconsItems(HWND sysListView32)
{
    QVector<IconItem> iconItemList;
    TCHAR szClassName[256];
    GetClassName(sysListView32, szClassName, sizeof(szClassName));
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
        TCHAR Object[512] = {};
        ListView_GetItemPosition(sysListView32, i, ori);
        ReadProcessMemory(hProcess, ori, &point, sizeof(POINT), NULL);
        GetSpecifyItemString(hProcess, sysListView32, i, 0, Object, sizeof(Object) / sizeof(Object[0]));

        QString qName = QString::fromWCharArray(Object);
        int nullIdx = qName.indexOf(QChar('\0'));
        if (nullIdx != -1) {
            qName = qName.left(nullIdx);
        }

        IconItem iItem;
        iItem.name = qName;
        iItem.position = QPoint(point.x, point.y);
        iconItemList.push_back(iItem);
        qDebug() << "Name :" << qName;
        qDebug() << QString("Item(%1) 0 Position:(%2, %3)").arg(i).arg(point.x).arg(point.y);
    }
    VirtualFreeEx(hProcess, ori, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    return iconItemList;
}

QVector<IconItem> CSystemApi::GetDesktopPIDLItems()
{
    QVector<IconItem> iconItems;
    
    HWND sysListView32 = FindSysListView32();
    QMap<QString, QPoint> posMap;
    if (sysListView32) {
        QVector<IconItem> visualItems = GetDesktopIconsItems(sysListView32);
        for (const auto& vi : visualItems) {
            posMap[vi.name.trimmed().toLower()] = vi.position;
        }
    }

    wil::com_ptr_nothrow<IShellFolder> deskISF;
    HRESULT hr = SHGetDesktopFolder(&deskISF);
    if (FAILED(hr)) {
        DWORD error = GetLastError();
        qWarning() << "Failed to get desktop folder interface: Error" << error;
        return iconItems;
    }
    wil::com_ptr_nothrow<IEnumIDList> enumIDList;
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

    LPITEMIDLIST pidlItem = nullptr;

    while (enumIDList->Next(1, &pidlItem, NULL) == S_OK) {
        STRRET strret;
        IconItem i;
        wchar_t szPath[MAX_PATH];
        wchar_t szName[MAX_PATH];
        wchar_t szParsingPath[MAX_PATH];
        
        hr = deskISF->GetDisplayNameOf(pidlItem, SHGDN_NORMAL, &strret);
        if (SUCCEEDED(hr)) {
            StrRetToBufW(&strret, pidlItem, szName, MAX_PATH);
            i.name = QString::fromWCharArray(szName);
        }
        else {
            DWORD error = GetLastError();
            qWarning() << "Failed to get name: Error" << error;
        }

        hr = deskISF->GetDisplayNameOf(pidlItem, SHGDN_FORPARSING, &strret);
        if (SUCCEEDED(hr)) {
            StrRetToBufW(&strret, pidlItem, szParsingPath, MAX_PATH);
            if (QString::fromWCharArray(szName) == QString::fromStdString(Tool::getWindowsUsername())) {
                i.path = "::{59031a47-3f72-44a7-89c5-5595fe6b30ee}";
            } else {
                i.path = QString::fromWCharArray(szParsingPath);
            }
        }

        hr = deskISF->GetDisplayNameOf(pidlItem, SHGDN_NORMAL | SHGDN_INFOLDER, &strret);
        if (SUCCEEDED(hr)) {
            StrRetToBufW(&strret, pidlItem, szPath, MAX_PATH);
        }

        std::wstring shortcutTargetPath;
        std::wstring shortcutTargetDisplayName;
        if (ResolveShortcutTarget(szParsingPath, shortcutTargetPath, shortcutTargetDisplayName)) {
            i.shortcutTargetPath = QString::fromStdWString(shortcutTargetPath);
            i.shortcutTargetDisplayName = QString::fromStdWString(shortcutTargetDisplayName);
            const wchar_t* targetSpecialSource = !shortcutTargetPath.empty()
                ? shortcutTargetPath.c_str()
                : (!shortcutTargetDisplayName.empty() ? shortcutTargetDisplayName.c_str() : nullptr);
            i.shortcutTargetSpecialFolder = getFolderTypeFromName(targetSpecialSource);
        }

        if (rootPidl) {
            i.pidl_p = ILCombine(rootPidl.get(), pidlItem);
        } else {
            i.pidl_p = ILClone(pidlItem);
        }
        ILFree(pidlItem);

        if (i.pidl_p && QString::fromWCharArray(szName) != QString::fromStdString(Tool::getWindowsUsername())) {
            std::wstring resolvedItemPath;
            std::wstring resolvedItemDisplayName;
            if (FillTargetNamesFromPidl(i.pidl_p, resolvedItemPath, resolvedItemDisplayName) && !resolvedItemPath.empty()) {
                i.path = QString::fromStdWString(resolvedItemPath);
            }
        }

        i.specialFolder = getFolderTypeFromName(IsCurrentUserFolderPath(szParsingPath) ? L_USER_FOLDER : szParsingPath);
        
        QString searchName = i.name.trimmed().toLower();
        if (posMap.contains(searchName)) {
            i.position = posMap[searchName];
        }

        iconItems.push_back(i);
    }
    return iconItems;
}

void CSystemApi::GetDesktopConfigFlags(bool& outAutoArrange, bool& outSnapToGrid)
{
    outAutoArrange = false;
    outSnapToGrid = false;
    
    HWND sysListView = FindSysListView32();
    if (sysListView) {
        DWORD style = GetWindowLong(sysListView, GWL_STYLE);
        outAutoArrange = (style & LVS_AUTOARRANGE) != 0;

        DWORD exStyle = (DWORD)SendMessage(sysListView, LVM_GETEXTENDEDLISTVIEWSTYLE, 0, 0);
        outSnapToGrid = (exStyle & LVS_EX_SNAPTOGRID) != 0;

        qDebug() << "[SystemApi] Found Desktop Window. AutoArrange=" << outAutoArrange << "SnapToGrid=" << outSnapToGrid;
        return;
    }

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

QString CSystemApi::GetTextFromDll(const QString& filePath, int index)
{
    if (filePath.isEmpty()) {
        return QString();
    }

    wil::unique_hmodule hModule(LoadLibraryW(filePath.toStdWString().c_str()));
    if (!hModule) {
        DWORD error = GetLastError();
        qWarning() << "Failed to load DLL:" << filePath << "Error:" << error;
        return QString();
    }

    wchar_t strBuffer[MAX_PATH];
    int result = LoadStringW(hModule.get(), index, strBuffer, MAX_PATH);
    if (result == 0) {
        DWORD error = GetLastError();
        qWarning() << "Failed to load string from DLL:" << filePath << "Index:" << index << "Error:" << error;
        return QString();
    }

    strBuffer[result] = L'\0';
    return QString::fromWCharArray(strBuffer);
}

QString CSystemApi::GetTextFromDll(const QString& input)
{
    if (input.isEmpty()) {
        return QString();
    }
    
    QString result;
    ResourceInfo ri = Tool::GetResourceInfo(input.toStdString().c_str());
    if (!ri.path.empty()) {
        result = CSystemApi::GetTextFromDll(QString::fromStdString(ri.path), ri.index);
    }
    return result;
}

bool CSystemApi::IsLinkFile(const QString& filePath)
{
    DWORD attributes = GetFileAttributesW(filePath.toStdWString().c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
        !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return true;
    }
    return false;
}

bool CSystemApi::DeleteFileNative(const QString& filePath)
{
    if (::DeleteFileW(filePath.toStdWString().c_str()) == TRUE) {
        return true;
    } else {
        DWORD error = GetLastError();
        SetLastError(error);
        return false;
    }
}

bool CSystemApi::DeleteFiles(const QStringList& filePaths, bool toRecycleBin, bool confirm)
{
    if (filePaths.isEmpty()) return true;

    std::vector<wchar_t> pathsBuffer;
    for (const auto& path : filePaths) {
        std::wstring wPath = path.toStdWString();
        for (wchar_t c : wPath) pathsBuffer.push_back(c);
        pathsBuffer.push_back(0);
    }
    pathsBuffer.push_back(0);

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

bool CSystemApi::CopyFiles(const QStringList& filePaths, const QString& destDir)
{
    if (filePaths.isEmpty()) return true;

    std::vector<wchar_t> srcBuffer;
    for (const auto& path : filePaths) {
        std::wstring wPath = path.toStdWString();
        for (wchar_t c : wPath) srcBuffer.push_back(c);
        srcBuffer.push_back(0);
    }
    srcBuffer.push_back(0);

    std::wstring wDestDir = destDir.toStdWString();
    std::vector<wchar_t> destBuffer;
    for (wchar_t c : wDestDir) destBuffer.push_back(c);
    destBuffer.push_back(0);
    destBuffer.push_back(0);

    SHFILEOPSTRUCTW fileOp = { 0 };
    fileOp.wFunc = FO_COPY;
    fileOp.pFrom = srcBuffer.data();
    fileOp.pTo = destBuffer.data();
    fileOp.fFlags = FOF_ALLOWUNDO;

    return (SHFileOperationW(&fileOp) == 0);
}

bool CSystemApi::MoveFiles(const QStringList& filePaths, const QString& destDir)
{
    if (filePaths.isEmpty()) return true;
    if (destDir.isEmpty()) return false;

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
    hr = SHCreateItemFromParsingName(destDir.toStdWString().c_str(), nullptr, IID_PPV_ARGS(&destinationItem));
    if (FAILED(hr) || !destinationItem) {
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return false;
    }

    for (const auto& path : filePaths) {
        if (path.isEmpty()) {
            continue;
        }

        wil::com_ptr_nothrow<IShellItem> sourceItem;
        hr = SHCreateItemFromParsingName(path.toStdWString().c_str(), nullptr, IID_PPV_ARGS(&sourceItem));
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

bool CSystemApi::DeleteFileToRecycleBin(const QString& filePath, bool confirm)
{
    std::wstring wPath = filePath.toStdWString();
    std::vector<wchar_t> pathsBuffer;
    for (wchar_t c : wPath) pathsBuffer.push_back(c);
    pathsBuffer.push_back(0);
    pathsBuffer.push_back(0);

    SHFILEOPSTRUCTW fileOp = { 0 };
    fileOp.hwnd = NULL;
    fileOp.wFunc = FO_DELETE;
    fileOp.pFrom = pathsBuffer.data();

    if (confirm) {
        fileOp.fFlags = FOF_ALLOWUNDO;
    } else {
        fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION;
    }
    
    int result = SHFileOperationW(&fileOp);
    if (result == 0) {
        return true;
    } else {
        DWORD error = GetLastError();
        SetLastError(error);
        return false;
    }
}

PathType CSystemApi::GetPathType(const QString& filePath)
{
    if (filePath.isEmpty()) {
        return InvalidPath;
    }

    if (filePath.startsWith("::")) {
        return DirectoryPath;
    }

    std::wstring wPath = filePath.toStdWString();
    DWORD attributes = GetFileAttributesW(wPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return InvalidPath;
    }

    if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
        return DirectoryPath;
    } else {
        return FilePath;
    }
}

void CSystemApi::File_RightMenuOperations(const QString& filePath, File_RightMenu_Operations operation)
{
    SHELLEXECUTEINFOW shell{};
    shell.hwnd = NULL;
    switch (operation)
    {
    case Z_OPEN:
        shell.lpVerb = L"open";
        break;
    case Z_EDIT:
        shell.lpVerb = L"edit";
        break;
    case Z_PRINT:
        shell.lpVerb = L"print";
        break;
    case Z_COPY:
        shell.lpVerb = L"copy";
        break;
    case Z_CREATE_SHORTCUT:
        shell.lpVerb = L"create shortcut";
        break;
    case Z_DELETE:
        shell.lpVerb = L"delete";
        break;
    case Z_PROPERTIES:
        shell.lpVerb = L"properties";
        break;
    default:
        return;
    }
    std::wstring wPath = filePath.toStdWString();
    shell.lpFile = wPath.c_str();
    shell.lpDirectory = NULL;
    shell.lpParameters = NULL;
    shell.nShow = SW_SHOWNORMAL;
    shell.fMask = SEE_MASK_INVOKEIDLIST;
    shell.lpIDList = NULL;
    shell.cbSize = sizeof(SHELLEXECUTEINFOW);
    ShellExecuteExW(&shell);
}

HBITMAP CSystemApi::GetShellThumbnail(const QString& path, int width, int height)
{
    if (path.isEmpty()) return NULL;

    wil::com_ptr_nothrow<IShellItem> shellItem;
    HRESULT hr = SHCreateItemFromParsingName(path.toStdWString().c_str(), nullptr, IID_PPV_ARGS(&shellItem));
    if (FAILED(hr)) return NULL;

    wil::com_ptr_nothrow<IShellItemImageFactory> imageFactory;
    hr = shellItem->QueryInterface(IID_PPV_ARGS(&imageFactory));
    if (FAILED(hr)) {
        return NULL;
    }

    SIZE size = { width, height };
    HBITMAP hBitmap = NULL;
    hr = imageFactory->GetImage(size, SIIGBF_RESIZETOFIT, &hBitmap);

    if (SUCCEEDED(hr) && hBitmap) {
        return hBitmap;
    }
    return NULL;
}

int OpenCLSID(const QString& clsidStr)
{
    std::wstring wClsid = clsidStr.toStdWString();
    LPCWSTR CLSID_String = wClsid.c_str();

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_CLASSNAME;
    sei.lpClass = CLSID_String;
    sei.lpVerb = L"open";
    sei.nShow = SW_SHOWNORMAL;

    if (ShellExecuteExW(&sei))
    {
        return 0;
    }
    else
    {
        DWORD dwError = GetLastError();
        return dwError;
    }
    return 0;
}

#ifdef _DEBUG
void WriteLocalFile(const QVector<IconItem>* items) {
    std::ofstream outputFile("items_content.txt");

    if (outputFile.is_open()) {
        for (const auto& item : *items) {
            outputFile << item.name.toStdString() << std::endl;
        }
        outputFile.close();
        std::cout << "List content has been written to 'items_content.txt'." << std::endl;
    }
    else {
        std::cout << "Unable to open file for writing." << std::endl;
    }
}
#endif // DEBUG
