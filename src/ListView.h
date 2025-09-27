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

class ListView : public QWidget {
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
	QPoint startPos;    //鼠标点击坐标
	bool choosingFlag = false;   //是否移动
	IconPosMode iconPosMode; //将图标与网格对齐
	QRect drawingMaskRect;    //拖拽选择框矩形
protected:
	void paintEvent(QPaintEvent* event) override;  // 重写绘制事件来绘制背景
	void contextMenuEvent(QContextMenuEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event);
	void enterEvent(QEvent* event) override;
	//# 鼠标按下
	void mousePressEvent(QMouseEvent* event) {
		selectArea->show();
		choosingFlag = true;
		startPos = event->pos();
		selectArea->setGeometry(startPos.x(), startPos.y(), 0, 0);
		drawingMaskRect = QRect(event->x(), event->y(), 0, 0);
		update();
	}
	// 鼠标松开
	void mouseReleaseEvent(QMouseEvent* event) {
		selectArea->hide();
		choosingFlag = false;
		selectArea->setGeometry(0, 0, 0, 0);
		drawingMaskRect = QRect(0, 0, 0, 0);
		update();
	}
		
//signals:
//	void itemSelected(ListItem* item);
//	void selectionCleared();
//	void selectionChanged();

private:
	explicit ListView(QWidget* parent = nullptr);
	~ListView();

	static ListView* m_instance;	//图标视图

	int currentHoverItemIndex = -1; // 当前鼠标悬停的图标索引
	QRect currentHoverItemRect;  // 当前鼠标悬停的图标区域
	RightMenu* rightMenu;   //右键菜单对象
	SelectArea* selectArea; //选择区域对象
	
	QVector<ListItem*> m_items;             // 存储所有 ListItem 对象
	QVector<ListItem*> m_selectedItems;      // 存储所有选中的项
	bool m_isMultiSelection;               // 是否启用多选
	QPixmap m_backgroundImage;             // 存储背景壁纸图片

	

};
