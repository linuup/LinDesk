// SystemApi.cpp : Defines the exported functions for the DLL.
#include "SystemApi.h"
#include "Tool.h"
#include <Shlwapi.h>
#include <fstream>
#include <cstdio>

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
            ch = static_cast<wchar_t>(towlower(ch));
        }
    }

    while (path.length() > 3 && (path.back() == L'\\' || path.back() == L'/')) {
        path.pop_back();
    }

    return path;
}

std::wstring CurrentUserProfilePath()
{
    wchar_t username[256] = { 0 };
    DWORD usernameLength = static_cast<DWORD>(sizeof(username) / sizeof(username[0]));
    std::wstring currentUsername;
    if (GetUserNameW(username, &usernameLength) && username[0] != L'\0') {
        currentUsername.assign(username);
    }

    wil::unique_cotaskmem_string profilePath;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, wil::out_param(profilePath))) && profilePath) {
        std::wstring resolvedProfilePath = profilePath.get();
        if (!currentUsername.empty()) {
            const size_t lastSlash = resolvedProfilePath.find_last_of(L"\\/");
            const std::wstring leafName = (lastSlash == std::wstring::npos)
                ? resolvedProfilePath
                : resolvedProfilePath.substr(lastSlash + 1);
            if (_wcsicmp(leafName.c_str(), currentUsername.c_str()) == 0) {
                return NormalizeShellPath(resolvedProfilePath);
            }
        }
        return NormalizeShellPath(resolvedProfilePath);
    }

    if (!currentUsername.empty()) {
        wchar_t systemDrive[MAX_PATH] = { 0 };
        DWORD driveLength = GetEnvironmentVariableW(L"SystemDrive", systemDrive, MAX_PATH);
        std::wstring baseDrive = (driveLength > 0 && systemDrive[0] != L'\0') ? systemDrive : L"C:";
        return NormalizeShellPath(baseDrive + L"\\Users\\" + currentUsername);
    }

    return std::wstring();
}

bool IsCurrentUserFolderPath(const wchar_t* name)
{
    if (!name || name[0] == L'\0') {
        return false;
    }

    static const std::wstring kCurrentUserProfilePath = CurrentUserProfilePath();
    if (kCurrentUserProfilePath.empty()) {
        return false;
    }

    return NormalizeShellPath(name) == kCurrentUserProfilePath;
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
    
    // 检查扩展名（支持大小写�?
    std::wstring pathStr(shortcutPath);
    std::wstring extension;
    size_t dotPos = pathStr.find_last_of(L"./\\");
    if (dotPos != std::wstring::npos && dotPos + 1 < pathStr.length()) {
        extension = pathStr.substr(dotPos + 1);
        for (auto& c : extension) c = towlower(c);
    }
    if (extension != L"lnk") {
#ifndef NDEBUG
        wprintf(L"[ResolveShortcutTarget] Skipping non-lnk file: %s (ext: %s)\n", shortcutPath, extension.c_str());
#endif
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
#ifndef NDEBUG
        wprintf(L"[ResolveShortcutTarget] CoCreateInstance failed: hr=0x%08X\n", hr);
#endif
        if (shouldUninit) CoUninitialize();
        return false;
    }

    hr = shellLink->QueryInterface(IID_PPV_ARGS(&persistFile));
    if (FAILED(hr) || !persistFile) {
#ifndef NDEBUG
        wprintf(L"[ResolveShortcutTarget] QueryInterface(IPersistFile) failed: hr=0x%08X\n", hr);
#endif
        if (shouldUninit) CoUninitialize();
        return false;
    }

    hr = persistFile->Load(shortcutPath, STGM_READ);
    if (FAILED(hr)) {
#ifndef NDEBUG
        wprintf(L"[ResolveShortcutTarget] IPersistFile::Load failed: hr=0x%08X, path=%s\n", hr, shortcutPath);
#endif
        if (shouldUninit) CoUninitialize();
        return false;
    }

    // 方法1: 通过 PIDL 获取目标
    hr = shellLink->GetIDList(wil::out_param(targetPidl));
    if (SUCCEEDED(hr) && targetPidl) {
        resolved = FillTargetNamesFromPidl(targetPidl.get(), targetParsingPath, targetDisplayName);
#ifndef NDEBUG
        if (resolved) {
            wprintf(L"[ResolveShortcutTarget] GetIDList succeeded: %s -> %s\n", shortcutPath, targetParsingPath.c_str());
        }
#endif
    }

    // 方法2: 直接获取路径
    if (!resolved) {
        WIN32_FIND_DATAW findData{};
        wchar_t resolvedPath[MAX_PATH] = { 0 };
        hr = shellLink->GetPath(resolvedPath, MAX_PATH, &findData, SLGP_RAWPATH);
        if (SUCCEEDED(hr) && resolvedPath[0] != L'\0') {
            targetParsingPath = resolvedPath;
            targetDisplayName = PathFindFileNameW(resolvedPath);
            resolved = true;
#ifndef NDEBUG
            wprintf(L"[ResolveShortcutTarget] GetPath succeeded: %s -> %s\n", shortcutPath, targetParsingPath.c_str());
#endif
        }
    }

    // 方法3: 直接读取文件
    if (!resolved) {
        resolved = TryReadShortcutTargetFromFile(shortcutPath, targetParsingPath, targetDisplayName);
#ifndef NDEBUG
        if (resolved) {
            wprintf(L"[ResolveShortcutTarget] TryReadShortcutTargetFromFile succeeded: %s -> %s\n", shortcutPath, targetParsingPath.c_str());
        }
#endif
    }

#ifndef NDEBUG
    if (!resolved) {
        wprintf(L"[ResolveShortcutTarget] All methods failed for: %s\n", shortcutPath);
    }
#endif

    if (shouldUninit) {
        CoUninitialize();
    }
    return resolved;
}
}

