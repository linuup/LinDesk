#pragma once

#include "desktop/DesktopRootItemRenderData.h"
#include "models/data/ListItemData.h"
#include <CommonEnum.h>
#include <QFileInfo>
#include <QIcon>
#include <QPoint>
#include <QSize>
#include <QDateTime>

/// 桌面项的布局模式（镜像 ListItem::LayoutMode，避免头文件依赖）
enum class DesktopLayoutMode {
    IconMode,
    ListMode
};

/// 桌面项的完整数据模型 —— 持有渲染和逻辑所需的全部信息。
/// 将来可直接从文件路径初始化，不依赖 ListItem QWidget。
struct DesktopRootItemModel
{
    // =========================================
    //  身份 / 类型
    // =========================================
    QString         filePath;
    QString         normalizedPath;
    ObjectType      objectType    = ObjectType::Empty_ObjectType;
    SpecialFolders  specialFolder = SpecialFolders::Empty;
    QString         clsid;
    ListItemData    itemData;

    // =========================================
    //  文件属性（用于显示名、排序、文件操作）
    // =========================================
    QFileInfo       fileInfo;
    QString         displayName;          // 处理过的显示名（去 .lnk 后缀等）
    QString         sortName;             // displayName.toLower()，用于排序
    qint64          fileSize       = 0;   // 文件大小（排序用）
    QDateTime       lastModified;         // 最后修改时间（排序用）
    QString         fileTypeName;         // 文件类型描述（排序用）
    QString         expandedPreviewText;

    // =========================================
    //  图标数据
    // =========================================
    QIcon           sourceIcon;           // 原始系统图标（用于缓存失效后重建）
    QString         iconCacheKey;         // 图标缓存键
    bool            isShortcut     = false;  // .lnk / .url / .appref-ms

    // =========================================
    //  布局
    // =========================================
    DesktopLayoutMode layoutMode = DesktopLayoutMode::IconMode;
    QPoint          position;             // 在宿主中的逻辑位置
    QSize           baseSize;             // 基础尺寸（ViewMode 决定）
    QSize           effectiveSize;        // 实际尺寸（文本展开后可能更高）
    int             visibleTextHeight = 0;

    // =========================================
    //  交互状态（运行时，不持久化）
    // =========================================
    bool            isSelected          = false;
    bool            isHovered           = false;
    bool            focused             = false;
    bool            isCutMarked         = false;
    bool            isFullNameExpanded  = false;
    bool            isHiddenFileVisual  = false;  // 半透明显示（隐藏文件）

    // =========================================
    //  归属
    // =========================================
    QString         boxId;
    int             customSortOrder = -1;

    // =========================================
    //  渲染快照
    // =========================================
    DesktopRootItemRenderData renderData;

    // =========================================
    //  逻辑状态
    // =========================================
    bool            visible    = false;
    bool            isRenaming = false;
    bool            isReady    = false;
};
