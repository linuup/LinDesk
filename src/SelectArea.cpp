#include "SelectArea.h"
#include "QPushButton"
SelectArea::SelectArea(QWidget *parent)
	: QWidget(parent)
{
    QPalette pal=this->palette();
    //pal.setBrush(QPalette::Background, QBrush(QColor(100, 160, 240, 70)));
    setPalette(pal);
    //setAutoFillBackground(true);
	setGeometry(this->parentWidget()->geometry());
    //setAttribute(Qt::WA_AlwaysStackOnTop);
    //setAttribute(Qt::WA_NoSystemBackground, true); // 禁止背景重绘
    hide();
    //pixmap = QPixmap(QSize(600,1080));
    //image = QImage(1, 1, QImage::Format_ARGB32);
    //image.fill(Qt::transparent);  // 透明背景
    //pixmap.fill(Qt::transparent);  // 填充透明背景
}
void SelectArea::mouseMoveEvent(QMouseEvent* event)
{
}
void SelectArea::mousePressEvent(QMouseEvent* event)
{

}
void SelectArea::mouseReleaseEvent(QMouseEvent* event)
{

}
void SelectArea::paintEvent(QPaintEvent* event)
{
     QPainter painter(this);
    painter.fillRect(rect, QColor(100, 160, 240, 70));

     
} 
void SelectArea::showEvent(QShowEvent* event)
{
}
SelectArea::~SelectArea()
{}
