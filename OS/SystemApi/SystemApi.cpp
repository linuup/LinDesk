// SystemApi.cpp : Defines the exported functions for the DLL.
#include "SystemApi.h"
#include "Tool.h"
#include <Shlwapi.h>

// 静态成员变量定义（必须在类外部进行）
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

    // 计算 UTF-8 缓冲区大小
    int utf8_length = WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, nullptr, 0, nullptr, nullptr);
    if (utf8_length == 0) return std::string();

    // 创建 std::string 并分配空间
    std::string utf8_string(utf8_length - 1, 0); // 不需要末尾 \0

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

        // 这里直接把 TCHAR 当成 wchar_t 处理
        const wchar_t* wideStr = Object;
        std::string utf8_string = WideToUtf8(wideStr);

        // 去掉末尾的NULL字符
        //size_t length = wcslen(Object);
        // 分配足够的空间来存储 UTF-8 字符串
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
        // 查找第一个\0的位置
        size_t nullPos = utf8_string.find('\0');

        // 如果找到了\0，则截取第一个\0之前的部分
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
    IEnumIDList* pEnumIDList;
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
    hr = deskISF->EnumObjects(NULL,
        SHCONTF_FOLDERS |
        SHCONTF_NONFOLDERS |
        SHCONTF_INCLUDEHIDDEN |
        SHCONTF_INCLUDESUPERHIDDEN |
        SHCONTF_ENABLE_ASYNC
        , &pEnumIDList);

    if (FAILED(hr)) {
        DWORD error = GetLastError();
        _tprintf(_T("Failed to get enumeration of desktop items:",error));
        return iconItems;
    }

    // 枚举每个文件项
    LPITEMIDLIST pidlItem = nullptr;  // 临时存储PIDL

    while (pEnumIDList->Next(1, &pidlItem, NULL) == S_OK) {
        // 通过智能指针使用 pidlItem
        STRRET strret;
        IconItem i;
        wchar_t szPath[MAX_PATH];
        wchar_t szName[MAX_PATH];
        // 获取名称（SHGDN_NORMAL）
        hr = deskISF->GetDisplayNameOf(pidlItem, SHGDN_NORMAL, &strret);
        if (SUCCEEDED(hr)) {
            StrRetToBufW(&strret, pidlItem, szName, MAX_PATH);
            i.name = Tool::WstrToStr(szName);
        }
        else {
            DWORD error = GetLastError();
            _tprintf(_T("Failed to get name:\n", error));
        }

        // 获取路径（SHGDN_FORPARSING）
        hr = deskISF->GetDisplayNameOf(pidlItem, SHGDN_FORPARSING, &strret);
        if (SUCCEEDED(hr)) {
            StrRetToBufW(&strret, pidlItem, szPath, MAX_PATH);
            i.path = Tool::WstrToStr(szPath);
			//Tool::ConvertToGUID(L"{20D04FE0-3AEA-1069-A2D8-08002B30309D}");
        }
        else {
            DWORD error = GetLastError();
            //_tprintf(_T("Failed to get path:\n",error));
        }
        i.pidl_p = std::move(pidlItem);
		i.specialFolder = getFolderTypeFromName(szPath);
        iconItems.push_back(i);
#ifndef NDEBUG
		//std::wcout << L"\nName: " << szName << L", Path: " << szPath << std::endl;
#endif
        
    }
    // 释放枚举器
    pEnumIDList->Release();
    return iconItems;
}

const wchar_t* L_MY_COMPUTER(TEXT(MY_COMPUTER_CLS));

const wchar_t* L_RECYCLE_BIN(TEXT(RECYCLE_BIN_CLS));
const wchar_t* L_CONTROL_PANEL(TEXT(CONTROL_PANEL_CLS));
const wchar_t* L_CONTROL_PANEL2(TEXT(CONTROL_PANEL_CLS2));
const wchar_t* L_NETWORK(TEXT(NETWORK_CLS));
const wchar_t* L_USER_FOLDER(TEXT(USER_FOLDER_CLS));
const wchar_t* L_LIARIES(TEXT(LIBRARIES_CLS));
const wchar_t* L_LINUX(TEXT(LINUX_CLS));
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
    else if (wcsstr(name, L_USER_FOLDER)) {
        return SpecialFolders::UserFolder;
    }
    else if (wcsstr(name, L_LIARIES)) {
        return SpecialFolders::Libraries;
    }
    else if (wcsstr(name, L_LINUX)) {
        return SpecialFolders::Linux;
    }

    return SpecialFolders::UnknownFolder;  // 如果没有匹配到任何特殊文件夹，返回未知
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

    // 使用 LoadString 从 DLL 中加载指定索引的字符串资源
    char* strBuffer = new char[MAX_PATH]; // 假设 MAX_LOADSTRING 是足够大的缓冲区大小
    int result = LoadStringA(hModule, index, strBuffer, MAX_PATH);
    if (result == 0) {
        // 如果加载字符串失败，打印错误信息，释放 DLL，并返回 nullptr
        DWORD error = GetLastError();
        _tprintf(_T("Failed to load string from DLL: Error %d\n"), error);
        FreeLibrary(hModule);
        delete[] strBuffer;
        return nullptr;
    }

    // 字符串加载成功，确保字符串以 null 结尾（LoadString 可能未填充整个缓冲区）
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
        //std::cout << "该路径可能是快捷方式。" << std::endl;
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

bool CSystemApi::DeleteFileToRecycleBin(const wchar_t* filePath, bool comfirm)
{
    SHFILEOPSTRUCTW fileOp = { 0 };
    fileOp.hwnd = NULL; // 可以指定一个窗口句柄用于显示进度或错误信息
    fileOp.wFunc = FO_DELETE; // 指定操作类型为删除
    fileOp.pFrom = filePath; // 指向待删除文件的路径，路径字符串必须以双null字符结尾（L"...\0\0"）

    //启用确认对话框
    if (comfirm)
    {
        fileOp.fFlags = FOF_ALLOWUNDO;
    }
    else {
        fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION; // 允许放入回收站并禁用确认对话框
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
lpVerb 参数与 ShellExecute 的 lpOperation 参数一致：
edit 用编辑器打开 lpFile 指定的文档，如果 lpFile 不是文档，则会失败
explore 浏览 lpFile 指定的文件夹
find 搜索 lpDirectory 指定的目录
open 打开 lpFile 文件，lpFile 可以是文件或文件夹
print 打印 lpFile，如果 lpFile 不是文档，则函数失败
properties 显示属性
runas 请求以管理员权限运行，比如以管理员权限运行某个exe
NULL 执行默认”open”动作
**/
void CSystemApi::File_RightMenuOperations(const TCHAR* filePath, File_RightMenu_Operations operation)
{
    SHELLEXECUTEINFO shell;
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
int OpenCLSID(const wchar_t* clsidStr)
{
    // 替换下面的 CLSID_String 字符串为你想要打开的 CLSID 对应的程序的 CLSID 字符串
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
        // 关闭文件流
        outputFile.close();
        std::cout << "List content has been written to 'items_content.txt'." << std::endl;
    }
    else {
        std::cout << "Unable to open file for writing." << std::endl;
    }
}

#endif // DEBUG