#pragma once

#include <QWidget>

class QLabel;
class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QTimer;
class QWidget;

class FluentInfoTip : public QWidget
{
    Q_OBJECT
public:
    explicit FluentInfoTip(QWidget* parent = nullptr);

    void setText(const QString& text);
    void showAt(const QPoint& globalPos, int autoHideMs = 2500);
    void showCentered(const QRect& globalRect, int autoHideMs = 2500);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void animateTo(qreal targetOpacity);
    void updateMask();

    QWidget* m_panel = nullptr;
    QLabel* m_iconLabel = nullptr;
    QLabel* m_label = nullptr;
    QTimer* m_hideTimer = nullptr;
    QGraphicsOpacityEffect* m_opacityEffect = nullptr;
    QPropertyAnimation* m_opacityAnimation = nullptr;
};
