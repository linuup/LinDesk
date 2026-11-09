#include "ui/settings/SettingsUICommon.h"
#include "services/SysInfoUtil.h"
#include <QDateTime>
#include <QPainter>
#include <QLinearGradient>

namespace SettingsUICommon {

QLabel* createPageTitle(const QString& text, QWidget* parent)
{
    QLabel* label = new QLabel(text, parent);
    label->setObjectName("pageTitle");
    return label;
}

QLabel* createPageSubtitle(const QString& text, QWidget* parent)
{
    QLabel* label = new QLabel(text, parent);
    label->setObjectName("pageSubtitle");
    label->setWordWrap(true);
    return label;
}

QLabel* createCardTitle(const QString& text, QWidget* parent)
{
    QLabel* label = new QLabel(text, parent);
    label->setObjectName("cardTitle");
    label->setWordWrap(true);
    label->setMinimumWidth(0);
    label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    return label;
}

QLabel* createCardSubtitle(const QString& text, QWidget* parent)
{
    QLabel* label = new QLabel(text, parent);
    label->setObjectName("cardSubtitle");
    label->setWordWrap(true);
    return label;
}

QWidget* createPanel(QWidget* parent, const QString& objectName)
{
    QWidget* panel = new QWidget(parent);
    panel->setObjectName(objectName);
    panel->setAttribute(Qt::WA_StyledBackground, true);
    panel->setMinimumWidth(0);
    panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    return panel;
}

QLabel* createGlyphLabel(const QString& glyphText, QWidget* parent, int pixelSize, const QColor& color)
{
    QLabel* label = new QLabel(glyphText, parent);
    label->setObjectName("glyphLabel");
    label->setAlignment(Qt::AlignCenter);
    label->setFixedSize(pixelSize + 10, pixelSize + 10);
    label->setStyleSheet(QString(
        "QLabel#glyphLabel {"
        " background: transparent;"
        " color: %1;"
        " font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets';"
        " font-size: %2px;"
        "}").arg(color.name()).arg(pixelSize));
    return label;
}

QFrame* createDivider(QWidget* parent)
{
    QFrame* divider = new QFrame(parent);
    divider->setFrameShape(QFrame::HLine);
    divider->setFixedHeight(1);
    divider->setObjectName("softDivider");
    return divider;
}

QPushButton* createSettingsActionButton(const QString& text, QWidget* parent, bool accent, bool destructive)
{
    QPushButton* button = new QPushButton(text, parent);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedHeight(32);
    button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    if (accent) {
        button->setStyleSheet(QStringLiteral(
            "QPushButton {"
            "  background: #0067c0;"
            "  color: #ffffff;"
            "  border: 1px solid #005fb8;"
            "  border-radius: 6px;"
            "  padding: 0 14px;"
            "  font-size: 13px;"
            "  font-weight: 600;"
            "}"
            "QPushButton:hover { background: #1975c5; border-color: #146bb8; }"
            "QPushButton:pressed { background: #005299; }"
            "QPushButton:disabled { background: #e5e5e5; border-color: #d6d6d6; color: #9e9e9e; }"));
    } else if (destructive) {
        button->setStyleSheet(QStringLiteral(
            "QPushButton {"
            "  background: #fbfbfb;"
            "  color: #c42b1c;"
            "  border: 1px solid #e0e0e0;"
            "  border-radius: 6px;"
            "  padding: 0 14px;"
            "  font-size: 13px;"
            "  font-weight: 500;"
            "}"
            "QPushButton:hover { background: #fdf2f2; border-color: #f1b8b4; }"
            "QPushButton:pressed { background: #fae5e5; }"
            "QPushButton:disabled { background: #f3f3f3; border-color: #e5e5e5; color: #a6a6a6; }"));
    } else {
        button->setStyleSheet(QStringLiteral(
            "QPushButton {"
            "  background: #fbfbfb;"
            "  color: #1b1b1b;"
            "  border: 1px solid #d6d6d6;"
            "  border-bottom: 1px solid #c2c2c2;"
            "  border-radius: 6px;"
            "  padding: 0 14px;"
            "  font-size: 13px;"
            "  font-weight: 500;"
            "}"
            "QPushButton:hover { background: #f3f3f3; border-color: #c8c8c8; }"
            "QPushButton:pressed { background: #ebebeb; color: #5c5c5c; }"
            "QPushButton:disabled { background: #f3f3f3; border-color: #e5e5e5; color: #a6a6a6; }"));
    }
    return button;
}

QWidget* createSwitchRow(const QString& icon,
                         const QString& title,
                         const QString& subtitle,
                         FluentToggleSwitch* toggle,
                         QWidget* parent)
{
    QWidget* row = new QWidget(parent);
    row->setObjectName("settingsRow");
    row->setAttribute(Qt::WA_StyledBackground, true);
    row->setMinimumWidth(0);
    row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QHBoxLayout* layout = new QHBoxLayout(row);
    layout->setContentsMargins(16, 13, 16, 13);
    layout->setSpacing(14);

    layout->addWidget(createGlyphLabel(icon, row, 18, QColor("#1b1b1b")), 0, Qt::AlignVCenter);

    QVBoxLayout* textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);

