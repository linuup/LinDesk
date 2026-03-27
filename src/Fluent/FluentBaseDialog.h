#pragma once
#include <QDialog>
#include <QVBoxLayout>
#include <QGraphicsDropShadowEffect>
#include <QMouseEvent>
#include <QPainter>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

/**
 * @brief Floating modal Dialog with card style.
 * This is DIFFERENT from FluentDialog (smoke backdrop).
 * Use this for floating, draggable, modal config popups.
 */
class FluentBaseDialog : public QDialog {
public:
    explicit FluentBaseDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
        setAttribute(Qt::WA_TranslucentBackground);
        
        m_rootLayout = new QVBoxLayout(this);
        m_rootLayout->setContentsMargins(15, 15, 15, 15);

        m_container = new QWidget(this);
        m_container->setObjectName("fluentDialogContainer");
        m_container->setStyleSheet(
            "QWidget#fluentDialogContainer { "
            "   background-color: rgba(255, 255, 255, 0.98); "
            "   border: 1px solid rgba(0, 0, 0, 0.1); "
            "   border-radius: 12px; "
            "} "
        );
        m_rootLayout->addWidget(m_container);

        m_centralLayout = new QVBoxLayout(m_container);
        m_centralLayout->setContentsMargins(0, 0, 0, 0);
        m_centralLayout->setSpacing(0);

        auto* shadow = new QGraphicsDropShadowEffect(this);
        shadow->setBlurRadius(25);
        shadow->setXOffset(0);
        shadow->setYOffset(5);
        shadow->setColor(QColor(0, 0, 0, 70));
        m_container->setGraphicsEffect(shadow);
    }

    QWidget* container() const { return m_container; }
    QVBoxLayout* centralLayout() const { return m_centralLayout; }

    void setResizable(bool resizable) { m_resizable = resizable; }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        // Fallback for non-windows or if nativeEvent isn't used
        if (event->button() == Qt::LeftButton) {
            m_dragging = true;
            m_dragPos = event->globalPos() - frameGeometry().topLeft();
            event->accept();
        }
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (m_dragging && (event->buttons() & Qt::LeftButton)) {
            move(event->globalPos() - m_dragPos);
            event->accept();
        }
    }
    void mouseReleaseEvent(QMouseEvent*) override { m_dragging = false; }
    void keyPressEvent(QKeyEvent* event) override { QDialog::keyPressEvent(event); }

    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override {
#ifdef Q_OS_WIN
        MSG* msg = static_cast<MSG*>(message);
        if (msg->message == WM_NCHITTEST) {
            int border = 12; // Increased hit-test area for better feel
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
                if (child && (child->objectName() == "closeBtn" || 
                    (child->parentWidget() && child->parentWidget()->objectName() == "closeBtn"))) {
                    return false;
                }
                *result = HTCAPTION;
                return true;
            }
        }
#endif
        return QDialog::nativeEvent(eventType, message, result);
    }

private:
    QVBoxLayout* m_rootLayout;
    QWidget* m_container;
    QVBoxLayout* m_centralLayout;
    bool m_dragging = false;
    bool m_resizable = false;
    QPoint m_dragPos;
};
