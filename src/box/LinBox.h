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
#include "desktop/ListItem.h"
#include <QMimeData>
#include <QUuid>
#include <QPixmap>
#include <QScopedPointer>
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
    void addListItem(ListItem* item, bool refresh = true);
    void insertListItem(ListItem* item, int index, bool refresh = true);
    void removeListItem(ListItem* item, bool refresh = true);
    void setBoxTitle(const QString& title);
    void setResizeBorderWidth(int width);
    void toggleCollapse();
    int resizeBorderWidth() const { return m_borderWidth; }
    void refreshLayout();

    QString boxId() const { return m_id; }
    void setBoxId(const QString& id) { m_id = id; }

    QString boxTitle() const { return m_title; }
    BoxShowType showType() const { return m_showType; }
    bool isCollapsed() const { return m_isCollapsed; }
    int originalHeight() const { return m_originalHeight; }
    QVector<ListItem*> items() const { return m_items; }
    bool isFolderMapped() const { return !m_mappedFolderPath.isEmpty(); }
    QString mappedFolderPath() const { return m_mappedFolderPath; }
    MappedFolderState mappedFolderState() const { return m_mappedFolderState; }
    void setMappedFolderPath(const QString& path);
    void clearMappedFolderPath();
    void setMappedFolderMonitor(LinDirectoryMonitor* monitor);
    LinDirectoryMonitor* mappedFolderMonitor() const { return m_mappedFolderMonitor.data(); }

    void setBackgroundColor(const QColor& color);
    void setBackgroundOpacity(float opacity);
    QColor backgroundColor() const { return m_bgColor; }
    float backgroundOpacity() const { return m_bgOpacity; }
    
    bool glassEffect() const { return m_glassEffect; }
    void setGlassEffect(bool enabled);

    void refreshStyles();

signals:
    void itemRelayClicked(LinBox* box, ListItem* item, QMouseEvent* event);
    void itemRelayMoved(LinBox* box, ListItem* item, QMouseEvent* event);
    void itemRelayReleased(LinBox* box, ListItem* item, QMouseEvent* event);
    void itemRelayDoubleClicked(LinBox* box, ListItem* item, QMouseEvent* event);
    void itemRelayContextMenuRequested(LinBox* box, ListItem* item, const QPoint& globalPos);
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
    void showCustomContextMenu(const QPoint& globalPos);
    void beginTitleEditing();
    void commitTitleEditing();
    ListItem* hitItemAt(const QPoint& boxPos) const;
    ListItem* findItemByPath(const QString& filePath) const;
    int insertionIndexForPos(const QPoint& boxPos, const QVector<ListItem*>& draggedItems, bool* alignAfterPrev = nullptr) const;
    QRect insertionIndicatorRect() const;
    void clearReorderPreview();
    void showDropTipForItem(ListItem* item, const QPoint& boxPos, bool isCtrl, bool createShortcut = false);
    void scheduleBlurRefresh();
    void updateBlurCache();
    QPixmap captureBackgroundPixmap() const;
    void beginItemSelection(const QPoint& contentPos);
    void updateItemSelection(const QPoint& contentPos);
    void finishItemSelection();
    void activateForKeyboardFocus();
    QString mappedFolderDisplayText() const;
    QString sanitizedTitleText(const QString& title) const;

    BoxShowType m_showType = BoxShowType::Icon_ShowType;
    QVector<ListItem*> m_items;
    
    // UI Elements
    QWidget* m_titleBar;
    QString m_title;
    QLineEdit* m_titleEdit;
    QPushButton* m_viewSwitchBtn;
    QPushButton* m_collapseBtn;
    QScrollArea* m_scrollArea;
    QWidget* m_contentArea;
    QVBoxLayout* m_mainLayout;

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

    QColor m_bgColor = QColor(40, 40, 40); // Standard dark gray instead of pure black
    float m_bgOpacity = 0.45f;            // Default opacity
    bool m_glassEffect = false;
    QString m_id; 
    QString m_mappedFolderPath;
    MappedFolderState m_mappedFolderState = MappedFolderState::Normal;

private:
    class FluentDragTip* m_dragTip = nullptr;
    void updateDragTip(const QPoint& pos, bool isCtrl, bool createShortcut = false);
    class QTimer* m_blurRefreshTimer = nullptr;
    QPixmap m_blurCache;
    bool m_isCapturingBackground = false;
    bool m_isRefreshingLayout = false;
    bool m_isSelectingItems = false;
    QPoint m_selectionStartPos;
    SelectArea* m_selectionArea = nullptr;
    int m_lastLayoutContentWidth = -1;
    int m_lastLayoutItemCount = -1;
    BoxShowType m_lastLayoutShowType = BoxShowType::Icon_ShowType;
    quint64 m_lastLayoutVisualSignature = 0;
    QVector<ListItem*> m_reorderDragItems;
    int m_reorderInsertIndex = -1;
    bool m_reorderIndicatorAfterPrev = false;
    QScopedPointer<LinDirectoryMonitor> m_mappedFolderMonitor;
};

#endif // LINBOX_H