    QLabel* titleLabel = new QLabel(title, row);
    titleLabel->setObjectName("rowTitle");
    titleLabel->setWordWrap(true);
    titleLabel->setMinimumWidth(0);
    titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QLabel* subtitleLabel = new QLabel(subtitle, row);
    subtitleLabel->setObjectName("rowSubtitle");
    subtitleLabel->setWordWrap(true);
    subtitleLabel->setMinimumWidth(0);
    subtitleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    textLayout->addWidget(titleLabel);
    textLayout->addWidget(subtitleLabel);

    layout->addLayout(textLayout, 1);
    if (toggle) {
        layout->addWidget(toggle, 0, Qt::AlignVCenter);
    }

    return row;
}

QPushButton* createColorPreset(const QColor& color, QWidget* parent)
{
    QPushButton* button = new QPushButton(parent);
    button->setObjectName("colorPreset");
    button->setFixedSize(28, 28);
    button->setCursor(Qt::PointingHandCursor);
    button->setProperty("presetColor", color.name());
    button->setStyleSheet(QString(
        "QPushButton#colorPreset {"
        " background: %1;"
        " border: 2px solid rgba(255,255,255,0.96);"
        " border-radius: 6px;"
        "}"
        "QPushButton#colorPreset:hover {"
        " border: 2px solid #0067c0;"
        "}").arg(color.name()));
    return button;
}

void updateColorPresetIndicators(QWidget* container, const QColor& selectedColor)
{
    if (!container) {
        return;
    }

    const QString selectedName = selectedColor.name().toLower();
    const QList<QLabel*> indicators = container->findChildren<QLabel*>(QStringLiteral("colorPresetIndicator"));
    bool matchedPreset = false;
    for (QLabel* indicator : indicators) {
        const QString presetName = indicator->property("presetColor").toString().toLower();
        const bool isPresetMatch = !presetName.isEmpty() && presetName == selectedName;
        if (isPresetMatch) {
            matchedPreset = true;
        }
        indicator->setVisible(isPresetMatch);
    }

    for (QLabel* indicator : indicators) {
        if (indicator->property("customColorIndicator").toBool()) {
            indicator->setVisible(!matchedPreset);
        }
    }
}

QWidget* createHomeTile(const QString& icon, const QString& title, const QString& subtitle, QWidget* parent)
{
    QWidget* tile = new QWidget(parent);
    tile->setObjectName("homeTile");
    tile->setAttribute(Qt::WA_StyledBackground, true);
    tile->setMinimumHeight(74);
    tile->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QHBoxLayout* layout = new QHBoxLayout(tile);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(14);

    QLabel* iconBadge = new QLabel(icon, tile);
    iconBadge->setAlignment(Qt::AlignCenter);
    iconBadge->setFixedSize(36, 36);
    iconBadge->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  background-color: #eff6fc;"
        "  color: #0067c0;"
        "  border-radius: 8px;"
        "  font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets';"
        "  font-size: 18px;"
        "}"));
    layout->addWidget(iconBadge, 0, Qt::AlignVCenter);

    QVBoxLayout* textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);

    QLabel* titleLabel = new QLabel(title, tile);
    titleLabel->setObjectName("homeTileTitle");
    titleLabel->setWordWrap(true);
    textLayout->addWidget(titleLabel);

    QLabel* subtitleLabel = new QLabel(subtitle, tile);
    subtitleLabel->setObjectName("homeTileSubtitle");
    subtitleLabel->setWordWrap(true);
    textLayout->addWidget(subtitleLabel);

    layout->addLayout(textLayout, 1);

    QLabel* chevron = createGlyphLabel(glyph(0xE76C), tile, 12, QColor("#5c5c5c"));
    layout->addWidget(chevron, 0, Qt::AlignVCenter);
    return tile;
}

