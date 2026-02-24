#include "LinBox.h"
#include <QPainter>
#include <QStyleOption>
#include <QDebug>

LinBox::LinBox(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    
    resize(300, 400);

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(m_borderWidth, m_borderWidth, m_borderWidth, m_borderWidth);
    m_mainLayout->setSpacing(0);

    // Title Bar
    m_titleBar = new QWidget(this);
    m_titleBar->setFixedHeight(m_titleHeight);
    m_titleBar->setObjectName("titleBar");
    m_titleBar->setStyleSheet("QWidget#titleBar { background: rgba(50, 50, 50, 150); border-top-left-radius: 5px; border-top-right-radius: 5px; }");

    QHBoxLayout* titleLayout = new QHBoxLayout(m_titleBar);
    titleLayout->setContentsMargins(10, 0, 5, 0);

    m_titleLabel = new QLabel("Brand New Grid", m_titleBar);
    m_titleLabel->setStyleSheet("color: white; font-weight: bold;");
    
    m_titleEdit = new QLineEdit(m_titleBar);
    m_titleEdit->setVisible(false);
    m_titleEdit->setStyleSheet("background: white; color: black;");

    m_viewSwitchBtn = new QPushButton("V", m_titleBar);
    m_viewSwitchBtn->setFixedSize(20, 20);
    m_viewSwitchBtn->setStyleSheet("QPushButton { background: rgba(255, 255, 255, 50); color: white; border: none; } QPushButton:hover { background: rgba(255, 255, 255, 100); }");

    titleLayout->addWidget(m_titleLabel);
    titleLayout->addWidget(m_titleEdit);
    titleLayout->addStretch();
    titleLayout->addWidget(m_viewSwitchBtn);

    // Content Area
    m_contentArea = new QWidget(this);
    m_contentArea->setObjectName("contentArea");
    m_contentArea->setStyleSheet("QWidget#contentArea { background: rgba(0, 0, 0, 80); border-bottom-left-radius: 5px; border-bottom-right-radius: 5px; }");

    m_mainLayout->addWidget(m_titleBar);
    m_mainLayout->addWidget(m_contentArea);

    // Signals
    connect(m_viewSwitchBtn, &QPushButton::clicked, this, [this]() {
        if (m_showType == BoxShowType::Icon_ShowType) {
            setShowType(BoxShowType::List_ShowType);
        } else {
            setShowType(BoxShowType::Icon_ShowType);
        }
    });

    connect(m_titleEdit, &QLineEdit::returnPressed, this, [this]() {
        setBoxTitle(m_titleEdit->text());
        m_titleEdit->setVisible(false);
        m_titleLabel->setVisible(true);
    });

    connect(m_titleEdit, &QLineEdit::editingFinished, this, [this]() {
        if (m_titleEdit->isVisible()) {
            setBoxTitle(m_titleEdit->text());
            m_titleEdit->setVisible(false);
            m_titleLabel->setVisible(true);
        }
    });

    // Double click title to edit
    m_titleLabel->installEventFilter(this);
}

LinBox::~LinBox() {}

void LinBox::setShowType(BoxShowType type) {
    if (m_showType == type) return;
    m_showType = type;
    qDebug() << "Switching view mode to:" << (type == BoxShowType::Icon_ShowType ? "Icon" : "List");
    refreshLayout();
}

void LinBox::addListItem(ListItem* item) {
    if (!item) return;
    m_items.append(item);
    item->setParent(m_contentArea);
    
    refreshLayout();
    item->show();
}

void LinBox::refreshLayout() {
    if (m_items.isEmpty()) return;

    // Use ListView's constants for consistency
    const int itemW = 76;
    const int itemH = 78;
    const int colSpacing = 8;
    const int rowSpacing = 8;
    const int marginLeft = 5; // Internal margin for LinBox
    const int marginTop = 5;

    int contentW = m_contentArea->width();

    if (m_showType == BoxShowType::Icon_ShowType) {
        int cols = qMax(1, (contentW - 2 * marginLeft + colSpacing) / (itemW + colSpacing));
        
        for (int i = 0; i < m_items.size(); ++i) {
            int row = i / cols;
            int col = i % cols;
            int x = marginLeft + col * (itemW + colSpacing);
            int y = marginTop + row * (itemH + rowSpacing);
            
            m_items[i]->setItemSize(itemW, itemH);
            m_items[i]->move(x, y);
            m_items[i]->updateLocationInfo(QPoint(x, y));
        }
    } else {
        int listItemH = 30; // Standard list item height
        int listItemW = contentW - 2 * marginLeft;
        
        for (int i = 0; i < m_items.size(); ++i) {
            int x = marginLeft;
            int y = marginTop + i * (listItemH + 2);
            
            m_items[i]->setItemSize(listItemW, listItemH);
            m_items[i]->move(x, y);
            m_items[i]->updateLocationInfo(QPoint(x, y));
        }
    }
}

void LinBox::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    refreshLayout();
}

void LinBox::setBoxTitle(const QString& title) {
    m_titleLabel->setText(title);
    m_titleEdit->setText(title);
}

