#include "pch.h"

namespace wdj = winrt::Windows::Data::Json;
namespace wf = winrt::Windows::Foundation;
namespace ws = winrt::Windows::System;
namespace wu = winrt::Windows::UI;
namespace wux = winrt::Windows::UI::Xaml;
namespace wuxc = winrt::Windows::UI::Xaml::Controls;
namespace wuxcp = winrt::Windows::UI::Xaml::Controls::Primitives;
namespace wuxh = winrt::Windows::UI::Xaml::Hosting;
namespace wuxinput = winrt::Windows::UI::Xaml::Input;
namespace wuxi = winrt::Windows::UI::Xaml::Interop;
namespace wuxm = winrt::Windows::UI::Xaml::Media;
namespace wuxmi = winrt::Windows::UI::Xaml::Media::Imaging;

namespace
{
    constexpr wchar_t kWindowClassName[] = L"LinDeskWin11MenuIslandHost";
    constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\LinDesk.Win11MenuHost";
    constexpr UINT kShowMenuMessage = WM_APP + 41;
    constexpr int kAnchorWindowSize = 48;
    constexpr int kAnchorOffset = 40;
    constexpr DWORD kMaxMessageSize = 1024 * 1024;

    struct AppState
    {
        HWND hostWindow{};
        HWND islandWindow{};
        POINT requestedPoint{};
        std::filesystem::path assetDirectory;
        std::wstring requestId;
        std::wstring pendingRequestJson;
        HANDLE currentPipe{ INVALID_HANDLE_VALUE };
        HANDLE parentProcess{ nullptr };
        HANDLE responseSentEvent{ nullptr };
        std::mutex pipeMutex;
        std::atomic_bool shutdownRequested{ false };
        bool menuShown{};
        bool responseSent{};
        wuxh::WindowsXamlManager xamlManager{ nullptr };
        wuxh::DesktopWindowXamlSource desktopSource{ nullptr };
        wuxc::Grid root{ nullptr };
        wuxc::Border anchor{ nullptr };
        wuxc::MenuFlyout menu{ nullptr };
    };

    AppState g_state;

    void ApplyTransparentHost(HWND hwnd);

    std::filesystem::path GetExecutablePath()
    {
        std::wstring buffer(MAX_PATH, L'\0');
        DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        while (length == buffer.size())
        {
            buffer.resize(buffer.size() * 2, L'\0');
            length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        }
        buffer.resize(length);
        return std::filesystem::path(buffer);
    }

    std::filesystem::path FindAssetDirectory()
    {
        auto current = GetExecutablePath().parent_path();
        for (int depth = 0; depth < 8 && !current.empty(); ++depth)
        {
            const auto candidate = current / L"Win11ContextMenu" / L"light";
            if (std::filesystem::exists(candidate / L"View.png"))
            {
                return candidate;
            }
            current = current.parent_path();
        }
        return {};
    }

    struct LaunchOptions
    {
        bool launchedByLinDesk{};
        DWORD parentPid{};
    };

    LaunchOptions ParseLaunchOptions()
    {
        LaunchOptions options;
        int argc = 0;
        PWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!argv)
        {
            return options;
        }

        for (int i = 1; i < argc; ++i)
        {
            if (wcscmp(argv[i], L"--lindesk-menu-host") == 0)
            {
                options.launchedByLinDesk = true;
            }
            else if (wcscmp(argv[i], L"--parent-pid") == 0 && i + 1 < argc)
            {
                options.parentPid = wcstoul(argv[i + 1], nullptr, 10);
                ++i;
            }
        }

