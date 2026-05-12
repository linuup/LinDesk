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
#include <QHash>
#include <QSet>
#include <QRegion>
#include <memory>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include "desktop/DesktopRootItemState.h"
#include <windows.h>
#include <shlobj_core.h>
#include "services/DesktopItemLoader.h"
#include "services/LayoutPersistenceService.h"
#include "desktop/RightMenu.h"
#include "desktop/RenameManager.h"
#include "monitor/DesktopInfoMonitor.h"
#include "monitor/DirectoryMonitor.h"
#include "monitor/LinDirectoryMonitor.h"
#include "monitor/WindowsConfigMonitor.h"
#include "desktop/SelectArea.h"
#include "ui/SettingUI.h"
class LinBox;
class BoxManager;
class DesktopQuadTree;
class FluentInfoTip;
class ListViewSelectionController;
class ListViewMappedBoxHelper;
class ListViewDebugPanel;

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
		SortByCustom,
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
	static constexpr int kLargeRowSpacing = 14;
	static constexpr int kLargeColSpacing = 10;
	//
	static constexpr int kMediumRowSpacing = 6;
	static constexpr int kMediumColSpacing = 4;
	//
	static constexpr int kSmallRowSpacing = 4;
	static constexpr int kSmallColSpacing = 2;
	//arrow size
	static constexpr int kLargeArrowSize = 42;
	static constexpr int kMediumArrowSize = 32;
	static constexpr int kSmallArrowSize = 24;
	static constexpr int kListArrowSize = 20;
	static constexpr int kBaseBottomPadding = 5;
	static constexpr int kBaseMarginTop = 4;
	static constexpr int kBaseMarginLeft = 2;

	// 结构体定义
	struct IconShowRule {
		IconPosMode iconPosMode = IconPosMode::AlignIconToGrid; //图标位置模式，默认与网格对齐
		ViewMode viewMode = ViewMode::MediumIcon;	//图标显示模式，默认中等图标
		SortOrderType sortType = SortOrderType::Default;	// 默认使用"默认排序"
		SortOrderType sortTypeBeforeAutoArrange = SortOrderType::Default;
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
	void applyAutoArrangeLayout(bool persist = true, bool refreshSelection = true);
	void loadAutoLayout();
	void loadGridLayout();
	void loadPersistentLayout();
	void loadBoxes(bool autoUpdateState = true);
	void loadMappedFolderItemsForBox(LinBox* box);
	void startListeningSpecialIcon();
	void initRightMenuConnections();
	void syncViewModeSizes();
	QPoint remapPositionForViewMetrics(const QPoint& pos,
		int oldIconWidth,
		int oldIconHeight,
		int oldRowSpacing,
		int oldColSpacing,
		int oldMarginTop,
		int oldMarginLeft) const;
	void applyViewModeMetricsChange(int oldIconWidth,
		int oldIconHeight,
		int oldRowSpacing,
		int oldColSpacing,
		int oldMarginTop,
		int oldMarginLeft);
	void refreshForDpiChange();
	qreal currentDpiScale() const;
	int scalePx(int logicalPx) const;

	// 渲染管理
	void updateAllItemRenderData();
	DesktopRenderParams currentRenderParams() const;


	// 配置加载
	void applyBoxAssignmentsFromConfig(const QVector<QMap<QString, QVariant>>& boxData);
	void applySavedPositionsToItems(const QMap<QString, QMap<QString, QVariant>>& iconPositions);

	// Item管理
	bool addItem(const QString& filePath, bool refresh = true);
	bool addItemNoDelete(const QString& filePath);
	void removeItem(const QString& filePath);
	void removeItemNoDelete(const QString& filePath);
	DesktopRootItemState* findItemByPath(const QString& filePath);
	const DesktopRootItemState* findItemByPath(const QString& filePath) const;
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
	void sortByCustom();

	// 选择相关
	void setSelectedItemPaths(const QStringList& filePaths, const QString& focusedPath = QString(), bool expandSingle = false);
	void toggleItemSelection(const QString& filePath);
	void selectRangeToPath(const QString& filePath, LinBox* box);
	void clearCheckedItem();
	void clearSelectionState();
	QVector<DesktopRootItemState> queryItemsInRect(const QRect& rect) const;
	bool IsSelecting() const { return m_isSelecting; }
	void DisableSelecting() { endBackgroundSelection(); }
	void setItemFullNameExpanded(const QString& filePath, bool expanded);
	void setItemExpandedPreviewText(const QString& filePath, const QString& text);
	void clearItemExpandedState(const QString& filePath);
	void setItemRenaming(const QString& filePath, bool renaming);

	// 焦点相关
	void setFocusedItemPath(const QString& filePath);
	void moveFocusInHost(LinBox* box, FocusMoveDirection direction, bool extendSelection, bool focusOnly);
	QString focusedItemPath() const;
	QString currentSpaceLookHoveredPath() const;
	QString anchorItemPath() const;
	QString draggedItemPath() const;
	void setDraggedItemPath(const QString& filePath, bool fromBox);
	void clearDraggedItemPath(bool fromBoxOnly = false);

	// 拖放相关
	QStringList orderedDraggedPaths(const QStringList& paths) const;
	void createDragGhosts(const QPoint& clickPos);
	bool shouldCreateShortcutForExternalDrop(const QStringList& sourcePaths) const;
	bool shouldPreferShortcutForDrop(const QStringList& sourcePaths, Qt::KeyboardModifiers modifiers, bool isExternalSource) const;
	bool createShortcutsInFolder(const QStringList& sourcePaths, const QString& targetFolder) const;
	QStringList resolveDraggedLocalPaths(const QMimeData* mimeData) const;
	struct DropInteractionOptions {
		bool copyMode = false;
		bool preferShortcut = false;
		bool preHideDesktopSources = false;
		bool finalizeMovedSourcesAfterSuccess = false;
	};
	bool handleDroppedPathsOnItemTarget(const QStringList& sourcePaths,
	                                    const DesktopRootItemModel& targetModel,
	                                    const DropInteractionOptions& options);
	bool handleDroppedPathsOnDesktopTarget(const QStringList& sourcePaths,
	                                       const DesktopRootItemModel& targetModel,
	                                       bool copyMode,
	                                       bool preferShortcut,
	                                       bool preHideDesktopSources);
	bool finalizeMovedModelSourcesAfterFileOp(const QStringList& sourcePaths);
	bool handleDroppedPathsOnModel(const QStringList& sourcePaths, const DesktopRootItemModel& targetModel, bool copyMode, bool preferShortcut = false);
	bool resolveItemModelByPath(const QString& filePath, DesktopRootItemModel& modelOut) const;
	QRect globalRenameEditorRectForPath(const QString& filePath) const;
	bool sendPreviewRequestByPath(const QString& filePath);
	bool openItemByPath(const QString& filePath);
	bool showPropertiesForPath(const QString& filePath) const;

	// Box管理
	QVector<LinBox*> boxes() const;
	void moveItemToBox(const QString& filePath, LinBox* targetBox, bool refresh = true, int insertIndex = -1);
	void moveItemsToBox(const QStringList& filePaths, LinBox* targetBox, int insertIndex = -1, bool refresh = true);
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
	void applyCutMarks(const QStringList& filePaths);
	bool createNewFolderInHost(LinBox* targetBox = nullptr);

	// 右键菜单
	void requestSystemRightMenu(ObjectType objType, QPoint point, const QStringList& checkedPaths = {});
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
	QStringList selectedItemPaths() const;
	void rebuildSpatialIndex();
	void refreshCheckedItem(QRect rect);
	void showSettings(LinBox* target = nullptr);
	void showTransientTip(const QString& message, int autoHideMs = 2500);
	void showDebugPanel();

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
	void leaveEvent(QEvent* event) override;
	bool nativeEvent(const QByteArray& eventType, void* message, long* result) override;
	void handleHotkey(int id);

private:
	// 工具函数
	void ensureDragTip();
	void ensureInfoTip();
	QRect desktopLayoutBounds() const;
	int desktopLayoutLeft() const;
	int desktopLayoutTop() const;
	int desktopUsableRight() const;
	int desktopUsableBottom() const;
	int desktopItemFootprintHeight() const;
	QVector<DesktopRootItemState*> collectGridOrderedDesktopStates() const;
	bool applyGridAlignedDesktopDropLayout(const QStringList& draggedPaths, const QPoint& releasePos);
	QVector<DesktopRootItemState*> collectAutoArrangeStates() const;
	void sortAutoArrangeStates(QVector<DesktopRootItemState*>& states, SortOrderType order) const;
	void assignCustomSortOrders(const QVector<DesktopRootItemState*>& orderedStates);
	void clearDesktopCustomSortOrders();
	int desktopCustomInsertionIndexForPos(const QPoint& pos, const QStringList& draggedPaths) const;
	void updateDesktopAutoArrangeReorderPreview(const QPoint& pos, const QStringList& draggedPaths);
	void clearDesktopAutoArrangeReorderPreview();
	QRect desktopAutoArrangeInsertionIndicatorRect() const;
	QRect desktopAutoArrangeInsertionIndicatorRectForIndex(int insertIndex, const QVector<DesktopRootItemState*>& orderedStates) const;
	QVector<QPoint> layoutAutoArrangeItems(const QVector<DesktopRootItemState*>& states) const;
	void finalizeAutoArrangeLayout(const QVector<DesktopRootItemState*>& states,
		const QVector<QPoint>& positions,
		bool persist,
		bool refreshSelection);
	void applyAutoArrangeAfterDesktopMutation(bool persist = true, bool refreshSelection = true);
	bool fileHasHiddenAttribute(const QString& filePath) const;
	void applyWindowsHiddenVisualState(DesktopRootItemModel& model);
	void setSelectedItems(const QStringList& filePaths, bool expandSingle = false);
	void applySelectionState(const QStringList& filePaths, const QString& focusedPath = QString(), bool expandSingle = false);
	void clearFocusedItem();
	bool shouldShowFileSystemItem(const DesktopRootItemModel& model) const;
	void refreshVisibilityForWindowsConfig();
	bool handlePendingShellCreatedItem(const QString& newPath);
	DesktopItemInitContext currentDesktopItemInitContext() const;
	bool addDesktopModelItemFromPath(const QString& filePath);
	bool refreshDesktopModelItemPath(const QString& oldPath, const QString& newPath);
	void invalidateFolderItemIconCache(const QString& folderPath);
	bool refreshBoxModelItemPath(LinBox* box, const QString& oldPath, const QString& newPath);
	bool refreshFolderItemByPath(const QString& folderPath);
	void removeItemsBatch(const QStringList& paths, bool refresh = true);
	void setItemsTransientHidden(const QStringList& paths, bool hidden);
	void trackPendingExternalRemovals(const QStringList& paths);
	void removeItemPathsBatch(const QStringList& paths, bool refresh = true);
	void setItemPathsTransientHidden(const QStringList& paths, bool hidden);
	void trackPendingExternalRemovalPaths(const QStringList& paths);
	bool consumePendingExternalRemoval(const QString& path);
	DesktopRootItemState* findAnyItemStateByPath(const QString& filePath);
	const DesktopRootItemState* findAnyItemStateByPath(const QString& filePath) const;
	DesktopRootItemState* findDesktopStateAt(const QPoint& pos);
	const DesktopRootItemState* findDesktopStateAt(const QPoint& pos) const;
	DesktopRootItemState* findDesktopStateByPath(const QString& filePath);
	const DesktopRootItemState* findDesktopStateByPath(const QString& filePath) const;
	void markDesktopStateIndexDirty();
	void rebuildDesktopStateIndex();
	bool shouldPaintDesktopModelState(const DesktopRootItemState& state) const;
	QString normalizeDesktopItemPath(const QString& filePath) const;
	bool isDesktopPathSelected(const QString& filePath) const;
	QVector<DesktopRootItemModel> selectedDesktopModels() const;
	QStringList selectedDesktopPaths() const;
	QStringList selectedBoxPaths() const;
	QString effectiveFocusedDesktopPath() const;
	QString effectiveAnchorDesktopPath() const;
	QString effectiveFocusedBoxPath() const;
	QString effectiveAnchorBoxPath() const;
	QStringList selectableDesktopPaths() const;
	QStringList selectableBoxPaths(LinBox* box) const;
	QString findDirectionalDesktopFocusCandidate(const QString& focusedPath, FocusMoveDirection direction) const;
	QString findDirectionalBoxFocusCandidate(LinBox* box, const QString& focusedPath, FocusMoveDirection direction) const;
	void setDesktopItemSelected(DesktopRootItemState* state, const QString& normalizedPath, bool selected, QRegion* dirtyRegion = nullptr);
	void setDesktopItemSelected(const QString& filePath, bool selected, QRegion* dirtyRegion = nullptr);
	void clearDesktopSelection(QRegion* dirtyRegion = nullptr);
	void applyDesktopSelection(const QStringList& filePaths, const QString& focusedPath, bool expandSingle);
	DesktopRootItemState* findDesktopItemAt(const QPoint& pos);
	const DesktopRootItemState* findDesktopItemAt(const QPoint& pos) const;
	QRect desktopSelectionRectForItem(const DesktopRootItemState& state) const;
	void updateHoveredDesktopItem(const QPoint& pos);
	void clearHoveredDesktopItem();
	void removeDesktopStateByPath(const QString& filePath);
	void markDesktopRenderDataDirty(DesktopRootItemState* state);
	void markAllDesktopRenderDataDirty();
	void markSpatialIndexDirty();
	void rebuildBackgroundSelectionOcclusionRegion();
	void resetBackgroundSelectionCandidateCache();
	void updateBackgroundSelectionCandidateCache(const QRegion& uncoveredSelectionRegion);
	void flushBackgroundSelectionUpdate();
	int currentBackgroundSelectionThrottleMs() const;
	void updateBackgroundSelectionThrottle(qint64 refreshElapsedMs, const QRect& selectionRect);
	void finalizeActiveDrag(LinBox* box, QMouseEvent* event);
	void updateGhostDragHoverPreview(const QPoint& globalPos);
	void clearDestroyedBoxRuntimeState(LinBox* box);

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
	void onMenuOrganizeDesktop();
	void onMenuOrganizeDesktopByNameSort();
	void onMenuQuitApp();
	void onMenuLinDesk();
	void onMenuPaste();
	void onMenuPasteShortcut();
	void onMenuDestroyBox(LinBox* box);
	void onMenuCustomBox(LinBox* box);

	// Item代理

	// 系统图标
	void updateSystemIconVisibility(const QString& clsid, bool hidden);
	void onWindowsShowHiddenFilesChanged(bool showHiddenFiles);
	void resetRenameClickState();
	void armPendingShellNewTargetBox(LinBox* box);
	void clearPendingShellNewTargetBox();

private:
	// 友元声明
	friend class ListViewMappedBoxHelper;

	struct GhostInfo {
		QLabel* widget;
		QString filePath;
		QPoint offset;
	};

private:
	// 静态成员
	static ListView* m_instance;

	// UI组件
	RightMenu* rightMenu;
	QPointer<SettingUI> m_settingsWindow;
	QPointer<ListViewDebugPanel> m_debugPanel;
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

	int m_bottomPadding = kBaseBottomPadding;
	int m_marginTop = kBaseMarginTop;
	int m_marginLeft = kBaseMarginLeft;

	// 数据存储
	QVector<DesktopRootItemState> m_itemStates;
	QList<DesktopInfoMonitor::DesktopIcon> sysIconRegisterInfo;
	QVector<QString> m_selectedDesktopPaths;
	QSet<QString> m_selectedDesktopPathSet;
	QVector<QString> m_selectedBoxPaths;
	QVector<GhostInfo> m_ghosts;
	QStringList m_desktopReorderDraggedPaths;
	QRect m_desktopReorderIndicatorRect;
	int m_desktopReorderInsertIndex = -1;

	// 映射和索引
	QMap<QString, QStringList> m_mappedBoxFilePaths;
	QHash<QString, quint64> m_mappedBoxLoadRequestIds;
	QMap<QString, PersistentIconInfo> m_savedIconPositions;
	QMap<QString, QPoint> m_pendingCreatedItemPositions;
	QSet<QString> m_pendingExternalRemovalPaths;
	QHash<QString, QPointer<LinBox>> m_pendingCreatedItemBoxes;
	QHash<QString, int> m_pendingCreatedItemInsertIndices;
	std::unique_ptr<DesktopQuadTree> m_spatialIndex;
	QHash<QString, DesktopRootItemState*> m_desktopStateIndex;
	QRect m_spatialIndexBounds;
	bool m_spatialIndexDirty = true;
	bool m_desktopStateIndexDirty = true;

	// 指针和引用
	QString m_hoveredDesktopPath;
	QString m_focusedDesktopPath;
	QString m_anchorDesktopPath;
	QString m_focusedBoxPath;
	QString m_anchorBoxPath;
	QPointer<LinBox> m_pendingShellNewTargetBox;
	QString m_lastClickedDesktopPath;
	QString m_lastClickedBoxPath;
	QString m_draggedDesktopPath;
	QString m_draggedBoxPath;
	DesktopInfoMonitor* monitor;
	LinDirectoryMonitor* m_linMonitor;
	WindowsConfigMonitor* m_windowsConfigMonitor = nullptr;

	// 计时器
	QTimer* m_pendingShellNewTargetTimer = nullptr;
	QTimer* m_backgroundSelectionThrottleTimer = nullptr;
	QElapsedTimer m_clickTimer;
	QElapsedTimer m_backgroundSelectionFrameTimer;

	// 状态标志
	bool m_isMultiSelection = false;
	bool m_isDragging = false;
	bool m_isSelecting = false;
	bool m_wasSelectedBeforeClick = false;
	bool m_showHiddenFiles = false;

	// 坐标和区域
	QPoint m_dragOffset;
	QPoint m_originalPos;
	QPoint m_lastRightClickPos;
	QPoint m_startPos;
	QPoint m_selectionStart;
	QRect m_selectionRect;
	QPoint m_pendingBackgroundSelectionPos;
	bool m_hasPendingBackgroundSelectionUpdate = false;
	QRegion m_backgroundSelectionOcclusionRegion;
	QRegion m_backgroundSelectionPreviousUncoveredRegion;
	QVector<DesktopRootItemState*> m_backgroundSelectionCandidateStates;
	QVector<DesktopRootItemState*> m_backgroundSelectionCandidateScratchStates;
	QVector<DesktopRootItemState*> m_backgroundSelectionNextSelectionStates;
	QVector<QString> m_backgroundSelectionPreviousSelectionPaths;
	QSet<DesktopRootItemState*> m_backgroundSelectionCandidateSet;
	QSet<QString> m_backgroundSelectionNextSelectionSet;
	int m_backgroundSelectionThrottleMs = 8;
	qreal m_backgroundSelectionRefreshCostMs = 0.0;
	int m_backgroundSelectionThrottleRelaxFrames = 0;
};
