#include "ui/settings/pages/DesktopSettingsPage.h"
#include "ui/settings/SettingsUICommon.h"
#include "desktop/ListView.h"
#include "desktop/LinDesktop.h"
#include "desktop/SelectArea.h"
#include "desktop/RightMenu.h"
#include "box/LinBox.h"
#include "ui/Fluent/FluentToggleSwitch.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>
#include <QColorDialog>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <QTimer>

using namespace SettingsUICommon;

static QColor desktopSelectionFillColor(const QColor& baseColor)
{
    QSettings settings(settingsConfigPath(), QSettings::IniFormat);
    const int opacity = qBound(20, settings.value("Desktop/SelectionRectangleOpacity", 75).toInt(), 100);
    QColor fill = baseColor;
    fill.setAlpha(qBound(35, opacity * 140 / 100, 180));
    return fill;
}

static QColor desktopSelectionBorderColor(const QColor& baseColor)
{
    QSettings settings(settingsConfigPath(), QSettings::IniFormat);
    const int contrast = qBound(0, settings.value("Desktop/SelectionRectangleContrast", 60).toInt(), 100);
    QColor border = baseColor.darker(110);
    border.setAlpha(qBound(95, contrast * 230 / 100, 230));
    return border;
}

DesktopSettingsPage::DesktopSettingsPage(QWidget* parent)
    : QWidget(parent)
{
    QSettings settings(settingsConfigPath(), QSettings::IniFormat);
    const QColor color(settings.value("Desktop/SelectionRectangleColor", QStringLiteral("#64A0F0")).toString());
    m_selectionPreviewColor = color.isValid() ? color : QColor(100, 160, 240);

    initUI();
}

void DesktopSettingsPage::reloadSelectionOverlays()
{
    if (ListView* lv = ListView::instance()) {
        lv->loadSelectionColors();
        lv->update();
        if (LinDesktop* desktop = qobject_cast<LinDesktop*>(lv->parentWidget())) {
            if (desktop->selectArea) {
                desktop->selectArea->reloadColorsFromSettings();
            }
        }

        const QVector<LinBox*> allBoxes = lv->boxes();
        for (LinBox* box : allBoxes) {
            if (box) {
                box->refreshSelectionOverlayColor();
            }
        }
    }
}

