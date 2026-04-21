#include "ui/Fluent/FluentInfoTip.h"
#include "app/AppFonts.h"

#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QTimer>
#include <QWidget>
#include <windows.h>

FluentInfoTip::FluentInfoTip(QWidget* parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(0);

    m_panel = new QWidget(this);
    m_panel->setObjectName("infoPanel");
    m_panel->setAttribute(Qt::WA_StyledBackground, true);
    m_panel->setStyleSheet(QStringLiteral(
        "QWidget#infoPanel {"
        "  background-color: rgba(248, 250, 255, 248);"
        "  border: 1px solid rgba(0, 120, 212, 70);"
        "  border-radius: 14px;"
        "}"
        "QLabel#infoBadge {"
        "  background-color: #0078d4;"
        "  color: white;"
        "  border-radius: 12px;"
        "  font-size: 14px;"
        "  font-weight: 700;"
        "  font-family: %1;"
        "}"
        "QLabel#infoText {"
        "  background: transparent;"
        "  color: #1f1f1f;"
        "  font-size: 13px;"
        "  font-weight: 500;"
        "  font-family: %1;"
        "}"
    ).arg(AppFonts::uiCssFamily()));

    QHBoxLayout* panelLayout = new QHBoxLayout(m_panel);
    panelLayout->setContentsMargins(14, 12, 16, 12);
    panelLayout->setSpacing(12);

    m_iconLabel = new QLabel(m_panel);
    m_iconLabel->setObjectName("infoBadge");
    m_iconLabel->setFixedSize(24, 24);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setText("i");

    m_label = new QLabel(m_panel);
    m_label->setObjectName("infoText");
    m_label->setWordWrap(true);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setFixedWidth(420);

    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(m_panel);
    shadow->setBlurRadius(28);
    shadow->setColor(QColor(15, 23, 42, 70));
    shadow->setOffset(0, 10);
    m_panel->setGraphicsEffect(shadow);

    panelLayout->addWidget(m_iconLabel, 0, Qt::AlignVCenter);
    panelLayout->addWidget(m_label, 1, Qt::AlignVCenter);
    layout->addWidget(m_panel);

    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    connect(m_hideTimer, &QTimer::timeout, this, [this]() {
        animateTo(0.0);
    });

    m_opacityEffect = new QGraphicsOpacityEffect(this);
    m_opacityEffect->setOpacity(0.0);
    setGraphicsEffect(m_opacityEffect);

    m_opacityAnimation = new QPropertyAnimation(m_opacityEffect, "opacity", this);
    m_opacityAnimation->setDuration(180);
    m_opacityAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_opacityAnimation, &QPropertyAnimation::finished, this, [this]() {
        if (m_opacityEffect && m_opacityEffect->opacity() <= 0.01) {
            hide();
        }
    });
}

void FluentInfoTip::setText(const QString& text)
{
    m_label->setText(text);
    m_panel->adjustSize();
    adjustSize();
    updateMask();
}

void FluentInfoTip::showAt(const QPoint& globalPos, int autoHideMs)
{
    m_hideTimer->stop();
    m_opacityAnimation->stop();
    adjustSize();
    move(globalPos);
    show();
    raise();
    animateTo(1.0);

    if (HWND hwnd = reinterpret_cast<HWND>(winId())) {
        SetWindowPos(
            hwnd,
            HWND_TOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW
        );
    }

    if (autoHideMs > 0) {
        m_hideTimer->start(autoHideMs);
    } else {
        m_hideTimer->stop();
    }
}

void FluentInfoTip::showCentered(const QRect& globalRect, int autoHideMs)
{
    adjustSize();
    const QPoint centeredPos(
        globalRect.left() + (globalRect.width() - width()) / 2,
        globalRect.top() + (globalRect.height() - height()) / 2);
    showAt(centeredPos, autoHideMs);
}

void FluentInfoTip::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateMask();
}

void FluentInfoTip::animateTo(qreal targetOpacity)
{
    if (!m_opacityEffect || !m_opacityAnimation) {
        return;
    }

    m_opacityAnimation->stop();
    m_opacityAnimation->setStartValue(m_opacityEffect->opacity());
    m_opacityAnimation->setEndValue(targetOpacity);
    m_opacityAnimation->start();
}

void FluentInfoTip::updateMask()
{
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()), 14, 14);
    setMask(QRegion(path.toFillPolygon().toPolygon()));
}
