#pragma once
#include <QScrollArea>
#include <QScrollBar>
#include <QPropertyAnimation>
#include <QWheelEvent>
#include <QEasingCurve>

/**
 * @brief SmoothScrollArea provides buttery-smooth, physics-eased mouse wheel scrolling
 * inspired by QFluentWidgets and Windows 11 Fluent Design.
 * 
 * Features:
 * - Velocity/accumulator model: multiple rapid wheel ticks smoothly accumulate target delta.
 * - OutCubic easing curve for organic deceleration without jarring jumps.
 * - Viewport WA_OpaquePaintEvent optimization for hardware BitBlt blitting.
 */
class SmoothScrollArea : public QScrollArea {
    Q_OBJECT
    Q_PROPERTY(int smoothValue READ smoothValue WRITE setSmoothValue)
public:
    explicit SmoothScrollArea(QWidget* parent = nullptr)
        : QScrollArea(parent) {
        setWidgetResizable(true);
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

        if (viewport()) {
            viewport()->setAttribute(Qt::WA_OpaquePaintEvent, true);
        }

        m_animation = new QPropertyAnimation(this, "smoothValue", this);
        m_animation->setDuration(220);
        m_animation->setEasingCurve(QEasingCurve::OutCubic);
    }

    int smoothValue() const {
        return verticalScrollBar() ? verticalScrollBar()->value() : 0;
    }

    void setSmoothValue(int value) {
        if (verticalScrollBar()) {
            verticalScrollBar()->setValue(value);
        }
    }

protected:
    void wheelEvent(QWheelEvent* event) override {
        QScrollBar* bar = verticalScrollBar();
        if (!bar || bar->maximum() <= bar->minimum()) {
            QScrollArea::wheelEvent(event);
            return;
        }

        const int numDegrees = event->angleDelta().y();
        if (numDegrees == 0) {
            QScrollArea::wheelEvent(event);
            return;
        }

        // Standard mouse wheel delta is 120 per notch; map to comfortable 100px scroll step
        const int numSteps = numDegrees / 120;
        constexpr int kStepSize = 100;
        int scrollDelta = 0;
        if (numSteps != 0) {
            scrollDelta = -numSteps * kStepSize;
        } else {
            scrollDelta = -(numDegrees * kStepSize) / 120;
        }

        // Accumulate targets when user rapidly spins the wheel
        const int baseValue = (m_animation->state() == QAbstractAnimation::Running) ? m_targetValue : bar->value();
        m_targetValue = qBound(bar->minimum(), baseValue + scrollDelta, bar->maximum());

        m_animation->stop();
        m_animation->setStartValue(bar->value());
        m_animation->setEndValue(m_targetValue);
        m_animation->start();

        event->accept();
    }

private:
    QPropertyAnimation* m_animation = nullptr;
    int m_targetValue = 0;
};
