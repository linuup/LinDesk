#include "desktop/SelectArea.h"
#include "desktop/ListView.h"

SelectArea::SelectArea(QWidget* parent)
    : QWidget(parent)
    , m_hostWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    hide();
}

SelectArea::~SelectArea()
{}

void SelectArea::setSelectionRect(const QRect& selectionRect)
{
    m_rect = selectionRect;
}

void SelectArea::clearAndHide()
{
    m_rect = QRect();
    hide();
}

void SelectArea::syncToParent()
{
    if (m_hostWidget) {
        syncToRect(m_hostWidget->rect());
    }
}

void SelectArea::syncToRect(const QRect& rect)
{
    if (geometry() == rect) {
        return;
    }
    setGeometry(rect);
}

void SelectArea::reloadColorsFromSettings()
{
    if (ListView* lv = ListView::instance()) {
        lv->loadSelectionColors();
        lv->update();
    }
}
