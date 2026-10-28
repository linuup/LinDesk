#pragma once

#include <QList>
#include <QVector>
#include <QString>
#include <vector>

#include "desktop/DesktopRootItemState.h"
#include "models/data/ListItemData.h"

struct DisplayNameOverride {
    QString normalizedPath;
    QString zhName;
    QString enName;
};

class DesktopItemLoader {
public:
    std::vector<ListItemData> loadAllItems() const;
    ListItemData loadItem(const QString& filePath) const;
    static bool shouldTreatAsHiddenShellFolder(const ListItemData& item);
    static QList<DisplayNameOverride> displayNameOverrides();
    static QString applyDisplayNameOverride(const QString& filePath, const QString& originalName);
    static QString dumpJsonFileContent(const QString& filePath);
    static QString dumpDirectoryJsonFiles(const QString& directoryPath);
    static QString dumpListItemDataJson(const std::vector<ListItemData>& items);
    static QString dumpDesktopRootStatesJson(const QVector<DesktopRootItemState>& states);

private:
    static ListItemData toListItemData(const IconItem& item);
};
