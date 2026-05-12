
#include "ShellContextMenu.h"
#include <filesystem>
#include <fstream>
#include <string>

namespace {
std::filesystem::path GetDesktopLogPath()
{
    PWSTR localAppDataPath = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppDataPath);
    if (FAILED(hr) || !localAppDataPath) {
        return {};
    }

    std::filesystem::path logPath(localAppDataPath);
    CoTaskMemFree(localAppDataPath);
    logPath /= L"LinDesk";
    logPath /= L"desktop.log";
    return logPath;
}

std::string WideToUtf8(const std::wstring& text)
{
    if (text.empty()) {
        return {};
    }

    const int requiredSize = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (requiredSize <= 0) {
        return {};
    }

    std::string utf8(requiredSize, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), utf8.data(), requiredSize, nullptr, nullptr);
    return utf8;
}

std::wstring BuildTimestamp()
{
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    wchar_t timeBuffer[64] = {};
    swprintf_s(timeBuffer, L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return timeBuffer;
}

bool IsBoxMenuDebugEnabled()
{
    wchar_t value[8] = {};
    const DWORD length = GetEnvironmentVariableW(L"LINDESK_BOX_MENU_DEBUG", value, ARRAYSIZE(value));
    return length > 0 && value[0] != L'0';
}

void EmitBoxMenuDebugLine(const wchar_t* moduleName, const std::wstring& info)
{
    if (!IsBoxMenuDebugEnabled()) {
        return;
    }

    const std::wstring line = BuildTimestamp() + L"-" + (moduleName ? moduleName : L"Menu") + L"-" + info + L"\n";
    OutputDebugStringW(line.c_str());

    const std::filesystem::path logPath = GetDesktopLogPath();
    if (logPath.empty()) {
        return;
    }

    std::error_code ec;
    const bool logFileExists = std::filesystem::exists(logPath, ec) && !ec;
    if (!logFileExists) {
        return;
    }

    std::ofstream logFile(logPath, std::ios::app | std::ios::binary);
    if (!logFile.is_open()) {
        return;
    }

    const std::string utf8Line = WideToUtf8(line);
    if (!utf8Line.empty()) {
        logFile.write(utf8Line.data(), static_cast<std::streamsize>(utf8Line.size()));
    }
}

void DebugLogTopLevelMenuItems(HMENU menu, const wchar_t* tag)
{
    if (!menu || !IsBoxMenuDebugEnabled()) {
        return;
    }

    const int count = GetMenuItemCount(menu);
    EmitBoxMenuDebugLine(tag, L"top level menu count=" + std::to_wstring(count));

    for (int i = 0; i < count; ++i) {
        const UINT state = GetMenuState(menu, i, MF_BYPOSITION);
        if (state == static_cast<UINT>(-1)) {
            continue;
        }

        if ((state & MF_SEPARATOR) == MF_SEPARATOR) {
            EmitBoxMenuDebugLine(tag, L"[" + std::to_wstring(i) + L"] <separator>");
            continue;
        }

        std::wstring text = MenuHelper::GetMenuItemString(menu, i, true);
        if (text.empty()) {
            text = L"<empty>";
        }

        EmitBoxMenuDebugLine(tag, L"[" + std::to_wstring(i) + L"] " + text);
    }
}

int FindNewMenuInsertPosition(HMENU menu)
{
    if (!menu) {
        return -1;
    }

    const int newMenuPos = MenuHelper::FindMenuItem(menu, TEXT("Ne&w"));
    if (newMenuPos != -1) {
        return newMenuPos + 1;
    }

    const int plainNewMenuPos = MenuHelper::FindMenuItem(menu, TEXT("New"));
    if (plainNewMenuPos != -1) {
        return plainNewMenuPos + 1;
    }

    const int cnNewMenuPos = MenuHelper::FindMenuItem(menu, TEXT("新建"));
    if (cnNewMenuPos != -1) {
        return cnNewMenuPos + 1;
    }

    return -1;
}

void KeepOnlyNewMenuForBoxBackground(HMENU menu)
{
    if (!menu) {
        return;
    }

    auto normalizeMenuText = [](const std::wstring& text) {
        std::wstring normalized;
        normalized.reserve(text.size());

        for (size_t i = 0; i < text.size(); ++i) {
            const wchar_t ch = text[i];
            if (ch == L'&') {
                continue;
            }
            if (ch == L'(' && i + 2 < text.size() && text[i + 1] == L'&') {
                while (i < text.size() && text[i] != L')') {
                    ++i;
                }
                continue;
            }
            normalized.push_back(ch);
        }

        return normalized;
    };

    const wchar_t* exactCandidates[] = {
        L"View",
        L"视图",
        L"Sort by",
        L"排序",
        L"Refresh",
        L"刷新",
        L"Paste",
        L"粘贴",
        L"Paste shortcut",
        L"粘贴快捷方式",
        L"Browse background",
        L"浏览背景",
        L"Next background",
        L"下一个背景",
        L"Next desktop background",
        L"Display settings",
        L"显示设置",
        L"Personalize",
        L"个性化",
        L"Organize Desktop",
        L"Desktop organizer",
    };

    for (int i = GetMenuItemCount(menu) - 1; i >= 0; --i) {
        const std::wstring normalizedText = normalizeMenuText(MenuHelper::GetMenuItemString(menu, i, true));
        if (normalizedText.empty()) {
            continue;
        }

        bool shouldDelete = false;
        for (const auto* candidate : exactCandidates) {
            if (normalizedText == normalizeMenuText(candidate)) {
                shouldDelete = true;
                break;
            }
        }

        if (shouldDelete) {
            DeleteMenu(menu, static_cast<UINT>(i), MF_BYPOSITION);
        }
    }

    while (GetMenuItemCount(menu) > 0) {
        MENUITEMINFOW menuItemInfo = {};
        menuItemInfo.cbSize = sizeof(menuItemInfo);
        menuItemInfo.fMask = MIIM_FTYPE;
        if (!GetMenuItemInfo(menu, 0, TRUE, &menuItemInfo)) {
            break;
        }
        if (menuItemInfo.fType != MFT_SEPARATOR) {
            break;
        }
        DeleteMenu(menu, 0, MF_BYPOSITION);
    }

    MenuHelper::RemoveAdjacentSeparators(menu);
    MenuHelper::RemoveTrailingSeparators(menu);
}

}

