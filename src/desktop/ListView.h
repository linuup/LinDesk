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
#include <QElapsedTimer>
#include <QCollator>
#include <QTimer>
#include <QSharedPointer>
#include <QPointer>
#include <QMap>
#include <memory>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <windows.h>
#include <shlobj_core.h>
#include "desktop/ListItem.h"
#include "services/DesktopItemLoader.h"
#include "services/LayoutPersistenceService.h"
#include "desktop/RightMenu.h"
#include "desktop/RenameManager.h"
#include "monitor/DesktopInfoMonitor.h"
#include "monitor/DirectoryMonitor.h"
#include "monitor/LinDirectoryMonitor.h"
#include "desktop/SelectArea.h"
#include "ui/SettingUI.h"
class LinBox;
class BoxManager; 
class DesktopQuadTree;

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
		Default,					// 默认排序（无特殊排序）
		SortByName_ASC,				// A-Z 升序排列
		SortByName_DESC,			// Z-A 降序排列
		SortBySize_ASC,
		SortBySize_DESC,
		SortByType_ASC,
		SortByType_DESC,
		SortByDateModified_ASC,
		SortByDateModified_DESC,
		SortByCategorized			// 自动分类排序（系统、文件夹、应用、文档）
	};
	// icon size show mode
	enum class ViewMode {
		LargeIcon,
		MediumIcon,
		SmallIcon
	};

	// ViewMode 预设尺寸
	static constexpr int kLargeIconW  = 100, kLargeIconH  = 104;
	static constexpr int kMediumIconW = 76,  kMediumIconH = 78;
	static constexpr int kSmallIconW  = 62,  kSmallIconH  = 64;
	static constexpr int kLargeSpacing  = 12;
	static constexpr int kMediumSpacing = 10;
	static constexpr int kSmallSpacing  = 8;

	// 快捷方式小图标预设尺寸 (与 ViewMode 一一对应)
	static constexpr int kLargeArrowSize  = 42;
	static constexpr int kMediumArrowSize = 32;
	static constexpr int kSmallArrowSize  = 24;
	static constexpr int kListArrowSize   = 20;
	// icon show rule
	struct IconShowRule {
		IconPosMode iconPosMode = IconPosMode::AlignIconToGrid; //图标位置模式，默认与网格对齐
		ViewMode viewMode = ViewMode::MediumIcon;	//图标显示模式，默认中等图标
		SortOrderType sortType = SortOrderType::Default;	// 默认使用"默认排序"
		bool autoArrange = false;        // 是否自动排列
		bool gridAlignment = false;      // 是否网格对齐
		bool showDesktopIcons = true;	//是否显示桌面图标
		bool doubleClickHideIcons = true; // 双击隐藏桌面图标
		bool showShortCutArrow = true;	//是否显示快捷方式小箭头
	};

	// 加载所有桌面图标数据

	/// <summary>
	/// Init default Data 
	/// </summary>
	void initialize();

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
	void sortBySize(bool ascending = true);
	void sortByType(bool ascending = true);
	void sortByDate(bool ascending = true);
	void sortByCategorized();

	/// <summary>
	/// 监听桌面系统文件注册表
	/// </summary>
	void startListeningSpecialIcon();
	
	QVector<ListItem*> items() {
		return m_items;
	}
	// 添加一个 ListItem
	void addItem(ListItem* item, bool refresh = true);  // Add to the desktop render collection
    void addItemNoDelete(ListItem* item);  // Add existing item back to desktop

	void removeItem(ListItem* item);
    void removeItemNoDelete(ListItem* item);
    ListItem* findItemByPath(const QString& filePath) const;

	// 设置选中的项
	// 获取当前选中的项
	bool persistenceEnabled() const { return m_enablePersistence; }
	void setPersistenceEnabled(bool enabled);
	void setShortcutArrowVisible(bool visible);
	bool shortcutArrowVisible() const { return m_iconShowRule.showShortCutArrow; }
    void setDoubleClickHideIconsEnabled(bool enabled);
    	bool isDoubleClickHideIconsEnabled() const;
	bool isAutoArrangeEnabled() const { return m_iconShowRule.autoArrange; }
	ViewMode viewMode() const { return m_iconShowRule.viewMode; }
	int rowSpacing() const { return m_rowSpacing; }
	int colSpacing() const { return m_colSpacing; }
	void setDebugGridSpacing(int rowSpacing, int colSpacing);
	ListItem* draggedItem() const { return m_draggedItem; }
	void loadPersistentLayout();
	void updateRightMenuState();

	/// <summary>
	/// 生成Item的坐标
	/// </summary>
	/// <returns></returns>
	QPoint generateItemPosition();
	// 清除所有选中的项
	// 检查一个项是否被选中
	// 获取所有选中的项（支持多选）
	QVector<LinBox*> boxes() const;
    
	// 获取是否启用多选
	bool IsSelecting() const {
		return m_isSelecting;
	}

	void showSettings(LinBox* target = nullptr);
	static ListView* instance(QWidget* parent = nullptr);
	void moveItemToBox(const QString& filePath, LinBox* targetBox, bool refresh = true, int insertIndex = -1);
	void moveItemsToBox(const QVector<ListItem*>& items, LinBox* targetBox, int insertIndex = -1, bool refresh = true);
	QVector<ListItem*> orderedDraggedItemsForPaths(const QStringList& paths) const;
	bool importPathsToBox(const QStringList& sourcePaths, LinBox* targetBox, bool copyMode, int insertIndex = -1);
	QStringList resolveDraggedLocalPaths(const QMimeData* mimeData) const;
	bool handleDroppedPathsOnItem(const QStringList& sourcePaths, ListItem* targetItem, bool copyMode);
	void reloadMappedFolderBox(LinBox* box);
	void clearMappedFolderBox(LinBox* box);
	bool isItemInAnyBox(const QString& filePath) const;
	QString getBoxIdForItem(const QString& filePath) const;
	void saveBoxes();
	void requestSaveBoxes(); // Move to public
	QString dumpAllItemsJson() const;
	QString dumpVisibleItemsJson() const;
	QString dumpHiddenItemsJson() const;
	void DisableSelecting() {
		endBackgroundSelection();
	}

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
	void setSelectedItems(const QVector<ListItem*>& items, bool expandSingle = false);
	void beginBackgroundSelection(const QPoint& startPos);
	bool updateBackgroundSelection(const QPoint& currentPos, QRect* selectionRect = nullptr);
	void endBackgroundSelection();
	bool isBackgroundSelectionActive() const { return m_isSelecting; }
	QRect backgroundSelectionRect() const { return m_selectionRect; }
	/// <summary>
	/// 
	/// </summary>
	/// <param name="path"></param>
	/// <returns></returns>
	ListItem* selectItemByPath(const QString path);
	void clearCheckedItem();
    void rebuildSpatialIndex();
    QVector<ListItem*> queryItemsInRect(const QRect& rect) const;

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
	
