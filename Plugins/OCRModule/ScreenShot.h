#pragma once
#include <QWidget>
#include <QPixmap>
#include <QScreen>
#include <QApplication>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QBuffer>
#include <QDebug>

class ScreenShot : public QWidget {
    Q_OBJECT
public:
    explicit ScreenShot(QWidget* parent = nullptr) : QWidget(parent) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        setAttribute(Qt::WA_DeleteOnClose, false);
        m_screen = QApplication::primaryScreen();
    }

    void start() {
        if (m_onUse) return;
        m_onUse = true;
        
        m_fullScreenPixmap = m_screen->grabWindow(0);
        m_maskPixmap = QPixmap(m_fullScreenPixmap.size());
        m_maskPixmap.fill(QColor(0, 0, 0, 100));
        
        m_displayPixmap = m_fullScreenPixmap;
        QPainter p(&m_displayPixmap);
        p.drawPixmap(0, 0, m_maskPixmap);
        
        m_selectionRect = QRect(); // Reset selection for each new shot
        
        setGeometry(m_screen->geometry());
        showFullScreen();
        raise();
        activateWindow();
    }

signals:
    void imageCaptured(const QImage& image);

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            m_startPos = event->pos();
            m_selectionRect = QRect(m_startPos, m_startPos);
            update();
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (event->buttons() & Qt::LeftButton) {
            m_selectionRect = QRect(m_startPos, event->pos()).normalized();
            update();
        }
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            m_onUse = false;
            if (m_selectionRect.width() > 5 && m_selectionRect.height() > 5) {
                QImage capture = m_fullScreenPixmap.copy(m_selectionRect).toImage();
                emit imageCaptured(capture);
            }
            close();
        }
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
            m_onUse = false;
            close();
        }
    }

    void paintEvent(QPaintEvent* event) override {
        QPainter painter(this);
        painter.drawPixmap(0, 0, m_displayPixmap);
        
        if (!m_selectionRect.isNull()) {
            painter.drawPixmap(m_selectionRect, m_fullScreenPixmap, m_selectionRect);
            painter.setPen(QPen(QColor(0, 120, 212), 2));
            painter.drawRect(m_selectionRect);
        }
    }

private:
    QScreen* m_screen = nullptr;
    QPixmap m_fullScreenPixmap;
    QPixmap m_maskPixmap;
    QPixmap m_displayPixmap;
    QPoint m_startPos;
    QRect m_selectionRect;
    bool m_onUse = false;
};
