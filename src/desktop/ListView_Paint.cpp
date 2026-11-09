#include "desktop/ListView_Internal.h"

// ============================================================================
// ListView Paint & Rendering Implementation
// ============================================================================

void ListView::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    const QRegion dirtyRegion = event->region();
    const bool reducePaintCostForSelection = m_isSelecting;
    painter.setRenderHint(QPainter::Antialiasing, !reducePaintCostForSelection);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, !reducePaintCostForSelection);
    painter.setRenderHint(QPainter::TextAntialiasing);


    auto& bridge = LinWidgetRuntimeBridge::instance();
    const bool runtimeLoaded = bridge.isLoaded();
    if (runtimeLoaded && bridge.hasActiveWallpaper() && !bridge.isWallpaperInteractive()) {
        painter.fillRect(event->rect(), QColor(0, 0, 0, 1));
    }

    const bool widgetsEmbedded = runtimeLoaded ? bridge.isWidgetLayerEmbedded() : true;

    // 1. 如果处于嵌入模式（小组件在图标下方），优先绘制 CEF OSR 画布
    if (widgetsEmbedded && runtimeLoaded) {
        bridge.renderCanvas(&painter, dirtyRegion);
    }

    if (m_hideDesktopItemsForRefresh) {
        return;
    }

    const bool isFading = (m_desktopHideAnim && m_desktopHideAnim->state() == QAbstractAnimation::Running);
    const qreal currentOpacity = isFading ? m_desktopElementsOpacity : (m_iconShowRule.showDesktopIcons ? 1.0 : 0.0);
    if (currentOpacity <= 0.0 && !isFading) {
        return;
    }
    if (!isFading) {
        painter.setOpacity(currentOpacity);
    }

    auto paintDesktopState = [&](const DesktopRootItemState& state) {
        if (!shouldPaintDesktopModelState(state)) {
            return;
        }

        if (!dirtyRegion.intersects(state.model.renderData.itemRect) &&
            !dirtyRegion.intersects(state.model.renderData.textRect)) {
            return;
        }

        if (isFading) {
            const QRect totalRect = state.model.renderData.itemRect.united(state.model.renderData.textRect);
            const ItemHideTransform xform = computeItemHideTransform(totalRect.center());
            if (xform.opacity <= 0.001) {
                return;
            }
            const int dy = qRound(xform.dy);
            if (dy != 0) {
                painter.save();
                painter.translate(0, dy);
                painter.setOpacity(xform.opacity);
                paintDesktopRootItemDynamic(painter, state.model.renderData);
                paintDesktopRootItemStatic(painter, state.model.renderData);
                painter.restore();
            } else {
                painter.setOpacity(xform.opacity);
                paintDesktopRootItemDynamic(painter, state.model.renderData);
                paintDesktopRootItemStatic(painter, state.model.renderData);
            }
        } else {
            paintDesktopRootItemDynamic(painter, state.model.renderData);
            paintDesktopRootItemStatic(painter, state.model.renderData);
        }
    };

    const QRect dirtyBoundingRect = dirtyRegion.boundingRect();
    const bool useSpatialIndex = m_spatialIndex &&
                                 !m_spatialIndexDirty &&
                                 m_spatialIndexBounds == this->rect() &&
                                 dirtyBoundingRect.isValid() &&
                                 dirtyBoundingRect != rect() &&
                                 dirtyRegion.rectCount() <= 20 &&
                                 m_itemStates.size() >= 16;

    if (useSpatialIndex) {
        const QRect queryRect = dirtyBoundingRect.adjusted(-32, -32, 32, 32);
        const QVector<DesktopRootItemState*> candidates = m_spatialIndex->query(queryRect);

        for (const DesktopRootItemState* state : candidates) {
            if (!state || state->model.renderData.expandedFullName) {
                continue;
            }
            paintDesktopState(*state);
        }

        for (const DesktopRootItemState* state : candidates) {
            if (!state || !state->model.renderData.expandedFullName) {
                continue;
            }
            paintDesktopState(*state);
        }
    } else {
        // Paint normal items first so expanded filename labels can sit above siblings.
        for (const auto& state : m_itemStates) {
            if (state.model.renderData.expandedFullName) {
                continue;
            }
            paintDesktopState(state);
        }

        for (const auto& state : m_itemStates) {
            if (!state.model.renderData.expandedFullName) {
                continue;
            }
            paintDesktopState(state);
        }
    }

    // 2. 如果处于浮动模式（小组件在图标上方），后置绘制 CEF OSR 画布
    if (!widgetsEmbedded && runtimeLoaded) {
        bridge.renderCanvas(&painter, dirtyRegion);
    }

    const QRect indicatorRect = desktopAutoArrangeInsertionIndicatorRect();
    if (!indicatorRect.isNull() && dirtyRegion.intersects(indicatorRect.adjusted(-4, -3, 4, 3))) {
        painter.save();
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 220, 220, 80));
        painter.drawRoundedRect(indicatorRect.adjusted(-4, -3, 4, 3), 5, 5);
        painter.setBrush(QColor(0, 220, 220, 230));
        painter.drawRoundedRect(indicatorRect, 2, 2);
        painter.restore();
    }

    paintLinWidgetBlockedCellsOverlay(painter, dirtyRegion);

    if (m_isSelecting && !m_renderedSelectionRect.isNull() && m_renderedSelectionRect.isValid() && dirtyRegion.intersects(m_renderedSelectionRect)) {
        painter.save();
        const QRect selRect = m_renderedSelectionRect;

        if (m_fluentSelectionEnabled) {
            painter.setRenderHint(QPainter::Antialiasing, true);
            const qreal radius = qBound(0.0, static_cast<qreal>(m_fluentSelectionCornerRadius), 16.0);
            const qreal bWidth = qBound(0.0, static_cast<qreal>(m_selectionBorderWidth), 3.0);

            // 1. Apple 级极简高透光光学质感填充 (Optical Glass Fill)
            // 绝不堆砌浓重发光与机械角标，通透、克制、自然，与壁纸完美融合
            QLinearGradient fillGrad(selRect.topLeft(), selRect.bottomRight());
            QColor topFill = m_selectionFillColor;
            topFill.setAlpha(qBound(24, topFill.alpha() * 70 / 100, 110));
            QColor bottomFill = m_selectionFillColor;
            bottomFill.setAlpha(qBound(16, bottomFill.alpha() * 45 / 100, 80));
            fillGrad.setColorAt(0.0, topFill);
            fillGrad.setColorAt(1.0, bottomFill);
            painter.setBrush(fillGrad);

            // 2. 选框边框 (支持 0px 无描边 ~ 3px)
            if (bWidth > 0.05) {
                QColor strokeColor = m_selectionBorderColor;
                strokeColor.setAlpha(qBound(110, strokeColor.alpha() * 95 / 100, 220));
                painter.setPen(QPen(strokeColor, bWidth));
                const qreal halfW = bWidth / 2.0;
                if (radius > 0.1) {
                    painter.drawRoundedRect(QRectF(selRect).adjusted(halfW, halfW, -halfW, -halfW), radius, radius);
                } else {
                    painter.drawRect(QRectF(selRect).adjusted(halfW, halfW, -halfW, -halfW));
                }
            } else {
                painter.setPen(Qt::NoPen);
                if (radius > 0.1) {
                    painter.drawRoundedRect(QRectF(selRect), radius, radius);
                } else {
                    painter.drawRect(QRectF(selRect));
                }
            }

            // 3. 极细顶沿玻璃微光折射 (仅在有描边且有一定尺寸时渲染)
            if (bWidth > 0.05 && selRect.width() > 14 && selRect.height() > 10) {
                painter.setPen(QPen(QColor(255, 255, 255, 45), 1.0));
                const qreal inset = radius > 0.1 ? (radius + 1.0) : 1.0;
                painter.drawLine(QPointF(selRect.left() + inset, selRect.top() + bWidth / 2.0),
                                 QPointF(selRect.right() - inset, selRect.top() + bWidth / 2.0));
            }

        } else {
            // 经典 Windows 原生矩形选框 (先填充内部半透明底色，再绘制可选边框)
            const qreal bWidth = qBound(0.0, m_selectionBorderWidth, 3.0);
            painter.fillRect(selRect, m_selectionFillColor);
            if (bWidth > 0.05) {
                painter.setPen(QPen(m_selectionBorderColor, bWidth));
                painter.setBrush(Qt::NoBrush);
                const qreal halfW = bWidth / 2.0;
                painter.drawRect(QRectF(selRect).adjusted(halfW, halfW, -halfW, -halfW));
            }
        }

        // 4. 独立开关控制的极简轻量微胶囊徽章 (纯净数字，现代毛玻璃选框与经典选框均全面支持)
        if (m_selectionCountBadgeEnabled) {
            const int count = m_selectedDesktopPaths.size();
            if (count > 0 && selRect.width() >= 44 && selRect.height() >= 28) {
                painter.setRenderHint(QPainter::Antialiasing, true);
                const QString badgeText = QString::number(count);
                QFont badgeFont = painter.font();
                badgeFont.setPixelSize(10);
                badgeFont.setWeight(QFont::Medium);
                painter.setFont(badgeFont);

                QFontMetrics fm(badgeFont);
                const int textW = fm.horizontalAdvance(badgeText);
                const int badgeH = 18;
                const int padH = 6;
                const int badgeW = qMax(18, textW + padH * 2);

                const qreal badgeCorner = qBound(0.0, static_cast<qreal>(m_selectionBadgeRadius), badgeH / 2.0);
                const qreal badgeX = selRect.right() - badgeW - 5;
                const qreal badgeY = selRect.bottom() - badgeH - 5;
                const QRectF badgeRect(badgeX, badgeY, badgeW, badgeH);

                // a. 极轻柔环境微阴影
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(0, 0, 0, 40));
                painter.drawRoundedRect(badgeRect.translated(0, 1.0), badgeCorner, badgeCorner);

                // b. 半透明极简微磨砂黑底
                painter.setBrush(QColor(22, 24, 28, 205));

                // c. 0.75px 极细微透描边
                painter.setPen(QPen(QColor(255, 255, 255, 40), 0.75));
                painter.drawRoundedRect(badgeRect, badgeCorner, badgeCorner);

                // d. 纯白清晰数字 (无红点，无任何多余元素，纯净通透)
                painter.setPen(QColor(250, 252, 255));
                painter.drawText(badgeRect, Qt::AlignCenter, badgeText);
            }
        }

        painter.restore();
    }

    // 绘制小组件在按住 Ctrl 调整大小时的高亮辅助框和手柄
    const QString activeResizeId = m_resizingWidget ? m_resizingWidgetId :
                                  (m_draggingWidget ? m_draggedWidgetId : m_resizeHintWidgetId);
    if (!activeResizeId.isEmpty() && runtimeLoaded) {
        const QRect widgetGeom = bridge.widgetGeometry(activeResizeId);
        if (widgetGeom.isValid()) {
            painter.save();
            painter.setRenderHint(QPainter::Antialiasing, true);

            // 1. Fluent 强调色发光高亮边框
            QColor accentColor(0, 120, 215);
            painter.setPen(QPen(accentColor, 2.0));
            painter.setBrush(QColor(accentColor.red(), accentColor.green(), accentColor.blue(), 20));
            painter.drawRoundedRect(widgetGeom, 6, 6);

            // 2. 顶部拖拽抓手胶囊 (Top grab pill indicator)
            const int pillW = 38;
            const int pillH = 4;
            const int pillX = widgetGeom.center().x() - pillW / 2;
            const int pillY = widgetGeom.top() + 7;
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 255, 255, 180));
            painter.drawRoundedRect(QRect(pillX, pillY, pillW, pillH), 2, 2);

            // 3. 8个控制点小手柄 (Corners & Midpoints)
            const int handleSize = 7;
            const int half = handleSize / 2;
            const QPoint handles[8] = {
                widgetGeom.topLeft(),
                QPoint(widgetGeom.center().x(), widgetGeom.top()),
                widgetGeom.topRight(),
                QPoint(widgetGeom.right(), widgetGeom.center().y()),
                widgetGeom.bottomRight(),
                QPoint(widgetGeom.center().x(), widgetGeom.bottom()),
                widgetGeom.bottomLeft(),
                QPoint(widgetGeom.left(), widgetGeom.center().y())
            };

            for (const QPoint& pt : handles) {
                QRect handleRect(pt.x() - half, pt.y() - half, handleSize, handleSize);
                painter.setPen(QPen(accentColor, 1.5));
                painter.setBrush(Qt::white);
                painter.drawEllipse(handleRect);
            }

            // 3. 尺寸胶囊徽章 (e.g. 320 × 240)
            const QString sizeText = QString("%1 × %2").arg(widgetGeom.width()).arg(widgetGeom.height());
            QFont tipFont = painter.font();
            tipFont.setPixelSize(11);
            tipFont.setWeight(QFont::Medium);
            painter.setFont(tipFont);
            QFontMetrics fm(tipFont);
            const int textW = fm.horizontalAdvance(sizeText);
            const int badgeW = textW + 16;
            const int badgeH = 22;
            int badgeX = widgetGeom.center().x() - badgeW / 2;
            int badgeY = widgetGeom.top() - badgeH - 6;
            if (badgeY < 10) {
                badgeY = widgetGeom.top() + 8;
            }
            QRect badgeRect(badgeX, badgeY, badgeW, badgeH);

            painter.setPen(QPen(QColor(255, 255, 255, 60), 1.0));
            painter.setBrush(QColor(20, 20, 20, 210));
            painter.drawRoundedRect(badgeRect, 11, 11);

            painter.setPen(Qt::white);
            painter.drawText(badgeRect, Qt::AlignCenter, sizeText);

            painter.restore();
        }
    }
}


