#include "services/DebugInfoExporter.h"

#include "desktop/ListView.h"
#include "services/DesktopItemLoader.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

bool DebugInfoExporter::exportToApplicationDir(ListView* listView, QString* outputPath, QString* errorMessage)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString debugFilePath = appDir + "/debug.json";
    if (outputPath) {
        *outputPath = debugFilePath;
    }

    if (!listView) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("ListView is not ready.");
        }
        return false;
    }

    QJsonObject root;
    const QJsonDocument directoryDoc = QJsonDocument::fromJson(
        DesktopItemLoader::dumpDirectoryJsonFiles(appDir).toUtf8());
    root["currentDirectoryJsonFiles"] = directoryDoc.object();

    const QJsonDocument allItemsDoc = QJsonDocument::fromJson(listView->dumpAllItemsJson().toUtf8());
    const QJsonDocument visibleItemsDoc = QJsonDocument::fromJson(listView->dumpVisibleItemsJson().toUtf8());
    const QJsonDocument hiddenItemsDoc = QJsonDocument::fromJson(listView->dumpHiddenItemsJson().toUtf8());

    root["m_allItems"] = allItemsDoc.array();
    root["m_items"] = visibleItemsDoc.array();
    root["m_hiddenIcons"] = hiddenItemsDoc.array();

    QFile debugFile(debugFilePath);
    if (!debugFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = debugFile.errorString();
        }
        return false;
    }

    debugFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    debugFile.close();
    return true;
}
