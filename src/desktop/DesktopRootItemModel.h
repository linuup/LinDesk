#pragma once

#include "desktop/DesktopRootItemRenderData.h"
#include "models/data/ListItemData.h"

#include <CommonEnum.h>
#include <QDateTime>
#include <QFileInfo>
#include <QIcon>
#include <QPoint>
#include <QSize>

/// Layout mode for model driven desktop items.
enum class DesktopLayoutMode {
    IconMode,
    ListMode
};

/// Full data model for a desktop item, including render and logic state.
struct DesktopRootItemModel
{
    QString filePath;
    QString normalizedPath;
    ObjectType objectType = ObjectType::Empty_ObjectType;
    SpecialFolders specialFolder = SpecialFolders::Empty;
    QString clsid;
    ListItemData itemData;

    QFileInfo fileInfo;
    QString displayName;
    QString sortName;
    qint64 fileSize = 0;
    QDateTime lastModified;
    QString fileTypeName;
    QString expandedPreviewText;

    QIcon sourceIcon;
    QString iconCacheKey;
    bool isShortcut = false;

    DesktopLayoutMode layoutMode = DesktopLayoutMode::IconMode;
    QPoint position;
    QSize baseSize;
    QSize effectiveSize;
    int visibleTextHeight = 0;

    bool isSelected = false;
    bool isHovered = false;
    bool focused = false;
    bool isCutMarked = false;
    bool isFullNameExpanded = false;
    bool isHiddenFileVisual = false;

    QString boxId;
    int customSortOrder = -1;

    DesktopRootItemRenderData renderData;

    bool visible = false;
    bool isRenaming = false;
    bool isReady = false;
};