void DesktopSettingsPage::initUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(54, 34, 54, 36);
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignTop);

    QLabel* titleLabel = createPageTitle(tr("Desktop Customization"), this);
    titleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(titleLabel, 0, Qt::AlignLeft);
    layout->addSpacing(8);

    // Selection Rectangle Color & Style Panel (Compact Inline Preview)
    QWidget* selectionPanel = createPanel(this);
    selectionPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QVBoxLayout* selectionLayout = new QVBoxLayout(selectionPanel);
    selectionLayout->setContentsMargins(22, 18, 22, 18);
    selectionLayout->setSpacing(0);

    selectionLayout->addWidget(createCardTitle(tr("Selection Rectangle Style"), selectionPanel));
    selectionLayout->addSpacing(4);
    selectionLayout->addWidget(createCardSubtitle(tr("Preview and customize the color, contrast, and opacity of the desktop drag-selection marquee."), selectionPanel));
    selectionLayout->addSpacing(12);

    m_desktopSelectionPreview = new QLabel(selectionPanel);
    m_desktopSelectionPreview->setFixedHeight(92);
    m_desktopSelectionPreview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_desktopSelectionPreview->setAlignment(Qt::AlignCenter);
    m_desktopSelectionPreview->setObjectName(QStringLiteral("previewSurface"));
    selectionLayout->addWidget(m_desktopSelectionPreview);
    selectionLayout->addSpacing(14);

    QWidget* presetsRow = new QWidget(selectionPanel);
    presetsRow->setObjectName("settingsInnerRow");
    presetsRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QHBoxLayout* presetsLayout = new QHBoxLayout(presetsRow);
    presetsLayout->setContentsMargins(12, 8, 12, 8);
    presetsLayout->setSpacing(8);
    QLabel* presetsLabel = new QLabel(tr("Presets"), presetsRow);
    presetsLabel->setObjectName("rowTitle");
    presetsLayout->addWidget(presetsLabel);
    presetsLayout->addStretch();

    m_selectionColorValueEdit = new QLineEdit(presetsRow);
    m_selectionColorValueEdit->setReadOnly(true);
    m_selectionColorValueEdit->setAlignment(Qt::AlignCenter);
    m_selectionColorValueEdit->setMinimumWidth(72);
    m_selectionColorValueEdit->setMaximumWidth(84);
    m_selectionColorValueEdit->setFixedHeight(30);
    m_selectionColorValueEdit->setText(m_selectionPreviewColor.name().toUpper());
    m_selectionColorValueEdit->setObjectName("valueEdit");
    presetsLayout->addWidget(m_selectionColorValueEdit);

    const QVector<QColor> presets = {
        QColor("#F0CF24"),
        QColor("#63A8F5"),
        QColor("#00C853"),
        QColor("#8B45E6"),
        QColor("#E94B98"),
        QColor("#FF8A24"),
        QColor("#AEB4BE")
    };
    for (const QColor& preset : presets) {
        QWidget* presetContainer = new QWidget(presetsRow);
        presetContainer->setObjectName("colorPresetContainer");
        QVBoxLayout* presetContainerLayout = new QVBoxLayout(presetContainer);
        presetContainerLayout->setContentsMargins(0, 0, 0, 0);
        presetContainerLayout->setSpacing(3);

        QPushButton* button = createColorPreset(preset, presetContainer);
        button->setFixedSize(24, 24);
        QLabel* indicator = new QLabel(presetContainer);
        indicator->setObjectName("colorPresetIndicator");
        indicator->setProperty("presetColor", preset.name());
        indicator->setFixedSize(6, 6);
        indicator->setStyleSheet(QStringLiteral(
            "QLabel#colorPresetIndicator {"
            " background: #1870ff;"
            " border-radius: 3px;"
            "}"));
        indicator->setVisible(false);

        presetContainerLayout->addWidget(button, 0, Qt::AlignHCenter);
        presetContainerLayout->addWidget(indicator, 0, Qt::AlignHCenter);
        connect(button, &QPushButton::clicked, this, [this, presetsRow, preset]() {
            m_selectionPreviewColor = preset;
            if (m_selectionColorValueEdit) {
                m_selectionColorValueEdit->setText(preset.name().toUpper());
            }
            QSettings settings(settingsConfigPath(), QSettings::IniFormat);
            settings.setValue("Desktop/SelectionRectangleColor", preset.name());
            settings.sync();
            reloadSelectionOverlays();
            updateColorPresetIndicators(presetsRow, preset);
            updatePreview();
        });
        presetsLayout->addWidget(presetContainer);
    }

    QWidget* pickerContainer = new QWidget(presetsRow);
    pickerContainer->setObjectName("colorPresetContainer");
    QVBoxLayout* pickerContainerLayout = new QVBoxLayout(pickerContainer);
    pickerContainerLayout->setContentsMargins(0, 0, 0, 0);
    pickerContainerLayout->setSpacing(3);

    QPushButton* pickerButton = new QPushButton(glyph(0xE790), pickerContainer);
    pickerButton->setObjectName("colorPickerButton");
    pickerButton->setFixedSize(24, 24);
    pickerButton->setCursor(Qt::PointingHandCursor);
    QLabel* pickerIndicator = new QLabel(pickerContainer);
    pickerIndicator->setObjectName("colorPresetIndicator");
    pickerIndicator->setProperty("customColorIndicator", true);
    pickerIndicator->setFixedSize(6, 6);
    pickerIndicator->setStyleSheet(QStringLiteral(
        "QLabel#colorPresetIndicator {"
        " background: #1870ff;"
        " border-radius: 3px;"
        "}"));
    pickerIndicator->setVisible(false);
    pickerContainerLayout->addWidget(pickerButton, 0, Qt::AlignHCenter);
    pickerContainerLayout->addWidget(pickerIndicator, 0, Qt::AlignHCenter);
    presetsLayout->addWidget(pickerContainer);
    updateColorPresetIndicators(presetsRow, m_selectionPreviewColor);
    selectionLayout->addWidget(presetsRow);
    selectionLayout->addWidget(createDivider(selectionPanel));

    auto applySelectionColor = [this, presetsRow](const QColor& color) {
        if (!color.isValid()) {
            return;
        }
        m_selectionPreviewColor = color;
        if (m_selectionColorValueEdit) {
            m_selectionColorValueEdit->setText(color.name().toUpper());
        }
        QSettings settings(settingsConfigPath(), QSettings::IniFormat);
        settings.setValue("Desktop/SelectionRectangleColor", color.name());
        settings.sync();
        reloadSelectionOverlays();
        updateColorPresetIndicators(presetsRow, color);
        updatePreview();
    };

    connect(pickerButton, &QPushButton::clicked, this, [this, applySelectionColor]() {
        QColorDialog* dialog = new QColorDialog(m_selectionPreviewColor, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(tr("Selection Rectangle Color"));
        dialog->setOption(QColorDialog::NoButtons, true);
        connect(dialog, &QColorDialog::currentColorChanged, this, [applySelectionColor](const QColor& color) {
            applySelectionColor(color);
        });
        connect(dialog, &QColorDialog::colorSelected, this, [applySelectionColor](const QColor& color) {
            applySelectionColor(color);
        });
        dialog->open();
    });

    // Contrast Slider
    QWidget* contrastRow = new QWidget(selectionPanel);
    contrastRow->setObjectName("settingsInnerRow");
    contrastRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QHBoxLayout* contrastLayout = new QHBoxLayout(contrastRow);
    contrastLayout->setContentsMargins(14, 8, 14, 8);
    contrastLayout->setSpacing(12);
    QVBoxLayout* contrastText = new QVBoxLayout();
    contrastText->setContentsMargins(0, 0, 0, 0);
    contrastText->setSpacing(2);
    QLabel* contrastTitle = new QLabel(tr("Contrast"), contrastRow);
    contrastTitle->setObjectName("rowTitle");
    contrastTitle->setWordWrap(true);
    contrastTitle->setMinimumWidth(0);
    contrastTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    contrastText->addWidget(contrastTitle);
    contrastLayout->addLayout(contrastText, 1);
    contrastLayout->addWidget(createGlyphLabel(glyph(0xE706), contrastRow, 18, QColor("#5b6b86")));
    QSlider* contrastSlider = new QSlider(Qt::Horizontal, contrastRow);
    contrastSlider->setRange(0, 100);
    QSettings desktopSettings(settingsConfigPath(), QSettings::IniFormat);
    contrastSlider->setValue(qBound(0, desktopSettings.value("Desktop/SelectionRectangleContrast", 60).toInt(), 100));
    contrastSlider->setMinimumWidth(120);
    contrastSlider->setMaximumWidth(160);
    contrastSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    contrastLayout->addWidget(contrastSlider, 2);
    QLabel* contrastValue = new QLabel(QStringLiteral("%1%").arg(contrastSlider->value()), contrastRow);
    contrastValue->setObjectName("valueLabel");
    contrastValue->setFixedWidth(44);
    contrastValue->setAlignment(Qt::AlignCenter);
    contrastLayout->addWidget(contrastValue);
    connect(contrastSlider, &QSlider::valueChanged, this, [this, contrastValue](int value) {
        contrastValue->setText(QStringLiteral("%1%").arg(value));
        QSettings settings(settingsConfigPath(), QSettings::IniFormat);
        settings.setValue("Desktop/SelectionRectangleContrast", value);
        settings.sync();
        reloadSelectionOverlays();
        updatePreview();
    });
    selectionLayout->addWidget(contrastRow);
    selectionLayout->addWidget(createDivider(selectionPanel));

    // Opacity / Strength Slider
    QWidget* strengthRow = new QWidget(selectionPanel);
    strengthRow->setObjectName("settingsInnerRow");
    strengthRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QHBoxLayout* strengthLayout = new QHBoxLayout(strengthRow);
    strengthLayout->setContentsMargins(14, 8, 14, 8);
    strengthLayout->setSpacing(12);
    QVBoxLayout* strengthText = new QVBoxLayout();
    strengthText->setContentsMargins(0, 0, 0, 0);
    strengthText->setSpacing(2);
    QLabel* strengthTitle = new QLabel(tr("Opacity / Strength"), strengthRow);
    strengthTitle->setObjectName("rowTitle");
    strengthTitle->setWordWrap(true);
    strengthTitle->setMinimumWidth(0);
    strengthTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    strengthText->addWidget(strengthTitle);
    strengthLayout->addLayout(strengthText, 1);
    strengthLayout->addWidget(createGlyphLabel(glyph(0xEB48), strengthRow, 18, QColor("#5b6b86")));
    QSlider* strengthSlider = new QSlider(Qt::Horizontal, strengthRow);
    strengthSlider->setRange(20, 100);
    strengthSlider->setValue(qBound(20, desktopSettings.value("Desktop/SelectionRectangleOpacity", 75).toInt(), 100));
    strengthSlider->setMinimumWidth(120);
    strengthSlider->setMaximumWidth(160);
    strengthSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    strengthLayout->addWidget(strengthSlider, 2);
    QLabel* strengthValue = new QLabel(QStringLiteral("%1%").arg(strengthSlider->value()), strengthRow);
    strengthValue->setObjectName("valueLabel");
    strengthValue->setFixedWidth(44);
    strengthValue->setAlignment(Qt::AlignCenter);
    strengthLayout->addWidget(strengthValue);
    connect(strengthSlider, &QSlider::valueChanged, this, [this, strengthValue](int value) {
        strengthValue->setText(QStringLiteral("%1%").arg(value));
        QSettings settings(settingsConfigPath(), QSettings::IniFormat);
        settings.setValue("Desktop/SelectionRectangleOpacity", value);
        settings.sync();
        reloadSelectionOverlays();
        updatePreview();
    });
    selectionLayout->addWidget(strengthRow);

    // Toggle: Win11 Fluent Marquee & Stats Badge
    QWidget* fluentRow = new QWidget(selectionPanel);
    fluentRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QHBoxLayout* fluentLayout = new QHBoxLayout(fluentRow);
    fluentLayout->setContentsMargins(14, 8, 14, 8);
    fluentLayout->setSpacing(12);

    QVBoxLayout* fluentText = new QVBoxLayout();
    fluentText->setContentsMargins(0, 0, 0, 0);
    fluentText->setSpacing(2);
    QLabel* fluentTitle = new QLabel(tr("Win11 Fluent Marquee & Stats Badge"), fluentRow);
    fluentTitle->setObjectName("rowTitle");
    fluentTitle->setWordWrap(true);
    fluentTitle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QLabel* fluentSubtitle = new QLabel(tr("Enable modern rounded glass marquee with gradient glow border and real-time selection stats pill."), fluentRow);
    fluentSubtitle->setObjectName("rowSubtitle");
    fluentSubtitle->setWordWrap(true);
    fluentSubtitle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    fluentText->addWidget(fluentTitle);
    fluentText->addWidget(fluentSubtitle);
    fluentLayout->addLayout(fluentText, 1);

    m_fluentSelectionToggle = new FluentToggleSwitch(fluentRow);
    const bool isFluentEnabled = desktopSettings.value("Desktop/FluentSelectionMarquee", true).toBool();
    m_fluentSelectionToggle->setChecked(isFluentEnabled);
    fluentLayout->addWidget(m_fluentSelectionToggle, 0, Qt::AlignRight | Qt::AlignVCenter);

    connect(m_fluentSelectionToggle, &FluentToggleSwitch::toggled, this, [this](bool checked) {
        QSettings settings(settingsConfigPath(), QSettings::IniFormat);
        settings.setValue("Desktop/FluentSelectionMarquee", checked);
        settings.sync();
        reloadSelectionOverlays();
        updatePreview();
    });

    selectionLayout->addWidget(createDivider(selectionPanel));
    selectionLayout->addWidget(fluentRow);
    layout->addWidget(selectionPanel, 0);

    // 3. Desktop Behavior Panel
    QWidget* behaviorPanel = createPanel(this);
    behaviorPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QVBoxLayout* behaviorLayout = new QVBoxLayout(behaviorPanel);
    behaviorLayout->setContentsMargins(22, 18, 22, 18);
    behaviorLayout->setSpacing(0);
    behaviorLayout->addWidget(createCardTitle(tr("Desktop Behavior"), behaviorPanel));
    behaviorLayout->addSpacing(12);

    ListView* lv = ListView::instance();

    // Toggle: Shortcut Arrows
    FluentToggleSwitch* arrowToggle = new FluentToggleSwitch(this);
    arrowToggle->setChecked(lv ? lv->shortcutArrowVisible() : true);
    connect(arrowToggle, &FluentToggleSwitch::toggled, this, [](bool checked){
        ListView* lv = ListView::instance();
        if (lv) lv->setShortcutArrowVisible(checked);
    });
    behaviorLayout->addWidget(createSwitchRow(glyph(0xE724), tr("Shortcut Arrows"), tr("Toggle visibility of the small arrow on desktop shortcut icons."), arrowToggle, behaviorPanel));
    behaviorLayout->addWidget(createDivider(behaviorPanel));

    // Toggle: Double Click to Hide
    FluentToggleSwitch* hideToggle = new FluentToggleSwitch(this);
    hideToggle->setChecked(lv ? lv->isDoubleClickHideIconsEnabled() : true);
    connect(hideToggle, &FluentToggleSwitch::toggled, this, [](bool checked){
        ListView* lv = ListView::instance();
        if (lv) lv->setDoubleClickHideIconsEnabled(checked);
    });
    behaviorLayout->addWidget(createSwitchRow(glyph(0xE949), tr("Double Click to Hide"), tr("Quickly show or hide all desktop icons by double-clicking on empty spaces."), hideToggle, behaviorPanel));
    behaviorLayout->addWidget(createDivider(behaviorPanel));

    // Toggle: Fade Transition on Hide
    FluentToggleSwitch* fadeToggle = new FluentToggleSwitch(this);
    fadeToggle->setChecked(lv ? lv->isFadeTransitionOnHideEnabled() : true);
    connect(fadeToggle, &FluentToggleSwitch::toggled, this, [](bool checked){
        ListView* lv = ListView::instance();
        if (lv) lv->setFadeTransitionOnHideEnabled(checked);
    });
    behaviorLayout->addWidget(createSwitchRow(glyph(0xE790), tr("Fade Transition on Hide"), tr("Play a smooth opacity fade-in/out transition when hiding or showing desktop elements."), fadeToggle, behaviorPanel));

    layout->addWidget(behaviorPanel, 0);

    // 4. Windows 11 Context Menu Panel
    layout->addWidget(createWindows11MenuPanel(this), 0);

    layout->addStretch();
    updatePreview();
}

