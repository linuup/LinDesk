#pragma once

#include <QWidget>
#include <QColor>
#include <QPainter>
#include <QMouseEvent>
#include <QConicalGradient>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QtMath>

class FluentColorWheel : public QWidget {
    Q_OBJECT
public:
    explicit FluentColorWheel(QWidget* parent = nullptr)
        : QWidget(parent),
          m_currentColor(0, 120, 212),
          m_hue(m_currentColor.hueF() >= 0.0 ? m_currentColor.hueF() : 0.0),
          m_sat(m_currentColor.saturationF()),
          m_val(m_currentColor.valueF()),
          m_activePart(None)
    {
        setFixedSize(222, 184);
        setMouseTracking(true);
    }

    QColor color() const { return m_currentColor; }

    void setColor(const QColor& c) {
        QColor valid = c.isValid() ? c : QColor(0, 120, 212);
        m_currentColor = valid;
        m_hue = valid.hueF() >= 0.0 ? valid.hueF() : m_hue;
        m_sat = qBound(0.0, valid.saturationF(), 1.0);
        m_val = qBound(0.0, valid.valueF(), 1.0);
        update();
    }

signals:
    void colorChanged(const QColor& color);

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        p.setPen(Qt::NoPen);

        const QRectF wheelRect = hueWheelRect();
        const QRectF valueRect = valueSliderRect();
        const QPointF center = wheelRect.center();
        const qreal radius = wheelRect.width() / 2.0;

        p.setBrush(QColor(255, 255, 255, 120));
        p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 14, 14);

        // Base hue disc.
        QConicalGradient hueGradient(center, 90);
        for (int i = 0; i <= 360; i += 30) {
            hueGradient.setColorAt(i / 360.0, QColor::fromHsv(i % 360, 255, 255));
        }
        p.setBrush(hueGradient);
        p.drawEllipse(wheelRect);

        // Fade toward white in the center to mimic the system picker feel.
        QRadialGradient whiteBlend(center, radius * 0.98);
        whiteBlend.setColorAt(0.0, QColor(255, 255, 255, 255));
        whiteBlend.setColorAt(0.25, QColor(255, 255, 255, 180));
        whiteBlend.setColorAt(0.58, QColor(255, 255, 255, 28));
        whiteBlend.setColorAt(1.0, QColor(255, 255, 255, 0));
        p.setBrush(whiteBlend);
        p.drawEllipse(wheelRect);

        p.setPen(QPen(QColor(0, 0, 0, 28), 1));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(wheelRect.adjusted(0.5, 0.5, -0.5, -0.5));

        QColor hueOnly = QColor::fromHsvF(m_hue, 1.0, 1.0);
        Q_UNUSED(hueOnly);

        // Vertical value slider.
        QLinearGradient valueGradient(valueRect.topLeft(), valueRect.bottomLeft());
        valueGradient.setColorAt(0.0, QColor::fromHsvF(m_hue, m_sat, 1.0));
        valueGradient.setColorAt(1.0, QColor(24, 33, 41));
        p.setBrush(valueGradient);
        p.setPen(QPen(QColor(0, 0, 0, 18), 1));
        p.drawRoundedRect(valueRect, 5, 5);

        // Wheel selector.
        const qreal selectorRadius = radius * m_sat * 0.92;
        const qreal selectorAngle = m_hue * 2.0 * M_PI;
        const QPointF selector(center.x() + qCos(selectorAngle) * selectorRadius,
                               center.y() - qSin(selectorAngle) * selectorRadius);
        drawHandle(p, selector, QColor::fromHsvF(m_hue, m_sat, qMax(0.7, m_val)));

        // Value selector.
        const qreal valueY = valueRect.bottom() - (m_val * valueRect.height());
        QRectF valueKnob(valueRect.left() - 3, valueY - 6, valueRect.width() + 6, 12);
        p.setPen(QPen(QColor(255, 255, 255, 235), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(valueKnob, 6, 6);
    }

    void mousePressEvent(QMouseEvent* event) override {
        updateFromPosition(event->pos(), true);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (event->buttons() & Qt::LeftButton) {
            updateFromPosition(event->pos(), false);
        }
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        Q_UNUSED(event);
        m_activePart = None;
    }

private:
    enum ActivePart {
        None,
        Wheel,
        ValueSlider
    };

    QRectF hueWheelRect() const {
        return QRectF(10, 12, 156, 156);
    }

    QRectF valueSliderRect() const {
        return QRectF(180, 18, 28, 146);
    }

    void updateFromPosition(const QPoint& pos, bool determinePart) {
        const QRectF wheel = hueWheelRect();
        const QRectF value = valueSliderRect();

        if (determinePart) {
            if (value.adjusted(-8, -8, 8, 8).contains(pos)) {
                m_activePart = ValueSlider;
            } else if (pointInsideWheel(pos)) {
                m_activePart = Wheel;
            } else {
                m_activePart = None;
            }
        }

        bool changed = false;

        if (m_activePart == Wheel) {
            const QPointF center = wheel.center();
            const qreal dx = pos.x() - center.x();
            const qreal dy = center.y() - pos.y();
            qreal angle = qAtan2(dy, dx);
            if (angle < 0.0) {
                angle += 2.0 * M_PI;
            }

            const qreal maxRadius = wheel.width() / 2.0;
            const qreal dist = qSqrt(dx * dx + dy * dy);
            m_hue = angle / (2.0 * M_PI);
            m_sat = qBound(0.0, dist / (maxRadius * 0.92), 1.0);
            changed = true;
        } else if (m_activePart == ValueSlider) {
            const qreal valuePos = 1.0 - ((pos.y() - value.top()) / value.height());
            m_val = qBound(0.0, valuePos, 1.0);
            changed = true;
        }

        if (changed) {
            m_currentColor = QColor::fromHsvF(m_hue, m_sat, m_val);
            update();
            emit colorChanged(m_currentColor);
        }
    }

    bool pointInsideWheel(const QPoint& pos) const {
        const QRectF wheel = hueWheelRect();
        const QPointF center = wheel.center();
        const qreal radius = wheel.width() / 2.0;
        const qreal dx = pos.x() - center.x();
        const qreal dy = pos.y() - center.y();
        return (dx * dx + dy * dy) <= radius * radius;
    }

    void drawHandle(QPainter& p, const QPointF& center, const QColor& fill) const {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 55));
        p.drawEllipse(center + QPointF(0, 1), 8.5, 8.5);

        p.setBrush(Qt::white);
        p.drawEllipse(center, 8, 8);

        p.setBrush(fill);
        p.drawEllipse(center, 4.8, 4.8);
    }

    QColor m_currentColor;
    qreal m_hue;
    qreal m_sat;
    qreal m_val;
    ActivePart m_activePart;
};