// 静态成员变量定义（必须在类外部进行�?
HWND CSystemApi::global_SysListView32 = nullptr;
HWND CSystemApi::global_realDesktop = nullptr;
//reference
//https://github.com/Puellaquae/Desktop-Snake
void CSystemApi::GetSpecifyItemString(HANDLE MMEListViewProcess, HWND hwnd, int row, int column, TCHAR* buffer, int bufferSize)
{
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
                    cout << "Failed to read memory" << endl;
                }
            }
            else {
                cout << "SendMessage failed" << endl;
                // Handle the error accordingly
            }
        }
        VirtualFreeEx(MMEListViewProcess, tempItem, 0, MEM_RELEASE);
        VirtualFreeEx(MMEListViewProcess, stringBuffer, 0, MEM_RELEASE);
    }
}



// Helper function to convert wide string (wchar_t*) to UTF-8 std::string
std::string WideToUtf8(const wchar_t* wideStr)
{
    if (!wideStr) return std::string();

    // 计算 UTF-8 缓冲区大�?
    int utf8_length = WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, nullptr, 0, nullptr, nullptr);
    if (utf8_length == 0) return std::string();

    // 创建 std::string 并分配空�?
    std::string utf8_string(utf8_length - 1, 0); // 不需要末�?\0

    // 执行转换
    WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, utf8_string.data(), utf8_length, nullptr, nullptr);

    return utf8_string;
}

