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
#include <QCollator>
#include <QSharedPointer>
#include <windows.h>
#include <shlobj_core.h>
#include "ListItem.h"
#include "RightMenu.h"
#include "Monitor/DesktopInfoMonitor.h"
#include "DirectoryMonitor.h"
#include "Monitor/LinDirectoryMonitor.h"
#include <SelectArea.h>

class ListView: public QWidget {
	Q_OBJECT

public:

	//桌面图标布局模式
	enum class IconPosMode {
		AlignIconToGrid,  // 对齐网格
		UnAlignIconToGrid // 自由位置
	};
	/// <summary>
	/// Windows 自然排序（Natural Sort）规则
	/// 数字智能排序：file1, file2, file10, file20（不是 file1, file10, file2, file20）
	/// 文件夹优先：所有文件夹排在文件前面
	/// 不区分大小写：File.txt和 file.txt视为相同
	/// 特殊字符处理：空格、连字符、下划线等有特定排序顺序
	/// 本地化支持：支持中文拼音排序、数字排序等
	/// 
	/// 排序优先级顺序
	/// 1. 特殊文件夹（桌面、文档、图片等系统文件夹）
	/// 2. 普通文件夹（按名称排序）
	/// 3. 普通文件（按名称排序）
	/// 4. 隐藏文件和系统文件
	/// </summary>
	enum class SortOrderType {
		SortByName_ASC,    // A-Z 升序排列
		SortByName_DESC,     // Z-A 降序排列
		SortBySize_ASC,
		SortBySize_DESC,
		SortByType_ASC,
		SortByType_DESC,
		SortByDateModified_ASC,
		SortByDateModified_DESC
	};
	// icon size show mode
	enum class ViewMode {
		LargeIcon,
		MediumIcon,
		SmallIcon
	};
	// icon show rule
	struct IconShowRule {
		IconPosMode iconPosMode = IconPosMode::AlignIconToGrid; //图标位置模式，默认与网格对齐
		ViewMode viewMode = ViewMode::MediumIcon;	//图标显示模式，默认中等图标
		SortOrderType sortType = SortOrderType::SortByName_ASC;	//图标排序方式，默认按名称升序排列
		bool autoArrange = false;        // 是否自动排列
		bool gridAlignment = true;      // 是否网格对齐
		bool showDesktopIcons = true;	//是否显示桌面图标
		bool doubleClickHideIcons = true; // 双击隐藏桌面图标
		bool showShortCutArrow = false;	//是否显示快捷方式小箭头
	};

	// 获取单例对象
	static ListView* instance(QWidget* parent = nullptr);

	// 加载所有桌面图标数据
	std::vector<ListItemData> loadAllItemsData();

	/// <summary>
	/// Init default Data 
	/// </summary>
	void initialize();

	// 
	void loadAutoLayout();
	//
	void loadGridLayout();
	// load Desktop Item on UI
	void loadIconLayer();

	SortOrderType getOppositeDirection();
	/// <summary>
	/// Using m_sortType to Sort Items
	/// </summary>
	/// <param name="order"></param>
	void sortItems(SortOrderType order);

	/// <summary>
	/// 
	/// </summary>
	/// <param name="ascending">
	/// True Ascending 
	/// False Descending
	/// </param>
	void sortByName(bool ascending = true);
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

	void removeItem(ListItem* item);

	// 设置选中的项
	void setSelectedItem(ListItem* item);

	// 获取当前选中的项
	ListItem* selectedItem() const;

	/// <summary>
	/// 生成Item的坐标
	/// </summary>
	/// <returns></returns>
	QPoint generateItemPosition();
	// 清除所有选中的项
	void clearSelection();

	// 检查一个项是否被选中
	bool isItemSelected(ListItem* item) const;

	// 获取所有选中的项（支持多选）
	QVector<ListItem*> selectedItems() const;

	// 获取是否启用多选
	bool isMultiSelection() const;
	
	bool IsSelecting() const {
		return m_isSelecting;
	}
	void DisableSelecting() {
		m_isSelecting = false;
	}

	int currentHoverItemIndex = -1; // 当前鼠标悬停的图标索引
	int previousHoverItemIndex = -1; // 上一个鼠标悬停的图标索引
	QRect currentHoverItemRect;  // 当前鼠标悬停的图标区域

