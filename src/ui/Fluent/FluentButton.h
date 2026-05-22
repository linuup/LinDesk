#pragma once
#include <QPushButton>
#include <QPainter>
#include <QVariantAnimation>
#include <QEnterEvent>

class FluentButton : public QPushButton {
    Q_OBJECT
    Q_PROPERTY(QColor bgColor READ bgColor WRITE setBgColor)

public:
    explicit FluentButton(const QString& text, QWidget* parent = nullptr) : QPushButton(text, parent) {
        setFixedSize(120, 32);
        setCursor(Qt::PointingHandCursor);
        
        m_hoverAnim = new QVariantAnimation(this);
        m_hoverAnim->setDuration(150);
        m_hoverAnim->setStartValue(m_normalColor);
        m_hoverAnim->setEndValue(m_hoverColor);
        connect(m_hoverAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v){
            m_bgColor = v.value<QColor>();
            update();
        });
    }

    QColor bgColor() const { return m_bgColor; }
    void setBgColor(const QColor& c) { m_bgColor = c; update(); }

    void setAccent(bool accent) {
        m_accent = accent;
        if (m_accent) {
            setColors(QColor(0, 120, 212), QColor(0, 100, 180), Qt::white, QColor());
        } else {
            setColors(QColor(239, 246, 255), QColor(219, 234, 254), QColor(31, 41, 55), QColor(147, 197, 253));
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
        m_hoverAnim->setDirection(QAbstractAnimation::Backward);
        m_hoverAnim->start();
        QPushButton::leaveEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // 1. Background
        const QColor disabledBg(243, 243, 243);
        const QColor disabledBorder(218, 218, 218);
        const QColor disabledText(161, 161, 161);

        p.setBrush(isEnabled() ? m_bgColor : disabledBg);
        if (isEnabled()) {
            p.setPen(m_borderColor.isValid() ? QPen(m_borderColor, 1) : Qt::NoPen);
        } else {
            p.setPen(QPen(disabledBorder, 1));
        }
        p.drawRoundedRect(rect().adjusted(1,1,-1,-1), 6, 6);

        // 2. Text
        p.setPen(isEnabled() ? m_textColor : disabledText);
        QFont f = font();
        f.setPixelSize(13);
        f.setWeight(QFont::Medium);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter, text());
    }

private:
    QColor m_bgColor;
    QColor m_normalColor = QColor(0, 120, 212);
    QColor m_hoverColor = QColor(0, 100, 180);
    QColor m_textColor = Qt::white;
    QColor m_borderColor;
    QVariantAnimation* m_hoverAnim;
    bool m_accent = true;
};
