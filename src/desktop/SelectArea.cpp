#include "desktop/SelectArea.h"
#include <QCoreApplication>
#include <QSettings>

SelectArea::SelectArea(QWidget* parent)
    : QWidget(parent)
    , m_hostWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground);
    if (m_hostWidget) {
        syncToRect(m_hostWidget->rect());
    }
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
    if (m_hostWidget) {
        syncToRect(m_hostWidget->rect());
    }
}

void SelectArea::syncToRect(const QRect& rect)
{
    setGeometry(rect);
}

void SelectArea::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (!m_rect.isValid() || m_rect.isEmpty()) {
        return;
    }

    QSettings settings(QCoreApplication::applicationDirPath() + "/config.ini", QSettings::IniFormat);
    const QColor baseColor(settings.value("Desktop/SelectionRectangleColor", QStringLiteral("#64A0F0")).toString());
    QColor fillColor = baseColor.isValid() ? baseColor : QColor(100, 160, 240);
    QColor borderColor = fillColor.darker(110);
    fillColor.setAlpha(70);
    borderColor.setAlpha(180);

    QPainter painter(this);
    painter.setPen(QPen(borderColor, 1));
    painter.fillRect(m_rect, fillColor);
    painter.drawRect(m_rect.adjusted(0, 0, -1, -1));
}

SelectArea::~SelectArea()
{}
