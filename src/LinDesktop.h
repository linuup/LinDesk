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


	QPoint startPos;    //鼠标点击坐标

	bool choosingFlag = false;   //是否移动
	QRect drawingMaskRect;    //拖拽选择框矩形
    int currentHoverItemIndex = -1; // 当前鼠标悬停的图标索引
    int previousHoverItemIndex = -1; // 上一个鼠标悬停的图标索引
    QRect currentHoverItemRect;  // 当前鼠标悬停的图标区域
    /// <summary>
    /// 请求系统右键菜单
    /// </summary>
    /// <param name="objType"></param>
    /// <param name="point">相对屏幕的坐标</param>
    /// <param name="objPath">不传参，默认请求桌面右键菜单</param>
    void requestSystemRightMenu(ObjectType objType, QPoint point, const QString objPath = nullptr) const {
        RightMenu* r = RightMenu::getInstance();
        r->setShowType(objType);
        r->showMenu(point, objPath);
    }
private:
    RightMenu* rightMenu;

protected:
    //void contextMenuEvent(QContextMenuEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) {
        // draw mouse hover effect
        //if (!this->choosingFlag) {
        //    auto currentPoint = event->pos();
        //    // 如果鼠标悬浮的位置没变化，直接返回
        //    if (currentHoverItemRect.contains(currentPoint)) {
        //        previousHoverItemIndex = currentHoverItemIndex;
        //        /*qDebug() << "currentHoverItemIndex:" << currentHoverItemIndex;
        //        qDebug() << "previousHoverItemIndex:"<< previousHoverItemIndex;*/
        //        return; // 不进行重复更新
        //    }

        //    //&& currentHoverItemIndex == previousHoverItemIndex
        //    currentHoverItemIndex = -1;
        //    update();
        //    for (auto it = m_items.begin(); it != m_items.end(); it++)
        //    {
        //        ListItem* item = *it;
        //        QRect itemRect = item->rect();
        //        if (item->type() == ObjectType::SpecialFolder || ObjectType::Empty_ObjectType) {
        //            return;
        //        }
        //        if (itemRect.contains(currentPoint)) {

        //            // 鼠标右键点击位置在该图标区域内，显示该图标的右键菜单
        //            currentHoverItemIndex = std::distance(m_items.begin(), it);
        //            currentHoverItemRect = itemRect;

        //            //qDebug() << "MouseMoveEvent:Hovering on item[Index:"<<currentHoverItemIndex <<"]" << item->baseName();
        //            // 使用 QRegion 来更新特定区域
        //            QRegion region(currentHoverItemRect);
        //            update(region);  // 仅更新悬浮项区域
        //            return; // 退出循环，避免重复处理
        //        }
        //    }
        //}


        //qDebug() << "MouseMoveEvent:CurrentPoint:" << currentPoint;
        //update drawingMask 
        if (event->buttons() && Qt::LeftButton) {
            int start_x = startPos.x();
            int start_y = startPos.y();

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
            selectArea->rect = rect;
            selectArea->resize(rect.width(), rect.height());
            selectArea->move(rect.x(), rect.y());
            QRegion region(rect);
            update(rect);  // 仅更新悬浮项区域
        }
	}
    void contextMenuEvent(QContextMenuEvent* event) override;
		//# 鼠标按下
	void mousePressEvent(QMouseEvent* event) {
		selectArea->show();
		choosingFlag = true;
		startPos = event->pos();
		selectArea->setGeometry(startPos.x(), startPos.y(), 0, 0);
		update();
	}
	// 鼠标松开
	void mouseReleaseEvent(QMouseEvent* event) {
		selectArea->hide();
		choosingFlag = false;
		selectArea->setGeometry(0, 0, 0, 0);
		update();
	}
    
};

