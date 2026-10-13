#include "desktop/widgets/cef/LinWidgetCanvasWidget.h"
#include "desktop/ListView.h"

#include <QPainter>
#include <QPaintEvent>
#include <QDebug>

namespace LinCef {

LinWidgetCanvasWidget::LinWidgetCanvasWidget(QWidget* parent)
    : QWidget(parent)
{
    // 不启用 NativeWindow 和 TranslucentBackground，避免在 Windows 桌面(WorkerW)下冲突导致 UpdateLayeredWindowIndirect 报错
    setFocusPolicy(Qt::NoFocus);
}

LinWidgetCanvasWidget::~LinWidgetCanvasWidget() = default;

void LinWidgetCanvasWidget::updateWidgetFrame(const QString& widgetId,
                                              const QRect& widgetGeometry,
                                              const QImage& image,
                                              const QVector<QRect>& dirtyRects)
{
    WidgetRenderBuffer& buffer = m_widgetBuffers[widgetId];
    buffer.geometry = widgetGeometry;
    buffer.imageBuffer = image;
    buffer.visible = true;

    // 直接通知 ListView 进行图层合成局部重绘
    if (dirtyRects.isEmpty()) {
        if (ListView::instance()) {
            ListView::instance()->update(widgetGeometry);
        }
    } else {
        for (const QRect& dirty : dirtyRects) {
            const QRect translatedDirty = dirty.translated(widgetGeometry.topLeft());
            if (ListView::instance()) {
                ListView::instance()->update(translatedDirty);
            }
        }
    }
}

void LinWidgetCanvasWidget::removeWidgetFrame(const QString& widgetId)
{
    auto it = m_widgetBuffers.find(widgetId);
    if (it != m_widgetBuffers.end()) {
        const QRect rectToClear = it.value().geometry;
        m_widgetBuffers.erase(it);
        if (ListView::instance()) {
            ListView::instance()->update(rectToClear);
        }
    }
}

void LinWidgetCanvasWidget::setWidgetVisible(const QString& widgetId, bool visible)
{
    auto it = m_widgetBuffers.find(widgetId);
    if (it != m_widgetBuffers.end() && it.value().visible != visible) {
        it.value().visible = visible;
        if (ListView::instance()) {
            ListView::instance()->update(it.value().geometry);
        }
    }
}

void LinWidgetCanvasWidget::clearAllFrames()
{
    m_widgetBuffers.clear();
    if (ListView::instance()) {
        ListView::instance()->update();
    }
}

bool LinWidgetCanvasWidget::hasWidgetAt(const QPoint& pos) const
{
    return !widgetAt(pos).isEmpty();
}

QString LinWidgetCanvasWidget::widgetAt(const QPoint& pos) const
{
    for (auto it = m_widgetBuffers.constBegin(); it != m_widgetBuffers.constEnd(); ++it) {
        const WidgetRenderBuffer& buf = it.value();
        if (!buf.visible || !buf.geometry.contains(pos)) {
            continue;
        }

        // 默认页面：仅居中卡片区域响应交互，周围广大透明区域完全穿透给 ListView
        if (it.key() == QStringLiteral("__default_page__")) {
            const QRect geom = buf.geometry;
            const int cardW = 540;
            const int cardH = 300;
            const QRect cardRect(geom.x() + (geom.width() - cardW) / 2,
                                 geom.y() + (geom.height() - cardH) / 2,
                                 cardW, cardH);
            if (cardRect.contains(pos)) {
                return it.key();
            }
            continue;
        }

        // 针对普通小组件，如果点击位置的像素 Alpha 接近 0，则允许事件穿透给底层图标
        if (!buf.imageBuffer.isNull()) {
            const QPoint localPos = pos - buf.geometry.topLeft();
            const qreal dpr = buf.imageBuffer.devicePixelRatio();
            const int px = static_cast<int>(localPos.x() * (dpr > 0.0 ? dpr : 1.0));
            const int py = static_cast<int>(localPos.y() * (dpr > 0.0 ? dpr : 1.0));
            if (px >= 0 && px < buf.imageBuffer.width() &&
                py >= 0 && py < buf.imageBuffer.height()) {
                if (qAlpha(buf.imageBuffer.pixel(px, py)) < 10) {
                    continue; // 透明像素穿透
                }
            }
        }

        return it.key();
    }
    return QString();
}

void LinWidgetCanvasWidget::setShowBounds(bool show)
{
    if (m_showBounds != show) {
        m_showBounds = show;
        if (ListView::instance()) {
            ListView::instance()->update();
        }
    }
}

void LinWidgetCanvasWidget::renderCanvas(QPainter& painter, const QRegion& clipRegion)
{
    if (m_widgetBuffers.isEmpty()) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QRect clipBoundingRect = clipRegion.boundingRect();

    for (auto it = m_widgetBuffers.constBegin(); it != m_widgetBuffers.constEnd(); ++it) {
        const WidgetRenderBuffer& buffer = it.value();
        if (!buffer.visible || buffer.imageBuffer.isNull()) {
            continue;
        }

        if (clipBoundingRect.isNull() || buffer.geometry.intersects(clipBoundingRect)) {
            painter.drawImage(buffer.geometry, buffer.imageBuffer);

            if (m_showBounds) {
                painter.save();
                QPen pen(QColor(0, 180, 216), 2, Qt::DashLine);
                painter.setPen(pen);
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(buffer.geometry.adjusted(0, 0, -1, -1));

                const QString tag = QStringLiteral("CEF OSR: %1").arg(it.key());
                QFont font = painter.font();
                font.setPixelSize(11);
                font.setBold(true);
                painter.setFont(font);

                QFontMetrics fm(font);
                const QRect tagRect(buffer.geometry.x(), buffer.geometry.y(), fm.horizontalAdvance(tag) + 12, fm.height() + 6);
                painter.fillRect(tagRect, QColor(0, 180, 216, 220));
                painter.setPen(Qt::white);
                painter.drawText(tagRect, Qt::AlignCenter, tag);
                painter.restore();
            }
        }
    }

    painter.restore();
}

void LinWidgetCanvasWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
}

} // namespace LinCef
