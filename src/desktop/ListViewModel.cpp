#include "desktop/ListViewModel.h"
#include "desktop/ListViewFileOpsHelper.h"
#include "SystemApi/SystemApi.h"
#include <QDir>

ListViewModel::ListViewModel(QObject* parent)
    : QObject(parent)
{
}


void ListViewModel::clearSelection()
{
    if (!m_selectedDesktopPathSet.isEmpty()) {
        m_selectedDesktopPathSet.clear();
        emit selectionChanged();
    }
}

void ListViewModel::selectPath(const QString& normalizedPath, bool selected)
{
    bool changed = false;
    if (selected) {
        if (!m_selectedDesktopPathSet.contains(normalizedPath)) {
            m_selectedDesktopPathSet.insert(normalizedPath);
            changed = true;
        }
    } else {
        if (m_selectedDesktopPathSet.contains(normalizedPath)) {
            m_selectedDesktopPathSet.remove(normalizedPath);
            changed = true;
        }
    }

    if (changed) {
        emit selectionChanged();
    }
}

void ListViewModel::setItems(const QVector<DesktopRootItemState>& states)
{
    m_itemStates = states;
    emit dataChanged();
}

bool ListViewModel::createShortcutsInFolder(const QStringList& sourcePaths, const QString& targetFolder)
{
    if (sourcePaths.isEmpty() || targetFolder.isEmpty()) {
        return false;
    }

    bool ok = true;
    for (const QString& sourcePath : sourcePaths) {
        if (sourcePath.isEmpty()) {
            continue;
        }
        const QString shortcutPath = ListViewFileOpsHelper::uniqueShortcutPathForTarget(sourcePath, targetFolder);
        ok = ListViewFileOpsHelper::createShellShortcut(sourcePath, shortcutPath) && ok;
    }

    return ok;
}

bool ListViewModel::handleDropFiles(const QStringList& draggedPaths, const QString& desktopDir, bool preferShortcut, bool isCopyAction)
{
    if (draggedPaths.isEmpty() || desktopDir.isEmpty()) {
        return false;
    }

    if (preferShortcut) {
        return createShortcutsInFolder(draggedPaths, desktopDir);
    } else if (isCopyAction) {
        return ListViewFileOpsHelper::copyFilesToDirectoryWithRule(draggedPaths, desktopDir);
    } else {
        return ListViewFileOpsHelper::moveFilesToDirectoryWithRule(draggedPaths, desktopDir);
    }
}

bool ListViewModel::deleteSelectedItems(const QStringList& desktopPaths, const QStringList& boxPaths, bool permanently, QStringList& outDeletedDesktopPaths, QStringList& outDeletedBoxPaths)
{
    QStringList pathsToDelete;
    QSet<QString> seenDeletePaths;

    for (const QString& path : desktopPaths) {
        const QString normalizedPath = QDir::toNativeSeparators(path);
        if (normalizedPath.isEmpty() || seenDeletePaths.contains(normalizedPath)) {
            continue;
        }
        seenDeletePaths.insert(normalizedPath);
        outDeletedDesktopPaths.append(path);
        pathsToDelete.append(normalizedPath);
    }

    for (const QString& path : boxPaths) {
        const QString normalizedPath = QDir::toNativeSeparators(path);
        if (normalizedPath.isEmpty() || seenDeletePaths.contains(normalizedPath)) {
            continue;
        }
        seenDeletePaths.insert(normalizedPath);
        outDeletedBoxPaths.append(path);
        pathsToDelete.append(normalizedPath);
    }

    if (pathsToDelete.isEmpty()) {
        return false;
    }

    return CSystemApi::DeleteFiles(pathsToDelete, !permanently, true);
}