bool ListView::shouldPaintDesktopModelState(const DesktopRootItemState& state) const
{
    const bool isFading = (m_desktopHideAnim && m_desktopHideAnim->state() == QAbstractAnimation::Running);
    if (!m_iconShowRule.showDesktopIcons && (!isFading || m_desktopElementsOpacity <= 0.0)) {
        return false;
    }
    if (!state.model.isReady) {
        return false;
    }
    if (!state.model.visible) {
        return false;
    }
    if (!state.model.boxId.isEmpty()) {
        return false;
    }
    return true;
}

DesktopRootItemState* ListView::findDesktopItemAt(const QPoint& pos)
{
    return findDesktopStateAt(pos);
}

const DesktopRootItemState* ListView::findDesktopItemAt(const QPoint& pos) const
{
    return findDesktopStateAt(pos);
}

QRect ListView::desktopSelectionRectForItem(const DesktopRootItemState& state) const
{
    return ListViewSelectionController::desktopSelectionRectForItem(*this, state);
}


void ListView::setShortcutArrowVisible(bool visible) {
    m_iconShowRule.showShortCutArrow = visible;
    
    markAllDesktopRenderDataDirty();
    updateAllItemRenderData();
    rebuildSpatialIndex();
    update();
    requestSaveBoxes();
}



