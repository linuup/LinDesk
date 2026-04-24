#include "pch.h"

#include <sstream>

namespace wf = winrt::Windows::Foundation;
namespace wu = winrt::Windows::UI;
namespace wux = winrt::Windows::UI::Xaml;
namespace wuxc = winrt::Windows::UI::Xaml::Controls;
namespace wuxcp = winrt::Windows::UI::Xaml::Controls::Primitives;
namespace wuxh = winrt::Windows::UI::Xaml::Hosting;
namespace wuxi = winrt::Windows::UI::Xaml::Interop;
namespace wuxm = winrt::Windows::UI::Xaml::Media;
namespace wuxmi = winrt::Windows::UI::Xaml::Media::Imaging;

namespace
{
    constexpr wchar_t kWindowClassName[] = L"LinDeskWin11MenuIslandHost";
    constexpr UINT_PTR kShowMenuTimerId = 1;
    constexpr int kAnchorWindowSize = 8;

    struct AppState
    {
        HWND hostWindow{};
        HWND islandWindow{};
        POINT initialPoint{};
        std::filesystem::path assetDirectory;
        bool menuShown{};
        wuxh::WindowsXamlManager xamlManager{ nullptr };
        wuxh::DesktopWindowXamlSource desktopSource{ nullptr };
        wuxc::Grid root{ nullptr };
        wuxc::Border anchor{ nullptr };
        wuxc::MenuFlyout menu{ nullptr };
    };

    AppState g_state;

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

    std::wstring ToFileUri(std::filesystem::path path)
    {
        path.make_preferred();
        std::wstring value = path.wstring();
        std::replace(value.begin(), value.end(), L'\\', L'/');
        return L"file:///" + value;
    }

    std::wstring FormatHresultText(HRESULT hr)
    {
        wchar_t* systemText = nullptr;
        const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
        const DWORD lang = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);
        std::wstring result;

        if (FormatMessageW(flags, nullptr, static_cast<DWORD>(hr), lang, reinterpret_cast<LPWSTR>(&systemText), 0, nullptr) && systemText)
        {
            result = systemText;
            LocalFree(systemText);
        }

        while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n' || result.back() == L' '))
        {
            result.pop_back();
        }

