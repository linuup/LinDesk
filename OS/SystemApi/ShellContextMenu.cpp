
#include "ShellContextMenu.h"

void ShellContextMenu::DisplayDesktopContextMenu()
{
    operationType = OperationType::BackGround;
    SHGetKnownFolderIDList(FOLDERID_Desktop, 0, NULL, wil::out_param(m_pidlParent));
}

void ShellContextMenu::DisplayItemContextMenu(LPITEMIDLIST pidl)
{
    if (pidl && operationType!=OperationType::BackGround) {
        GetDesktop_IshellFolder();
        HRESULT hr = m_pDesktopFolder->GetUIObjectOf(NULL, 1, (LPCITEMIDLIST*)&pidl, IID_IContextMenu, NULL, (LPVOID*)&m_pContextMenu);
        if (FAILED(hr))
        {
            OutputDebugString(L"Failed to get the file's IContextMenu interface.");
            return;
        }
        //Get object Menu interface
        m_contextMenu = IContextMenuFromIShellFolder(&pidl, m_pShellFolder);
    }
}

void ShellContextMenu::DisplayLinkContextMenu(const wchar_t* path) {
    operationType = OperationType::link;
    //获取ShellFolder接口
    m_pShellFolder = GetIshellFolderByPath(path);
    if (m_pShellFolder == nullptr) {
        return;
    }
    HRESULT hr = m_pDesktopFolder->GetUIObjectOf(NULL, 1, (LPCITEMIDLIST*)&m_pPIDL, IID_IContextMenu, NULL, (LPVOID*)&m_pContextMenu);
    if (FAILED(hr))
    {
        OutputDebugString(L"Failed to get the file's IContextMenu interface.");
        return;
    }
    //Get object Menu interface
    m_contextMenu = IContextMenuFromIShellFolder(&m_pPIDL, m_pShellFolder);
    
}

void ShellContextMenu::DisplayFileContextMenu(const wchar_t* path)
{
    operationType = OperationType::file;
    //获取ShellFolder接口
    m_pShellFolder = GetIshellFolderByPath(path);
    if (m_pShellFolder == nullptr) {
        return;
    }
    HRESULT hr = m_pDesktopFolder->GetUIObjectOf(NULL, 1, (LPCITEMIDLIST*)&m_pPIDL, IID_IContextMenu, NULL, (LPVOID*)&m_pContextMenu);
    if (FAILED(hr))
    {
        OutputDebugString(L"Failed to get the file's IContextMenu interface.");
        return;
    }
    //Get object Menu interface
    m_contextMenu = IContextMenuFromIShellFolder(&m_pPIDL, m_pShellFolder);
    
}

void ShellContextMenu::DisplayFolderContextMenu(const wchar_t* folderPath)
{
    operationType = OperationType::folder;
    //获取ShellFolder接口
    m_pShellFolder = GetIshellFolderByPath(folderPath);
    if (m_pShellFolder == nullptr) {
        return;
    }
    HRESULT hr = m_pDesktopFolder->GetUIObjectOf(NULL, 1, (LPCITEMIDLIST*)&m_pPIDL, IID_IContextMenu, NULL, (LPVOID*)&m_pContextMenu);
    if (FAILED(hr))
    {
        OutputDebugString(L"Failed to get the file's IContextMenu interface.");
        return;
    }
    //Get object Menu interface
    m_contextMenu = IContextMenuFromIShellFolder(&m_pPIDL, m_pShellFolder);
}

void ShellContextMenu::DisplaySpecialFolderContextMenu(SpecialFolders sf)
{
    operationType = OperationType::specialFolder;
    unique_pidl_absolute pidlsf;
    HRESULT hr;
    switch (sf)
    {
    case SpecialFolders::UnknownFolder:
        break;
    case SpecialFolders::ThisPc:
        hr= SHGetKnownFolderIDList(FOLDERID_ComputerFolder, 0, NULL, wil::out_param(pidlsf));
        break;
    case SpecialFolders::RecycleBin:
        hr = SHGetKnownFolderIDList(FOLDERID_RecycleBinFolder, 0, NULL, wil::out_param(pidlsf));
        break;
    case SpecialFolders::ControlPanel:
        hr = SHGetKnownFolderIDList(FOLDERID_ControlPanelFolder, 0, NULL, wil::out_param(pidlsf));
        break;
    case SpecialFolders::Network:
        hr = SHGetKnownFolderIDList(FOLDERID_NetworkFolder, 0, NULL, wil::out_param(pidlsf));
        break;
    case SpecialFolders::UserFolder:
        hr = SHGetKnownFolderIDList(FOLDERID_UsersFiles, 0, NULL, wil::out_param(pidlsf));
        break;
    }
    //获取桌面IShellFolder接口到m_pDesktopFolder
    GetDesktop_IshellFolder();
    wil::com_ptr_nothrow<IShellFolder> pThisPcShellFolder;
    // 获取 IShellFolder 接口
    hr = m_pDesktopFolder->BindToObject(pidlsf.get(), nullptr, IID_IShellFolder, (void**)&pThisPcShellFolder);
    //Get object Menu interface
    m_contextMenu = IContextMenuFromIShellFolder(pidlsf.get(), pThisPcShellFolder.get());
}

