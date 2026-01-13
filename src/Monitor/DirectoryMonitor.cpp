#include "DirectoryMonitor.h"
#include "DirectoryMonitor.h"
#include <QDebug>
#include <QDateTime>
#include <shlobj.h>

// Static member initialization
DirectoryMonitor* instanceFromWindow(HWND hwnd) {
    return reinterpret_cast<DirectoryMonitor*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
}

// Window procedure
LRESULT CALLBACK DirectoryMonitor::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }

    DirectoryMonitor* self = instanceFromWindow(hwnd);

    if (self && msg == WM_SHELLNOTIFY) {
        LPITEMIDLIST* ppidl = nullptr;
        LONG lEvent = 0;

        HANDLE hNotifyLock = SHChangeNotification_Lock(
            reinterpret_cast<HANDLE>(wParam),
            static_cast<DWORD>(lParam),
            &ppidl,
            &lEvent
        );

        if (hNotifyLock) {
            self->handleShellEvent(lEvent, ppidl);
            SHChangeNotification_Unlock(hNotifyLock);
        }
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// Constructor
DirectoryMonitor::DirectoryMonitor(HWND hwnd,QObject* parent) : 
    IDirectoryMonitorListener("Desktop Directory Monitor", parent)
    ,m_hwnd(hwnd)
{
    logMessage("DesktopIconsMonitor created");
    // Register Windows window class
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"DirectoryMonitorWindow";
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (!RegisterClassEx(&wc)) {
        qDebug() << "Failed to register window class!";
    }

    // Create message-only window
    m_hwnd = CreateWindowEx(
        0,
        L"DirectoryMonitorWindow",
        L"Directory Monitor",
        0, 0, 0, 0, 0,
        HWND_MESSAGE,  // Message-only window
        nullptr,
        GetModuleHandle(nullptr),
        this
    );
}
// Destructor
DirectoryMonitor::~DirectoryMonitor() {
    stopMonitoring();

    if (m_hwnd) {
        DestroyWindow(m_hwnd);
    }
    stopListening();
    logMessage("Directory Monitor destroyed");
}

// Start monitorings
void DirectoryMonitor::startMonitoring() {
    const wchar_t* directoryPath = L"D://Desktop";
    if (!m_hwnd) {
        qDebug() << "Cannot start monitoring: Window not created";
        return;
    }

    // Create PIDL for the directory
    LPITEMIDLIST pidl = nullptr;
    if (directoryPath) {
        pidl = ILCreateFromPath(directoryPath);
        if (!pidl) {
            qDebug() << "Failed to create PIDL for:" << QString::fromWCharArray(directoryPath);
            return;
        }
    }

    // Setup monitoring entry
    SHChangeNotifyEntry entry = { pidl, TRUE };

    // Register for shell notifications
    HRESULT hr = SHChangeNotifyRegister(
        m_hwnd,
        SHCNRF_ShellLevel | SHCNRF_NewDelivery | SHCNRF_InterruptLevel,
        SHCNE_RENAMEITEM | SHCNE_CREATE | SHCNE_DELETE |
        SHCNE_MKDIR | SHCNE_RMDIR | SHCNE_RENAMEFOLDER |
        SHCNE_UPDATEITEM | SHCNE_UPDATEDIR | SHCNE_ATTRIBUTES,
        WM_SHELLNOTIFY,
        1,
        &entry
    );

    // Clean up PIDL
    if (pidl) {
        ILFree(pidl);
    }

    if (SUCCEEDED(hr)) {
        if (directoryPath) {
            qDebug() << "✅ Started monitoring directory:" << QString::fromWCharArray(directoryPath);
        }
        else {
            qDebug() << "✅ Started monitoring entire file system";
        }
        return;
    }
    else {
        qDebug() << "❌ Failed to start monitoring";
        return;
    }
}

// Stop monitoring
void DirectoryMonitor::stopMonitoring() {
    if (m_regId) {
        SHChangeNotifyDeregister(m_regId);
        m_regId = 0;
        qDebug() << "Monitoring stopped";
    }
}

// Handle shell events
void DirectoryMonitor::handleShellEvent(LONG lEvent, LPITEMIDLIST* ppidl) {
    QString eventDescription;
    QString filePath;

    // Extract file path from PIDL
    if (ppidl && ppidl[0]) {
        wchar_t path[MAX_PATH] = { 0 };
        if (SHGetPathFromIDListW(ppidl[0], path)) {
            filePath = QString::fromWCharArray(path);
        }
    }

    // Map event codes to descriptions
    switch (lEvent) {
    case SHCNE_RENAMEITEM:
        eventDescription = "File renamed";
        break;
    case SHCNE_CREATE:
        eventDescription = "File created";
        break;
    case SHCNE_DELETE:
        eventDescription = "File deleted";
        break;
    case SHCNE_MKDIR:
        eventDescription = "Directory created";
        break;
    case SHCNE_RMDIR:
        eventDescription = "Directory deleted";
        break;
    case SHCNE_RENAMEFOLDER:
        eventDescription = "Directory renamed";
        break;
    case SHCNE_UPDATEITEM:
        eventDescription = "File updated";
        break;
    case SHCNE_UPDATEDIR:
        eventDescription = "Directory updated";
        break;
    case SHCNE_ATTRIBUTES:
        eventDescription = "File attributes changed";
        break;
    case SHCNE_MEDIAREMOVED:
        eventDescription = "Media removed";
        break;
    case SHCNE_DRIVEADD:
        eventDescription = "Drive added";
        break;
    case SHCNE_DRIVEREMOVED:
        eventDescription = "Drive removed";
        break;
    case SHCNE_ASSOCCHANGED:
        eventDescription = "File association changed";
        break;
    case SHCNE_SERVERDISCONNECT:
        eventDescription = "Server disconnected";
        break;
    default:
        eventDescription = QString("Unknown event: 0x%1").arg(lEvent, 0, 16);
    }

    // Log the event
    QString timestamp = QDateTime::currentDateTime().toString("[hh:mm:ss.zzz]");
    QString logMessage = QString("%1 %2").arg(timestamp).arg(eventDescription);

    if (!filePath.isEmpty()) {
        logMessage += QString(": %1").arg(filePath);
    }

    qDebug() << logMessage;
}

// Run message loop
void DirectoryMonitor::run() {
    qDebug() << "Directory Monitor is running. Press Ctrl+C to exit.";

    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

bool DirectoryMonitor::startListening()
{
    return false;
}

void DirectoryMonitor::stopListening()
{
}

