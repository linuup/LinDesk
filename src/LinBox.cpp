#include "LinBox.h"
#include <windows.h>
#include <windowsx.h>
#include <winuser.h>
#include <QPainter>
#include <QShowEvent>
#include <QStyleOption>
#include <QDebug>
#include <QMenu>
#include <QAction>
#include "SettingUI.h"
#include "ListView.h"
#include "ListItem.h"
#include "BoxManager.h"
#include "WinBlurHelper.h"

LinBox::LinBox(QWidget* parent) : QWidget(parent) {
    // 使用Qt::SubWindow使Box成为ListView的真正子窗口，不会覆盖其他软件
    setWindowFlags(Qt::FramelessWindowHint | Qt::SubWindow);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setFocusPolicy(Qt::StrongFocus);
    setStyleSheet("LinBox { background: transparent; }");
    setMouseTracking(true);
    
    resize(300, 400);
    setAcceptDrops(true);

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(m_borderWidth, m_borderWidth, m_borderWidth, m_borderWidth);
    m_mainLayout->setSpacing(0);

    this->m_title = "Brand New Box";
    m_titleBar = new QWidget(this);
    m_titleBar->setFixedHeight(m_titleHeight);
    m_titleBar->setObjectName("titleBar");
    m_titleBar->setAttribute(Qt::WA_StyledBackground); 
    
    QHBoxLayout* titleLayout = new QHBoxLayout(m_titleBar);
    titleLayout->setContentsMargins(10, 0, 5, 0);

    m_titleEdit = new QLineEdit(m_titleBar);
    m_titleEdit->setVisible(false);
    m_titleEdit->setText(m_title);
    m_titleEdit->setStyleSheet("background: white; color: black; border-radius: 4px; padding: 2px;");

    m_viewSwitchBtn = new QPushButton(m_titleBar);
    m_viewSwitchBtn->setFixedSize(24, 24);
    
    QFont iconFont("Segoe Fluent Icons");
    if (!QFontInfo(iconFont).exactMatch()) {
        iconFont.setFamily("Segoe MDL2 Assets");
    }
    iconFont.setPixelSize(14);
    m_viewSwitchBtn->setFont(iconFont);
    m_viewSwitchBtn->setText(u8"\uE8EF"); 
    m_viewSwitchBtn->setStyleSheet("QPushButton { background: transparent; color: white; border: none; border-radius: 4px; } "
                                   "QPushButton:hover { background: rgba(255, 255, 255, 40); } "
                                   "QPushButton:pressed { background: rgba(255, 255, 255, 20); }");

    m_collapseBtn = new QPushButton(m_titleBar);
    m_collapseBtn->setFixedSize(24, 24);
    m_collapseBtn->setFont(iconFont);
    m_collapseBtn->setText(u8"\uE70E"); 
    m_collapseBtn->setStyleSheet("QPushButton { background: transparent; color: white; border: none; border-radius: 4px; } "
                                 "QPushButton:hover { background: rgba(255, 255, 255, 40); } ");

    titleLayout->insertWidget(0, m_collapseBtn);
    titleLayout->addWidget(m_titleEdit);
    titleLayout->addStretch();
    titleLayout->addWidget(m_viewSwitchBtn);

    // Content Area inside Scroll Area
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName("scrollArea");
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setStyleSheet("QScrollArea { background: transparent; border: none; } "
                                "QScrollBar:vertical { width: 4px; background: transparent; margin: 0px 0px 0px 0px; } "
                                "QScrollBar::handle:vertical { background: rgba(255, 255, 255, 120); border-radius: 2px; min-height: 20px; } "
                                "QScrollBar::handle:vertical:hover { background: rgba(255, 255, 255, 200); } "
                                "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; } "
                                "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; } ");

    m_contentArea = new QWidget(m_scrollArea);
    m_contentArea->setObjectName("contentArea");
    m_contentArea->setAttribute(Qt::WA_StyledBackground);
    m_scrollArea->setWidget(m_contentArea);

    m_mainLayout->addWidget(m_titleBar);
    m_mainLayout->addWidget(m_scrollArea);

    refreshStyles();

    // Signals
    connect(m_viewSwitchBtn, &QPushButton::clicked, this, [this]() {
        if (m_showType == BoxShowType::Icon_ShowType) {
            setShowType(BoxShowType::List_ShowType);
        } else {
            setShowType(BoxShowType::Icon_ShowType);
        }
    });

    connect(m_collapseBtn, &QPushButton::clicked, this, &LinBox::toggleCollapse);

    connect(m_titleEdit, &QLineEdit::returnPressed, this, [this]() {
        setBoxTitle(m_titleEdit->text());
        m_titleEdit->setVisible(false);
    });

    connect(m_titleEdit, &QLineEdit::editingFinished, this, [this]() {
        if (m_titleEdit->isVisible()) {
            setBoxTitle(m_titleEdit->text());
            m_titleEdit->setVisible(false);
        }
    });

    m_titleBar->installEventFilter(this);
    m_contentArea->installEventFilter(this);
    raise(); 
}

