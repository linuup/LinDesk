#include "desktop/DesktopComponentFloatingWindow.h"

#include "desktop/DesktopComponentRenderer.h"

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>

DesktopComponentFloatingWindow::DesktopComponentFloatingWindow(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setFocusPolicy(Qt::NoFocus);
}

void DesktopComponentFloatingWindow::setComponent(const DesktopComponentItemModel& item, qreal dpiScale)
{
    m_item = item;
    m_dpiScale = dpiScale;
    const QSize itemSize = item.rect.size().isEmpty() ? QSize(180, 90) : item.rect.size();
    m_item.rect = QRect(QPoint(0, 0), itemSize);
    m_item.position = QPoint(0, 0);
    m_item.selected = false;

    if (size() != itemSize) {
        resize(itemSize);
    }
    update();
}

void DesktopComponentFloatingWindow::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    paintDesktopComponent(painter, m_item, event->region(), m_dpiScale);
}

void DesktopComponentFloatingWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragStartGlobalPos = event->globalPos();
        m_dragStartWindowPos = pos();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void DesktopComponentFloatingWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && !m_item.id.isEmpty()) {
        const QPoint nextPos = m_dragStartWindowPos + (event->globalPos() - m_dragStartGlobalPos);
        move(nextPos);
        emit movedRequested(m_item.id, nextPos);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void DesktopComponentFloatingWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        if (!m_item.id.isEmpty()) {
            emit movedRequested(m_item.id, pos());
            emit moveFinished(m_item.id);
        }
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void DesktopComponentFloatingWindow::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !m_item.id.isEmpty()) {
        emit startPauseRequested(m_item.id);
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void DesktopComponentFloatingWindow::wheelEvent(QWheelEvent* event)
{
    if (!m_item.id.isEmpty()) {
        const int deltaMinutes = event->angleDelta().y() > 0 ? 1 : -1;
        emit wheelAdjustRequested(m_item.id, deltaMinutes);
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

void DesktopComponentFloatingWindow::contextMenuEvent(QContextMenuEvent* event)
{
    if (!m_item.id.isEmpty()) {
        emit contextMenuRequested(m_item.id, event->globalPos());
        event->accept();
        return;
    }
    QWidget::contextMenuEvent(event);
}
