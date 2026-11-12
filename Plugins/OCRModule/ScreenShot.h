#pragma once
#include <QWidget>
#include <QPixmap>
#include <QScreen>
#include <QApplication>
#include <QClipboard>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTimer>
#include <QLabel>
#include <QHBoxLayout>
#include <QPushButton>

class ScreenShot : public QWidget {
    Q_OBJECT
public:
    enum class CaptureMode {
        Ocr,            // Screenshot for OCR recognition
        ScreenshotOnly  // Screenshot and copy original image directly to clipboard
    };

    explicit ScreenShot(QWidget* parent = nullptr) : QWidget(parent) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        setAttribute(Qt::WA_DeleteOnClose, false);
        setAttribute(Qt::WA_OpaquePaintEvent, true);
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);
        setFocusPolicy(Qt::StrongFocus);
        m_screen = QApplication::primaryScreen();
    }

    void start(CaptureMode mode = CaptureMode::Ocr) {
        if (m_onUse) return;
        m_mode = mode;
        m_screen = QApplication::primaryScreen();
        if (!m_screen) return;
        m_onUse = true;
        
        m_fullScreenPixmap = m_screen->grabWindow(0);
        if (m_fullScreenPixmap.isNull()) {
            m_onUse = false;
            return;
        }

        m_maskPixmap = QPixmap(m_fullScreenPixmap.size());
        m_maskPixmap.fill(QColor(0, 0, 0, 110));
        
        m_displayPixmap = m_fullScreenPixmap;
        QPainter p(&m_displayPixmap);
        p.drawPixmap(0, 0, m_maskPixmap);
        
        m_selectionRect = QRect();
        m_isSelecting = false;
        
        setGeometry(m_screen->geometry());
        showFullScreen();
        raise();
        activateWindow();
        setFocus();
    }

signals:
    void imageCaptured(const QImage& image);
    void screenshotCopied(const QImage& image);

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            m_startPos = event->pos();
            m_selectionRect = QRect(m_startPos, m_startPos);
            m_isSelecting = true;
            update();
        } else if (event->button() == Qt::RightButton) {
            // Right-click cancels screenshot
            m_onUse = false;
            close();
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
            m_isSelecting = false;
            const QRect captureRect = m_selectionRect;
            if (captureRect.width() > 8 && captureRect.height() > 8) {
                // Keep selection visible and finalize or auto-finish based on mode
                finishCapture(captureRect);
            } else {
                update();
            }
        }
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            // Double click: immediately copy original image to clipboard and exit
            const QRect captureRect = m_selectionRect.isNull() ? rect() : m_selectionRect;
            copyOriginalImageToClipboard(captureRect);
        }
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
            m_onUse = false;
            close();
        } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if (!m_selectionRect.isNull() && m_selectionRect.width() > 5 && m_selectionRect.height() > 5) {
                finishCapture(m_selectionRect);
            }
        } else if (event->matches(QKeySequence::Copy)) {
            if (!m_selectionRect.isNull()) {
                copyOriginalImageToClipboard(m_selectionRect);
            }
        }
    }

    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.drawPixmap(0, 0, m_displayPixmap);
        
        if (!m_selectionRect.isNull() && m_selectionRect.width() > 1 && m_selectionRect.height() > 1) {
            // 1. Draw clear un-masked original image area
            painter.drawPixmap(m_selectionRect, m_fullScreenPixmap, m_selectionRect);
            
            // 2. Strict Sharp Rectangle Border (No rounded rect as requested)
            painter.setRenderHint(QPainter::Antialiasing, false);
            
            // Outer high-contrast white border
            painter.setPen(QPen(QColor(255, 255, 255, 240), 1.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(m_selectionRect);

            // Inner WinUI 3 accent border (#0067c0)
            painter.setPen(QPen(QColor(0, 103, 192), 1.5));
            painter.drawRect(m_selectionRect.adjusted(1, 1, -1, -1));

            // 3. Crisp rectangular anchor handles at 4 corners
            const int handleSize = 6;
            painter.setBrush(QColor(255, 255, 255));
            painter.setPen(QPen(QColor(0, 103, 192), 1.0));
            
            const QRect r = m_selectionRect;
            // Top-left, Top-right, Bottom-left, Bottom-right
            painter.drawRect(r.left(), r.top(), handleSize, handleSize);
            painter.drawRect(r.right() - handleSize + 1, r.top(), handleSize, handleSize);
            painter.drawRect(r.left(), r.bottom() - handleSize + 1, handleSize, handleSize);
            painter.drawRect(r.right() - handleSize + 1, r.bottom() - handleSize + 1, handleSize, handleSize);

            // 4. Size Info Pill at top-left
            const QString sizeText = QStringLiteral(" %1 × %2 ").arg(r.width()).arg(r.height());
            painter.setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::DemiBold));
            QFontMetrics fm = painter.fontMetrics();
            int badgeW = fm.horizontalAdvance(sizeText) + 8;
            int badgeH = fm.height() + 4;
            int badgeY = (r.top() - badgeH - 4 >= 0) ? (r.top() - badgeH - 4) : (r.top() + 6);
            int badgeX = r.left();

            painter.setBrush(QColor(30, 30, 30, 220));
            painter.setPen(Qt::NoPen);
            painter.drawRect(badgeX, badgeY, badgeW, badgeH);

            painter.setPen(QColor(255, 255, 255));
            painter.drawText(QRect(badgeX, badgeY, badgeW, badgeH), Qt::AlignCenter, sizeText);
        }
    }

