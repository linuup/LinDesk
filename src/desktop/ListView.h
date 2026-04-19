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
class FluentInfoTip;
class ListViewSelectionController;
class ListViewMappedBoxHelper;

class ListView : public QWidget {
	Q_OBJECT

public:
	// 枚举定义
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

	enum class ViewMode {
		LargeIcon,
		MediumIcon,
		SmallIcon
	};

	enum class FocusMoveDirection {
		Left,
		Right,
		Up,
		Down
	};

	// 静态常量
	static constexpr int kLargeIconW = 100, kLargeIconH = 104;
	static constexpr int kMediumIconW = 76, kMediumIconH = 78;
	static constexpr int kSmallIconW = 62, kSmallIconH = 64;
	//
	static constexpr int kLargeRowSpacing = 12;
	static constexpr int kLargeColSpacing = 8;
	//
	static constexpr int kMediumRowSpacing = 8;
	static constexpr int kMediumColSpacing = 4;
	//
	static constexpr int kSmallRowSpacing = 6;
	static constexpr int kSmallColSpacing = 2;
	//arrow size
	static constexpr int kLargeArrowSize = 42;
	static constexpr int kMediumArrowSize = 32;
	static constexpr int kSmallArrowSize = 24;
	static constexpr int kListArrowSize = 20;

	// 结构体定义
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

	// 构造函数/析构函数/单例
	explicit ListView(QWidget* parent = nullptr);
	~ListView();
	static ListView* instance(QWidget* parent = nullptr);

	// 初始化相关
	void initialize();
	void loadIconLayer();
	void loadAutoLayout();
	void loadGridLayout();
	void loadPersistentLayout();
	void loadBoxes(bool autoUpdateState = true);
	void loadMappedFolderItemsForBox(LinBox* box);
	void startListeningSpecialIcon();
	void initRightMenuConnections();
	void syncViewModeSizes();
	QPoint remapPositionForViewMetrics(const QPoint& pos, int oldIconWidth, int oldIconHeight, int oldRowSpacing, int oldColSpacing) const;
	void applyViewModeMetricsChange(int oldIconWidth, int oldIconHeight, int oldRowSpacing, int oldColSpacing);

	// 配置加载
	void applyBoxAssignmentsFromConfig(const QVector<QMap<QString, QVariant>>& boxData);
	void applySavedPositionsToItems(const QMap<QString, QMap<QString, QVariant>>& iconPositions);

	// Item管理
	QVector<ListItem*> items() const { return m_items; }
	QVector<ListItem*> items() { return m_items; }
	void addItem(ListItem* item, bool refresh = true);
	void addItemNoDelete(ListItem* item);
	void removeItem(ListItem* item);
	void removeItemNoDelete(ListItem* item);
	ListItem* findItemByPath(const QString& filePath) const;
	void setupItemConnections(ListItem* item, LinBox* box = nullptr);
	void clearMappedFolderItemsForBox(LinBox* box);

	// 排序相关
	void performSort(SortOrderType order);
	void sortItems(SortOrderType order);
	SortOrderType getOppositeDirection();
	void sortByName(bool ascending = true);
	void sortBySize(bool ascending = true);
	void sortByType(bool ascending = true);
	void sortByDate(bool ascending = true);
	void sortByCategorized();

	// 选择相关
	QVector<ListItem*> selectedItems() const { return m_selectedItems; }
	void setSelectedItems(const QVector<ListItem*>& items, bool expandSingle = false);
	void toggleItemSelection(ListItem* item);
	void selectRangeToItem(ListItem* item, LinBox* box);
	void clearCheckedItem();
	void clearSelectionState();
	void applySelectionState(const QVector<ListItem*>& items, ListItem* focusedItem = nullptr, bool expandSingle = false);
	ListItem* selectItemByPath(const QString path);
	QVector<ListItem*> queryItemsInRect(const QRect& rect) const;
	bool IsSelecting() const { return m_isSelecting; }
	void DisableSelecting() { endBackgroundSelection(); }

	// 焦点相关
	ListItem* focusedItem() const { return m_focusedItem; }
	void setFocusedItem(ListItem* item);
	void moveFocusInHost(LinBox* box, FocusMoveDirection direction, bool extendSelection, bool focusOnly);
	QString focusedItemPath() const;
	QString anchorItemPath() const;

