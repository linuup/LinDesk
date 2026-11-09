#include "ui/Fluent/FluentDialog.h"
#include "app/AppFonts.h"
#include <QPainterPath>
#include <QEvent>
#include <QMouseEvent>
#include <QApplication>
#include <QScreen>

FluentDialog::FluentDialog(QWidget* parent) : QDialog(parent) {
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);

    QFont font = AppFonts::uiFont(10);
    setFont(font);

    setupUi();

    if (QWidget* topWin = hostTopWindow()) {
        topWin->installEventFilter(this);
    }
}

FluentDialog::~FluentDialog() = default;

QWidget* FluentDialog::hostTopWindow() const {
    if (!parentWidget()) {
        return nullptr;
    }
    QWidget* topWin = parentWidget()->window();
    return topWin ? topWin : parentWidget();
}

bool FluentDialog::hasVisibleHostWindow() const {
    QWidget* topWin = hostTopWindow();
    return topWin && topWin->isVisible() && !(topWin->windowState() & Qt::WindowMinimized);
}

void FluentDialog::setupUi() {
    // Card container is transparent; FluentDialog::paintEvent paints the exact
    // WinUI 3 ContentDialog layers (#FFFFFF body, #F3F3F3 CommandSpace, #E5E5E5 separator,
    // and 1px SurfaceStrokeColorDefault border) so child widgets never clip the border.
    m_contentWidget = new QWidget(this);
    m_contentWidget->setObjectName("contentWidget");
    m_contentWidget->setStyleSheet(QStringLiteral("background: transparent; border: none;"));

    m_cardLayout = new QVBoxLayout(m_contentWidget);
    m_cardLayout->setContentsMargins(1, 1, 1, 1);
    m_cardLayout->setSpacing(0);

    // WinUI 3 ContentDialog DialogSpace (ContentDialogPadding: 24, 24, 24, 24)
    QWidget* bodyContainer = new QWidget(m_contentWidget);
    bodyContainer->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    m_bodyLayout = new QVBoxLayout(bodyContainer);
    m_bodyLayout->setContentsMargins(24, 24, 24, 24);
    m_bodyLayout->setSpacing(12); // ContentDialogTitleMargin: 0, 0, 0, 12

    m_titleLabel = new QLabel(bodyContainer);
    m_titleLabel->setStyleSheet(QStringLiteral(
        "QLabel { background: transparent; font-size: 20px; font-weight: 600; color: #1b1b1b; }"));
    m_titleLabel->setWordWrap(true);
    m_titleLabel->setVisible(false);
    m_bodyLayout->addWidget(m_titleLabel);

    m_messageLabel = new QLabel(bodyContainer);
    m_messageLabel->setStyleSheet(QStringLiteral(
        "QLabel { background: transparent; font-size: 14px; font-weight: 400; color: #1b1b1b; }"));
    m_messageLabel->setWordWrap(true);
    m_messageLabel->setVisible(false);
    m_bodyLayout->addWidget(m_messageLabel);

    // Trailing spacer so contentLayout()->insertWidget(count() - 1, w) inserts above this spacer
    m_bodyLayout->addSpacing(2);

    m_cardLayout->addWidget(bodyContainer, 1);

    // WinUI 3 ContentDialog CommandSpace Footer (#F3F3F3 painted in paintEvent)
    m_footerWidget = new QWidget(m_contentWidget);
    m_footerWidget->setObjectName("fluentDialogFooter");
    m_footerWidget->setStyleSheet(QStringLiteral("background: transparent; border: none;"));

    m_buttonLayout = new QHBoxLayout(m_footerWidget);
    m_buttonLayout->setContentsMargins(24, 20, 24, 20);
    m_buttonLayout->setSpacing(8); // ContentDialogButtonSpacing: 8px

    m_cardLayout->addWidget(m_footerWidget);

    // WinUI 3 ContentDialog width constraints (MinWidth 320..360, MaxWidth 548)
    m_contentWidget->setMinimumWidth(368);
    m_contentWidget->setMaximumWidth(480);

    m_outerLayout = new QVBoxLayout(this);
    m_outerLayout->setContentsMargins(16, 16, 16, 16);
    m_outerLayout->setSpacing(0);
    m_outerLayout->addWidget(m_contentWidget, 0, Qt::AlignCenter);
}

void FluentDialog::setDialogTitle(const QString& title) {
    m_titleLabel->setText(title);
    m_titleLabel->setVisible(!title.isEmpty());
}

