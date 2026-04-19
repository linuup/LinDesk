#pragma once

#include <QByteArray>
#include <QList>
#include <QMimeData>
#include <QString>
#include <QStringList>
#include <QUrl>

class ListViewFileOpsHelper
{
public:
    static QStringList extractLocalPathsFromWindowsMimePayload(const QByteArray& payload, bool utf16);
    static QStringList draggedLocalFilePaths(const QMimeData* mimeData);
    static QStringList clipboardLocalFilePaths();
    static bool clipboardContainsCutItems();
    static void writeUrlsToClipboard(const QList<QUrl>& urls, bool cutOperation);
    static QString uniquePasteDestinationPath(const QString& sourcePath, const QString& destDir);
    static QString uniqueShortcutPathForTarget(const QString& sourcePath, const QString& targetDir);
    static bool copyPathToExactDestination(const QString& sourcePath, const QString& destinationPath);
    static bool movePathToExactDestination(const QString& sourcePath, const QString& destinationPath);
    static bool copyFilesToDirectoryWithRule(const QStringList& sourcePaths, const QString& targetDir);
    static bool createShellShortcut(const QString& sourcePath, const QString& shortcutPath);
    static bool shouldCreateShortcutForExternalPath(const QString& sourcePath);
};