LinBox::~LinBox() {}

void LinBox::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    raise();
}

void LinBox::setShowType(BoxShowType type) {
    if (m_showType == type) return;
    m_showType = type;
    if (m_showType == BoxShowType::Icon_ShowType) {
        m_viewSwitchBtn->setText(u8"\uE8EF"); 
    } else {
        m_viewSwitchBtn->setText(u8"\uECA5"); 
    }
    refreshLayout();
    emit geometryChanged(this);
}

void LinBox::addListItem(ListItem* item, bool refresh) {
    if (!item || m_items.contains(item)) return;
    m_items.append(item);
    item->setParent(m_contentArea);
    item->setBoxId(m_id); // Ensure item knows it's in this box
    item->show();
    item->raise();
    if (refresh) {
        refreshLayout();
        emit geometryChanged(this);
    }
}

void LinBox::removeListItem(ListItem* item) {
    if (!item) return;
    m_items.removeAll(item);
    item->setBoxId(""); // Item is no longer in this box
    // Note: Do NOT change parent here. Caller is responsible for reparenting.
    item->hide();
    refreshLayout();
    emit geometryChanged(this);
}

void LinBox::refreshLayout() {
    if (m_items.isEmpty()) return;
    const int itemW = 76;
    const int itemH = 78;
    const int colSpacing = 8;
    const int rowSpacing = 8;
    const int marginLeft = 5; 
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
            m_items[i]->setLayoutMode(ListItem::LayoutMode::IconMode);
            m_items[i]->move(x, y);
            m_items[i]->updateLocationInfo(QPoint(x, y));
            m_items[i]->show();
            m_items[i]->raise();
        }
    } else {
        int listItemH = 30; 
        int listItemW = contentW - 2 * marginLeft;
        for (int i = 0; i < m_items.size(); ++i) {
            int x = marginLeft;
            int y = marginTop + i * (listItemH + 2);
            m_items[i]->setLayoutMode(ListItem::LayoutMode::ListMode);
            m_items[i]->setItemSize(listItemW, listItemH);
            m_items[i]->move(x, y);
            m_items[i]->updateLocationInfo(QPoint(x, y));
            m_items[i]->show();
            m_items[i]->raise();
        }
    }
    m_contentArea->update();
    
    // Update content area height to enable scroll area
    int totalHeight = 0;
    if (m_showType == BoxShowType::Icon_ShowType) {
        int cols = qMax(1, (contentW - 2 * marginLeft + colSpacing) / (itemW + colSpacing));
        int rows = (m_items.size() + cols - 1) / cols;
        totalHeight = marginTop + rows * (itemH + rowSpacing) + 10;
    } else {
        totalHeight = marginTop + m_items.size() * (30 + 2) + 10;
    }
    m_contentArea->setMinimumHeight(totalHeight);
    m_contentArea->resize(m_contentArea->width(), totalHeight);
    
    // Ensure structural layout changes are persisted
    emit geometryChanged(this);
}

void LinBox::moveEvent(QMoveEvent* event) {
    if (QWidget* parent = parentWidget()) {
        const QRect oldRect(event->oldPos(), size());
        const QRect newRect(pos(), size());
        const QRect dirty = oldRect.united(newRect).adjusted(-12, -12, 12, 12);
        parent->update(dirty);
    }

    raise();
    QWidget::moveEvent(event);
}