void ShellContextMenu::DisplayDriveContextMenu(const wchar_t* driveLetter, HWND hWnd, int xPos, int yPos)
{

}
void ShellContextMenu::DisplayMultipleContextMenu(vector<LPITEMIDLIST> pidls) {
    if (pidls.size()==0) {
        return;
    }
    operationType = OperationType::MultObject;
    unique_pidl_absolute pidlsf;
    // 获取上下文菜单
    IContextMenu* contextMenu = nullptr;
    GetDesktop_IshellFolder();
    m_pDesktopFolder->GetUIObjectOf(nullptr, pidls.size(),
        (LPCITEMIDLIST*)pidls.data(), IID_IContextMenu, nullptr, (void**)&m_contextMenu);

}

bool ShellContextMenu::GetDesktop_IshellFolder()
{
    HRESULT hr = SHGetDesktopFolder(&m_pDesktopFolder);
    return SUCCEEDED(hr) ? true : false;
}

// It's possible for a folder to not provide any IContextMenu instance (for example, the Home folder
// in Windows 11 doesn't provide any IContextMenu instance for the background menu). So, this method
// may return null.
wil::com_ptr_nothrow<IContextMenu> ShellContextMenu::MaybeGetShellContextMenu(HWND hwnd) const
{
    //1.获取桌面的PIDL
    //2.获取绑定桌面的IShellFolder 接口
    //3.获取IContextMenu接口
    unique_pidl_absolute pidlDesktop;
    HRESULT hr = SHGetKnownFolderIDList(FOLDERID_Desktop, 0, NULL, wil::out_param(pidlDesktop));
    if (SUCCEEDED(hr))
    {
        // 获取 IShellFolder 接口
        wil::com_ptr_nothrow<IShellFolder> shellFolder;
        hr = BindToIdl(m_pidlParent.get(), IID_PPV_ARGS(&shellFolder));

        if (SUCCEEDED(hr))
        {
            wil::com_ptr_nothrow<IContextMenu> contextMenu;

            hr = shellFolder->CreateViewObject(hwnd, IID_PPV_ARGS(&contextMenu));

            if (FAILED(hr))
            {
                return nullptr;
            }
            return contextMenu;
        }
    }
    return nullptr;
}

// Returns the parsing path for the current directory, but only if it's a filesystem path.
std::optional<std::string> ShellContextMenu::MaybeGetFilesystemDirectory() const
{
    wil::com_ptr_nothrow<IShellItem> shellItem;
    unique_pidl_absolute pidlDesktop;
    HRESULT hr = SHGetKnownFolderIDList(FOLDERID_Desktop, 0, NULL, wil::out_param(pidlDesktop));

    hr = SHCreateItemFromIDList(pidlDesktop.get(), IID_PPV_ARGS(&shellItem));

    if (FAILED(hr))
    {
        return std::nullopt;
    }

    wil::unique_cotaskmem_string parsingPath;
    hr = shellItem->GetDisplayName(SIGDN_FILESYSPATH, &parsingPath);

    if (SUCCEEDED(hr))
    {
        return Tool::WstrToStr(parsingPath.get());
    }

    // In Windows Explorer, it appears that when a menu item is invoked from the background context
    // menu in a library folder, the directory that's used is the default directory for that
    // library. The functionality here tries to mimic that behavior.
    // Without this, menu items like "Git Bash Here" wouldn't work when invoked from a library
    // directory (since a library doesn't have a filesystem path, even though it may be filesystem
    // backed).
    wil::com_ptr_nothrow<IShellLibrary> shellLibrary;
    hr = SHLoadLibraryFromItem(shellItem.get(), STGM_READ, IID_PPV_ARGS(&shellLibrary));

    if (FAILED(hr))
    {
        return std::nullopt;
    }

    wil::com_ptr_nothrow<IShellItem> defaultLocation;
    hr = shellLibrary->GetDefaultSaveFolder(DSFT_DETECT, IID_PPV_ARGS(&defaultLocation));

    if (FAILED(hr))
    {
        return std::nullopt;
    }

    hr = defaultLocation->GetDisplayName(SIGDN_FILESYSPATH, &parsingPath);

    if (FAILED(hr))
    {
        return std::nullopt;
    }

    return Tool::WstrToStr(parsingPath.get());
}

