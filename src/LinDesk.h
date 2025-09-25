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

class DebugDialog : public QDialog {
public:
    DebugDialog(int iconCount, QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle("Debug");
        QLabel* label = new QLabel(QString("1").arg(iconCount), this);
        label->setAlignment(Qt::AlignCenter);
        resize(200, 80);
    }
};


class LinDesk : public QWidget {
    Q_OBJECT

public:
    // 获取单例对象
    static LinDesk* instance(QWidget* parent = nullptr);
	// 加载所有桌面图标数据
    std::vector<ListItemData> loadAllItemsData();

	void initialize();
    
    void loadIconLayer();

    //QVector<ListItem*> getAllItems();

    /// <summary>
    /// 请求系统右键菜单
    /// </summary>
    /// <param name="objType"></param>
    /// <param name="point"></param>
    /// <param name="objPath">不传参，默认请求桌面右键菜单</param>
    void requestSystemRightMenu(ObjectType objType, QPoint point, const QString objPath = nullptr) const {
        rightMenu->setShowType(objType);
        rightMenu->showMenu(point, objPath);
    }
private:
    explicit LinDesk(QWidget* parent = nullptr);
    ~LinDesk();

    QVector<ListItem*> m_listItems;

    static LinDesk* m_instance;

    RightMenu* rightMenu;   //右键菜单对象

protected:
	void contextMenuEvent(QContextMenuEvent* event) override;

    
};