QWidget* DesktopSettingsPage::createWindows11MenuPanel(QWidget* parent)
{
    QWidget* win11MenuPanel = createPanel(parent);
    win11MenuPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QVBoxLayout* win11MenuLayout = new QVBoxLayout(win11MenuPanel);
    win11MenuLayout->setContentsMargins(22, 18, 22, 18);
    win11MenuLayout->setSpacing(0);
    win11MenuLayout->addWidget(createCardTitle(tr("Windows11 Menu"), win11MenuPanel));
    win11MenuLayout->addSpacing(12);

    const bool windows11ContextMenuSupported = RightMenu::isWindows11SystemContextMenuSupported();
    const QString windows11UnsupportedTip = tr("This option is available only on Windows 11 in Release builds.");

    FluentToggleSwitch* win11Toggle = new FluentToggleSwitch(parent);
    win11Toggle->setChecked(RightMenu::isWindows11SystemContextMenuEnabled());
    win11Toggle->setEnabled(windows11ContextMenuSupported);
    if (!windows11ContextMenuSupported) {
        win11Toggle->setToolTip(windows11UnsupportedTip);
    }

    connect(win11Toggle, &FluentToggleSwitch::toggled, this, [win11Toggle, windows11ContextMenuSupported](bool checked) {
        if (!windows11ContextMenuSupported) {
            if (win11Toggle) {
                const QSignalBlocker blocker(win11Toggle);
                win11Toggle->setChecked(false);
            }
            return;
        }
        RightMenu::setWindows11SystemContextMenuEnabled(checked);
    });

    win11MenuLayout->addWidget(createSwitchRow(
        glyph(0xECAA),
        tr("Enable Windows 11 Context Menu"),
        tr("Use the modern Windows 11 context menu island for desktop items."),
        win11Toggle,
        win11MenuPanel));

    return win11MenuPanel;
}

