#include "Tool.h"

class Tool;

unique_pidl_absolute Tool::GetRootPidl()
{
    // While using SHGetKnownFolderIDList() with FOLDERID_Desktop would be simpler than the method
    // used here, that method fails in Windows PE (with ERROR_FILE_NOT_FOUND). That failure is
    // unusual, since although the filesystem desktop folder doesn't exist, the virtual desktop
    // folder at the root of the shell namespace is still accessible and the pidl returned by
    // SHGetKnownFolderIDList() represents the root folder.
    // Retrieving the pidl using the method below works consistently, however.
    // Example:
    // unique_pidl_absolute rootPidl;
    // HRESULT hr = GetRootPidl(wil::out_param(rootPidl));
    HRESULT hr;
    unique_pidl_absolute pidl;
    hr = SHGetKnownFolderIDList(FOLDERID_Desktop, 0, NULL, wil::out_param(pidl));
    if (FAILED(hr)) {
        // 如果获取 PIDL 失败，输出错误并返回 nullptr
        std::cout << "Failed to get PIDL from desktop folder. HRESULT: " << hr;
        return nullptr;
    }
    //HRESULT hr;
    //wil::com_ptr_nothrow<IShellFolder> desktop;
    //hr = SHGetDesktopFolder(&desktop);
    //if (FAILED(hr)) {
    //    // 如果失败，输出错误并返回 nullptr
    //    std::cout << "Failed to get desktop folder. HRESULT: " << hr;
    //    return nullptr;
    //}
    //// 获取桌面文件夹的 PIDL
    //hr = SHGetIDListFromObject(desktop.get(), pidl);
    //if (FAILED(hr)) {
    //    // 如果获取 PIDL 失败，输出错误并返回 nullptr
    //    std::cout << "Failed to get PIDL from desktop folder. HRESULT: " << hr;
    //    return nullptr;
    //}
    return pidl;// 使用传入的pidl参数
}

HRESULT Tool::BindToIdl(PIDLIST_ABSOLUTE pidl, REFIID riid, void** ppv)
{
    IShellFolder* pDesktop = nullptr;

    HRESULT hr = SHGetDesktopFolder(&pDesktop);

    if (SUCCEEDED(hr))
    {
        /* See http://blogs.msdn.com/b/oldnewthing/archive/2011/08/30/10202076.aspx. */
        if (pidl->mkid.cb)
        {
            hr = pDesktop->BindToObject(pidl, nullptr, riid, ppv);
        }
        else
        {
            hr = pDesktop->QueryInterface(riid, ppv);
        }

        pDesktop->Release();
    }

    return hr;
}


// Helper function to convert wide string (wchar_t*) to UTF-8 std::string
std::string Tool::WideToUtf8(const wchar_t* wideStr)
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
ResourceInfo Tool::GetResourceInfo(const char* input)
{
    //std::unique_ptr<ResourceInfo> uptr(new ResourceInfo());
    ResourceInfo ri;
    //ResourceInfo* uptr = nullptr;
    // Find the last occurrence of ',' in the input string  
    const char* commaPos = strrchr(input, ',');
    if (commaPos != nullptr) {
        // Extract the path part (up to the comma)
        const char* commaPos = strrchr(input, ',');
        ri.path = ri.path.assign(input, commaPos).replace(0, 1, "");
        // Extract the index part (after the comma)
        char* endPtr;
        int index = std::strtol(commaPos + 1, &endPtr, 10);
        ri.index = std::abs(index); // Convert to integer
        if (*endPtr != '\0') { // Check if there's any non-digit character after the number
            // throw std::invalid_argument("Invalid index format");
        }
    }
    else {
        // throw std::invalid_argument("Invalid resource string format: Missing comma separator");
    }
    //return uptr.get();
    return ri;
}

bool Tool::IsExistResourceFromDll(std::string input)
{
    return !input.find("@") ? true : false;
}

std::string Tool::WstrToStr(const wchar_t* wstr)
{
    if (wstr == nullptr)
        return std::string();

    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (sizeNeeded == 0)
        return std::string();

    std::string strTo(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &strTo[0], sizeNeeded, nullptr, nullptr);
    return strTo;
}

void Tool::ConvertToGUID(const std::wstring& clsidStr)
{
    CLSID clsid;
    HRESULT hr = CLSIDFromString(clsidStr.c_str(), &clsid);

    if (SUCCEEDED(hr))
    {
        // 输出 GUID
        wprintf(L"GUID: {%08lX-%04X-%04X-%04X-%04X%04X%04X}\n",
            clsid.Data1, clsid.Data2, clsid.Data3,
            (clsid.Data4[0] << 8) | clsid.Data4[1],
            (clsid.Data4[2] << 8) | clsid.Data4[3],
            (clsid.Data4[4] << 8) | clsid.Data4[5],
            (clsid.Data4[6] << 8) | clsid.Data4[7]);
    }
    else
    {
        std::wcout << L"Failed to convert string to GUID" << std::endl;
    }
}
