#include "app/AppLaunchOptions.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "services/DesktopItemLoader.h"
#include "desktop/LinDesktop.h"
#include "desktop/ListView.h"

AppLaunchOptions AppLaunchOptions::fromArguments(const QStringList& arguments)
{
    AppLaunchOptions options;

    for (int i = 1; i < arguments.size(); ++i) {
        const QString argument = arguments[i].trimmed();
        if (argument.compare("-debug", Qt::CaseInsensitive) == 0) {
            options.m_debugEnabled = true;
        }
    }

    return options;
}

void AppLaunchOptions::apply(LinDesktop* linDesktop) const
{
    if (m_debugEnabled) {
        handleDebugOption(linDesktop);
    }
}

void AppLaunchOptions::handleDebugOption(LinDesktop* linDesktop) const
{
    Q_UNUSED(linDesktop);

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString debugFilePath = appDir + "/debug.json";

    QJsonObject root;
    const QJsonDocument directoryDoc = QJsonDocument::fromJson(
        DesktopItemLoader::dumpDirectoryJsonFiles(appDir).toUtf8());
    root["currentDirectoryJsonFiles"] = directoryDoc.object();

    ListView* listView = ListView::instance();
    if (!listView) {
        qDebug() << "[Debug] ListView is not ready.";
        return;
    }

    const QJsonDocument allItemsDoc = QJsonDocument::fromJson(listView->dumpAllItemsJson().toUtf8());
    const QJsonDocument visibleItemsDoc = QJsonDocument::fromJson(listView->dumpVisibleItemsJson().toUtf8());
    const QJsonDocument hiddenItemsDoc = QJsonDocument::fromJson(listView->dumpHiddenItemsJson().toUtf8());

    root["m_allItems"] = allItemsDoc.array();
    root["m_items"] = visibleItemsDoc.array();
    root["m_hiddenIcons"] = hiddenItemsDoc.array();

    QFile debugFile(debugFilePath);
    if (!debugFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qDebug() << "[Debug] Failed to write debug.json:" << debugFile.errorString();
        return;
    }

    debugFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    debugFile.close();
    qDebug() << "[Debug] Wrote debug info to" << debugFilePath;
}
