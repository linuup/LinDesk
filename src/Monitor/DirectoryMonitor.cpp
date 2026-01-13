// DirectoryMonitor.cpp
#include "DirectoryMonitor.h"
#include <QDebug>
#include <QFileInfo>
#include <QRegularExpression>

DirectoryMonitor::DirectoryMonitor(QObject* parent)
    : QObject(parent)
    , m_watcher(new QFileSystemWatcher(this))
    , m_recursiveWatch(true)
    , m_checkInterval(1000)
    , m_isRunning(false)
    , m_isPaused(false)
{
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(500);

    connect(m_watcher, &QFileSystemWatcher::fileChanged,
        this, &DirectoryMonitor::onFileChanged);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
        this, &DirectoryMonitor::onDirectoryChanged);
    connect(m_debounceTimer, &QTimer::timeout,
        this, &DirectoryMonitor::processDebouncedEvents);
}

DirectoryMonitor::~DirectoryMonitor()
{
    stop();
}

bool DirectoryMonitor::addWatchPath(const QString& path, bool recursive)
{
    QFileInfo info(path);
    if (!info.exists()) {
        emit errorOccurred(QString("Path does not exist: %1").arg(path));
        return false;
    }

    if (m_watchedPaths.contains(path)) {
        emit errorOccurred(QString("Path is already being monitored: %1").arg(path));
        return false;
    }

    if (info.isDir()) {
        if (!m_watcher->addPath(path)) {
            emit errorOccurred(QString("Failed to monitor directory: %1").arg(path));
            return false;
        }

        m_watchedPaths.insert(path);
        m_knownDirectories.insert(path); // 记录已知目录
        m_recursiveWatch = recursive;
        scanDirectory(path);

        if (recursive) {
            addRecursiveWatches(path);
        }

        emit monitoringStarted(path);
        qDebug() << "Started monitoring directory:" << path;
        return true;
    }
    else {
        if (!m_watcher->addPath(path)) {
            emit errorOccurred(QString("Failed to monitor file: %1").arg(path));
            return false;
        }

        m_watchedPaths.insert(path);
        updateFileInfo(path);

        emit monitoringStarted(path);
        qDebug() << "Started monitoring file:" << path;
        return true;
    }
}

void DirectoryMonitor::addRecursiveWatches(const QString& directory)
{
    QDir dir(directory);
    dir.setFilter(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);

    for (const QFileInfo& subDir : dir.entryInfoList()) {
        QString subDirPath = subDir.absoluteFilePath();
        if (!m_watchedPaths.contains(subDirPath)) {
            if (m_watcher->addPath(subDirPath)) {
                m_watchedPaths.insert(subDirPath);
                m_knownDirectories.insert(subDirPath); // 记录已知目录
                addRecursiveWatches(subDirPath);
            }
        }
    }
}

void DirectoryMonitor::scanDirectory(const QString& path)
{
    QDir dir(path);

    // 扫描文件
    auto files = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo& fileInfo : files) {
        QString filePath = fileInfo.absoluteFilePath();
        if (shouldFilterFile(filePath)) {
            continue;
        }
        updateFileInfo(filePath);
    }

    // 记录当前目录下的所有子目录
    auto dirs = dir.entryInfoList(QDir::AllDirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& dirInfo : dirs) {
        m_knownDirectories.insert(dirInfo.absoluteFilePath());
    }
}

void DirectoryMonitor::updateFileInfo(const QString& filePath)
{
    QFileInfo info(filePath);
    if (info.exists()) {
        m_fileTimestamps[filePath] = info.lastModified();
        m_fileSizes[filePath] = info.size();
    }
}

void DirectoryMonitor::onFileChanged(const QString& path)
{
    if (m_isPaused || !m_isRunning) return;

    QFileInfo info(path);

    if (!info.exists()) {
        FileEvent event(FileDeleted, path);
        m_pendingEvents[path] = event;
    }
    else {
        QDateTime lastModified = info.lastModified();
        qint64 fileSize = info.size();

        if (!m_fileTimestamps.contains(path)) {
            FileEvent event(FileCreated, path);
            event.fileSize = fileSize;
            m_pendingEvents[path] = event;
        }
        else if (m_fileTimestamps[path] != lastModified ||
            m_fileSizes[path] != fileSize) {
            FileEvent event(FileModified, path);
            event.fileSize = fileSize;
            m_pendingEvents[path] = event;
        }

        m_fileTimestamps[path] = lastModified;
        m_fileSizes[path] = fileSize;
        m_watcher->addPath(path);
    }

    m_debounceTimer->start();
}

void DirectoryMonitor::onDirectoryChanged(const QString& path)
{
    if (m_isPaused || !m_isRunning) return;

    emit directoryChanged(path);
    checkDirectoryChanges(path);
    m_debounceTimer->start();
}

