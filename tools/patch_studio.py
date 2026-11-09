import os

def patch_file():
    path = 'src/ui/hub/WallpaperStudioWindow.cpp'
    with open(path, 'r', encoding='utf-8') as f:
        content = f.read()

    target = """void WallpaperStudioWindow::showStudio(int defaultTab) {
    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (!bridge.isLoaded()) {
        bridge.loadRuntime();
    }

    auto* win = instance();
    if (win->isMinimized()) {
        win->showNormal();
    }
    win->show();
    win->raise();
    win->activateWindow();
    if (!win->m_hubWebWidget) {
        win->switchToTab(defaultTab);
    }
}"""

    replacement = """void WallpaperStudioWindow::showStudio(int defaultTab) {
    qInfo() << "[WallpaperStudio] showStudio called! defaultTab=" << defaultTab;
    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (!bridge.isLoaded()) {
        bridge.loadRuntime();
    }

    auto* win = instance();
    if (win->isMinimized()) {
        win->showNormal();
    }
    win->show();
    win->raise();
    win->activateWindow();
    if (!win->m_hubWebWidget) {
        win->switchToTab(defaultTab);
    }

#ifdef Q_OS_WIN
    // 强制突破 Windows 10/11 前台激活锁定，确保独立应用窗口置顶并在任务栏完整显示
    HWND hwnd = reinterpret_cast<HWND>(win->winId());
    if (hwnd && IsWindow(hwnd)) {
        LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        if (!(exStyle & WS_EX_APPWINDOW) || (exStyle & WS_EX_TOOLWINDOW)) {
            exStyle |= WS_EX_APPWINDOW;
            exStyle &= ~WS_EX_TOOLWINDOW;
            SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle);
        }

        if (GetParent(hwnd) != nullptr) {
            SetParent(hwnd, nullptr);
        }

        ShowWindow(hwnd, SW_SHOW);
        SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);

        HWND foreHwnd = GetForegroundWindow();
        DWORD foreThread = foreHwnd ? GetWindowThreadProcessId(foreHwnd, nullptr) : 0;
        DWORD appThread = GetCurrentThreadId();

        if (foreThread != 0 && foreThread != appThread) {
            AttachThreadInput(foreThread, appThread, TRUE);
            BringWindowToTop(hwnd);
            SetForegroundWindow(hwnd);
            AttachThreadInput(foreThread, appThread, FALSE);
        } else {
            BringWindowToTop(hwnd);
            SetForegroundWindow(hwnd);
        }
        qInfo() << "[WallpaperStudio] Win32 ForceForegroundWindow applied successfully to HWND:" << QString::number(reinterpret_cast<quintptr>(hwnd), 16);
    }
#endif
}"""

    content_norm = content.replace('\r\n', '\n')
    target_norm = target.replace('\r\n', '\n')
    replacement_norm = replacement.replace('\r\n', '\n')

    if target_norm in content_norm:
        new_content = content_norm.replace(target_norm, replacement_norm, 1)
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(new_content)
        print('SUCCESSFULLY PATCHED!')
    else:
        print('TARGET NOT FOUND')

if __name__ == '__main__':
    patch_file()
