#include "desktop/widgets/cef/LinWidgetCanvasWidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QDebug>

namespace LinCef {

LinWidgetCanvasWidget::LinWidgetCanvasWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
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

    if (dirtyRects.isEmpty()) {
        update(widgetGeometry);
    } else {
        for (const QRect& dirty : dirtyRects) {
            // Translate widget-local dirty rect to canvas screen coordinates
            const QRect translatedDirty = dirty.translated(widgetGeometry.topLeft());
            update(translatedDirty);
        }
    }
}

void LinWidgetCanvasWidget::removeWidgetFrame(const QString& widgetId)
{
    auto it = m_widgetBuffers.find(widgetId);
    if (it != m_widgetBuffers.end()) {
        const QRect rectToClear = it.value().geometry;
        m_widgetBuffers.erase(it);
        update(rectToClear);
    }
}

void LinWidgetCanvasWidget::setWidgetVisible(const QString& widgetId, bool visible)
{
    auto it = m_widgetBuffers.find(widgetId);
    if (it != m_widgetBuffers.end() && it.value().visible != visible) {
        it.value().visible = visible;
        update(it.value().geometry);
    }
}

void LinWidgetCanvasWidget::clearAllFrames()
{
    m_widgetBuffers.clear();
    update();
}

bool LinWidgetCanvasWidget::hasWidgetAt(const QPoint& pos) const
{
    return !widgetAt(pos).isEmpty();
}

QString LinWidgetCanvasWidget::widgetAt(const QPoint& pos) const
{
    for (auto it = m_widgetBuffers.constBegin(); it != m_widgetBuffers.constEnd(); ++it) {
        if (it.value().visible && it.value().geometry.contains(pos)) {
            return it.key();
        }
    }
    return QString();
}

void LinWidgetCanvasWidget::paintEvent(QPaintEvent* event)
{
    if (m_widgetBuffers.isEmpty()) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QRect clipRect = event ? event->rect() : rect();

    for (auto it = m_widgetBuffers.constBegin(); it != m_widgetBuffers.constEnd(); ++it) {
        const WidgetRenderBuffer& buffer = it.value();
        if (!buffer.visible || buffer.imageBuffer.isNull()) {
            continue;
        }

        if (buffer.geometry.intersects(clipRect)) {
            painter.drawImage(buffer.geometry.topLeft(), buffer.imageBuffer);
        }
    }
}

} // namespace LinDesk
