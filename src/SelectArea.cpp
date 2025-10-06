#include "SelectArea.h"
#include "QPushButton"
SelectArea::SelectArea(QWidget *parent)
	: QWidget(parent)
{
    QPalette pal=this->palette();
    pal.setBrush(QPalette::Background, QBrush(QColor(224, 224, 100, 100)));
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
     //painter.setCompositionMode(QPainter::CompositionMode_Source); // 跳过透明计算
     //painter.fillRect(this->geometry(), QColor(224, 224, 100, 100));

    // 绘制到 QImage 上
    //image.scaled(rect.width(), rect.height(), Qt::KeepAspectRatio);
    //QPainter imagePainter(&image);
    
    //imagePainter.setRenderHint(QPainter::Antialiasing);  // 开启抗锯齿
    //imagePainter.fillRect(rect, QColor(224, 224, 100, 100));  // 绘制矩形
    //imagePainter.setBrush(QColor(224, 224, 100, 100));
    //imagePainter.drawRect(0, 0, rect.width(), rect.height());  // 绘制整个图像
    //imagePainter.end();

    //绘制图片
    painter.fillRect(rect, QColor(100, 224, 100, 100));

    //QPainter pixmapPainter(&pixmap);
    //pixmapPainter.setRenderHint(QPainter::Antialiasing);  // 开启抗锯齿
   	//pixmapPainter.fillRect(rect, QColor(0, 162, 232, 50));  // 绘制矩形
    ////pixmapPainter.setBrush(Qt::red);  // 设置矩形填充色
    //painter.drawPixmap(rect.x(),rect.y(), pixmap);
     
}

void SelectArea::showEvent(QShowEvent* event)
{
    raise();
}

SelectArea::~SelectArea()
{}