void FluentDialog::setMessage(const QString& msg) {
    m_messageLabel->setText(msg);
    m_messageLabel->setVisible(!msg.isEmpty());
}

QVBoxLayout* FluentDialog::contentLayout() const {
    return m_bodyLayout;
}

void FluentDialog::keepOpenOnce() {
    m_keepOpenOnce = true;
}

void FluentDialog::addActionButton(const QString& text, ActionType type, std::function<void()> callback) {
    FluentButton* button = new FluentButton(text, m_footerWidget);
    if (type == ActionType::Destructive) {
        button->setDestructive(true);
    } else {
        button->setAccent(type == ActionType::Primary);
    }

    if (type == ActionType::Primary || type == ActionType::Destructive) {
        button->setDefault(true);
        button->setFocus();
    }

    connect(button, &QPushButton::clicked, this, [this, type, callback]() {
        if (callback) callback();
        if (m_keepOpenOnce) {
            m_keepOpenOnce = false;
            return;
        }
        if (type == ActionType::Primary || type == ActionType::Destructive) {
            this->accept();
        } else {
            this->reject();
        }
    });

    m_actions.append({ button, type, callback });
    rebuildButtonLayout();
}

void FluentDialog::rebuildButtonLayout() {
    if (!m_buttonLayout) {
        return;
    }

    while (QLayoutItem* item = m_buttonLayout->takeAt(0)) {
        delete item;
    }

    // WinUI 3 ContentDialog CommandSpace grid:
    // - 1 option: right 50% width ([1* empty spacer] + [1* button])
    // - 2+ options: equal width columns across 100% of footer ([1* btn1] + [1* btn2] ...)
    if (m_actions.size() == 1) {
        m_buttonLayout->addStretch(1);
        m_actions[0].button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_buttonLayout->addWidget(m_actions[0].button, 1);
    } else {
        for (const ActionInfo& info : m_actions) {
            info.button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            m_buttonLayout->addWidget(info.button, 1);
        }
    }
}

void FluentDialog::paintEvent(QPaintEvent*) {
    if (!m_contentWidget) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 1. Official WinUI 3 Smoke Layer (SmokeFillColorDefault: #4D000000 = rgba(0, 0, 0, 77))
    // Dims the host window behind the modal ContentDialog so the #FFFFFF card stands out clearly.
    if (hasVisibleHostWindow()) {
        QWidget* topWin = hostTopWindow();
        const bool hostMaximized = topWin && (topWin->isMaximized() || (topWin->windowState() & Qt::WindowMaximized));
        const qreal hostRadius = hostMaximized ? 0.0 : 8.0;
        QPainterPath smokePath;
        smokePath.addRoundedRect(QRectF(rect()), hostRadius, hostRadius);
        painter.fillPath(smokePath, QColor(0, 0, 0, 77));
    }

    const QRectF cardRect = QRectF(m_contentWidget->geometry());
    const qreal cardRadius = 8.0; // WinUI 3 OverlayCornerRadius: 8px

    QPainterPath cardPath;
    cardPath.addRoundedRect(cardRect, cardRadius, cardRadius);

    // 2. Subtle WinUI 3 Elevation Shadow (strictly clipped outside cardPath so it NEVER darkens
    // the card border or corners, and with gentle alpha 3..8 so it never forms a black ring).
    QPainterPath outerBounds;
    outerBounds.addRect(QRectF(rect()));
    const QPainterPath outsideCardMask = outerBounds.subtracted(cardPath);

    painter.save();
    painter.setClipPath(outsideCardMask);
    painter.setPen(Qt::NoPen);
    for (int i = 6; i >= 1; --i) {
        const qreal spread = i * 1.8;
        const qreal offsetY = i * 0.7;
        const int alpha = (7 - i) * 3; // Max cumulative alpha ~18/255 (7% soft shadow, never a black halo)
        painter.setBrush(QColor(0, 0, 0, alpha));
        painter.drawRoundedRect(
            cardRect.adjusted(-spread, -spread * 0.5 + offsetY, spread, spread + offsetY),
            cardRadius + spread * 0.45,
            cardRadius + spread * 0.45
        );
    }
    painter.restore();

    // 3. Card Interior Surfaces (clipped to 8px rounded cardPath):
    //    - Upper DialogSpace (ContentDialogTopOverlay): #FFFFFF
    //    - Lower CommandSpace (ContentDialogBackground): #F3F3F3
    //    - Separator (ContentDialogSeparatorBorderBrush): 1px #E5E5E5
    painter.save();
    painter.setClipPath(cardPath);
    painter.fillRect(cardRect, QColor("#ffffff"));

    if (m_footerWidget && m_footerWidget->isVisible()) {
        const qreal footerTop = cardRect.top() + m_footerWidget->geometry().top();
        const QRectF footerRect(cardRect.left(), footerTop, cardRect.width(), cardRect.bottom() - footerTop);
        painter.fillRect(footerRect, QColor("#f3f3f3"));

        painter.setPen(QPen(QColor("#e5e5e5"), 1.0));
        painter.drawLine(QPointF(cardRect.left(), footerTop + 0.5), QPointF(cardRect.right(), footerTop + 0.5));
    }
    painter.restore();

    // 4. Official WinUI 3 ContentDialogBorderBrush (SurfaceStrokeColorDefault: #757575 @ 45% opacity)
    // Drawn AFTER the interior fills so the 1px border is 100% continuous and clearly visible.
    const QRectF borderRect = cardRect.adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(117, 117, 117, 128), 1.0));
    painter.drawRoundedRect(borderRect, cardRadius, cardRadius);
}

