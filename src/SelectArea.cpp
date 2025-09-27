#include "SelectArea.h"
#include "QPushButton"
SelectArea::SelectArea(QWidget *parent)
	: QWidget(parent)
{
    QPalette pal=this->palette();
    pal.setBrush(QPalette::Background, QBrush(QColor(224, 224, 100, 100)));
    setPalette(pal);
    setAutoFillBackground(true);
    hide();
}
void SelectArea::mouseMoveEvent(QMouseEvent* event)
{

}
void SelectArea::mousePressEvent(QMouseEvent* event)
{
    /*rect.setRect(event->x(), event->y(), 0, 0);
    startPos = event->pos();
    update();
    hide();*/
}
void SelectArea::mouseReleaseEvent(QMouseEvent* event)
{
    /*rect.setRect(0,0,0,0);
    update();*/
}
void SelectArea::paintEvent(QPaintEvent* event)
{
}

void SelectArea::showEvent(QShowEvent* event)
{
    raise();
}

SelectArea::~SelectArea()
{}
