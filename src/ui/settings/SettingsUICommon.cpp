#include "ui/settings/SettingsUICommon.h"

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
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
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
    panel->setMinimumWidth(0);
    panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* shadow = new QGraphicsDropShadowEffect(panel);
    shadow->setBlurRadius(24);
    shadow->setXOffset(0);
    shadow->setYOffset(8);
    shadow->setColor(QColor(15, 23, 42, 18));
    panel->setGraphicsEffect(shadow);
    return panel;
}

QLabel* createGlyphLabel(const QString& glyphText, QWidget* parent, int pixelSize, const QColor& color)
{
    QLabel* label = new QLabel(glyphText, parent);
    label->setObjectName("glyphLabel");
    label->setAlignment(Qt::AlignCenter);
    label->setFixedSize(pixelSize + 18, pixelSize + 18);
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
    button->setMinimumHeight(32);
    button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    button->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: 7px;"
        "  padding: 0 12px;"
        "  font-size: 13px;"
        "  font-weight: 700;"
        "}"
        "QPushButton:hover { background: %4; }"
        "QPushButton:disabled { background: #e5e7eb; border-color: #d1d5db; color: #9ca3af; }")
        .arg(accent ? QStringLiteral("#0078d4") : (destructive ? QStringLiteral("#fff1f2") : QStringLiteral("#ffffff")),
             accent ? QStringLiteral("#ffffff") : (destructive ? QStringLiteral("#b91c1c") : QStringLiteral("#111827")),
             accent ? QStringLiteral("#0078d4") : (destructive ? QStringLiteral("#fecdd3") : QStringLiteral("#cfd8e6")),
             accent ? QStringLiteral("#0b86e8") : (destructive ? QStringLiteral("#ffe4e6") : QStringLiteral("#f5f9ff"))));
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
    row->setMinimumWidth(0);
    row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QHBoxLayout* layout = new QHBoxLayout(row);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(14);

    layout->addWidget(createGlyphLabel(icon, row, 20), 0, Qt::AlignVCenter);

    QVBoxLayout* textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);

    QLabel* titleLabel = new QLabel(title, row);
    titleLabel->setObjectName("rowTitle");
    titleLabel->setWordWrap(true);
    titleLabel->setMinimumWidth(0);
    titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    QLabel* subtitleLabel = new QLabel(subtitle, row);
    subtitleLabel->setObjectName("rowSubtitle");
    subtitleLabel->setWordWrap(true);
    subtitleLabel->setMinimumWidth(0);
    subtitleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
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
        " border-radius: 7px;"
        "}"
        "QPushButton#colorPreset:hover {"
        " border: 2px solid #0078d4;"
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
    tile->setMinimumHeight(142);
    QVBoxLayout* layout = new QVBoxLayout(tile);
    layout->setContentsMargins(16, 18, 16, 16);
    layout->setSpacing(8);
    layout->addWidget(createGlyphLabel(icon, tile, 34), 0, Qt::AlignHCenter);

    QLabel* titleLabel = new QLabel(title, tile);
    titleLabel->setObjectName("homeTileTitle");
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    QLabel* subtitleLabel = new QLabel(subtitle, tile);
    subtitleLabel->setObjectName("homeTileSubtitle");
    subtitleLabel->setWordWrap(true);
    subtitleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(subtitleLabel);
    layout->addStretch();
    return tile;
}

QWidget* createQuickAction(const QString& icon, const QString& title, const QString& subtitle, QWidget* parent)
{
    QWidget* action = new QWidget(parent);
    action->setObjectName("quickAction");
    QHBoxLayout* layout = new QHBoxLayout(action);
    layout->setContentsMargins(14, 12, 12, 12);
    layout->setSpacing(12);
    layout->addWidget(createGlyphLabel(icon, action, 22), 0, Qt::AlignVCenter);

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
    layout->addWidget(createGlyphLabel(glyph(0xE974), action, 16, QColor("#27344c")), 0, Qt::AlignVCenter);
    return action;
}

QWidget* createPluginBadge(const QString& icon, const QString& text, QWidget* parent, const QColor& color)
{
    QWidget* badge = new QWidget(parent);
    badge->setObjectName("pluginBadge");
    badge->setStyleSheet(QString(
        "QWidget#pluginBadge { background: %1; border: 1px solid %2; border-radius: 7px; }")
        .arg(color.lighter(185).name())
        .arg(color.lighter(145).name()));
    QHBoxLayout* layout = new QHBoxLayout(badge);
    layout->setContentsMargins(8, 4, 10, 4);
    layout->setSpacing(6);
    layout->addWidget(createGlyphLabel(icon, badge, 12, color));
    QLabel* label = new QLabel(text, badge);
    label->setObjectName("badgeLabel");
    label->setStyleSheet(QString("color: %1;").arg(color.darker(115).name()));
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
            " background: #eef3f8;"
            " border-radius: 12px;"
            "}"));
        return icon;
    }
    icon->setText(settingsWidgetIconGlyph(id));
    icon->setStyleSheet(QStringLiteral(
        "QLabel {"
        " background: %1;"
        " color: white;"
        " border-radius: 12px;"
        " font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets';"
        " font-size: %2px;"
        " font-weight: 700;"
        "}").arg(settingsWidgetAccentColor(id)).arg(qMax(18, size / 2)));
    return icon;
}

QLabel* createWidgetMetaLabel(const QString& title, const QString& value, QWidget* parent)
{
    QLabel* label = new QLabel(QStringLiteral("<span style='color:#7a8498;font-weight:700;'>%1</span><br><span style='color:#1f2a44;font-weight:800;'>%2</span>")
                                   .arg(title.toHtmlEscaped(), value.toHtmlEscaped()),
                               parent);
    label->setTextFormat(Qt::RichText);
    label->setMinimumWidth(110);
    return label;
}

} // namespace SettingsUICommon