CSystemApi::CSystemApi()
{
#ifdef _DEBUG
    cout << "SystemApi_DLL_Init" << endl;
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
SpecialFolders CSystemApi::getFolderTypeFromName(const wchar_t* name)
{
    if (!name) return SpecialFolders::Empty;

    if (wcsstr(name, L_MY_COMPUTER)) {
        return SpecialFolders::ThisPc;
    }
    else if (wcsstr(name, L_RECYCLE_BIN)) {
        return SpecialFolders::RecycleBin;
    }
    else if (wcsstr(name, L_CONTROL_PANEL)) {
        return SpecialFolders::ControlPanel;
    }
    else if (wcsstr(name, L_CONTROL_PANEL2)) {
        return SpecialFolders::ControlPanel2;
    }
    else if (wcsstr(name, L_NETWORK)) {
        return SpecialFolders::Network;
    }
    else if (wcsstr(name, L"OneDrive") != nullptr ||
        wcsstr(name, L"Microsoft OneDrive") != nullptr ||
        wcsstr(name, L"ONEDRIVE") != nullptr ||
        wcsstr(name, L_ONE_DRIVE)) {
        return SpecialFolders::OneDrive;
    }
    else if (wcsstr(name, L"\\Desktop\\") != nullptr ||
        wcsstr(name, L"\\桌面\\") != nullptr ||
        (wcsstr(name, L"\\Users\\") != nullptr && wcsstr(name, L"\\Desktop") != nullptr) ||
        (wcsstr(name, L"\\用户\\") != nullptr && wcsstr(name, L"\\桌面") != nullptr)) {
        return SpecialFolders::UnknownFolder;
    }
    else if (wcsstr(name, L_USER_FOLDER) || IsCurrentUserFolderPath(name)) {
        return SpecialFolders::UserFolder;
    }
    else if (wcsstr(name, L_LIARIES)) {
        return SpecialFolders::Libraries;
    }
    else if (wcsstr(name, L_LINUX)) {
        return SpecialFolders::Linux;
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
//寻找桌面图标窗口
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

//寻找桌面窗口
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
    //寻找第二个workerw
    if (syslistview32 == 0)
    {
        EnumWindows(FindSysListView32Internal, 0);
        syslistview32 = global_SysListView32;

    }
#ifdef _DEBUG
    cout << "syslistview32 hwnd:" << syslistview32 << endl;
#endif
    return syslistview32;
}

HWND CSystemApi::FindRealDesktop()
{
    HWND hparent = ::FindWindow(_T("progman"), _T("program manager"));
    SendMessageTimeout(hparent, 0x52c, 0, 0, 0, 100, 0);
    HWND hshelldll_defview = ::FindWindowEx(hparent, NULL, _T("shelldll_defview"), NULL);
    //寻找第二个workerw
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
    LPVOID ori = VirtualAllocEx(hProcess, NULL, sizeof(POINT), MEM_COMMIT, PAGE_READWRITE);
    POINT point = {};
    for (int i = 0; i < ListView_GetItemCount(sysListView32); i++)
    {
        //图标名称
        TCHAR Object[512];
        //图标位置
        ListView_GetItemPosition(sysListView32, i, ori);
        ReadProcessMemory(hProcess, ori, &point, sizeof(POINT), NULL);
        //获取图标名称
        GetSpecifyItemString(hProcess, sysListView32, i, 0, Object, sizeof(Object));

        // 这里直接�?TCHAR 当成 wchar_t 处理
        const wchar_t* wideStr = Object;
        std::string utf8_string = WideToUtf8(wideStr);

        // 去掉末尾的NULL字符
        //size_t length = wcslen(Object);
        // 分配足够的空间来存储 UTF-8 字符�?
        //int utf8_length = WideCharToMultiByte(CP_UTF8, 0, Object, -1, nullptr, 0, nullptr, nullptr);
        //char* utf8_buffer = new char[utf8_length];
        // 执行转换
        //WideCharToMultiByte(CP_UTF8, 0, Object, -1, utf8_buffer, utf8_length, nullptr, nullptr);
        //std::string utf8_string(utf8_buffer);
        // 转换
        //WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, utf8_string.data(), utf8_length, nullptr, nullptr);
        //std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        ////TCHAR类型转为为std::wstring
        //std::wstring utf16_string((wchar_t*)Object, sizeof(Object) / sizeof(wchar_t) - 1); // 去掉末尾的NULL字符
        //std::string utf8_string = converter.to_bytes(utf16_string);
        
        //清理末尾的所有\0
        std::string cleanedString;
        // 查找第一个\0的位�?
        size_t nullPos = utf8_string.find('\0');

        // 如果找到了\0，则截取第一个\0之前的部�?
        if (nullPos != std::string::npos) {
            cleanedString = utf8_string.substr(0, nullPos).c_str();
        }
        else {
            // 如果没有找到\0，则原字符串不变
            cleanedString = utf8_string;
        }
        IconItem iItem;
        iItem.name = cleanedString;
        iItem.position = point;
        iconItemList.push_back(iItem);
#ifndef NDEBUG
        cout << "Name :" << utf8_string.c_str() << endl;
        printf("Item(%d) 0 Position:(%d, %d)\n", i, point.x, point.y);
#endif
    }
    VirtualFreeEx(hProcess, ori, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    return iconItemList;
}

vector<IconItem> CSystemApi::GetDesktopPIDLItems()
{
	//vector<IconItem>* iconItems = new vector<IconItem>();
	vector <IconItem> iconItems;
    
    // 提前抓取可视坐标 (通过远线程注入提�?SysListView32)
    HWND sysListView32 = FindSysListView32();
    std::unordered_map<std::string, POINT> posMap;
    if (sysListView32) {
        vector<IconItem> visualItems = GetDesktopIconsItems(sysListView32);
        for (const auto& vi : visualItems) {
            // 存储为小写且去空格的名字，用于模糊匹�?
            std::string normalizedName = vi.name;
            while(!normalizedName.empty() && (normalizedName.back() == ' ' || normalizedName.back() == '\0')) normalizedName.pop_back();
            while(!normalizedName.empty() && (normalizedName.front() == ' ')) normalizedName.erase(0, 1);
            for(auto &c : normalizedName) c = tolower(c);
            
            posMap[normalizedName] = vi.position;
        }
    }

    // 获取桌面文件夹的 PIDL
    unique_pidl_absolute pidl = Tool::GetRootPidl();

    // 获取桌面文件夹的 ShellFolder
    wil::com_ptr_nothrow<IShellFolder> deskISF;
    HRESULT hr = SHGetDesktopFolder(&deskISF);
    if (FAILED(hr)) {
        // 如果加载失败，打印错误信息并返回 nullptr
        DWORD error = GetLastError();
        _tprintf(_T("Failed to get desktop folder interface: Error %d\n"), error);
        return iconItems;
    }
    // 创建枚举器以枚举桌面上的所有项
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
        _tprintf(_T("Failed to get enumeration of desktop items: %d\n"), error);
        return iconItems;
    }

    // 枚举每个文件�?
    LPITEMIDLIST pidlItem = nullptr;  // 临时存储PIDL

    while (enumIDList->Next(1, &pidlItem, NULL) == S_OK) {
        // 通过智能指针使用 pidlItem
        STRRET strret;
        IconItem i;
        wchar_t szPath[MAX_PATH];
        wchar_t szName[MAX_PATH];
        wchar_t szParsingPath[MAX_PATH];
        
        // 获取名称（SHGDN_NORMAL�?
        hr = deskISF->GetDisplayNameOf(pidlItem, SHGDN_NORMAL, &strret);
        if (SUCCEEDED(hr)) {
            StrRetToBufW(&strret, pidlItem, szName, MAX_PATH);
            i.name = Tool::WstrToStr(szName);
        }
        else {
            DWORD error = GetLastError();
            _tprintf(_T("Failed to get name: %d\n"), error);
        }

        // 获取解析路径（SHGDN_FORPARSING�?
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

        // 获取实际文件系统路径（用于判断是否为 .lnk 文件�?
        hr = deskISF->GetDisplayNameOf(pidlItem, SHGDN_NORMAL | SHGDN_INFOLDER, &strret);
        if (SUCCEEDED(hr)) {
            StrRetToBufW(&strret, pidlItem, szPath, MAX_PATH);
        }

        std::wstring shortcutTargetPath;
        std::wstring shortcutTargetDisplayName;
        // 使用 szParsingPath�?lnk 文件的绝对路径）来解析快捷方式目�?
        if (ResolveShortcutTarget(szParsingPath, shortcutTargetPath, shortcutTargetDisplayName)) {
            i.shortcutTargetPath = Tool::WstrToStr(shortcutTargetPath.c_str());
            i.shortcutTargetDisplayName = Tool::WstrToStr(shortcutTargetDisplayName.c_str());
            const wchar_t* targetSpecialSource = !shortcutTargetPath.empty()
                ? shortcutTargetPath.c_str()
                : (!shortcutTargetDisplayName.empty() ? shortcutTargetDisplayName.c_str() : nullptr);
            i.shortcutTargetSpecialFolder = getFolderTypeFromName(targetSpecialSource);
        }

		// 创建绝对 PIDL 以支持高质量图标提取和其�?Shell 操作
        if (rootPidl) {
            i.pidl_p = ILCombine(rootPidl.get(), pidlItem);
        } else {
            i.pidl_p = ILClone(pidlItem);
        }
        ILFree(pidlItem); // 释放 EnumIDList 返回的相�?PIDL

        if (i.pidl_p && Tool::WstrToStr(szName) != Tool::getWindowsUsername()) {
            std::wstring resolvedItemPath;
            std::wstring resolvedItemDisplayName;
            if (FillTargetNamesFromPidl(i.pidl_p, resolvedItemPath, resolvedItemDisplayName) && !resolvedItemPath.empty()) {
                i.path = Tool::WstrToStr(resolvedItemPath.c_str());
            }
        }

		i.specialFolder = getFolderTypeFromName(szParsingPath);
        
        // 尝试从可视抓取数组中匹配坐标 (同样进行归一化处�?
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
    // 释放枚举�?
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

#ifndef NDEBUG
        printf("[SystemApi] Found Desktop Window. AutoArrange=%d, SnapToGrid=%d\n", outAutoArrange, outSnapToGrid);
#endif
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
#ifndef NDEBUG
                printf("[SystemApi] Registry Path Success (%ls): AutoArrange=%d, SnapToGrid=%d\n", path, outAutoArrange, outSnapToGrid);
#endif
                return;
            }
            RegCloseKey(hKey);
        }
    }
}


