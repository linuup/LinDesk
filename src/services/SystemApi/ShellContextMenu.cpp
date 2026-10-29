
#include "ShellContextMenu.h"
#include <algorithm>
#include <cwctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <Shlwapi.h>
#include <ShlObj.h>
#include <shellapi.h>
#include <QDebug>
#include <QElapsedTimer>
#include <mutex>

namespace {
constexpr wchar_t kShellMenuExclusionSection[] = L"ShellMenuExclusions";

HBITMAP CopyMenuBitmap(HBITMAP source)
{
    const uintptr_t rawValue = reinterpret_cast<uintptr_t>(source);
    if (!source || rawValue <= 16 || rawValue == static_cast<uintptr_t>(-1)) {
        return nullptr;
    }

    return reinterpret_cast<HBITMAP>(CopyImage(source, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
}

// ==========================================
// SEH 结构化异常保护辅助函数 (隔离第三方 COM 插件崩溃)
// ==========================================
HRESULT SafeInvokeCommand(IContextMenu* contextMenu, CMINVOKECOMMANDINFO* cmi)
{
    if (!contextMenu || !cmi) return E_POINTER;
    __try {
        return contextMenu->InvokeCommand(cmi);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return E_FAIL;
    }
}

HRESULT SafeQueryContextMenu(IContextMenu* contextMenu, HMENU hmenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags)
{
    if (!contextMenu) return E_POINTER;
    __try {
        return contextMenu->QueryContextMenu(hmenu, indexMenu, idCmdFirst, idCmdLast, uFlags);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return E_FAIL;
    }
}

HRESULT SafeHandleMenuMsg2(IContextMenu3* contextMenu3, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT* plResult)
{
    if (!contextMenu3) return E_POINTER;
    __try {
        return contextMenu3->HandleMenuMsg2(uMsg, wParam, lParam, plResult);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return E_FAIL;
    }
}

HRESULT SafeHandleMenuMsg(IContextMenu2* contextMenu2, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (!contextMenu2) return E_POINTER;
    __try {
        return contextMenu2->HandleMenuMsg(uMsg, wParam, lParam);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return E_FAIL;
    }
}

HRESULT SafeGetCommandString(IContextMenu* contextMenu, UINT_PTR idCmd, UINT uType, UINT* pReserved, LPSTR pszName, UINT cchMax)
{
    if (!contextMenu) return E_POINTER;
    __try {
        return contextMenu->GetCommandString(idCmd, uType, pReserved, pszName, cchMax);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return E_FAIL;
    }
}

HRESULT SafeGetCommandStringW(IContextMenu* contextMenu, UINT_PTR idCmd, UINT uType, UINT* pReserved, LPWSTR pszName, UINT cchMax)
{
    if (!contextMenu) return E_POINTER;
    __try {
        return contextMenu->GetCommandString(idCmd, uType, pReserved, reinterpret_cast<LPSTR>(pszName), cchMax);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return E_FAIL;
    }
}

// 动态适配 Windows 原生暗黑右键菜单主题
void EnableWindowDarkMenuTheme(HWND hwnd)
{
    if (!hwnd) return;
    static HMODULE hUxtheme = LoadLibraryW(L"uxtheme.dll");
    if (!hUxtheme) return;

    using fnSetPreferredAppMode = int(WINAPI*)(int appMode);
    using fnAllowDarkModeForWindow = bool(WINAPI*)(HWND hWnd, bool allow);

    static auto pSetPreferredAppMode = reinterpret_cast<fnSetPreferredAppMode>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135)));
    static auto pAllowDarkModeForWindow = reinterpret_cast<fnAllowDarkModeForWindow>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(133)));

    static std::once_flag initDarkFlag;
    std::call_once(initDarkFlag, []() {
        if (pSetPreferredAppMode) {
            pSetPreferredAppMode(1); // AllowDark = 1 (自动跟随系统深浅模式)
        }
    });

    if (pAllowDarkModeForWindow) {
        pAllowDarkModeForWindow(hwnd, true);
    }
    // 还原 1.0.5 模式：移除 pFlushMenuThemes()，不强制清空系统菜单缓存，消除 30~80ms 阻塞
}

HBITMAP BitmapFromIcon(HICON icon)
{
    if (!icon) {
        return nullptr;
    }

    ICONINFO iconInfo = {};
    if (!GetIconInfo(icon, &iconInfo)) {
        return nullptr;
    }

    BITMAP maskInfo = {};
    if (iconInfo.hbmColor) {
        GetObjectW(iconInfo.hbmColor, sizeof(maskInfo), &maskInfo);
    } else if (iconInfo.hbmMask) {
        GetObjectW(iconInfo.hbmMask, sizeof(maskInfo), &maskInfo);
    }

    const int width = maskInfo.bmWidth > 0 ? maskInfo.bmWidth : GetSystemMetrics(SM_CXSMICON);
    const int height = iconInfo.hbmColor
        ? (maskInfo.bmHeight > 0 ? maskInfo.bmHeight : GetSystemMetrics(SM_CYSMICON))
        : (maskInfo.bmHeight > 1 ? maskInfo.bmHeight / 2 : GetSystemMetrics(SM_CYSMICON));

    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screenDc = GetDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(screenDc, &bitmapInfo, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap && screenDc) {
        HDC memoryDc = CreateCompatibleDC(screenDc);
        HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);
        RECT rect = { 0, 0, width, height };
        FillRect(memoryDc, &rect, reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH)));
        DrawIconEx(memoryDc, 0, 0, icon, width, height, 0, nullptr, DI_NORMAL);
        SelectObject(memoryDc, oldBitmap);
        DeleteDC(memoryDc);
    }
    if (screenDc) {
        ReleaseDC(nullptr, screenDc);
    }
    if (iconInfo.hbmColor) {
        DeleteObject(iconInfo.hbmColor);
    }
    if (iconInfo.hbmMask) {
        DeleteObject(iconInfo.hbmMask);
    }
    return bitmap;
}

std::wstring ExpandEnvironmentStringsValue(const std::wstring& value)
{
    if (value.empty()) {
        return {};
    }

    const DWORD required = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
    if (required == 0) {
        return value;
    }

    std::wstring expanded(required, L'\0');
    const DWORD written = ExpandEnvironmentStringsW(value.c_str(), expanded.data(), required);
    if (written == 0 || written > required) {
        return value;
    }
    expanded.resize(written - 1);
    return expanded;
}

std::wstring QueryRegistryString(HKEY root, const std::wstring& subKey, const wchar_t* valueName = nullptr)
{
    DWORD type = 0;
    DWORD size = 0;
    LONG result = RegGetValueW(root, subKey.c_str(), valueName, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, &type, nullptr, &size);
    if (result != ERROR_SUCCESS || size == 0) {
        return {};
    }

    std::wstring value(size / sizeof(wchar_t), L'\0');
    result = RegGetValueW(root, subKey.c_str(), valueName, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, &type, value.data(), &size);
    if (result != ERROR_SUCCESS) {
        return {};
    }
    while (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    return type == REG_EXPAND_SZ ? ExpandEnvironmentStringsValue(value) : value;
}

bool QueryRegistrySubKeyNames(HKEY root, const std::wstring& subKey, std::vector<std::wstring>& names)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }

    DWORD index = 0;
    wchar_t name[256] = {};
    DWORD nameLength = ARRAYSIZE(name);
    while (RegEnumKeyExW(key, index++, name, &nameLength, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        names.emplace_back(name, nameLength);
        nameLength = ARRAYSIZE(name);
    }
    RegCloseKey(key);
    return true;
}

std::wstring TrimMenuTextForRegistry(std::wstring text)
{
    std::wstring output;
    output.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const wchar_t ch = text[i];
        if (ch == L'&') {
            continue;
        }
        if (ch == L'\t') {
            break;
        }
        output.push_back(ch);
    }

    while (!output.empty() && std::iswspace(output.front())) {
        output.erase(output.begin());
    }
    while (!output.empty() && std::iswspace(output.back())) {
        output.pop_back();
    }
    return output;
}

std::wstring LowercaseText(std::wstring text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return text;
}

bool LooksLikeDynamicFileName(const std::wstring& text)
{
    const size_t dotPos = text.find_last_of(L'.');
    if (dotPos == std::wstring::npos || dotPos == 0 || dotPos + 1 >= text.size()) {
        return false;
    }

    const std::wstring extension = LowercaseText(text.substr(dotPos + 1));
    if (extension.size() < 2 || extension.size() > 8) {
        return false;
    }

    for (wchar_t ch : extension) {
        if (!std::iswalnum(ch)) {
            return false;
        }
    }
    return true;
}

bool TextContainsInsensitive(const std::wstring& text, const std::wstring& needle)
{
    if (needle.empty()) {
        return false;
    }
    return LowercaseText(text).find(LowercaseText(needle)) != std::wstring::npos;
}

