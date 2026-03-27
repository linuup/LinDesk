#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QGraphicsDropShadowEffect>
#include <QMouseEvent>
#include <QPainter>

/**
 * @brief Base class for modern, frameless, rounded-corner windows with drop shadows.
 * Features a semi-transparent 'acrylic-like' background and easy dragging.
 */
class FluentWidget : public QWidget {
    Q_OBJECT
public:
    explicit FluentWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::Window);
        setAttribute(Qt::WA_TranslucentBackground);
        
        // --- Root Layout (to accommodate shadow margin) ---
        m_rootLayout = new QVBoxLayout(this);
        m_rootLayout->setContentsMargins(15, 15, 15, 15); // Space for shadow

        // --- Visible Container ---
        m_container = new QWidget(this);
        m_container->setObjectName("fluentContainer");
        m_container->setStyleSheet(
            "QWidget#fluentContainer { "
            "   background-color: rgba(255, 255, 255, 0.95); "
            "   border: 1px solid rgba(0, 0, 0, 0.1); "
            "   border-radius: 12px; "
            "} "
        );
        m_rootLayout->addWidget(m_container);

        // --- Central Layout (where sub-classes add content) ---
        m_centralLayout = new QVBoxLayout(m_container);
        m_centralLayout->setContentsMargins(0, 0, 0, 0);
        m_centralLayout->setSpacing(0);

        // --- Shadow Effect ---
        auto* shadow = new QGraphicsDropShadowEffect(this);
        shadow->setBlurRadius(25);
        shadow->setXOffset(0);
        shadow->setYOffset(6);
        shadow->setColor(QColor(0, 0, 0, 80));
        m_container->setGraphicsEffect(shadow);
    }

    QWidget* container() const { return m_container; }
    QVBoxLayout* centralLayout() const { return m_centralLayout; }

protected:
    void mousePressEvent(QMouseEvent* event) override {
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

    void mouseReleaseEvent(QMouseEvent*) override {
        m_dragging = false;
    }

    // Allow sub-classes to customize background or styles
    void setContainerStyle(const QString& style) {
        m_container->setStyleSheet(style);
    }

private:
    QVBoxLayout* m_rootLayout;
    QWidget* m_container;
    QVBoxLayout* m_centralLayout;
    
    bool m_dragging = false;
    QPoint m_dragPos;
};