const char* CSystemApi::GetTextFromDll(const char* filePath,int index)
{
    // 加载 DLL 文件
    HMODULE hModule = LoadLibraryA(filePath);
    if (hModule == NULL) {
        // 如果加载失败，打印错误信息并返回 nullptr
        DWORD error = GetLastError();
        _tprintf(_T("Failed to load DLL: Error %d\n"), error);
        return nullptr;
    }

    // 使用 LoadString �?DLL 中加载指定索引的字符串资�?
    char* strBuffer = new char[MAX_PATH]; // 假设 MAX_LOADSTRING 是足够大的缓冲区大小
    int result = LoadStringA(hModule, index, strBuffer, MAX_PATH);
    if (result == 0) {
        // 如果加载字符串失败，打印错误信息，释�?DLL，并返回 nullptr
        DWORD error = GetLastError();
        _tprintf(_T("Failed to load string from DLL: Error %d\n"), error);
        FreeLibrary(hModule);
        delete[] strBuffer;
        return nullptr;
    }

    // 字符串加载成功，确保字符串以 null 结尾（LoadString 可能未填充整个缓冲区�?
    strBuffer[result] = '\0';

    // 释放 DLL，并返回加载的字符串
    FreeLibrary(hModule);
    return strBuffer;
}

const char* CSystemApi::GetTextFromDll(const char* input)
{
    
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
        //std::cout << "该路径可能是快捷方式�? << std::endl;
        return true;
    }
    return false;
}

