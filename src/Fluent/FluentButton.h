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
        m_bgColor = QColor(0, 120, 212); // Accent Blue
        
        m_hoverAnim = new QVariantAnimation(this);
        m_hoverAnim->setDuration(150);
        m_hoverAnim->setStartValue(QColor(0, 120, 212));
        m_hoverAnim->setEndValue(QColor(16, 110, 190)); // Darker on hover
        connect(m_hoverAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v){
            m_bgColor = v.value<QColor>();
            update();
        });
    }

    QColor bgColor() const { return m_bgColor; }
    void setBgColor(const QColor& c) { m_bgColor = c; update(); }

    void setAccent(bool accent) {
        m_accent = accent;
        if (!m_accent) {
            m_bgColor = QColor(243, 243, 243);
            m_hoverAnim->setStartValue(QColor(243, 243, 243));
            m_hoverAnim->setEndValue(QColor(235, 235, 235));
        }
    }

protected:
    void enterEvent(QEvent* event) override {
        m_hoverAnim->setDirection(QAbstractAnimation::Forward);
        m_hoverAnim->start();
        QPushButton::enterEvent(event);
    }
    void leaveEvent(QEvent* event) override {
        m_hoverAnim->setDirection(QAbstractAnimation::Backward);
        m_hoverAnim->start();
        QPushButton::leaveEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // 1. Background
        p.setBrush(m_bgColor);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(rect().adjusted(1,1,-1,-1), 6, 6);

        // 2. Text
        p.setPen(m_accent ? Qt::white : QColor(32, 32, 32));
        QFont f = font();
        f.setPixelSize(13);
        f.setWeight(QFont::Medium);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter, text());
    }

private:
    QColor m_bgColor;
    QVariantAnimation* m_hoverAnim;
    bool m_accent = true;
};
