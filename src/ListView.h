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
#include "DesktopIconsMonitor.h"
#include "DirectoryMonitor.h"
#include <SelectArea.h>

class ListView: public QWidget {
	Q_OBJECT

public:
	//桌面图标是否与网格对齐
	enum class IconPosMode {
		AlignIconToGrid,
		UnAlignIconToGrid
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
	
	// 获取单例对象
	static ListView* instance(QWidget* parent = nullptr);

	// 加载所有桌面图标数据
	std::vector<ListItemData> loadAllItemsData();

	/// <summary>
	/// Init default Data 
	/// </summary>
	void initialize();

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
	
	IconPosMode iconPosMode; //将图标与网格对齐
	
	bool IsSelecting() const {
		return m_isSelecting;
	}
	void DisableSelecting() {}

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
	

	void onError(const QString& error) {
		qDebug() << "Error DirectoryMonitor:" << error;
	}
protected:
	void paintEvent(QPaintEvent* event) override;  // 重写绘制事件来绘制背景
	void contextMenuEvent(QContextMenuEvent* event) override; //右键菜单事件
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;

	
	void enterEvent(QEvent* event) override;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override {
#else
	bool nativeEvent(const QByteArray & eventType, void* message, long* result) override {
#endif
		MSG* msg = static_cast<MSG*>(message);

		// 处理自定义的Shell通知消息s
		if (msg->message == WM_USER + 52) {
			handleShellNotification(msg->wParam, msg->lParam);
			if (result) *result = 0;
			return true;
		}

		return QWidget::nativeEvent(eventType, message, result);
	}

	void handleShellNotification(WPARAM wParam, LPARAM lParam) {
		LPITEMIDLIST* ppidl = nullptr;
		LONG lEvent = 0;

		HANDLE hNotifyLock = SHChangeNotification_Lock(
			reinterpret_cast<HANDLE>(wParam),
			static_cast<DWORD>(lParam),
			&ppidl,
			&lEvent
		);

		if (hNotifyLock) {
			processShellEvent(lEvent, ppidl);
			SHChangeNotification_Unlock(hNotifyLock);
		}
	}

	void processShellEvent(LONG lEvent, LPITEMIDLIST * ppidl) {
		QString eventStr;
		QString pathInfo;

		if (ppidl && ppidl[0]) {
			wchar_t path[MAX_PATH] = { 0 };
			if (SHGetPathFromIDListW(ppidl[0], path)) {
				pathInfo = QString::fromWCharArray(path);
			}
		}

		switch (lEvent) {
		case SHCNE_CREATE:
			eventStr = QString("File created: %1").arg(pathInfo);
			break;
		case SHCNE_DELETE:
			eventStr = QString("File deleted: %1").arg(pathInfo);
			break;
		case SHCNE_RENAMEITEM:
			eventStr = QString("File renamed: %1").arg(pathInfo);
			break;
			// ... 其他事件处理
		}

		if (!eventStr.isEmpty()) {
			qDebug() << QDateTime::currentDateTime().toString("[hh:mm:ss]") << eventStr;
		}
	}



	//signals:
	//	void itemSelected(ListItem* item);
	//	void selectionCleared();
	//	void selectionChanged();

private:
	explicit ListView(QWidget* parent = nullptr);
	~ListView();

	static ListView* m_instance;	//图标视图
	RightMenu* rightMenu;
	SortOrderType m_sortType = SortOrderType::SortByName_ASC;

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