void LinBox::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    refreshLayout();
}

void LinBox::setBoxTitle(const QString& title) {
    m_title = title;
    m_titleEdit->setText(title);
    m_titleBar->update();
    emit geometryChanged(this);
}

bool LinBox::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_titleBar && event->type() == QEvent::Paint) {
        if (!m_titleEdit->isVisible()) {
            QPainter p(m_titleBar);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::white);
            p.setFont(QFont("Microsoft YaHei UI", 9, QFont::Bold));
            
            // Adjust rect to avoid buttons: collapseBtn(30px) and viewSwitchBtn(right)
            // Left gap for collapse button + padding
            QRect textRect = m_titleBar->rect().adjusted(35, 0, -35, 0);
            QFontMetrics fm(p.font());
            QString elidedTitle = fm.elidedText(m_title, Qt::ElideRight, textRect.width());
            p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, elidedTitle);
        }
        return false; // Let the widget continue painting its own background/hover if it has style
    }

    if (watched == m_titleBar || watched == m_contentArea) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                BoxManager::instance()->bringToFront(this);
                // Determine if we should drag or resize
                // Map the child coordinate to LinBox coordinates
                QPoint mappedPos = watched == this ? mouseEvent->pos() : ((QWidget*)watched)->mapTo(this, mouseEvent->pos());
                m_resizeDir = getResizeDir(mappedPos);
                
                if (m_resizeDir != None && m_resizeDir != Move) {
                    m_isResizing = true;
                    m_dragStartPos = mouseEvent->globalPos();
                    m_dragStartGeometry = geometry();
                    return true;
                } else if (isDragArea(mappedPos)) {
                    // Performance optimization: Optional solid-ish background during move for visibility
                    m_contentArea->setStyleSheet("background: rgba(255, 255, 255, 40);");
                    m_titleBar->setStyleSheet("background: rgba(255, 255, 255, 40);");

                    ReleaseCapture();
                    SendMessage((HWND)winId(), WM_SYSCOMMAND, (WPARAM)0xF012, 0); 
                    
                    refreshStyles();
                    emit geometryChanged(this); 
                    return true;
                }
            } else if (mouseEvent->button() == Qt::RightButton) {
                showCustomContextMenu(mouseEvent->globalPos());
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonDblClick && watched == m_titleBar) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            // Only trigger rename if we clicked in the title area, not on buttons
            if (isDragArea(mouseEvent->pos())) {
                m_titleEdit->show();
                m_titleEdit->setFocus();
                m_titleEdit->selectAll();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void LinBox::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        BoxManager::instance()->bringToFront(this);
    }
    setFocus();
    if (event->button() == Qt::LeftButton) {
        m_resizeDir = getResizeDir(event->pos());
        if (isDragArea(event->pos())) {
            m_isMoving = true;
            m_dragStartPos = event->globalPos();
            m_dragStartGeometry = geometry();

            if (m_glassEffect) WinBlurHelper::applyBlur(this, WinBlurHelper::None);
            m_contentArea->setStyleSheet("background: rgba(255, 255, 255, 40);");
            m_titleBar->setStyleSheet("background: rgba(255, 255, 255, 40);");

            event->accept();
            return;
        } else if (m_resizeDir != None) {
            m_isResizing = true;
            m_dragStartPos = event->globalPos();
            m_dragStartGeometry = geometry();
        }
    } else if (event->button() == Qt::RightButton) {
        QWidget* child = childAt(event->pos());
        if (m_titleBar->geometry().contains(event->pos()) || !child || child == m_contentArea || child == this) {
            showCustomContextMenu(event->globalPos());
        }
    }
}

