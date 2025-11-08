#pragma once
#include <QList>
#include <QObject>
#include <QPainter>
#include <QPixmap>
#include <QString>
#include <QWidget>
#include <QApplication>
#include <QVector>
#include <QScreen>
#include <QDialog>
#include <QLabel>
#include <QDir>
#include <QUuid>
#include <QSharedPointer>
#include "ListItem.h"
#include "RightMenu.h"
#include "DesktopIconsMonitor.h"
#include <SelectArea.h>

class ListView: public QWidget {
	Q_OBJECT

public:
	//桌面图标是否与网格对齐
	enum class IconPosMode {
		AlignIconToGrid,
		UnAlignIconToGrid
	};
	// 获取单例对象
	static ListView* instance(QWidget* parent = nullptr);

	// 加载所有桌面图标数据
	std::vector<ListItemData> loadAllItemsData();

	/// <summary>
	/// Init default Data 
	/// </summary>
	void initialize();

	void loadIconLayer();

	//QVector<ListItem*> getAllItems();

	/// <summary>
	/// 监听桌面系统文件注册表
	/// </summary>
	void startListeningSpecialIcon();

	QVector<ListItem*> items() {
		return m_items;
	}
	// 添加一个 ListItem
	void addItem(ListItem* item);

	// 设置选中的项
	void setSelectedItem(ListItem* item);

	// 获取当前选中的项
	ListItem* selectedItem() const;

	// 清除所有选中的项
	void clearSelection();

	// 检查一个项是否被选中
	bool isItemSelected(ListItem* item) const;

	// 获取所有选中的项（支持多选）
	QVector<ListItem*> selectedItems() const;

	// 获取是否启用多选
	bool isMultiSelection() const;
	
	IconPosMode iconPosMode; //将图标与网格对齐
	
	bool IsSelecting() {
		return m_isSelecting;
	}
	void DisableSelecting() { m_isSelecting = false; }

	int currentHoverItemIndex = -1; // 当前鼠标悬停的图标索引
	int previousHoverItemIndex = -1; // 上一个鼠标悬停的图标索引
	QRect currentHoverItemRect;  // 当前鼠标悬停的图标区域

	/// <summary>
	/// 请求系统右键菜单
	/// </summary>
	/// <param name="objType"></param>
	/// <param name="point">相对屏幕的坐标</param>
	/// <param name="checkedItems">选中项目列表</param>
	void requestSystemRightMenu(ObjectType objType, QPoint point, const QVector<ListItem*> checkedItems) const {
		RightMenu* r = RightMenu::getInstance();
		if (checkedItems.size() == 1) {
			r->setShowType(objType);
			r->showMenu(point, checkedItems.at(0));
			return;
		}else if (objType == ObjectType::BackGround) {
			r->setShowType(ObjectType::BackGround);
			r->showMenu(point, nullptr);
			return;
		}else {
			r->setShowType(ObjectType::MultObject);
			r->showMultObjectMenu(point, checkedItems.toStdVector());
		}
			
	}

	/// <summary>
	/// update 
	/// </summary>
	/// <param name="rect"></param>
	void refreshCheckedItem(QRect rect);
	void drawHoverEffect();
	void clearCheckedItem();
protected:
	void paintEvent(QPaintEvent* event) override;  // 重写绘制事件来绘制背景
	void contextMenuEvent(QContextMenuEvent* event) override; //右键菜单事件
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;

	
	void enterEvent(QEvent* event) override;

	//signals:
	//	void itemSelected(ListItem* item);
	//	void selectionCleared();
	//	void selectionChanged();

private:
	explicit ListView(QWidget* parent = nullptr);
	~ListView();

	static ListView* m_instance;	//图标视图
	RightMenu* rightMenu;
	

	QVector<ListItem*> m_allItems;             // 存储所有 ListItem 对象
	QVector<ListItem*> m_items;             // 存储桌面显示可见的ListItem对象
	QVector<ListItem*> m_hiddenIcons;             // 存储桌面系统图标状态
	QList<DesktopIconsMonitor::DesktopIcon> sysIconRegisterInfo;             // 存储桌面系统图标状态

	QVector<ListItem*> m_selectedItems;      // 存储所有选中的项

	bool m_isMultiSelection;               // 是否启用多选

	ListItem* m_draggedItem = nullptr; // 当前被拖动的 ListItem

	bool m_isShowShortCutArrow = false;	//控制是否显示快捷方式小箭头
	bool m_isDragging = false;  // 是否正在拖动
	bool m_isSelecting = false;  // 是否正在拖拽选择
	QPoint m_dragOffset;	//拖拽偏移量
	QPoint m_originalPos; // 记录原始位置，用于取消拖动

	QPoint m_startPos;     // 鼠标点击开始时的位置

	DesktopIconsMonitor* monitor;   //Monitor Desktop System Default Icon:Mycomputer,NetWork,Control Panel

};
