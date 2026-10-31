#include "ui/settings/navigation/FluentNavigationPanel.h"
#include "app/AppFonts.h"
#include <QIcon>
#include <QFontDatabase>

FluentNavigationPanel::FluentNavigationPanel(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFixedWidth(190);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    // Ensure Segoe Fluent Icons font is registered
    static bool fontLoaded = false;
    if (!fontLoaded) {
        fontLoaded = true;
        QFontDatabase::addApplicationFont(QStringLiteral(":/Assets/Segoe Fluent Icons.ttf"));
    }

    m_pillAnim = new QPropertyAnimation(this, "pillY", this);
    m_pillAnim->setDuration(220);
    m_pillAnim->setEasingCurve(QEasingCurve::OutCubic);

    initDefaultItems();
}

void FluentNavigationPanel::initDefaultItems()
{
    m_items.clear();
    addItem(0xE713, tr("General"));
    addItem(0xE7F8, tr("Window"));
    addItem(0xECAA, tr("Context Menu"));
    addItem(0xF133, tr("Box"));
    addItem(0xF4A5, tr("LinWidgets"));
    addItem(0xF22C, tr("Plugins"));
    addItem(0xE946, tr("About"));
}

void FluentNavigationPanel::updateTranslations()
{
    if (m_items.size() >= 7) {
        m_items[0].text = tr("General");
        m_items[1].text = tr("Window");
        m_items[2].text = tr("Context Menu");
        m_items[3].text = tr("Box");
        m_items[4].text = tr("LinWidgets");
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
    const int startY = kHeaderHeight + 8;
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

    // 1. Draw Brand Header (App icon + Title)
    const QPixmap appIcon = QIcon(":/Assets/icon.png").pixmap(30, 30);
    painter.drawPixmap(20, 22, appIcon);

    QFont titleFont(AppFonts::uiFamily(), 12, QFont::Bold);
    painter.setFont(titleFont);
    painter.setPen(QColor(17, 24, 39));
    painter.drawText(QRect(58, 20, width() - 65, 34), Qt::AlignVCenter | Qt::AlignLeft, tr("LinDesk"));

    // 2. Draw Navigation Items
    QFont iconFont(QStringLiteral("Segoe Fluent Icons"));
    if (iconFont.exactMatch() == false) {
        iconFont.setFamily(QStringLiteral("Segoe MDL2 Assets"));
    }
    iconFont.setPixelSize(16);

    QFont textFont(AppFonts::uiFamily(), 9);

    for (int i = 0; i < m_items.size(); ++i) {
        const ItemData& item = m_items[i];
        const bool isSelected = (i == m_currentIndex);
        const bool isHovered = item.isHovered;

        // Visual background card (margins: 8px left/right, 2px top/bottom)
        const QRect cardRect = item.rect.adjusted(8, 2, -8, -2);

        if (isSelected) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(234, 243, 255)); // Fluent light accent fill
            painter.drawRoundedRect(cardRect, 6, 6);
        } else if (isHovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 10)); // Soft hover fill
            painter.drawRoundedRect(cardRect, 6, 6);
        }

        // Draw item glyph icon
        painter.setFont(iconFont);
        if (isSelected) {
            painter.setPen(QColor(0, 120, 212));
        } else if (isHovered) {
            painter.setPen(QColor(31, 41, 55));
        } else {
            painter.setPen(QColor(107, 114, 128));
        }

        const QRect iconRect(cardRect.left() + 14, cardRect.top(), 24, cardRect.height());
        painter.drawText(iconRect, Qt::AlignCenter, QString(QChar(item.icon)));

        // Draw item text
        textFont.setBold(isSelected);
        painter.setFont(textFont);
        if (isSelected) {
            painter.setPen(QColor(0, 120, 212));
        } else if (isHovered) {
            painter.setPen(QColor(17, 24, 39));
        } else {
            painter.setPen(QColor(75, 85, 99));
        }

        const QRect textRect(cardRect.left() + 44, cardRect.top(), cardRect.width() - 48, cardRect.height());
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, item.text);
    }

    // 3. Draw Animated Indicator Pill (Active selection)
    if (!m_items.isEmpty() && m_currentIndex >= 0 && m_currentIndex < m_items.size()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 120, 212));
        painter.drawRoundedRect(QRectF(11, m_pillY, kPillWidth, kPillHeight), 1.5, 1.5);
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
                setCurrentIndex(i, true);
                event->accept();
                return;
            }
        }
    }
    QWidget::mousePressEvent(event);
}

void FluentNavigationPanel::leaveEvent(QEvent* event)
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].isHovered) {
            m_items[i].isHovered = false;
            update(m_items[i].rect);
        }
    }
    setCursor(Qt::ArrowCursor);
    QWidget::leaveEvent(event);
}
