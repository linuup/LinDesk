#pragma once
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QFontMetrics>

class FluentButton : public QPushButton {
    Q_OBJECT
    Q_PROPERTY(QColor bgColor READ bgColor WRITE setBgColor)

public:
    explicit FluentButton(const QString& text, QWidget* parent = nullptr) : QPushButton(text, parent) {
        setMinimumSize(88, 32);
        setFixedHeight(32);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setCursor(Qt::PointingHandCursor);

        m_hoverAnim = new QVariantAnimation(this);
        m_hoverAnim->setDuration(110);
        m_hoverAnim->setStartValue(m_normalColor);
        m_hoverAnim->setEndValue(m_hoverColor);
        connect(m_hoverAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            if (!m_pressed) {
                m_bgColor = v.value<QColor>();
                update();
            }
        });
    }

    QSize sizeHint() const override {
        QFont f = font();
        f.setPixelSize(14);
        f.setWeight(QFont::Normal);
        QFontMetrics fm(f);
        const int textWidth = fm.horizontalAdvance(text());
        return QSize(qMax(96, textWidth + 32), 32);
    }

    QColor bgColor() const { return m_bgColor; }
    void setBgColor(const QColor& c) { m_bgColor = c; update(); }

    void setAccent(bool accent) {
        m_accent = accent;
        m_destructive = false;
        if (m_accent) {
            setColors(QColor("#0067c0"), QColor("#1975c5"), QColor("#ffffff"), QColor("#005fb8"));
            m_pressedColor = QColor("#005299");
            m_bottomBorderColor = QColor("#003e73");
        } else {
            // WinUI 3 standard Button on #f3f3f3 CommandSpace surface
            setColors(QColor("#ffffff"), QColor("#f6f6f6"), QColor("#1b1b1b"), QColor("#e5e5e5"));
            m_pressedColor = QColor("#ededed");
            m_bottomBorderColor = QColor("#cccccc");
        }
    }

    void setDestructive(bool destructive) {
        m_destructive = destructive;
        if (destructive) {
            m_accent = true;
            setColors(QColor("#c42b1c"), QColor("#b52517"), QColor("#ffffff"), QColor("#b02315"));
            m_pressedColor = QColor("#a21d10");
            m_bottomBorderColor = QColor("#7a150b");
        } else {
            setAccent(m_accent);
        }
    }

    void setColors(const QColor& normal, const QColor& hover, const QColor& text, const QColor& border = QColor()) {
        m_normalColor = normal;
        m_hoverColor = hover;
        m_textColor = text;
        m_borderColor = border;
        m_bgColor = normal;
        m_hoverAnim->setStartValue(normal);
        m_hoverAnim->setEndValue(hover);
        update();
    }

protected:
    void changeEvent(QEvent* event) override {
        if (event->type() == QEvent::EnabledChange) {
            if (!isEnabled()) {
                m_hoverAnim->stop();
                m_pressed = false;
                m_bgColor = m_normalColor;
            }
            setCursor(isEnabled() ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update();
        }
        QPushButton::changeEvent(event);
    }

    void enterEvent(QEvent* event) override {
        if (!isEnabled()) {
            QPushButton::enterEvent(event);
            return;
        }
        m_hoverAnim->setDirection(QAbstractAnimation::Forward);
        m_hoverAnim->start();
        QPushButton::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        if (!isEnabled()) {
            QPushButton::leaveEvent(event);
            return;
        }
        m_pressed = false;
        m_hoverAnim->setDirection(QAbstractAnimation::Backward);
        m_hoverAnim->start();
        QPushButton::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (isEnabled() && event->button() == Qt::LeftButton) {
            m_pressed = true;
            update();
        }
        QPushButton::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (m_pressed) {
            m_pressed = false;
            update();
        }
        QPushButton::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QColor disabledBg = m_accent ? QColor(0, 0, 0, 55) : QColor(245, 245, 245);
        const QColor disabledBorder(224, 224, 224);
        const QColor disabledText = m_accent ? QColor(255, 255, 255, 180) : QColor(158, 158, 158);

        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = 4.0;

        QColor fillColor = m_bgColor;
        if (!isEnabled()) {
            fillColor = disabledBg;
        } else if (m_pressed) {
            fillColor = m_pressedColor;
        }

        // Fill + main stroke
        p.setBrush(fillColor);
        if (isEnabled()) {
            p.setPen(m_borderColor.isValid() ? QPen(m_borderColor, 1.0) : Qt::NoPen);
        } else {
            p.setPen(QPen(disabledBorder, 1.0));
        }
        p.drawRoundedRect(r, radius, radius);

        // WinUI 3 bottom elevation lip (omitted when pressed or disabled)
        if (isEnabled() && !m_pressed && m_bottomBorderColor.isValid()) {
            QPainterPath clipPath;
            clipPath.addRoundedRect(QRectF(rect()), radius, radius);
            p.save();
            p.setClipPath(clipPath);
            p.setPen(QPen(m_bottomBorderColor, 1.0));
            p.drawLine(QPointF(r.left() + 1.5, r.bottom()), QPointF(r.right() - 1.5, r.bottom()));
            p.restore();
        }

        // Button label
        QColor txtColor = m_textColor;
        if (!isEnabled()) {
            txtColor = disabledText;
        } else if (m_pressed) {
            txtColor = m_accent ? QColor(255, 255, 255, 210) : QColor("#5c5c5c");
        }
        p.setPen(txtColor);
        QFont f = font();
        f.setPixelSize(14);
        f.setWeight(QFont::Normal);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter, text());
    }

private:
    QColor m_bgColor = QColor("#0067c0");
    QColor m_normalColor = QColor("#0067c0");
    QColor m_hoverColor = QColor("#1975c5");
    QColor m_pressedColor = QColor("#005299");
    QColor m_textColor = Qt::white;
    QColor m_borderColor = QColor("#005fb8");
    QColor m_bottomBorderColor = QColor("#003e73");
    QVariantAnimation* m_hoverAnim = nullptr;
    bool m_accent = true;
    bool m_destructive = false;
    bool m_pressed = false;
};