void ShellContextMenu::DisplayDesktopContextMenu()
{
    m_contextMenu = nullptr;
    operationType = OperationType::BackGround;
    if (FAILED(SHGetKnownFolderIDList(FOLDERID_Desktop, 0, NULL, wil::out_param(m_pidlParent)))) {
        m_pidlParent.reset();
    }
}

void ShellContextMenu::DisplayBoxBackgroundContextMenu()
{
    m_contextMenu = nullptr;
    operationType = OperationType::boxBackground;
    if (FAILED(SHGetKnownFolderIDList(FOLDERID_Desktop, 0, NULL, wil::out_param(m_pidlParent)))) {
        m_pidlParent.reset();
    }
}

void ShellContextMenu::DisplayItemContextMenu(LPITEMIDLIST pidl)
{
    m_contextMenu = nullptr;
    if (pidl && operationType != OperationType::BackGround && operationType != OperationType::boxBackground) {
        if (!GetDesktop_IshellFolder() || !m_pDesktopFolder) {
            return;
        }
        wil::com_ptr_nothrow<IContextMenu> contextMenu;
        HRESULT hr = m_pDesktopFolder->GetUIObjectOf(NULL, 1, (LPCITEMIDLIST*)&pidl, IID_IContextMenu, NULL, contextMenu.put_void());
        if (FAILED(hr))
        {
            OutputDebugString(L"Failed to get the file's IContextMenu interface.");
            return;
        }
        //Get object Menu interface
        m_contextMenu = IContextMenuFromIShellFolder(pidl, m_pShellFolder.get());
    }
}

