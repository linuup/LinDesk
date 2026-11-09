#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QGraphicsDropShadowEffect>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QScreen>
#include <QApplication>
#include <QParallelAnimationGroup>
#include <QEvent>
#include <QResizeEvent>
#include <QStackedLayout>
#include <QScrollArea>
#include <QFrame>
#include <QMimeData>
#include <QDropEvent>
#include <QUrl>
#include <QCoreApplication>
#include <QVector>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#endif

class FluentDrawer : public QWidget {
public:
    explicit FluentDrawer(QWidget* host, QWidget* parent = nullptr)
        : QWidget(parent ? parent : host), m_host(host) {
        setObjectName(QStringLiteral("fluentDrawerOverlay"));
        setAttribute(Qt::WA_StyledBackground, false);
        hide();

        m_panel = new QWidget(this);
        m_panel->setObjectName(QStringLiteral("fluentDrawerPanel"));
        m_panel->setAttribute(Qt::WA_StyledBackground, true);
        m_panel->setStyleSheet(
            "QWidget#fluentDrawerPanel { "
            "   background: #ffffff; "
            "   border-left: 1px solid rgba(0, 0, 0, 0.08); "
            "} "
        );

        auto* panelLayout = new QVBoxLayout(m_panel);
        panelLayout->setContentsMargins(0, 0, 0, 0);
        panelLayout->setSpacing(0);

        m_header = new QWidget(m_panel);
        m_header->setObjectName(QStringLiteral("fluentDrawerHeader"));
        auto* headerLayout = new QHBoxLayout(m_header);
        headerLayout->setContentsMargins(20, 12, 10, 12);
        headerLayout->setSpacing(8);

        m_titleLabel = new QLabel(m_header);
        m_titleLabel->setObjectName(QStringLiteral("fluentDrawerTitle"));
        m_titleLabel->setStyleSheet(
            "QLabel#fluentDrawerTitle { "
            "   color: #111827; "
            "   font-size: 16px; "
            "   font-weight: 600; "
            "} "
        );
        headerLayout->addWidget(m_titleLabel, 0, Qt::AlignVCenter);
        headerLayout->addStretch(1);

        m_closeButton = new QPushButton(QString::fromUtf8("\xEE\xA2\xBB"), m_header);
        m_closeButton->setObjectName(QStringLiteral("fluentDrawerCloseButton"));
        m_closeButton->setFixedSize(36, 32);
        m_closeButton->setCursor(Qt::ArrowCursor);
        m_closeButton->setStyleSheet(
            "QPushButton#fluentDrawerCloseButton { "
            "   font-family: 'Segoe MDL2 Assets'; "
            "   font-size: 10px; "
            "   color: #374151; "
            "   border: none; "
            "   background: transparent; "
            "} "
            "QPushButton#fluentDrawerCloseButton:hover { "
            "   background: rgba(0, 0, 0, 0.06); "
            "} "
        );
        headerLayout->addWidget(m_closeButton, 0, Qt::AlignTop);
        panelLayout->addWidget(m_header);

        m_scrollArea = new QScrollArea(m_panel);
        m_scrollArea->setObjectName(QStringLiteral("fluentDrawerScrollArea"));
        m_scrollArea->setWidgetResizable(true);
        m_scrollArea->setFrameShape(QFrame::NoFrame);
        m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_scrollArea->setStyleSheet(QStringLiteral(
            "QScrollArea#fluentDrawerScrollArea {"
            " background: transparent;"
            " border: none;"
            "}"
            "QScrollArea#fluentDrawerScrollArea > QWidget > QWidget {"
            " background: transparent;"
            "}"
            "QScrollBar:vertical {"
            " background: transparent;"
            " width: 10px;"
            " margin: 12px 2px 12px 0;"
            "}"
            "QScrollBar::handle:vertical {"
            " background: rgba(100, 116, 139, 0.32);"
            " border-radius: 4px;"
            " min-height: 34px;"
            "}"
            "QScrollBar::handle:vertical:hover {"
            " background: rgba(59, 130, 246, 0.48);"
            "}"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
            " height: 0;"
            "}"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
            " background: transparent;"
            "}"));
        m_body = new QWidget(m_scrollArea);
        m_body->setObjectName(QStringLiteral("fluentDrawerBody"));
        m_body->setMinimumWidth(0);
        m_body->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_bodyLayout = new QVBoxLayout(m_body);
        m_bodyLayout->setContentsMargins(20, 16, 26, 20);
        m_bodyLayout->setSpacing(12);
        m_scrollArea->setWidget(m_body);
        panelLayout->addWidget(m_scrollArea, 1);

        m_animation = new QPropertyAnimation(m_panel, "pos", this);
        m_animation->setDuration(160);
        m_animation->setEasingCurve(QEasingCurve::OutCubic);

        connect(m_closeButton, &QPushButton::clicked, this, [this]() {
            closeDrawer();
        });
    }

