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

inline QString ocrConfigFilePath()
{
    return QDir(userPluginConfigDir()).filePath(QStringLiteral("ocr_config.ini"));
}

}
