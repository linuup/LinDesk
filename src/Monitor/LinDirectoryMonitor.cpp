#include "monitor/LinDirectoryMonitor.h"
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <memory>
#include <cstring>

// Constructor
LinDirectoryMonitor::LinDirectoryMonitor(const QString& path, bool recursive, QObject* parent) : 
    IDirectoryMonitorListener("Lin Directory Monitor", parent)
    , m_path(path)
    , m_recursiveMonitor(recursive)
    , m_running(false)
{
    // 鏍煎紡鍖栬矾寰?
    m_path = QDir::toNativeSeparators(m_path);
    if (m_path.endsWith(QDir::separator())) {
        m_path.chop(1);
    }
    logMessage("LinDirectoryMonitor created");
}

// Destructor
LinDirectoryMonitor::~LinDirectoryMonitor() {
    stopListening();
    logMessage("LinDirectoryMonitor destroyed");
}

// Start Listening
bool LinDirectoryMonitor::startListening() {
    if (m_running) {
        qDebug() << "LinDirectoryMonitor already running";
        return true;
    }

    // 鎵撳紑鐩綍鍙ユ焺
    m_hDirectory = CreateFileW(
        (LPCWSTR)m_path.toStdWString().c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr
    );

    if (m_hDirectory == INVALID_HANDLE_VALUE) {
        qDebug() << "鉂?Failed to open directory:" << m_path << "Error:" << GetLastError();
        emit errorOccurred("Failed to open directory");
        return false;
    }

    // 鍒涘缓鍋滄浜嬩欢
    m_hStopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!m_hStopEvent) {
        CloseHandle(m_hDirectory);
        m_hDirectory = INVALID_HANDLE_VALUE;
        qDebug() << "鉂?Failed to create stop event";
        return false;
    }

    m_running = true;
    m_thread = std::make_unique<std::thread>(&LinDirectoryMonitor::monitorThread, this);
    
    qDebug() << "鉁?Started monitoring directory:" << m_path;
    return true;
}

// Stop Listening
void LinDirectoryMonitor::stopListening()
{
    if (!m_running) return;

    m_running = false;

    // 瑙﹀彂鍋滄浜嬩欢
    if (m_hStopEvent) {
        SetEvent(m_hStopEvent);
    }
    if (m_hDirectory != INVALID_HANDLE_VALUE) {
        CancelIoEx(m_hDirectory, nullptr);
    }

    // 绛夊緟绾跨▼缁撴潫
    if (m_thread && m_thread->joinable()) {
        m_thread->join();
    }
    m_thread.reset();

    // 鍏抽棴鍙ユ焺
    if (m_hDirectory != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hDirectory);
        m_hDirectory = INVALID_HANDLE_VALUE;
    }

    if (m_hStopEvent) {
        CloseHandle(m_hStopEvent);
        m_hStopEvent = NULL;
    }

    qDebug() << "Monitoring stopped";
}

// 鐩戞帶绾跨▼
void LinDirectoryMonitor::monitorThread()
{
    DWORD notifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME |
                         FILE_NOTIFY_CHANGE_DIR_NAME |
                         FILE_NOTIFY_CHANGE_ATTRIBUTES |
                         FILE_NOTIFY_CHANGE_SIZE |
                         FILE_NOTIFY_CHANGE_LAST_WRITE |
                         FILE_NOTIFY_CHANGE_CREATION;

    while (m_running) {
        DWORD bytesReturned = 0;
        
        // 鍚屾璋冪敤 ReadDirectoryChangesW
        BOOL success = ReadDirectoryChangesW(
            m_hDirectory,
            m_buffer,
            LIN_BUFFER_SIZE,
            m_recursiveMonitor,
            notifyFilter,
            &bytesReturned,
            nullptr,
            nullptr
        );

        if (!m_running) break;

        if (success && bytesReturned > 0) {
            FILE_NOTIFY_INFORMATION* pNotify = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(m_buffer);
            processNotification(pNotify);
        } else {
            DWORD error = GetLastError();
            if (error == ERROR_OPERATION_ABORTED) {
                break;
            }
            qDebug() << "ReadDirectoryChangesW failed, error:" << error;
            Sleep(100);
        }
    }
}

