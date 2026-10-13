#pragma once

#include <QByteArray>
#include <QList>
#include <QMimeData>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <Windows.h>
#include <shlobj.h>

class WindowsDragMimeData : public QMimeData
{
public:
    static WindowsDragMimeData* fromMimeData(const QMimeData* mimeData);
    void setDropDescription(DROPIMAGETYPE type, const QString& message, const QString& insert);
    void setDropDescriptionForAction(Qt::DropAction action, const QString& insertText);
    void setInvalidDropDescription(const QString& insertText);
    void refreshDragWindow() const;

private:
    static QString windowsMimeType(const char* value);
};

class ListViewFileOpsHelper
{
public:
    static QStringList extractLocalPathsFromWindowsMimePayload(const QByteArray& payload, bool utf16);
    static QStringList draggedLocalFilePaths(const QMimeData* mimeData);
    static QStringList clipboardLocalFilePaths();
    static bool clipboardContainsCutItems();
    static bool clipboardHasVirtualFileItems();
    static QString clipboardFormatListDump();
    static QString clipboardDebugDump(bool includeVirtualContents = true);
    static int pasteVirtualClipboardFilesToDirectory(const QString& targetDir, QStringList* savedPaths = nullptr);
    static void writeUrlsToClipboard(const QList<QUrl>& urls, bool cutOperation);
    static QString uniquePasteDestinationPath(const QString& sourcePath, const QString& destDir);
    static QString uniqueNewFolderPath(const QString& targetDir, const QString& baseName = QString());
    static QString normalizeNewFolderCasePath(const QString& path);
    static QString uniqueShortcutPathForTarget(const QString& sourcePath, const QString& targetDir);
    static bool createDirectoryPath(const QString& targetPath);
    static bool copyPathToExactDestination(const QString& sourcePath, const QString& destinationPath);
    static bool movePathToExactDestination(const QString& sourcePath, const QString& destinationPath);
    static bool copyFilesToDirectoryWithRule(const QStringList& sourcePaths, const QString& targetDir);
    static bool moveFilesToDirectoryWithRule(const QStringList& sourcePaths, const QString& targetDir);
    static bool createShellShortcut(const QString& sourcePath, const QString& shortcutPath);
    static bool shouldCreateShortcutForExternalPath(const QString& sourcePath);
    static void updateInternalDragDescriptionCustom(const QMimeData* mimeData, DROPIMAGETYPE type, const QString& message, const QString& insertText);
    static void updateInternalDragDescription(const QMimeData* mimeData, Qt::DropAction action, const QString& insertText, bool forbidden = false);
};