/**
 * 删除文件的原生API函数
*/
bool CSystemApi::DeleteFileNative(const wchar_t* filePath)
{
    if (::DeleteFile(filePath) == TRUE)
    {
        // 文件删除成功
        return true;
    }
    else
    {
        // 获取错误代码
        DWORD error = GetLastError();
        // 处理错误，例如输出错误信息到调试器或日志
        SetLastError(error);
        return false;
    }
}

bool CSystemApi::DeleteFiles(const std::vector<std::wstring>& filePaths, bool toRecycleBin, bool confirm)
{
    if (filePaths.empty()) return true;

    // SHFileOperation �?pFrom 要求以双 null 结尾，内部各路径以单 null 分隔
    std::vector<wchar_t> pathsBuffer;
    for (const auto& path : filePaths) {
        for (wchar_t c : path) pathsBuffer.push_back(c);
        pathsBuffer.push_back(0); // �?null 分隔
    }
    pathsBuffer.push_back(0); // �?null 结尾

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
    fileOp.hwnd = NULL; // 可以指定一个窗口句柄用于显示进度或错误信息
    fileOp.wFunc = FO_DELETE; // 指定操作类型为删�?
    fileOp.pFrom = filePath; // 指向待删除文件的路径，路径字符串必须以双null字符结尾（L"...\0\0"�?

    //启用确认对话�?
    if (comfirm)
    {
        fileOp.fFlags = FOF_ALLOWUNDO;
    }
    else {
        fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION; // 允许放入回收站并禁用确认对话�?
    }
    // 执行文件操作
    int result = SHFileOperationW(&fileOp);
    if (result == 0)
    {
        // 操作成功
        return true;
    }
    else
    {
        // 获取错误代码
        DWORD error = GetLastError();
        SetLastError(error);
        // 处理错误
        return false;
    }
}

