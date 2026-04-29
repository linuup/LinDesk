#pragma once

#include <QVector>
#include <QString>
#include <vector>

#include "desktop/DesktopRootItemState.h"
#include "models/data/ListItemData.h"

class ListItem;

class DesktopItemLoader {
public:
    std::vector<ListItemData> loadAllItems() const;
    ListItemData loadItem(const QString& filePath) const;
    static bool shouldTreatAsHiddenShellFolder(const ListItemData& item);
    static QString dumpJsonFileContent(const QString& filePath);
    static QString dumpDirectoryJsonFiles(const QString& directoryPath);
    static QString dumpListItemDataJson(const std::vector<ListItemData>& items);
    static QString dumpListItemsJson(const QVector<ListItem*>& items);
    static QString dumpDesktopRootStatesJson(const QVector<DesktopRootItemState>& states);

private:
    static ListItemData toListItemData(const IconItem& item);
};