public slots:
	void onMenuDelete(bool permanently = false);
	void onMenuRefresh();
	void onMenuCreateBox();
	void onMenuMapFolder();
	void onMenuQuitApp();
	void onMenuLinDesk();
    void resetRenameClickState();
    void armPendingShellNewTargetBox(LinBox* box);
    void clearPendingShellNewTargetBox();

public slots:
	void performSort(SortOrderType order);
	void onMenuPaste();
	void onMenuPasteShortcut();
	void onItemRelayClicked(LinBox* box, ListItem* item, QMouseEvent* event);
	void onItemRelayMoved(LinBox* box, ListItem* item, QMouseEvent* event);
	void onItemRelayReleased(LinBox* box, ListItem* item, QMouseEvent* event);
	void onItemRelayDoubleClicked(LinBox* box, ListItem* item, QMouseEvent* event);
	void onItemRelayContextMenuRequested(LinBox* box, ListItem* item, const QPoint& globalPos);
	void onMenuDestroyBox(LinBox* box);
	void onMenuCustomBox(LinBox* box);

protected:
	void paintEvent(QPaintEvent* event) override;  // 重写绘制事件来绘制背景
	void contextMenuEvent(QContextMenuEvent* event) override; //右键菜单事件
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
	void enterEvent(QEvent* event) override;

    // Platform-specific event handling (for Global Hotkeys)
    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;
    void handleHotkey(int id);
	
	void createDragGhosts(const QPoint& clickPos);
    void setupItemConnections(ListItem* item, LinBox* box = nullptr);
    void applyBoxAssignmentsFromConfig(const QVector<QMap<QString, QVariant>>& boxData);
    void applySavedPositionsToItems(const QMap<QString, QMap<QString, QVariant>>& iconPositions);
    QStringList mappedFolderEntryPaths(const QString& folderPath) const;
    void loadMappedFolderItemsForBox(LinBox* box);
    void clearMappedFolderItemsForBox(LinBox* box);
	QPoint resolveCreatePosition(const QPoint& requestedPos) const;
	void reserveCreatedItemPosition(const QString& filePath, const QPoint& requestedPos);
	bool takeReservedCreatedItemPosition(const QString& filePath, QPoint& posOut);
	
	void loadBoxes(bool autoUpdateState = true);
	void createBoxFromSavedData(const QString& title, const QRect& geom, bool collapsed, int origH, int showType, const QStringList& files, const QColor& color, float opacity, bool glass, const QString& explicitId = "", const QString& mappedFolderPath = "", bool refresh = true);
    bool handlePendingShellCreatedItem(const QString& newPath);