std::wstring ExecutableFromCommand(const std::wstring& command)
{
    std::wstring expanded = ExpandEnvironmentStringsValue(command);
    while (!expanded.empty() && std::iswspace(expanded.front())) {
        expanded.erase(expanded.begin());
    }
    if (expanded.empty()) {
        return {};
    }

    if (expanded.front() == L'"') {
        const size_t endQuote = expanded.find(L'"', 1);
        if (endQuote != std::wstring::npos) {
            return expanded.substr(1, endQuote - 1);
        }
    }

    const size_t exePos = LowercaseText(expanded).find(L".exe");
    if (exePos != std::wstring::npos) {
        return expanded.substr(0, exePos + 4);
    }

    const size_t dllPos = LowercaseText(expanded).find(L".dll");
    if (dllPos != std::wstring::npos) {
        return expanded.substr(0, dllPos + 4);
    }

    const size_t firstSpace = expanded.find(L' ');
    return firstSpace == std::wstring::npos ? expanded : expanded.substr(0, firstSpace);
}

HBITMAP BitmapFromIconLocation(const std::wstring& location)
{
    const std::wstring expanded = ExpandEnvironmentStringsValue(location);
    if (expanded.empty()) {
        return nullptr;
    }

    std::wstring path = expanded;
    int iconIndex = 0;
    const size_t commaPos = expanded.rfind(L',');
    if (commaPos != std::wstring::npos) {
        path = expanded.substr(0, commaPos);
        try {
            iconIndex = std::stoi(expanded.substr(commaPos + 1));
        } catch (...) {
            iconIndex = 0;
        }
    }

    while (!path.empty() && std::iswspace(path.front())) {
        path.erase(path.begin());
    }
    while (!path.empty() && std::iswspace(path.back())) {
        path.pop_back();
    }
    if (path.size() >= 2 && path.front() == L'"' && path.back() == L'"') {
        path = path.substr(1, path.size() - 2);
    }
    if (path.empty()) {
        return nullptr;
    }

    HICON largeIcon = nullptr;
    HICON smallIcon = nullptr;
    const UINT count = ExtractIconExW(path.c_str(), iconIndex, &largeIcon, &smallIcon, 1);
    HICON icon = smallIcon ? smallIcon : largeIcon;
    HBITMAP bitmap = count > 0 && icon ? BitmapFromIcon(icon) : nullptr;
    if (largeIcon) {
        DestroyIcon(largeIcon);
    }
    if (smallIcon) {
        DestroyIcon(smallIcon);
    }
    return bitmap;
}

std::wstring CommandVerbForMenuItem(IContextMenu3* contextMenu, UINT commandId)
{
    if (!contextMenu || commandId < MIN_SHELL_MENU_ID || commandId > MAX_SHELL_MENU_ID) {
        return {};
    }

    wchar_t wideVerb[256] = {};
    if (SUCCEEDED(SafeGetCommandStringW(contextMenu, commandId - MIN_SHELL_MENU_ID, GCS_VERBW, nullptr,
        wideVerb, static_cast<UINT>(sizeof(wideVerb))))) {
        if (wideVerb[0] != L'\0') {
            return wideVerb;
        }
    }

    char verb[256] = {};
    if (SUCCEEDED(SafeGetCommandString(contextMenu, commandId - MIN_SHELL_MENU_ID, GCS_VERBA, nullptr,
        verb, static_cast<UINT>(std::size(verb))))) {
        if (verb[0] != '\0') {
            const int required = MultiByteToWideChar(CP_UTF8, 0, verb, -1, nullptr, 0);
            if (required > 1) {
                std::wstring result(required - 1, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, verb, -1, result.data(), required);
                return result;
            }
            const int ansiRequired = MultiByteToWideChar(CP_ACP, 0, verb, -1, nullptr, 0);
            if (ansiRequired > 1) {
                std::wstring result(ansiRequired - 1, L'\0');
                MultiByteToWideChar(CP_ACP, 0, verb, -1, result.data(), ansiRequired);
                return result;
            }
        }
    }
    return {};
}

bool RegistryShellItemMatches(const std::wstring& subKeyName,
    const std::wstring& menuText,
    const std::wstring& verb,
    const std::wstring& displayText)
{
    const std::wstring cleanText = TrimMenuTextForRegistry(menuText);
    if (!verb.empty() && _wcsicmp(subKeyName.c_str(), verb.c_str()) == 0) {
        return true;
    }
    if (!displayText.empty() && (TextContainsInsensitive(cleanText, displayText) || TextContainsInsensitive(displayText, cleanText))) {
        return true;
    }
    if (!subKeyName.empty() && TextContainsInsensitive(cleanText, subKeyName)) {
        return true;
    }
    return false;
}

HBITMAP BitmapFromRegistryShellItem(HKEY root,
    const std::wstring& shellRoot,
    const std::wstring& subKeyName)
{
    const std::wstring itemKey = shellRoot + L"\\" + subKeyName;
    HBITMAP bitmap = BitmapFromIconLocation(QueryRegistryString(root, itemKey, L"Icon"));
    if (bitmap) {
        return bitmap;
    }

    const std::wstring command = QueryRegistryString(root, itemKey + L"\\command");
    const std::wstring executable = ExecutableFromCommand(command);
    return BitmapFromIconLocation(executable);
}

HBITMAP BitmapFromRegistryShellRoots(const std::vector<std::wstring>& shellRoots,
    const std::wstring& menuText,
    const std::wstring& verb)
{
    for (const std::wstring& shellRoot : shellRoots) {
        std::vector<std::wstring> subKeyNames;
        if (!QueryRegistrySubKeyNames(HKEY_CLASSES_ROOT, shellRoot, subKeyNames)) {
            continue;
        }

        for (const std::wstring& subKeyName : subKeyNames) {
            const std::wstring itemKey = shellRoot + L"\\" + subKeyName;
            std::wstring displayText = QueryRegistryString(HKEY_CLASSES_ROOT, itemKey, L"MUIVerb");
            if (displayText.empty()) {
                displayText = QueryRegistryString(HKEY_CLASSES_ROOT, itemKey);
            }

            if (!RegistryShellItemMatches(subKeyName, menuText, verb, displayText)) {
                continue;
            }

            HBITMAP bitmap = BitmapFromRegistryShellItem(HKEY_CLASSES_ROOT, shellRoot, subKeyName);
            if (bitmap) {
                return bitmap;
            }
        }
    }
    return nullptr;
}

std::wstring RegistryDefaultValue(const std::wstring& key)
{
    return QueryRegistryString(HKEY_CLASSES_ROOT, key);
}

std::vector<std::wstring> RegistryShellRootsForExclusionKey(const std::wstring& key)
{
    std::vector<std::wstring> roots;
    auto addRoot = [&roots](const std::wstring& root) {
        if (!root.empty() && std::find(roots.begin(), roots.end(), root) == roots.end()) {
            roots.push_back(root);
        }
    };

    if (key == L"desktop" || key == L"box_background") {
        addRoot(L"DesktopBackground\\Shell");
        addRoot(L"Directory\\Background\\shell");
        addRoot(L"Directory\\Background\\shellex\\ContextMenuHandlers");
    } else if (key == L"folder") {
        addRoot(L"Directory\\shell");
        addRoot(L"Folder\\shell");
        addRoot(L"AllFilesystemObjects\\shell");
    } else if (key == L"shortcut") {
        addRoot(L"lnkfile\\shell");
        addRoot(L"AllFilesystemObjects\\shell");
        addRoot(L"*\\shell");
    } else if (key == L"disk") {
        addRoot(L"Drive\\shell");
        addRoot(L"Folder\\shell");
    } else if (key == L"file_no_extension") {
        addRoot(L"*\\shell");
        addRoot(L"AllFilesystemObjects\\shell");
    } else if (key.rfind(L"file_ext:", 0) == 0) {
        const std::wstring extension = key.substr(wcslen(L"file_ext:"));
        const std::wstring progId = RegistryDefaultValue(extension);
        addRoot(extension + L"\\shell");
        if (!progId.empty()) {
            addRoot(progId + L"\\shell");
        }
        addRoot(L"*\\shell");
        addRoot(L"AllFilesystemObjects\\shell");
    }
    return roots;
}

HBITMAP ResolveFallbackMenuBitmap(IContextMenu3* contextMenu,
    UINT commandId,
    const std::wstring& menuText,
    const std::vector<std::wstring>& registryShellRoots)
{
    if (registryShellRoots.empty() || menuText.empty()) {
        return nullptr;
    }

    const std::wstring verb = CommandVerbForMenuItem(contextMenu, commandId);
    std::wstring cacheKey = registryShellRoots.front() + L"|" + menuText + L"|" + verb;

    static std::unordered_map<std::wstring, HBITMAP> s_cachedBitmaps;
    static std::unordered_set<std::wstring> s_negativeCache;
    static std::mutex s_cacheMutex;

    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        if (s_negativeCache.find(cacheKey) != s_negativeCache.end()) {
            return nullptr;
        }
        auto it = s_cachedBitmaps.find(cacheKey);
        if (it != s_cachedBitmaps.end()) {
            return CopyMenuBitmap(it->second);
        }
    }

    HBITMAP rawBitmap = BitmapFromRegistryShellRoots(registryShellRoots, menuText, verb);
    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        if (rawBitmap) {
            s_cachedBitmaps[cacheKey] = CopyMenuBitmap(rawBitmap);
        } else {
            s_negativeCache.insert(cacheKey);
        }
    }
    return rawBitmap;
}

