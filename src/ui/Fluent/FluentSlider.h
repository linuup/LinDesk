#pragma once

#include <QSlider>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QtMath>
#include <QStyleOptionSlider>

/**
 * @brief FluentSlider - Windows 11 Fluent Design 2 Slider Component
 *
 * Implements Microsoft Fluent 2 slider specification:
 * - 4px rounded pill track with Accent / Neutral states
 * - Dual-layer thumb (18px white circle with delicate shadow + centered 10px accent core dot)
 * - Micro-interaction feedback: smooth hover growth & press micro-shrink
 * - Immediate click-to-position and buttery smooth continuous dragging
 * - Full QSlider API compatibility
 */
class FluentSlider : public QSlider {
    Q_OBJECT
    Q_PROPERTY(qreal visualPos READ visualPos WRITE setVisualPos)
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)
    Q_PROPERTY(qreal pressProgress READ pressProgress WRITE setPressProgress)

public:
    explicit FluentSlider(QWidget* parent = nullptr)
        : FluentSlider(Qt::Horizontal, parent) {}

    explicit FluentSlider(Qt::Orientation orientation, QWidget* parent = nullptr)
        : QSlider(orientation, parent)
    {
        setAttribute(Qt::WA_Hover, true);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        setTracking(true);
        setMouseTracking(true);

        // Reset inherited QSlider stylesheet rules that might interfere with custom painting
        setStyleSheet(QStringLiteral(
            "FluentSlider {"
            "   background: transparent;"
            "   border: none;"
            "}"
            "FluentSlider::groove:horizontal, FluentSlider::groove:vertical {"
            "   background: transparent;"
            "   border: none;"
            "   height: 0px;"
            "}"
            "FluentSlider::sub-page:horizontal, FluentSlider::sub-page:vertical {"
            "   background: transparent;"
            "   border: none;"
            "}"
            "FluentSlider::handle:horizontal, FluentSlider::handle:vertical {"
            "   background: transparent;"
            "   border: none;"
            "   width: 0px;"
            "   height: 0px;"
            "}"
        ));

        // Hover scale animation
        m_hoverAnim = new QVariantAnimation(this);
        m_hoverAnim->setDuration(140);
        m_hoverAnim->setEasingCurve(QEasingCurve::OutQuad);
        connect(m_hoverAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& val) {
            m_hoverProgress = val.toReal();
            update();
        });

        // Press / drag physical feedback animation
        m_pressAnim = new QVariantAnimation(this);
        m_pressAnim->setDuration(100);
        m_pressAnim->setEasingCurve(QEasingCurve::OutQuad);
        connect(m_pressAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& val) {
            m_pressProgress = val.toReal();
            update();
        });

        // Click-to-jump easing animation
        m_jumpAnim = new QVariantAnimation(this);
        m_jumpAnim->setDuration(120);
        m_jumpAnim->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_jumpAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& val) {
            m_visualPos = val.toReal();
            update();
        });
    }

    ~FluentSlider() override = default;

    QSize sizeHint() const override {
        return orientation() == Qt::Horizontal ? QSize(140, 24) : QSize(24, 140);
    }

    QSize minimumSizeHint() const override {
        return orientation() == Qt::Horizontal ? QSize(50, 20) : QSize(20, 50);
    }

    qreal visualPos() const { return m_visualPos; }
    void setVisualPos(qreal p) {
        m_visualPos = qBound(0.0, p, 1.0);
        update();
    }

    qreal hoverProgress() const { return m_hoverProgress; }
    void setHoverProgress(qreal p) {
        m_hoverProgress = qBound(0.0, p, 1.0);
        update();
    }

    qreal pressProgress() const { return m_pressProgress; }
    void setPressProgress(qreal p) {
        m_pressProgress = qBound(0.0, p, 1.0);
        update();
    }

    QColor accentColor() const { return m_accentColor; }
    void setAccentColor(const QColor& color) {
        m_accentColor = color;
        update();
    }

