#pragma once

#include <windows.h>
#include <unknwn.h>
#include <roapi.h>

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <string>
#include <filesystem>
#include <algorithm>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>

#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "runtimeobject.lib")