void ShellContextMenu::RemoveTencentDeskGoMenu(HMENU hmenu)
{

    // 找到菜单项的位置
    int menuPos = MenuHelper::FindMenuItem(hmenu, TEXT("开启桌面整理"));
    // 如果找到了，删除该菜单项
    if (menuPos != -1)
    {
        bool res = DeleteMenu(hmenu, menuPos, MF_BYPOSITION);
        if (res) {
            MenuHelper::RemoveAdjacentSeparators(hmenu);
        }
    }
}

void ShellContextMenu::ShowMenu(HWND hwnd, const POINT* pt, IUnknown* site, Flags flags) {
    if(operationType == ShellContextMenu::OperationType::BackGround){
        m_contextMenu = MaybeGetShellContextMenu(hwnd);
    }
    if (!m_contextMenu) {
        return;
    }
    // 创建空的弹出式菜单
    wil::unique_hmenu hMenu(CreatePopupMenu());
    if (!hMenu) {
        OutputDebugString(L"Failed to create popup menu.");
        return;
    }

    /* IUnknown* site = NULL;
     if (site; auto objectWithSite = m_contextMenu.try_query<IObjectWithSite>())
     {
         objectWithSite->SetSite(site);
     }*/

     //填充新建菜单
     // 使用 std::bind 绑定 this 指针
    auto boundFunction = std::bind(&ShellContextMenu::ParentWindowSubclass, this, hwnd, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);
    // Subclass the owner window, so that menu messages can be handled.
    // 使用 lambda 表达式绑定 this 指针
    auto handlerFunction = [this](HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
        return this->ParentWindowSubclass(hWnd, msg, wParam, lParam);
        };
    // 创建 WindowSubclassWrapper 实例
    auto winWrapper = std::make_unique<WindowSubclassWrapper>(hwnd, handlerFunction);

    m_contextMenu->QueryContextMenu(hMenu.get(), 0, MIN_SHELL_MENU_ID, MAX_SHELL_MENU_ID,
        CMF_NORMAL | CMF_EXPLORE);

    if (operationType == OperationType::BackGround) {
        RemoveTencentDeskGoMenu(hMenu.get()); 
    }
    


    //int itemCount = GetMenuItemCount(hMenu.get());
    //for (int i = 0; i < itemCount; i++) {
    //    wchar_t buffer[256];
    //    MENUITEMINFO mii = { sizeof(MENUITEMINFO) };
    //    mii.fMask = MIIM_STRING | MIIM_ID;
    //    mii.dwTypeData = buffer;
    //    mii.cch = 256;

    //    if (GetMenuItemInfo(hMenu.get(), i, TRUE, &mii)) {
    //        if (wcsstr(buffer, L"重命名") != nullptr ||
    //            wcsstr(buffer, L"rename") != nullptr) {
    //            // 找到重命名项，使用其命令ID
    //            UINT cmdID = mii.wID;

    //            CMINVOKECOMMANDINFOEX invokeInfo = { 0 };
    //            invokeInfo.cbSize = sizeof(CMINVOKECOMMANDINFOEX);
    //            invokeInfo.fMask = CMIC_MASK_ASYNCOK | CMIC_MASK_PTINVOKE;
    //            invokeInfo.hwnd = hwnd;
    //            invokeInfo.lpVerb = MAKEINTRESOURCEA(cmdID - 1);  // 减去基础偏移
    //            invokeInfo.nShow = SW_SHOWNORMAL;
    //            invokeInfo.ptInvoke = *pt;

    //            m_contextMenu->InvokeCommand((LPCMINVOKECOMMANDINFO)&invokeInfo);
    //        }
    //    }
    //}

    UINT cmd = TrackPopupMenu(hMenu.get(), TPM_LEFTALIGN | TPM_RETURNCMD, pt->x, pt->y, 0, hwnd, nullptr);
    if (cmd == 0)
    {
        return;
    }



    if (cmd >= MIN_SHELL_MENU_ID && cmd <= MAX_SHELL_MENU_ID)
    {
        TCHAR verb[64] = _T("");
        HRESULT hr = m_contextMenu->GetCommandString(cmd - MIN_SHELL_MENU_ID, GCS_VERB, nullptr,
            reinterpret_cast<LPSTR>(verb), static_cast<UINT>(std::size(verb)));
        bool handled = false;
        // Pass the menu back to the caller to give it the chance to handle it.
        if (SUCCEEDED(hr))
        {
            // 使用安全的比较函数
            auto compareVerb = [](const TCHAR* verb, const TCHAR* target) -> bool {
                return _wcsicmp(verb, target) == 0;  // 不区分大小写的宽字符比较
            };

            if (verb == L"open")
            {
                handled = true;
            }
            else if (verb == L"rename")
            {
                handled = true;
            }
            else if (compareVerb(verb,L"copy"))
            {
                handled = true;
            }
            else if (verb == L"cut")
            {

                handled = true;
            }else if (verb == L"delete")
            {
                handled = true;
            }

        }

        //auto viewMenus = BuildViewsMenu();

        if (!handled)
        {
            std::optional<std::string> parsingPathOpt = MaybeGetFilesystemDirectory();

            // Note that some menu items require the directory field to be set. For example, the
            // "Git Bash Here" item (which is added by Git for Windows) requires that.
            CMINVOKECOMMANDINFO commandInfo = {};
            commandInfo.cbSize = sizeof(commandInfo);
            commandInfo.fMask = 0;
            commandInfo.hwnd = hwnd;
            commandInfo.lpVerb = reinterpret_cast<LPCSTR>(MAKEINTRESOURCE(cmd - MIN_SHELL_MENU_ID));
            commandInfo.lpDirectory = parsingPathOpt ? parsingPathOpt->c_str() : nullptr;
            commandInfo.nShow = SW_SHOWNORMAL;
            m_contextMenu->InvokeCommand(&commandInfo);
        }
    }
        
}

