#pragma once

#include "ui/SoftDesktopTextRenderer.h"

#include <QFont>
#include <QImage>
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
    QRect staticContentRect;

    QRect outerHighlightRect;
    QRect innerHighlightRect;
    int outerHighlightRadius = 8;
    int innerHighlightRadius = 2;
    qreal selectionBorderWidth = 1.0;
    qreal hoverBorderWidth = 1.0;

    QImage iconImage;
    QImage shortcutArrowImage;
    QPixmap iconPixmap;
    QPixmap shortcutArrowPixmap;
    QPixmap staticContentPixmap;
    QFont labelFont;
    QStringList textLines;
    SoftDesktopTextRenderer::Options textOptions;

    bool selected = false;
    bool hovered = false;
    bool cutMarked = false;
    bool expandedFullName = false;
    qreal iconOpacity = 1.0;
};

DesktopRootItemRenderData localizeDesktopRootItemRenderData(
    const DesktopRootItemRenderData& item,
    const QPoint& origin);
