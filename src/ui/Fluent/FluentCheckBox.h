#pragma once

#include <QCheckBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

class FluentCheckBox : public QCheckBox {
public:
    explicit FluentCheckBox(QWidget* parent = nullptr) : QCheckBox(parent)
    {
        setFixedSize(20, 20);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        setFocusPolicy(Qt::StrongFocus);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const QRectF boxRect = rect().adjusted(2, 2, -2, -2);
        QColor borderColor = QColor(96, 108, 128);
        QColor fillColor = Qt::white;

        if (!isEnabled()) {
            borderColor = QColor(196, 204, 216);
            fillColor = QColor(242, 244, 247);
        } else if (isChecked()) {
            fillColor = m_pressed ? QColor(0, 95, 184) : QColor(0, 120, 212);
            borderColor = fillColor;
        } else if (m_pressed) {
            fillColor = QColor(232, 240, 250);
            borderColor = QColor(0, 95, 184);
        } else if (m_hovered || hasFocus()) {
            fillColor = QColor(246, 250, 255);
            borderColor = QColor(0, 120, 212);
        }

        painter.setPen(QPen(borderColor, 1.4));
        painter.setBrush(fillColor);
        painter.drawRoundedRect(boxRect, 3, 3);

        if (!isChecked()) {
            return;
        }

        QPen checkPen(isEnabled() ? QColor(Qt::white) : QColor(160, 170, 184), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(checkPen);
        QPainterPath path;
        path.moveTo(6.0, 10.5);
        path.lineTo(9.0, 13.2);
        path.lineTo(14.4, 7.2);
        painter.drawPath(path);
    }

    void enterEvent(QEvent* event) override
    {
        m_hovered = true;
        update();
        QCheckBox::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        m_hovered = false;
        m_pressed = false;
        update();
        QCheckBox::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && isEnabled()) {
            m_pressed = true;
            update();
        }
        QCheckBox::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        QCheckBox::mouseReleaseEvent(event);
        if (event->button() == Qt::LeftButton) {
            m_pressed = false;
            update();
        }
    }

    void changeEvent(QEvent* event) override
    {
        QCheckBox::changeEvent(event);
        if (event->type() == QEvent::EnabledChange) {
            setCursor(isEnabled() ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update();
        }
    }

private:
    bool m_hovered = false;
    bool m_pressed = false;
};