IShellFolder* ShellContextMenu::GetIshellFolderByPath(const wchar_t* path)
{
    HRESULT hr = NULL;
    //获得父节点的 IShellFolder 接口
    if (!GetDesktop_IshellFolder()) {
        OutputDebugString(L"----------Get SHGetDesktopFolder error\n");
        return nullptr;
    }
    PathType pathType = CSystemApi::GetPathType(path);
    switch (pathType) {
    case InvalidPath:// 处理无效路径
        OutputDebugString(L"Input Path error");
        return nullptr;
    case FilePath:// 处理文件路径
        OutputDebugString(L"Process File Path");
        // 解析文件路径以得到文件相对于桌面的 ITEMIDLIST
        hr = m_pDesktopFolder->ParseDisplayName(NULL, NULL, (LPWSTR)path, NULL, &m_pPIDL, NULL);
        if (FAILED(hr) || m_pPIDL == nullptr)
        {
            OutputDebugString(L"\nFailed to parse display name for the file:");
            return nullptr;
        }
        return m_pDesktopFolder;
    case DirectoryPath://文件夹路径
        IShellFolder* iShellFolder = nullptr;
        hr = SHParseDisplayName(path, nullptr, &m_pPIDL, 0, nullptr);
        if (FAILED(hr)) {
            // 处理错误，例如路径无效
            OutputDebugString(L"----------Get PIDL error\n");
            return iShellFolder;
        }

        hr = SHBindToObject(nullptr, m_pPIDL, nullptr, IID_IShellFolder, (void**)&iShellFolder);
        if (FAILED(hr)) {
            // 释放pidl资源
            ILFree(m_pPIDL);
            // 处理错误，例如文件夹接口无效
            OutputDebugString(L"----------Get IShellFolder error\n");
            WCHAR szErrMsg[512];
            FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), szErrMsg, _countof(szErrMsg), NULL);
            OutputDebugStringW(szErrMsg);
            return iShellFolder;
        }
        return iShellFolder;
    }
	return nullptr;
    
}