void ShellContextMenu::DisplayLinkContextMenu(const wchar_t* path) {
    m_contextMenu = nullptr;
    operationType = OperationType::link;
    //
    m_pShellFolder = GetIshellFolderByPath(path);
    if (!m_pShellFolder || !m_pDesktopFolder || !m_pPIDL) {
        return;
    }
    wil::com_ptr_nothrow<IContextMenu> contextMenu;
    HRESULT hr = m_pDesktopFolder->GetUIObjectOf(NULL, 1, (LPCITEMIDLIST*)&m_pPIDL, IID_IContextMenu, NULL, contextMenu.put_void());
    if (FAILED(hr))
    {
        OutputDebugString(L"Failed to get the file's IContextMenu interface.");
        return;
    }
    //Get object Menu interface
    m_contextMenu = IContextMenuFromIShellFolder(m_pPIDL.get(), m_pShellFolder.get());
    
}

void ShellContextMenu::DisplayFileContextMenu(const wchar_t* path)
{
    m_contextMenu = nullptr;
    operationType = OperationType::file;
    //鑾峰彇ShellFolder鎺ュ彛
    m_pShellFolder = GetIshellFolderByPath(path);
    if (!m_pShellFolder || !m_pDesktopFolder || !m_pPIDL) {
        return;
    }
    wil::com_ptr_nothrow<IContextMenu> contextMenu;
    HRESULT hr = m_pDesktopFolder->GetUIObjectOf(NULL, 1, (LPCITEMIDLIST*)&m_pPIDL, IID_IContextMenu, NULL, contextMenu.put_void());
    if (FAILED(hr))
    {
        OutputDebugString(L"Failed to get the file's IContextMenu interface.");
        return;
    }
    //Get object Menu interface
    m_contextMenu = IContextMenuFromIShellFolder(m_pPIDL.get(), m_pShellFolder.get());
    
}

void ShellContextMenu::DisplayFolderContextMenu(const wchar_t* folderPath)
{
    m_contextMenu = nullptr;
    operationType = OperationType::folder;
    //鑾峰彇ShellFolder鎺ュ彛
    m_pShellFolder = GetIshellFolderByPath(folderPath);
    if (!m_pShellFolder || !m_pDesktopFolder || !m_pPIDL) {
        return;
    }
    wil::com_ptr_nothrow<IContextMenu> contextMenu;
    HRESULT hr = m_pDesktopFolder->GetUIObjectOf(NULL, 1, (LPCITEMIDLIST*)&m_pPIDL, IID_IContextMenu, NULL, contextMenu.put_void());
    if (FAILED(hr))
    {
        OutputDebugString(L"Failed to get the file's IContextMenu interface.");
        return;
    }
    //Get object Menu interface
    m_contextMenu = IContextMenuFromIShellFolder(m_pPIDL.get(), m_pShellFolder.get());
}

void ShellContextMenu::DisplaySpecialFolderContextMenu(SpecialFolders sf)
{
    m_contextMenu = nullptr;
    operationType = OperationType::specialFolder;
    unique_pidl_absolute pidlsf;
    HRESULT hr;
    switch (sf)
    {
    case SpecialFolders::UnknownFolder:
        return;
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
    default:
        return;
    }
    if (FAILED(hr) || !pidlsf) {
        return;
    }
    //鑾峰彇妗岄潰IShellFolder鎺ュ彛鍒癿_pDesktopFolder
    if (!GetDesktop_IshellFolder() || !m_pDesktopFolder) {
        return;
    }
    wil::com_ptr_nothrow<IShellFolder> pThisPcShellFolder;
    // 鑾峰彇 IShellFolder 鎺ュ彛
    hr = m_pDesktopFolder->BindToObject(pidlsf.get(), nullptr, IID_IShellFolder, (void**)&pThisPcShellFolder);
    //Get object Menu interface
    m_contextMenu = IContextMenuFromIShellFolder(pidlsf.get(), pThisPcShellFolder.get());
}

