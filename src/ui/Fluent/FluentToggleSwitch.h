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
        setCursor(Qt::PointingHandCursor);
        m_ani = new QPropertyAnimation(this, "thumbPos", this);
        m_ani->setDuration(160);
        m_ani->setEasingCurve(QEasingCurve::OutCubic);
    }

    bool checked() const { return m_checked; }
    void setChecked(bool c, bool animated = true) {
        if (m_checked == c) return;
        m_checked = c;
        if (animated) {
            m_ani->stop();
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
    void enterEvent(QEvent* event) override {
        m_hovered = true;
        update();
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        m_hovered = false;
        update();
        QWidget::leaveEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const QRectF trackRect = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = trackRect.height() / 2.0;

        if (!isEnabled()) {
            const QColor bgColor = m_checked ? QColor(204, 204, 204) : QColor(245, 245, 245);
            const QColor borderColor = m_checked ? Qt::transparent : QColor(200, 200, 200);
            const QColor thumbColor = m_checked ? QColor(255, 255, 255) : QColor(160, 160, 160);

            p.setPen(borderColor == Qt::transparent ? Qt::NoPen : QPen(borderColor, 1.0));
            p.setBrush(bgColor);
            p.drawRoundedRect(trackRect, radius, radius);

            const qreal d = 12.0;
            const qreal margin = (height() - d) / 2.0;
            const qreal startX = margin;
            const qreal endX = width() - margin - d;
            const qreal currentX = startX + (endX - startX) * m_thumbPos;
            p.setPen(Qt::NoPen);
            p.setBrush(thumbColor);
            p.drawEllipse(QRectF(currentX, margin, d, d));
            return;
        }

        // Authentic WinUI 3 ToggleSwitch:
        // OFF state: light neutral track (#f5f5f5 / hover #ececec) with 1px #616161 stroke and #5c5c5c thumb
        // ON state: solid Fluent Accent Blue (#0067c0 / hover #1975c5) with white thumb
        const QColor offBg = m_hovered ? QColor(236, 236, 236) : QColor(245, 245, 245);
        const QColor onBg = m_hovered ? QColor(25, 117, 197) : QColor(0, 103, 192);

        const int bgR = qRound(offBg.red() + (onBg.red() - offBg.red()) * m_thumbPos);
        const int bgG = qRound(offBg.green() + (onBg.green() - offBg.green()) * m_thumbPos);
        const int bgB = qRound(offBg.blue() + (onBg.blue() - offBg.blue()) * m_thumbPos);
        const QColor trackFill(bgR, bgG, bgB);

        const int borderAlpha = qRound(255.0 * (1.0 - m_thumbPos));
        if (borderAlpha > 4) {
            p.setPen(QPen(QColor(97, 97, 97, borderAlpha), 1.0));
        } else {
            p.setPen(Qt::NoPen);
        }
        p.setBrush(trackFill);
        p.drawRoundedRect(trackRect, radius, radius);

        // Thumb circle (12px normal, 14px on hover like Win11 WinUI 3)
        const qreal d = m_hovered ? 13.5 : 12.0;
        const qreal margin = (height() - d) / 2.0;
        const qreal startX = 4.0;
        const qreal endX = width() - 4.0 - d;
        const qreal currentX = startX + (endX - startX) * m_thumbPos;

        const QColor offThumb(92, 92, 92);
        const QColor onThumb(255, 255, 255);
        const int thR = qRound(offThumb.red() + (onThumb.red() - offThumb.red()) * m_thumbPos);
        const int thG = qRound(offThumb.green() + (onThumb.green() - offThumb.green()) * m_thumbPos);
        const int thB = qRound(offThumb.blue() + (onThumb.blue() - offThumb.blue()) * m_thumbPos);

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(thR, thG, thB));
        p.drawEllipse(QRectF(currentX, margin, d, d));
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (isEnabled() && event->button() == Qt::LeftButton) {
            setChecked(!m_checked);
        }
    }

private:
    bool m_checked = false;
    bool m_hovered = false;
    qreal m_thumbPos = 0.0;
    QPropertyAnimation* m_ani = nullptr;
};