void ResetSnapshotBitmap(ShellMenuSnapshotItem& item)
{
    if (item.ownsHBitmap && item.hBitmap) {
        DeleteObject(item.hBitmap);
    }
    item.hBitmap = nullptr;
    item.ownsHBitmap = false;
}

void CopySnapshotBitmap(ShellMenuSnapshotItem& target, const ShellMenuSnapshotItem& source)
{
    target.hBitmap = nullptr;
    target.ownsHBitmap = false;
    if (!source.hBitmap) {
        return;
    }

    if (source.ownsHBitmap) {
        target.hBitmap = CopyMenuBitmap(source.hBitmap);
        target.ownsHBitmap = target.hBitmap != nullptr;
    } else {
        target.hBitmap = source.hBitmap;
        target.ownsHBitmap = false;
    }
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

    qDebug().noquote() << QString::fromWCharArray(moduleName ? moduleName : L"Menu")
                       << QString::fromStdWString(info);
}

void EmitDesktopShellMenuListLine(const wchar_t* tag, const std::wstring& info)
{
    if (!IsBoxMenuDebugEnabled()) {
        return;
    }

    qDebug().noquote() << QStringLiteral("[DesktopShellMenuList]")
                       << QString::fromWCharArray(tag ? tag : L"Desktop")
                       << QString::fromStdWString(info);
}

std::wstring MenuStateToText(UINT state)
{
    std::wstring text;
    text += ((state & (MFS_DISABLED | MFS_GRAYED)) == 0) ? L"enabled=1" : L"enabled=0";
    text += (state & MFS_CHECKED) ? L" checked=1" : L" checked=0";
    text += (state & MFS_DEFAULT) ? L" default=1" : L" default=0";
    return text;
}

void DebugLogMenuTree(HMENU menu, IContextMenu3* contextMenu, const wchar_t* tag, int depth = 0)
{
    if (!menu || depth > 8 || !IsBoxMenuDebugEnabled()) {
        return;
    }

    const int itemCount = GetMenuItemCount(menu);
    if (depth == 0) {
        EmitDesktopShellMenuListLine(tag, L"begin count=" + std::to_wstring(itemCount));
    }

    for (int i = 0; i < itemCount; ++i) {
        wchar_t textBuffer[512] = {};
        MENUITEMINFOW itemInfo = {};
        itemInfo.cbSize = sizeof(itemInfo);
        itemInfo.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STATE | MIIM_STRING | MIIM_SUBMENU;
        itemInfo.dwTypeData = textBuffer;
        itemInfo.cch = ARRAYSIZE(textBuffer);

        if (!GetMenuItemInfoW(menu, static_cast<UINT>(i), TRUE, &itemInfo)) {
            EmitDesktopShellMenuListLine(tag,
                std::wstring(depth * 2, L' ')
                + L"depth=" + std::to_wstring(depth)
                + L" index=" + std::to_wstring(i)
                + L" read_failed");
            continue;
        }

        const bool isSeparator = (itemInfo.fType & MFT_SEPARATOR) == MFT_SEPARATOR;
        const bool hasSubMenu = itemInfo.hSubMenu != nullptr;
        std::wstring itemText = textBuffer;
        if (itemText.empty()) {
            itemText = isSeparator ? L"<separator>" : L"<empty>";
        }

        std::wstring line = std::wstring(depth * 2, L' ')
            + L"depth=" + std::to_wstring(depth)
            + L" index=" + std::to_wstring(i)
            + L" type=" + (isSeparator ? L"separator" : (hasSubMenu ? L"submenu" : L"action"))
            + L" id=" + std::to_wstring(itemInfo.wID)
            + L" " + MenuStateToText(itemInfo.fState)
            + L" text=\"" + itemText + L"\"";
        EmitDesktopShellMenuListLine(tag, line);
        if (hasSubMenu) {
            if (contextMenu) {
                SafeHandleMenuMsg2(contextMenu, WM_INITMENUPOPUP,
                    reinterpret_cast<WPARAM>(itemInfo.hSubMenu),
                    MAKELPARAM(i, FALSE),
                    nullptr);
            }
            MSG msg;
            for (int k = 0; k < 10; ++k)
            {
                while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
                {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }

                Sleep(1);
            }
            DebugLogMenuTree(itemInfo.hSubMenu, contextMenu, tag, depth + 1);
        }
    }

    if (depth == 0) {
        EmitDesktopShellMenuListLine(tag, L"end");
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

int FindNewMenuPosition(HMENU menu)
{
    if (!menu) {
        return -1;
    }

    const int newMenuPos = MenuHelper::FindMenuItem(menu, TEXT("Ne&w"));
    if (newMenuPos != -1) {
        return newMenuPos;
    }

    const int plainNewMenuPos = MenuHelper::FindMenuItem(menu, TEXT("New"));
    if (plainNewMenuPos != -1) {
        return plainNewMenuPos;
    }

    const int cnNewMenuPos = MenuHelper::FindMenuItem(menu, TEXT("新建"));
    if (cnNewMenuPos != -1) {
        return cnNewMenuPos;
    }

    return -1;
}

int FindNewMenuInsertPosition(HMENU menu)
{
    const int newMenuPos = FindNewMenuPosition(menu);
    return newMenuPos >= 0 ? newMenuPos + 1 : -1;
}

std::wstring MatchPathKey(const std::vector<std::wstring>& matchPath)
{
    std::wstring key;
    for (const std::wstring& segment : matchPath) {
        key.append(segment);
        key.push_back(L'\x1f');
    }
    return key;
}

bool MenuItemMatchesExclusion(const ShellContextMenu::CompiledExclusions& exclusions,
    const std::wstring& rawText,
    const std::wstring& normalizedText,
    const std::wstring& matchText,
    const std::vector<std::wstring>& matchPath)
{
    if (exclusions.empty) {
        return false;
    }
    if (!matchPath.empty() && exclusions.matchPath.find(MatchPathKey(matchPath)) != exclusions.matchPath.end()) {
        return true;
    }
    if (!matchText.empty() && exclusions.matchText.find(matchText) != exclusions.matchText.end()) {
        return true;
    }
    if (!rawText.empty() && exclusions.rawText.find(rawText) != exclusions.rawText.end()) {
        return true;
    }
    return !normalizedText.empty() && exclusions.normalizedText.find(normalizedText) != exclusions.normalizedText.end();
}

void RemoveConfiguredMenuItems(HMENU menu,
    const ShellContextMenu::CompiledExclusions& exclusions,
    const std::vector<std::wstring>& parentMatchPath = {})
{
    if (!menu || exclusions.empty) {
        return;
    }

    for (int i = GetMenuItemCount(menu) - 1; i >= 0; --i) {
        const std::wstring rawText = MenuHelper::GetMenuItemString(menu, static_cast<UINT>(i), true);
        if (rawText.empty()) {
            continue;
        }
        const std::wstring normalizedText = ShellContextMenu::NormalizeMenuText(rawText);
        const std::wstring matchText = ShellContextMenu::MatchMenuText(rawText);
        std::vector<std::wstring> currentMatchPath = parentMatchPath;
        currentMatchPath.push_back(matchText);

        MENUITEMINFOW itemInfo = {};
        itemInfo.cbSize = sizeof(itemInfo);
        itemInfo.fMask = MIIM_SUBMENU;
        if (GetMenuItemInfoW(menu, static_cast<UINT>(i), TRUE, &itemInfo) && itemInfo.hSubMenu) {
            RemoveConfiguredMenuItems(itemInfo.hSubMenu, exclusions, currentMatchPath);
        }

        const bool shouldDelete = MenuItemMatchesExclusion(exclusions, rawText, normalizedText, matchText, currentMatchPath);
        if (shouldDelete && DeleteMenu(menu, static_cast<UINT>(i), MF_BYPOSITION)) {
            MenuHelper::RemoveAdjacentSeparators(menu);
            MenuHelper::RemoveTrailingSeparators(menu);
        }
    }
}

bool IsValidShellCommandId(UINT shellCommandId)
{
    return shellCommandId >= MIN_SHELL_MENU_ID && shellCommandId <= MAX_SHELL_MENU_ID;
}

bool HasActionItems(const std::vector<ShellMenuSnapshotItem>& items)
{
    for (const ShellMenuSnapshotItem& item : items) {
        if (!item.isSeparator) {
            return true;
        }
    }
    return false;
}

bool IsSendToMenuText(const std::wstring& text)
{
    const std::wstring normalized = ShellContextMenu::NormalizeMenuText(text);
    return normalized == L"send to" ||
        normalized.find(L"send to") != std::wstring::npos ||
        normalized.find(L"发送到") != std::wstring::npos;
}

std::wstring ShellDisplayNameForPath(const std::filesystem::path& path)
{
    SHFILEINFOW info = {};
    if (SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_DISPLAYNAME)) {
        std::wstring displayName = info.szDisplayName;
        if (!displayName.empty()) {
            return displayName;
        }
    }

    std::wstring fallback = path.stem().wstring();
    return fallback.empty() ? path.filename().wstring() : fallback;
}

HBITMAP ShellSmallIconBitmapForPath(const std::filesystem::path& path)
{
    SHFILEINFOW info = {};
    if (!SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_ICON | SHGFI_SMALLICON) || !info.hIcon) {
        return nullptr;
    }

    HBITMAP bitmap = BitmapFromIcon(info.hIcon);
    DestroyIcon(info.hIcon);
    return bitmap;
}

