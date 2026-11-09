#include "ui/Fluent/FluentInfoTip.h"
#include "app/AppFonts.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QTimer>
#include <QWidget>
#include <windows.h>
#include <dwmapi.h>

FluentInfoTip::FluentInfoTip(QWidget* parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput | Qt::NoDropShadowWindowHint)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_panel = new QWidget(this);
    m_panel->setObjectName("infoPanel");
    m_panel->setAttribute(Qt::WA_StyledBackground, true);
    m_panel->setStyleSheet(QStringLiteral(
        "QWidget#infoPanel {"
        "  background-color: #ffffff;"
        "  border: 1px solid #d6d6d6;"
        "  border-radius: 8px;"
        "}"
        "QLabel#infoBadge {"
        "  background-color: #0067c0;"
        "  color: white;"
        "  border-radius: 10px;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "  font-family: %1;"
        "}"
        "QLabel#infoText {"
        "  background: transparent;"
        "  color: #1b1b1b;"
        "  font-size: 13px;"
        "  font-weight: 400;"
        "  font-family: %1;"
        "}"
    ).arg(AppFonts::uiCssFamily()));

    QHBoxLayout* panelLayout = new QHBoxLayout(m_panel);
    panelLayout->setContentsMargins(16, 12, 18, 12);
    panelLayout->setSpacing(12);

    m_iconLabel = new QLabel(m_panel);
    m_iconLabel->setObjectName("infoBadge");
    m_iconLabel->setFixedSize(20, 20);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setText("i");

    m_label = new QLabel(m_panel);
    m_label->setObjectName("infoText");
    m_label->setWordWrap(true);
    m_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_label->setFixedWidth(400);

    panelLayout->addWidget(m_iconLabel, 0, Qt::AlignVCenter);
    panelLayout->addWidget(m_label, 1, Qt::AlignVCenter);
    layout->addWidget(m_panel);

    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    connect(m_hideTimer, &QTimer::timeout, this, [this]() {
        animateTo(0.0);
    });

    setWindowOpacity(0.0);

    m_opacityAnimation = new QPropertyAnimation(this, "windowOpacity", this);
    m_opacityAnimation->setDuration(160);
    m_opacityAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_opacityAnimation, &QPropertyAnimation::finished, this, [this]() {
        if (windowOpacity() <= 0.01) {
            hide();
        }
    });
}

void FluentInfoTip::setText(const QString& text)
{
    m_label->setText(text);
    m_panel->adjustSize();
    adjustSize();
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
        const DWORD cornerPref = 2; // DWMWCP_ROUND
        DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &cornerPref, sizeof(cornerPref));
        const COLORREF borderColor = RGB(0xD6, 0xD6, 0xD6);
        DwmSetWindowAttribute(hwnd, 34 /* DWMWA_BORDER_COLOR */, &borderColor, sizeof(borderColor));

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
}

void FluentInfoTip::animateTo(qreal targetOpacity)
{
    if (!m_opacityAnimation) {
        return;
    }

    m_opacityAnimation->stop();
    m_opacityAnimation->setStartValue(windowOpacity());
    m_opacityAnimation->setEndValue(targetOpacity);
    m_opacityAnimation->start();
}

void FluentInfoTip::updateMask()
{
}