protected:
    void sliderChange(SliderChange change) override {
        QSlider::sliderChange(change);
        if ((change == SliderValueChange || change == SliderRangeChange) && !m_isDragging) {
            syncVisualPosFromValue();
        }
    }

    void resizeEvent(QResizeEvent* event) override {
        QSlider::resizeEvent(event);
        if (!m_isDragging) {
            syncVisualPosFromValue();
        }
    }

    void enterEvent(QEvent* event) override {
        QSlider::enterEvent(event);
        m_hoverAnim->stop();
        m_hoverAnim->setStartValue(m_hoverProgress);
        m_hoverAnim->setEndValue(1.0);
        m_hoverAnim->start();
    }

    void leaveEvent(QEvent* event) override {
        QSlider::leaveEvent(event);
        m_hoverAnim->stop();
        m_hoverAnim->setStartValue(m_hoverProgress);
        m_hoverAnim->setEndValue(0.0);
        m_hoverAnim->start();
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (!isEnabled() || event->button() != Qt::LeftButton) {
            QSlider::mousePressEvent(event);
            return;
        }

        m_jumpAnim->stop();
        m_isDragging = true;
        setSliderDown(true);

        m_pressAnim->stop();
        m_pressAnim->setStartValue(m_pressProgress);
        m_pressAnim->setEndValue(1.0);
        m_pressAnim->start();

        const qreal ratio = ratioFromMousePos(event->pos());
        m_visualPos = ratio;
        const int targetVal = valueFromRatio(ratio);
        if (targetVal != value()) {
            setValue(targetVal);
            emit sliderMoved(targetVal);
        }
        emit sliderPressed();
        update();
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!isEnabled()) {
            QSlider::mouseMoveEvent(event);
            return;
        }

        if (m_isDragging && (event->buttons() & Qt::LeftButton)) {
            m_jumpAnim->stop();
            const qreal ratio = ratioFromMousePos(event->pos());
            m_visualPos = ratio;
            const int targetVal = valueFromRatio(ratio);
            if (targetVal != value()) {
                setValue(targetVal);
                emit sliderMoved(targetVal);
            }
            update();
            event->accept();
            return;
        }

        QSlider::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && m_isDragging) {
            m_isDragging = false;
            setSliderDown(false);

            m_pressAnim->stop();
            m_pressAnim->setStartValue(m_pressProgress);
            m_pressAnim->setEndValue(0.0);
            m_pressAnim->start();

            syncVisualPosFromValue();

            emit sliderReleased();
            update();
            event->accept();
            return;
        }

        QSlider::mouseReleaseEvent(event);
    }

    void wheelEvent(QWheelEvent* event) override {
        if (!isEnabled()) {
            event->ignore();
            return;
        }
        const int numDegrees = event->angleDelta().y();
        if (numDegrees == 0) {
            event->ignore();
            return;
        }
        const int steps = numDegrees / 120;
        const int stepVal = singleStep() > 0 ? singleStep() : 1;
        const int newVal = qBound(minimum(), value() + steps * stepVal, maximum());
        if (newVal != value()) {
            setValue(newVal);
            emit sliderMoved(newVal);
            if (maximum() > minimum()) {
                const qreal targetRatio = (newVal - minimum()) / qreal(maximum() - minimum());
                m_jumpAnim->stop();
                m_jumpAnim->setStartValue(m_visualPos);
                m_jumpAnim->setEndValue(targetRatio);
                m_jumpAnim->start();
            }
        }
        event->accept();
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const bool isHoriz = (orientation() == Qt::Horizontal);
        const qreal margin = m_trackMargin;
        const qreal trackLength = isHoriz ? (width() - margin * 2.0) : (height() - margin * 2.0);
        if (trackLength <= 0.0) return;

        const qreal trackThickness = 4.0;
        const qreal trackRadius = 2.0;

        // Dynamic thumb sizing based on hover and press states (Fluent 2)
        // Outer white circle: Normal 18px (r=9.0), Hover 20px (r=10.0), Press 15px (r=7.5)
        const qreal outerRadius = 9.0 + (1.0 * m_hoverProgress) - (1.5 * m_pressProgress);
        // Inner accent dot: Normal 10px (r=5.0), Hover 12px (r=6.0), Press 7.6px (r=3.8)
        const qreal innerRadius = 5.0 + (1.0 * m_hoverProgress) - (1.2 * m_pressProgress);

        const bool enabled = isEnabled();
        QColor activeColor = enabled ? m_accentColor : QColor(199, 224, 244);
        if (enabled) {
            if (m_pressProgress > 0.05) {
                activeColor = m_accentColor.darker(115);
            } else if (m_hoverProgress > 0.05) {
                activeColor = m_accentColor.lighter(112);
            }
        }

        const QColor inactiveColor = enabled 
            ? (m_hoverProgress > 0.05 ? QColor(206, 212, 220) : QColor(222, 226, 232))
            : QColor(238, 240, 244);

        if (isHoriz) {
            const qreal cy = height() / 2.0;
            const qreal thumbX = margin + m_visualPos * trackLength;

            // 1. Inactive Track (Full track groove)
            const QRectF grooveRect(margin, cy - trackThickness / 2.0, trackLength, trackThickness);
            p.setPen(Qt::NoPen);
            p.setBrush(inactiveColor);
            p.drawRoundedRect(grooveRect, trackRadius, trackRadius);

            // 2. Active Track (From left margin up to thumb center)
            const qreal activeWidth = thumbX - margin;
            if (activeWidth > 0.0) {
                const QRectF activeRect(margin, cy - trackThickness / 2.0, activeWidth, trackThickness);
                p.setBrush(activeColor);
                p.drawRoundedRect(activeRect, trackRadius, trackRadius);
            }

            // 3. Thumb Soft Elevation Drop Shadow
            if (enabled) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0, 0, 0, 16));
                p.drawEllipse(QPointF(thumbX, cy + 1.6), outerRadius + 1.2, outerRadius + 1.2);
                p.setBrush(QColor(0, 0, 0, 32));
                p.drawEllipse(QPointF(thumbX, cy + 0.8), outerRadius + 0.2, outerRadius + 0.2);
            }

            // 4. Thumb Outer Pill (White with fine outline)
            p.setPen(QPen(QColor(0, 0, 0, enabled ? 34 : 20), 1.0));
            p.setBrush(enabled ? Qt::white : QColor(245, 247, 250));
            p.drawEllipse(QPointF(thumbX, cy), outerRadius, outerRadius);

            // 5. Thumb Inner Accent Core Dot
            p.setPen(Qt::NoPen);
            p.setBrush(enabled ? activeColor : QColor(186, 194, 204));
            p.drawEllipse(QPointF(thumbX, cy), innerRadius, innerRadius);

            // 6. Keyboard Focus Ring
            if (hasFocus() && enabled) {
                p.setPen(QPen(m_accentColor, 1.5, Qt::DashLine));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(QPointF(thumbX, cy), outerRadius + 3.0, outerRadius + 3.0);
            }
        } else {
            // Vertical Slider
            const qreal cx = width() / 2.0;
            const qreal thumbY = margin + (1.0 - m_visualPos) * trackLength;

            // 1. Inactive Track
            const QRectF grooveRect(cx - trackThickness / 2.0, margin, trackThickness, trackLength);
            p.setPen(Qt::NoPen);
            p.setBrush(inactiveColor);
            p.drawRoundedRect(grooveRect, trackRadius, trackRadius);

            // 2. Active Track
            const qreal activeHeight = (height() - margin) - thumbY;
            if (activeHeight > 0.0) {
                const QRectF activeRect(cx - trackThickness / 2.0, thumbY, trackThickness, activeHeight);
                p.setBrush(activeColor);
                p.drawRoundedRect(activeRect, trackRadius, trackRadius);
            }

            // 3. Thumb Soft Elevation Drop Shadow
            if (enabled) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0, 0, 0, 16));
                p.drawEllipse(QPointF(cx, thumbY + 1.6), outerRadius + 1.2, outerRadius + 1.2);
                p.setBrush(QColor(0, 0, 0, 32));
                p.drawEllipse(QPointF(cx, thumbY + 0.8), outerRadius + 0.2, outerRadius + 0.2);
            }

            // 4. Thumb Outer Pill
            p.setPen(QPen(QColor(0, 0, 0, enabled ? 34 : 20), 1.0));
            p.setBrush(enabled ? Qt::white : QColor(245, 247, 250));
            p.drawEllipse(QPointF(cx, thumbY), outerRadius, outerRadius);

            // 5. Thumb Inner Accent Core Dot
            p.setPen(Qt::NoPen);
            p.setBrush(enabled ? activeColor : QColor(186, 194, 204));
            p.drawEllipse(QPointF(cx, thumbY), innerRadius, innerRadius);

            // 6. Keyboard Focus Ring
            if (hasFocus() && enabled) {
                p.setPen(QPen(m_accentColor, 1.5, Qt::DashLine));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(QPointF(cx, thumbY), outerRadius + 3.0, outerRadius + 3.0);
            }
        }
    }

