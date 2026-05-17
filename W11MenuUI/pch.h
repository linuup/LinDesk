#pragma once

#include <windows.h>
#include <shellapi.h>
#include <unknwn.h>
#include <roapi.h>

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <string>
#include <filesystem>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <sstream>
#include <string_view>
#include <thread>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.System.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>

#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "runtimeobject.lib")
