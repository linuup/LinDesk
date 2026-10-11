#pragma once

#include <QWidget>
#include <QHash>
#include <QImage>
#include <QRect>
#include <QString>
#include <QVector>

namespace LinCef {

class LinWidgetCanvasWidget : public QWidget {
    Q_OBJECT

public:
    explicit LinWidgetCanvasWidget(QWidget* parent = nullptr);
    ~LinWidgetCanvasWidget() override;

    // Frame buffer management for active widgets
    void updateWidgetFrame(const QString& widgetId,
                           const QRect& widgetGeometry,
                           const QImage& image,
                           const QVector<QRect>& dirtyRects);
    void removeWidgetFrame(const QString& widgetId);
    void setWidgetVisible(const QString& widgetId, bool visible);
    void clearAllFrames();

    bool hasWidgetAt(const QPoint& pos) const;
    QString widgetAt(const QPoint& pos) const;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct WidgetRenderBuffer {
        QRect geometry;
        QImage imageBuffer;
        bool visible = true;
    };

    QHash<QString, WidgetRenderBuffer> m_widgetBuffers;
};

} // namespace LinCef