void DirectoryMonitor::checkDirectoryChanges(const QString& path)
{
    QDir dir(path);

    // 检查文件变化
    auto currentFiles = dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QString& fileName : currentFiles) {
        QString filePath = dir.absoluteFilePath(fileName);
        if (!m_fileTimestamps.contains(filePath) && !shouldFilterFile(filePath)) {
            FileEvent event(FileCreated, filePath);
            m_pendingEvents[filePath] = event;
            updateFileInfo(filePath);
        }
    }

    // 检查文件删除
    auto oldFiles = m_fileTimestamps.keys();
    for (const QString& oldFile : oldFiles) {
        if (QFileInfo(oldFile).absolutePath() == path && !QFile::exists(oldFile)) {
            FileEvent event(FileDeleted, oldFile);
            m_pendingEvents[oldFile] = event;
            m_fileTimestamps.remove(oldFile);
            m_fileSizes.remove(oldFile);
        }
    }

    // 检查目录变化
    auto currentDirs = dir.entryList(QDir::AllDirs | QDir::NoDotAndDotDot);
    QSet<QString> currentDirPaths;

    for (const QString& dirName : currentDirs) {
        QString dirPath = dir.absoluteFilePath(dirName);
        currentDirPaths.insert(dirPath);

        // 检测新增目录
        if (!m_knownDirectories.contains(dirPath)) {
            m_knownDirectories.insert(dirPath);
            emit directoryCreated(dirPath);
            qDebug() << "Directory created:" << dirPath;
        }
    }

    // 检测删除的目录
    QSet<QString> knownDirsCopy = m_knownDirectories;
    for (const QString& knownDir : knownDirsCopy) {
        if (QFileInfo(knownDir).absolutePath() == path && !currentDirPaths.contains(knownDir)) {
            m_knownDirectories.remove(knownDir);
            emit directoryRemoved(knownDir);
            qDebug() << "Directory removed:" << knownDir;
        }
    }

    if (m_recursiveWatch) {
        auto watchedDirs = m_watchedPaths;

        for (const QString& watchedDir : watchedDirs) {
            if (QFileInfo(watchedDir).absolutePath() == path &&
                !currentDirs.contains(QFileInfo(watchedDir).fileName())) {
                removeWatchPath(watchedDir);
            }
        }

        for (const QString& subDir : currentDirs) {
            QString subDirPath = dir.absoluteFilePath(subDir);
            if (!m_watchedPaths.contains(subDirPath)) {
                addWatchPath(subDirPath, true);
            }
        }
    }
}

void DirectoryMonitor::processDebouncedEvents()
{
    if (m_pendingEvents.isEmpty()) return;

    for (auto it = m_pendingEvents.begin(); it != m_pendingEvents.end(); ++it) {
        const QString& filePath = it.key();
        const FileEvent& event = it.value();

        // 发射信号，第二个参数是被修改文件的路径
        emit fileChanged(event, filePath);
    }
    m_pendingEvents.clear();
}

bool DirectoryMonitor::shouldFilterFile(const QString& filePath)
{
    QFileInfo info(filePath);
    QString fileName = info.fileName();

    for (const QString& exclude : m_excludeFilters) {
        QRegularExpression re(exclude, QRegularExpression::CaseInsensitiveOption);
        if (re.match(fileName).hasMatch()) {
            return true;
        }
    }

    if (!m_includeFilters.isEmpty()) {
        for (const QString& include : m_includeFilters) {
            QRegularExpression re(include, QRegularExpression::CaseInsensitiveOption);
            if (re.match(fileName).hasMatch()) {
                return false;
            }
        }
        return true;
    }

    return false;
}

void DirectoryMonitor::setFileFilters(const QStringList& includeFilters, const QStringList& excludeFilters)
{
    m_includeFilters = includeFilters;
    m_excludeFilters = excludeFilters;
}

void DirectoryMonitor::setDebounceInterval(int milliseconds)
{
    m_debounceTimer->setInterval(milliseconds);
}

void DirectoryMonitor::start()
{
    m_isRunning = true;
    m_isPaused = false;
    qDebug() << "Directory monitoring started";
}

void DirectoryMonitor::stop()
{
    m_isRunning = false;
    m_isPaused = false;
    m_watcher->removePaths(m_watcher->files());
    m_watcher->removePaths(m_watcher->directories());
    m_watchedPaths.clear();
    m_knownDirectories.clear(); // 清空已知目录
    m_fileTimestamps.clear();
    m_fileSizes.clear();
    m_pendingEvents.clear();
    qDebug() << "Directory monitoring stopped";
}

void DirectoryMonitor::pause()
{
    m_isPaused = true;
    qDebug() << "Directory monitoring paused";
}

void DirectoryMonitor::resume()
{
    m_isPaused = false;
    qDebug() << "Directory monitoring resumed";
}

bool DirectoryMonitor::removeWatchPath(const QString& path)
{
    if (m_watcher->removePath(path)) {
        m_watchedPaths.remove(path);
        m_knownDirectories.remove(path); // 从已知目录中移除
        m_fileTimestamps.remove(path);
        m_fileSizes.remove(path);
        emit monitoringStopped(path);
        return true;
    }
    return false;
}

QSet<QString> DirectoryMonitor::getWatchedPaths() const
{
    return m_watchedPaths;
}