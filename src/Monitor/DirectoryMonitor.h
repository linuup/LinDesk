#pragma once
#include <windows.h>
#include <QString>
#include "ShObjIdl_core.h"
#include <QObject>
#include "monitor/IDirectoryMonitorInterface.h"

class DirectoryMonitor : public IDirectoryMonitorListener
{
public:
    DirectoryMonitor(HWND hwnd, bool recursive, QObject* parent = nullptr);
    virtual ~DirectoryMonitor();
    void run();
    // ========== IListenerInterface 接口实现 =========s=
    bool startListening() override;
    void stopListening() override;

    // ========== IRegistryListener 接口实现 ==========
    // Windows SHCNE 事件转换
        static FileSystemEvent fromWinEvent(LONG winEvent) {
        switch (winEvent) {
        case 0x00000001L: return FileSystemEvent::FileRenamed;         // SHCNE_RENAMEITEM
        case 0x00000002L: return FileSystemEvent::FileCreated;         // SHCNE_CREATE
        case 0x00000004L: return FileSystemEvent::FileDeleted;         // SHCNE_DELETE
        case 0x00000008L: return FileSystemEvent::FileAttributesChanged; // SHCNE_ATTRIBUTES
        case 0x00000020L: return FileSystemEvent::DirectoryCreated;    // SHCNE_MKDIR
        case 0x00000040L: return FileSystemEvent::DirectoryDeleted;    // SHCNE_RMDIR
        case 0x00000100L: return FileSystemEvent::DriveAdded;          // SHCNE_DRIVEADD
        case 0x00000200L: return FileSystemEvent::DriveRemoved;        // SHCNE_DRIVEREMOVED
        case 0x00001000L: return FileSystemEvent::FileUpdated;         // SHCNE_UPDATEITEM
        case 0x00002000L: return FileSystemEvent::DirectoryUpdated;    // SHCNE_UPDATEDIR
        case 0x00004000L: return FileSystemEvent::ServerDisconnected;  // SHCNE_SERVERDISCONNECT
        case 0x00008000L: return FileSystemEvent(0x00008000L);         // SHCNE_UPDATEIMAGE
        case 0x00010000L: return FileSystemEvent::DriveAddedGUI;       // SHCNE_DRIVEADDGUIs
        case 0x00020000L: return FileSystemEvent::DirectoryRenamed;    // SHCNE_RENAMEFOLDER
        case 0x00040000L: return FileSystemEvent(0x00040000L);         // SHCNE_FREESPACE
        case 0x04000000L: return FileSystemEvent(0x04000000L);         // SHCNE_EXTENDED_EVENT
        case 0x08000000L: return FileSystemEvent::FileAssociationChanged; // SHCNE_ASSOCCHANGED
        default:         return FileSystemEvent(0);
        }
    }
    QString eventToString(FileSystemEvent event);
private:
    HWND m_hwnd = nullptr;
    ULONG m_regId = 0;
    bool m_recursiveMonitor = false;


    // Windows message window procedure
    //static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Shell event handler
    //void handleShellEvent(LONG lEvent, LPITEMIDLIST* ppidl);

    // Shell notification message
    static const UINT WM_SHELLNOTIFY = WM_USER + 52;
};