void DesktopSettingsPage::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updatePreview();
}

void DesktopSettingsPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    QTimer::singleShot(0, this, [this]() {
        updatePreview();
    });
}

void DesktopSettingsPage::updatePreview()
{
    if (!m_desktopSelectionPreview) {
        return;
    }

    QSize previewSize = m_desktopSelectionPreview->contentsRect().size();
    if (previewSize.width() < 10 || previewSize.height() < 10) {
        previewSize = QSize(qMax(280, width() - 88), 92);
    }

    const qreal dpr = qMax<qreal>(1.0, devicePixelRatioF());
    const QSize physicalSize(qMax(1, qRound(previewSize.width() * dpr)),
                             qMax(1, qRound(previewSize.height() * dpr)));

    QPixmap canvas(physicalSize);
    canvas.setDevicePixelRatio(dpr);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::TextAntialiasing);

    const int radius = 8;
    const QRect bounds(0, 0, previewSize.width(), previewSize.height());

    // 1. Draw rounded real wallpaper clip
    QPainterPath clipPath;
    clipPath.addRoundedRect(bounds, radius, radius);
    painter.setClipPath(clipPath);

    QPixmap wp = getDesktopWallpaperThumbnail(previewSize, dpr);
    painter.drawPixmap(0, 0, wp);
    // Subtle darkening for crisp contrast
    painter.fillRect(bounds, QColor(0, 0, 0, 22));

    // 2. Draw mock desktop icons (showing unselected vs selected state)
    const int iconBaseY = qMax(6, (previewSize.height() - 52) / 2);
    const int item1X = qMax(24, previewSize.width() * 12 / 100);

    // Left item: Normal unselected document
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 220));
    painter.drawRoundedRect(item1X + 8, iconBaseY + 2, 24, 28, 3, 3);
    painter.setBrush(QColor(70, 140, 245, 200));
    painter.drawRoundedRect(item1X + 11, iconBaseY + 7, 18, 3, 1, 1);
    painter.drawRoundedRect(item1X + 11, iconBaseY + 13, 14, 3, 1, 1);
    painter.drawRoundedRect(item1X + 11, iconBaseY + 19, 16, 3, 1, 1);
    painter.setBrush(QColor(255, 255, 255, 190));
    painter.drawRoundedRect(item1X + 4, iconBaseY + 34, 32, 6, 2, 2);

    // Center/Right item: Inside selection marquee
    const int item2X = qMax(item1X + 90, previewSize.width() * 50 / 100);
    painter.setBrush(QColor(255, 195, 60, 230));
    painter.drawRoundedRect(item2X + 6, iconBaseY + 4, 30, 24, 3, 3);
    painter.setBrush(QColor(255, 220, 110, 240));
    painter.drawRoundedRect(item2X + 6, iconBaseY + 7, 30, 21, 3, 3);
    painter.setBrush(QColor(0, 120, 215, 210));
    painter.drawRoundedRect(item2X + 2, iconBaseY + 33, 38, 9, 3, 3);
    painter.setBrush(Qt::white);
    painter.drawRoundedRect(item2X + 7, iconBaseY + 36, 28, 3, 1, 1);

    // 3. Draw clean, native Windows 11 drag selection marquee
    const QColor fillColor = desktopSelectionFillColor(m_selectionPreviewColor);
    const QColor borderColor = desktopSelectionBorderColor(m_selectionPreviewColor);
    const int selX = qMax(item1X + 60, previewSize.width() * 38 / 100);
    const int selY = qMax(6, (previewSize.height() - 66) / 2);
    const int selW = qBound(120, previewSize.width() * 46 / 100, 280);
    const int selH = 66;
    const QRect selectionRect(selX, selY, selW, selH);

    QSettings settings(settingsConfigPath(), QSettings::IniFormat);
    const bool isFluent = settings.value("Desktop/FluentSelectionMarquee", true).toBool();

    if (isFluent) {
        painter.setRenderHint(QPainter::Antialiasing, true);
        const qreal radius = 6.0;

        // 1. 多层发光光晕 (Multi-tier Outer Bloom)
        QColor glowColor = borderColor;
        if (glowColor.alpha() < 100) glowColor.setAlpha(180);
        painter.setPen(QPen(QColor(glowColor.red(), glowColor.green(), glowColor.blue(), 35), 4.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(selectionRect).adjusted(-2.0, -2.0, 2.0, 2.0), radius + 2.0, radius + 2.0);

        painter.setPen(QPen(QColor(glowColor.red(), glowColor.green(), glowColor.blue(), 80), 2.0));
        painter.drawRoundedRect(QRectF(selectionRect).adjusted(-0.8, -0.8, 0.8, 0.8), radius + 0.8, radius + 0.8);

        // 2. Acrylic 玻璃填充
        QLinearGradient fillGrad(selectionRect.topLeft(), selectionRect.bottomRight());
        QColor topFill = fillColor;
        topFill.setAlpha(qBound(45, topFill.alpha() + 25, 200));
        QColor bottomFill = fillColor;
        bottomFill.setAlpha(qBound(20, bottomFill.alpha() - 15, 160));
        fillGrad.setColorAt(0.0, topFill);
        fillGrad.setColorAt(1.0, bottomFill);
        painter.setBrush(fillGrad);

        // 3. 核心发光描边
        QLinearGradient borderGrad(selectionRect.topLeft(), selectionRect.bottomRight());
        borderGrad.setColorAt(0.0, glowColor.lighter(135));
        borderGrad.setColorAt(1.0, glowColor);
        painter.setPen(QPen(QBrush(borderGrad), 1.8));
        painter.drawRoundedRect(QRectF(selectionRect).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);

        // 4. 顶边高光
        painter.setPen(QPen(QColor(255, 255, 255, 100), 1.0));
        painter.drawLine(QPointF(selectionRect.left() + radius + 2, selectionRect.top() + 1.2),
                         QPointF(selectionRect.right() - radius - 2, selectionRect.top() + 1.2));

        // 5. 四角高光精细定位角标
        const qreal cornerLen = 8.0;
        painter.setPen(QPen(QColor(255, 255, 255, 220), 1.8, Qt::SolidLine, Qt::SquareCap));
        painter.drawLine(QPointF(selectionRect.left() + 2, selectionRect.top() + 2), QPointF(selectionRect.left() + 2 + cornerLen, selectionRect.top() + 2));
        painter.drawLine(QPointF(selectionRect.left() + 2, selectionRect.top() + 2), QPointF(selectionRect.left() + 2, selectionRect.top() + 2 + cornerLen));
        painter.drawLine(QPointF(selectionRect.right() - 2, selectionRect.top() + 2), QPointF(selectionRect.right() - 2 - cornerLen, selectionRect.top() + 2));
        painter.drawLine(QPointF(selectionRect.right() - 2, selectionRect.top() + 2), QPointF(selectionRect.right() - 2, selectionRect.top() + 2 + cornerLen));
        painter.drawLine(QPointF(selectionRect.left() + 2, selectionRect.bottom() - 2), QPointF(selectionRect.left() + 2 + cornerLen, selectionRect.bottom() - 2));
        painter.drawLine(QPointF(selectionRect.left() + 2, selectionRect.bottom() - 2), QPointF(selectionRect.left() + 2, selectionRect.bottom() - 2 - cornerLen));
        painter.drawLine(QPointF(selectionRect.right() - 2, selectionRect.bottom() - 2), QPointF(selectionRect.right() - 2 - cornerLen, selectionRect.bottom() - 2));
        painter.drawLine(QPointF(selectionRect.right() - 2, selectionRect.bottom() - 2), QPointF(selectionRect.right() - 2, selectionRect.bottom() - 2 - cornerLen));

        // 6. Preview HUD Pill badge
        const QString badgeText = QStringLiteral("🎯 1 项");
        QFont badgeFont = painter.font();
        badgeFont.setPixelSize(10);
        badgeFont.setBold(true);
        painter.setFont(badgeFont);
        QFontMetrics fm(badgeFont);
        const int badgeW = fm.horizontalAdvance(badgeText) + 16;
        const int badgeH = 22;
        const QRectF badgeRect(selectionRect.right() - badgeW - 6, selectionRect.bottom() - badgeH - 6, badgeW, badgeH);

        painter.setPen(QPen(QColor(glowColor.red(), glowColor.green(), glowColor.blue(), 70), 2.0));
        painter.setBrush(QColor(16, 20, 28, 225));
        painter.drawRoundedRect(badgeRect, badgeH / 2.0, badgeH / 2.0);

        painter.setPen(QPen(QColor(0, 220, 255, 190), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(badgeRect, badgeH / 2.0, badgeH / 2.0);

        painter.setPen(QColor(0, 235, 255));
        painter.drawText(badgeRect, Qt::AlignCenter, badgeText);
    } else {
        painter.fillRect(selectionRect, fillColor);
        painter.setPen(QPen(borderColor, 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(selectionRect);
    }

    // 4. Subtle inner card border
    painter.setClipping(false);
    painter.setPen(QPen(QColor(0, 0, 0, 32), 1));
    painter.drawRoundedRect(bounds.adjusted(0, 0, -1, -1), radius, radius);

    painter.end();
    m_desktopSelectionPreview->setPixmap(canvas);
}
