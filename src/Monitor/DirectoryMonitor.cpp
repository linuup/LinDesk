#include "monitor/DirectoryMonitor.h"
#include <QDebug>
#include <QDateTime>
#include <QStandardPaths>
#include <shlobj.h>

// Static member initialization
//DirectoryMonitor* instanceFromWindow(HWND hwnd) {
    //return reinterpret_cast<DirectoryMonitor*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
//}

// Window procedure
//LRESULT CALLBACK DirectoryMonitor::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // if (msg == WM_CREATE) {
    //     CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
    //     SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    // }

    // DirectoryMonitor* self = instanceFromWindow(hwnd);

    // if (self && msg == WM_SHELLNOTIFY) {
    //     LPITEMIDLIST* ppidl = nullptr;
    //     LONG lEvent = 0;

    //     HANDLE hNotifyLock = SHChangeNotification_Lock(
    //         reinterpret_cast<HANDLE>(wParam),
    //         static_cast<DWORD>(lParam),
    //         &ppidl,
    //         &lEvent
    //     );

    //     if (hNotifyLock) {
    //         self->handleShellEvent(lEvent, ppidl);
    //         SHChangeNotification_Unlock(hNotifyLock);
    //     }
    //     return 0;
    // }

    //return DefWindowProc(hwnd, msg, wParam, lParam);
//}

// Constructor
DirectoryMonitor::DirectoryMonitor(HWND hwnd,bool recursive,QObject* parent) : 
    IDirectoryMonitorListener("Desktop Directory Monitor", parent)
    ,m_recursiveMonitor(recursive),m_hwnd(hwnd)
{
    logMessage("DesktopIconsMonitor created");
}
// Destructor
DirectoryMonitor::~DirectoryMonitor() {
    stopListening();

    if (m_hwnd) {
        DestroyWindow(m_hwnd);
    }
    stopListening();
    logMessage("Directory Monitor destroyed");
}

// Start Listening
bool DirectoryMonitor::startListening() {
    std::wstring desktopStd = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation).toStdWString();
    const wchar_t* directoryPath = desktopStd.c_str();
    if (!m_hwnd) {
        qDebug() << "Cannot start monitoring: Window not created";
        return false;
    }
    // Create PIDL for the directory
    LPITEMIDLIST pidl = nullptr;
    if (directoryPath) {
        pidl = ILCreateFromPath(directoryPath);
        if (!pidl) {
            qDebug() << "Failed to create PIDL for:" << QString::fromWCharArray(directoryPath);
            return false;
        }
    }

    // Setup monitoring entry
    SHChangeNotifyEntry entry = { pidl, m_recursiveMonitor };

    // Register for shell notifications
    HRESULT hr = SHChangeNotifyRegister(
        m_hwnd,
        SHCNRF_ShellLevel | SHCNRF_NewDelivery | SHCNRF_InterruptLevel,
        SHCNE_RENAMEITEM | SHCNE_CREATE | SHCNE_DELETE |
        SHCNE_MKDIR | SHCNE_RMDIR | SHCNE_RENAMEFOLDER |
        SHCNE_UPDATEITEM | SHCNE_UPDATEDIR | SHCNE_ATTRIBUTES,
        WM_USER + 52,
        1,
        &entry
    );

    // Clean up PIDL
    if (pidl) {
        ILFree(pidl);
    }

    if (SUCCEEDED(hr)) {
        if (directoryPath) {
            qDebug() << "Started monitoring directory:" << QString::fromWCharArray(directoryPath);
        }
        else {
            qDebug() << "Started monitoring entire file system";
        }
        return true;
    }
    else {
        qDebug() << "❌ Failed to start monitoring";
        return false;
    }
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

void DirectoryMonitor::stopListening()
{
    if (m_regId) {
        SHChangeNotifyDeregister(m_regId);
        m_regId = 0;
        qDebug() << "Monitoring stopped";
    }
}

QString DirectoryMonitor::eventToString(FileSystemEvent event)
{

    switch (event) {
    case FileSystemEvent::FileRenamed:         return "FileRenamed";
    case FileSystemEvent::FileCreated:         return "FileCreated";
    case FileSystemEvent::FileDeleted:         return "FileDeleted";
    case FileSystemEvent::FileAttributesChanged: return "FileAttributesChanged";
    case FileSystemEvent::FileUpdated:         return "FileUpdated";
    case FileSystemEvent::DirectoryCreated:    return "DirectoryCreated";
    case FileSystemEvent::DirectoryDeleted:    return "DirectoryDeleted";
    case FileSystemEvent::DirectoryRenamed:    return "DirectoryRenamed";
    case FileSystemEvent::DirectoryUpdated:    return "DirectoryUpdated";
    case FileSystemEvent::DriveAdded:          return "DriveAdded";
    case FileSystemEvent::DriveRemoved:        return "DriveRemoved";
    case FileSystemEvent::NetworkUnshared:     return "NetworkUnshared";
    case FileSystemEvent::ServerDisconnected:  return "ServerDisconnected";
    case FileSystemEvent::FileAssociationChanged: return "FileAssociationChanged";
    default:                                  return "UnknownEvent";
    }
}

