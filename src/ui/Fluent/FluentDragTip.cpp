#include "ui/Fluent/FluentDragTip.h"

#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainterPath>
#include <QResizeEvent>
#include <windows.h>

FluentDragTip::FluentDragTip(QWidget* parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(0);

    m_label = new QLabel(this);
    m_label->setTextFormat(Qt::RichText);
    m_label->setAttribute(Qt::WA_StyledBackground, true);
    m_label->setStyleSheet(
        "QLabel {"
        "  background-color: rgba(255, 255, 255, 235);"
        "  color: #202020;"
        "  border: 1px solid rgba(0, 0, 0, 40);"
        "  border-radius: 8px;"
        "  padding: 10px 16px;"
        "  font-size: 13px;"
        "  font-family: 'Segoe UI Variable Display', 'Segoe UI', sans-serif;"
        "}"
    );

    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(m_label);
    shadow->setBlurRadius(20);
    shadow->setColor(QColor(0, 0, 0, 80));
    shadow->setOffset(0, 4);
    m_label->setGraphicsEffect(shadow);

    layout->addWidget(m_label);
}

void FluentDragTip::setText(const QString& text)
{
    m_label->setText(text);
    m_label->adjustSize();
    adjustSize();
    updateMask();
}

void FluentDragTip::showAt(const QPoint& globalPos)
{
    move(globalPos);
    show();
    raise();

    if (HWND hwnd = reinterpret_cast<HWND>(winId())) {
        SetWindowPos(
            hwnd,
            HWND_TOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW
        );
    }
}

void FluentDragTip::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateMask();
}

void FluentDragTip::updateMask()
{
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()), 12, 12);
    setMask(QRegion(path.toFillPolygon().toPolygon()));
}