void AppendSendToFallbackItems(ShellMenuSnapshotItem& parentItem)
{
    if (!IsSendToMenuText(parentItem.text) || HasActionItems(parentItem.children)) {
        return;
    }

    PWSTR sendToPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_SendTo, 0, nullptr, &sendToPath)) || !sendToPath) {
        return;
    }

    const std::filesystem::path sendToDir(sendToPath);
    CoTaskMemFree(sendToPath);

    std::error_code ec;
    if (!std::filesystem::exists(sendToDir, ec) || ec) {
        return;
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(sendToDir, ec)) {
        if (ec) {
            break;
        }

        const std::filesystem::path path = entry.path();
        const std::wstring fileName = path.filename().wstring();
        if (fileName.empty() || _wcsicmp(fileName.c_str(), L"desktop.ini") == 0) {
            continue;
        }

        ShellMenuSnapshotItem item;
        item.text = ShellDisplayNameForPath(path);
        item.normalizedText = ShellContextMenu::NormalizeMenuText(item.text);
        item.matchText = ShellContextMenu::MatchMenuText(item.text);
        item.menuPath = parentItem.menuPath;
        item.menuPath.push_back(item.text);
        item.matchPath = parentItem.matchPath;
        item.matchPath.push_back(item.matchText);
        item.source = L"sendto";
        item.enabled = true;
        item.hBitmap = ShellSmallIconBitmapForPath(path);
        item.ownsHBitmap = item.hBitmap != nullptr;
        parentItem.children.push_back(std::move(item));
    }
}

void TrimSnapshotSeparators(std::vector<ShellMenuSnapshotItem>& items)
{
    while (!items.empty() && items.front().isSeparator) {
        items.erase(items.begin());
    }
    while (!items.empty() && items.back().isSeparator) {
        items.pop_back();
    }
    for (int i = static_cast<int>(items.size()) - 1; i > 0; --i) {
        if (items[i].isSeparator && items[i - 1].isSeparator) {
            items.erase(items.begin() + i);
        }
    }
}

std::wstring NormalizeMenuTextForFilter(const std::wstring& text)
{
    std::wstring normalized;
    normalized.reserve(text.size());

    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t ch = text[i];
        if (ch == L'\t') {
            break;
        }
        if (ch == L'&') {
            continue;
        }
        if (ch == L'(' && i + 2 < text.size() && text[i + 1] == L'&') {
            while (i < text.size() && text[i] != L')') {
                ++i;
            }
            continue;
        }
        normalized.push_back(static_cast<wchar_t>(std::towlower(ch)));
    }

    const auto isSpace = [](wchar_t ch) { return std::iswspace(ch) != 0; };
    while (!normalized.empty() && isSpace(normalized.front())) {
        normalized.erase(normalized.begin());
    }
    while (!normalized.empty() && isSpace(normalized.back())) {
        normalized.pop_back();
    }

    return normalized;
}

std::filesystem::path GetUserConfigPath()
{
    wchar_t appData[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appData))) {
        return {};
    }

    std::filesystem::path dir(appData);
    dir /= L"LinDesk";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    dir /= L"config.ini";
    return dir;
}

std::filesystem::path GetShellMenuSampleDir()
{
    wchar_t appData[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appData))) {
        return {};
    }

    std::filesystem::path dir(appData);
    dir /= L"LinDesk";
    dir /= L"ShellMenuSamples";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::wstring EscapeConfigValue(const std::wstring& value)
{
    std::wstring output;
    output.reserve(value.size());
    for (wchar_t ch : value) {
        if (ch == L'\\' || ch == L'|' || ch == L'\n' || ch == L'\r') {
            output.push_back(L'\\');
            if (ch == L'\n') {
                output.push_back(L'n');
            } else if (ch == L'\r') {
                output.push_back(L'r');
            } else {
                output.push_back(ch);
            }
        } else {
            output.push_back(ch);
        }
    }
    return output;
}

std::wstring UnescapeConfigValue(const std::wstring& value)
{
    std::wstring output;
    output.reserve(value.size());
    bool escaped = false;
    for (wchar_t ch : value) {
        if (!escaped && ch == L'\\') {
            escaped = true;
            continue;
        }
        if (escaped && ch == L'n') {
            output.push_back(L'\n');
        } else if (escaped && ch == L'r') {
            output.push_back(L'\r');
        } else {
            output.push_back(ch);
        }
        escaped = false;
    }
    if (escaped) {
        output.push_back(L'\\');
    }
    return output;
}

std::wstring ConfigNameForKey(const std::wstring& key)
{
    std::wstring output = L"exclude.";
    for (wchar_t ch : key) {
        if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' || ch == L'?' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|' || ch == L'.') {
            output.push_back(L'_');
        } else {
            output.push_back(static_cast<wchar_t>(std::towlower(ch)));
        }
    }
    return output;
}

