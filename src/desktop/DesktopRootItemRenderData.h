#pragma once

#include <QFont>
#include <QPixmap>
#include <QRect>
#include <QString>
#include <QStringList>
#include "ui/SoftDesktopTextRenderer.h"

class QPainter;

/// 桌面项的渲染快照 —— 纯值类型，包含绘制一个桌面图标所需的全部信息。
/// 一旦构建完成就与 ListItem / QWidget 完全无关。
struct DesktopRootItemRenderData
{
    // --- 身份 ---
    QString filePath;
    QString displayName;

    // --- 几何（在宿主坐标系中） ---
    QRect itemRect;
    QRect iconRect;
    QRect textRect;
    QRect renameEditorRect;
    QRect shortcutArrowRect;
    QRect selectionContentRect;

    // --- 高亮几何（由 selectionContentRect 派生） ---
    QRect outerHighlightRect;
    QRect innerHighlightRect;
    int   outerHighlightRadius = 8;
    int   innerHighlightRadius = 2;
    qreal selectionBorderWidth = 1.0;
    qreal hoverBorderWidth     = 1.0;

    // --- 视觉数据 ---
    QPixmap iconPixmap;
    QPixmap shortcutArrowPixmap;
    QFont   labelFont;
    QStringList textLines;
    SoftDesktopTextRenderer::Options textOptions;

    // --- 状态标志 ---
    bool selected         = false;
    bool hovered          = false;
    bool cutMarked        = false;
    bool expandedFullName = false;
};

// ---- 纯函数：只依赖 RenderData，不依赖任何 QWidget ----

/// 将所有坐标平移，使 origin 成为新的原点。返回新副本。
DesktopRootItemRenderData localizeDesktopRootItemRenderData(
    const DesktopRootItemRenderData& item,
    const QPoint& origin);

/// 计算需要重绘的脏区域（用于 update(QRect) 局部刷新）。
QRect desktopRootItemHighlightDirtyRect(const DesktopRootItemRenderData& item);

/// 无状态绘制函数：把一个桌面图标绘制到 painter 上。
void paintDesktopRootItem(QPainter& painter, const DesktopRootItemRenderData& item);
