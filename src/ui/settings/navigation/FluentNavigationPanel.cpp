#include "ui/settings/navigation/FluentNavigationPanel.h"
#include "app/AppFonts.h"
#include <QIcon>
#include <QFontDatabase>

FluentNavigationPanel::FluentNavigationPanel(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFixedWidth(216);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    // Ensure Segoe Fluent Icons font is registered
    static bool fontLoaded = false;
    if (!fontLoaded) {
        fontLoaded = true;
        QFontDatabase::addApplicationFont(QStringLiteral(":/Assets/Segoe Fluent Icons.ttf"));
    }

    m_pillAnim = new QPropertyAnimation(this, "pillY", this);
    m_pillAnim->setDuration(180);
    m_pillAnim->setEasingCurve(QEasingCurve::OutCubic);

    initDefaultItems();
}

void FluentNavigationPanel::initDefaultItems()
{
    m_items.clear();
    addItem(0xE713, tr("General"));
    addItem(0xE7F8, tr("Desktop"));
    addItem(0xECAA, tr("Context Menu"));
    addItem(0xF133, tr("Box"));
    addItem(0xEB9F, tr("壁纸与小组件"));
    addItem(0xF22C, tr("Plugins"));
    addItem(0xE946, tr("About"));
}

void FluentNavigationPanel::updateTranslations()
{
    if (m_items.size() >= 7) {
        m_items[0].text = tr("General");
        m_items[1].text = tr("Desktop");
        m_items[2].text = tr("Context Menu");
        m_items[3].text = tr("Box");
        m_items[4].text = tr("壁纸与小组件");
        m_items[5].text = tr("Plugins");
        m_items[6].text = tr("About");
        update();
    }
}

void FluentNavigationPanel::addItem(char16_t iconGlyph, const QString& text)
{
    ItemData data;
    data.icon = iconGlyph;
    data.text = text;
    m_items.append(data);
    updateItemGeometries();

    if (m_items.size() == 1) {
        m_pillY = targetPillYForIndex(0);
    }
    update();
}

void FluentNavigationPanel::setCurrentIndex(int index, bool animated)
{
    if (index < 0 || index >= m_items.size()) {
        return;
    }

    const bool changed = (m_currentIndex != index);
    m_currentIndex = index;

    const qreal targetY = targetPillYForIndex(index);
    if (animated && isVisible()) {
        m_pillAnim->stop();
        m_pillAnim->setStartValue(m_pillY);
        m_pillAnim->setEndValue(targetY);
        m_pillAnim->start();
    } else {
        setPillY(targetY);
    }

    if (changed) {
        emit currentIndexChanged(m_currentIndex);
    }
    update();
}

void FluentNavigationPanel::setPillY(qreal y)
{
    m_pillY = y;
    update();
}

qreal FluentNavigationPanel::targetPillYForIndex(int index) const
{
    if (index < 0 || index >= m_items.size()) {
        return 0.0;
    }
    const QRect& r = m_items[index].rect;
    return r.top() + (r.height() - kPillHeight) / 2.0;
}

void FluentNavigationPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateItemGeometries();
    if (m_pillAnim->state() != QAbstractAnimation::Running) {
        m_pillY = targetPillYForIndex(m_currentIndex);
    }
}

void FluentNavigationPanel::updateItemGeometries()
{
    const int startY = kHeaderHeight + 4;
    const int widthAvailable = width();

    for (int i = 0; i < m_items.size(); ++i) {
        const int itemY = startY + i * (kItemHeight + kItemSpacing);
        m_items[i].rect = QRect(0, itemY, widthAvailable, kItemHeight);
    }
}