void ShellContextMenu::DisplayMultipleContextMenu(vector<LPITEMIDLIST> pidls) {
    m_contextMenu = nullptr;
    if (pidls.size()==0) {
        return;
    }
    operationType = OperationType::MultObject;
    unique_pidl_absolute pidlsf;
    // 鑾峰彇涓婁笅鏂囪彍鍗?
    if (!GetDesktop_IshellFolder() || !m_pDesktopFolder) {
        return;
    }
    wil::com_ptr_nothrow<IContextMenu> contextMenu;
    HRESULT hr = m_pDesktopFolder->GetUIObjectOf(nullptr, pidls.size(),
        (LPCITEMIDLIST*)pidls.data(), IID_IContextMenu, nullptr, contextMenu.put_void());
    if (SUCCEEDED(hr) && contextMenu) {
        contextMenu->QueryInterface(IID_PPV_ARGS(&m_contextMenu));
    }

}

bool ShellContextMenu::GetDesktop_IshellFolder()
{
    HRESULT hr = SHGetDesktopFolder(&m_pDesktopFolder);
    return SUCCEEDED(hr) ? true : false;
}

// It's possible for a folder to not provide any IContextMenu instance (for example, the Home folder
// in Windows 11 doesn't provide any IContextMenu instance for the background menu). So, this method
// may return null.
wil::com_ptr_nothrow<IContextMenu3> ShellContextMenu::MaybeGetShellContextMenu(HWND hwnd) const
{
    //1.鑾峰彇妗岄潰鐨凱IDL
    //2.鑾峰彇缁戝畾妗岄潰鐨処ShellFolder 鎺ュ彛
    //3.鑾峰彇IContextMenu鎺ュ彛*/
    unique_pidl_absolute pidlDesktop;
    HRESULT hr = SHGetKnownFolderIDList(FOLDERID_Desktop, 0, NULL, wil::out_param(pidlDesktop));
    if (SUCCEEDED(hr))
    {
        if (!m_pidlParent) {
            return nullptr;
        }
        // 鑾峰彇 IShellFolder 鎺ュ彛
        wil::com_ptr_nothrow<IShellFolder> shellFolder;
        wil::com_ptr_nothrow<IContextMenu> contextMenu;
        hr = BindToIdl(m_pidlParent.get(), IID_PPV_ARGS(&shellFolder));
        if (FAILED(hr))
        {
            return nullptr;
        }
        hr = shellFolder->CreateViewObject(hwnd, IID_PPV_ARGS(&contextMenu));

        if (FAILED(hr))
        {
            return nullptr;
        }
        wil::com_ptr_nothrow<IContextMenu3> contextMenu3;
        hr = contextMenu->QueryInterface(IID_PPV_ARGS(&contextMenu3));
        if (FAILED(hr))
        {
            return nullptr;
        }
        return contextMenu3;
    }
    return nullptr;
}

// Returns the parsing path for the current directory, but only if it's a filesystem path.
std::optional<std::string> ShellContextMenu::MaybeGetFilesystemDirectory() const
{
    if (m_directoryOverride && !m_directoryOverride->empty()) {
        return m_directoryOverride;
    }

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

    // 鎵惧埌鑿滃崟椤圭殑浣嶇疆
    int menuPos = MenuHelper::FindMenuItem(hmenu, TEXT("\x5F00\x542F\x684C\x9762\x6574\x7406"));
    
    // 濡傛灉鎵惧埌浜嗭紝鍒犻櫎璇ヨ彍鍗曢」
    if (menuPos != -1)
    {
        bool res = DeleteMenu(hmenu, menuPos, MF_BYPOSITION);
        if (res) {
            MenuHelper::RemoveAdjacentSeparators(hmenu);
        }
    }
}

