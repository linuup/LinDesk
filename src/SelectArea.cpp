#include "SelectArea.h"

SelectArea::SelectArea(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground);
    syncToParent();
    hide();
}

void SelectArea::setSelectionRect(const QRect& selectionRect)
{
    if (m_rect == selectionRect) {
        return;
    }

    const QRect dirty = m_rect.united(selectionRect);
    m_rect = selectionRect;
    update(dirty.adjusted(-1, -1, 1, 1));
}

void SelectArea::syncToParent()
{
    if (parentWidget()) {
        setGeometry(parentWidget()->rect());
    }
}

void SelectArea::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (!m_rect.isValid() || m_rect.isEmpty()) {
        return;
    }

    QPainter painter(this);
    painter.setPen(QPen(QColor(80, 140, 230, 180), 1));
    painter.fillRect(m_rect, QColor(100, 160, 240, 70));
    painter.drawRect(m_rect.adjusted(0, 0, -1, -1));
}

SelectArea::~SelectArea()
{}
