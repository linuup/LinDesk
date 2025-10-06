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

	void initialize();

	void loadIconLayer();

	//QVector<ListItem*> getAllItems();

	
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

	// 设置多选模式
	void setMultiSelection(bool enabled);

	// 获取是否启用多选
	bool isMultiSelection() const;

	// 设置背景壁纸图片
	void setBackgroundImage(const QString& imagePath);

	// 获取背景壁纸图片路径
	QString backgroundImage() const;
	
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
	/// <param name="objPath">不传参，默认请求桌面右键菜单</param>
	void requestSystemRightMenu(ObjectType objType, QPoint point, const QString objPath = nullptr) const {
		RightMenu* r = RightMenu::getInstance();
		r->setShowType(objType);
		r->showMenu(point, objPath);
	}
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
	

	QVector<ListItem*> m_items;             // 存储所有 ListItem 对象
	QVector<ListItem*> m_selectedItems;      // 存储所有选中的项

	bool m_isMultiSelection;               // 是否启用多选
	QPixmap m_backgroundImage;             // 存储背景壁纸图片

	ListItem* m_draggedItem = nullptr; // 当前被拖动的 ListItem
	bool m_isDragging = false;  // 是否正在拖动
	bool m_isSelecting = false;  // 是否正在拖拽选择
	QPoint m_dragm_startPos;     // 拖动开始时的位置
	QPoint m_startPos;     // 鼠标点击开始时的位置

};
