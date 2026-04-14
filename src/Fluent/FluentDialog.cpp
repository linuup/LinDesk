#include "FluentDialog.h"
#include <QPainterPath>
#include <QEvent>
#include <QApplication>
#include <QScreen>
#include <QGraphicsOpacityEffect>
#include <QFontDatabase>

FluentDialog::FluentDialog(QWidget* parent) : QDialog(parent) {
    // Basic setup: Modern frameless
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    
    // Smooth font (Segoe UI Variable is best on Win11)
    QFont font("Segoe UI Variable Display", 10);
    if (font.family() == "MS Shell Dlg 2") {
        font.setFamily("Segoe UI"); // Fallback for Win10
    }
    setFont(font);

    setupUi();
    
    // Install event filter on parent to track window resize (for backdrop)
    if (parent) {
        parent->installEventFilter(this);
    }
}

FluentDialog::~FluentDialog() {
}

void FluentDialog::setupUi() {
    // The Container Widget (The actual dialog body)
    m_contentWidget = new QWidget(this);
    m_contentWidget->setObjectName("contentWidget");
    m_contentWidget->setStyleSheet(
        "#contentWidget {"
        "  background-color: white;"
        "}"
    );

    m_mainLayout = new QVBoxLayout(m_contentWidget);
    m_mainLayout->setContentsMargins(24, 24, 24, 24);
    m_mainLayout->setSpacing(12);

    // Title
    m_titleLabel = new QLabel(this);
    m_titleLabel->setStyleSheet("font-size: 20px; font-weight: 600; color: #1f2937;");
    m_titleLabel->setWordWrap(true);
    m_mainLayout->addWidget(m_titleLabel);

    // Message
    m_messageLabel = new QLabel(this);
    m_messageLabel->setStyleSheet("font-size: 14px; color: #6b7280;");
    m_messageLabel->setWordWrap(true);
    m_mainLayout->addWidget(m_messageLabel);

    // Spacer between message and buttons
    m_mainLayout->addSpacing(8);

    // Buttons Layout
    m_buttonLayout = new QHBoxLayout();
    m_buttonLayout->addStretch();
    m_buttonLayout->setSpacing(8);
    m_mainLayout->addLayout(m_buttonLayout);

    // Consistency: Set minimum scale for the content dialog
    m_contentWidget->setMinimumWidth(400);
    m_contentWidget->setMaximumWidth(600);

    // Outer layout for positioning the content widget on top of the smoke layer
    QVBoxLayout* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(m_contentWidget, 0, Qt::AlignCenter);

    // Initial shadow effect (optional but recommended)
    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(m_contentWidget);
    shadow->setBlurRadius(20);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(0, 0, 0, 40));
    m_contentWidget->setGraphicsEffect(shadow);
}

void FluentDialog::setDialogTitle(const QString& title) {
    m_titleLabel->setText(title);
    m_titleLabel->setVisible(!title.isEmpty());
}

void FluentDialog::setMessage(const QString& msg) {
    m_messageLabel->setText(msg);
    m_messageLabel->setVisible(!msg.isEmpty());
}

void FluentDialog::addActionButton(const QString& text, ActionType type, std::function<void()> callback) {
    QPushButton* button = new QPushButton(text, m_contentWidget);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedHeight(32);
    button->setMinimumWidth(80);

    // Style the button based on Fluent standards
    if (type == ActionType::Primary) {
        button->setStyleSheet(
            "QPushButton {"
            "  background-color: #0078d4;" // Fluent Blue
            "  color: white;"
            "  border: none;"
            "  border-radius: 6px;"
            "  padding: 6px 24px;"
            "  font-weight: 500;"
            "}"
            "QPushButton:hover { background-color: #006cbd; }"
            "QPushButton:pressed { background-color: #005a9e; }"
        );
    } else if (type == ActionType::Secondary) {
        button->setStyleSheet(
            "QPushButton {"
            "  background-color: #ffffff;"
            "  border: 1px solid #e5e7eb;"
            "  border-radius: 6px;"
            "  padding: 6px 24px;"
            "  color: #1f2937;"
            "}"
            "QPushButton:hover { background-color: #f9fafb; border-color: #d1d5db; }"
            "QPushButton:pressed { background-color: #f3f4f6; }"
        );
    } else { // ActionType::Close
        button->setStyleSheet(
            "QPushButton {"
            "  background-color: transparent;"
            "  border: none;"
            "  border-radius: 6px;"
            "  padding: 6px 24px;"
            "  color: #6b7280;"
            "}"
            "QPushButton:hover { background-color: rgba(0, 0, 0, 0.06); }"
            "QPushButton:pressed { background-color: rgba(0, 0, 0, 0.1); }"
        );
    }

    m_buttonLayout->addWidget(button);
    
    // Connect click logic
    connect(button, &QPushButton::clicked, this, [this, callback]() {
        if (callback) callback();
        this->accept(); // Close dialog on any action click by default
    });

    m_actions.append({ button, type, callback });
}

void FluentDialog::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // No smoke backdrop - keep background transparent
    // painter.fillRect(rect(), QColor(0, 0, 0, 80)); // Disabled: removed backdrop overlay

    // The content widget paints itself via stylesheets, but we can add secondary effects here if needed
}

void FluentDialog::showEvent(QShowEvent* event) {
    // Standard Fluent behavior: The dialog background (smoke) covers the full parent area or full screen.
    if (parentWidget() && parentWidget()->isVisible()) {
        setGeometry(parentWidget()->geometry());
    } else {
        // Fallback to screen geometry
        setGeometry(QApplication::primaryScreen()->geometry());
    }
    
    // Ensure the dialog is REALLY at the top level and catches the eye
    raise();
    activateWindow();

    // Subtle entry animation (scale/fade)
    QGraphicsOpacityEffect* opacityEffect = new QGraphicsOpacityEffect(m_contentWidget);
    m_contentWidget->setGraphicsEffect(opacityEffect);
    
    QPropertyAnimation* fadeAnim = new QPropertyAnimation(opacityEffect, "opacity");
    fadeAnim->setDuration(220); // Slightly slower for elegance
    fadeAnim->setStartValue(0);
    fadeAnim->setEndValue(1);
    fadeAnim->setEasingCurve(QEasingCurve::OutCubic);
    fadeAnim->start(QAbstractAnimation::DeleteWhenStopped);

    QDialog::showEvent(event);
}

bool FluentDialog::eventFilter(QObject* obj, QEvent* event) {
    if (obj == parentWidget() && event->type() == QEvent::Resize) {
        if (parentWidget()) setGeometry(parentWidget()->geometry());
    }
    return QDialog::eventFilter(obj, event);
}

// Static shortcut
void FluentDialog::showInfo(QWidget* parent, const QString& title, const QString& msg) {
    FluentDialog* dlg = new FluentDialog(parent);
    dlg->setDialogTitle(title);
    dlg->setMessage(msg);
    dlg->addActionButton("OK", ActionType::Primary);
    dlg->exec();
    dlg->deleteLater();
}
