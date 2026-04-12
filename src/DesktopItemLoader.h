#pragma once

#include <QString>
#include <vector>

#include "Data/ListItemData.h"

class DesktopItemLoader {
public:
    std::vector<ListItemData> loadAllItems() const;
    ListItemData loadItem(const QString& filePath) const;

private:
    static ListItemData toListItemData(const IconItem& item);
};
