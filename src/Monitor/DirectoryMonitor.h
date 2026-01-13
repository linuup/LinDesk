// DirectoryMonitor.h
#pragma once

#include <QObject>
#include <QFileSystemWatcher>
#include <QDir>
#include <QDateTime>
#include <QTimer>
#include <QSet>
#include <QMap>

class DirectoryMonitor : public QObject
{
    Q_OBJECT

public:
    enum EventType {
        FileCreated,
        FileModified,
        FileDeleted,
        FileRenamed,
        DirectoryCreated,
        DirectoryDeleted
    };

    struct FileEvent {
        EventType type;
        QString filePath;
        QString oldFilePath;
        QDateTime timestamp;
        qint64 fileSize;

        FileEvent()
            : type(FileCreated), filePath(""), oldFilePath(""),
            timestamp(QDateTime::currentDateTime()), fileSize(0) {}

        FileEvent(EventType t, const QString& path)
            : type(t), filePath(path), oldFilePath(""),
            timestamp(QDateTime::currentDateTime()), fileSize(0) {}

        FileEvent(const FileEvent& other)
            : type(other.type), filePath(other.filePath), oldFilePath(other.oldFilePath),
            timestamp(other.timestamp), fileSize(other.fileSize) {}

        FileEvent& operator=(const FileEvent& other) {
            if (this != &other) {
                type = other.type;
                filePath = other.filePath;
                oldFilePath = other.oldFilePath;
                timestamp = other.timestamp;
                fileSize = other.fileSize;
            }
            return *this;
        }
    };

    explicit DirectoryMonitor(QObject* parent = nullptr);
    ~DirectoryMonitor();

    bool addWatchPath(const QString& path, bool recursive = true);
    bool removeWatchPath(const QString& path);
    QSet<QString> getWatchedPaths() const;

    void setFileFilters(const QStringList& includeFilters, const QStringList& excludeFilters = {});
    void setCheckInterval(int milliseconds);
    void setDebounceInterval(int milliseconds);

public slots:
    void start();
    void stop();
    void pause();
    void resume();

signals:
    // 修改信号签名：第二个参数是被修改文件的路径
    void fileChanged(const FileEvent& event, const QString& filePath);
    void directoryChanged(const QString& path);
    // 添加文件夹新增信号
    void directoryCreated(const QString& path);
    void directoryRemoved(const QString& path);
    void errorOccurred(const QString& error);
    void monitoringStarted(const QString& path);
    void monitoringStopped(const QString& path);

private slots:
    void onFileChanged(const QString& path);
    void onDirectoryChanged(const QString& path);
    void processDebouncedEvents();

private:
    void setupWatcher();
    void scanDirectory(const QString& path);
    bool shouldFilterFile(const QString& filePath);
    void addRecursiveWatches(const QString& directory);
    void updateFileInfo(const QString& filePath);
    void checkDirectoryChanges(const QString& path);

    QFileSystemWatcher* m_watcher;
    QSet<QString> m_watchedPaths;
    QSet<QString> m_knownDirectories; // 新增：记录已知的目录
    QMap<QString, QDateTime> m_fileTimestamps;
    QMap<QString, qint64> m_fileSizes;

    QStringList m_includeFilters;
    QStringList m_excludeFilters;
    bool m_recursiveWatch;
    int m_checkInterval;
    bool m_isRunning;
    bool m_isPaused;

    QTimer* m_debounceTimer;
    QMap<QString, FileEvent> m_pendingEvents;
};