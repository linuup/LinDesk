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
		//this->resize(76, 88); // 设置默认大小

        setStyleSheet("background-color: yellow;");
    }

    // 禁止拷贝构造函数
    //ListItem(const ListItem&) = delete;
    // 禁止赋值操作符
    //ListItem& operator=(const ListItem&) = delete;

    ~ListItem() {
		qDebug() << "ListItem deleted:" << this->objectName();
    }

     // 设置对应的 ListItemData
     void setItemData(ListItemData data) {
         m_itemData = data;
         update();  // 数据改变时请求重绘
     }
     //获取对应的 ListItemData
	 ListItemData getItemData(){
        return m_itemData; 
    }

protected:
    void ListItem::paintEvent(QPaintEvent* event) {
        
        //qDebug() << this->size();
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
		painter.fillRect(0, 0, 76, 88, Qt::yellow);
        // 绘制图标
        QPixmap pixmap = m_itemData.icon().pixmap(48, 48); // 获取48x48的图标
        //QPixmap pixmap = QIcon("C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\Common7\\IDE\\devenv.exe").pixmap(64, 64); // 获取48x48的图标
        int iconX = (width() - pixmap.width()) / 2; // 图标水平居中
        int iconY = 10; // 图标距离顶部10像素
		painter.drawPixmap(iconX, iconY, pixmap);
        
        
        // 计算文字宽度并居中
        int textWidth = painter.fontMetrics().horizontalAdvance(m_itemData.fullName());
        int textX = (width() - textWidth) / 2; // 文本水平居中
        int textY = height() - 10; // 距离底部10像素
        painter.setBrush(QBrush(Qt::white));
        // 绘制文字
        painter.drawText(10, height()-10, m_itemData.fullName());
        
        
        
    }
    

private:
     ListItemData m_itemData;  // 关联的数据类
     
};
#endif // LISTITEM_H