private:
    qreal ratioFromMousePos(const QPoint& pos) const {
        const bool isHoriz = (orientation() == Qt::Horizontal);
        const qreal margin = m_trackMargin;
        if (isHoriz) {
            const qreal trackLength = qMax(1.0, width() - margin * 2.0);
            return qBound(0.0, (pos.x() - margin) / trackLength, 1.0);
        } else {
            const qreal trackLength = qMax(1.0, height() - margin * 2.0);
            return qBound(0.0, 1.0 - (pos.y() - margin) / trackLength, 1.0);
        }
    }

    int valueFromRatio(qreal ratio) const {
        if (maximum() <= minimum()) return minimum();
        return minimum() + qRound(ratio * (maximum() - minimum()));
    }

    void syncVisualPosFromValue() {
        if (maximum() > minimum()) {
            m_visualPos = qBound(0.0, (value() - minimum()) / qreal(maximum() - minimum()), 1.0);
        } else {
            m_visualPos = 0.0;
        }
        update();
    }

    const qreal m_trackMargin = 10.0;
    qreal m_visualPos = 0.0;
    qreal m_hoverProgress = 0.0;
    qreal m_pressProgress = 0.0;
    bool m_isDragging = false;
    QColor m_accentColor = QColor(0, 120, 212); // Fluent Blue #0078d4

    QVariantAnimation* m_hoverAnim = nullptr;
    QVariantAnimation* m_pressAnim = nullptr;
    QVariantAnimation* m_jumpAnim = nullptr;
};
