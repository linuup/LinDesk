#ifndef LISTITEM_H
#define LISTITEM_H

#include <QWidget>
#include <QPainter>
#include "core/Data/ListItemData.h"

class ListItem : public QWidget {
    Q_OBJECT

public:
    explicit ListItem(QWidget* parent = nullptr)
        : QWidget(parent) {
		this->resize(90, 90); // 设置默认大小
    }

    // 禁止拷贝构造函数
    //ListItem(const ListItem&) = delete;
    // 禁止赋值操作符
    //ListItem& operator=(const ListItem&) = delete;

    ~ListItem() {
		qDebug() << "ListItem deleted:" << this->objectName();
    }

    QString aaa;
     // 设置对应的 ListItemData
     void setItemData(ListItemData data) {
         m_itemData = data;
		 qDebug() << "======ListItem=======" << " Name:" << m_itemData.text();
         //update();  // 数据改变时请求重绘
     }
     //获取对应的 ListItemData
	 ListItemData getItemData(){
        return m_itemData; 
    }

protected:
    void ListItem::paintEvent(QPaintEvent* event) {
        if (&m_itemData == nullptr) {
            return;
        }
        QPainter painter(this);
        
        painter.setRenderHint(QPainter::Antialiasing);  // 启用抗锯齿

        // 设置蓝色边框的颜色和宽度
        QPen pen;
        if (m_itemData.fileType() == SpecialFolders::UnknownFolder) {
            pen.setColor(Qt::red);  // 设置边框颜色为蓝色
        }
        else {
            pen.setColor(Qt::blue);  // 设置边框颜色为蓝色
        }
        
    
        pen.setWidth(1);  // 设置边框宽度为 3
        painter.setPen(pen);  // 应用 QPen
        
        // 绘制矩形边框，使用控件的大小
        painter.drawRect(0, 0, width() - 1, height() - 1);  // 绘制边框，减去 1 防止外部溢出

        // 绘制一个蓝色圆形
        painter.setBrush(QBrush(Qt::blue));  // 设置填充颜色为蓝色
        int radius = 30;  // 圆的半径
        int centerX = width() / 2;  // 圆心的 X 坐标
        int centerY = height() / 2;  // 圆心的 Y 坐标
        //painter.drawEllipse(centerX - radius, centerY - radius, 2 * radius, 2 * radius);  // 绘制圆形
        
        painter.drawText(centerX - radius, centerY - radius, m_itemData.text());
    }
    

private:
     ListItemData m_itemData;  // 关联的数据类
     
};
#endif // LISTITEM_H