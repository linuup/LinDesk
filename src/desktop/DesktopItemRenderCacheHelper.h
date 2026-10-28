#pragma once

#include "desktop/DesktopRootItemRenderData.h"
#include "ui/SoftDesktopTextRenderer.h"

#include <QPainter>

namespace desktop_render_cache_helper {

inline void paintDesktopRootItemStaticElements(QPainter& painter, const DesktopRootItemRenderData& data)
{
    if (data.cutMarked) {
        painter.setOpacity(0.55);
    }
    QPixmap iconPix = !data.iconPixmap.isNull() ? data.iconPixmap : (!data.iconImage.isNull() ? QPixmap::fromImage(data.iconImage) : QPixmap());
    if (!iconPix.isNull()) {
        QPoint iconPos = data.iconRect.topLeft();
        if (iconPix.width() < data.iconRect.width()) {
            iconPos.setX(data.iconRect.x() + (data.iconRect.width() - iconPix.width()) / 2);
        }
        if (iconPix.height() < data.iconRect.height()) {
            iconPos.setY(data.iconRect.y() + (data.iconRect.height() - iconPix.height()) / 2);
        }
        painter.drawPixmap(iconPos, iconPix);
    }
    if (data.cutMarked) {
        painter.setOpacity(1.0);
    }
    QPixmap arrowPix = !data.shortcutArrowPixmap.isNull() ? data.shortcutArrowPixmap : (!data.shortcutArrowImage.isNull() ? QPixmap::fromImage(data.shortcutArrowImage) : QPixmap());
    if (!arrowPix.isNull()) {
        painter.drawPixmap(data.shortcutArrowRect.topLeft(), arrowPix);
    }
    if (!data.textLines.isEmpty()) {
        SoftDesktopTextRenderer::drawText(&painter, data.textRect, data.textLines, data.labelFont, data.textOptions);
    }
}

inline void paintDesktopRootItemDynamicElements(QPainter& painter, const DesktopRootItemRenderData& data)
{
    if (data.selected) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(100, 150, 255, 30));
        painter.drawRoundedRect(data.outerHighlightRect, data.outerHighlightRadius, data.outerHighlightRadius);
        painter.setBrush(QColor(255, 255, 255, 90));
        painter.drawRoundedRect(data.innerHighlightRect, data.innerHighlightRadius, data.innerHighlightRadius);
        painter.setPen(QPen(QColor(255, 255, 255, 150), data.selectionBorderWidth));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(data.innerHighlightRect, data.innerHighlightRadius, data.innerHighlightRadius);
        return;
    }

    if (data.hovered) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255, 25));
        painter.drawRoundedRect(data.innerHighlightRect, data.innerHighlightRadius, data.innerHighlightRadius);
        QPen hoverPen(QColor(255, 255, 255, 100));
        hoverPen.setWidthF(0.8);
        hoverPen.setStyle(Qt::DotLine);
        hoverPen.setDashPattern({1.0, 1.0});
        hoverPen.setCapStyle(Qt::FlatCap);
        painter.setPen(hoverPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(data.innerHighlightRect, data.innerHighlightRadius, data.innerHighlightRadius);
    }
}

} // namespace desktop_render_cache_helper

inline QRect desktopRootItemHighlightDirtyRect(const DesktopRootItemRenderData& item)
{
    QRect dirty = item.itemRect.united(item.outerHighlightRect).united(item.selectionContentRect);
    if (!item.shortcutArrowRect.isNull()) {
        dirty = dirty.united(item.shortcutArrowRect);
    }
    return dirty.adjusted(-1, -1, 1, 1);
}

inline void prepareDesktopRootItemStaticContentCache(DesktopRootItemRenderData& item)
{
    if (item.staticContentRect.isEmpty()) {
        item.staticContentRect = item.itemRect;
    }
    if (item.staticContentRect.isEmpty()) {
        item.staticContentPixmap = QPixmap();
        return;
    }

    item.staticContentPixmap = QPixmap(item.staticContentRect.size());
    item.staticContentPixmap.fill(Qt::transparent);
    QPainter contentPainter(&item.staticContentPixmap);
    contentPainter.setRenderHint(QPainter::Antialiasing);
    contentPainter.setRenderHint(QPainter::SmoothPixmapTransform);
    contentPainter.setRenderHint(QPainter::TextAntialiasing);
    contentPainter.translate(-item.staticContentRect.topLeft());
    DesktopRootItemRenderData staticData = item;
    staticData.cutMarked = false;
    desktop_render_cache_helper::paintDesktopRootItemStaticElements(contentPainter, staticData);
}

inline void paintDesktopRootItemStatic(QPainter& painter, const DesktopRootItemRenderData& item)
{
    if (item.itemRect.isNull()) return;
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, painter.renderHints().testFlag(QPainter::SmoothPixmapTransform));
    painter.setRenderHint(QPainter::TextAntialiasing, painter.renderHints().testFlag(QPainter::TextAntialiasing));
    if (item.cutMarked || item.staticContentPixmap.isNull()) {
        desktop_render_cache_helper::paintDesktopRootItemStaticElements(painter, item);
    } else {
        painter.drawPixmap(item.staticContentRect.topLeft(), item.staticContentPixmap);
    }
    painter.restore();
}

inline void paintDesktopRootItemDynamic(QPainter& painter, const DesktopRootItemRenderData& item)
{
    if (item.itemRect.isNull() || (!item.selected && !item.hovered)) return;
    const bool antialiasingEnabled = painter.renderHints().testFlag(QPainter::Antialiasing);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, antialiasingEnabled);
    desktop_render_cache_helper::paintDesktopRootItemDynamicElements(painter, item);
    painter.restore();
}

inline void paintDesktopRootItem(QPainter& painter, const DesktopRootItemRenderData& item)
{
    if (item.itemRect.isNull()) return;
    const bool antialiasingEnabled = painter.renderHints().testFlag(QPainter::Antialiasing);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, antialiasingEnabled);
    desktop_render_cache_helper::paintDesktopRootItemDynamicElements(painter, item);
    desktop_render_cache_helper::paintDesktopRootItemStaticElements(painter, item);
    painter.restore();
}

inline void paintDesktopRootItemGhost(QPainter& painter, const DesktopRootItemRenderData& item)
{
    if (item.itemRect.isNull()) return;

    DesktopRootItemRenderData ghostItem = item;
    ghostItem.selected = false;
    ghostItem.hovered = false;
    ghostItem.cutMarked = false;
    ghostItem.shortcutArrowRect = QRect();
    ghostItem.shortcutArrowPixmap = QPixmap();
    ghostItem.staticContentPixmap = QPixmap();

    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, painter.renderHints().testFlag(QPainter::SmoothPixmapTransform));
    painter.setRenderHint(QPainter::TextAntialiasing, painter.renderHints().testFlag(QPainter::TextAntialiasing));
    desktop_render_cache_helper::paintDesktopRootItemStaticElements(painter, ghostItem);
    painter.restore();
}