private:
	explicit ListView(QWidget* parent = nullptr);
	~ListView();
	
	// 初始化右键菜单信号连接
	void initRightMenuConnections();
	void syncViewModeSizes();

	static ListView* m_instance;	//图标视图
	RightMenu* rightMenu;
	IconShowRule m_iconShowRule;	// icon show rule
	QString m_persistencePath;
	QPointer<SettingUI> m_settingsWindow;
	bool m_enablePersistence = true;
    bool m_isLoadingPersistence = false;
    bool m_forceConfigLayout = false;
    bool m_hasLoadedConfigLayout = false;
	QMap<QString, PersistentIconInfo> m_savedIconPositions;
    std::unique_ptr<DesktopQuadTree> m_spatialIndex;

	// 网格布局变量（可随 ViewMode 动态改变）
    QString m_desktopDir; // 存储桌面物理路径
	int m_iconWidth   = kMediumIconW;
	int m_iconHeight  = kMediumIconH;
	int m_rowSpacing  = kMediumSpacing;
	int m_colSpacing  = kMediumSpacing;
	int m_bottomPadding = 32; // 自定义底部缓冲高度
	const int m_marginTop  = 6;
	const int m_marginLeft = 2;

	QVector<ListItem*> m_allItems;             // 存储所有 ListItem 对象
	QVector<ListItem*> m_items;             // 存储当前桌面渲染体系维护的文件对象（含 Box 内对象）
	QVector<ListItem*> m_hiddenIcons;             // 存储桌面系统图标状态
    QMap<QString, QStringList> m_mappedBoxFilePaths; // boxId -> direct child paths of mapped folder
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
    
    // Rename timing
    QElapsedTimer m_clickTimer;
    ListItem* m_lastClickedItem = nullptr;
    bool m_wasSelectedBeforeClick = false;

	QPoint m_selectionStart;     // 鼠标点击开始时的位置
	QRect m_selectionRect;
	ListItem* m_draggedItem = nullptr; // 当前被拖动的 ListItem
	QMap<QString, QPoint> m_pendingCreatedItemPositions;
	QHash<QString, QPointer<LinBox>> m_pendingCreatedItemBoxes;
	QHash<QString, int> m_pendingCreatedItemInsertIndices;
    QPointer<LinBox> m_pendingShellNewTargetBox;
    QTimer* m_pendingShellNewTargetTimer = nullptr;
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
    QLabel* m_dragTip = nullptr; // 用于显示"使用某程序打开"的提示

	void ensureDragTip();
};
