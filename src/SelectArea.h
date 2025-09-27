#pragma once

#include <QWidget>
#include <qlabel.h>
#include <QMouseEvent>
#include <QPainter>
#include <qdebug.h>
class SelectArea : public QWidget
{
	Q_OBJECT

public:
	SelectArea(QWidget *parent = nullptr);
	~SelectArea();
public:
    //鼠标点击坐标
    QPoint startPos;
    QRect rect;  //拖拽选择矩形
    //QPixmap* selectArea; //拖拽选择绘制区域
private:
    //鼠标松开
    void mouseReleaseEvent(QMouseEvent* event) override;
    //鼠标移动
    void mouseMoveEvent(QMouseEvent* event) override;
    ////鼠标事件
    void mousePressEvent(QMouseEvent* event) override;
    //绘制矩形
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
};