private:
    void finishCapture(const QRect& captureRect) {
        m_onUse = false;
        close();

        QTimer::singleShot(10, this, [this, captureRect]() {
            if (m_fullScreenPixmap.isNull()) return;

            const qreal dpr = m_fullScreenPixmap.devicePixelRatio();
            QRect cropRect = captureRect;
            if (dpr > 1.01 || dpr < 0.99) {
                cropRect = QRect(
                    qRound(captureRect.x() * dpr),
                    qRound(captureRect.y() * dpr),
                    qRound(captureRect.width() * dpr),
                    qRound(captureRect.height() * dpr)
                );
            }

            QPixmap cropped = m_fullScreenPixmap.copy(cropRect);
            cropped.setDevicePixelRatio(1.0);
            QImage capture = cropped.toImage();

            if (m_mode == CaptureMode::ScreenshotOnly) {
                // Direct Clipboard writing
                QClipboard* clipboard = QApplication::clipboard();
                clipboard->setPixmap(cropped);
                clipboard->setImage(capture);
                emit screenshotCopied(capture);
            } else {
                emit imageCaptured(capture);
            }
        });
    }

    void copyOriginalImageToClipboard(const QRect& captureRect) {
        m_onUse = false;
        close();

        if (m_fullScreenPixmap.isNull()) return;
        const qreal dpr = m_fullScreenPixmap.devicePixelRatio();
        QRect cropRect = captureRect;
        if (dpr > 1.01 || dpr < 0.99) {
            cropRect = QRect(
                qRound(captureRect.x() * dpr),
                qRound(captureRect.y() * dpr),
                qRound(captureRect.width() * dpr),
                qRound(captureRect.height() * dpr)
            );
        }
        QPixmap cropped = m_fullScreenPixmap.copy(cropRect);
        cropped.setDevicePixelRatio(1.0);
        QImage capture = cropped.toImage();

        QClipboard* clipboard = QApplication::clipboard();
        clipboard->setPixmap(cropped);
        clipboard->setImage(capture);
        emit screenshotCopied(capture);
    }

    QScreen* m_screen = nullptr;
    QPixmap m_fullScreenPixmap;
    QPixmap m_maskPixmap;
    QPixmap m_displayPixmap;
    QPoint m_startPos;
    QRect m_selectionRect;
    bool m_onUse = false;
    bool m_isSelecting = false;
    CaptureMode m_mode = CaptureMode::Ocr;
};