std::vector<std::wstring> SplitConfigList(const std::wstring& value)
{
    std::vector<std::wstring> items;
    std::wstring current;
    bool escaped = false;
    for (wchar_t ch : value) {
        if (!escaped && ch == L'\\') {
            escaped = true;
            current.push_back(ch);
            continue;
        }
        if (!escaped && ch == L';') {
            items.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(ch);
        escaped = false;
    }
    if (!current.empty()) {
        items.push_back(current);
    }
    return items;
}

std::vector<std::wstring> SplitFields(const std::wstring& value)
{
    std::vector<std::wstring> fields;
    std::wstring current;
    bool escaped = false;
    for (wchar_t ch : value) {
        if (!escaped && ch == L'\\') {
            escaped = true;
            current.push_back(ch);
            continue;
        }
        if (!escaped && ch == L'|') {
            fields.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(ch);
        escaped = false;
    }
    fields.push_back(current);
    return fields;
}

std::wstring ReadProfileStringValue(const std::wstring& name)
{
    const std::filesystem::path configPath = GetUserConfigPath();
    if (configPath.empty()) {
        return {};
    }

    std::wstring buffer(32768, L'\0');
    const DWORD length = GetPrivateProfileStringW(
        kShellMenuExclusionSection,
        name.c_str(),
        L"",
        buffer.data(),
        static_cast<DWORD>(buffer.size()),
        configPath.c_str());
    buffer.resize(length);
    return buffer;
}

void WriteProfileStringValue(const std::wstring& name, const std::wstring& value)
{
    const std::filesystem::path configPath = GetUserConfigPath();
    if (configPath.empty()) {
        return;
    }

    WritePrivateProfileStringW(kShellMenuExclusionSection, name.c_str(), value.c_str(), configPath.c_str());
}

std::wstring FileExtensionLower(const wchar_t* path)
{
    if (!path || *path == L'\0') {
        return {};
    }

    const wchar_t* extension = PathFindExtensionW(path);
    if (!extension || *extension == L'\0') {
        return {};
    }

    std::wstring output(extension);
    std::transform(output.begin(), output.end(), output.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return output;
}

std::wstring ProgIdForPath(const wchar_t* path)
{
    const std::wstring extension = FileExtensionLower(path);
    if (extension.empty()) {
        return {};
    }

    wchar_t progId[512] = {};
    DWORD size = sizeof(progId);
    if (RegGetValueW(HKEY_CLASSES_ROOT, extension.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, progId, &size) != ERROR_SUCCESS) {
        return {};
    }
    return progId;
}

std::filesystem::path KnownFolderPath(REFKNOWNFOLDERID folderId)
{
    PWSTR path = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(folderId, 0, nullptr, &path);
    if (FAILED(hr) || !path) {
        return {};
    }

    std::filesystem::path result(path);
    CoTaskMemFree(path);
    return result;
}

bool IsExistingDirectory(const std::filesystem::path& path)
{
    if (path.empty()) {
        return false;
    }

    std::error_code ec;
    return std::filesystem::exists(path, ec) && !ec && std::filesystem::is_directory(path, ec) && !ec;
}

std::filesystem::path FirstExistingDirectory(std::initializer_list<KNOWNFOLDERID> folderIds)
{
    for (const KNOWNFOLDERID& folderId : folderIds) {
        const std::filesystem::path path = KnownFolderPath(folderId);
        if (IsExistingDirectory(path)) {
            return path;
        }
    }
    return {};
}

bool IsShortcutPath(const std::filesystem::path& path)
{
    return _wcsicmp(path.extension().c_str(), L".lnk") == 0;
}

std::filesystem::path FindFirstShortcutInDirectory(const std::filesystem::path& dir, bool recursive)
{
    if (!IsExistingDirectory(dir)) {
        return {};
    }

    std::error_code ec;
    if (!recursive) {
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (!ec && entry.is_regular_file(ec) && !ec && IsShortcutPath(entry.path())) {
                return entry.path();
            }
        }
        return {};
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (!ec && entry.is_regular_file(ec) && !ec && IsShortcutPath(entry.path())) {
            return entry.path();
        }
    }
    return {};
}

std::filesystem::path FindRealShortcutSample()
{
    const std::filesystem::path desktopShortcut = FindFirstShortcutInDirectory(KnownFolderPath(FOLDERID_Desktop), false);
    if (!desktopShortcut.empty()) {
        return desktopShortcut;
    }

    const std::filesystem::path commonDesktopShortcut = FindFirstShortcutInDirectory(KnownFolderPath(FOLDERID_PublicDesktop), false);
    if (!commonDesktopShortcut.empty()) {
        return commonDesktopShortcut;
    }

    const std::filesystem::path startMenuShortcut = FindFirstShortcutInDirectory(KnownFolderPath(FOLDERID_StartMenu), true);
    if (!startMenuShortcut.empty()) {
        return startMenuShortcut;
    }

    return FindFirstShortcutInDirectory(KnownFolderPath(FOLDERID_CommonStartMenu), true);
}

std::filesystem::path EnsureSampleFile(const std::wstring& key)
{
    const std::filesystem::path dir = GetShellMenuSampleDir();
    if (dir.empty()) {
        return {};
    }

    std::filesystem::path samplePath;
    if (key.rfind(L"file_ext:", 0) == 0) {
        const std::wstring extension = key.substr(wcslen(L"file_ext:"));
        samplePath = dir / (L"sample" + extension);
    } else if (key == L"file_no_extension") {
        samplePath = dir / L"sample_no_extension";
    }

    if (!samplePath.empty() && !std::filesystem::exists(samplePath)) {
        std::ofstream sample(samplePath, std::ios::binary);
        sample << "LinDesk shell menu sample";
    }
    return samplePath;
}

std::filesystem::path EnsureSampleFolder()
{
    const std::filesystem::path realFolder = FirstExistingDirectory({
        FOLDERID_Desktop,
        FOLDERID_Documents,
        FOLDERID_Downloads,
        FOLDERID_Profile
    });
    if (!realFolder.empty()) {
        return realFolder;
    }

    const std::filesystem::path dir = GetShellMenuSampleDir();
    if (dir.empty()) {
        return {};
    }
    const std::filesystem::path folder = dir / L"SampleFolder";
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    return folder;
}

std::filesystem::path EnsureSampleShortcut()
{
    const std::filesystem::path realShortcut = FindRealShortcutSample();
    if (!realShortcut.empty()) {
        return realShortcut;
    }

    const std::filesystem::path dir = GetShellMenuSampleDir();
    if (dir.empty()) {
        return {};
    }
    const std::filesystem::path shortcut = dir / L"SampleShortcut.lnk";

    wchar_t systemDir[MAX_PATH] = {};
    if (GetSystemDirectoryW(systemDir, ARRAYSIZE(systemDir))) {
        const std::filesystem::path cmdPath = std::filesystem::path(systemDir) / L"cmd.exe";
        wil::com_ptr_nothrow<IShellLinkW> shellLink;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink))) && shellLink) {
            shellLink->SetPath(cmdPath.c_str());
            shellLink->SetWorkingDirectory(systemDir);
            shellLink->SetDescription(L"LinDesk shell menu sample shortcut");

            wil::com_ptr_nothrow<IPersistFile> persistFile;
            if (SUCCEEDED(shellLink.query_to(&persistFile)) && persistFile &&
                SUCCEEDED(persistFile->Save(shortcut.c_str(), TRUE))) {
                return shortcut;
            }
        }
    }

    if (!std::filesystem::exists(shortcut)) {
        std::ofstream sample(shortcut, std::ios::binary);
    }
    return shortcut;
}

bool IsSystemDesktopBackgroundMenuText(const std::wstring& text)
{
    const std::wstring normalizedText = NormalizeMenuTextForFilter(text);
    if (normalizedText.empty()) {
        return true;
    }

    const wchar_t* systemMenuTexts[] = {
        L"View",
        L"查看",
        L"Sort by",
        L"排序方式",
        L"排序",
        L"Refresh",
        L"刷新",
        L"Paste",
        L"粘贴",
        L"Paste shortcut",
        L"粘贴快捷方式",
        L"New",
        L"新建",
        L"Browse background",
        L"浏览背景",
        L"Next background",
        L"下一个背景",
        L"Next desktop background",
        L"下一个桌面背景",
        L"Display settings",
        L"显示设置",
        L"Personalize",
        L"个性化",
        L"Show more options",
        L"显示更多选项",
        L"Open in Terminal",
        L"在终端中打开",
        L"Open in Windows Terminal",
        L"在 Windows 终端中打开",
        L"Open Windows Terminal here",
        L"Desktop organizer",
        L"Organize Desktop",
        L"整理桌面"
    };

    for (const wchar_t* systemText : systemMenuTexts) {
        if (normalizedText == NormalizeMenuTextForFilter(systemText)) {
            return true;
        }
    }

    return false;
}

