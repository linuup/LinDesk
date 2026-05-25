#include "desktop/widgets/linwidget/WidgetPackage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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

    *manifest = parsed;
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

}
