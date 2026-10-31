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
 * @brief Modern Windows 11 Fluent Navigation Panel with an animated indicator pill.
 * Features:
 * - Brand header with app icon & title
 * - Unified 7-item vertical menu list
 * - Animated sliding indicator pill on active selection (OutCubic interpolation)
 * - Subtle hover & press micro-interactions
 */
class FluentNavigationPanel : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal pillY READ pillY WRITE setPillY)

public:
    explicit FluentNavigationPanel(QWidget* parent = nullptr);
    ~FluentNavigationPanel() override = default;

    void addItem(char16_t iconGlyph, const QString& text);
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
        char16_t icon;
        QString text;
        QRect rect;
        bool isHovered = false;
    };

    QVector<ItemData> m_items;
    int m_currentIndex = 0;
    qreal m_pillY = 0.0;
    QPropertyAnimation* m_pillAnim = nullptr;

    const int kHeaderHeight = 72;
    const int kItemHeight = 44;
    const int kItemSpacing = 4;
    const int kPillHeight = 18;
    const int kPillWidth = 3;

protected:
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
};
