#include "desktop/widgets/linwidget/WidgetPackage.h"

#include <QDir>
#include <QFile>
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

    return manifestFromJson(document.object(), manifest, errorMessage);
}

}
