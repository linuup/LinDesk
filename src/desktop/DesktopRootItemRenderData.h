#pragma once

#include "ui/SoftDesktopTextRenderer.h"

#include <QFont>
#include <QPixmap>
#include <QRect>
#include <QString>
#include <QStringList>

class QPainter;

/// Render snapshot used by the single canvas paint pipeline.
struct DesktopRootItemRenderData
{
    QString filePath;
    QString displayName;

    QRect itemRect;
    QRect iconRect;
    QRect textRect;
    QRect renameEditorRect;
    QRect shortcutArrowRect;
    QRect selectionContentRect;

    QRect outerHighlightRect;
    QRect innerHighlightRect;
    int outerHighlightRadius = 8;
    int innerHighlightRadius = 2;
    qreal selectionBorderWidth = 1.0;
    qreal hoverBorderWidth = 1.0;

    QPixmap iconPixmap;
    QPixmap shortcutArrowPixmap;
    QFont labelFont;
    QStringList textLines;
    SoftDesktopTextRenderer::Options textOptions;

    bool selected = false;
    bool hovered = false;
    bool cutMarked = false;
    bool expandedFullName = false;
};

DesktopRootItemRenderData localizeDesktopRootItemRenderData(
    const DesktopRootItemRenderData& item,
    const QPoint& origin);

QRect desktopRootItemHighlightDirtyRect(const DesktopRootItemRenderData& item);

void paintDesktopRootItem(QPainter& painter, const DesktopRootItemRenderData& item);
