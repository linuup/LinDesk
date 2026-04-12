#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QGraphicsDropShadowEffect>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QHBoxLayout>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QScreen>
#include <QApplication>
#include <QParallelAnimationGroup>

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
            "   border: 1px solid rgba(0, 0, 0, 0.1); "
            "   border-radius: 12px; "
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
                "   background: transparent; border: none; color: #333; "
                "   border-top-right-radius: 0px; border-bottom-right-radius: 0px; "
                "} "
                "QPushButton:hover { background: rgba(0,0,0,0.05); } "
                "QPushButton#closeBtn:hover { background: #f44336; color: white; border-top-right-radius: 11px; } "
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

        auto* shadow = new QGraphicsDropShadowEffect(this);
        shadow->setBlurRadius(25);
        shadow->setXOffset(0);
        shadow->setYOffset(6);
        shadow->setColor(QColor(0, 0, 0, 80));
        m_container->setGraphicsEffect(shadow);

        setWindowBtnMode(None); // Default
    }

    QWidget* container() const { return m_container; }
    QVBoxLayout* centralLayout() const { return m_centralLayout; }
    
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
        if (msg->message == WM_NCHITTEST) {
            int border = 15; // Increased to 15px to match root layout margins for better sensitivity
            long x = GET_X_LPARAM(msg->lParam);
            long y = GET_Y_LPARAM(msg->lParam);
            QPoint pos = mapFromGlobal(QPoint(x, y));

            int w = width();
            int h = height();

            if (m_resizable) {
                bool left = pos.x() < border;
                bool right = pos.x() > w - border;
                bool top = pos.y() < border;
                bool bottom = pos.y() > h - border;

                if (left && top) { *result = HTTOPLEFT; return true; }
                if (right && top) { *result = HTTOPRIGHT; return true; }
                if (left && bottom) { *result = HTBOTTOMLEFT; return true; }
                if (right && bottom) { *result = HTBOTTOMRIGHT; return true; }
                if (left) { *result = HTLEFT; return true; }
                if (right) { *result = HTRIGHT; return true; }
                if (top) { *result = HTTOP; return true; }
                if (bottom) { *result = HTBOTTOM; return true; }
            }

            // Standard drag area
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
        
        if (maximized) {
            m_container->setGraphicsEffect(nullptr); 
        } else {
            // Re-apply shadow carefully
            if (!m_container->graphicsEffect()) {
                auto* shadow = new QGraphicsDropShadowEffect(this);
                shadow->setBlurRadius(20); shadow->setXOffset(0); shadow->setYOffset(4);
                shadow->setColor(QColor(0, 0, 0, 60));
                m_container->setGraphicsEffect(shadow);
            }
        }

        m_container->setStyleSheet(
            QString("QWidget#fluentBaseContainer { "
            "   background-color: rgba(255, 255, 255, 1.0); "
            "   border: %1; "
            "   border-radius: %2px; "
            "} ").arg(maximized ? "none" : "1px solid rgba(0, 0, 0, 0.1)").arg(maximized ? 0 : 12)
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

protected:
    QHBoxLayout* m_btnLayout;
    QWidget* m_btnContainer;
    QPushButton* m_minBtn;
    QPushButton* m_maxBtn;
    QPushButton* m_closeBtn;

private:
    QVBoxLayout* m_rootLayout;
    QWidget* m_container;
    QVBoxLayout* m_mainVLayout;
    QVBoxLayout* m_centralLayout;

    bool m_isMaximized = false;
    bool m_dragging = false;
    bool m_resizable = false;
    bool m_isAnimating = false;
    QPoint m_dragPos;
    QRect m_normalGeometry;
};