// 澶勭悊閫氱煡
void LinDirectoryMonitor::processNotification(FILE_NOTIFY_INFORMATION* info)
{
    while (true) {
        std::wstring fileName(info->FileName, info->FileNameLength / sizeof(wchar_t));
        QString qFileName = QString::fromStdWString(fileName);
        QString fullPath = m_path + QDir::separator() + qFileName;

        FileSystemEventInfo eventInfo;
        eventInfo.timestamp = QDateTime::currentDateTime();
        
        // 妫€鏌ユ槸鍚︽槸鐩綍
        QFileInfo fileInfo(fullPath);
        bool isDirectory = fileInfo.exists() ? fileInfo.isDir() : false;
        eventInfo.isDirectory = isDirectory;

        switch (info->Action) {
        case FILE_ACTION_ADDED:
            if (isDirectory) {
                eventInfo.eventType = FileSystemEvent::DirectoryCreated;
                qDebug() << "[DirectoryCreated]" << fullPath;
            } else {
                eventInfo.eventType = FileSystemEvent::FileCreated;
                qDebug() << "[FileCreated]" << fullPath;
            }
            eventInfo.newPath = fullPath;
            break;
            
        case FILE_ACTION_REMOVED:
            // 鍒犻櫎鏃舵枃浠跺凡涓嶅瓨鍦?鏃犳硶鍒ゆ柇绫诲瀷
            // 鏍规嵁杩囨护鍣?DIR_NAME 鍙樺寲浼氬崟鐙€氱煡
            // 杩欓噷鏆傛椂閮界敤 FileDeleted,ListView 浼氬鐞?
            eventInfo.eventType = FileSystemEvent::FileDeleted;
            eventInfo.oldPath = fullPath;
            qDebug() << "[FileDeleted]" << fullPath;

            break;
            
        case FILE_ACTION_MODIFIED:
            if (isDirectory) {
                eventInfo.eventType = FileSystemEvent::DirectoryUpdated;
                qDebug() << "[DirectoryUpdated]" << fullPath;
            } else {
                eventInfo.eventType = FileSystemEvent::FileUpdated;
                qDebug() << "[FileUpdated]" << fullPath;
            }
            eventInfo.newPath = fullPath;
            break;
            
        case FILE_ACTION_RENAMED_OLD_NAME:
            m_lastOldPath = fullPath;
            // 鏆傚瓨鏄惁鏄洰褰?濡傛灉杩樺瓨鍦ㄧ殑璇?
            m_lastOldPathIsDir = isDirectory;
            qDebug() << "[RenamedOld]" << fullPath << (isDirectory ? "(Dir)" : "(File)");
            break;
            
        case FILE_ACTION_RENAMED_NEW_NAME:
            if (isDirectory || m_lastOldPathIsDir) {
                eventInfo.eventType = FileSystemEvent::DirectoryRenamed;
                qDebug() << "[DirectoryRenamed]" << m_lastOldPath << "->" << fullPath;
            } else {
                eventInfo.eventType = FileSystemEvent::FileRenamed;
                qDebug() << "[FileRenamed]" << m_lastOldPath << "->" << fullPath;
            }
            eventInfo.oldPath = m_lastOldPath;
            eventInfo.newPath = fullPath;
            emit fileSystemChanged(eventInfo);
            m_lastOldPath.clear();
            m_lastOldPathIsDir = false;
            break;
            
        default:
            break;
        }

        // 鍙戦€侀潪閲嶅懡鍚嶄簨浠?
        if (info->Action != FILE_ACTION_RENAMED_OLD_NAME && 
            info->Action != FILE_ACTION_RENAMED_NEW_NAME) {
            emit fileSystemChanged(eventInfo);
        }

        if (info->NextEntryOffset == 0) break;
        
        info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
            reinterpret_cast<char*>(info) + info->NextEntryOffset
        );
    }
}

// Windows 浜嬩欢杞崲
FileSystemEvent LinDirectoryMonitor::fromWinEvent(DWORD action) {
    switch (action) {
    case FILE_ACTION_ADDED:           return FileSystemEvent::FileCreated;
    case FILE_ACTION_REMOVED:         return FileSystemEvent::FileDeleted;
    case FILE_ACTION_MODIFIED:        return FileSystemEvent::FileUpdated;
    case FILE_ACTION_RENAMED_OLD_NAME: return FileSystemEvent::FileRenamed;
    case FILE_ACTION_RENAMED_NEW_NAME: return FileSystemEvent::FileRenamed;
    default:                          return FileSystemEvent(0);
    }
}

// 浜嬩欢杞瓧绗︿覆
QString LinDirectoryMonitor::eventToString(FileSystemEvent event)
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

