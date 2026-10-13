#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QString>

namespace AppPaths {

inline QString userConfigDir()
{
    QString basePath = qEnvironmentVariable("APPDATA");
    QString dirPath;
    if (!basePath.isEmpty()) {
        dirPath = QDir(basePath).filePath(QStringLiteral("LinDesk"));
    } else {
        dirPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    if (dirPath.isEmpty()) {
        dirPath = QDir::home().filePath(QStringLiteral("LinDesk"));
    }
    QDir().mkpath(dirPath);
    return dirPath;
}

inline QString userPluginConfigDir()
{
    const QString dirPath = QDir(userConfigDir()).filePath(QStringLiteral("plugins"));
    QDir().mkpath(dirPath);
    return dirPath;
}

inline QString userLocalConfigDir()
{
    QString basePath = qEnvironmentVariable("LOCALAPPDATA");
    QString dirPath;
    if (!basePath.isEmpty()) {
        dirPath = QDir(basePath).filePath(QStringLiteral("LinDesk"));
    } else {
        dirPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    }
    if (dirPath.isEmpty()) {
        dirPath = QDir::home().filePath(QStringLiteral("LinDesk"));
    }
    QDir().mkpath(dirPath);
    return dirPath;
}

inline QString userLogsDir()
{
    const QString dirPath = QDir(userLocalConfigDir()).filePath(QStringLiteral("logs"));
    QDir().mkpath(dirPath);
    return dirPath;
}

inline QString userCrashDumpsDir()
{
    const QString dirPath = QDir(userLogsDir()).filePath(QStringLiteral("crash"));
    QDir().mkpath(dirPath);
    return dirPath;
}

inline QString userLinWidgetsDir()
{
    const QString dirPath = QDir(userConfigDir()).filePath(QStringLiteral("LinWidgets"));
    QDir().mkpath(dirPath);
    return dirPath;
}

inline QString linWidgetRegistryFilePath()
{
    return QDir(userLinWidgetsDir()).filePath(QStringLiteral("registry.json"));
}

inline QString userWallpapersDir()
{
    const QString dirPath = QDir(userConfigDir()).filePath(QStringLiteral("LinWallpapers"));
    QDir().mkpath(dirPath);
    return dirPath;
}

inline QString wallpaperRegistryFilePath()
{
    return QDir(userWallpapersDir()).filePath(QStringLiteral("registry.json"));
}

inline QString runtimeWallpapersDir()
{
    const QString dirPath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/wallpapers"));
    QDir().mkpath(dirPath);
    return dirPath;
}

inline QString linWidgetLogsDir()
{
    const QString dirPath = userLogsDir();
    QDir().mkpath(dirPath);
    return dirPath;
}

inline QString linWidgetDataDir()
{
    const QString dirPath = QDir(userConfigDir()).filePath(QStringLiteral("LinWidgetsData"));
    QDir().mkpath(dirPath);
    return dirPath;
}

inline QString configFilePath()
{
    return QDir(userConfigDir()).filePath(QStringLiteral("config.ini"));
}

inline QString desktopDataFilePath()
{
#ifdef QT_DEBUG
    return QDir(userConfigDir()).filePath(QStringLiteral("data.json"));
#else
    return QDir(userConfigDir()).filePath(QStringLiteral("data.dat"));
#endif
}

inline QString shellMenuExclusionsFilePath()
{
#ifdef QT_DEBUG
    return QDir(userConfigDir()).filePath(QStringLiteral("Menuexclusions.json"));
#else
    return QDir(userConfigDir()).filePath(QStringLiteral("Menuexclusions.dat"));
#endif
}

inline QString ocrConfigFilePath()
{
    return QDir(userPluginConfigDir()).filePath(QStringLiteral("ocr_config.ini"));
}

}
