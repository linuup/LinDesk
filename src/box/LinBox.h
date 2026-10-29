#ifndef LINBOX_H
#define LINBOX_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QVector>
#include <QAction>
#include <QMenu>
#include <QScrollArea>
#include <QScrollBar>
#include "desktop/SelectArea.h"
#include "desktop/DesktopRootItemState.h"
#include <QMimeData>
#include <QUuid>
#include <QPixmap>
#include <QScopedPointer>
#include <QPointer>
#include <QElapsedTimer>
#include <QtGlobal>

class LinDirectoryMonitor;

class LinBox : public QWidget {
    Q_OBJECT
public:
    enum class BoxShowType {
        Icon_ShowType,
        List_ShowType
    };

    enum class MappedFolderState {
        Normal,
        Missing,
        NoAccess
    };

    explicit LinBox(QWidget* parent = nullptr);
    ~LinBox();

    void setShowType(BoxShowType type);
    void addItemState(const DesktopRootItemState& state, bool refresh = true);
    void insertItemState(const DesktopRootItemState& state, int index, bool refresh = true);
    bool removeItemState(const QString& filePath, bool refresh = true);
    bool takeItemState(const QString& filePath, DesktopRootItemState& stateOut, bool refresh = true);
    void setBoxTitle(const QString& title);
    void setResizeBorderWidth(int width);
    void toggleCollapse();
    int resizeBorderWidth() const { return m_borderWidth; }
    void refreshLayout();
    QRect globalRenameEditorRectForPath(const QString& filePath) const;
    void previewGhostDropAt(const QPoint& boxPos, const QStringList& draggedPaths, Qt::KeyboardModifiers modifiers);
    void clearDropPreview();

    QString boxId() const { return m_id; }
    void setBoxId(const QString& id) { m_id = id; }

    QString boxTitle() const { return m_title; }
    BoxShowType showType() const { return m_showType; }
    bool isCollapsed() const { return m_isCollapsed; }
    int originalHeight() const { return m_originalHeight; }
    QVector<QString> itemPaths() const;
    const QVector<DesktopRootItemState>& itemStates() const { return m_itemStates; }
    bool containsItemPath(const QString& filePath) const;
    QString hoveredItemPath() const;
    DesktopRootItemState* findItemStateByPath(const QString& filePath);
    const DesktopRootItemState* findItemStateByPath(const QString& filePath) const;
    bool isFolderMapped() const { return !m_mappedFolderPath.isEmpty(); }
    QString mappedFolderPath() const { return m_mappedFolderPath; }
    MappedFolderState mappedFolderState() const { return m_mappedFolderState; }
    void setMappedFolderPath(const QString& path);
    void clearMappedFolderPath();
    void setMappedFolderMonitor(LinDirectoryMonitor* monitor);
    LinDirectoryMonitor* mappedFolderMonitor() const { return m_mappedFolderMonitor.data(); }
    void setMappedFolderLoading(bool loading);
    bool isMappedFolderLoading() const { return m_isMappedFolderLoading; }

    void setBackgroundColor(const QColor& color);
    void setBackgroundOpacity(float opacity);
    QColor backgroundColor() const { return m_bgColor; }
    float backgroundOpacity() const { return m_bgOpacity; }
    
    bool glassEffect() const { return m_glassEffect; }
    void setGlassEffect(bool enabled);
    void scheduleBlurRefresh();
    void updateBlurCache();

    void refreshStyles();
    void refreshSelectionOverlayColor();
    void loadSelectionColors();
    void setRefreshFlashing(bool flashing);
    bool isRefreshFlashing() const { return m_flashingForRefresh; }

signals:
    void geometryChanged(LinBox* box);
    void destroyRequested(LinBox* box);
    void customRequested(LinBox* box);

protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void enterEvent(QEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, long* result) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    enum ResizeDir {
        None = 0,
        Left = 0x1,
        Right = 0x2,
        Top = 0x4,
        Bottom = 0x8,
        TopLeft = Top | Left,
        TopRight = Top | Right,
        BottomLeft = Bottom | Left,
        BottomRight = Bottom | Right,
        Move = 0x10
    };