void LinBox::mouseMoveEvent(QMouseEvent* event) {
    if (m_isResizing) {
        QPoint delta = event->globalPos() - m_dragStartPos;
        QRect newGeom = m_dragStartGeometry;
        if (m_resizeDir & Left) newGeom.setLeft(m_dragStartGeometry.left() + delta.x());
        if (m_resizeDir & Right) newGeom.setRight(m_dragStartGeometry.right() + delta.x());
        if (m_resizeDir & Top) newGeom.setTop(m_dragStartGeometry.top() + delta.y());
        if (m_resizeDir & Bottom) newGeom.setBottom(m_dragStartGeometry.bottom() + delta.y());
        if (newGeom.width() >= 100 && newGeom.height() >= 100) {
            setGeometry(newGeom);
            update();
        }
    } else if (m_isMoving) {
        // 实时跟随鼠标移动
        QPoint delta = event->globalPos() - m_dragStartPos;
        QPoint newPos = m_dragStartGeometry.topLeft() + delta;
        
        // 移动到新位置
        move(newPos);
        
        // 只更新Box本身，避免层级变化
        update();
    } else {
        updateCursorShape(event->pos());
    }
}

void LinBox::mouseReleaseEvent(QMouseEvent* event) {
    if (m_isResizing || m_isMoving) {
        refreshStyles();
        emit geometryChanged(this);
    }
    m_isResizing = false;
    m_isMoving = false;
    updateCursorShape(event->pos());
}

void LinBox::paintEvent(QPaintEvent* event) {
    if (!m_glassEffect) {
        QPainter p(this);
        QStyleOption opt;
        opt.init(this);
        style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
        
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(255, 255, 255, 50), 1));
        QRect contentRect = rect().adjusted(m_borderWidth, m_borderWidth, -m_borderWidth, -m_borderWidth);
        p.drawRect(contentRect.adjusted(0, 0, -1, -1));
    }
    // In glass mode, we do NOT draw any background.
    // The native 0x00000001 tint in WinBlurHelper ensures mouse hit-testing.
}

void LinBox::enterEvent(QEvent* event) {
    QWidget::enterEvent(event);
}

void LinBox::leaveEvent(QEvent* event) {
    setCursor(Qt::ArrowCursor);
    QWidget::leaveEvent(event);
}

LinBox::ResizeDir LinBox::getResizeDir(const QPoint& pos) {
    int x = pos.x(), y = pos.y(), w = width(), h = height();
    int cs = qBound(30, qMin(w, h) / 8, 80);    
    int th = qBound(8, qMin(w, h) / 20, 15);   
    if (x >= 0 && x <= cs && y >= 0 && y <= cs) return TopLeft;
    if (x >= w - cs && x <= w && y >= 0 && y <= cs) return TopRight;
    if (x >= 0 && x <= cs && y >= h - cs && y <= h) return BottomLeft;
    if (x >= w - cs && x <= w && y >= h - cs && y <= h) return BottomRight;
    if (y >= 0 && y <= m_titleHeight && x > cs && x < w - cs) return Move;
    if (x >= 0 && x <= th) return Left;
    if (x >= w - th && x <= w) return Right;
    if (y >= h - th && y <= h) return Bottom;
    if (y >= 0 && y <= th && x > cs && x < w - cs) return Top;
    return None;
}

bool LinBox::nativeEvent(const QByteArray& eventType, void* message, long* result) {
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_NCHITTEST) {
        if (m_glassEffect) {
            int x = GET_X_LPARAM(msg->lParam);
            int y = GET_Y_LPARAM(msg->lParam);
            QPoint localPos = mapFromGlobal(QPoint(x, y));
            
            if (rect().contains(localPos)) {
                // 1. Full Title bar area must be solid to capture ALL mouse events (drag + buttons)
                if (m_titleBar->geometry().contains(localPos)) {
                    *result = HTCLIENT;
                    return true;
                }
                
                // 2. Content area (within scroll area): Icons and ScrollBars are solid; empty space is click-through
                if (m_scrollArea->geometry().contains(localPos)) {
                    QPoint scrollPos = m_scrollArea->mapFrom(this, localPos);
                    QWidget* child = m_scrollArea->childAt(scrollPos);
                    
                    // If it's a scrollbar or a child of contentArea, it should be solid
                    if (child && (child == m_scrollArea->verticalScrollBar() || m_contentArea->childAt(m_contentArea->mapFrom(m_scrollArea, scrollPos)))) {
                        *result = HTCLIENT;
                        return true;
                    }
                }
                
                // 3. Fallback for all other internal area (empty content background) -> Click through
                *result = HTTRANSPARENT; 
                return true; 
            }
        }
    }
    return QWidget::nativeEvent(eventType, message, result);
}

