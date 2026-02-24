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
#include "ListItem.h"

class LinBox : public QWidget {
    Q_OBJECT
public:
    enum class BoxShowType {
        Icon_ShowType,
        List_ShowType
    };

    explicit LinBox(QWidget* parent = nullptr);
    ~LinBox();

    void setShowType(BoxShowType type);
    void addListItem(ListItem* item);
    void setBoxTitle(const QString& title);
    void setResizeBorderWidth(int width);
    int resizeBorderWidth() const { return m_borderWidth; }
    void refreshLayout();

signals:
    void itemRelayClicked(LinBox* box, ListItem* item, QMouseEvent* event);
    void itemRelayMoved(LinBox* box, ListItem* item, QMouseEvent* event);
    void itemRelayReleased(LinBox* box, ListItem* item, QMouseEvent* event);
    void itemRelayDoubleClicked(LinBox* box, ListItem* item, QMouseEvent* event);
    void itemRelayContextMenuRequested(LinBox* box, ListItem* item, const QPoint& globalPos);

public:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void enterEvent(QEvent* event) override;
    void leaveEvent(QEvent* event) override;

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
        BottomRight = Bottom | Right
    };

    void updateCursorShape(const QPoint& pos);
    ResizeDir getResizeDir(const QPoint& pos);

    BoxShowType m_showType = BoxShowType::Icon_ShowType;
    QVector<ListItem*> m_items;
    
    // UI Elements
    QWidget* m_titleBar;
    QLabel* m_titleLabel;
    QLineEdit* m_titleEdit;
    QPushButton* m_viewSwitchBtn;
    QWidget* m_contentArea;
    QVBoxLayout* m_mainLayout;

    // Interaction State
    bool m_isResizing = false;
    bool m_isMoving = false;
    ResizeDir m_resizeDir = None;
    QPoint m_dragStartPos;
    QRect m_dragStartGeometry;
    
    int m_borderWidth = 8;
    int m_titleHeight = 30;
};

#endif // LINBOX_H
