#include "services/AppPaths.h"
#include <QProcess>
#include <QUuid>
#include "desktop/widgets/linwidget/WidgetPackage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>

namespace LinWidget {

QString WidgetPackage::manifestPath(const QString& packagePath)
{
    return QDir(packagePath).filePath(QStringLiteral("manifest.json"));
}

QString WidgetPackage::entryPath(const QString& packagePath, const WidgetManifest& manifest)
{
    return QDir(packagePath).filePath(manifest.entry);
}

bool WidgetPackage::loadManifest(const QString& packagePath, WidgetManifest* manifest, QString* errorMessage)
{
    QFile file(manifestPath(packagePath));
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot open widget manifest: %1").arg(file.errorString());
        }
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget manifest must be a JSON object.");
        }
        return false;
    }

    WidgetManifest parsed;
    if (!manifestFromJson(document.object(), &parsed, errorMessage)) {
        return false;
    }

    const QFileInfo entryInfo(entryPath(packagePath, parsed));
    if (!entryInfo.exists() || !entryInfo.isFile()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget entry file does not exist: %1").arg(entryInfo.absoluteFilePath());
        }
        return false;
    }

    if (!parsed.settings.isEmpty()) {
        const QFileInfo settingsInfo(QDir(packagePath).filePath(parsed.settings));
        if (!settingsInfo.exists() || !settingsInfo.isFile()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Widget settings file does not exist: %1").arg(settingsInfo.absoluteFilePath());
            }
            return false;
        }
    }

    if (!parsed.icon.isEmpty()) {
        const QFileInfo iconInfo(QDir(packagePath).filePath(parsed.icon));
        if (!iconInfo.exists() || !iconInfo.isFile()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Widget icon file does not exist: %1").arg(iconInfo.absoluteFilePath());
            }
            return false;
        }
        QImageReader reader(iconInfo.absoluteFilePath());
        reader.setAutoTransform(true);
        if (!reader.canRead()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Widget icon file cannot be loaded as an image: %1").arg(iconInfo.absoluteFilePath());
            }
            return false;
        }
        const QSize iconSize = reader.size();
        if (!iconSize.isValid() || iconSize.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Widget icon image has an invalid size: %1").arg(iconInfo.absoluteFilePath());
            }
            return false;
        }
    }

    *manifest = parsed;
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}



bool WidgetPackage::installPackage(const QString& packageOrLwPath,
                                   QString* installedTargetDir,
                                   QString* detectedType,
                                   QString* errorMessage)
{
    const QFileInfo fileInfo(packageOrLwPath);
    if (!fileInfo.exists()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Source package path does not exist: %1").arg(packageOrLwPath);
        }
        return false;
    }

    auto copyDirRecursively = [](const QString& srcPath, const QString& dstPath) -> bool {
        auto copyHelper = [](auto& self, const QString& src, const QString& dst) -> bool {
            QDir srcDir(src);
            if (!srcDir.exists()) return false;
            QDir().mkpath(dst);
            const QFileInfoList entries = srcDir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
            for (const QFileInfo& entry : entries) {
                const QString srcItem = entry.absoluteFilePath();
                const QString dstItem = QDir(dst).filePath(entry.fileName());
                if (entry.isDir()) {
                    if (!self(self, srcItem, dstItem)) return false;
                } else {
                    QFile::remove(dstItem);
                    if (!QFile::copy(srcItem, dstItem)) return false;
                }
            }
            return true;
        };
        return copyHelper(copyHelper, srcPath, dstPath);
    };

    QString sourceDir;
    QString tempExtractDir;
    bool isArchive = false;

    if (fileInfo.isFile()) {
        const QString ext = fileInfo.suffix().toLower();
        if (ext != QStringLiteral("lw") && ext != QStringLiteral("zip")) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Unsupported package archive format: .%1 (expected .lw or .zip)").arg(ext);
            }
            return false;
        }

        tempExtractDir = QDir(AppPaths::userLocalConfigDir()).filePath(
            QStringLiteral("temp_lw_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QDir().mkpath(tempExtractDir);

#ifdef Q_OS_WIN
        QProcess proc;
        QStringList args;
        args << "-xf" << QDir::toNativeSeparators(fileInfo.absoluteFilePath()) << "-C" << QDir::toNativeSeparators(tempExtractDir);
        proc.start("tar", args);
        if (!proc.waitForFinished(30000) || proc.exitCode() != 0) {
            QDir(tempExtractDir).removeRecursively();
            if (errorMessage) {
                *errorMessage = QStringLiteral("Failed to extract .lw package archive.");
            }
            return false;
        }
#else
        QDir(tempExtractDir).removeRecursively();
        if (errorMessage) {
            *errorMessage = QStringLiteral("Platform does not support .lw package archive extraction.");
        }
        return false;
#endif
        isArchive = true;
        sourceDir = tempExtractDir;

        if (!QFile::exists(manifestPath(sourceDir))) {
            const QFileInfoList subDirs = QDir(sourceDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            if (subDirs.size() == 1 && QFile::exists(manifestPath(subDirs.first().absoluteFilePath()))) {
                sourceDir = subDirs.first().absoluteFilePath();
            }
        }
    } else if (fileInfo.isDir()) {
        sourceDir = fileInfo.absoluteFilePath();
    } else {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Package path is neither a file nor a directory.");
        }
        return false;
    }

    WidgetManifest manifest;
    QString loadErr;
    if (!loadManifest(sourceDir, &manifest, &loadErr)) {
        if (isArchive) {
            QDir(tempExtractDir).removeRecursively();
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid package manifest: %1").arg(loadErr);
        }
        return false;
    }

    const bool isWallpaper = (manifest.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0);
    const QString targetParentDir = isWallpaper ? AppPaths::userWallpapersDir() : AppPaths::userLinWidgetsDir();
    const QString destinationDir = QDir(targetParentDir).filePath(manifest.id);

    if (QDir(destinationDir).exists()) {
        QDir(destinationDir).removeRecursively();
    }
    QDir().mkpath(destinationDir);

    bool copyOk = copyDirRecursively(sourceDir, destinationDir);
    if (isArchive) {
        QDir(tempExtractDir).removeRecursively();
    }

    if (!copyOk) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to copy package contents to destination: %1").arg(destinationDir);
        }
        return false;
    }

    if (installedTargetDir) {
        *installedTargetDir = destinationDir;
    }
    if (detectedType) {
        *detectedType = isWallpaper ? QStringLiteral("wallpaper") : QStringLiteral("widget");
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

}
