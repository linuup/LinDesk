#pragma once

#include "desktop/DesktopRootItemModel.h"
#include "monitor/DesktopInfoMonitor.h"
#include <QSet>
#include <functional>

class ListItem;

/// 渐进迁移桥梁：同时持有遗留 ListItem 指针和新数据模型。
/// 迁移完成后 legacyItem 可移除。
struct DesktopRootItemState
{
    ListItem* legacyItem = nullptr;
    DesktopRootItemModel model;
};

// ---- Build 函数：从 ListItem 提取快照 ----

/// 从 ListItem 构建完整逻辑状态。
DesktopRootItemState buildDesktopRootItemState(const ListItem& item);

/// 从 ListItem 直接构建渲染快照。
/// @param useParentCoordinates  true = 坐标相对于父控件，false = 本地坐标
DesktopRootItemRenderData buildDesktopRootItemRenderData(
    const ListItem& item,
    bool forceSelected = false,
    bool useParentCoordinates = true);

/// 从已有 State 构建渲染快照（可覆盖选中状态）。
DesktopRootItemRenderData buildDesktopRootItemRenderData(
    const DesktopRootItemState& state,
    bool forceSelected = false);

/// 计算 State 的高亮脏区域。
QRect desktopRootItemHighlightDirtyRect(const DesktopRootItemState& state);

// ---- 从 ListItemData 直接初始化模型（不依赖 ListItem QWidget）----

class ListItemData;

/// 初始化上下文：模型初始化时需要的外部环境数据。
struct DesktopItemInitContext {
    QList<DesktopInfoMonitor::DesktopIcon> sysIconRegisterInfo;
    QSet<QString> systemFolderCLSIDs;
    bool showHiddenFiles = false;
    std::function<QString(const QString&)> getBoxId; // Box 归属查询函数
};

/// 从 ListItemData 初始化 DesktopRootItemModel 的身份、文件属性、图标等核心字段。
/// @param context 外部环境上下文（如系统图标可见性、Box 归属等）。
void initDesktopRootItemModel(
    DesktopRootItemModel& model,
    const ListItemData& data,
    const DesktopItemInitContext& context);

// ---- 从模型直接构建渲染数据（不依赖 ListItem QWidget）----

/// 渲染参数：由 ListView 提供的视图级上下文。
struct DesktopRenderParams {
    int iconWidth   = 76;   // 当前 ViewMode 的项目宽度
    int iconHeight  = 78;   // 当前 ViewMode 的项目高度
    int largeIconW  = 100;  // ListView::kLargeIconW
    int smallIconW  = 62;   // ListView::kSmallIconW
    int iconTextGap = 8;
    qreal dpiScale  = 1.0;  // DPI 缩放系数
    QFont labelFont;        // 文件名标签字体
    bool showShortcutArrow = true;  // 是否显示快捷方式箭头
};

/// 从 DesktopRootItemModel + RenderParams 构建完整的 RenderData。
/// 包含：布局计算 + 图标像素处理 + 快捷方式箭头 + 高亮区域。
/// 完全不依赖 QWidget。
DesktopRootItemRenderData buildRenderDataFromModel(
    const DesktopRootItemModel& model,
    const DesktopRenderParams& params);

bool openDesktopRootItem(const DesktopRootItemModel& model);

/// 无状态绘制函数：把一个桌面图标绘制到 painter 上。
void paintDesktopRootItem(QPainter& painter, const DesktopRootItemRenderData& item);