    void setContent(QWidget* content, const QString& title, int width = 420) {
        clearBody();
        m_content = content;
        m_titleLabel->setText(title);
        m_titleLabel->setVisible(!title.trimmed().isEmpty());
        m_panelWidth = qBound(280, width, 760);
        if (m_content) {
            m_content->setParent(m_body);
            m_content->setMinimumWidth(0);
            m_content->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            m_bodyLayout->addWidget(m_content);
        }
        updateGeometryForState(false);
    }

    void openDrawer() {
        if (!m_host) {
            return;
        }
        setGeometry(m_host->rect());
        setUpdatesEnabled(true);
        raise();
        show();
        m_open = true;
        animateToOpen();
    }

    void closeDrawer() {
        if (!isVisible() || !m_host) {
            return;
        }
        m_open = false;
        animateToClosed();
    }

    bool isOpen() const { return m_open; }
    QWidget* contentWidget() const { return m_content; }

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.fillRect(rect(), QColor(15, 23, 42, 82));
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (m_maskClosable && m_panel && !m_panel->geometry().contains(event->pos())) {
            closeDrawer();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        updateGeometryForState(m_open);
    }

private:
    void clearBody() {
        while (QLayoutItem* item = m_bodyLayout->takeAt(0)) {
            if (QWidget* widget = item->widget()) {
                widget->deleteLater();
            }
            delete item;
        }
        m_content = nullptr;
    }

    QRect openedGeometry() const {
        return QRect(qMax(0, width() - m_panelWidth), 0, m_panelWidth, height());
    }

    QRect closedGeometry() const {
        return QRect(width(), 0, m_panelWidth, height());
    }

    void updateGeometryForState(bool open) {
        if (!m_panel) {
            return;
        }
        const QRect target = open ? openedGeometry() : closedGeometry();
        m_panel->setGeometry(target);
    }

    void animateToOpen() {
        m_animation->stop();
        m_panel->setGeometry(closedGeometry());
        m_animation->setStartValue(closedGeometry().topLeft());
        m_animation->setEndValue(openedGeometry().topLeft());
        m_animation->start();
    }

    void animateToClosed() {
        m_animation->stop();
        m_animation->setStartValue(m_panel->pos());
        m_animation->setEndValue(closedGeometry().topLeft());
        if (m_closeConnection) {
            disconnect(m_closeConnection);
        }
        m_closeConnection = connect(m_animation, &QPropertyAnimation::finished, this, [this]() {
            disconnect(m_closeConnection);
            m_closeConnection = QMetaObject::Connection();
            if (!m_open) {
                hide();
            }
        });
        m_animation->start();
    }

    QWidget* m_host = nullptr;
    QWidget* m_panel = nullptr;
    QWidget* m_header = nullptr;
    QWidget* m_body = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QLabel* m_titleLabel = nullptr;
    QPushButton* m_closeButton = nullptr;
    QVBoxLayout* m_bodyLayout = nullptr;
    QPropertyAnimation* m_animation = nullptr;
    QMetaObject::Connection m_closeConnection;
    QWidget* m_content = nullptr;
    int m_panelWidth = 420;
    bool m_open = false;
    bool m_maskClosable = true;
};

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

/**
 * @brief Base class for modern, floating frameless windows with shadows.
 * This is DIFFERENT from FluentDialog (smoke backdrop). 
 * This is a floating 'Card' style window.
 */
