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

inline QString userIconCacheDir()
{
    const QString dirPath = QDir(userConfigDir()).filePath(QStringLiteral("IconCache"));
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

inline bool isDebugOrDevMode()
{
    // 单元测试执行时不切换为本地程序目录，保持测试环境独立
    if (QCoreApplication::applicationName().contains(QStringLiteral("Test"), Qt::CaseInsensitive)) {
        return false;
    }

#if defined(QT_DEBUG) || !defined(NDEBUG) || defined(_DEBUG)
    return true;
#else
    if (qEnvironmentVariableIsSet("LINDESK_DEV_MODE")) {
        return true;
    }
    const QString appDir = QDir::cleanPath(QCoreApplication::applicationDirPath());
    if (appDir.contains(QStringLiteral("build/"), Qt::CaseInsensitive) ||
        appDir.contains(QStringLiteral("build\\"), Qt::CaseInsensitive)) {
        return true;
    }
    return false;
#endif
}

inline QString userLinWidgetsDir()
{
    if (isDebugOrDevMode()) {
        const QString appResourcesWidgets = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/LinWidgets"));
        if (QDir(appResourcesWidgets).exists()) {
            return appResourcesWidgets;
        }
        const QString appWidgets = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("LinWidgets"));
        if (QDir(appWidgets).exists()) {
            return appWidgets;
        }
    }

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
    if (isDebugOrDevMode()) {
        const QString appResourcesWallpapers = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/wallpapers"));
        if (QDir(appResourcesWallpapers).exists()) {
            return appResourcesWallpapers;
        }
        const QString appWallpapers = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("wallpapers"));
        if (QDir(appWallpapers).exists()) {
            return appWallpapers;
        }
    }

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

inline QString userLinWidgetsDataDir()
{
    return linWidgetDataDir();
}

inline QString appDataDir()
{
    return userConfigDir();
}

inline QString userRuntimeDir()
{
    const QString dirPath = QDir(userLocalConfigDir()).filePath(QStringLiteral("runtime"));
    QDir().mkpath(dirPath);
    return dirPath;
}

inline QString linWidgetRuntimeDir()
{
    const QString dirPath = QDir(userRuntimeDir()).filePath(QStringLiteral("linwidget"));
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