void LinBox::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        // 默认设置为 MoveAction，按住 Ctrl 时切换为 CopyAction
        if (event->keyboardModifiers() & Qt::ControlModifier) {
            event->setDropAction(Qt::CopyAction);
        } else {
            event->setDropAction(Qt::MoveAction);
        }
        event->accept();
        updateDragTip(event->pos(), event->keyboardModifiers() & Qt::ControlModifier);
    }
}

void LinBox::dragLeaveEvent(QDragLeaveEvent* event) {
    if (m_dragTip) m_dragTip->hide();
}

void LinBox::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData()->hasUrls()) {
        // 默认设置为 MoveAction，按住 Ctrl 时切换为 CopyAction
        if (event->keyboardModifiers() & Qt::ControlModifier) {
            event->setDropAction(Qt::CopyAction);
        } else {
            event->setDropAction(Qt::MoveAction);
        }
        event->accept();
        updateDragTip(event->pos(), event->keyboardModifiers() & Qt::ControlModifier);
    } else {
        if (m_dragTip) m_dragTip->hide();
    }
}

void LinBox::dropEvent(QDropEvent* event) {
    if (m_dragTip) m_dragTip->hide();
    const QMimeData* mime = event->mimeData();
    if (!mime->hasUrls()) return;
    
    ListView* lv = ListView::instance();
    if (!lv) return;
    
    for (const QUrl& url : mime->urls()) {
        QString filePath = url.toLocalFile();
        if (!filePath.isEmpty()) {
            lv->moveItemToBox(filePath, this, false); // Optimized: no immediate persistence or layout
        }
    }
    
    // 2. Refresh layouts efficiently
    this->refreshLayout();
    this->raise(); 
    
    // Desktop Sync: Only trigger full redistribution if Auto-Arrange is active.
    // In manual mode, the icon was already hidden from the desktop visible pool.
    if (lv->isAutoArrangeEnabled()) {
        lv->loadIconLayer();
    } else {
        lv->update();
    }
    
    lv->requestSaveBoxes();
    
    // 接受当前的 drop action（Move 或 Copy）
    event->accept();
}