QWidget* createQuickAction(const QString& icon, const QString& title, const QString& subtitle, QWidget* parent)
{
    QWidget* action = new QWidget(parent);
    action->setObjectName("quickAction");
    action->setAttribute(Qt::WA_StyledBackground, true);
    QHBoxLayout* layout = new QHBoxLayout(action);
    layout->setContentsMargins(14, 12, 12, 12);
    layout->setSpacing(12);
    layout->addWidget(createGlyphLabel(icon, action, 18, QColor("#1b1b1b")), 0, Qt::AlignVCenter);

    QVBoxLayout* textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);
    QLabel* titleLabel = new QLabel(title, action);
    titleLabel->setObjectName("rowTitle");
    QLabel* subtitleLabel = new QLabel(subtitle, action);
    subtitleLabel->setObjectName("rowSubtitle");
    subtitleLabel->setWordWrap(true);
    textLayout->addWidget(titleLabel);
    textLayout->addWidget(subtitleLabel);
    layout->addLayout(textLayout, 1);
    layout->addWidget(createGlyphLabel(glyph(0xE76C), action, 14, QColor("#5c5c5c")), 0, Qt::AlignVCenter);
    return action;
}

QWidget* createPluginBadge(const QString& icon, const QString& text, QWidget* parent, const QColor& color)
{
    QWidget* badge = new QWidget(parent);
    badge->setObjectName("pluginBadge");
    badge->setAttribute(Qt::WA_StyledBackground, true);
    badge->setStyleSheet(QString(
        "QWidget#pluginBadge { background: %1; border: 1px solid %2; border-radius: 6px; }")
        .arg(color.lighter(188).name())
        .arg(color.lighter(155).name()));
    QHBoxLayout* layout = new QHBoxLayout(badge);
    layout->setContentsMargins(8, 3, 9, 3);
    layout->setSpacing(5);

    QLabel* iconLabel = new QLabel(icon, badge);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setFixedSize(14, 14);
    iconLabel->setStyleSheet(QString(
        "QLabel { background: transparent; color: %1; font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets'; font-size: 11px; }")
        .arg(color.darker(110).name()));
    layout->addWidget(iconLabel);

    QLabel* label = new QLabel(text, badge);
    label->setObjectName("badgeLabel");
    label->setStyleSheet(QString("QLabel { background: transparent; color: %1; font-size: 12px; font-weight: 600; }")
                         .arg(color.darker(120).name()));
    layout->addWidget(label);
    return badge;
}

QString settingsWidgetAccentColor(const QString& id)
{
    const uint hash = qHash(id.toLower());
    const QStringList colors {
        QStringLiteral("#2878ff"),
        QStringLiteral("#2ecb86"),
        QStringLiteral("#8b5cf6"),
        QStringLiteral("#ff8a18"),
        QStringLiteral("#06b6d4"),
        QStringLiteral("#ef4444")
    };
    return colors.at(static_cast<int>(hash % colors.size()));
}

QString settingsWidgetIconGlyph(const QString& id)
{
    const QString lower = id.toLower();
    if (lower.contains(QStringLiteral("note"))) {
        return glyph(0xE70B);
    }
    if (lower.contains(QStringLiteral("monitor")) || lower.contains(QStringLiteral("system"))) {
        return glyph(0xE9D9);
    }
    if (lower.contains(QStringLiteral("timer")) || lower.contains(QStringLiteral("clock"))) {
        return glyph(0xE916);
    }
    if (lower.contains(QStringLiteral("weather"))) {
        return glyph(0xE706);
    }
    return glyph(0xE8A5);
}