	// 拖放相关
	QVector<ListItem*> orderedDraggedItemsForPaths(const QStringList& paths) const;
	void createDragGhosts(const QPoint& clickPos);
	bool shouldCreateShortcutForExternalDrop(const QStringList& sourcePaths) const;
	bool createShortcutsInFolder(const QStringList& sourcePaths, const QString& targetFolder) const;
	QStringList resolveDraggedLocalPaths(const QMimeData* mimeData) const;
	bool handleDroppedPathsOnItem(const QStringList& sourcePaths, ListItem* targetItem, bool copyMode, bool preferShortcut = false);

	// Box管理
	QVector<LinBox*> boxes() const;
	void moveItemToBox(const QString& filePath, LinBox* targetBox, bool refresh = true, int insertIndex = -1);
	void moveItemsToBox(const QVector<ListItem*>& items, LinBox* targetBox, int insertIndex = -1, bool refresh = true);
	bool importPathsToBox(const QStringList& sourcePaths, LinBox* targetBox, bool copyMode, int insertIndex = -1, bool preferShortcut = false);
	void reloadMappedFolderBox(LinBox* box);
	void clearMappedFolderBox(LinBox* box);
	void saveBoxes();
	void requestSaveBoxes();
	void createBoxFromSavedData(const QString& title, const QRect& geom, bool collapsed, int origH, int showType, const QStringList& files, const QColor& color, float opacity, bool glass, const QString& explicitId = "", const QString& mappedFolderPath = "", bool refresh = true);

	// 路径映射
	bool isItemInAnyBox(const QString& filePath) const;
	QString getBoxIdForItem(const QString& filePath) const;
	LinBox* findMappedFolderBox(const QString& folderPath, const LinBox* excludeBox = nullptr) const;
	QStringList mappedFolderEntryPaths(const QString& folderPath) const;

	// 位置管理
	QPoint generateItemPosition();
	QPoint resolveCreatePosition(const QPoint& requestedPos) const;
	void reserveCreatedItemPosition(const QString& filePath, const QPoint& requestedPos);
	bool takeReservedCreatedItemPosition(const QString& filePath, QPoint& posOut);

	// 剪贴板操作
	void writeSelectionToClipboard(bool cutOperation);
	void clearCutMarks();
	void applyCutMarks(const QVector<ListItem*>& items);

	// 右键菜单
	void requestSystemRightMenu(ObjectType objType, QPoint point, const QVector<ListItem*> checkedItems);
	void updateRightMenuState();

	// 状态管理
	void restoreSelectionStateByPaths(const QStringList& selectedPaths, const QString& focusedPath, const QString& anchorPath, bool expandSingle = false);

	// 杂项功能
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
	QVector<ListItem*> selectableItemsForHost(LinBox* box) const;
	QStringList selectedItemPaths() const;
	void rebuildSpatialIndex();
	void refreshCheckedItem(QRect rect);
	void showSettings(LinBox* target = nullptr);
	void showTransientTip(const QString& message, int autoHideMs = 2500);

	// 调试工具
	QString dumpAllItemsJson() const;
	QString dumpVisibleItemsJson() const;
	QString dumpHiddenItemsJson() const;

	// 拖拽选择
	void beginBackgroundSelection(const QPoint& startPos);
	bool updateBackgroundSelection(const QPoint& currentPos, QRect* selectionRect = nullptr);
	void endBackgroundSelection();
	bool isBackgroundSelectionActive() const { return m_isSelecting; }
	QRect backgroundSelectionRect() const { return m_selectionRect; }

	// 事件处理
protected:
	void paintEvent(QPaintEvent* event) override;
	void contextMenuEvent(QContextMenuEvent* event) override;
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
	bool nativeEvent(const QByteArray& eventType, void* message, long* result) override;
	void handleHotkey(int id);

private:
	// 工具函数
	void ensureDragTip();
	void ensureInfoTip();
	bool handlePendingShellCreatedItem(const QString& newPath);

public slots:
	// 目录监控
	void onDirectoryChanged(const QString& path) { qDebug() << "[DirectoryMonitor]-Directory Changed:" << path; }
	void onFileSystemChanged(const FileSystemEventInfo& eventInfo);
	void onError(const QString& error) { qDebug() << "Error DirectoryMonitor:" << error; }