void FluentDialog::syncDialogGeometry() {
    if (hasVisibleHostWindow()) {
        QWidget* topWin = hostTopWindow();
        m_outerLayout->setContentsMargins(24, 24, 24, 24);
        m_contentWidget->adjustSize();
        setGeometry(topWin->geometry());
    } else {
        m_outerLayout->setContentsMargins(14, 14, 14, 14);
        if (layout()) {
            layout()->activate();
        }
        m_contentWidget->adjustSize();
        adjustSize();
        if (QScreen* screen = QApplication::primaryScreen()) {
            const QRect avail = screen->availableGeometry();
            move(avail.left() + (avail.width() - width()) / 2,
                 avail.top() + (avail.height() - height()) / 2);
        }
    }
}

void FluentDialog::showEvent(QShowEvent* event) {
    rebuildButtonLayout();
    syncDialogGeometry();

    raise();
    activateWindow();
    QDialog::showEvent(event);
}

void FluentDialog::mousePressEvent(QMouseEvent* event) {
    if (!hasVisibleHostWindow() && event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->globalPos() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QDialog::mousePressEvent(event);
}

void FluentDialog::mouseMoveEvent(QMouseEvent* event) {
    if (!hasVisibleHostWindow() && m_dragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPos() - m_dragOffset);
        event->accept();
        return;
    }
    QDialog::mouseMoveEvent(event);
}

void FluentDialog::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
    }
    QDialog::mouseReleaseEvent(event);
}

bool FluentDialog::eventFilter(QObject* obj, QEvent* event) {
    QWidget* topWin = hostTopWindow();
    if (obj == topWin && (event->type() == QEvent::Resize || event->type() == QEvent::Move)) {
        if (topWin && isVisible()) {
            syncDialogGeometry();
        }
    }
    return QDialog::eventFilter(obj, event);
}

void FluentDialog::showInfo(QWidget* parent, const QString& title, const QString& msg) {
    FluentDialog dlg(parent);
    dlg.setDialogTitle(title);
    dlg.setMessage(msg);
    dlg.addActionButton(tr("确定"), ActionType::Primary);
    dlg.exec();
}

void FluentDialog::showWarning(QWidget* parent, const QString& title, const QString& msg) {
    FluentDialog dlg(parent);
    dlg.setDialogTitle(title);
    dlg.setMessage(msg);
    dlg.addActionButton(tr("知道了"), ActionType::Primary);
    dlg.exec();
}

bool FluentDialog::confirm(QWidget* parent,
                           const QString& title,
                           const QString& msg,
                           const QString& confirmText,
                           const QString& cancelText,
                           bool destructive) {
    FluentDialog dlg(parent);
    dlg.setDialogTitle(title);
    dlg.setMessage(msg);
    bool accepted = false;
    dlg.addActionButton(
        confirmText.isEmpty() ? tr("确定") : confirmText,
        destructive ? ActionType::Destructive : ActionType::Primary,
        [&accepted]() { accepted = true; }
    );
    dlg.addActionButton(
        cancelText.isEmpty() ? tr("取消") : cancelText,
        ActionType::Secondary
    );
    dlg.exec();
    return accepted;
}