        LocalFree(argv);
        return options;
    }

    HANDLE OpenValidatedLinDeskParentProcess(const LaunchOptions& options)
    {
        if (!options.launchedByLinDesk || options.parentPid == 0 || options.parentPid == GetCurrentProcessId())
        {
            return nullptr;
        }

        HANDLE parentProcess = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, options.parentPid);
        if (!parentProcess)
        {
            return nullptr;
        }

        std::wstring processPath(MAX_PATH, L'\0');
        DWORD pathLength = static_cast<DWORD>(processPath.size());
        while (!QueryFullProcessImageNameW(parentProcess, 0, processPath.data(), &pathLength))
        {
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            {
                CloseHandle(parentProcess);
                return nullptr;
            }

            processPath.resize(processPath.size() * 2, L'\0');
            pathLength = static_cast<DWORD>(processPath.size());
        }

        processPath.resize(pathLength);
        const std::wstring fileName = std::filesystem::path(processPath).filename().wstring();
        if (_wcsicmp(fileName.c_str(), L"LinDesk.exe") != 0)
        {
            CloseHandle(parentProcess);
            return nullptr;
        }

        return parentProcess;
    }

    void StartParentProcessWatch(HANDLE parentProcess)
    {
        if (!parentProcess)
        {
            return;
        }

        g_state.parentProcess = parentProcess;
        std::thread([parentProcess]() {
            const DWORD waitResult = WaitForSingleObject(parentProcess, INFINITE);
            if (waitResult == WAIT_OBJECT_0 && !g_state.shutdownRequested.load())
            {
                g_state.shutdownRequested.store(true);
                if (g_state.hostWindow)
                {
                    PostMessageW(g_state.hostWindow, WM_CLOSE, 0, 0);
                }
            }
        }).detach();
    }

    std::wstring ToFileUri(std::filesystem::path path)
    {
        path.make_preferred();
        std::wstring value = path.wstring();
        std::replace(value.begin(), value.end(), L'\\', L'/');
        return L"file:///" + value;
    }

    std::wstring Utf8ToWide(std::string_view value)
    {
        if (value.empty())
        {
            return {};
        }

        const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
        if (size <= 0)
        {
            return {};
        }

        std::wstring result(size, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
        return result;
    }

    std::string WideToUtf8(std::wstring_view value)
    {
        if (value.empty())
        {
            return {};
        }

        const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0)
        {
            return {};
        }

        std::string result(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
        return result;
    }

    std::wstring JsonEscape(std::wstring_view value)
    {
        std::wstring result;
        result.reserve(value.size() + 8);
        for (wchar_t ch : value)
        {
            switch (ch)
            {
            case L'\\':
                result += L"\\\\";
                break;
            case L'"':
                result += L"\\\"";
                break;
            case L'\r':
                result += L"\\r";
                break;
            case L'\n':
                result += L"\\n";
                break;
            case L'\t':
                result += L"\\t";
                break;
            default:
                result += ch;
                break;
            }
        }
        return result;
    }

    bool WriteAll(HANDLE pipe, const void* data, DWORD size)
    {
        const char* cursor = static_cast<const char*>(data);
        DWORD remaining = size;
        while (remaining > 0)
        {
            DWORD written = 0;
            if (!WriteFile(pipe, cursor, remaining, &written, nullptr) || written == 0)
            {
                return false;
            }
            cursor += written;
            remaining -= written;
        }
        return true;
    }

    bool ReadAll(HANDLE pipe, void* data, DWORD size)
    {
        char* cursor = static_cast<char*>(data);
        DWORD remaining = size;
        while (remaining > 0)
        {
            DWORD read = 0;
            if (!ReadFile(pipe, cursor, remaining, &read, nullptr) || read == 0)
            {
                return false;
            }
            cursor += read;
            remaining -= read;
        }
        return true;
    }

    bool WriteMessage(HANDLE pipe, const std::string& payload)
    {
        if (payload.empty() || payload.size() > kMaxMessageSize)
        {
            return false;
        }

        const uint32_t length = static_cast<uint32_t>(payload.size());
        return WriteAll(pipe, &length, sizeof(length)) &&
            WriteAll(pipe, payload.data(), length);
    }

    bool ReadMessage(HANDLE pipe, std::string* payload)
    {
        uint32_t length = 0;
        if (!ReadAll(pipe, &length, sizeof(length)) || length == 0 || length > kMaxMessageSize)
        {
            return false;
        }

        std::string buffer(length, '\0');
        if (!ReadAll(pipe, buffer.data(), length))
        {
            return false;
        }

        if (payload)
        {
            *payload = std::move(buffer);
        }
        return true;
    }

    std::wstring GetString(wdj::JsonObject const& object, wchar_t const* key, wchar_t const* fallback = L"")
    {
        return object.GetNamedString(key, fallback).c_str();
    }

    bool GetBool(wdj::JsonObject const& object, wchar_t const* key, bool fallback = false)
    {
        return object.GetNamedBoolean(key, fallback);
    }

    wuxm::SolidColorBrush MakeBrush(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
    {
        return wuxm::SolidColorBrush(wu::Color{ a, r, g, b });
    }

    wuxc::IconElement CreateMenuIcon(std::wstring const& fileName)
    {
        if (fileName.empty() || g_state.assetDirectory.empty())
        {
            return nullptr;
        }

        const auto path = g_state.assetDirectory / fileName;
        if (!std::filesystem::exists(path))
        {
            return nullptr;
        }

        wuxc::BitmapIcon icon;
        icon.UriSource(wf::Uri(winrt::hstring(ToFileUri(path))));
        icon.ShowAsMonochrome(false);
        return icon;
    }

    void SendMenuResult(std::wstring_view status, std::wstring_view commandId = {})
    {
        HANDLE pipe = INVALID_HANDLE_VALUE;
        {
            std::lock_guard<std::mutex> lock(g_state.pipeMutex);
            if (g_state.responseSent)
            {
                return;
            }
            g_state.responseSent = true;
            pipe = g_state.currentPipe;
        }

        if (pipe != INVALID_HANDLE_VALUE)
        {
            std::wostringstream response;
            response << L"{\"type\":\"menuResult\",\"requestId\":\""
                     << JsonEscape(g_state.requestId)
                     << L"\",\"status\":\""
                     << JsonEscape(status)
                     << L"\"";
            if (!commandId.empty())
            {
                response << L",\"commandId\":\"" << JsonEscape(commandId) << L"\"";
            }
            response << L"}";

            const std::string responseUtf8 = WideToUtf8(response.str());
            WriteMessage(pipe, responseUtf8);
            FlushFileBuffers(pipe);
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);

            std::lock_guard<std::mutex> lock(g_state.pipeMutex);
            g_state.currentPipe = INVALID_HANDLE_VALUE;
        }

        if (g_state.responseSentEvent)
        {
            SetEvent(g_state.responseSentEvent);
        }

        ShowWindow(g_state.hostWindow, SW_HIDE);
        g_state.menuShown = false;
    }

    void OnMenuItemClick(wf::IInspectable const& sender, wux::RoutedEventArgs const&)
    {
        auto item = sender.as<wuxc::MenuFlyoutItemBase>();
        auto tag = winrt::unbox_value_or<winrt::hstring>(item.Tag(), L"");
        if (!tag.empty())
        {
            SendMenuResult(L"accepted", tag.c_str());
        }
    }

    void ApplyShortcut(wuxc::MenuFlyoutItemBase const& item, std::wstring const& shortcut)
    {
        if (shortcut.empty())
        {
            return;
        }

        if (auto menuItem = item.try_as<wuxc::MenuFlyoutItem>())
        {
            menuItem.KeyboardAcceleratorTextOverride(winrt::hstring(shortcut));
        }

        ws::VirtualKey key = ws::VirtualKey::None;
        if (shortcut == L"Ctrl+Shift+2")
        {
            key = ws::VirtualKey::Number2;
        }
        else if (shortcut == L"Ctrl+Shift+3")
        {
            key = ws::VirtualKey::Number3;
        }
        else if (shortcut == L"Ctrl+Shift+4")
        {
            key = ws::VirtualKey::Number4;
        }

        if (key == ws::VirtualKey::None)
        {
            return;
        }

        wuxinput::KeyboardAccelerator accelerator;
        accelerator.Key(key);
        accelerator.Modifiers(ws::VirtualKeyModifiers::Control | ws::VirtualKeyModifiers::Shift);
        item.KeyboardAccelerators().Append(accelerator);
    }

    wuxc::MenuFlyoutItemBase CreateMenuItemFromJson(wdj::JsonObject const& data);

    wuxc::MenuFlyoutItemBase CreateActionFromJson(wdj::JsonObject const& data)
    {
        const std::wstring text = GetString(data, L"text");
        const std::wstring commandId = GetString(data, L"commandId");
        const std::wstring iconName = GetString(data, L"icon");
        const std::wstring shortcut = GetString(data, L"shortcut");
        const bool enabled = GetBool(data, L"enabled", true);
        const bool checkable = GetBool(data, L"checkable", false);
        const bool checked = GetBool(data, L"checked", false);

        if (checkable)
        {
            wuxc::ToggleMenuFlyoutItem item;
            item.Text(winrt::hstring(text));
            item.IsEnabled(enabled);
            item.IsChecked(checked);
            if (auto icon = CreateMenuIcon(iconName))
            {
                item.Icon(icon);
            }
            ApplyShortcut(item, shortcut);
            if (enabled)
            {
                item.Tag(winrt::box_value(winrt::hstring(commandId)));
                item.Click(OnMenuItemClick);
            }
            return item;
        }

        wuxc::MenuFlyoutItem item;
        item.Text(winrt::hstring(text));
        item.IsEnabled(enabled);
        if (auto icon = CreateMenuIcon(iconName))
        {
            item.Icon(icon);
        }
        ApplyShortcut(item, shortcut);
        if (enabled)
        {
            item.Tag(winrt::box_value(winrt::hstring(commandId)));
            item.Click(OnMenuItemClick);
        }
        return item;
    }

    wuxc::MenuFlyoutItemBase CreateSubmenuFromJson(wdj::JsonObject const& data)
    {
        wuxc::MenuFlyoutSubItem item;
        item.Text(winrt::hstring(GetString(data, L"text")));
        if (auto icon = CreateMenuIcon(GetString(data, L"icon")))
        {
            item.Icon(icon);
        }

        if (data.HasKey(L"items"))
        {
            auto children = data.GetNamedArray(L"items");
            for (uint32_t i = 0; i < children.Size(); ++i)
            {
                if (children.GetAt(i).ValueType() == wdj::JsonValueType::Object)
                {
                    item.Items().Append(CreateMenuItemFromJson(children.GetAt(i).GetObject()));
                }
            }
        }
        return item;
    }

    wuxc::MenuFlyoutItemBase CreateMenuItemFromJson(wdj::JsonObject const& data)
    {
        const std::wstring type = GetString(data, L"type", L"action");
        if (type == L"separator")
        {
            return wuxc::MenuFlyoutSeparator{};
        }
        if (type == L"submenu")
        {
            return CreateSubmenuFromJson(data);
        }
        return CreateActionFromJson(data);
    }

    wux::Style CreateMenuPresenterStyle()
    {
        wux::Style style;
        wuxi::TypeName targetType;
        targetType.Name = L"Windows.UI.Xaml.Controls.MenuFlyoutPresenter";
        targetType.Kind = wuxi::TypeKind::Metadata;
        style.TargetType(targetType);

        wux::Setter backgroundSetter;
        backgroundSetter.Property(wuxc::Control::BackgroundProperty());
        backgroundSetter.Value(MakeBrush(232, 243, 244, 247));
        style.Setters().Append(backgroundSetter);

        wux::Setter borderBrushSetter;
        borderBrushSetter.Property(wuxc::Control::BorderBrushProperty());
        borderBrushSetter.Value(MakeBrush(180, 214, 216, 222));
        style.Setters().Append(borderBrushSetter);

        wux::Setter borderThicknessSetter;
        borderThicknessSetter.Property(wuxc::Control::BorderThicknessProperty());
        borderThicknessSetter.Value(winrt::box_value(wux::ThicknessHelper::FromUniformLength(1.0)));
        style.Setters().Append(borderThicknessSetter);

        wux::Setter cornerRadiusSetter;
        cornerRadiusSetter.Property(wuxc::Control::CornerRadiusProperty());
        cornerRadiusSetter.Value(winrt::box_value(wux::CornerRadiusHelper::FromRadii(12.0, 12.0, 12.0, 12.0)));
        style.Setters().Append(cornerRadiusSetter);

        wux::Setter paddingSetter;
        paddingSetter.Property(wuxc::Control::PaddingProperty());
        paddingSetter.Value(winrt::box_value(wux::ThicknessHelper::FromLengths(4.0, 6.0, 4.0, 6.0)));
        style.Setters().Append(paddingSetter);

        return style;
    }

    wuxc::MenuFlyout BuildMenuFromRequest(wdj::JsonObject const& request)
    {
        wuxc::MenuFlyout menu;
        menu.MenuFlyoutPresenterStyle(CreateMenuPresenterStyle());

        if (request.HasKey(L"items"))
        {
            auto items = request.GetNamedArray(L"items");
            for (uint32_t i = 0; i < items.Size(); ++i)
            {
                if (items.GetAt(i).ValueType() == wdj::JsonValueType::Object)
                {
                    menu.Items().Append(CreateMenuItemFromJson(items.GetAt(i).GetObject()));
                }
            }
        }

        menu.Closed([](wf::IInspectable const&, wf::IInspectable const&)
        {
            if (!g_state.responseSent)
            {
                SendMenuResult(L"cancelled");
            }
        });

        return menu;
    }

    RECT CalculateHostRect()
    {
        RECT rect{};
        rect.left = g_state.requestedPoint.x - kAnchorOffset;
        rect.top = g_state.requestedPoint.y - kAnchorOffset;
        rect.right = rect.left + kAnchorWindowSize;
        rect.bottom = rect.top + kAnchorWindowSize;

        HMONITOR monitor = MonitorFromPoint(g_state.requestedPoint, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (GetMonitorInfoW(monitor, &monitorInfo))
        {
            rect.left = std::max(rect.left, monitorInfo.rcWork.left);
            rect.top = std::max(rect.top, monitorInfo.rcWork.top);
            rect.right = std::min(rect.right, monitorInfo.rcWork.right);
            rect.bottom = std::min(rect.bottom, monitorInfo.rcWork.bottom);
        }

        return rect;
    }

    void ResizeIslandToHost()
    {
        if (!g_state.islandWindow || !g_state.root)
        {
            return;
        }

        RECT clientRect{};
        GetClientRect(g_state.hostWindow, &clientRect);
        const int width = clientRect.right - clientRect.left;
        const int height = clientRect.bottom - clientRect.top;
        SetWindowPos(g_state.islandWindow, nullptr, 0, 0, width, height, SWP_SHOWWINDOW);
        ApplyTransparentHost(g_state.islandWindow);
        g_state.root.Width(static_cast<double>(width));
        g_state.root.Height(static_cast<double>(height));
    }

    void ApplyTransparentHost(HWND hwnd)
    {
        if (!hwnd)
        {
            return;
        }
        LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        exStyle |= WS_EX_LAYERED;
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
        SetLayeredWindowAttributes(hwnd, 0, 1, LWA_ALPHA);
    }

    void ShowMenuWindow()
    {
        if (g_state.menuShown || !g_state.menu || !g_state.anchor)
        {
            return;
        }

        const RECT rect = CalculateHostRect();
        SetWindowPos(
            g_state.hostWindow,
            HWND_TOPMOST,
            rect.left,
            rect.top,
            rect.right - rect.left,
            rect.bottom - rect.top,
            SWP_SHOWWINDOW);
        ShowWindow(g_state.hostWindow, SW_SHOW);
        UpdateWindow(g_state.hostWindow);
        ApplyTransparentHost(g_state.hostWindow);
        SetForegroundWindow(g_state.hostWindow);
        SetActiveWindow(g_state.hostWindow);
        SetFocus(g_state.hostWindow);
        ResizeIslandToHost();

        wuxcp::FlyoutShowOptions options;
        options.ShowMode(wuxcp::FlyoutShowMode::Standard);
        options.Placement(wuxcp::FlyoutPlacementMode::BottomEdgeAlignedLeft);
        options.Position(wf::Point{ 0.0f, 0.0f });

        g_state.menuShown = true;
        g_state.menu.ShowAt(g_state.anchor, options);
    }

    void HandleShowMenuRequest()
    {
        try
        {
            const auto request = wdj::JsonObject::Parse(winrt::hstring(g_state.pendingRequestJson));
            g_state.requestId = GetString(request, L"requestId");
            g_state.requestedPoint.x = static_cast<LONG>(request.GetNamedNumber(L"screenX", 0));
            g_state.requestedPoint.y = static_cast<LONG>(request.GetNamedNumber(L"screenY", 0));
            g_state.responseSent = false;
            g_state.menuShown = false;
            g_state.menu = BuildMenuFromRequest(request);
            ShowMenuWindow();
        }
        catch (...)
        {
            SendMenuResult(L"failed");
        }
    }

    void InitializeIsland(HWND hwnd)
    {
        g_state.xamlManager = wuxh::WindowsXamlManager::InitializeForCurrentThread();
        g_state.assetDirectory = FindAssetDirectory();
        g_state.desktopSource = wuxh::DesktopWindowXamlSource{};

        auto interop = g_state.desktopSource.as<IDesktopWindowXamlSourceNative>();
        winrt::check_hresult(interop->AttachToWindow(hwnd));
        winrt::check_hresult(interop->get_WindowHandle(&g_state.islandWindow));

        g_state.root = wuxc::Grid{};
        g_state.root.Background(wuxm::SolidColorBrush(wu::Color{ 0, 0, 0, 0 }));

        g_state.anchor = wuxc::Border{};
        g_state.anchor.Width(1.0);
        g_state.anchor.Height(1.0);
        g_state.anchor.Opacity(0.0);
        g_state.anchor.Margin(wux::ThicknessHelper::FromLengths(kAnchorOffset, kAnchorOffset, 0.0, 0.0));
        g_state.anchor.HorizontalAlignment(wux::HorizontalAlignment::Left);
        g_state.anchor.VerticalAlignment(wux::VerticalAlignment::Top);
        g_state.root.Children().Append(g_state.anchor);

        g_state.desktopSource.Content(g_state.root);
        ResizeIslandToHost();
    }

    void PipeServerLoop()
    {
        while (!g_state.shutdownRequested.load())
        {
            HANDLE pipe = CreateNamedPipeW(
                kPipeName,
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                1,
                kMaxMessageSize,
                kMaxMessageSize,
                0,
                nullptr);

            if (pipe == INVALID_HANDLE_VALUE)
            {
                Sleep(100);
                continue;
            }

            const BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
            if (!connected)
            {
                CloseHandle(pipe);
                continue;
            }

            std::string requestUtf8;
            if (!ReadMessage(pipe, &requestUtf8))
            {
                DisconnectNamedPipe(pipe);
                CloseHandle(pipe);
                continue;
            }

            const std::wstring requestJson = Utf8ToWide(requestUtf8);
            bool shutdown = false;
            try
            {
                const auto request = wdj::JsonObject::Parse(winrt::hstring(requestJson));
                shutdown = GetString(request, L"type") == L"shutdown";
            }
            catch (...)
            {
                shutdown = false;
            }

            if (shutdown)
            {
                try
                {
                    const auto request = wdj::JsonObject::Parse(winrt::hstring(requestJson));
                    g_state.requestId = GetString(request, L"requestId");
                }
                catch (...)
                {
                    g_state.requestId = L"";
                }
                {
                    std::lock_guard<std::mutex> lock(g_state.pipeMutex);
                    g_state.currentPipe = pipe;
                    g_state.responseSent = false;
                }
                SendMenuResult(L"accepted");
                g_state.shutdownRequested.store(true);
                PostMessageW(g_state.hostWindow, WM_CLOSE, 0, 0);
                break;
            }

            {
                std::lock_guard<std::mutex> lock(g_state.pipeMutex);
                g_state.pendingRequestJson = requestJson;
                g_state.currentPipe = pipe;
                g_state.responseSent = false;
                ResetEvent(g_state.responseSentEvent);
            }

            PostMessageW(g_state.hostWindow, kShowMenuMessage, 0, 0);
            WaitForSingleObject(g_state.responseSentEvent, INFINITE);
        }
    }

    LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_CREATE:
            try
            {
                g_state.hostWindow = hwnd;
                ApplyTransparentHost(hwnd);
                InitializeIsland(hwnd);
                return 0;
            }
            catch (...)
            {
                return -1;
            }

        case WM_SIZE:
            ResizeIslandToHost();
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            BeginPaint(hwnd, &paint);
            EndPaint(hwnd, &paint);
            return 0;
        }

        case kShowMenuMessage:
            HandleShowMenuRequest();
            return 0;

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE)
            {
                SendMenuResult(L"cancelled");
                return 0;
            }
            break;

        case WM_DESTROY:
            g_state.shutdownRequested.store(true);
            if (g_state.parentProcess)
            {
                CloseHandle(g_state.parentProcess);
                g_state.parentProcess = nullptr;
            }
            g_state.menu = nullptr;
            g_state.anchor = nullptr;
            g_state.root = nullptr;
            g_state.desktopSource = nullptr;
            g_state.xamlManager = nullptr;
            g_state.islandWindow = nullptr;
            g_state.hostWindow = nullptr;
            g_state.menuShown = false;
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    ATOM RegisterHostClass(HINSTANCE instance)
    {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.hInstance = instance;
        windowClass.lpfnWndProc = WindowProc;
        windowClass.lpszClassName = kWindowClassName;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = nullptr;
        return RegisterClassExW(&windowClass);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    HANDLE parentProcess = OpenValidatedLinDeskParentProcess(ParseLaunchOptions());
    if (!parentProcess)
    {
        return 0;
    }

    winrt::init_apartment(winrt::apartment_type::single_threaded);

    if (!RegisterHostClass(instance))
    {
        CloseHandle(parentProcess);
        return 0;
    }

    g_state.responseSentEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_state.responseSentEvent)
    {
        CloseHandle(parentProcess);
        return 0;
    }

    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED,
        kWindowClassName,
        L"Win11MenuIsland",
        WS_POPUP,
        0,
        0,
        kAnchorWindowSize,
        kAnchorWindowSize,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (!hwnd)
    {
        CloseHandle(parentProcess);
        CloseHandle(g_state.responseSentEvent);
        return 0;
    }

    StartParentProcessWatch(parentProcess);

    std::thread(PipeServerLoop).detach();

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0))
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (g_state.responseSentEvent)
    {
        CloseHandle(g_state.responseSentEvent);
        g_state.responseSentEvent = nullptr;
    }

    winrt::uninit_apartment();
    return static_cast<int>(message.wParam);
}