void ShellContextMenu::ShowMenu(HWND hwnd, const POINT* pt, IUnknown* site, Flags flags) {
    if (!hwnd || !pt) {
        return;
    }
    if(operationType == ShellContextMenu::OperationType::BackGround || operationType == ShellContextMenu::OperationType::boxBackground){
        m_contextMenu = MaybeGetShellContextMenu(hwnd);
    }
    if (!m_contextMenu) {
        return;
    }
    // 鍒涘缓绌虹殑寮瑰嚭寮忚彍鍗?
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

     //濉厖鏂板缓鑿滃崟
     // 浣跨敤 std::bind 缁戝畾 this 鎸囬拡
    auto boundFunction = std::bind(&ShellContextMenu::ParentWindowSubclass, this, hwnd, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);
    // Subclass the owner window, so that menu messages can be handled.
    // 浣跨敤 lambda 琛ㄨ揪寮忕粦瀹?this 鎸囬拡
    auto handlerFunction = [this](HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
        return this->ParentWindowSubclass(hWnd, msg, wParam, lParam);
        };
    // 鍒涘缓 WindowSubclassWrapper 瀹炰緥
    auto winWrapper = std::make_unique<WindowSubclassWrapper>(hwnd, handlerFunction);

    HRESULT queryHr = m_contextMenu->QueryContextMenu(hMenu.get(), 0, MIN_SHELL_MENU_ID, MAX_SHELL_MENU_ID,
        CMF_NORMAL | CMF_EXPLORE | CMF_CANRENAME);
    if (FAILED(queryHr)) {
        return;
    }

    if (operationType == OperationType::BackGround || operationType == OperationType::boxBackground) {
        RemoveTencentDeskGoMenu(hMenu.get());
        if (operationType == OperationType::boxBackground) {
            KeepOnlyNewMenuForBoxBackground(hMenu.get());
        }

        // 灏嗚嚜瀹氫箟鑿滃崟椤规彃鍏ュ埌鑳屾櫙鑿滃崟椤堕儴
        if (!m_customItems.empty()) {
            m_customCallbackMap.clear();
            UINT nextId = CUSTOM_MENU_ID_BASE;

            // 閫掑綊鏋勫缓鑿滃崟鐨勮緟鍔╁嚱鏁?
            std::function<void(HMENU, const std::vector<CustomMenuItem>&, int)> addItems =
                [&](HMENU hMenuTarget, const std::vector<CustomMenuItem>& items, int insertPosition) {
                const bool appendAfterAnchor = hMenuTarget == hMenu.get() && insertPosition >= 0;
                UINT currentInsertPos = appendAfterAnchor ? static_cast<UINT>(insertPosition) : 0;
                if (hMenuTarget == hMenu.get()) {
                    InsertMenuW(hMenuTarget, currentInsertPos, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
                    if (appendAfterAnchor) {
                        ++currentInsertPos;
                    }
                }

                const int start = appendAfterAnchor ? 0 : static_cast<int>(items.size()) - 1;
                const int end = appendAfterAnchor ? static_cast<int>(items.size()) : -1;
                const int step = appendAfterAnchor ? 1 : -1;
                for (int i = start; i != end; i += step) {
                    const auto& item = items[i];
                    if (item.isSeparator) {
                        InsertMenuW(hMenuTarget, currentInsertPos, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
                        if (appendAfterAnchor) {
                            ++currentInsertPos;
                        }
                    } else if (!item.children.empty()) {
                        HMENU hSubMenu = CreatePopupMenu();
                        addItems(hSubMenu, item.children, -1);
                        InsertMenuW(hMenuTarget, currentInsertPos, MF_BYPOSITION | MF_POPUP, (UINT_PTR)hSubMenu, item.text.c_str());
                        if (item.hBitmap) {
                            MENUITEMINFOW mii = { sizeof(mii) };
                            mii.fMask = MIIM_BITMAP;
                            mii.hbmpItem = item.hBitmap;
                            SetMenuItemInfoW(hMenuTarget, currentInsertPos, TRUE, &mii);
                        }
                        if (appendAfterAnchor) {
                            ++currentInsertPos;
                        }
                    } else {
                        UINT id = nextId++;
                        UINT flags = MF_BYPOSITION | MF_STRING;
                        if (item.checked) flags |= MF_CHECKED;
                        if (!item.enabled) flags |= MF_GRAYED;

                        InsertMenuW(hMenuTarget, currentInsertPos, flags, id, item.text.c_str());
                        
                        if (item.hBitmap) {
                            MENUITEMINFOW mii = { sizeof(mii) };
                            mii.fMask = MIIM_BITMAP;
                            mii.hbmpItem = item.hBitmap;
                            SetMenuItemInfoW(hMenuTarget, currentInsertPos, TRUE, &mii);
                        }
                        
                        if (item.callback) {
                            m_customCallbackMap[id] = item.callback;
                        }
                        if (appendAfterAnchor) {
                            ++currentInsertPos;
                        }
                    }
                }
            };

            if (operationType == OperationType::boxBackground) {
                std::vector<CustomMenuItem> sharedItems;
                std::vector<CustomMenuItem> boxTailItems;
                int splitIndex = -1;
                auto trimSeparators = [](std::vector<CustomMenuItem>& menuItems) {
                    while (!menuItems.empty() && menuItems.front().isSeparator) {
                        menuItems.erase(menuItems.begin());
                    }
                    while (!menuItems.empty() && menuItems.back().isSeparator) {
                        menuItems.pop_back();
                    }
                    for (int i = static_cast<int>(menuItems.size()) - 1; i > 0; --i) {
                        if (menuItems[i].isSeparator && menuItems[i - 1].isSeparator) {
                            menuItems.erase(menuItems.begin() + i);
                        }
                    }
                };

                for (int i = 0; i < static_cast<int>(m_customItems.size()); ++i) {
                    if (m_customItems[i].isSeparator) {
                        splitIndex = i;
                    }
                }

                if (splitIndex >= 0) {
                    for (int i = 0; i < static_cast<int>(m_customItems.size()); ++i) {
                        if (i < splitIndex) {
                            sharedItems.push_back(m_customItems[i]);
                        } else if (i > splitIndex) {
                            boxTailItems.push_back(m_customItems[i]);
                        }
                    }
                } else {
                    sharedItems = m_customItems;
                }

                trimSeparators(sharedItems);
                trimSeparators(boxTailItems);

                if (!sharedItems.empty()) {
                    addItems(hMenu.get(), sharedItems, -1);
                }

                if (!boxTailItems.empty()) {
                    int insertPosition = FindNewMenuInsertPosition(hMenu.get());
                    if (insertPosition < 0) {
                        insertPosition = GetMenuItemCount(hMenu.get());
                    }
                    addItems(hMenu.get(), boxTailItems, insertPosition);
                }
            } else {
                addItems(hMenu.get(), m_customItems, -1);
            }
        }
    }

    if (operationType == OperationType::boxBackground) {
        DebugLogTopLevelMenuItems(hMenu.get(), L"BoxContextMenu");
    }

    UINT cmd = TrackPopupMenu(hMenu.get(), TPM_LEFTALIGN | TPM_RETURNCMD, pt->x, pt->y, 0, hwnd, nullptr);
    if (cmd == 0)
    {
        return;
    }

    // 澶勭悊鑷畾涔夎彍鍗曢」锛堝寘鎷繁灞傚瓙鑿滃崟椤癸級
    auto it = m_customCallbackMap.find(cmd);
    if (it != m_customCallbackMap.end()) {
        if (it->second) {
            it->second();
        }
        return;
    }

    if (cmd >= MIN_SHELL_MENU_ID && cmd <= MAX_SHELL_MENU_ID)
    {
        char verb[64] = "";
        HRESULT hr = m_contextMenu->GetCommandString(cmd - MIN_SHELL_MENU_ID, GCS_VERBA, nullptr,
            verb, static_cast<UINT>(std::size(verb)));
        bool handled = false;
        // Pass the menu back to the caller to give it the chance to handle it.
        if (SUCCEEDED(hr))
        {
            auto compareVerb = [](const char* verb, const char* target) -> bool {
                return _stricmp(verb, target) == 0;
            };

            /*if (compareVerb(verb, L"open"))
            {
                handled = true;
            }*/
            if (compareVerb(verb, "rename"))
            {
                handled = true;
                if (hwnd) {
                    SendMessage(hwnd, WM_LINDESK_START_INLINE_RENAME, 0, 0);
                }
            }
            /*else if (compareVerb(verb, L"copy"))
            {
                handled = true;
            }
            else if (compareVerb(verb, L"cut"))
            {
            }else
            if (compareVerb(verb, L"delete"))
            {
                handled = false;

                //SendMessage(hwnd, WM_USER + 52, 0, 0);
            }*/
            
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

wil::com_ptr_nothrow<IShellFolder> ShellContextMenu::GetIshellFolderByPath(const wchar_t* path)
{
    HRESULT hr = NULL;
    //鑾峰緱鐖惰妭鐐圭殑 IShellFolder 鎺ュ彛
    if (!GetDesktop_IshellFolder()) {
        OutputDebugString(L"----------Get SHGetDesktopFolder error\n");
        return nullptr;
    }
    PathType pathType = CSystemApi::GetPathType(path);
    switch (pathType) {
    case InvalidPath:// 澶勭悊鏃犳晥璺緞
        OutputDebugString(L"Input Path error");
        return nullptr;
    case FilePath:// 澶勭悊鏂囦欢璺緞
        OutputDebugString(L"Process File Path");
        // 瑙ｆ瀽鏂囦欢璺緞浠ュ緱鍒版枃浠剁浉瀵逛簬妗岄潰鐨?ITEMIDLIST
        hr = m_pDesktopFolder->ParseDisplayName(NULL, NULL, (LPWSTR)path, NULL, wil::out_param(m_pPIDL), NULL);
        if (FAILED(hr) || !m_pPIDL)
        {
            OutputDebugString(L"\nFailed to parse display name for the file:");
            return nullptr;
        }
        return m_pDesktopFolder;
    case DirectoryPath://鏂囦欢澶硅矾寰?
        wil::com_ptr_nothrow<IShellFolder> iShellFolder;
        hr = SHParseDisplayName(path, nullptr, wil::out_param(m_pPIDL), 0, nullptr);
        if (FAILED(hr)) {
            // 澶勭悊閿欒锛屼緥濡傝矾寰勬棤鏁?
            OutputDebugString(L"----------Get PIDL error\n");
            return iShellFolder;
        }

        hr = SHBindToObject(nullptr, m_pPIDL.get(), nullptr, IID_PPV_ARGS(&iShellFolder));
        if (FAILED(hr)) {
            // 閲婃斁pidl璧勬簮
            // 澶勭悊閿欒锛屼緥濡傛枃浠跺す鎺ュ彛鏃犳晥
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


wil::com_ptr_nothrow<IContextMenu3> ShellContextMenu::IContextMenuFromIShellFolder(PCIDLIST_ABSOLUTE pidl, IShellFolder* pShellFolder)
{
    LPCITEMIDLIST relativePIDL = nullptr;
    wil::com_ptr_nothrow<IContextMenu3> pContextMenu;
    if (pidl) {
        wil::com_ptr_nothrow<IShellFolder> parentShellFolder;
        HRESULT hr = SHBindToParent(pidl, IID_PPV_ARGS(&parentShellFolder), &relativePIDL);
        if (SUCCEEDED(hr)) {
            wil::com_ptr_nothrow<IContextMenu> baseContextMenu;
            hr = parentShellFolder->GetUIObjectOf(NULL, 1, &relativePIDL, IID_IContextMenu, nullptr, baseContextMenu.put_void());
            if (SUCCEEDED(hr) && baseContextMenu) {
                baseContextMenu->QueryInterface(IID_PPV_ARGS(&pContextMenu));
            }
        }
    }
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
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

HRESULT GetRootPidl(PIDLIST_ABSOLUTE* pidl)
{
    wil::com_ptr_nothrow<IShellFolder> desktop;
    RETURN_IF_FAILED(SHGetDesktopFolder(&desktop));
    return SHGetIDListFromObject(desktop.get(), pidl);
}

HRESULT BindToIdl(PIDLIST_ABSOLUTE pidl, REFIID riid, void** ppv)
{
    if (!pidl || !ppv) {
        return E_INVALIDARG;
    }

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