void LinBox::setResizeBorderWidth(int width) {
    m_borderWidth = width;
    if (m_mainLayout) {
        m_mainLayout->setContentsMargins(m_borderWidth, m_borderWidth, m_borderWidth, m_borderWidth);
    }
}

bool LinBox::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_titleLabel && event->type() == QEvent::MouseButtonDblClick) {
        m_titleLabel->setVisible(false);
        m_titleEdit->setVisible(true);
        m_titleEdit->setFocus();
        m_titleEdit->selectAll();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void LinBox::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_resizeDir = getResizeDir(event->pos());
        if (m_resizeDir != None) {
            m_isResizing = true;
        } else if (m_titleBar->geometry().contains(event->pos())) {
            m_isMoving = true;
        }
        m_dragStartPos = event->globalPos();
        m_dragStartGeometry = geometry();
    }
}

void LinBox::mouseMoveEvent(QMouseEvent* event) {
    if (m_isResizing || m_isMoving) {
        // Safety Cancellation: If mouse moves too far into the interior area, cancel.
        // We use a safe threshold (40px) or 30% of size, whichever is smaller.
        int threshX = qMin(40, width() / 3);
        int threshY = qMin(40, height() / 3);
        
        QPoint p = event->pos();
        bool inInterior = (p.x() > threshX && p.x() < width() - threshX &&
                            p.y() > threshY && p.y() < height() - threshY);
        
        if (inInterior) {
            m_isResizing = false;
            m_isMoving = false;
            updateCursorShape(event->pos());
            return;
        }

        if (m_isResizing) {
            QPoint delta = event->globalPos() - m_dragStartPos;
            QRect newGeom = m_dragStartGeometry;

            if (m_resizeDir & Left) newGeom.setLeft(m_dragStartGeometry.left() + delta.x());
            if (m_resizeDir & Right) newGeom.setRight(m_dragStartGeometry.right() + delta.x());
            if (m_resizeDir & Top) newGeom.setTop(m_dragStartGeometry.top() + delta.y());
            if (m_resizeDir & Bottom) newGeom.setBottom(m_dragStartGeometry.bottom() + delta.y());

            if (newGeom.width() >= 100 && newGeom.height() >= 100) {
                setGeometry(newGeom);
            }
        } else if (m_isMoving) {
            move(m_dragStartGeometry.topLeft() + (event->globalPos() - m_dragStartPos));
        }
    } else {
        updateCursorShape(event->pos());
    }
}

void LinBox::mouseReleaseEvent(QMouseEvent* event) {
    m_isResizing = false;
    m_isMoving = false;
    updateCursorShape(event->pos()); // Update cursor back to whatever zone we are in
}

void LinBox::paintEvent(QPaintEvent* event) {
    QStyleOption opt;
    opt.init(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);

    // Draw border highlight around the visible content area (inset by m_borderWidth)
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(255, 255, 255, 50), 1));
    QRect contentRect = rect().adjusted(m_borderWidth, m_borderWidth, -m_borderWidth, -m_borderWidth);
    p.drawRoundedRect(contentRect.adjusted(0, 0, -1, -1), 5, 5);
}

void LinBox::enterEvent(QEvent* event) {
    QWidget::enterEvent(event);
}

void LinBox::leaveEvent(QEvent* event) {
    setCursor(Qt::ArrowCursor);
    QWidget::leaveEvent(event);
}

LinBox::ResizeDir LinBox::getResizeDir(const QPoint& pos) {
    int x = pos.x();
    int y = pos.y();
    int w = width();
    int h = height();
    
    const int cornerSize = 30; // Corner hit area
    const int hBorder = m_borderWidth; // side resize strip
    const int vHitHeight = 20; // Hit height for top/bottom strips

    // 1. Top Row
    if (y >= 0 && y <= vHitHeight) {
        if (x >= 0 && x <= cornerSize) return TopLeft;
        if (x >= w - cornerSize && x <= w) return TopRight;
        // Middle area is for moving, return None for cursor so it shows Arrow
        return None;
    }

    // 2. Bottom Row
    if (y >= h - cornerSize && y <= h) {
        if (x >= 0 && x <= cornerSize) return BottomLeft;
        if (x >= w - cornerSize && x <= w) return BottomRight;
        if (y >= h - hBorder) return Bottom; // Middle part of bottom row
    }

    // 3. Sides (Middle)
    if (x >= 0 && x <= hBorder) return Left;
    if (x >= w - hBorder && x <= w) return Right;

    return None;
}

void LinBox::updateCursorShape(const QPoint& pos) {
    ResizeDir dir = getResizeDir(pos);
    switch (dir) {
        case Top:
        case Bottom: setCursor(Qt::SizeVerCursor); break;
        case Left:
        case Right: setCursor(Qt::SizeHorCursor); break;
        case TopLeft:
        case BottomRight: setCursor(Qt::SizeFDiagCursor); break;
        case TopRight:
        case BottomLeft: setCursor(Qt::SizeBDiagCursor); break;
        default:
            if (m_titleBar->geometry().contains(pos)) {
                setCursor(Qt::ArrowCursor);
            } else {
                setCursor(Qt::ArrowCursor);
            }
            break;
    }
}
