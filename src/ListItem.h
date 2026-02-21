#ifndef LISTITEM_H
#define LISTITEM_H

#include <QWidget>
#include <QPainter>
#include <QLabel>
#include <QPainterpath>
#include <QGraphicsEffect>
#include <QGraphicsDropShadowEffect>


#include "Data/ListItemData.h"


enum hoverStyle {
    default,

};
class ListItem : public QWidget {
    Q_OBJECT

public:
    explicit ListItem(QWidget* parent = nullptr);

    // 有参构造函数 - 通过 ListItemData 初始化
    explicit ListItem(const QString& path, QWidget* parent = nullptr);
    // 禁止拷贝构造函数
    //ListItem(const ListItem&) = delete;
    // 禁止赋值操作符
    //ListItem& operator=(const ListItem&) = delete;

    ~ListItem();
	// 初始化数据
	void initData();
    // 获取文件信息
    const QFileInfo& fileInfo() const { return m_fileInfo; };
    //const QRect& rect() const { return m_itemRect; };

    const QIcon& icon() const { return m_icon; }
    const QString& filePath() const { return m_filePath; }
    QString windowsFilePath() const {
        QString tmp = m_filePath;
        return tmp.replace("\\", "/");
    }
    const QString& baseName() const { return m_baseName; }
    const QSize& size() const { return m_itemSize; }
    const QPoint& pos() const { return m_itemPos; }
    const ObjectType type() const { return m_type; }
    const QString ListItem::getSortName() const {
        return m_baseName.toLower();
    }
    const QRect& getItemRect() const { return m_itemRect; }
    const QRect& getChooseItemRect() const { return m_chooseRect; }
    ListItemData getItemData(); // 设置对应的 ListItemData

    void setItemData(ListItemData data);
    void setItemSize(int width,int heigh);
    void setItemPos(int startX,int startY);
    void setItemPos(QPoint pos);

    /// <summary>
    /// Update Item UI LocationInfo
    /// </summary>
    /// <param name="pos"></param>
    void updateLocationInfo(QPoint pos);
    void checkSelf();
    void unCheckSelf();
    void openItem();
    
     //获取对应的 ListItemData
    
    bool isReady = false;
	bool isSelected = false; // 是否被选中
protected:
    void ListItem::contextMenuEvent(QContextMenuEvent* event);
    void ListItem::paintEvent(QPaintEvent* event);
    
    

private:
     ListItemData m_itemData;  // 关联的数据类
	 QSize m_itemSize;    //图标大小
	 QPoint m_itemPos;    //图标位置
     QPoint m_iconPos;   //图标起始点
     QRect m_itemRect;  //元素Rect
     QRect m_chooseRect;   //拖拽时选择判定区域
     QFileInfo m_fileInfo;
     QString m_baseName;           //基本文件名
     QString m_suffixName;         //文件后缀名
     QLabel* m_iconWidget = nullptr;       //图标控件
     QIcon m_icon;
     ObjectType m_type = ObjectType::Empty_ObjectType;  //数据对象类型
	 QString m_filePath;    //Windows 文件路径

     QLabel* textLabel = nullptr; //文件名称Label
     
};
#endif // LISTITEM_H