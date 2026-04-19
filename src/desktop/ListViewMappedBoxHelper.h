#pragma once

#include <QString>
#include <QStringList>

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QPointer>
#include <QObject>

#include "desktop/ListView.h"
#include "desktop/ListItem.h"
#include "box/LinBox.h"
#include "box/BoxManager.h"
#include "services/DesktopItemLoader.h"
#include "monitor/LinDirectoryMonitor.h"

class LinBox;
class ListView;

class ListViewMappedBoxHelper
{
public:
    static inline QStringList mappedFolderEntryPaths(const QString& folderPath)
    {
        const QString normalizedFolderPath = QDir::toNativeSeparators(folderPath);
        if (normalizedFolderPath.isEmpty()) {
            return {};
        }

        QDir dir(normalizedFolderPath);
        if (!dir.exists()) {
            return {};
        }

        const QFileInfoList entries = dir.entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
            QDir::DirsFirst | QDir::IgnoreCase | QDir::Name);

        QStringList paths;
        const QSet<QString> blockedFolderNames = {
            QStringLiteral("$RECYCLE.BIN"),
            QStringLiteral("System Volume Information"),
            QStringLiteral("desktop.ini")
        };
        for (const QFileInfo& entry : entries) {
            if (blockedFolderNames.contains(entry.fileName())) {
                continue;
            }
            paths.append(QDir::toNativeSeparators(entry.absoluteFilePath()));
        }
        return paths;
    }

    static inline void clearMappedFolderItemsForBox(ListView& view, LinBox* box)
    {
        if (!box) {
            return;
        }

        const QStringList previousPaths = view.m_mappedBoxFilePaths.value(box->boxId());
        if (previousPaths.isEmpty()) {
            return;
        }

        const QVector<ListItem*> currentItems = box->items();
        for (ListItem* item : currentItems) {
            if (!item) {
                continue;
            }

            const QString normalizedPath = QDir::toNativeSeparators(item->filePath());
            if (!previousPaths.contains(normalizedPath)) {
                continue;
            }

            item->disconnect(&view);
            item->disconnect(box);
            item->disconnect();
            box->removeListItem(item, false);
            view.m_hiddenIcons.removeOne(item);
            view.m_items.removeOne(item);
            view.m_allItems.removeOne(item);
            view.m_selectedItems.removeOne(item);
            item->deleteLater();
        }

        view.m_mappedBoxFilePaths.remove(box->boxId());
    }

    static inline void loadMappedFolderItemsForBox(ListView& view, LinBox* box)
    {
        if (!box || !box->isFolderMapped()) {
            return;
        }

        box->setMappedFolderPath(box->mappedFolderPath());
        clearMappedFolderItemsForBox(view, box);

        const QFileInfo mappedInfo(box->mappedFolderPath());
        if (!mappedInfo.exists() || !mappedInfo.isDir() || !mappedInfo.isReadable()) {
            view.m_mappedBoxFilePaths.remove(box->boxId());
            box->refreshLayout();
            box->raise();
            return;
        }

        const QStringList paths = mappedFolderEntryPaths(box->mappedFolderPath());
        view.m_mappedBoxFilePaths[box->boxId()] = paths;
        DesktopItemLoader itemLoader;

        for (const QString& path : paths) {
            ListItem* item = view.selectItemByPath(path);
            if (!item) {
                item = new ListItem(path, &view);
                view.setupItemConnections(item, box);
                view.m_allItems.push_back(item);
                view.m_hiddenIcons.push_back(item);
            } else {
                item->setItemData(itemLoader.loadItem(path));
                item->initData();
                view.setupItemConnections(item, box);
                view.m_items.removeOne(item);
                if (!view.m_hiddenIcons.contains(item)) {
                    view.m_hiddenIcons.push_back(item);
                }
            }

            item->setBoxId(box->boxId());
            item->hide();
            box->addListItem(item, false);
        }

        if (LinDirectoryMonitor* oldMonitor = box->mappedFolderMonitor()) {
            oldMonitor->stopListening();
        }

        auto* monitor = new LinDirectoryMonitor(box->mappedFolderPath(), false, box);
        QObject::connect(monitor, &LinDirectoryMonitor::fileSystemChanged, &view, [&view, box](const FileSystemEventInfo&) {
            if (!box) {
                return;
            }
            ListViewMappedBoxHelper::loadMappedFolderItemsForBox(view, box);
            box->refreshLayout();
            box->raise();
        }, Qt::UniqueConnection);
        monitor->startListening();
        box->setMappedFolderMonitor(monitor);

        box->refreshLayout();
        box->raise();
    }

    static inline LinBox* findMappedFolderBox(const ListView& view, const QString& folderPath, const LinBox* excludeBox = nullptr)
    {
        Q_UNUSED(view);
        auto normalizeMappedFolderPathForLookupHelper = [](const QString& path) {
            QString normalized = QDir::toNativeSeparators(QDir(path).absolutePath());
            if (normalized.length() == 2 && normalized[1] == ':') {
                normalized += '\\';
            } else if (normalized.length() > 3) {
                while (normalized.endsWith('\\') || normalized.endsWith('/')) {
                    normalized.chop(1);
                }
            }
            return normalized;
        };

        const QString normalizedTarget = normalizeMappedFolderPathForLookupHelper(folderPath);
        if (normalizedTarget.isEmpty()) {
            return nullptr;
        }

        for (const QPointer<LinBox>& boxPtr : BoxManager::instance()->allBoxes()) {
            LinBox* box = boxPtr.data();
            if (!box || box == excludeBox || !box->isFolderMapped()) {
                continue;
            }

            const QString mappedPath = normalizeMappedFolderPathForLookupHelper(box->mappedFolderPath());
            if (QString::compare(mappedPath, normalizedTarget, Qt::CaseInsensitive) == 0) {
                return box;
            }
        }

        return nullptr;
    }
};
