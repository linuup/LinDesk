#ifndef LISTITEM_H
#define LISTITEM_H

#include <QWidget>
#include <QPainter>
#include "core/Data/ListItemData.h"


class ListItem : public QWidget {
    Q_OBJECT

public:
    explicit ListItem(QWidget* parent = nullptr);

    // 禁止拷贝构造函数
    //ListItem(const ListItem&) = delete;
    // 禁止赋值操作符
    //ListItem& operator=(const ListItem&) = delete;

    ~ListItem();

     // 设置对应的 ListItemData
    void setItemData(ListItemData data);
     //获取对应的 ListItemData
    ListItemData getItemData();

protected:
    void ListItem::contextMenuEvent(QContextMenuEvent* event);
    void ListItem::paintEvent(QPaintEvent* event);
    

private:
     ListItemData m_itemData;  // 关联的数据类
     
};
#endif // LISTITEM_H