QString settingsDefaultWidgetIconPath()
{
    const QString appDirPath = QCoreApplication::applicationDirPath();
    const QStringList candidates {
        QStringLiteral(":/defaultIcon.png"),
        QDir(appDirPath).filePath(QStringLiteral("resources/defaultIcon.png")),
        QDir(appDirPath).filePath(QStringLiteral("defaultIcon.png")),
        QDir(QDir::currentPath()).filePath(QStringLiteral("src/resources/defaultIcon.png")),
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return candidates.last();
}

bool settingsIsSafeRelativeWidgetPath(const QString& path)
{
    if (path.isEmpty() || path.startsWith(QLatin1Char('/')) || path.startsWith(QLatin1Char('\\'))) {
        return false;
    }
    if (path.contains(QLatin1Char(':')) || path.contains(QLatin1Char('\0'))) {
        return false;
    }
    const QStringList parts = path.split(QRegularExpression(QStringLiteral("[/\\\\]")), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        if (part == QStringLiteral(".") || part == QStringLiteral("..")) {
            return false;
        }
    }
    return true;
}

bool settingsCanLoadIconImage(const QString& path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    if (!reader.canRead()) {
        return false;
    }
    const QSize size = reader.size();
    return size.isValid() && !size.isEmpty();
}

QString settingsResolveWidgetIconPath(const QString& packagePath, const QString& iconPath)
{
    const QString trimmed = iconPath.trimmed();
    if (!trimmed.isEmpty() && settingsIsSafeRelativeWidgetPath(trimmed)) {
        const QFileInfo iconInfo(QDir(packagePath).filePath(trimmed));
        if (iconInfo.exists() && iconInfo.isFile() && settingsCanLoadIconImage(iconInfo.absoluteFilePath())) {
            return iconInfo.absoluteFilePath();
        }
    }
    return settingsDefaultWidgetIconPath();
}

QLabel* createWidgetIconLabel(const QString& id, const QString& iconPath, QWidget* parent, int size)
{
    QLabel* icon = new QLabel(parent);
    icon->setAlignment(Qt::AlignCenter);
    icon->setFixedSize(size, size);
    const QString fallbackIconPath = settingsDefaultWidgetIconPath();
    QPixmap pixmap(iconPath);
    if (pixmap.isNull() && iconPath != fallbackIconPath) {
        pixmap.load(fallbackIconPath);
    }
    if (!pixmap.isNull()) {
        icon->setPixmap(pixmap.scaled(QSize(size, size), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        icon->setStyleSheet(QStringLiteral(
            "QLabel {"
            " background: #f3f3f3;"
            " border-radius: 8px;"
            "}"));
        return icon;
    }
    icon->setText(settingsWidgetIconGlyph(id));
    icon->setStyleSheet(QStringLiteral(
        "QLabel {"
        " background: %1;"
        " color: white;"
        " border-radius: 8px;"
        " font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets';"
        " font-size: %2px;"
        " font-weight: 600;"
        "}").arg(settingsWidgetAccentColor(id)).arg(qMax(18, size / 2)));
    return icon;
}

QLabel* createWidgetMetaLabel(const QString& title, const QString& value, QWidget* parent)
{
    QLabel* label = new QLabel(QStringLiteral("<span style='color:#5c5c5c;font-weight:500;'>%1</span><br><span style='color:#1b1b1b;font-weight:600;'>%2</span>")
                                   .arg(title.toHtmlEscaped(), value.toHtmlEscaped()),
                               parent);
    label->setTextFormat(Qt::RichText);
    label->setMinimumWidth(110);
    return label;
}

QPixmap getDesktopWallpaperThumbnail(const QSize& logicalSize, qreal dpr)
{
    static QString s_cachedPath;
    static QImage s_cachedImage;
    static QDateTime s_cachedTime;

    const QString currentPath = SysInfoUtil::GetRobustWallpaperPath();
    const bool pathChanged = (currentPath != s_cachedPath);
    const bool cacheExpired = s_cachedTime.isValid() && s_cachedTime.msecsTo(QDateTime::currentDateTime()) > 15000;

    if (pathChanged || cacheExpired || s_cachedImage.isNull()) {
        s_cachedPath = currentPath;
        s_cachedTime = QDateTime::currentDateTime();
        if (!currentPath.isEmpty() && QFile::exists(currentPath)) {
            QImageReader reader(currentPath);
            reader.setAutoTransform(true);
            s_cachedImage = reader.read();
        } else {
            s_cachedImage = QImage();
        }
    }

    const QSize physicalSize(qMax(1, qRound(logicalSize.width() * dpr)),
                             qMax(1, qRound(logicalSize.height() * dpr)));

    QPixmap result(physicalSize);
    result.setDevicePixelRatio(dpr);
    result.fill(Qt::transparent);

    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    if (!s_cachedImage.isNull()) {
        const QImage scaled = s_cachedImage.scaled(physicalSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const int cropX = qMax(0, (scaled.width() - physicalSize.width()) / 2);
        const int cropY = qMax(0, (scaled.height() - physicalSize.height()) / 2);
        painter.drawImage(QRect(0, 0, logicalSize.width(), logicalSize.height()),
                          scaled,
                          QRect(cropX, cropY, physicalSize.width(), physicalSize.height()));
    } else {
        QLinearGradient grad(0, 0, logicalSize.width(), logicalSize.height());
        grad.setColorAt(0.0, QColor(26, 78, 126));
        grad.setColorAt(0.5, QColor(38, 108, 168));
        grad.setColorAt(1.0, QColor(10, 30, 56));
        painter.fillRect(QRect(0, 0, logicalSize.width(), logicalSize.height()), grad);
    }
    return result;
}

} // namespace SettingsUICommon