void LinBox::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete) {
        ListView* lv = ListView::instance();
        if (lv) {
            bool shiftPressed = event->modifiers() & Qt::ShiftModifier;
            lv->onMenuDelete(shiftPressed);
            event->accept();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

bool LinBox::isDragArea(const QPoint& pos) {
    if (m_titleBar->geometry().contains(pos)) {
        QPoint titlePos = m_titleBar->mapFrom(this, pos);
        QWidget* child = m_titleBar->childAt(titlePos);
        // Drag allowed if clicking the title bar itself or an invisible part
        // Exclude specific interaction widgets
        if (child && (child == m_collapseBtn || child == m_viewSwitchBtn || child == m_titleEdit)) {
            return false;
        }
        return true;
    }
    
    // 2. Content area can also initiate drag if background clicked
    if (m_scrollArea->geometry().contains(pos)) {
        QPoint scrollPos = m_scrollArea->mapFrom(this, pos);
        QWidget* child = m_scrollArea->childAt(scrollPos);
        // Drag allowed if click is on scrollarea but NOT on scrollbar or an icon
        if (child == m_scrollArea->verticalScrollBar()) return false;
        
        QPoint contentPos = m_contentArea->mapFrom(this, pos);
        if (m_contentArea->childAt(contentPos) == nullptr) {
            return true;
        }
    }
    
    return false;
}

void LinBox::updateCursorShape(const QPoint& pos) {
    ResizeDir dir = getResizeDir(pos);
    switch (dir) {
        case Top: case Bottom: setCursor(Qt::SizeVerCursor); break;
        case Left: case Right: setCursor(Qt::SizeHorCursor); break;
        case TopLeft: case BottomRight: setCursor(Qt::SizeFDiagCursor); break;
        case TopRight: case BottomLeft: setCursor(Qt::SizeBDiagCursor); break;
        default: setCursor(Qt::ArrowCursor); break;
    }
}

void LinBox::toggleCollapse() {
    if (!m_isCollapsed) {
        m_originalHeight = height();
        m_scrollArea->hide(); // Hide scroll area instead of content area directly
        setFixedHeight(m_titleHeight + 2 * m_borderWidth);
        m_collapseBtn->setText(u8"\uE70D");
        m_isCollapsed = true;
    } else {
        m_scrollArea->show();
        setMinimumHeight(100); setMaximumHeight(16777215);
        resize(width(), m_originalHeight);
        m_collapseBtn->setText(u8"\uE70E");
        m_isCollapsed = false;
    }
    emit geometryChanged(this);
}

void LinBox::setBackgroundColor(const QColor& color) { m_bgColor = color; refreshStyles(); emit geometryChanged(this); }
void LinBox::setBackgroundOpacity(float opacity) { m_bgOpacity = opacity; refreshStyles(); emit geometryChanged(this); }
void LinBox::setGlassEffect(bool enabled) { m_glassEffect = enabled; WinBlurHelper::applyBlur(this, enabled ? WinBlurHelper::Acrylic : WinBlurHelper::None); refreshStyles(); emit geometryChanged(this); }

void LinBox::refreshStyles() {
    int r = m_bgColor.red(), g = m_bgColor.green(), b = m_bgColor.blue();
    
    // Ensure the window is transparent to show the DWM blur effect
    this->setAttribute(Qt::WA_TranslucentBackground);
    this->setStyleSheet("background: transparent; border: none;");

    // In glass mode, alpha should be nearly zero (1/255) for both stylesheet and accent policy
    // to ensure the window is responsive to mouse but visually driven by DWM blur.
    int finalA = m_glassEffect ? 1 : (int)(m_bgOpacity * 255);
    int titleA = m_glassEffect ? 1 : (int)(m_bgOpacity * 255);
    
    int finalR = m_glassEffect ? 0 : r;
    int finalG = m_glassEffect ? 0 : g;
    int finalB = m_glassEffect ? 0 : b;

    if (!m_glassEffect && finalA < 5) finalA = 20;

    QString titleBgs = m_glassEffect ? "rgba(0,0,0,1)" : QString("rgba(%1, %2, %3, %4)").arg(finalR).arg(finalG).arg(finalB).arg(titleA);
    QString contentBgs = m_glassEffect ? "rgba(0,0,0,1)" : QString("rgba(%1, %2, %3, %4)").arg(finalR).arg(finalG).arg(finalB).arg(finalA);

    m_titleBar->setStyleSheet(QString("QWidget#titleBar { background: %1; border-radius: 0px; }").arg(titleBgs));
    m_contentArea->setStyleSheet(QString("QWidget#contentArea { background: %1; border-radius: 0px; }").arg(contentBgs));
    
    // Re-apply blur if enabled to ensure the native window state is correct
    if (m_glassEffect) {
        WinBlurHelper::applyBlur(this, WinBlurHelper::Acrylic);
    } else {
        WinBlurHelper::applyBlur(this, WinBlurHelper::None);
    }
}

void LinBox::showCustomContextMenu(const QPoint& globalPos) {
    QMenu menu(this);
    menu.setWindowFlags(menu.windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    
    // 100% Windows 11 Context Menu Clone (Light Theme)
    menu.setStyleSheet(
        "QMenu { "
        "  background-color: rgba(255, 255, 255, 175); "
        "  border: 1px solid rgba(0, 0, 0, 0.15); "
        "  border-top: 1px solid rgba(255, 255, 255, 0.45); " // Glass highlight
        "  border-radius: 12px; "
        "  padding: 6px 2px; "
        "  color: #000000; "
        "  font-family: 'Segoe UI Variable Text', 'Segoe UI', sans-serif; "
        "  font-size: 14px; "
        "} "
        "QMenu::item { "
        "  background-color: transparent; "
        "  padding: 6px 40px 6px 12px; " // Spacing for icons and text
        "  border-radius: 4px; "
        "  margin: 1px 6px; "
        "} "
        "QMenu::item:selected { "
        "  background-color: rgba(0, 0, 0, 0.065); "
        "  color: #000000; "
        "} "
        "QMenu::separator { "
        "  height: 1px; "
        "  background: rgba(0, 0, 0, 0.06); "
        "  margin: 4px 14px; "
        "}"
    );

    // Apply Blur to the menu window (Acrylic effect)
    QTimer::singleShot(0, [&menu]() {
        WinBlurHelper::applyBlur(&menu, WinBlurHelper::Acrylic);
    });

    // Dynamic Icon Creation (High DPI & Fallback aware)
    auto createIcon = [](char16_t code, const QColor& color) {
        QPixmap pix(32, 32); 
        pix.fill(Qt::transparent); 
        QPainter p(&pix); 
        p.setRenderHint(QPainter::Antialiasing); 
        p.setRenderHint(QPainter::TextAntialiasing);
        
        QFont f("Segoe Fluent Icons");
        if (!QFontInfo(f).exactMatch()) f.setFamily("Segoe MDL2 Assets");
        f.setPixelSize(16);
        p.setFont(f); 
        p.setPen(color); 
        p.drawText(pix.rect(), Qt::AlignCenter, QString(code)); 
        return QIcon(pix);
    };

    QAction* renameAction = new QAction("Rename", &menu); 
    renameAction->setIcon(createIcon(0xE8AC, QColor(28,28,28)));
    
    QAction* viewSwitchAction = new QAction(m_showType == BoxShowType::Icon_ShowType ? "List View" : "Icon View", &menu);
    viewSwitchAction->setIcon(createIcon(0xE10F, QColor(28,28,28)));

    QAction* destroyAction = new QAction("Destroy Box", &menu); 
    destroyAction->setIcon(createIcon(0xE74D, QColor(255,69,58)));

    QAction* settingsAction = new QAction("Box Settings", &menu); 
    settingsAction->setIcon(createIcon(0xE713, QColor(28,28,28)));
    
    menu.addAction(renameAction);
    menu.addAction(viewSwitchAction);
    menu.addSeparator();
    menu.addAction(settingsAction);
    menu.addAction(destroyAction);

    QAction* selectedAction = menu.exec(globalPos);
    if (selectedAction == renameAction) {
        m_titleEdit->show(); 
        m_titleEdit->setFocus(); 
        m_titleEdit->selectAll(); 
        m_titleBar->update(); 
    } 
    else if (selectedAction == viewSwitchAction) {
        setShowType(m_showType == BoxShowType::Icon_ShowType ? BoxShowType::List_ShowType : BoxShowType::Icon_ShowType);
    } 
    else if (selectedAction == settingsAction) {
        if (ListView::instance()) ListView::instance()->showSettings(this);
    }
    else if (selectedAction == destroyAction) {
        emit destroyRequested(this);
    }
}

void LinBox::updateDragTip(const QPoint& pos, bool isCtrl) {
    if (isCtrl) {
        if (!m_dragTip) {
            m_dragTip = new QLabel(this);
            m_dragTip->setStyleSheet(
                "QLabel {"
                "  background-color: rgba(255, 255, 255, 235);" 
                "  color: #202020;"
                "  border: 1px solid rgba(0, 0, 0, 40);"
                "  border-radius: 8px;"
                "  padding: 10px 16px;"
                "  font-size: 13px;"
                "  font-family: 'Segoe UI Variable Display', 'Segoe UI', sans-serif;"
                "}"
            );
            m_dragTip->setAttribute(Qt::WA_TransparentForMouseEvents);
            QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(m_dragTip);
            shadow->setBlurRadius(20);
            shadow->setColor(QColor(0, 0, 0, 80)); 
            shadow->setOffset(0, 4);
            m_dragTip->setGraphicsEffect(shadow);
        }
        
        m_dragTip->setText("<b style='color:#70a1ff'>Copy to</b> <span>Box</span>");
        m_dragTip->adjustSize();

        QPoint targetPos = pos + QPoint(24, 24);
        if (targetPos.x() + m_dragTip->width() > width()) targetPos.setX(pos.x() - m_dragTip->width() - 8);
        if (targetPos.y() + m_dragTip->height() > height()) targetPos.setY(pos.y() - m_dragTip->height() - 8);

        m_dragTip->move(targetPos);
        m_dragTip->show();
        m_dragTip->raise();
    } else {
        if (m_dragTip) m_dragTip->hide();
    }
}
