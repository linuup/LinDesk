import os

def patch_file():
    path = 'src/desktop/LinDesktop.cpp'
    with open(path, 'r', encoding='utf-8') as f:
        content = f.read()

    target = """    dialog->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    dialog->setDialogTitle(QCoreApplication::translate("LinDesktop", "LinDesk is Already Running"));
    dialog->setMessage(QCoreApplication::translate("LinDesktop",
        "LinDesk is already running in the background, but the desktop presentation may have been obscured by the Windows desktop or third-party software.\\n\\n"
        "• Click [Restore Desktop] to bring LinDesk back to the front.\\n"
        "• Click [Cancel] to keep the current desktop and show the icon in the system tray."));"""

    replacement = """    dialog->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    dialog->setDialogTitle(QCoreApplication::translate("LinDesktop", "LinDesk is Already Running"));
    dialog->setMessage(QCoreApplication::translate("LinDesktop",
        "LinDesk is already running in the background, but the desktop presentation may have been obscured by the Windows desktop or third-party software.\\n\\n"
        "• Click [Restore Desktop] to bring LinDesk back to the front.\\n"
        "• Click [Cancel] to keep the current desktop and show the icon in the system tray."));"""

    target2 = """    dialog->show();
    const QPoint center = screenRect.center() - QPoint(dialog->width() / 2, dialog->height() / 2);
    dialog->move(center);
    dialog->raise();
    dialog->activateWindow();"""

    replacement2 = """    dialog->show();
    const QPoint center = screenRect.center() - QPoint(dialog->width() / 2, dialog->height() / 2);
    dialog->move(center);
    dialog->raise();
    dialog->activateWindow();

#ifdef Q_OS_WIN
    HWND dlgHwnd = reinterpret_cast<HWND>(dialog->winId());
    if (dlgHwnd && IsWindow(dlgHwnd)) {
        LONG_PTR exStyle = GetWindowLongPtr(dlgHwnd, GWL_EXSTYLE);
        exStyle |= WS_EX_APPWINDOW;
        exStyle &= ~WS_EX_TOOLWINDOW;
        SetWindowLongPtr(dlgHwnd, GWL_EXSTYLE, exStyle);

        ShowWindow(dlgHwnd, SW_SHOW);
        SetWindowPos(dlgHwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);

        HWND foreHwnd = GetForegroundWindow();
        DWORD foreThread = foreHwnd ? GetWindowThreadProcessId(foreHwnd, nullptr) : 0;
        DWORD appThread = GetCurrentThreadId();
        if (foreThread != 0 && foreThread != appThread) {
            AttachThreadInput(foreThread, appThread, TRUE);
            BringWindowToTop(dlgHwnd);
            SetForegroundWindow(dlgHwnd);
            AttachThreadInput(foreThread, appThread, FALSE);
        } else {
            BringWindowToTop(dlgHwnd);
            SetForegroundWindow(dlgHwnd);
        }
    }
#endif"""

    content_norm = content.replace('\r\n', '\n')
    target_norm = target.replace('\r\n', '\n')
    replacement_norm = replacement.replace('\r\n', '\n')
    target2_norm = target2.replace('\r\n', '\n')
    replacement2_norm = replacement2.replace('\r\n', '\n')

    if target_norm in content_norm and target2_norm in content_norm:
        content_norm = content_norm.replace(target_norm, replacement_norm, 1)
        content_norm = content_norm.replace(target2_norm, replacement2_norm, 1)
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(content_norm)
        print('SUCCESSFULLY PATCHED LINDESKTOP!')
    else:
        print('TARGET NOT FOUND')

if __name__ == '__main__':
    patch_file()
