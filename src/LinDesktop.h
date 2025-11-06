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
#include "SysInfoUtil.h"
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
    void loadBackImagePixmap();
	// 请求绘制鼠标悬浮效果
    void requestHoverEffect();
    
private:
    QLabel* backImageLabel;  //background image
    QPixmap m_backImagePixmap;
    /// <summary>
    /// Insert to SystemDesktopWindow
    /// </summary>
    void embedToDesktop();
protected:
    void mousePressEvent(QMouseEvent* event);
    void mouseMoveEvent(QMouseEvent* event);
    void mouseReleaseEvent(QMouseEvent* event);
    void paintEvent(QPaintEvent* event);

    void contextMenuEvent(QContextMenuEvent* event) override;
    
};