class FluentBaseWidget : public QWidget {
public:
    enum WindowBtnMode { None, All, CloseOnly };

    explicit FluentBaseWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::Window);
        setAttribute(Qt::WA_TranslucentBackground);
        
        m_rootLayout = new QVBoxLayout(this);
        m_rootLayout->setContentsMargins(0, 0, 0, 0); // No more 15px buffer

        m_container = new QWidget(this);
        m_container->setObjectName("fluentBaseContainer");
        m_container->setStyleSheet(
            "QWidget#fluentBaseContainer { "
            "   background-color: rgba(255, 255, 255, 1.0); " // Standard card opaque
            "   border: 1px solid rgba(0, 0, 0, 0.08); "
            "} "
        );
        m_rootLayout->addWidget(m_container);

        // --- Main container layout ---
        m_mainVLayout = new QVBoxLayout(m_container);
        m_mainVLayout->setContentsMargins(0, 0, 0, 0);
        m_mainVLayout->setSpacing(0);

        // --- TitleBar specialized row ---
        m_btnContainer = new QWidget(m_container);
        m_btnContainer->setFixedHeight(32);
        m_btnLayout = new QHBoxLayout(m_btnContainer);
        m_btnLayout->setContentsMargins(0, 0, 0, 0);
        m_btnLayout->setSpacing(0);
        m_btnLayout->addStretch();

        auto createBtn = [&](const QString& icon, const QString& objName) {
            QPushButton* btn = new QPushButton(icon, m_btnContainer);
            btn->setObjectName(objName);
            btn->setFixedSize(45, 32);
            btn->setCursor(Qt::ArrowCursor); // Standard window controls don't use hand cursor
            btn->setStyleSheet(
                "QPushButton { "
                "   font-family: 'Segoe MDL2 Assets'; font-size: 10px; "
                "   background: transparent; border: none; color: #1f2937; "
                "} "
                "QPushButton:hover { background: rgba(0,0,0,0.06); } "
                "QPushButton#closeBtn:hover { background: #dc2626; color: white; } "
            );
            return btn;
        };

        m_minBtn = createBtn(u8"\uE921", "minBtn");
        m_maxBtn = createBtn(u8"\uE922", "maxBtn");
        m_closeBtn = createBtn(u8"\uE8BB", "closeBtn");

        connect(m_minBtn, &QPushButton::clicked, this, &FluentBaseWidget::animateMinimize);
        connect(m_maxBtn, &QPushButton::clicked, this, [this](){
            bool currentlyMax = (windowState() & Qt::WindowMaximized) || m_isMaximized;
            if (currentlyMax) {
                animateMaximize(false);
                m_maxBtn->setText(u8"\uE922");
            } else {
                animateMaximize(true);
                m_maxBtn->setText(u8"\uE923");
            }
        });
        connect(m_closeBtn, &QPushButton::clicked, this, &QWidget::close);

        m_btnLayout->addWidget(m_minBtn);
        m_btnLayout->addWidget(m_maxBtn);
        m_btnLayout->addWidget(m_closeBtn);
        
        m_mainVLayout->addWidget(m_btnContainer);

        // --- Subclass central area ---
        m_centralLayout = new QVBoxLayout();
        m_mainVLayout->addLayout(m_centralLayout);

        // Note: QGraphicsDropShadowEffect on a full top-level container forces full-surface
        // CPU Gaussian blur rasterization on every repaint/scroll/hover.
        // We use crisp hardware-rendered borders without CPU blur bottlenecks.

        setWindowBtnMode(None); // Default
    }

    QWidget* container() const { return m_container; }
    QVBoxLayout* centralLayout() const { return m_centralLayout; }
    FluentDrawer* drawer() const { return m_drawer; }

    FluentDrawer* openDrawer(QWidget* content, const QString& title = QString(), int width = 420) {
        ensureDrawer();
        m_drawer->setContent(content, title, width);
        m_drawer->openDrawer();
        return m_drawer;
    }

    void closeDrawer() {
        if (m_drawer) {
            m_drawer->closeDrawer();
        }
    }
    
    void setResizable(bool resizable) { m_resizable = resizable; }

    void setWindowBtnMode(WindowBtnMode mode) {
        m_minBtn->setVisible(mode == All);
        m_maxBtn->setVisible(mode == All);
        m_closeBtn->setVisible(mode == All || mode == CloseOnly);
        m_btnContainer->setVisible(mode != None);
    }

    void addTitleBarWidget(QWidget* w) {
        if (!w || !m_btnLayout) return;
        m_btnLayout->insertWidget(0, w); // Insert before standard window buttons
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            m_dragging = true;
            m_dragPos = event->globalPos() - frameGeometry().topLeft();
            event->accept();
        }
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (m_dragging && (event->buttons() & Qt::LeftButton) && !isMaximized()) {
            move(event->globalPos() - m_dragPos);
            event->accept();
        }
    }
    void mouseReleaseEvent(QMouseEvent*) override { m_dragging = false; }

    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override {
#ifdef Q_OS_WIN
        MSG* msg = static_cast<MSG*>(message);
        if (msg->message == WM_DROPFILES) {
            HDROP hDrop = reinterpret_cast<HDROP>(msg->wParam);
            const UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
            QList<QUrl> urls;
            for (UINT i = 0; i < count; ++i) {
                const UINT len = DragQueryFileW(hDrop, i, nullptr, 0);
                if (len > 0) {
                    QVector<wchar_t> buf(static_cast<int>(len) + 1, L'\0');
                    DragQueryFileW(hDrop, i, buf.data(), len + 1);
                    urls.append(QUrl::fromLocalFile(QString::fromWCharArray(buf.constData())));
                }
            }
            POINT pt{};
            DragQueryPoint(hDrop, &pt);
            DragFinish(hDrop);
            if (!urls.isEmpty()) {
                QMimeData mime;
                mime.setUrls(urls);
                QDropEvent dropEv(QPointF(pt.x, pt.y), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(this, &dropEv);
            }
            if (result) {
                *result = 0;
            }
            return true;
        }
        if (msg->message == WM_NCHITTEST) {
            const HWND hwnd = reinterpret_cast<HWND>(winId());
            const LONG nativeX = GET_X_LPARAM(msg->lParam);
            const LONG nativeY = GET_Y_LPARAM(msg->lParam);
            RECT nativeRect{};
            GetWindowRect(hwnd, &nativeRect);
            const UINT dpi = GetDpiForWindow(hwnd);
            const qreal scale = dpi > 0 ? static_cast<qreal>(dpi) / 96.0 : qMax<qreal>(1.0, devicePixelRatioF());
            const bool customWebTitle = m_btnContainer && m_btnContainer->isHidden();
            const int nativeBorder = qMax(1, qRound((customWebTitle ? 6 : 15) * scale));
            const QPoint pos(qRound((nativeX - nativeRect.left) / scale),
                             qRound((nativeY - nativeRect.top) / scale));

            if (m_resizable && !isMaximized()) {
                const bool left = nativeX >= nativeRect.left && nativeX < nativeRect.left + nativeBorder;
                const bool right = nativeX <= nativeRect.right && nativeX > nativeRect.right - nativeBorder;
                const bool top = nativeY >= nativeRect.top && nativeY < nativeRect.top + nativeBorder;
                const bool bottom = nativeY <= nativeRect.bottom && nativeY > nativeRect.bottom - nativeBorder;

                if (left && top) { *result = HTTOPLEFT; return true; }
                if (right && top) { *result = HTTOPRIGHT; return true; }
                if (left && bottom) { *result = HTBOTTOMLEFT; return true; }
                if (right && bottom) { *result = HTBOTTOMRIGHT; return true; }
                if (left) { *result = HTLEFT; return true; }
                if (right) { *result = HTRIGHT; return true; }
                if (top) { *result = HTTOP; return true; }
                if (bottom) { *result = HTBOTTOM; return true; }
            }

            // Custom Web Title Bar (44px height): return HTCLIENT for interactive tab/button rects,
            // and native HTCAPTION everywhere else for 100% native Windows drag, Aero Snap, and double-click maximize.
            if (customWebTitle) {
                if (pos.y() >= 0 && pos.y() < 44) {
                    const QVariantList customRects = property("webTitleInteractiveRects").toList();
                    if (!customRects.isEmpty()) {
                        for (const QVariant& v : customRects) {
                            if (v.toRect().contains(pos)) {
                                return false; // HTCLIENT: pass to Web UI
                            }
                        }
                    } else {
                        const QRect defaultNavRect(136, 0, 340, 44);
                        const QRect defaultControlsRect(width() - 190, 0, 190, 44);
                        if (defaultNavRect.contains(pos) || defaultControlsRect.contains(pos)) {
                            return false; // HTCLIENT: pass to Web UI
                        }
                    }
                    *result = HTCAPTION;
                    return true;
                }
                return QWidget::nativeEvent(eventType, message, result);
            }

            // Standard drag area (only when native Qt title bar is visible)
            if (pos.y() < 60) {
                QWidget* child = childAt(pos);
                // Recursively check parents to see if we hit a button or interactive component
                // This handles cases where childAt returns an internal sub-widget of a button.
                bool hitInteractive = false;
                QWidget* curr = child;
                while (curr && curr != this) {
                    if (qobject_cast<QPushButton*>(curr) || curr->inherits("QPushButton") || curr->inherits("QAbstractButton")) {
                        hitInteractive = true;
                        break;
                    }
                    curr = curr->parentWidget();
                }

                if (hitInteractive) {
                    return false;
                }
                *result = HTCAPTION;
                return true;
            }
        }
#endif
        return QWidget::nativeEvent(eventType, message, result);
    }

    void updateLayoutState(bool maximized) {
        // ALWAYS 0 margins to remove any white/transparent buffer
        m_rootLayout->setContentsMargins(0, 0, 0, 0); 
        
        // Maximized layout state update

        m_container->setStyleSheet(
            QString("QWidget#fluentBaseContainer { "
            "   background-color: rgba(255, 255, 255, 1.0); "
            "   border: %1; "
            "} ").arg(maximized ? "none" : "1px solid rgba(0, 0, 0, 0.08)")
        );

        // Force a full layout and repaint cycle to clear any ghosts
        m_rootLayout->activate();
        m_btnContainer->update();
        this->update();
    }

    void animateMinimize() {
        showMinimized();
    }

    void animateMaximize(bool toMax) {
        if (toMax) {
            m_normalGeometry = geometry();
            setWindowState(Qt::WindowMaximized);
            updateLayoutState(true);
        } else {
            setWindowState(Qt::WindowNoState);
            updateLayoutState(false);
        }
        m_isMaximized = toMax;
    }

    // Explicit state check to avoid conflicts during animation
    bool isCustomMaximized() const { return m_isAnimating ? (geometry().width() > m_normalGeometry.width()) : isMaximized(); }

    void changeEvent(QEvent* event) override {
        if (event->type() == QEvent::WindowStateChange) {
            // Restore opacity if we were minimized (now coming back)
            if (!(windowState() & Qt::WindowMinimized)) {
                if (windowOpacity() < 0.9) setWindowOpacity(1.0);
            }

            if (!m_isAnimating) {
                bool maximized = isMaximized();
                if (maximized != m_isMaximized) {
                   m_isMaximized = maximized;
                   updateLayoutState(maximized);
                }
            }
        }
        QWidget::changeEvent(event);
    }

    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        if (m_drawer) {
            m_drawer->setGeometry(rect());
        }
    }

protected:
    QHBoxLayout* m_btnLayout;
    QWidget* m_btnContainer;
    QPushButton* m_minBtn;
    QPushButton* m_maxBtn;
    QPushButton* m_closeBtn;

private:
    void ensureDrawer() {
        if (m_drawer) {
            return;
        }
        m_drawer = new FluentDrawer(this, this);
        m_drawer->setGeometry(rect());
    }

    QVBoxLayout* m_rootLayout;
    QWidget* m_container;
    QVBoxLayout* m_mainVLayout;
    QVBoxLayout* m_centralLayout;
    FluentDrawer* m_drawer = nullptr;

    bool m_isMaximized = false;
    bool m_dragging = false;
    bool m_resizable = false;
    bool m_isAnimating = false;
    QPoint m_dragPos;
    QRect m_normalGeometry;
};