void FluentNavigationPanel::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // 1. Draw Brand Header (App icon + Title aligned with Win11 NavigationView header)
    const QPixmap appIcon = QIcon(QStringLiteral(":/Assets/icon.png")).pixmap(22, 22);
    painter.drawPixmap(18, 17, appIcon);

    QFont titleFont(AppFonts::uiFamily());
    titleFont.setPixelSize(13);
    titleFont.setWeight(QFont::DemiBold);
    painter.setFont(titleFont);
    painter.setPen(QColor(0x1b, 0x1b, 0x1b));
    painter.drawText(QRect(48, 12, width() - 56, 32), Qt::AlignVCenter | Qt::AlignLeft, tr("LinDesk"));

    // 2. Draw Navigation Items (Authentic WinUI 3 NavigationViewItem)
    QFont iconFont(QStringLiteral("Segoe Fluent Icons"));
    if (!iconFont.exactMatch()) {
        iconFont.setFamily(QStringLiteral("Segoe MDL2 Assets"));
    }
    iconFont.setPixelSize(16);

    QFont textFont(AppFonts::uiFamily());
    textFont.setPixelSize(13);

    for (int i = 0; i < m_items.size(); ++i) {
        const ItemData& item = m_items[i];
        const bool isSelected = (i == m_currentIndex);
        const bool isHovered = item.isHovered;
        const bool isPressed = item.isPressed;

        // Visual background card (margins: 10px left/right, 2px top/bottom -> 36px visual height)
        const QRect cardRect = item.rect.adjusted(10, 2, -10, -2);

        if (isSelected) {
            painter.setPen(Qt::NoPen);
            // Win11 NavigationView selected item neutral fill
            painter.setBrush(isHovered ? QColor(0, 0, 0, 12) : QColor(0, 0, 0, 16));
            painter.drawRoundedRect(cardRect, 6, 6);
        } else if (isPressed) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 14));
            painter.drawRoundedRect(cardRect, 6, 6);
        } else if (isHovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 10));
            painter.drawRoundedRect(cardRect, 6, 6);
        }

        // Draw item glyph icon
        painter.setFont(iconFont);
        if (isSelected) {
            painter.setPen(QColor(0x00, 0x67, 0xc0)); // Win11 Accent #0067c0
        } else {
            painter.setPen(isPressed ? QColor(0x5c, 0x5c, 0x5c) : QColor(0x1b, 0x1b, 0x1b));
        }

        const QRect iconRect(cardRect.left() + 12, cardRect.top(), 20, cardRect.height());
        painter.drawText(iconRect, Qt::AlignCenter, QString(QChar(item.icon)));

        // Draw item text
        textFont.setWeight(isSelected ? QFont::DemiBold : QFont::Normal);
        painter.setFont(textFont);
        painter.setPen(isPressed ? QColor(0x5c, 0x5c, 0x5c) : QColor(0x1b, 0x1b, 0x1b));

        const QRect textRect(cardRect.left() + 42, cardRect.top(), cardRect.width() - 48, cardRect.height());
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, item.text);
    }

    // 3. Draw Animated Indicator Pill (Active selection: 3px x 16px #0067c0)
    if (!m_items.isEmpty() && m_currentIndex >= 0 && m_currentIndex < m_items.size()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0x00, 0x67, 0xc0));
        painter.drawRoundedRect(QRectF(10.0, m_pillY, kPillWidth, kPillHeight), 1.5, 1.5);
    }
}

void FluentNavigationPanel::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint pos = event->pos();
    bool anyHovered = false;

    for (int i = 0; i < m_items.size(); ++i) {
        const bool hovered = m_items[i].rect.contains(pos);
        if (m_items[i].isHovered != hovered) {
            m_items[i].isHovered = hovered;
            update(m_items[i].rect);
        }
        if (hovered) {
            anyHovered = true;
        }
    }

    setCursor(anyHovered ? Qt::PointingHandCursor : Qt::ArrowCursor);
    QWidget::mouseMoveEvent(event);
}

void FluentNavigationPanel::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        const QPoint pos = event->pos();
        for (int i = 0; i < m_items.size(); ++i) {
            if (m_items[i].rect.contains(pos)) {
                m_items[i].isPressed = true;
                setCurrentIndex(i, true);
                event->accept();
                return;
            }
        }
    }
    QWidget::mousePressEvent(event);
}

void FluentNavigationPanel::mouseReleaseEvent(QMouseEvent* event)
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].isPressed) {
            m_items[i].isPressed = false;
            update(m_items[i].rect);
        }
    }
    QWidget::mouseReleaseEvent(event);
}

void FluentNavigationPanel::leaveEvent(QEvent* event)
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].isHovered || m_items[i].isPressed) {
            m_items[i].isHovered = false;
            m_items[i].isPressed = false;
            update(m_items[i].rect);
        }
    }
    setCursor(Qt::ArrowCursor);
    QWidget::leaveEvent(event);
}