IContextMenu* ShellContextMenu::IContextMenuFromIShellFolder(LPITEMIDLIST* pidl, IShellFolder* pShellFolder)
{
    LPCITEMIDLIST relativePIDL = nullptr;
    IContextMenu* pContextMenu = nullptr;
    if (pidl) {
        HRESULT hr = SHBindToParent(*pidl, IID_IShellFolder, (void**)&pShellFolder, &relativePIDL);
        if (FAILED(hr)) {
            OutputDebugString(L"Transfer PIDL Type Fail");
            return nullptr;
        }
        hr = pShellFolder->GetUIObjectOf(NULL, 1, &relativePIDL, IID_IContextMenu, nullptr, reinterpret_cast<void**>(&pContextMenu));
        if (FAILED(hr)) {
            WCHAR szErrMsg[512];
            FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), szErrMsg, _countof(szErrMsg), NULL);
            OutputDebugString(L"Get IContextMenu* Fail");
            OutputDebugStringW(szErrMsg);
            return nullptr; // 或者 throw exception...
        }
    }
    return pContextMenu;
}

IContextMenu* ShellContextMenu::IContextMenuFromIShellFolder(ITEMIDLIST *pidl, IShellFolder* pShellFolder)
{
    LPCITEMIDLIST relativePIDL = nullptr;
    IContextMenu* pContextMenu = nullptr;
    HRESULT hr = SHBindToParent(pidl, IID_IShellFolder, (void**)&pShellFolder, &relativePIDL);
    hr = m_pDesktopFolder->GetUIObjectOf(NULL, 1, &relativePIDL, IID_IContextMenu, nullptr, reinterpret_cast<void**>(&pContextMenu));
    return pContextMenu;
}

LRESULT ShellContextMenu::ParentWindowSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_MEASUREITEM:
    case WM_DRAWITEM:
    case WM_INITMENUPOPUP:
    case WM_MENUCHAR:
        // wParam is 0 if this item was sent by a menu.
        if ((msg == WM_MEASUREITEM || msg == WM_DRAWITEM) && wParam != 0)
        {
            break;
        }

        if (!m_contextMenu)
        {
            break;
        }

        if (auto contextMenu3 = m_contextMenu.try_query<IContextMenu3>())
        {
            LRESULT result;
            HRESULT hr = contextMenu3->HandleMenuMsg2(msg, wParam, lParam, &result);

            if (SUCCEEDED(hr))
            {
                return result;
            }
        }
        else if (auto contextMenu2 = m_contextMenu.try_query<IContextMenu2>())
        {
            contextMenu2->HandleMenuMsg(msg, wParam, lParam);
        }
        break;

        //case WM_MENUSELECT:
        //{
        //    if (!m_statusBar)
        //    {
        //        break;
        //    }

        //    if (HIWORD(wParam) == 0xFFFF && lParam == 0)
        //    {
        //        m_statusBar->HandleStatusBarMenuClose();
        //    }
        //    else
        //    {
        //        m_statusBar->HandleStatusBarMenuOpen();

        //        UINT menuItemId = LOWORD(wParam);

        //        if (WI_IsAnyFlagSet(HIWORD(wParam), MF_POPUP | MF_SEPARATOR))
        //        {
        //            m_statusBar->SetPartText(0, L"");
        //        }
        //        else if (menuItemId >= MIN_SHELL_MENU_ID && menuItemId <= MAX_SHELL_MENU_ID)
        //        {
        //            //CHECK(m_contextMenu);

        //            TCHAR helpString[512];
        //            HRESULT hr = m_contextMenu->GetCommandString(menuItemId - MIN_SHELL_MENU_ID,
        //                GCS_HELPTEXT, nullptr, reinterpret_cast<LPSTR>(helpString),
        //                static_cast<UINT>(std::size(helpString)));

        //            if (FAILED(hr))
        //            {
        //                StringCchCopy(helpString, std::size(helpString), L"");
        //            }

        //            m_statusBar->SetPartText(0, helpString);
        //        }
        //        else
        //        {
        //            std::wstring helpString = m_handler->GetHelpTextForItem(menuItemId);
        //            m_statusBar->SetPartText(0, helpString.c_str());
        //        }
        //    }

        //    // Prevent the message from been passed onto the original window.
        //    return 0;
        //}
        break;
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

wil::unique_hmenu ShellContextMenu::BuildViewMenu()
{
    return wil::unique_hmenu();
}

wil::unique_hmenu ShellContextMenu::BuildSortMenu()
{
    return wil::unique_hmenu();
}

HRESULT GetRootPidl(PIDLIST_ABSOLUTE* pidl)
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

    wil::com_ptr_nothrow<IShellFolder> desktop;
    RETURN_IF_FAILED(SHGetDesktopFolder(&desktop));
    return SHGetIDListFromObject(desktop.get(), pidl);
}

HRESULT BindToIdl(PIDLIST_ABSOLUTE pidl, REFIID riid, void** ppv)
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