	/// <summary>
	/// 请求系统右键菜单
	/// </summary>
	/// <param name="objType"></param>
	/// <param name="point">相对屏幕的坐标</param>
	/// <param name="checkedItems">选中项目列表</param>
	void requestSystemRightMenu(ObjectType objType, QPoint point, const QVector<ListItem*> checkedItems);
	/// <summary>
	/// update 
	/// </summary>
	/// <param name="rect"></param>
	void refreshCheckedItem(QRect rect);
	/// <summary>
	/// 
	/// </summary>
	/// <param name="path"></param>
	/// <returns></returns>
	ListItem* selectItemByPath(const QString path);
	void drawHoverEffect();
	void clearCheckedItem();

private slots:


	void onDirectoryChanged(const QString& path) {
		qDebug() << "[DirectoryMonitor]-Directory Changed:" << path;

	}
	void updateSystemIconVisibility(const QString& clsid, bool hidden);
	

	void onError(const QString& error) {
		qDebug() << "Error DirectoryMonitor:" << error;
	}
	
	void onFileSystemChanged(const FileSystemEventInfo& eventInfo);
	
	// RightMenu Slot handles
	void onMenuLargeIcons();
	void onMenuMediumIcons();
	void onMenuSmallIcons();
	void onMenuAutoArrangeIcons();
	void onMenuAlignIconsToGrid();
	void onMenuShowDesktopIcons();
	void onMenuDoubleClickHideIcons();
	
	void onMenuSortByName();
	void onMenuSortBySize();
	void onMenuSortByType();
	void onMenuSortByDate();
	
	void onMenuRefresh();
	void onMenuPaste();
	void onMenuPasteShortcut();
	void onMenuUndoDelete();

protected:
	void paintEvent(QPaintEvent* event) override;  // 重写绘制事件来绘制背景
	void contextMenuEvent(QContextMenuEvent* event) override; //右键菜单事件
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void enterEvent(QEvent* event) override;

	void refreshView();

private:
	explicit ListView(QWidget* parent = nullptr);
	~ListView();
	
	// 初始化右键菜单信号连接
	void initRightMenuConnections();

	static ListView* m_instance;	//图标视图
	RightMenu* rightMenu;
	IconShowRule m_iconShowRule;	// icon show rule
	

	// 网格布局常量
	const int m_iconWidth = 76;
	const int m_iconHeight = 78;
	const int m_rowSpacing = 8;
	const int m_colSpacing = 8;
	const int m_marginTop = 6;
	const int m_marginLeft = 2;

	QVector<ListItem*> m_allItems;             // 存储所有 ListItem 对象
	QVector<ListItem*> m_items;             // 存储桌面显示可见的ListItem对象
	QVector<ListItem*> m_hiddenIcons;             // 存储桌面系统图标状态
	QList<DesktopInfoMonitor::DesktopIcon> sysIconRegisterInfo;             // 存储桌面系统图标状态


	//store selected items
	QVector<ListItem*> m_selectedItems;
	// is enable multi selection
	bool m_isMultiSelection;            
	
	bool m_isDragging = false;  // 是否正在拖动
	bool m_isSelecting = false;  // 是否正在拖拽选择
	QPoint m_dragOffset;	//拖拽偏移量
	QPoint m_originalPos; // 记录原始位置，用于取消拖动s
	QPoint m_lastRightClickPos; // 记录最后一次右键点击的位置
	QPoint m_startPos;     // 鼠标点击开始时的位置
	ListItem* m_draggedItem = nullptr; // 当前被拖动的 ListItem
	//Monitor Desktop System Default Icon:Mycomputer,NetWork,Control Panel,Recycle Bin
	DesktopInfoMonitor* monitor;
	//use SHChangeNotifyRegister to monitor desktop directory changes
	//DirectoryMonitor* m_deskMonitor;
	//use readirectorychangesw to monitor directory changes
	LinDirectoryMonitor* m_linMonitor;  // Lin目录监控器
	
	struct GhostInfo {
		QLabel* widget;
		ListItem* item;
		QPoint offset; // 距离鼠标点击位置的偏移量
	};
	QVector<GhostInfo> m_ghosts; // 用于多选虚拟拖拽的半透明图标列表
};
