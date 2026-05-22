#pragma once
#include <QWidget>
#include <QPropertyAnimation>
#include <QPainter>
#include <QMouseEvent>

class FluentToggleSwitch : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal thumbPos READ thumbPos WRITE setThumbPos)

public:
    explicit FluentToggleSwitch(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(40, 20);
        m_ani = new QPropertyAnimation(this, "thumbPos", this);
        m_ani->setDuration(200);
        m_ani->setEasingCurve(QEasingCurve::InOutQuad);
    }

    bool checked() const { return m_checked; }
    void setChecked(bool c, bool animated = true) {
        if (m_checked == c) return;
        m_checked = c;
        if (animated) {
            m_ani->setStartValue(m_thumbPos);
            m_ani->setEndValue(m_checked ? 1.0 : 0.0);
            m_ani->start();
        } else {
            setThumbPos(m_checked ? 1.0 : 0.0);
        }
        emit toggled(m_checked);
    }

    qreal thumbPos() const { return m_thumbPos; }
    void setThumbPos(qreal p) {
        m_thumbPos = p;
        update();
    }

signals:
    void toggled(bool checked);

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Core Fluent 2 Colors
        QColor accentColor(0, 120, 212); // Fluent Blue #0078d4
        QColor offColor(118, 118, 118);  // Neutral Dark Gray

        if (!isEnabled()) {
            const QColor bgColor = m_checked ? QColor(199, 224, 244) : QColor(229, 229, 229);
            const QColor thumbColor = m_checked ? QColor(255, 255, 255) : QColor(189, 189, 189);

            p.setPen(Qt::NoPen);
            p.setBrush(bgColor);
            p.drawRoundedRect(rect(), height() / 2, height() / 2);

            const int margin = 3;
            const int d = height() - margin * 2;
            const int startX = margin;
            const int endX = width() - margin - d;
            const int currentX = startX + static_cast<int>((endX - startX) * m_thumbPos);
            p.setBrush(thumbColor);
            p.drawEllipse(currentX, margin, d, d);
            return;
        }
        
        // Background Pill (Interpolated during animation)
        int r = offColor.red() + (accentColor.red() - offColor.red()) * m_thumbPos;
        int g = offColor.green() + (accentColor.green() - offColor.green()) * m_thumbPos;
        int b = offColor.blue() + (accentColor.blue() - offColor.blue()) * m_thumbPos;
        QColor bgColor(r, g, b);

        p.setPen(Qt::NoPen);
        p.setBrush(bgColor);
        p.drawRoundedRect(rect(), height() / 2, height() / 2);

        // Thumb Circle (Dynamic size/pos)
        // Fluent 2: Thumb is slightly smaller than the pill height
        int margin = 3;
        int d = height() - margin * 2;
        int startX = margin;
        int endX = width() - margin - d;
        int currentX = startX + (int)((endX - startX) * m_thumbPos);

        p.setBrush(Qt::white);
        p.drawEllipse(currentX, margin, d, d);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (isEnabled() && event->button() == Qt::LeftButton) {
            setChecked(!m_checked);
        }
    }

private:
    bool m_checked = false;
    qreal m_thumbPos = 0.0;
    QPropertyAnimation* m_ani = nullptr;
};