        return result;
    }

    [[noreturn]] void ThrowDetailedHresult(HRESULT hr, wchar_t const* step)
    {
        std::wostringstream stream;
        stream << step;
        const std::wstring systemText = FormatHresultText(hr);
        if (!systemText.empty())
        {
            stream << L": " << systemText;
        }
        throw winrt::hresult_error(hr, winrt::hstring(stream.str()));
    }

    void ExitMenuHost()
    {
        PostQuitMessage(0);
    }

    wu::Color MakeColor(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
    {
        return wu::Color{ a, r, g, b };
    }

    wuxm::SolidColorBrush MakeBrush(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
    {
        return wuxm::SolidColorBrush(MakeColor(a, r, g, b));
    }

    wuxm::ImageSource CreateImageSource(wchar_t const* fileName)
    {
        if (!fileName || g_state.assetDirectory.empty())
        {
            return nullptr;
        }

        const auto path = g_state.assetDirectory / fileName;
        if (!std::filesystem::exists(path))
        {
            return nullptr;
        }

        wuxmi::BitmapImage image;
        image.UriSource(wf::Uri(winrt::hstring(ToFileUri(path))));
        return image;
    }

    wuxc::IconElement CreateMenuIcon(wchar_t const* fileName)
    {
        auto source = CreateImageSource(fileName);
        if (!source)
        {
            return nullptr;
        }

        wuxc::BitmapIcon icon;
        icon.UriSource(wf::Uri(winrt::hstring(ToFileUri(g_state.assetDirectory / fileName))));
        icon.ShowAsMonochrome(false);
        return icon;
    }

    void LogCommandAndExit(wchar_t const* commandId)
    {
        std::wstring debugMessage = L"Win11MenuIsland command: ";
        debugMessage += commandId;
        debugMessage += L"\r\n";
        OutputDebugStringW(debugMessage.c_str());
        ExitMenuHost();
    }

    void OnMenuItemClick(wf::IInspectable const& sender, wux::RoutedEventArgs const&)
    {
        auto item = sender.as<wuxc::MenuFlyoutItemBase>();
        auto tag = winrt::unbox_value_or<winrt::hstring>(item.Tag(), L"");
        if (!tag.empty())
        {
            LogCommandAndExit(tag.c_str());
        }
    }

    wuxc::MenuFlyoutItem CreateActionItem(wchar_t const* text, wchar_t const* commandId, wchar_t const* iconName = nullptr, bool enabled = true)
    {
        wuxc::MenuFlyoutItem item;
        item.Text(text);
        item.IsEnabled(enabled);
        if (iconName)
        {
            auto icon = CreateMenuIcon(iconName);
            if (icon)
            {
                item.Icon(icon);
            }
        }
        if (enabled)
        {
            item.Tag(winrt::box_value(winrt::hstring(commandId)));
            item.Click(OnMenuItemClick);
        }
        return item;
    }

    wuxc::MenuFlyoutSubItem CreateViewSubmenu()
    {
        wuxc::MenuFlyoutSubItem item;
        item.Text(L"View");
        if (auto icon = CreateMenuIcon(L"View.png"))
        {
            item.Icon(icon);
        }
        item.Items().Append(CreateActionItem(L"Large icons", L"view_large", L"LargeIcons.png"));
        item.Items().Append(CreateActionItem(L"Medium icons", L"view_medium", L"MediumIcons.png"));
        item.Items().Append(CreateActionItem(L"Small icons", L"view_small", L"SmallIcons.png"));
        item.Items().Append(wuxc::MenuFlyoutSeparator{});
        item.Items().Append(CreateActionItem(L"Auto arrange icons", L"auto_arrange_icons", L"AutoArrangeIcons.png"));
        item.Items().Append(CreateActionItem(L"Show desktop icons", L"show_desktop_icons", L"ShowDesktopIcons.png"));
        return item;
    }

    wuxc::MenuFlyoutSubItem CreateSortSubmenu()
    {
        wuxc::MenuFlyoutSubItem item;
        item.Text(L"Sort by");
        if (auto icon = CreateMenuIcon(L"SortBy.png"))
        {
            item.Icon(icon);
        }
        item.Items().Append(CreateActionItem(L"Name", L"sort_name"));
        item.Items().Append(CreateActionItem(L"Size", L"sort_size"));
        item.Items().Append(CreateActionItem(L"Item type", L"sort_type"));
        item.Items().Append(CreateActionItem(L"Date modified", L"sort_date"));
        return item;
    }

    wuxc::MenuFlyoutSubItem CreateNewSubmenu()
    {
        wuxc::MenuFlyoutSubItem item;
        item.Text(L"New");
        if (auto icon = CreateMenuIcon(L"New.png"))
        {
            item.Icon(icon);
        }
        item.Items().Append(CreateActionItem(L"Folder", L"new_folder"));
        item.Items().Append(CreateActionItem(L"Shortcut", L"new_shortcut"));
        item.Items().Append(CreateActionItem(L"Text Document", L"new_text"));
        return item;
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
        backgroundSetter.Value(MakeBrush(228, 243, 244, 247));
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

    wuxc::MenuFlyout BuildMenu()
    {
        wuxc::MenuFlyout menu;
        menu.MenuFlyoutPresenterStyle(CreateMenuPresenterStyle());
        menu.Items().Append(CreateViewSubmenu());
        menu.Items().Append(CreateSortSubmenu());
        menu.Items().Append(CreateActionItem(L"Refresh", L"refresh", L"Refresh.png"));
        menu.Items().Append(wuxc::MenuFlyoutSeparator{});
        menu.Items().Append(CreateActionItem(L"Paste", L"paste", nullptr, false));
        menu.Items().Append(CreateActionItem(L"Paste shortcut", L"paste_shortcut", nullptr, false));
        menu.Items().Append(CreateActionItem(L"Open in Terminal", L"open_terminal"));
        menu.Items().Append(CreateNewSubmenu());
        menu.Items().Append(wuxc::MenuFlyoutSeparator{});
        menu.Items().Append(CreateActionItem(L"Display settings", L"display_settings", L"DisplaySettings.png"));
        menu.Items().Append(CreateActionItem(L"Personalize", L"personalize", L"Personalize.png"));
        menu.Items().Append(wuxc::MenuFlyoutSeparator{});
        menu.Items().Append(CreateActionItem(L"Show more options", L"show_more_options", L"ShowMoreOptions.png"));

        menu.Closed([](wf::IInspectable const&, wf::IInspectable const&)
        {
            ExitMenuHost();
        });

        return menu;
    }

    RECT CalculateHostRect()
    {
        RECT rect{};
        rect.left = g_state.initialPoint.x;
        rect.top = g_state.initialPoint.y;
        rect.right = rect.left + kAnchorWindowSize;
        rect.bottom = rect.top + kAnchorWindowSize;

        HMONITOR monitor = MonitorFromPoint(g_state.initialPoint, MONITOR_DEFAULTTONEAREST);
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
        g_state.root.Width(static_cast<double>(width));
        g_state.root.Height(static_cast<double>(height));
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
        g_state.anchor.HorizontalAlignment(wux::HorizontalAlignment::Left);
        g_state.anchor.VerticalAlignment(wux::VerticalAlignment::Top);
        g_state.root.Children().Append(g_state.anchor);

        g_state.menu = BuildMenu();
        g_state.desktopSource.Content(g_state.root);
        ResizeIslandToHost();
    }

    LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_CREATE:
            try
            {
                g_state.hostWindow = hwnd;
                InitializeIsland(hwnd);
                SetTimer(hwnd, kShowMenuTimerId, 30, nullptr);
                return 0;
            }
            catch (winrt::hresult_error const& ex)
            {
                std::wostringstream text;
                text << L"Failed to initialize XAML island.\r\n"
                     << L"HRESULT: 0x" << std::hex << std::uppercase << static_cast<uint32_t>(ex.code()) << L"\r\n"
                     << L"Message: " << ex.message().c_str();
                MessageBoxW(hwnd, text.str().c_str(), L"Win11MenuIsland", MB_ICONERROR);
                return -1;
            }
            catch (std::exception const& ex)
            {
                std::wstring exceptionMessage(ex.what(), ex.what() + strlen(ex.what()));
                std::wstring text = L"Failed to initialize XAML island.\r\nMessage: ";
                text += exceptionMessage;
                MessageBoxW(hwnd, text.c_str(), L"Win11MenuIsland", MB_ICONERROR);
                return -1;
            }

        case WM_SIZE:
            ResizeIslandToHost();
            return 0;

        case WM_TIMER:
            if (wParam == kShowMenuTimerId)
            {
                KillTimer(hwnd, kShowMenuTimerId);
                ShowMenuWindow();
                return 0;
            }
            break;

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE)
            {
                ExitMenuHost();
                return 0;
            }
            break;

        case WM_DESTROY:
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
        return RegisterClassExW(&windowClass);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    if (!RegisterHostClass(instance))
    {
        return 0;
    }

    GetCursorPos(&g_state.initialPoint);

    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        kWindowClassName,
        L"Win11MenuIsland",
        WS_POPUP,
        g_state.initialPoint.x,
        g_state.initialPoint.y,
        kAnchorWindowSize,
        kAnchorWindowSize,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (!hwnd)
    {
        return 0;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0))
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    winrt::uninit_apartment();
    return static_cast<int>(message.wParam);
}
