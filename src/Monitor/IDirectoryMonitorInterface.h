#pragma once
#include <windows.h>
#include <QString>
#include <QDateTime>
#include "ShObjIdl_core.h"
#include "IListenerInterface.h"
// 文件系统事件类型枚举
enum class FileSystemEvent : uint32_t
{
    // 文件相关事件
    FileRenamed = 0x00000001L,  // SHCNE_RENAMEITEM
    FileCreated = 0x00000002L,  // SHCNE_CREATE
    FileDeleted = 0x00000004L,  // SHCNE_DELETE
    FileAttributesChanged = 0x00000008L,  // SHCNE_ATTRIBUTES
    FileUpdated = 0x00001000L,  // SHCNE_UPDATEITEM

    // 目录相关事件
    DirectoryCreated = 0x00000020L,  // SHCNE_MKDIR
    DirectoryDeleted = 0x00000040L,  // SHCNE_RMDIR
    DirectoryRenamed = 0x00020000L,  // SHCNE_RENAMEFOLDER
    DirectoryUpdated = 0x00002000L,  // SHCNE_UPDATEDIR

    // 驱动器相关事件
    DriveAdded = 0x00000100L,  // SHCNE_DRIVEADD
    DriveRemoved = 0x00000200L,  // SHCNE_DRIVEREMOVED
    DriveAddedGUI = 0x00010000L,  // SHCNE_DRIVEADDGUI

    // 媒体相关事件
    MediaInserted = 0x00000020L,  // SHCNE_MEDIAINSERTED
    MediaRemoved = 0x00000040L,  // SHCNE_MEDIAREMOVED

    // 网络相关事件
    NetworkShared = 0x00000200L,  // SHCNE_NETSHARE
    NetworkUnshared = 0x00000400L,  // SHCNE_NETUNSHARE
    ServerDisconnected = 0x00004000L,  // SHCNE_SERVERDISCONNECT

    // 系统相关事件
    FileAssociationChanged = 0x08000000L,  // SHCNE_ASSOCCHANGED

    // 复合事件掩码
    AllDiskEvents = 0x0002381FL,  // SHCNE_DISKEVENTS
    AllGlobalEvents = 0x0C0581E0L,  // SHCNE_GLOBALEVENTS
    AllEvents = 0x7FFFFFFFL,  // SHCNE_ALLEVENTS

    // 自定义组合
    AllFileEvents = FileRenamed | FileCreated | FileDeleted |
    FileAttributesChanged | FileUpdated,
    AllDirectoryEvents = DirectoryCreated | DirectoryDeleted |
    DirectoryRenamed | DirectoryUpdated,
    AllDriveEvents = DriveAdded | DriveRemoved | MediaInserted | MediaRemoved
};
// 文件系统事件信息结构
struct FileSystemEventInfo
{
    FileSystemEvent eventType;      // 事件类型
    QString         oldPath;        // 旧路径（重命名时使用）
    QString         newPath;        // 新路径
    QString         affectedPath;   // 受影响的路径
    QDateTime       timestamp;      // 事件时间戳
    bool            isDirectory;    // 是否是目录
    quint64         fileSize;       // 文件大小
    quint32         attributes;     // 文件属性

    FileSystemEventInfo()
        : eventType(FileSystemEvent(0))
        , isDirectory(false)
        , fileSize(0)
        , attributes(0)
    {
        timestamp = QDateTime::currentDateTime();
    }
};

class IDirectoryMonitorListener: public IListenerInterface
{
    Q_OBJECT
public:


    IDirectoryMonitorListener(const QString& name = "DirectoryMonitor", QObject* parent = nullptr)
        : IListenerInterface(name, "DirectoryMonitor", parent) {}
    // ========== 必须实现的纯虚函数 ==========

    // 监听控制
    virtual bool startListening() override = 0;
    virtual void stopListening() override = 0;
    
    
//    IDirectoryMonitorInterface(QObject* parent = nullptr);s
//    ~IDirectoryMonitorInterface();
//    void stopMonitoring();
//    void run();
//    // ========== IListenerInterface 接口实现 ==========
//    bool startListening() override;
//    void stopListening() override;
//
//    // ========== IRegistryListener 接口实现 ==========
//private:
//    HWND m_hwnd = nullptr;
//    ULONG m_regId = 0;
//
//
//    // Windows message window procedure
//    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
//
//    // Shell event handler
//    void handleShellEvent(LONG lEvent, LPITEMIDLIST* ppidl);
//
//    // Shell notification message
//    static const UINT WM_SHELLNOTIFY = WM_USER + 100;
};
