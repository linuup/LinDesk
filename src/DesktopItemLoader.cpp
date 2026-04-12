#include "DesktopItemLoader.h"

#include <SystemApi.h>

ListItemData DesktopItemLoader::toListItemData(const IconItem& item)
{
    return ListItemData(item);
}

std::vector<ListItemData> DesktopItemLoader::loadAllItems() const
{
    std::vector<ListItemData> allItemsData;
    CSystemApi systemApi;
    const std::vector<IconItem> items = systemApi.GetDesktopPIDLItems();
    allItemsData.reserve(items.size());

    for (const IconItem& item : items) {
        allItemsData.push_back(toListItemData(item));
    }

    return allItemsData;
}

ListItemData DesktopItemLoader::loadItem(const QString& filePath) const
{
    const IconItem item = CSystemApi::CreateIconItem(filePath.toStdWString().c_str());
    return toListItemData(item);
}