PathType CSystemApi::GetPathType(const wchar_t* filePath)
{
    // Check for virtual Shell CLSID paths (e.g., ::{20D04FE0-3AEA-1069-A2D8-08002B30309D})
    if (filePath && wcsncmp(filePath, L"::", 2) == 0) {
        return DirectoryPath; // Virtual folders behave like directories for most Shell operations
    }

    DWORD attributes = GetFileAttributesW(filePath);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        // 无效路径
        return InvalidPath;
    }

    if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
        // 目录路径
        return DirectoryPath;
    }
    else {
        // 文件路径
        return FilePath;
    }
}

/**
lpVerb 参数�?ShellExecute �?lpOperation 参数一致：
edit 用编辑器打开 lpFile 指定的文档，如果 lpFile 不是文档，则会失�?
explore 浏览 lpFile 指定的文件夹
find 搜索 lpDirectory 指定的目�?
open 打开 lpFile 文件，lpFile 可以是文件或文件�?
print 打印 lpFile，如�?lpFile 不是文档，则函数失败
properties 显示属�?
runas 请求以管理员权限运行，比如以管理员权限运行某个exe
NULL 执行默认"open"动作
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
    case Z_DELETE:
        shell.lpVerb = TEXT("delete");
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
    // 替换下面�?CLSID_String 字符串为你想要打开�?CLSID 对应的程序的 CLSID 字符�?
    LPCWSTR CLSID_String = clsidStr;

    SHELLEXECUTEINFO sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_CLASSNAME;
    sei.lpClass = CLSID_String;
    sei.lpVerb = L"open";
    sei.nShow = SW_SHOWNORMAL;

    if (ShellExecuteEx(&sei))
    {
        // 执行成功
        return 0;
    }
    else
    {
        // 执行失败
        DWORD dwError = GetLastError();
        // 处理错误
        return dwError;
    }
    return 0;
}
//测试代码=========================================================
#ifdef _DEBUG

void WriteLocalFile(const vector<IconItem>* items) {
    // 打开要写入的文件
    std::ofstream outputFile("items_content.txt");

    if (outputFile.is_open()) {
        // 遍历队列并将内容写入文件
        for (const auto& item : *items) {
            //std::cout << item.name << std::endl;
            outputFile << item.name << std::endl;
        }
        // 关闭文件�?
        outputFile.close();
        std::cout << "List content has been written to 'items_content.txt'." << std::endl;
    }
    else {
        std::cout << "Unable to open file for writing." << std::endl;
    }
}

#endif // DEBUG