    void updateCursorShape(const QPoint& pos);
    ResizeDir getResizeDir(const QPoint& pos);
    bool isDragArea(const QPoint& pos);
    QString displayTitleText() const;
    void refreshMappedFolderState();
    void showCustomContextMenu(const QPoint& globalPos, bool extendedVerbs = false);
    void beginTitleEditing();
    void commitTitleEditing();
    DesktopRootItemState* hitItemStateAt(const QPoint& boxPos);
    const DesktopRootItemState* hitItemStateAt(const QPoint& boxPos) const;
    const DesktopRootItemState* dropTargetItemStateAt(const QPoint& boxPos) const;
    bool hasActiveItemRename() const;
    int insertionIndexForPos(const QPoint& boxPos, const QStringList& draggedPaths, bool* alignAfterPrev = nullptr) const;
    QRect insertionIndicatorRect() const;
    void clearReorderPreview();
    void showDropTipForItem(const DesktopRootItemState* state, const QPoint& boxPos, bool isCtrl, bool createShortcut = false);
    bool handleMappedBoxItemTargetDrop(const QStringList& sourcePaths,
                                       const DesktopRootItemState* targetState,
                                       bool copyMode,
                                       bool preferShortcut,
                                       QDropEvent* event);
    QPixmap captureBackgroundPixmap() const;
    void beginItemSelection(const QPoint& contentPos);
    void updateItemSelection(const QPoint& contentPos);
    void finishItemSelection();
    void clearSelectionOverlay();
    void activateForKeyboardFocus();
    void markItemRenderDataDirty(DesktopRootItemState* state);
    void markAllItemRenderDataDirty();
    bool hasDirtyItemRenderData() const;
    quint64 currentLayoutVisualSignature() const;
    void rebuildItemRenderData(DesktopRootItemState& state, const DesktopRenderParams& params);
    void updateHoveredItem(const QPoint& boxPos);
    void clearHoveredItem();
    bool handleItemMousePress(QMouseEvent* event, const QPoint& boxPos);
    bool handleItemMouseMove(QMouseEvent* event, const QPoint& boxPos);
    bool handleItemMouseRelease(QMouseEvent* event, const QPoint& boxPos);
    bool handleItemMouseDoubleClick(QMouseEvent* event, const QPoint& boxPos);
    bool handleItemContextMenu(const QPoint& globalPos, const QPoint& boxPos);
    bool startItemDrag(const QPoint& boxPos);
    QString mappedFolderDisplayText() const;
    QString sanitizedTitleText(const QString& title) const;
    QRect mappedTitleIconRect() const;
    QRect mappedTitleTextRect() const;
    bool openMappedFolderInExplorer();
    QRect mappedPlaceholderRect() const;
    bool tryRefreshMappedFolderFromPlaceholderClick(const QPoint& boxPos);
    bool remapMappedFolderInteractively();
    void checkMappedFolderStateAndRefresh();
    void updateDpiMetrics();

    BoxShowType m_showType = BoxShowType::Icon_ShowType;
    QVector<DesktopRootItemState> m_itemStates;
    
    // UI Elements
    QWidget* m_titleBar = nullptr;
    QString m_title;
    QLineEdit* m_titleEdit = nullptr;
    QPushButton* m_viewSwitchBtn = nullptr;
    QPushButton* m_collapseBtn = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_contentArea = nullptr;
    QVBoxLayout* m_mainLayout = nullptr;

    // Interaction State
    bool m_isResizing = false;
    bool m_isMoving = false;
    bool m_isCollapsed = false;
    int m_originalHeight = 300;
    ResizeDir m_resizeDir = None;
    QPoint m_dragStartPos;
    QRect m_dragStartGeometry;
    bool m_geometryChangedDuringInteraction = false;
    
    int m_borderWidth = 8;
    int m_titleHeight = 30;
    int m_minBoxWidth = 180;
    int m_minBoxHeight = 120;
    int m_maxCustomTitleLength = 64;

    QColor m_bgColor = QColor("#63A8F5"); // Unified default
    float m_bgOpacity = 0.28f;            // Unified default (28%)
    bool m_glassEffect = false;
    QString m_id; 
    QString m_mappedFolderPath;
    MappedFolderState m_mappedFolderState = MappedFolderState::Normal;
    qreal m_dpiScale = 1.0;
    bool m_isMappedFolderLoading = false;

private:
    QPointer<class FluentDragTip> m_dragTip;
    void updateDragTip(const QPoint& pos, bool isCtrl, bool createShortcut = false);
    class QTimer* m_blurRefreshTimer = nullptr;
    class QTimer* m_mappedFolderStateTimer = nullptr;
    class QTimer* m_moveRefreshTimer = nullptr;
    QPixmap m_blurCache;
    bool m_isCapturingBackground = false;
    bool m_isRefreshingLayout = false;
    bool m_flashingForRefresh = false;
    bool m_isSelectingItems = false;
    QPoint m_selectionStartPos;
    QRect m_selectionRect;
    QColor m_selectionFillColor;
    QColor m_selectionBorderColor;
    QString m_hoveredItemPath;
    QString m_pressedItemPath;
    QPoint m_pressedItemBoxPos;
    bool m_itemPressArmed = false;
    QElapsedTimer m_itemClickTimer;
    QPointer<SelectArea> m_selectionArea;
    int m_lastLayoutContentWidth = -1;
    int m_lastLayoutItemCount = -1;
    BoxShowType m_lastLayoutShowType = BoxShowType::Icon_ShowType;
    quint64 m_lastLayoutVisualSignature = 0;
    QStringList m_reorderDragPaths;
    int m_reorderInsertIndex = -1;
    bool m_reorderIndicatorAfterPrev = false;
    QScopedPointer<LinDirectoryMonitor> m_mappedFolderMonitor;
};

#endif // LINBOX_H