	// 右键菜单
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
	void onMenuDelete(bool permanently = false);
	void onMenuRefresh();
	void onMenuCreateBox();
	void onMenuMapFolder();
	void onMenuQuitApp();
	void onMenuLinDesk();
	void onMenuPaste();
	void onMenuPasteShortcut();
	void onMenuDestroyBox(LinBox* box);
	void onMenuCustomBox(LinBox* box);

	// Item代理
	void onItemRelayClicked(LinBox* box, ListItem* item, QMouseEvent* event);
	void onItemRelayMoved(LinBox* box, ListItem* item, QMouseEvent* event);
	void onItemRelayReleased(LinBox* box, ListItem* item, QMouseEvent* event);
	void onItemRelayDoubleClicked(LinBox* box, ListItem* item, QMouseEvent* event);
	void onItemRelayContextMenuRequested(LinBox* box, ListItem* item, const QPoint& globalPos);

	// 系统图标
	void updateSystemIconVisibility(const QString& clsid, bool hidden);
	void resetRenameClickState();
	void armPendingShellNewTargetBox(LinBox* box);
	void clearPendingShellNewTargetBox();

private:
	// 友元声明
	friend class ListViewSelectionController;
	friend class ListViewMappedBoxHelper;

	struct GhostInfo {
		QLabel* widget;
		ListItem* item;
		QPoint offset;
	};

private:
	// 静态成员
	static ListView* m_instance;

	// UI组件
	RightMenu* rightMenu;
	QPointer<SettingUI> m_settingsWindow;
	QLabel* m_dragTip = nullptr;
	FluentInfoTip* m_infoTip = nullptr;

	// 配置和状态
	IconShowRule m_iconShowRule;
	QString m_persistencePath;
	QString m_desktopDir;
	bool m_enablePersistence = true;
	bool m_isLoadingPersistence = false;
	bool m_forceConfigLayout = false;
	bool m_hasLoadedConfigLayout = false;
	int m_iconWidth = kMediumIconW;
	int m_iconHeight = kMediumIconH;
	int m_rowSpacing = kMediumRowSpacing;
	int m_colSpacing = kMediumColSpacing;

	int m_bottomPadding = 26;
	const int m_marginTop = 6;
	const int m_marginLeft = 2;

	// 数据存储
	QVector<ListItem*> m_allItems;
	QVector<ListItem*> m_items;
	QVector<ListItem*> m_hiddenIcons;
	QList<DesktopInfoMonitor::DesktopIcon> sysIconRegisterInfo;
	QVector<ListItem*> m_selectedItems;
	QVector<GhostInfo> m_ghosts;

	// 映射和索引
	QMap<QString, QStringList> m_mappedBoxFilePaths;
	QMap<QString, PersistentIconInfo> m_savedIconPositions;
	QMap<QString, QPoint> m_pendingCreatedItemPositions;
	QHash<QString, QPointer<LinBox>> m_pendingCreatedItemBoxes;
	QHash<QString, int> m_pendingCreatedItemInsertIndices;
	std::unique_ptr<DesktopQuadTree> m_spatialIndex;

	// 指针和引用
	QPointer<ListItem> m_focusedItem;
	QPointer<ListItem> m_anchorItem;
	QPointer<LinBox> m_pendingShellNewTargetBox;
	ListItem* m_lastClickedItem = nullptr;
	ListItem* m_draggedItem = nullptr;
	DesktopInfoMonitor* monitor;
	LinDirectoryMonitor* m_linMonitor;

	// 计时器
	QTimer* m_pendingShellNewTargetTimer = nullptr;
	QElapsedTimer m_clickTimer;

	// 状态标志
	bool m_isMultiSelection = false;
	bool m_isDragging = false;
	bool m_isSelecting = false;
	bool m_wasSelectedBeforeClick = false;

	// 坐标和区域
	QPoint m_dragOffset;
	QPoint m_originalPos;
	QPoint m_lastRightClickPos;
	QPoint m_startPos;
	QPoint m_selectionStart;
	QRect m_selectionRect;
};
