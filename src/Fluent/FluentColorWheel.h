#pragma once
#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QtMath>
#include <QConicalGradient>

/**
 * @brief A modern Fluent Design 2.0 Color Wheel.
 * Supports Hue selection via a circular ring and Saturation/Value via a central area.
 */
class FluentColorWheel : public QWidget {
    Q_OBJECT
public:
    explicit FluentColorWheel(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(100, 100);
        m_currentColor = QColor(0, 120, 212); // Default accent
        m_hue = m_currentColor.hueF();
    }

    QColor color() const { return m_currentColor; }
    void setColor(const QColor& c) {
        m_currentColor = c;
        m_hue = c.hueF();
        update();
    }

signals:
    void colorChanged(const QColor& color);

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        int side = qMin(width(), height()) - 10;
        QRect wheelRect(5, 5, side, side);
        int centerX = width() / 2;
        int centerY = height() / 2;
        int outerRadius = side / 2;
        // int innerRadius = outerRadius - 20; // This line is no longer needed based on the new code

        // 1. Draw Hue Ring
        QConicalGradient hueGrad(centerX, centerY, 0);
        for (double i = 0; i <= 1.0; i += 0.01) { // Smoother segments
            hueGrad.setColorAt(i, QColor::fromHsvF(1.0 - i, 1.0, 1.0));
        }
        p.setBrush(hueGrad);
        p.setPen(Qt::NoPen);
        
        int ringThickness = 6; // Elegant thin ring
        QPainterPath path;
        path.addEllipse(wheelRect);
        QPainterPath innerPath;
        int innerR = outerRadius - ringThickness;
        innerPath.addEllipse(QRect(centerX - innerR, centerY - innerR, innerR * 2, innerR * 2));
        p.drawPath(path.subtracted(innerPath));

        // 2. Center Dot
        p.setBrush(m_currentColor);
        p.setPen(QPen(Qt::white, 1));
        p.drawEllipse(QPointF(centerX, centerY), 3, 3);

        // 3. Draw Hue Knob (High-end Pill)
        double angle = m_hue * 2.0 * M_PI;
        double knobRadius = outerRadius - (ringThickness / 2.0);
        double hx = centerX + knobRadius * qCos(angle);
        double hy = centerY - knobRadius * qSin(angle);

        // Bloom (Soft Glow)
        QRadialGradient bloom(hx, hy, 12);
        QColor bloomC = m_currentColor;
        bloomC.setAlpha(60);
        bloom.setColorAt(0, bloomC);
        bloom.setColorAt(1, Qt::transparent);
        p.setBrush(bloom);
        p.drawEllipse(QPointF(hx, hy), 12, 12);

        // White Pill
        p.setBrush(Qt::white);
        p.setPen(QPen(QColor(0,0,0,60), 1));
        p.save();
        p.translate(hx, hy);
        p.rotate(-m_hue * 360 - 90);
        p.drawRoundedRect(QRect(-2.5, -6, 5, 12), 2.5, 2.5);
        p.restore();
    }

    void mousePressEvent(QMouseEvent* event) override { handleMouse(event); }
    void mouseMoveEvent(QMouseEvent* event) override { handleMouse(event); }

private:
    void handleMouse(QMouseEvent* event) {
        int centerX = width() / 2;
        int centerY = height() / 2;
        int dx = event->x() - centerX;
        int dy = centerY - event->y();
        double angle = qAtan2(dy, dx);
        if (angle < 0) angle += 2.0 * M_PI;
        m_hue = angle / (2.0 * M_PI);
        
        // Keep Sat/Val at 1.0 for the Ring picker unless updated elsewhere
        m_currentColor = QColor::fromHsvF(m_hue, 1.0, 1.0);
        update();
        emit colorChanged(m_currentColor);
    }

    QColor m_currentColor;
    double m_hue, m_sat, m_val;
};
