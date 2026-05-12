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
#include <QTimer>

class ScreenShot : public QWidget {
    Q_OBJECT
public:
    explicit ScreenShot(QWidget* parent = nullptr) : QWidget(parent) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        setAttribute(Qt::WA_DeleteOnClose, false);
        setAttribute(Qt::WA_OpaquePaintEvent, true);
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);
        setFocusPolicy(Qt::StrongFocus);
        m_screen = QApplication::primaryScreen();
    }

    void start() {
        if (m_onUse) return;
        m_screen = QApplication::primaryScreen();
        if (!m_screen) {
            return;
        }
        m_onUse = true;
        
        m_fullScreenPixmap = m_screen->grabWindow(0);
        if (m_fullScreenPixmap.isNull()) {
            m_onUse = false;
            return;
        }
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
        setFocus();
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
            const QRect captureRect = m_selectionRect;
            close();
            if (captureRect.width() > 5 && captureRect.height() > 5) {
                QTimer::singleShot(0, this, [this, captureRect]() {
                    if (m_fullScreenPixmap.isNull()) {
                        return;
                    }
                    QImage capture = m_fullScreenPixmap.copy(captureRect).toImage();
                    emit imageCaptured(capture);
                });
            }
        }
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
            m_onUse = false;
            close();
        }
    }

    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.drawPixmap(0, 0, m_displayPixmap);
        
        if (!m_selectionRect.isNull()) {
            painter.drawPixmap(m_selectionRect, m_fullScreenPixmap, m_selectionRect);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(QPen(QColor(255, 255, 255, 235), 2.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(m_selectionRect.adjusted(0, 0, -1, -1), 4.0, 4.0);
            painter.setPen(QPen(QColor(11, 127, 143, 230), 1.0));
            painter.drawRoundedRect(m_selectionRect.adjusted(2, 2, -3, -3), 3.0, 3.0);
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
