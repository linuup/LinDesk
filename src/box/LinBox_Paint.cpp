#include "box/LinBox_Internal.h"

// ============================================================================
// LinBox Paint, Blur & Visual Effects Implementation
// ============================================================================

void LinBox::paintEvent(QPaintEvent* event) {
    if (m_flashingForRefresh) {
        return;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRegion dirtyRegion = event->region();

    const QRect contentRect = rect().adjusted(m_borderWidth, m_borderWidth, -m_borderWidth, -m_borderWidth);

    const QColor baseColor = effectiveBackgroundColor();
    const int r = baseColor.red(), g = baseColor.green(), b = baseColor.blue();
    const int a = qBound(0, (int)(m_bgOpacity * 255), 255);

    if (isFolderMapped() && isMappedFolderLoading() && m_itemStates.isEmpty()) {
        if (a > 0) {
            p.fillRect(contentRect, QColor(r, g, b, a));
        }
        p.setPen(QPen(QColor(255, 255, 255, 50), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRect(contentRect.adjusted(0, 0, -1, -1));
    } else if (m_glassEffect && !m_blurCache.isNull()) {
        p.drawPixmap(contentRect.topLeft(), m_blurCache);
        p.fillRect(contentRect, QColor(255, 255, 255, 8));

        if (m_layeredZDepth) {
            const int titleH = m_titleBar ? m_titleBar->height() : scaledPx(this, kBaseBoxTitleHeight);
            const QRect titleRect(contentRect.x(), contentRect.y(), contentRect.width(), titleH);

            QLinearGradient titleSheen(titleRect.topLeft(), titleRect.bottomLeft());
            titleSheen.setColorAt(0.0, QColor(255, 255, 255, 42));
            titleSheen.setColorAt(0.3, QColor(255, 255, 255, 14));
            titleSheen.setColorAt(1.0, QColor(255, 255, 255, 4));
            p.fillRect(titleRect, titleSheen);

            p.setPen(QPen(QColor(255, 255, 255, 140), 1.0));
            p.drawLine(titleRect.left() + 1, titleRect.top(), titleRect.right() - 1, titleRect.top());

            p.setPen(QPen(QColor(255, 255, 255, 28), 1.0));
            p.drawLine(titleRect.left(), titleRect.bottom(), titleRect.right(), titleRect.bottom());

            const QRect bodyRect(contentRect.x(), contentRect.y() + titleH, contentRect.width(), contentRect.height() - titleH);
            if (!m_isCollapsed && bodyRect.height() > 0) {
                QLinearGradient innerShadow(bodyRect.topLeft(), bodyRect.topLeft() + QPoint(0, 16));
                innerShadow.setColorAt(0.0, QColor(0, 0, 0, 36));
                innerShadow.setColorAt(1.0, QColor(0, 0, 0, 0));
                p.fillRect(QRect(bodyRect.left(), bodyRect.top(), bodyRect.width(), qMin(16, bodyRect.height())), innerShadow);
            }

            QLinearGradient borderSheen(contentRect.topLeft(), contentRect.bottomRight());
            borderSheen.setColorAt(0.0, QColor(255, 255, 255, 160));
            borderSheen.setColorAt(0.4, QColor(255, 255, 255, 75));
            borderSheen.setColorAt(1.0, QColor(255, 255, 255, 130));
            p.setPen(QPen(QBrush(borderSheen), 1.0));
            p.setBrush(Qt::NoBrush);
            p.drawRect(contentRect.adjusted(0, 0, -1, -1));
        } else {
            p.setPen(QPen(QColor(255, 255, 255, 110), 1.0));
            p.setBrush(Qt::NoBrush);
            p.drawRect(contentRect.adjusted(0, 0, -1, -1));
        }
    } else {
        QStyleOption opt;
        opt.init(this);
        style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);

        if (a > 0) {
            p.fillRect(contentRect, QColor(r, g, b, a));
        }

        if (m_layeredZDepth) {
            const int titleH = m_titleBar ? m_titleBar->height() : scaledPx(this, kBaseBoxTitleHeight);
            const QRect titleRect(contentRect.x(), contentRect.y(), contentRect.width(), titleH);

            QLinearGradient titleSheen(titleRect.topLeft(), titleRect.bottomLeft());
            titleSheen.setColorAt(0.0, QColor(255, 255, 255, 36));
            titleSheen.setColorAt(0.3, QColor(255, 255, 255, 12));
            titleSheen.setColorAt(1.0, QColor(255, 255, 255, 2));
            p.fillRect(titleRect, titleSheen);

            p.setPen(QPen(QColor(255, 255, 255, 120), 1.0));
            p.drawLine(titleRect.left() + 1, titleRect.top(), titleRect.right() - 1, titleRect.top());

            p.setPen(QPen(QColor(255, 255, 255, 26), 1.0));
            p.drawLine(titleRect.left(), titleRect.bottom(), titleRect.right(), titleRect.bottom());

            const QRect bodyRect(contentRect.x(), contentRect.y() + titleH, contentRect.width(), contentRect.height() - titleH);
            if (!m_isCollapsed && bodyRect.height() > 0) {
                QLinearGradient innerShadow(bodyRect.topLeft(), bodyRect.topLeft() + QPoint(0, 14));
                innerShadow.setColorAt(0.0, QColor(0, 0, 0, 28));
                innerShadow.setColorAt(1.0, QColor(0, 0, 0, 0));
                p.fillRect(QRect(bodyRect.left(), bodyRect.top(), bodyRect.width(), qMin(14, bodyRect.height())), innerShadow);
            }

            QLinearGradient borderSheen(contentRect.topLeft(), contentRect.bottomRight());
            borderSheen.setColorAt(0.0, QColor(255, 255, 255, 120));
            borderSheen.setColorAt(0.4, QColor(255, 255, 255, 50));
            borderSheen.setColorAt(1.0, QColor(255, 255, 255, 90));
            p.setPen(QPen(QBrush(borderSheen), 1.0));
            p.setBrush(Qt::NoBrush);
            p.drawRect(contentRect.adjusted(0, 0, -1, -1));
        } else {
            p.setPen(QPen(QColor(255, 255, 255, 50), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(contentRect.adjusted(0, 0, -1, -1));
        }
    }

    // Active Drag Pulse & Glowing Edge
    if (m_activeDragPulse && m_isDragHovered) {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);

        QColor accentColor = effectiveBackgroundColor();
        if (!accentColor.isValid() || accentColor.alpha() == 0) {
            accentColor = QColor(56, 189, 248);
        }

        // 1. Ambient outer glow halo
        p.setPen(Qt::NoPen);
        QColor haloColor = accentColor;
        haloColor.setAlpha(55);
        p.setBrush(haloColor);
        p.drawRoundedRect(contentRect.adjusted(-3, -3, 3, 3), 3, 3);

        // 2. High-energy magnetic pulse border
        QLinearGradient pulseGrad(contentRect.topLeft(), contentRect.bottomRight());
        pulseGrad.setColorAt(0.0, QColor(56, 189, 248, 245));
        pulseGrad.setColorAt(0.5, QColor(168, 85, 247, 230));
        pulseGrad.setColorAt(1.0, QColor(56, 189, 248, 245));
        p.setPen(QPen(QBrush(pulseGrad), 2.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(contentRect.adjusted(0, 0, -1, -1), 1, 1);

        // 3. Subtle acceptance tint overlay
        QColor acceptTint = accentColor;
        acceptTint.setAlpha(36);
        p.fillRect(contentRect, acceptTint);

        p.restore();
    }

    const QRect indicatorRect = insertionIndicatorRect();
    if (!indicatorRect.isNull()) {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);

        // 1. Compute vibrant, high-energy accent color (preserve full saturation!)
        QColor beamColor = effectiveBackgroundColor();
        if (!beamColor.isValid() || beamColor.saturation() < 35 || beamColor.value() < 40) {
            beamColor = QColor(0, 210, 255); // High-voltage Electric Cyan (#00D2FF)
        } else {
            int h, s, v;
            beamColor.getHsv(&h, &s, &v);
            s = qBound(190, s + 50, 255); // High saturation, never wash out into white
            v = qBound(225, v + 25, 255); // Vivid brightness
            beamColor.setHsv(h, s, v);
        }

        if (m_showType == BoxShowType::List_ShowType) {
            // === List Mode: Horizontal Flow Line with Start Anchor Bead ===
            const QRectF lineRect(indicatorRect.x() + 4, indicatorRect.y(), indicatorRect.width() - 8, qMax(2.5, (qreal)indicatorRect.height()));

            // 1. Outer Ambient Glow (Vivid colored aura)
            const QRectF glowRect = lineRect.adjusted(-2, -3, 2, 3);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(beamColor.red(), beamColor.green(), beamColor.blue(), 80));
            p.drawRoundedRect(glowRect, 3.5, 3.5);

            // 2. Horizontal Flow Line with rich gradient
            QLinearGradient listGrad(lineRect.topLeft(), lineRect.topRight());
            listGrad.setColorAt(0.0, QColor(255, 255, 255, 240));
            listGrad.setColorAt(0.06, beamColor);
            listGrad.setColorAt(0.75, beamColor);
            listGrad.setColorAt(1.0, QColor(beamColor.red(), beamColor.green(), beamColor.blue(), 25));
            p.setBrush(listGrad);
            p.drawRoundedRect(lineRect, lineRect.height() / 2.0, lineRect.height() / 2.0);

            // 3. Start Magnetic Pin Bead
            const QPointF beadCenter(lineRect.left(), lineRect.center().y());
            p.setBrush(QColor(beamColor.red(), beamColor.green(), beamColor.blue(), 110));
            p.drawEllipse(beadCenter, 5.5, 5.5);
            p.setBrush(beamColor);
            p.drawEllipse(beadCenter, 3.5, 3.5);
            p.setBrush(Qt::white);
            p.drawEllipse(beadCenter, 1.5, 1.5);
        } else {
            // === Icon / Grid Mode: Vertical Luminous Pin & Beam ===
            const qreal beamW = qMax<qreal>(3.5, (qreal)scaledPx(this, 3));
            const qreal centerX = indicatorRect.center().x();
            const QRectF beamRect(centerX - beamW / 2.0, indicatorRect.top() + 3, beamW, indicatorRect.height() - 6);

            // 1. Outer Ambient Glow Halo (saturated color aura)
            const QRectF haloRect = beamRect.adjusted(-3.5, -2.0, 3.5, 2.0);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(beamColor.red(), beamColor.green(), beamColor.blue(), 85));
            p.drawRoundedRect(haloRect, 5.0, 5.0);

            // 2. Core Luminous Capsule (saturated gradient from top to bottom)
            int h, s, v;
            beamColor.getHsv(&h, &s, &v);
            QColor shiftColor;
            shiftColor.setHsv((h + 20) % 360, s, v);

            QLinearGradient beamGrad(beamRect.topLeft(), beamRect.bottomLeft());
            beamGrad.setColorAt(0.0, beamColor);
            beamGrad.setColorAt(0.5, shiftColor);
            beamGrad.setColorAt(1.0, beamColor);
            p.setBrush(beamGrad);
            p.drawRoundedRect(beamRect, beamW / 2.0, beamW / 2.0);

            // 3. Subtle micro-specular core line
            p.setPen(QPen(QColor(255, 255, 255, 110), 1.0));
            p.drawLine(QPointF(centerX, beamRect.top() + 4), QPointF(centerX, beamRect.bottom() - 4));

            // 4. Terminal Magnetic Anchor Pins (Top & Bottom Jewel Beads)
            const QPointF topPin(centerX, beamRect.top());
            const QPointF bottomPin(centerX, beamRect.bottom());

            p.setPen(Qt::NoPen);
            // Outer halo
            p.setBrush(QColor(beamColor.red(), beamColor.green(), beamColor.blue(), 110));
            p.drawEllipse(topPin, 5.0, 5.0);
            p.drawEllipse(bottomPin, 5.0, 5.0);

            // Solid vibrant bead core
            p.setBrush(beamColor);
            p.drawEllipse(topPin, 3.2, 3.2);
            p.drawEllipse(bottomPin, 3.2, 3.2);

            // Inner specular white glint
            p.setBrush(Qt::white);
            p.drawEllipse(topPin, 1.4, 1.4);
            p.drawEllipse(bottomPin, 1.4, 1.4);
        }

        p.restore();
    }

    const QRect placeholderRect = mappedPlaceholderRect();
    if (!placeholderRect.isNull()) {
        const bool isEmptyNonLoading = m_itemStates.isEmpty() && !isMappedFolderLoading();
        const QString resourcePath = mappedFolderPlaceholderResourcePath(mappedFolderState(), isEmptyNonLoading);
        if (!resourcePath.isEmpty()) {
            QSvgRenderer renderer(resourcePath);
            if (renderer.isValid()) {
                renderer.render(&p, placeholderRect);
            }
        }
    }

    if (isFolderMapped() && isMappedFolderLoading() && m_itemStates.isEmpty()) {
        p.save();
        p.setPen(QColor(255, 255, 255, 190));
        p.setFont(AppFonts::uiFont(10));
        const QRect targetRect = !placeholderRect.isNull()
            ? QRect(placeholderRect.left() - 40, placeholderRect.bottom() + scaledPx(this, 8), placeholderRect.width() + 80, scaledPx(this, 24))
            : (m_scrollArea ? m_scrollArea->geometry() : rect());
        p.drawText(targetRect, Qt::AlignCenter, tr("Loading folder items..."));
        p.restore();
    }

    if (!m_isCollapsed && m_contentArea && m_scrollArea && m_scrollArea->isVisible() && m_scrollArea->viewport()) {
        const QPoint contentOrigin = m_contentArea->mapTo(this, QPoint(0, 0));
        p.save();
        p.setClipRect(m_scrollArea->geometry());
        for (const DesktopRootItemState& state : m_itemStates) {
            DesktopRootItemRenderData renderData = state.model.renderData;
            translateRenderData(renderData, contentOrigin);
            if (!dirtyRegion.intersects(renderData.itemRect)) {
                continue;
            }
            paintDesktopRootItemDynamic(p, renderData);
            paintDesktopRootItemStatic(p, renderData);
        }

        if (m_isSelectingItems && !m_selectionRect.isNull() && m_selectionRect.isValid()) {
            const QRect boxSelectionRect = m_selectionRect.translated(contentOrigin);
            if (dirtyRegion.intersects(boxSelectionRect)) {
                if (m_fluentSelectionEnabled) {
                    p.setRenderHint(QPainter::Antialiasing, true);
                    const qreal radius = qBound(0.0, static_cast<qreal>(m_fluentSelectionCornerRadius), 16.0);
                    const qreal bWidth = qBound(0.0, static_cast<qreal>(m_selectionBorderWidth), 3.0);
                    QLinearGradient fillGrad(boxSelectionRect.topLeft(), boxSelectionRect.bottomRight());
                    QColor topFill = m_selectionFillColor;
                    topFill.setAlpha(qBound(24, topFill.alpha() * 70 / 100, 110));
                    QColor bottomFill = m_selectionFillColor;
                    bottomFill.setAlpha(qBound(16, bottomFill.alpha() * 45 / 100, 80));
                    fillGrad.setColorAt(0.0, topFill);
                    fillGrad.setColorAt(1.0, bottomFill);
                    p.setBrush(fillGrad);

                    if (bWidth > 0.05) {
                        QColor strokeColor = m_selectionBorderColor;
                        strokeColor.setAlpha(qBound(110, strokeColor.alpha() * 95 / 100, 220));
                        p.setPen(QPen(strokeColor, bWidth));
                        const qreal halfW = bWidth / 2.0;
                        if (radius > 0.1) {
                            p.drawRoundedRect(QRectF(boxSelectionRect).adjusted(halfW, halfW, -halfW, -halfW), radius, radius);
                        } else {
                            p.drawRect(QRectF(boxSelectionRect).adjusted(halfW, halfW, -halfW, -halfW));
                        }
                    } else {
                        p.setPen(Qt::NoPen);
                        if (radius > 0.1) {
                            p.drawRoundedRect(QRectF(boxSelectionRect), radius, radius);
                        } else {
                            p.drawRect(QRectF(boxSelectionRect));
                        }
                    }
                } else {
                    // 经典 Windows 原生矩形选框 (先填充内部半透明底色，再绘制可选边框)
                    const qreal bWidth = qBound(0.0, m_selectionBorderWidth, 3.0);
                    p.fillRect(boxSelectionRect, m_selectionFillColor);
                    if (bWidth > 0.05) {
                        p.setPen(QPen(m_selectionBorderColor, bWidth));
                        p.setBrush(Qt::NoBrush);
                        const qreal halfW = bWidth / 2.0;
                        p.drawRect(QRectF(boxSelectionRect).adjusted(halfW, halfW, -halfW, -halfW));
                    }
                }
            }
        }
        p.restore();
    }

}

void LinBox::setRefreshFlashing(bool flashing)
{
    if (m_flashingForRefresh == flashing) {
        return;
    }
    m_flashingForRefresh = flashing;
    if (m_titleBar) {
        m_titleBar->setVisible(!flashing);
    }
    if (m_scrollArea) {
        m_scrollArea->setVisible(!flashing);
    }
    update();
}


void LinBox::setBackgroundColor(const QColor& color) { m_bgColor = color; refreshStyles(); emit geometryChanged(this); }
void LinBox::setBackgroundOpacity(float opacity) { m_bgOpacity = opacity; refreshStyles(); emit geometryChanged(this); }
void LinBox::setGlassEffect(bool enabled) {
    m_glassEffect = enabled;
    if (!enabled) {
        m_blurCache = QPixmap();
    }
    refreshStyles();
    scheduleBlurRefresh();
    emit geometryChanged(this);
}

void LinBox::refreshStyles() {
    this->setStyleSheet("background: transparent; border: none;");

    // Let LinBox::paintEvent render one unified background for the full box.
    // Keeping title/content transparent avoids subtle alpha/color mismatches.
    m_titleBar->setStyleSheet("QWidget#titleBar { background: transparent; border-radius: 0px; }");
    m_contentArea->setStyleSheet("QWidget#contentArea { background: transparent; border-radius: 0px; }");
    update();
    if (QWidget* parent = parentWidget()) {
        parent->update(geometry().adjusted(-16, -16, 16, 16));
    }
}

void LinBox::scheduleBlurRefresh()
{
    if (m_isCapturingBackground || !m_glassEffect || !isVisible() || width() <= 0 || height() <= 0) {
        return;
    }
    if (m_blurRefreshTimer && !m_blurRefreshTimer->isActive()) {
        m_blurRefreshTimer->start();
    }
}

namespace {

bool isPixmapEffectivelyTransparent(const QPixmap& pixmap, int alphaThreshold = 10)
{
    if (pixmap.isNull()) {
        return true;
    }
    const QImage img = pixmap.toImage();
    if (img.isNull()) {
        return true;
    }
    if (!img.hasAlphaChannel()) {
        return false;
    }

    const int w = img.width();
    const int h = img.height();
    if (w <= 0 || h <= 0) {
        return true;
    }

    int transparentCount = 0;
    int totalSamples = 0;
    const int stepX = qMax(1, w / 8);
    const int stepY = qMax(1, h / 8);

    for (int y = 0; y < h; y += stepY) {
        for (int x = 0; x < w; x += stepX) {
            totalSamples++;
            if (qAlpha(img.pixel(x, y)) <= alphaThreshold) {
                transparentCount++;
            }
        }
    }

    if (totalSamples == 0) {
        return true;
    }

    return (transparentCount * 100 / totalSamples) >= 80;
}

} // namespace

QPixmap LinBox::captureBackgroundPixmap() const
{
    if (width() <= 0 || height() <= 0) {
        return QPixmap();
    }

    const qreal dpr = qMax<qreal>(1.0, devicePixelRatioF());
    const QRect captureRect = geometry();
    const QSize physicalSize(qMax(1, qRound(captureRect.width() * dpr)),
                             qMax(1, qRound(captureRect.height() * dpr)));

    QPixmap background(physicalSize);
    background.setDevicePixelRatio(dpr);
    background.fill(Qt::transparent);

    // 1. Search for ancestor LinDesktop to retrieve the pristine, unpolluted wallpaper.
    LinDesktop* desktop = nullptr;
    for (QWidget* w = parentWidget(); w; w = w->parentWidget()) {
        if (auto* d = qobject_cast<LinDesktop*>(w)) {
            desktop = d;
            break;
        }
    }

    bool hasWallpaper = false;
    if (desktop && !desktop->scaledWallpaperPixmap().isNull()) {
        const QPixmap& wp = desktop->scaledWallpaperPixmap();
        const qreal wpDpr = qMax<qreal>(1.0, wp.devicePixelRatio());
        const QPoint posInDesktop = mapTo(desktop, QPoint(0, 0));
        const QRect cropPhysical(qRound(posInDesktop.x() * wpDpr),
                                 qRound(posInDesktop.y() * wpDpr),
                                 qRound(width() * wpDpr),
                                 qRound(height() * wpDpr));
        const QRect validCrop = cropPhysical.intersected(QRect(0, 0, wp.width(), wp.height()));
        if (!validCrop.isEmpty()) {
            QPainter p(&background);
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            const QPixmap cropped = wp.copy(validCrop);
            p.drawPixmap(0, 0, width(), height(), cropped);
            hasWallpaper = true;
        }
    }

    // 2. If LinDesktop is not found or has no wallpaper pixmap, load Windows wallpaper directly.
    if (!hasWallpaper) {
        static QPixmap s_cachedWp;
        static QString s_cachedWpPath;
        static qint64 s_lastCheckMs = 0;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - s_lastCheckMs > 1500 || s_cachedWp.isNull()) {
            s_lastCheckMs = now;
            const QString wpPath = SysInfoUtil::GetRobustWallpaperPath();
            if (!wpPath.isEmpty() && (wpPath != s_cachedWpPath || s_cachedWp.isNull())) {
                QPixmap raw;
                if (raw.load(wpPath)) {
                    s_cachedWp = raw;
                    s_cachedWpPath = wpPath;
                }
            }
        }

        if (!s_cachedWp.isNull()) {
            QPainter p(&background);
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            const QRect screenGeom = QApplication::primaryScreen()
                ? QApplication::primaryScreen()->geometry()
                : QRect(0, 0, 1920, 1080);
            const QPoint globalPos = mapToGlobal(QPoint(0, 0));
            const qreal ratioX = (qreal)s_cachedWp.width() / (qreal)qMax(1, screenGeom.width());
            const qreal ratioY = (qreal)s_cachedWp.height() / (qreal)qMax(1, screenGeom.height());
            const QRect srcRect(qRound((globalPos.x() - screenGeom.x()) * ratioX),
                                qRound((globalPos.y() - screenGeom.y()) * ratioY),
                                qRound(width() * ratioX),
                                qRound(height() * ratioY));
            const QRect validSrc = srcRect.intersected(QRect(0, 0, s_cachedWp.width(), s_cachedWp.height()));
            if (!validSrc.isEmpty()) {
                p.drawPixmap(QRect(0, 0, width(), height()), s_cachedWp, validSrc);
                hasWallpaper = true;
            }
        }
    }

    if (!hasWallpaper) {
        background.fill(QColor(30, 30, 30));
    }

    // 3. Render sibling widgets underneath (strictly excluding this LinBox!)
    QWidget* sourceParent = parentWidget();
    if (sourceParent) {
        LinBox* self = const_cast<LinBox*>(this);
        self->m_isCapturingBackground = true;

        QPainter painter(&background);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.translate(-captureRect.topLeft());

        const QObjectList childObjects = sourceParent->children();
        for (QObject* childObject : childObjects) {
            QWidget* childWidget = qobject_cast<QWidget*>(childObject);
            if (!childWidget || childWidget == this || !childWidget->isVisible()) {
                continue;
            }

            const QRect childRect = childWidget->geometry();
            if (!childRect.intersects(captureRect)) {
                continue;
            }

            childWidget->render(
                &painter,
                childRect.topLeft(),
                QRegion(),
                QWidget::DrawWindowBackground | QWidget::DrawChildren | QWidget::IgnoreMask);
        }

        self->m_isCapturingBackground = false;
    }

    return background;
}

void LinBox::updateBlurCache()
{
    if (!m_glassEffect || !isVisible()) {
        return;
    }

    const QPixmap captured = captureBackgroundPixmap();
    if (captured.isNull()) {
        m_blurCache = QPixmap();
        return;
    }

    const QRect contentRect = rect().adjusted(m_borderWidth, m_borderWidth, -m_borderWidth, -m_borderWidth);
    if (contentRect.width() <= 0 || contentRect.height() <= 0) {
        m_blurCache = QPixmap();
        return;
    }

    const QPixmap contentCapture = captured.copy(contentRect);
    m_blurCache = buildBlurredGlass(contentCapture, effectiveBackgroundColor(), m_bgOpacity);
    if (isPixmapEffectivelyTransparent(m_blurCache, 40)) {
        m_blurCache = QPixmap();
    }
    update();
}


void LinBox::onIconLoaded(const QString& memKey, const QString& filePath)
{
    Q_UNUSED(memKey);
    DesktopRootItemState* state = findItemStateByPath(filePath);
    if (state) {
        state->renderDataDirty = true;
        m_fadingIconStartTimes[filePath] = QDateTime::currentMSecsSinceEpoch();
        if (!m_iconFadeTimer->isActive()) {
            m_iconFadeTimer->start();
        }
        update();
    }
}

void LinBox::onIconFadeTimerTimeout()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool anyFading = false;
    for (auto it = m_fadingIconStartTimes.begin(); it != m_fadingIconStartTimes.end();) {
        const qint64 elapsed = now - it.value();
        DesktopRootItemState* state = findItemStateByPath(it.key());
        if (state) {
            if (elapsed < 100) {
                state->model.renderData.iconOpacity = qBound(0.1, (qreal)elapsed / 100.0, 1.0);
                anyFading = true;
                ++it;
            } else {
                state->model.renderData.iconOpacity = 1.0;
                prepareDesktopRootItemStaticContentCache(state->model.renderData);
                it = m_fadingIconStartTimes.erase(it);
            }
        } else {
            it = m_fadingIconStartTimes.erase(it);
        }
    }
    update();
    if (!anyFading || m_fadingIconStartTimes.isEmpty()) {
        m_iconFadeTimer->stop();
    }
}