void ListView::onIconLoaded(const QString& memKey, const QString& filePath)
{
    Q_UNUSED(memKey);
    DesktopRootItemState* state = findDesktopStateByPath(filePath);
    if (state && state->model.visible) {
        state->renderDataDirty = true;
        m_fadingIconStartTimes[filePath] = QDateTime::currentMSecsSinceEpoch();
        if (!m_iconFadeTimer->isActive()) {
            m_iconFadeTimer->start();
        }
        update(state->model.renderData.itemRect);
    }
}

void ListView::onIconFadeTimerTimeout()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool anyFading = false;
    for (auto it = m_fadingIconStartTimes.begin(); it != m_fadingIconStartTimes.end();) {
        const qint64 elapsed = now - it.value();
        DesktopRootItemState* state = findDesktopStateByPath(it.key());
        if (state && state->model.visible) {
            if (elapsed < 100) {
                state->model.renderData.iconOpacity = qBound(0.1, (qreal)elapsed / 100.0, 1.0);
                update(state->model.renderData.itemRect);
                anyFading = true;
                ++it;
            } else {
                state->model.renderData.iconOpacity = 1.0;
                prepareDesktopRootItemStaticContentCache(state->model.renderData);
                update(state->model.renderData.itemRect);
                it = m_fadingIconStartTimes.erase(it);
            }
        } else {
            it = m_fadingIconStartTimes.erase(it);
        }
    }
    if (!anyFading || m_fadingIconStartTimes.isEmpty()) {
        m_iconFadeTimer->stop();
    }
}
