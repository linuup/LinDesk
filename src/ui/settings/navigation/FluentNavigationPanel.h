#pragma once
#include <QWidget>
#include <QVector>
#include <QString>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>

class FluentNavigationItem;

/**
 * @brief Windows 11 WinUI 3 Fluent NavigationView Panel with animated indicator pill.
 * Features:
 * - Brand header aligned with window title bar
 * - Unified 7-item vertical menu list with authentic neutral selection fill
 * - Animated sliding indicator pill on active selection (OutCubic interpolation)
 * - Subtle hover & press micro-interactions
 */
class FluentNavigationPanel : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal pillY READ pillY WRITE setPillY)

public:
    explicit FluentNavigationPanel(QWidget* parent = nullptr);
    ~FluentNavigationPanel() override = default;

    void addItem(char16_t iconGlyph, const QString& text, const QPixmap& iconPixmap = QPixmap());
    void initDefaultItems();
    void updateTranslations();
    int currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int index, bool animated = true);

    qreal pillY() const { return m_pillY; }
    void setPillY(qreal y);

signals:
    void currentIndexChanged(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateItemGeometries();
    qreal targetPillYForIndex(int index) const;

    struct ItemData {
        char16_t icon = 0;
        QString text;
        QPixmap iconPixmap;
        QRect rect;
        bool isHovered = false;
        bool isPressed = false;
    };

    QVector<ItemData> m_items;
    int m_currentIndex = 0;
    qreal m_pillY = 0.0;
    QPropertyAnimation* m_pillAnim = nullptr;

    const int kHeaderHeight = 56;
    const int kItemHeight = 40;
    const int kItemSpacing = 4;
    const int kPillHeight = 16;
    const int kPillWidth = 3;

protected:
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
};