void AppendMenuSnapshotItems(HMENU menu,
    IContextMenu3* contextMenu,
    std::vector<ShellMenuSnapshotItem>& outputItems,
    std::map<std::string, UINT>& commandMap,
    const char* commandPrefix,
    bool filterSystemItems,
    const std::vector<std::wstring>& registryShellRoots,
    const std::vector<std::wstring>& parentMenuPath,
    const std::vector<std::wstring>& parentMatchPath)
{
    if (!menu) {
        return;
    }

    const int itemCount = GetMenuItemCount(menu);
    for (int i = 0; i < itemCount; ++i) {
        wchar_t textBuffer[512] = {};
        MENUITEMINFOW itemInfo = {};
        itemInfo.cbSize = sizeof(itemInfo);
        itemInfo.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STATE | MIIM_STRING | MIIM_BITMAP | MIIM_SUBMENU;
        itemInfo.dwTypeData = textBuffer;
        itemInfo.cch = ARRAYSIZE(textBuffer);

        if (!GetMenuItemInfoW(menu, static_cast<UINT>(i), TRUE, &itemInfo)) {
            continue;
        }

        ShellMenuSnapshotItem snapshotItem;
        snapshotItem.isSeparator = (itemInfo.fType & MFT_SEPARATOR) == MFT_SEPARATOR;
        if (snapshotItem.isSeparator) {
            outputItems.push_back(snapshotItem);
            continue;
        }

        snapshotItem.text = textBuffer;
        snapshotItem.normalizedText = ShellContextMenu::NormalizeMenuText(snapshotItem.text);
        snapshotItem.matchText = ShellContextMenu::MatchMenuText(snapshotItem.text);
        snapshotItem.menuPath = parentMenuPath;
        snapshotItem.menuPath.push_back(snapshotItem.text);
        snapshotItem.matchPath = parentMatchPath;
        snapshotItem.matchPath.push_back(snapshotItem.matchText);
        snapshotItem.source = L"system";
        if (filterSystemItems && IsSystemDesktopBackgroundMenuText(snapshotItem.text)) {
            continue;
        }

        snapshotItem.enabled = (itemInfo.fState & (MFS_DISABLED | MFS_GRAYED)) == 0;
        snapshotItem.checked = (itemInfo.fState & MFS_CHECKED) == MFS_CHECKED;
        snapshotItem.hBitmap = CopyMenuBitmap(itemInfo.hbmpItem);
        snapshotItem.ownsHBitmap = snapshotItem.hBitmap != nullptr;
        if (!snapshotItem.hBitmap && IsValidShellCommandId(itemInfo.wID)) {
            snapshotItem.hBitmap = ResolveFallbackMenuBitmap(contextMenu, itemInfo.wID, snapshotItem.text, registryShellRoots);
            snapshotItem.ownsHBitmap = snapshotItem.hBitmap != nullptr;
        }

        if (itemInfo.hSubMenu) {
            if (contextMenu) {
                SafeHandleMenuMsg2(contextMenu, WM_INITMENUPOPUP,
                    reinterpret_cast<WPARAM>(itemInfo.hSubMenu),
                    MAKELPARAM(i, FALSE),
                    nullptr);
            }
            AppendMenuSnapshotItems(itemInfo.hSubMenu,
                contextMenu,
                snapshotItem.children,
                commandMap,
                commandPrefix,
                false,
                registryShellRoots,
                snapshotItem.menuPath,
                snapshotItem.matchPath);
            AppendSendToFallbackItems(snapshotItem);
            TrimSnapshotSeparators(snapshotItem.children);
            if (!HasActionItems(snapshotItem.children)) {
                continue;
            }
            outputItems.push_back(snapshotItem);
            continue;
        }

        if (IsValidShellCommandId(itemInfo.wID)) {
            snapshotItem.commandId = std::string(commandPrefix) + std::to_string(itemInfo.wID);
            commandMap[snapshotItem.commandId] = itemInfo.wID;
        }
        outputItems.push_back(snapshotItem);
    }

    TrimSnapshotSeparators(outputItems);
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
        L"Give access to",
        L"授予访问权限",
        L"Properties",
        L"属性",
        L"Customize this folder...",
        L"自定义此文件夹...",
        L"Customize this folder",
        L"自定义此文件夹",
    };

    for (int i = GetMenuItemCount(menu) - 1; i >= 0; --i) {
        const std::wstring normalizedText = normalizeMenuText(MenuHelper::GetMenuItemString(menu, i, true));
        if (normalizedText.empty()) {
            continue;
        }

        bool shouldDelete = false;
        for (const auto* candidate : exactCandidates) {
            if (_wcsicmp(normalizedText.c_str(), normalizeMenuText(candidate).c_str()) == 0) {
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

ShellMenuSnapshotItem::ShellMenuSnapshotItem(const ShellMenuSnapshotItem& other)
    : text(other.text),
      normalizedText(other.normalizedText),
      matchText(other.matchText),
      source(other.source),
      commandId(other.commandId),
      menuPath(other.menuPath),
      matchPath(other.matchPath),
      children(other.children),
      isSeparator(other.isSeparator),
      enabled(other.enabled),
      checked(other.checked)
{
    CopySnapshotBitmap(*this, other);
}

ShellMenuSnapshotItem& ShellMenuSnapshotItem::operator=(const ShellMenuSnapshotItem& other)
{
    if (this == &other) {
        return *this;
    }

    ResetSnapshotBitmap(*this);
    text = other.text;
    normalizedText = other.normalizedText;
    matchText = other.matchText;
    source = other.source;
    commandId = other.commandId;
    menuPath = other.menuPath;
    matchPath = other.matchPath;
    children = other.children;
    isSeparator = other.isSeparator;
    enabled = other.enabled;
    checked = other.checked;
    CopySnapshotBitmap(*this, other);
    return *this;
}

ShellMenuSnapshotItem::ShellMenuSnapshotItem(ShellMenuSnapshotItem&& other) noexcept
    : text(std::move(other.text)),
      normalizedText(std::move(other.normalizedText)),
      matchText(std::move(other.matchText)),
      source(std::move(other.source)),
      commandId(std::move(other.commandId)),
      menuPath(std::move(other.menuPath)),
      matchPath(std::move(other.matchPath)),
      children(std::move(other.children)),
      isSeparator(other.isSeparator),
      enabled(other.enabled),
      checked(other.checked),
      hBitmap(other.hBitmap),
      ownsHBitmap(other.ownsHBitmap)
{
    other.hBitmap = nullptr;
    other.ownsHBitmap = false;
}

ShellMenuSnapshotItem& ShellMenuSnapshotItem::operator=(ShellMenuSnapshotItem&& other) noexcept
{
    if (this == &other) {
        return *this;
    }

    ResetSnapshotBitmap(*this);
    text = std::move(other.text);
    normalizedText = std::move(other.normalizedText);
    matchText = std::move(other.matchText);
    source = std::move(other.source);
    commandId = std::move(other.commandId);
    menuPath = std::move(other.menuPath);
    matchPath = std::move(other.matchPath);
    children = std::move(other.children);
    isSeparator = other.isSeparator;
    enabled = other.enabled;
    checked = other.checked;
    hBitmap = other.hBitmap;
    ownsHBitmap = other.ownsHBitmap;
    other.hBitmap = nullptr;
    other.ownsHBitmap = false;
    return *this;
}

ShellMenuSnapshotItem::~ShellMenuSnapshotItem()
{
    ResetSnapshotBitmap(*this);
}

std::wstring ShellContextMenu::NormalizeMenuText(const std::wstring& text)
{
    return NormalizeMenuTextForFilter(text);
}

std::wstring ShellContextMenu::MatchMenuText(const std::wstring& text)
{
    std::wstring output;
    output.reserve(text.size());
    bool inQuote = false;
    wchar_t quoteChar = L'\0';
    std::wstring quotedText;

    auto flushQuoted = [&]() {
        if (LooksLikeDynamicFileName(quotedText)) {
            output += L"{target}";
        } else {
            output += quotedText;
        }
        quotedText.clear();
    };

    for (size_t i = 0; i < text.size(); ++i) {
        const wchar_t ch = text[i];
        if (!inQuote && ch == L'\t') {
            break;
        }
        if (ch == L'&') {
            continue;
        }

        const bool isOpeningQuote = !inQuote && (ch == L'"' || ch == L'\'' || ch == L'“' || ch == L'‘');
        const bool isClosingQuote = inQuote &&
            ((quoteChar == L'"' && ch == L'"') ||
                (quoteChar == L'\'' && ch == L'\'') ||
                (quoteChar == L'“' && ch == L'”') ||
                (quoteChar == L'‘' && ch == L'’'));

        if (isOpeningQuote) {
            inQuote = true;
            quoteChar = ch;
            output.push_back(ch);
            continue;
        }

        if (isClosingQuote) {
            flushQuoted();
            output.push_back(ch);
            inQuote = false;
            quoteChar = L'\0';
            continue;
        }

        if (inQuote) {
            quotedText.push_back(ch);
        } else {
            output.push_back(static_cast<wchar_t>(std::towlower(ch)));
        }
    }

    if (inQuote) {
        output += quotedText;
    }

    while (!output.empty() && std::iswspace(output.front())) {
        output.erase(output.begin());
    }
    while (!output.empty() && std::iswspace(output.back())) {
        output.pop_back();
    }
    return output;
}

bool ShellContextMenu::MenuEntryMatches(const ShellMenuExclusionEntry& exclusion,
    const std::wstring& rawText,
    const std::wstring& normalizedText,
    const std::wstring& matchText,
    const std::vector<std::wstring>& matchPath)
{
    if (!exclusion.enabled) {
        return false;
    }
    if (!exclusion.matchPath.empty() && !matchPath.empty() && exclusion.matchPath == matchPath) {
        return true;
    }
    if (!exclusion.matchText.empty() && !matchText.empty() && exclusion.matchText == matchText) {
        return true;
    }
    if (!exclusion.rawText.empty() && rawText == exclusion.rawText) {
        return true;
    }
    return !exclusion.normalizedText.empty() && normalizedText == exclusion.normalizedText;
}

void ShellContextMenu::rebuildCompiledExclusions()
{
    m_compiledExclusions = CompiledExclusions();
    for (const ShellMenuExclusionEntry& exclusion : m_exclusions) {
        if (!exclusion.enabled) {
            continue;
        }
        if (!exclusion.rawText.empty()) {
            m_compiledExclusions.rawText.insert(exclusion.rawText);
        }
        if (!exclusion.normalizedText.empty()) {
            m_compiledExclusions.normalizedText.insert(exclusion.normalizedText);
        }
        if (!exclusion.matchText.empty()) {
            m_compiledExclusions.matchText.insert(exclusion.matchText);
        }
        if (!exclusion.matchPath.empty()) {
            m_compiledExclusions.matchPath.insert(MatchPathKey(exclusion.matchPath));
        }
    }
    m_compiledExclusions.empty =
        m_compiledExclusions.rawText.empty() &&
        m_compiledExclusions.normalizedText.empty() &&
        m_compiledExclusions.matchText.empty() &&
        m_compiledExclusions.matchPath.empty();
}

std::vector<std::wstring> ShellContextMenu::ExclusionKeysForPath(OperationType type, const wchar_t* path)
{
    std::vector<std::wstring> keys;
    switch (type) {
    case OperationType::BackGround:
        keys.push_back(L"desktop");
        break;
    case OperationType::boxBackground:
        keys.push_back(L"box_background");
        break;
    case OperationType::folder:
        keys.push_back(L"folder");
        break;
    case OperationType::link:
        keys.push_back(L"shortcut");
        break;
    case OperationType::diskLetter:
        keys.push_back(L"disk");
        break;
    case OperationType::file: {
        const std::wstring progId = ProgIdForPath(path);
        if (!progId.empty()) {
            keys.push_back(L"file_progid:" + progId);
        }
        const std::wstring extension = FileExtensionLower(path);
        if (!extension.empty()) {
            keys.push_back(L"file_ext:" + extension);
        } else {
            keys.push_back(L"file_no_extension");
        }
        break;
    }
    default:
        break;
    }
    return keys;
}

std::vector<ShellMenuExclusionEntry> ShellContextMenu::LoadExclusions(const std::wstring& key)
{
    std::vector<ShellMenuExclusionEntry> entries;
    const std::wstring stored = ReadProfileStringValue(ConfigNameForKey(key));
    for (const std::wstring& item : SplitConfigList(stored)) {
        const std::vector<std::wstring> fields = SplitFields(item);
        if (fields.empty()) {
            continue;
        }

        ShellMenuExclusionEntry entry;
        entry.rawText = UnescapeConfigValue(fields[0]);
        entry.normalizedText = fields.size() > 1
            ? UnescapeConfigValue(fields[1])
            : ShellContextMenu::NormalizeMenuText(entry.rawText);
        entry.enabled = fields.size() <= 2 || fields[2] != L"0";
        if (!entry.rawText.empty() || !entry.normalizedText.empty()) {
            entries.push_back(entry);
        }
    }
    return entries;
}

void ShellContextMenu::SaveExclusions(const std::wstring& key, const std::vector<ShellMenuExclusionEntry>& entries)
{
    std::wstring stored;
    bool first = true;
    for (const ShellMenuExclusionEntry& entry : entries) {
        if (!first) {
            stored.push_back(L';');
        }
        first = false;
        stored += EscapeConfigValue(entry.rawText);
        stored.push_back(L'|');
        stored += EscapeConfigValue(entry.normalizedText);
        stored.push_back(L'|');
        stored += entry.enabled ? L"1" : L"0";
    }
    WriteProfileStringValue(ConfigNameForKey(key), stored);
}

void ShellContextMenu::AddExclusion(const std::wstring& key, const std::wstring& rawText)
{
    if (key.empty() || rawText.empty()) {
        return;
    }

    std::vector<ShellMenuExclusionEntry> entries = LoadExclusions(key);
    const std::wstring normalized = NormalizeMenuText(rawText);
    for (const ShellMenuExclusionEntry& entry : entries) {
        if (entry.rawText == rawText || entry.normalizedText == normalized) {
            return;
        }
    }

    ShellMenuExclusionEntry entry;
    entry.rawText = rawText;
    entry.normalizedText = normalized;
    entry.enabled = true;
    entries.push_back(entry);
    SaveExclusions(key, entries);
}

void ShellContextMenu::RemoveExclusion(const std::wstring& key, int index)
{
    std::vector<ShellMenuExclusionEntry> entries = LoadExclusions(key);
    if (index < 0 || index >= static_cast<int>(entries.size())) {
        return;
    }
    entries.erase(entries.begin() + index);
    SaveExclusions(key, entries);
}

std::vector<ShellMenuSnapshotItem> ShellContextMenu::SnapshotMenuForExclusionKey(const std::wstring& key)
{
    const std::wstring logTag = L"SettingsShellScan:" + key;
    EmitDesktopShellMenuListLine(logTag.c_str(), L"start");

    ShellContextMenu menu;
    if (key == L"desktop") {
        menu.DisplayDesktopContextMenu();
        menu.m_contextMenu = menu.MaybeGetShellContextMenu(GetDesktopWindow());
    } else if (key == L"box_background") {
        menu.DisplayBoxBackgroundContextMenu();
        menu.m_contextMenu = menu.MaybeGetShellContextMenu(GetDesktopWindow());
    } else if (key == L"folder") {
        const std::filesystem::path folder = EnsureSampleFolder();
        EmitDesktopShellMenuListLine(logTag.c_str(), L"source folder=\"" + folder.wstring() + L"\"");
        menu.DisplayFolderContextMenu(folder.c_str());
    } else if (key == L"shortcut") {
        const std::filesystem::path shortcut = EnsureSampleShortcut();
        EmitDesktopShellMenuListLine(logTag.c_str(), L"source shortcut=\"" + shortcut.wstring() + L"\"");
        menu.DisplayLinkContextMenu(shortcut.c_str());
    } else if (key == L"disk") {
        EmitDesktopShellMenuListLine(logTag.c_str(), L"source disk=\"C:\\\"");
        menu.DisplayFolderContextMenu(L"C:\\");
    } else if (key == L"file_no_extension" || key.rfind(L"file_ext:", 0) == 0) {
        const std::filesystem::path sample = EnsureSampleFile(key);
        EmitDesktopShellMenuListLine(logTag.c_str(), L"source file=\"" + sample.wstring() + L"\"");
        menu.DisplayFileContextMenu(sample.c_str());
    } else if (key.rfind(L"file_progid:", 0) == 0) {
        EmitDesktopShellMenuListLine(logTag.c_str(), L"skip unsupported file_progid");
        return {};
    } else {
        EmitDesktopShellMenuListLine(logTag.c_str(), L"skip unknown key");
        return {};
    }

    if (!menu.m_contextMenu) {
        EmitDesktopShellMenuListLine(logTag.c_str(), L"no context menu");
        return {};
    }

    wil::unique_hmenu hMenu(CreatePopupMenu());
    if (!hMenu) {
        EmitDesktopShellMenuListLine(logTag.c_str(), L"create popup menu failed");
        return {};
    }

    const HRESULT queryHr = SafeQueryContextMenu(
        menu.m_contextMenu.get(),
        hMenu.get(),
        0,
        MIN_SHELL_MENU_ID,
        MAX_SHELL_MENU_ID,
        CMF_NORMAL | CMF_CANRENAME);
    if (FAILED(queryHr)) {
        EmitDesktopShellMenuListLine(logTag.c_str(), L"QueryContextMenu failed hr=" + std::to_wstring(static_cast<long>(queryHr)));
        return {};
    }

    menu.RemoveTencentDeskGoMenu(hMenu.get());
    DebugLogMenuTree(hMenu.get(), menu.m_contextMenu.get(), logTag.c_str());

    std::vector<ShellMenuSnapshotItem> items;
    std::map<std::string, UINT> commandMap;
    AppendMenuSnapshotItems(hMenu.get(),
        menu.m_contextMenu.get(),
        items,
        commandMap,
        "scan:",
        false,
        RegistryShellRootsForExclusionKey(key),
        {},
        {});
    EmitDesktopShellMenuListLine(logTag.c_str(), L"finish output_count=" + std::to_wstring(items.size()));
    return items;
}

std::wstring ShellContextMenu::DisplayNameForExclusionKey(const std::wstring& key)
{
    if (key == L"desktop") return L"Desktop";
    if (key == L"box_background") return L"Box Background";
    if (key == L"folder") return L"Folder";
    if (key == L"shortcut") return L"Shortcut";
    if (key == L"disk") return L"Disk";
    if (key == L"file_no_extension") return L"No Extension File";
    if (key.rfind(L"file_ext:", 0) == 0) return key.substr(wcslen(L"file_ext:"));
    if (key.rfind(L"file_progid:", 0) == 0) return L"ProgID " + key.substr(wcslen(L"file_progid:"));
    return key;
}

void ShellContextMenu::DisplayDesktopContextMenu()
{
    m_contextMenu = nullptr;
    operationType = OperationType::BackGround;
    if (FAILED(SHGetKnownFolderIDList(FOLDERID_Desktop, 0, NULL, wil::out_param(m_pidlParent)))) {
        m_pidlParent.reset();
    }
}

void ShellContextMenu::DisplayBoxBackgroundContextMenu(const std::wstring& folderPath)
{
    m_contextMenu = nullptr;
    operationType = OperationType::boxBackground;
    if (!folderPath.empty() && PathFileExistsW(folderPath.c_str())) {
        PIDLIST_ABSOLUTE pidl = nullptr;
        HRESULT hr = SHParseDisplayName(folderPath.c_str(), NULL, &pidl, 0, NULL);
        if (SUCCEEDED(hr) && pidl) {
            m_pidlParent.reset(pidl);
            setFilesystemDirectory(Tool::WstrToStr(folderPath.c_str()));
            return;
        }
        pidl = ILCreateFromPathW(folderPath.c_str());
        if (pidl) {
            m_pidlParent.reset(pidl);
            setFilesystemDirectory(Tool::WstrToStr(folderPath.c_str()));
            return;
        }
    }
    if (FAILED(SHGetKnownFolderIDList(FOLDERID_Desktop, 0, NULL, wil::out_param(m_pidlParent)))) {
        m_pidlParent.reset();
    }
}

void ShellContextMenu::DisplayItemContextMenu(LPITEMIDLIST pidl, HWND hwndOwner)
{
    Q_UNUSED(hwndOwner);
    m_contextMenu = nullptr;
    if (!pidl || operationType == OperationType::BackGround || operationType == OperationType::boxBackground) {
        return;
    }
    if (!GetDesktop_IshellFolder() || !m_pDesktopFolder) {
        return;
    }
    // 还原 1.0.5 模式：传入 nullptr 作为 hwndOwner，触发轻量快速查询通道
    m_contextMenu = IContextMenuFromIShellFolder(pidl, m_pDesktopFolder.get(), nullptr);
}

void ShellContextMenu::DisplayLinkContextMenu(const wchar_t* path) {
    m_contextMenu = nullptr;
    operationType = OperationType::link;
    if (!path || !*path) return;
    if (!GetDesktop_IshellFolder() || !m_pDesktopFolder) return;

    unique_pidl_absolute itemPidl;
    HRESULT hr = SHParseDisplayName(path, nullptr, wil::out_param(itemPidl), 0, nullptr);
    if (SUCCEEDED(hr) && itemPidl) {
        DisplayItemContextMenu(itemPidl.get(), nullptr);
    }
}

void ShellContextMenu::DisplayFileContextMenu(const wchar_t* path)
{
    m_contextMenu = nullptr;
    operationType = OperationType::file;
    if (!path || !*path) return;
    if (!GetDesktop_IshellFolder() || !m_pDesktopFolder) return;

    unique_pidl_absolute itemPidl;
    HRESULT hr = SHParseDisplayName(path, nullptr, wil::out_param(itemPidl), 0, nullptr);
    if (SUCCEEDED(hr) && itemPidl) {
        DisplayItemContextMenu(itemPidl.get(), nullptr);
    }
}

void ShellContextMenu::DisplayFolderContextMenu(const wchar_t* folderPath)
{
    m_contextMenu = nullptr;
    operationType = OperationType::folder;
    if (!folderPath || !*folderPath) return;
    if (!GetDesktop_IshellFolder() || !m_pDesktopFolder) return;

    unique_pidl_absolute itemPidl;
    HRESULT hr = SHParseDisplayName(folderPath, nullptr, wil::out_param(itemPidl), 0, nullptr);
    if (SUCCEEDED(hr) && itemPidl) {
        DisplayItemContextMenu(itemPidl.get(), nullptr);
    }
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

ShellMenuCommandSession::~ShellMenuCommandSession()
{
    reset();
}

void ShellMenuCommandSession::reset()
{
    m_newItems.clear();
    m_thirdPartyItems.clear();
    m_commandMap.clear();
    m_directory.reset();
    m_contextMenu.reset();
    if (m_menu) {
        DestroyMenu(m_menu);
        m_menu = nullptr;
    }
}

bool ShellMenuCommandSession::invokeShellCommand(const std::string& commandId, HWND hwnd, const std::optional<std::string>& directoryOverride)
{
    if (!m_contextMenu || commandId.empty()) {
        return false;
    }

    const auto commandIt = m_commandMap.find(commandId);
    if (commandIt == m_commandMap.end() || !IsValidShellCommandId(commandIt->second)) {
        return false;
    }

    const std::optional<std::string>& directory = directoryOverride ? directoryOverride : m_directory;
    CMINVOKECOMMANDINFO commandInfo = {};
    commandInfo.cbSize = sizeof(commandInfo);
    commandInfo.fMask = 0;
    commandInfo.hwnd = hwnd;
    commandInfo.lpVerb = reinterpret_cast<LPCSTR>(MAKEINTRESOURCE(commandIt->second - MIN_SHELL_MENU_ID));
    commandInfo.lpDirectory = directory ? directory->c_str() : nullptr;
    commandInfo.nShow = SW_SHOWNORMAL;
    return SUCCEEDED(SafeInvokeCommand(m_contextMenu.get(), &commandInfo));
}

bool ShellContextMenu::prepareDesktopBackgroundMenuSnapshot(HWND hwnd, ShellMenuCommandSession& session)
{
    session.reset();
    if (!hwnd) {
        return false;
    }

    DisplayDesktopContextMenu();
    operationType = OperationType::BackGround;
    m_contextMenu = MaybeGetShellContextMenu(hwnd);
    if (!m_contextMenu) {
        return false;
    }

    wil::unique_hmenu hMenu(CreatePopupMenu());
    if (!hMenu) {
        return false;
    }

    const HRESULT queryHr = m_contextMenu->QueryContextMenu(hMenu.get(), 0, MIN_SHELL_MENU_ID, MAX_SHELL_MENU_ID,
        CMF_NORMAL | CMF_CANRENAME);
    if (FAILED(queryHr)) {
        return false;
    }

    RemoveTencentDeskGoMenu(hMenu.get());
    RemoveConfiguredMenuItems(hMenu.get(), m_compiledExclusions);
    DebugLogMenuTree(hMenu.get(), m_contextMenu.get(), L"Win11DesktopShellSnapshot");

    AppendMenuSnapshotItems(hMenu.get(),
        m_contextMenu.get(),
        session.m_thirdPartyItems,
        session.m_commandMap,
        "shell_external:",
        true,
        RegistryShellRootsForExclusionKey(L"desktop"),
        {},
        {});

    const int newMenuPos = FindNewMenuPosition(hMenu.get());
    if (newMenuPos >= 0) {
        MENUITEMINFOW newMenuInfo = {};
        newMenuInfo.cbSize = sizeof(newMenuInfo);
        newMenuInfo.fMask = MIIM_SUBMENU;
        if (GetMenuItemInfoW(hMenu.get(), static_cast<UINT>(newMenuPos), TRUE, &newMenuInfo) && newMenuInfo.hSubMenu) {
            m_contextMenu->HandleMenuMsg2(WM_INITMENUPOPUP,
                reinterpret_cast<WPARAM>(newMenuInfo.hSubMenu),
                MAKELPARAM(newMenuPos, FALSE),
                nullptr);

            AppendMenuSnapshotItems(newMenuInfo.hSubMenu,
                m_contextMenu.get(),
                session.m_newItems,
                session.m_commandMap,
                "shell_new:",
                false,
                {},
                {},
                {});
        }
    }

    session.m_contextMenu = m_contextMenu;
    session.m_directory = MaybeGetFilesystemDirectory();
    session.m_menu = hMenu.release();
    return true;
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

    const UINT queryFlags = CMF_NORMAL | CMF_CANRENAME
        | ((flags & Flags::ExtendedVerbs) != Flags::Standard ? CMF_EXTENDEDVERBS : 0);
    QElapsedTimer queryTimer;
    queryTimer.start();
    HRESULT queryHr = SafeQueryContextMenu(m_contextMenu.get(), hMenu.get(), 0, MIN_SHELL_MENU_ID, MAX_SHELL_MENU_ID,
        queryFlags);
    const qint64 queryContextMenuMs = queryTimer.elapsed();
    if (FAILED(queryHr)) {
        return;
    }
    qInfo() << "[ShellContextMenu] Direct ShowMenu ready: operationType=" << static_cast<int>(operationType)
            << "queryContextMenuMs=" << queryContextMenuMs
            << "menuItemCount=" << GetMenuItemCount(hMenu.get());
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

    RemoveConfiguredMenuItems(hMenu.get(), m_compiledExclusions);

    if (operationType == OperationType::boxBackground) {
        DebugLogTopLevelMenuItems(hMenu.get(), L"BoxContextMenu");
    } else if (operationType == OperationType::BackGround) {
        DebugLogMenuTree(hMenu.get(), m_contextMenu.get(), L"DefaultDesktopShellMenu");
    }

    EnableWindowDarkMenuTheme(hwnd);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);

    UINT cmd = TrackPopupMenu(hMenu.get(), TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, pt->x, pt->y, 0, hwnd, nullptr);
    PostMessage(hwnd, WM_NULL, 0, 0);
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
        HRESULT hr = SafeGetCommandString(m_contextMenu.get(), cmd - MIN_SHELL_MENU_ID, GCS_VERBA, nullptr,
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
            SafeInvokeCommand(m_contextMenu.get(), &commandInfo);
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
    PathType pathType = CSystemApi::GetPathType(QString::fromWCharArray(path));
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


wil::com_ptr_nothrow<IContextMenu3> ShellContextMenu::IContextMenuFromIShellFolder(PCIDLIST_ABSOLUTE pidl, IShellFolder* pShellFolder, HWND hwndOwner)
{
    Q_UNUSED(hwndOwner);
    LPCITEMIDLIST relativePIDL = nullptr;
    wil::com_ptr_nothrow<IContextMenu3> pContextMenu;
    if (pidl) {
        wil::com_ptr_nothrow<IShellFolder> parentShellFolder;
        HRESULT hr = SHBindToParent(pidl, IID_PPV_ARGS(&parentShellFolder), &relativePIDL);
        if (FAILED(hr) || !parentShellFolder || !relativePIDL) {
            if (pShellFolder) {
                parentShellFolder = pShellFolder;
                relativePIDL = pidl;
            } else if (m_pDesktopFolder) {
                parentShellFolder = m_pDesktopFolder;
                relativePIDL = pidl;
            }
        }
        if (parentShellFolder && relativePIDL) {
            wil::com_ptr_nothrow<IContextMenu> baseContextMenu;
            // 还原 1.0.5 模式：传入 NULL 作为 hwndOwner，触发 Shell 扩展轻量极速查询通道
            hr = parentShellFolder->GetUIObjectOf(NULL, 1, &relativePIDL, IID_IContextMenu, nullptr, baseContextMenu.put_void());
            if (SUCCEEDED(hr) && baseContextMenu) {
                // 还原 1.0.5 模式：不注入 Site，彻底规避快捷方式 (.lnk) 向上探测 Explorer 宿主引发的 RPC 阻塞
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
            LRESULT result = 0;
            HRESULT hr = SafeHandleMenuMsg2(contextMenu3.get(), msg, wParam, lParam, &result);

            if (SUCCEEDED(hr))
            {
                if (msg == WM_INITMENUPOPUP) {
                    RemoveConfiguredMenuItems(reinterpret_cast<HMENU>(wParam), m_compiledExclusions);
                }
                if (msg != WM_INITMENUPOPUP) {
                    return result;
                }
            }
        }
        else if (auto contextMenu2 = m_contextMenu.try_query<IContextMenu2>())
        {
            SafeHandleMenuMsg(contextMenu2.get(), msg, wParam, lParam);
            if (msg == WM_INITMENUPOPUP) {
                RemoveConfiguredMenuItems(reinterpret_cast<HMENU>(wParam), m_compiledExclusions);
            }
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
