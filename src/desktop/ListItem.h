#ifndef LISTITEM_H
#define LISTITEM_H

#include <QWidget>
#include <QPainter>
#include <QLabel>
#include <QPainterpath>
#include <QGraphicsEffect>
#include <QGraphicsDropShadowEffect>
#include "models/data/ListItemData.h"

class ListItem : public QWidget {
    Q_OBJECT

public:
    enum class LayoutMode {
        IconMode,
        ListMode
    };

    static constexpr int kIconTextGap = 8;

    explicit ListItem(QWidget* parent = nullptr);

    // 有参构造函数 - 通过 ListItemData 初始化
    explicit ListItem(const QString& path, QWidget* parent = nullptr);

    ~ListItem();
	// 初始化数据
	void initData();
    // 获取文件信息
    const QFileInfo& fileInfo() const { return m_fileInfo; };
    //const QRect& rect() const { return m_itemRect; };

    const QIcon& icon() const { return m_icon; }
    const QString& filePath() const {
        return m_filePath; 
    }
    QString windowsFilePath() const {
        QString tmp = m_filePath;
        return tmp.replace("\\", "/");
    }
    const QString& baseName() const { return m_baseName; }
    QString displayName() const { return m_baseName; }
    const QSize& size() const { return m_itemSize; }
    const QPoint& pos() const { return m_itemPos; }
    const ObjectType type() const { return m_type; }
    const QString getSortName() const { return m_baseName.toLower(); }
    const QRect& getItemRect() const { return m_itemRect; }
    const QRect& getChooseItemRect() const { return m_chooseRect; }
    ListItemData getItemData() const; // 设置对应的 ListItemData

    void setItemData(ListItemData data);
    void setItemSize(int width,int heigh);
    void setItemPos(int startX,int startY);
    void setItemPos(QPoint pos);
    void setLayoutMode(LayoutMode mode);
    LayoutMode layoutMode() const { return m_layoutMode; }

    QPoint getLocationInfo() const { return m_itemPos; }

    /// <summary>
    /// Update Item UI LocationInfo
    /// </summary>
    /// <param name="pos"></param>
    void updateLocationInfo(QPoint pos);
    void checkSelf();
    void unCheckSelf();
    void setFocused(bool focused);
    void setCutMarked(bool cutMarked);
    void openItem();
    void refreshIcon(); // Refresh icon from system
    QRect iconRect() const;
    QRect textRect() const;
    QRect renameEditorRect() const;
    int visibleTextHeight() const;
    QRect selectionContentRect() const;
    void refreshTextLabelPresentation();
    void setFullNameExpanded(bool expanded);
    bool isFullNameExpanded() const { return m_expandFullNameOnSelect; }
    void setExpandedPreviewText(const QString& text);

    QString boxId() const { return m_boxId; }
    void setBoxId(const QString& id) { m_boxId = id; }
    int customSortOrder() const { return m_customSortOrder; }
    void setCustomSortOrder(int order) { m_customSortOrder = order; }
    const QString& normalizedPath() const { return m_normalizedPath; }
    // 统一使用 m_itemPos 和 m_boxId，以下方法保持兼容
    bool hasPersistentRenderState() const { return m_itemPos != QPoint(0, 0) || !m_boxId.isEmpty(); }
    const QPoint& persistentRenderPos() const { return m_itemPos; }
    const QString& persistentBoxId() const { return m_boxId; }

    bool isReady = false;
    bool isSelected = false; // 是否被选中
    bool isHovered = false;  // 是否被鼠标悬浮
    bool isFocused = false;  // 是否为当前焦点项


signals:
    void itemClicked(ListItem* item, QMouseEvent* event);
    void itemMoved(ListItem* item, QMouseEvent* event);
    void itemReleased(ListItem* item, QMouseEvent* event);
    void itemDoubleClicked(ListItem* item, QMouseEvent* event);
    void itemContextMenuRequested(ListItem* item, const QPoint& globalPos);

protected:
    void ListItem::mousePressEvent(QMouseEvent* event) override;
    void ListItem::mouseMoveEvent(QMouseEvent* event) override;
    void ListItem::mouseReleaseEvent(QMouseEvent* event) override;
    void ListItem::mouseDoubleClickEvent(QMouseEvent* event) override;
    void ListItem::contextMenuEvent(QContextMenuEvent* event) override;
    void ListItem::paintEvent(QPaintEvent* event) override;
    void ListItem::enterEvent(QEvent* event) override;
    void ListItem::leaveEvent(QEvent* event) override;

    
    
    

private:
    
     ListItemData m_itemData;  // 关联的数据类
	 QSize m_itemSize;    //图标大小
	 QPoint m_itemPos;    //图标位置
     QPoint m_iconPos;   //图标起始点
     QRect m_itemRect;  //元素Rect
     QRect m_chooseRect;   //拖拽时选择判定区域
     QFileInfo m_fileInfo;
     QString m_baseName;           //基本文件名
     QString m_suffixName;         //文件后缀名
     QLabel* m_iconWidget = nullptr;       //图标控件
     QLabel* m_shortcutArrowOverlay = nullptr; //快捷方式小箭头
     QIcon m_icon;
     ObjectType m_type = ObjectType::Empty_ObjectType;  //数据对象类型
	 QString m_filePath;    //Windows 文件路径
     QLabel* textLabel = nullptr; //文件名称Label
     LayoutMode m_layoutMode = LayoutMode::IconMode;
	 QString m_boxId; // ID of the box this item belongs to (empty for desktop)
     int m_customSortOrder = -1;
     QString m_normalizedPath; // Normalized path for fast lookup
     bool m_expandFullNameOnSelect = false;
     QString m_expandedPreviewText;
     QSize m_baseItemSize;
     int m_visibleTextHeight = 0;
     QString m_pendingLabelText;
     bool m_labelRefreshScheduled = false;

     int desiredIconModeHeight() const;
     void applyEffectiveGeometry();
     void requestOwningBoxLayoutRefresh();
};
#endif // LISTITEM_H
