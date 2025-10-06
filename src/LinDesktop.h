#pragma once
#include <QWidget>
#include <QDialog>
#include <QLabel>
#include <QApplication>
#include <QScreen>
#include <QList>
#include <QDir>
#include "ListItem.h"
#include <QUuid>
#include <QVector>
#include <QSharedPointer>
#include <RightMenu.h>
#include "ListView.h"
class LinDesktop : public QWidget {
    Q_OBJECT

public:
    explicit LinDesktop(QWidget* parent = nullptr);
    SelectArea* selectArea; //选择区域对象
    //First Floor
    ListView* listView;
    ~LinDesktop();

    QRect m_lastSelectRect;
	QPoint m_startPos;    //鼠标点击坐标
	
	//QRect drawingMaskRect;    //拖拽选择框矩形
    
	// 请求绘制鼠标悬浮效果
    void requestHoverEffect();
    
private:
    

protected:
    void mousePressEvent(QMouseEvent* event) {
        qDebug() << "LinDesktop LeftClick Pos:" << event->pos();
        qDebug() << "SelectArea" << selectArea->geometry();
        selectArea->show();
        //selecting = true;
        m_startPos = event->pos();
        //selectArea->setGeometry(m_startPos.x(), m_startPos.y(), 0, 0);
        update();
    }; 
    void mouseMoveEvent(QMouseEvent* event) {
        //qDebug() << "LinDesktop MouseMoveEvent Pos:" << event->pos();
        



//qDebug() << "MouseMoveEvent:CurrentPoint:" << currentPoint;
//update drawingMask 
        
        int start_x = m_startPos.x();
        int start_y = m_startPos.y();
        QRect rect(start_x, start_y, event->x() - start_x, event->y() - start_y);

        int rectWidth = abs(rect.width());
        int rectHeight = abs(rect.height());
        if (rect.width() == 0 || rect.height() == 0) {
            return;
        }
        //矩形在左上角
        if (rect.width() < 0 && rect.height() < 0) {
            rect.setRect(start_x + rect.width(), start_y + rect.height(), rectWidth, rectHeight);
        }
        //矩形在右上角
        else if (rect.width() > 0 && rect.height() < 0) {
            rect.setRect(start_x, start_y - rectHeight, rectWidth, rectHeight);
        }//矩形在左下角
        else if (rect.width() < 0 && rect.height() > 0) {
            rect.setRect(start_x - rectWidth, start_y, rectWidth, rectHeight);

        }
        
        //drawingMaskRect = rect;
        
        //selectArea->resize(rect.width(), rect.height());
        //selectArea->move(rect.x(), rect.y());
        //QRegion region(rect);
        //update(region);  // 仅更新悬浮项区域

        selectArea->rect = rect;

        // 只更新新旧选区重叠的区域
        rect = m_lastSelectRect.united(rect);
        
        update(rect); // 仅更新必要区域
        m_lastSelectRect = rect;


    };

    void mouseReleaseEvent(QMouseEvent* event) {
        qDebug() << "ListView mouseReleaseEvent  Pos:" << event->pos();
        listView->DisableSelecting();
		m_startPos = QPoint(0, 0);
        selectArea->hide();
        //selecting = false;
        selectArea->rect = QRect();
        
    
    };
    void paintEvent(QPaintEvent* event) {
        qDebug() << "=====================LinDesktop Painter";
		QPainter hoverMaskPainter(this);    // 用于绘制悬浮遮罩
		QPainter painter(this);
		painter.fillRect(rect(), QColor(171, 100, 192)); // Clear with transparent color
    //    if (currentHoverItemIndex != -1) {

    //    hoverMaskPainter.setRenderHint(QPainter::Antialiasing);  // 开启抗锯齿

    //    // 设置填充颜色为半透明蓝色
    //    //hoverMaskPainter.setBrush(QBrush(QColor(0, 0, 255, 50)));  // 半透明蓝色

    //    // 设置边框颜色为红色，边框宽度为 10
    //    //hoverMaskPainter.setPen(QPen(Qt::red, 10));

    //    // 绘制圆角矩形
    //    hoverMaskPainter.fillRect(this->currentHoverItemRect, QBrush(QColor(255, 255, 255, 50)));

    //    // 绘制圆角矩形

    //    //q.drawRoundedRect(this->currentHoverItemRect, 5, 5);  // x, y, width, height, border radius X, border radius Y
    //}
    };

    void contextMenuEvent(QContextMenuEvent* event) override;
    
};

