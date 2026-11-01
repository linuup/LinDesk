#include "ui/settings/pages/DesktopSettingsPage.h"
#include "ui/settings/SettingsUICommon.h"
#include "desktop/ListView.h"
#include "desktop/LinDesktop.h"
#include "desktop/SelectArea.h"
#include "desktop/RightMenu.h"
#include "box/LinBox.h"
#include "ui/Fluent/FluentToggleSwitch.h"
#include "ui/Fluent/FluentComboBox.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>
#include <QTimer>
#include "FluentSlider.h"
#include <QColorDialog>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <QTimer>

using namespace SettingsUICommon;
namespace {
class ClickFilter : public QObject {
public:
    ClickFilter(QObject* parent, std::function<void()> onClick)
        : QObject(parent), m_onClick(std::move(onClick)) {}
protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::MouseButtonRelease) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton && m_onClick) {
                m_onClick();
                return true;
            }
        }
        return QObject::eventFilter(obj, event);
    }
private:
    std::function<void()> m_onClick;
};
}


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

void DesktopSettingsPage::queueSettingsSync()
{
    if (!m_settingsSyncTimer) {
        m_settingsSyncTimer = new QTimer(this);
        m_settingsSyncTimer->setSingleShot(true);
        m_settingsSyncTimer->setInterval(70);
        connect(m_settingsSyncTimer, &QTimer::timeout, this, [this]() {
            QSettings settings(settingsConfigPath(), QSettings::IniFormat);
            settings.sync();
            reloadSelectionOverlays();
        });
    }
    m_settingsSyncTimer->start();
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

    QSettings desktopSettings(settingsConfigPath(), QSettings::IniFormat);

    // 1. Toggle 1: Modern Glass Marquee (Apple / macOS 现代极简玻璃质感选框)
    QWidget* fluentRow = new QWidget(selectionPanel);
    fluentRow->setObjectName("settingsInnerRow");
    fluentRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QHBoxLayout* fluentLayout = new QHBoxLayout(fluentRow);
    fluentLayout->setContentsMargins(14, 8, 14, 8);
    fluentLayout->setSpacing(12);

    QVBoxLayout* fluentText = new QVBoxLayout();
    fluentText->setContentsMargins(0, 0, 0, 0);
    fluentText->setSpacing(2);
    QLabel* fluentTitle = new QLabel(tr("Modern Glass Marquee"), fluentRow);
    fluentTitle->setObjectName("rowTitle");
    fluentTitle->setWordWrap(true);
    fluentTitle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QLabel* fluentSubtitle = new QLabel(tr("Refined translucent optical glass fill with subtle continuous curvature and hairline border."), fluentRow);
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

    selectionLayout->addWidget(createDivider(selectionPanel));
    selectionLayout->addWidget(fluentRow);

    // 2. Toggle 2: Selection Count Badge (独立数量胶囊开关)
    QWidget* badgeRow = new QWidget(selectionPanel);
    badgeRow->setObjectName("settingsInnerRow");
    badgeRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QHBoxLayout* badgeLayout = new QHBoxLayout(badgeRow);
    badgeLayout->setContentsMargins(14, 8, 14, 8);
    badgeLayout->setSpacing(12);

    QVBoxLayout* badgeText = new QVBoxLayout();
    badgeText->setContentsMargins(0, 0, 0, 0);
    badgeText->setSpacing(2);
    QLabel* badgeTitle = new QLabel(tr("Selection Count Badge"), badgeRow);
    badgeTitle->setObjectName("rowTitle");
    badgeTitle->setWordWrap(true);
    badgeTitle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QLabel* badgeSubtitle = new QLabel(tr("Display an Apple-style floating count capsule when selecting multiple desktop items."), badgeRow);
    badgeSubtitle->setObjectName("rowSubtitle");
    badgeSubtitle->setWordWrap(true);
    badgeSubtitle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    badgeText->addWidget(badgeTitle);
    badgeText->addWidget(badgeSubtitle);
    badgeLayout->addLayout(badgeText, 1);

    m_selectionCountBadgeToggle = new FluentToggleSwitch(badgeRow);
    const bool isBadgeEnabled = desktopSettings.value("Desktop/SelectionCountBadgeEnabled", false).toBool();
    m_selectionCountBadgeToggle->setChecked(isBadgeEnabled);
    badgeLayout->addWidget(m_selectionCountBadgeToggle, 0, Qt::AlignRight | Qt::AlignVCenter);

    selectionLayout->addWidget(createDivider(selectionPanel));
    selectionLayout->addWidget(badgeRow);

    // 3. Expandable Header: Advanced Visual Options (折叠抽屉式高级细节微调)
    QWidget* advancedHeaderRow = new QWidget(selectionPanel);
    advancedHeaderRow->setObjectName("settingsInnerRow");
    advancedHeaderRow->setCursor(Qt::PointingHandCursor);
    advancedHeaderRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QHBoxLayout* advancedHeaderLayout = new QHBoxLayout(advancedHeaderRow);
    advancedHeaderLayout->setContentsMargins(14, 10, 14, 10);
    advancedHeaderLayout->setSpacing(12);

    advancedHeaderLayout->addWidget(createGlyphLabel(glyph(0xEB48), advancedHeaderRow, 18, QColor("#5b6b86")));

    QVBoxLayout* advancedHeaderText = new QVBoxLayout();
    advancedHeaderText->setContentsMargins(0, 0, 0, 0);
    advancedHeaderText->setSpacing(2);
    QLabel* advancedTitle = new QLabel(tr("Advanced Visual Options"), advancedHeaderRow);
    advancedTitle->setObjectName("rowTitle");
    advancedTitle->setWordWrap(true);
    advancedTitle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QLabel* advancedSubtitle = new QLabel(tr("Fine-tune opacity, contrast, border width, and corner radii."), advancedHeaderRow);
    advancedSubtitle->setObjectName("rowSubtitle");
    advancedSubtitle->setWordWrap(true);
    advancedSubtitle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    advancedHeaderText->addWidget(advancedTitle);
    advancedHeaderText->addWidget(advancedSubtitle);
    advancedHeaderLayout->addLayout(advancedHeaderText, 1);

    m_advancedChevron = createGlyphLabel(glyph(0xE70D), advancedHeaderRow, 14, QColor("#5b6b86"));
    advancedHeaderLayout->addWidget(m_advancedChevron, 0, Qt::AlignRight | Qt::AlignVCenter);

    selectionLayout->addWidget(createDivider(selectionPanel));
    selectionLayout->addWidget(advancedHeaderRow);

    // 4. Collapsible Container for Fine-Tuning Sliders
    m_advancedContainer = new QWidget(selectionPanel);
    m_advancedContainer->setObjectName("advancedOptionsContainer");
    m_advancedContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QVBoxLayout* advancedLayout = new QVBoxLayout(m_advancedContainer);
    advancedLayout->setContentsMargins(0, 0, 0, 0);
    advancedLayout->setSpacing(0);
    m_advancedContainer->setVisible(false); // Default collapsed for clean aesthetics!

    // Click handler to toggle expansion
    auto toggleAdvanced = [this]() {
        if (!m_advancedContainer) return;
        const bool willShow = !m_advancedContainer->isVisible();
        m_advancedContainer->setVisible(willShow);
        if (m_advancedChevron) {
            m_advancedChevron->setText(glyph(willShow ? 0xE70E : 0xE70D));
        }
    };
    advancedHeaderRow->installEventFilter(new ClickFilter(advancedHeaderRow, toggleAdvanced));
    for (QWidget* child : advancedHeaderRow->findChildren<QWidget*>()) {
        child->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }

    // Inside Advanced: 4.1 Fill Opacity Slider
    QWidget* strengthRow = new QWidget(m_advancedContainer);
    strengthRow->setObjectName("settingsInnerRow");
    strengthRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QHBoxLayout* strengthLayout = new QHBoxLayout(strengthRow);
    strengthLayout->setContentsMargins(14, 8, 14, 8);
    strengthLayout->setSpacing(12);
    QVBoxLayout* strengthText = new QVBoxLayout();
    strengthText->setContentsMargins(0, 0, 0, 0);
    strengthText->setSpacing(2);
    QLabel* strengthTitle = new QLabel(tr("Fill Opacity"), strengthRow);
    strengthTitle->setObjectName("rowTitle");
    strengthTitle->setWordWrap(true);
    strengthTitle->setMinimumWidth(0);
    strengthTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    strengthText->addWidget(strengthTitle);
    strengthLayout->addLayout(strengthText, 1);
    strengthLayout->addWidget(createGlyphLabel(glyph(0xEB48), strengthRow, 18, QColor("#5b6b86")));
    FluentSlider* strengthSlider = new FluentSlider(Qt::Horizontal, strengthRow);
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
        queueSettingsSync();
        updatePreview();
    });
    connect(strengthSlider, &QSlider::sliderReleased, this, [this]() {
        QSettings settings(settingsConfigPath(), QSettings::IniFormat);
        settings.sync();
        reloadSelectionOverlays();
    });
    advancedLayout->addWidget(strengthRow);
    advancedLayout->addWidget(createDivider(m_advancedContainer));

    // Inside Advanced: 4.2 Border Opacity / Contrast Slider
    QWidget* contrastRow = new QWidget(m_advancedContainer);
    contrastRow->setObjectName("settingsInnerRow");
    contrastRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QHBoxLayout* contrastLayout = new QHBoxLayout(contrastRow);
    contrastLayout->setContentsMargins(14, 8, 14, 8);
    contrastLayout->setSpacing(12);
    QVBoxLayout* contrastText = new QVBoxLayout();
    contrastText->setContentsMargins(0, 0, 0, 0);
    contrastText->setSpacing(2);
    QLabel* contrastTitle = new QLabel(tr("Border Opacity / Contrast"), contrastRow);
    contrastTitle->setObjectName("rowTitle");
    contrastTitle->setWordWrap(true);
    contrastTitle->setMinimumWidth(0);
    contrastTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    contrastText->addWidget(contrastTitle);
    contrastLayout->addLayout(contrastText, 1);
    contrastLayout->addWidget(createGlyphLabel(glyph(0xE706), contrastRow, 18, QColor("#5b6b86")));
    FluentSlider* contrastSlider = new FluentSlider(Qt::Horizontal, contrastRow);
    contrastSlider->setRange(0, 100);
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
        queueSettingsSync();
        updatePreview();
    });
    connect(contrastSlider, &QSlider::sliderReleased, this, [this]() {
        QSettings settings(settingsConfigPath(), QSettings::IniFormat);
        settings.sync();
        reloadSelectionOverlays();
    });
    advancedLayout->addWidget(contrastRow);
    advancedLayout->addWidget(createDivider(m_advancedContainer));

    // Inside Advanced: 4.3 Border Width Slider
    QWidget* marqueeBorderRow = new QWidget(m_advancedContainer);
    marqueeBorderRow->setObjectName("settingsInnerRow");
    marqueeBorderRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QHBoxLayout* marqueeBorderLayout = new QHBoxLayout(marqueeBorderRow);
    marqueeBorderLayout->setContentsMargins(14, 8, 14, 8);
    marqueeBorderLayout->setSpacing(12);
    QVBoxLayout* marqueeBorderText = new QVBoxLayout();
    marqueeBorderText->setContentsMargins(0, 0, 0, 0);
    marqueeBorderText->setSpacing(2);
    QLabel* marqueeBorderTitle = new QLabel(tr("Border Width"), marqueeBorderRow);
    marqueeBorderTitle->setObjectName("rowTitle");
    marqueeBorderTitle->setWordWrap(true);
    marqueeBorderTitle->setMinimumWidth(0);
    marqueeBorderTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    marqueeBorderText->addWidget(marqueeBorderTitle);
    marqueeBorderLayout->addLayout(marqueeBorderText, 1);
    marqueeBorderLayout->addWidget(createGlyphLabel(glyph(0xE74B), marqueeBorderRow, 18, QColor("#5b6b86")));
    m_marqueeBorderSlider = new FluentSlider(Qt::Horizontal, marqueeBorderRow);
    m_marqueeBorderSlider->setRange(0, 30);
    const qreal savedBorderW = qBound(0.0, desktopSettings.value("Desktop/SelectionBorderWidth", 1.0).toReal(), 3.0);
    m_marqueeBorderSlider->setValue(qRound(savedBorderW * 10.0));
    m_marqueeBorderSlider->setMinimumWidth(120);
    m_marqueeBorderSlider->setMaximumWidth(160);
    m_marqueeBorderSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    marqueeBorderLayout->addWidget(m_marqueeBorderSlider, 2);

    auto formatBorderWidth = [](int val) -> QString {
        if (val <= 0) return QStringLiteral("0.0 px");
        return QStringLiteral("%1 px").arg(val / 10.0, 0, 'f', 1);
    };

    m_marqueeBorderValue = new QLabel(formatBorderWidth(m_marqueeBorderSlider->value()), marqueeBorderRow);
    m_marqueeBorderValue->setObjectName("valueLabel");
    m_marqueeBorderValue->setFixedWidth(44);
    m_marqueeBorderValue->setAlignment(Qt::AlignCenter);
    marqueeBorderLayout->addWidget(m_marqueeBorderValue);
    connect(m_marqueeBorderSlider, &QSlider::valueChanged, this, [this, formatBorderWidth](int value) {
        if (m_marqueeBorderValue) m_marqueeBorderValue->setText(formatBorderWidth(value));
        const qreal floatVal = value / 10.0;
        QSettings settings(settingsConfigPath(), QSettings::IniFormat);
        settings.setValue("Desktop/SelectionBorderWidth", floatVal);
        queueSettingsSync();
        updatePreview();
    });
    connect(m_marqueeBorderSlider, &QSlider::sliderReleased, this, [this]() {
        QSettings settings(settingsConfigPath(), QSettings::IniFormat);
        settings.sync();
        reloadSelectionOverlays();
    });
    advancedLayout->addWidget(marqueeBorderRow);
    advancedLayout->addWidget(createDivider(m_advancedContainer));

    // Inside Advanced: 4.4 Marquee Corner Radius Slider (with dynamic enabled state)
    m_marqueeRadiusRow = new QWidget(m_advancedContainer);
    m_marqueeRadiusRow->setObjectName("settingsInnerRow");
    m_marqueeRadiusRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QHBoxLayout* marqueeRadiusLayout = new QHBoxLayout(m_marqueeRadiusRow);
    marqueeRadiusLayout->setContentsMargins(14, 8, 14, 8);
    marqueeRadiusLayout->setSpacing(12);
    QVBoxLayout* marqueeRadiusText = new QVBoxLayout();
    marqueeRadiusText->setContentsMargins(0, 0, 0, 0);
    marqueeRadiusText->setSpacing(2);
    QLabel* marqueeRadiusTitle = new QLabel(tr("Marquee Corner Radius"), m_marqueeRadiusRow);
    marqueeRadiusTitle->setObjectName("rowTitle");
    marqueeRadiusTitle->setWordWrap(true);
    marqueeRadiusTitle->setMinimumWidth(0);
    marqueeRadiusTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    marqueeRadiusText->addWidget(marqueeRadiusTitle);
    marqueeRadiusLayout->addLayout(marqueeRadiusText, 1);
    marqueeRadiusLayout->addWidget(createGlyphLabel(glyph(0xE91B), m_marqueeRadiusRow, 18, QColor("#5b6b86")));
    m_marqueeRadiusSlider = new FluentSlider(Qt::Horizontal, m_marqueeRadiusRow);
    m_marqueeRadiusSlider->setRange(0, 16);
    m_marqueeRadiusSlider->setValue(qBound(0, desktopSettings.value("Desktop/SelectionCornerRadius", 5).toInt(), 16));
    m_marqueeRadiusSlider->setMinimumWidth(120);
    m_marqueeRadiusSlider->setMaximumWidth(160);
    m_marqueeRadiusSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    marqueeRadiusLayout->addWidget(m_marqueeRadiusSlider, 2);
    m_marqueeRadiusValue = new QLabel(QStringLiteral("%1 px").arg(m_marqueeRadiusSlider->value()), m_marqueeRadiusRow);
    m_marqueeRadiusValue->setObjectName("valueLabel");
    m_marqueeRadiusValue->setFixedWidth(44);
    m_marqueeRadiusValue->setAlignment(Qt::AlignCenter);
    marqueeRadiusLayout->addWidget(m_marqueeRadiusValue);
    connect(m_marqueeRadiusSlider, &QSlider::valueChanged, this, [this](int value) {
        if (m_marqueeRadiusValue) m_marqueeRadiusValue->setText(QStringLiteral("%1 px").arg(value));
        QSettings settings(settingsConfigPath(), QSettings::IniFormat);
        settings.setValue("Desktop/SelectionCornerRadius", value);
        queueSettingsSync();
        updatePreview();
    });
    connect(m_marqueeRadiusSlider, &QSlider::sliderReleased, this, [this]() {
        QSettings settings(settingsConfigPath(), QSettings::IniFormat);
        settings.sync();
        reloadSelectionOverlays();
    });
    advancedLayout->addWidget(m_marqueeRadiusRow);
    advancedLayout->addWidget(createDivider(m_advancedContainer));

    // Dynamic state updater for Marquee Radius
    auto updateMarqueeRadiusState = [this](bool enabled) {
        if (m_marqueeRadiusRow) {
            m_marqueeRadiusRow->setEnabled(enabled);
            if (m_marqueeRadiusSlider) m_marqueeRadiusSlider->setEnabled(enabled);
        }
    };
    updateMarqueeRadiusState(isFluentEnabled);

    connect(m_fluentSelectionToggle, &FluentToggleSwitch::toggled, this, [this, updateMarqueeRadiusState](bool checked) {
        QSettings settings(settingsConfigPath(), QSettings::IniFormat);
        settings.setValue("Desktop/FluentSelectionMarquee", checked);
        settings.sync();
        reloadSelectionOverlays();
        updateMarqueeRadiusState(checked);
        updatePreview();
    });

    // Inside Advanced: 4.5 Badge Corner Radius Slider (with dynamic enabled state)
    m_badgeRadiusRow = new QWidget(m_advancedContainer);
    m_badgeRadiusRow->setObjectName("settingsInnerRow");
    m_badgeRadiusRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QHBoxLayout* badgeRadiusLayout = new QHBoxLayout(m_badgeRadiusRow);
    badgeRadiusLayout->setContentsMargins(14, 8, 14, 8);
    badgeRadiusLayout->setSpacing(12);
    QVBoxLayout* badgeRadiusText = new QVBoxLayout();
    badgeRadiusText->setContentsMargins(0, 0, 0, 0);
    badgeRadiusText->setSpacing(2);
    QLabel* badgeRadiusTitle = new QLabel(tr("Badge Corner Radius"), m_badgeRadiusRow);
    badgeRadiusTitle->setObjectName("rowTitle");
    badgeRadiusTitle->setWordWrap(true);
    badgeRadiusTitle->setMinimumWidth(0);
    badgeRadiusTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    badgeRadiusText->addWidget(badgeRadiusTitle);
    badgeRadiusLayout->addLayout(badgeRadiusText, 1);
    badgeRadiusLayout->addWidget(createGlyphLabel(glyph(0xE7B8), m_badgeRadiusRow, 18, QColor("#5b6b86")));
    m_badgeRadiusSlider = new FluentSlider(Qt::Horizontal, m_badgeRadiusRow);
    m_badgeRadiusSlider->setRange(0, 11);
    m_badgeRadiusSlider->setValue(qBound(0, desktopSettings.value("Desktop/SelectionBadgeRadius", 11).toInt(), 11));
    m_badgeRadiusSlider->setMinimumWidth(120);
    m_badgeRadiusSlider->setMaximumWidth(160);
    m_badgeRadiusSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    badgeRadiusLayout->addWidget(m_badgeRadiusSlider, 2);
    m_badgeRadiusValue = new QLabel(QStringLiteral("%1 px").arg(m_badgeRadiusSlider->value()), m_badgeRadiusRow);
    m_badgeRadiusValue->setObjectName("valueLabel");
    m_badgeRadiusValue->setFixedWidth(44);
    m_badgeRadiusValue->setAlignment(Qt::AlignCenter);
    badgeRadiusLayout->addWidget(m_badgeRadiusValue);
    connect(m_badgeRadiusSlider, &QSlider::valueChanged, this, [this](int value) {
        if (m_badgeRadiusValue) m_badgeRadiusValue->setText(QStringLiteral("%1 px").arg(value));
        QSettings settings(settingsConfigPath(), QSettings::IniFormat);
        settings.setValue("Desktop/SelectionBadgeRadius", value);
        queueSettingsSync();
        updatePreview();
    });
    connect(m_badgeRadiusSlider, &QSlider::sliderReleased, this, [this]() {
        QSettings settings(settingsConfigPath(), QSettings::IniFormat);
        settings.sync();
        reloadSelectionOverlays();
    });
    advancedLayout->addWidget(m_badgeRadiusRow);

    // Dynamic state updater for Badge Radius
    auto updateBadgeRadiusState = [this](bool enabled) {
        if (m_badgeRadiusRow) {
            m_badgeRadiusRow->setEnabled(enabled);
            if (m_badgeRadiusSlider) m_badgeRadiusSlider->setEnabled(enabled);
        }
    };
    updateBadgeRadiusState(isBadgeEnabled);

    connect(m_selectionCountBadgeToggle, &FluentToggleSwitch::toggled, this, [this, updateBadgeRadiusState](bool checked) {
        QSettings settings(settingsConfigPath(), QSettings::IniFormat);
        settings.setValue("Desktop/SelectionCountBadgeEnabled", checked);
        settings.sync();
        reloadSelectionOverlays();
        updateBadgeRadiusState(checked);
        updatePreview();
    });

    selectionLayout->addWidget(m_advancedContainer);
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

    // Master Switch: Windows 11 Style Context Menu
    FluentToggleSwitch* win11Toggle = new FluentToggleSwitch(parent);
    win11Toggle->setChecked(RightMenu::isWindows11SystemContextMenuEnabled());
    win11Toggle->setEnabled(windows11ContextMenuSupported);
    if (!windows11ContextMenuSupported) {
        win11Toggle->setToolTip(windows11UnsupportedTip);
    }

    QWidget* win11Row = createSwitchRow(
        glyph(0xE8B8),
        tr("Windows 11 Style Context Menu"),
        windows11ContextMenuSupported
            ? tr("Use the LinDesk Windows 11 style desktop context menu when right-clicking empty space in ListView.")
            : windows11UnsupportedTip,
        win11Toggle,
        win11MenuPanel);
    if (!windows11ContextMenuSupported) {
        win11Row->setToolTip(windows11UnsupportedTip);
    }
    win11MenuLayout->addWidget(win11Row);
    win11MenuLayout->addWidget(createDivider(win11MenuPanel));

    // Sub-switch 1: Show New Box Menu
    FluentToggleSwitch* showNewBoxMenuToggle = new FluentToggleSwitch(parent);
    showNewBoxMenuToggle->setChecked(RightMenu::isNewBoxMenuItemEnabled());
    connect(showNewBoxMenuToggle, &FluentToggleSwitch::toggled, this, [showNewBoxMenuToggle, windows11ContextMenuSupported](bool checked) {
        if (!windows11ContextMenuSupported) {
            if (showNewBoxMenuToggle) {
                const QSignalBlocker blocker(showNewBoxMenuToggle);
                showNewBoxMenuToggle->setChecked(RightMenu::isNewBoxMenuItemEnabled());
            }
            return;
        }
        RightMenu::setNewBoxMenuItemEnabled(checked);
    });
    QWidget* newBoxRow = createSwitchRow(
        glyph(0xE8B7),
        tr("Show New Box Menu"),
        tr("Show the New Box entry in the desktop context menu."),
        showNewBoxMenuToggle,
        win11MenuPanel);
    win11MenuLayout->addWidget(newBoxRow);
    win11MenuLayout->addWidget(createDivider(win11MenuPanel));

    // Sub-switch 2: Show LinDesk Menu
    FluentToggleSwitch* showLinDeskMenuToggle = new FluentToggleSwitch(parent);
    showLinDeskMenuToggle->setChecked(RightMenu::isLinDeskMenuEnabled());
    connect(showLinDeskMenuToggle, &FluentToggleSwitch::toggled, this, [showLinDeskMenuToggle, windows11ContextMenuSupported](bool checked) {
        if (!windows11ContextMenuSupported) {
            if (showLinDeskMenuToggle) {
                const QSignalBlocker blocker(showLinDeskMenuToggle);
                showLinDeskMenuToggle->setChecked(RightMenu::isLinDeskMenuEnabled());
            }
            return;
        }
        RightMenu::setLinDeskMenuEnabled(checked);
    });
    QWidget* linDeskRow = createSwitchRow(
        glyph(0xE713),
        tr("Show LinDesk Menu"),
        tr("Show the LinDesk submenu and its actions in the desktop context menu."),
        showLinDeskMenuToggle,
        win11MenuPanel);
    win11MenuLayout->addWidget(linDeskRow);
    win11MenuLayout->addWidget(createDivider(win11MenuPanel));

    // Sub-switch 3: Show Third Party Shell Menus
    FluentToggleSwitch* showThirdPartyShellMenuToggle = new FluentToggleSwitch(parent);
    showThirdPartyShellMenuToggle->setChecked(RightMenu::isThirdPartyShellMenuEnabled());
    connect(showThirdPartyShellMenuToggle, &FluentToggleSwitch::toggled, this, [showThirdPartyShellMenuToggle, windows11ContextMenuSupported](bool checked) {
        if (!windows11ContextMenuSupported) {
            if (showThirdPartyShellMenuToggle) {
                const QSignalBlocker blocker(showThirdPartyShellMenuToggle);
                showThirdPartyShellMenuToggle->setChecked(RightMenu::isThirdPartyShellMenuEnabled());
            }
            return;
        }
        RightMenu::setThirdPartyShellMenuEnabled(checked);
    });
    QWidget* shellMenuRow = createSwitchRow(
        glyph(0xECAA),
        tr("Show Third Party Shell Menus"),
        tr("Show registered third party Shell menu items in the Windows 11 style context menu."),
        showThirdPartyShellMenuToggle,
        win11MenuPanel);
    win11MenuLayout->addWidget(shellMenuRow);

    // Dynamic availability controller for sub-toggles
    auto updateSubTogglesAvailability = [windows11ContextMenuSupported,
                                         windows11UnsupportedTip,
                                         showNewBoxMenuToggle, newBoxRow,
                                         showLinDeskMenuToggle, linDeskRow,
                                         showThirdPartyShellMenuToggle, shellMenuRow](bool enabled) {
        const bool avail = windows11ContextMenuSupported && enabled;
        showNewBoxMenuToggle->setEnabled(avail);
        newBoxRow->setEnabled(avail);
        showLinDeskMenuToggle->setEnabled(avail);
        linDeskRow->setEnabled(avail);
        showThirdPartyShellMenuToggle->setEnabled(avail);
        shellMenuRow->setEnabled(avail);

        if (!windows11ContextMenuSupported) {
            showNewBoxMenuToggle->setToolTip(windows11UnsupportedTip);
            newBoxRow->setToolTip(windows11UnsupportedTip);
            showLinDeskMenuToggle->setToolTip(windows11UnsupportedTip);
            linDeskRow->setToolTip(windows11UnsupportedTip);
            showThirdPartyShellMenuToggle->setToolTip(windows11UnsupportedTip);
            shellMenuRow->setToolTip(windows11UnsupportedTip);
        }
    };

    updateSubTogglesAvailability(win11Toggle->checked());

    connect(win11Toggle, &FluentToggleSwitch::toggled, this, [win11Toggle, windows11ContextMenuSupported, updateSubTogglesAvailability](bool checked) {
        if (!windows11ContextMenuSupported) {
            if (win11Toggle) {
                const QSignalBlocker blocker(win11Toggle);
                win11Toggle->setChecked(false);
            }
            return;
        }
        RightMenu::setWindows11SystemContextMenuEnabled(checked);
        updateSubTogglesAvailability(checked);
    });

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

    const bool showBadge = settings.value("Desktop/SelectionCountBadgeEnabled", false).toBool();

    if (isFluent) {
        painter.setRenderHint(QPainter::Antialiasing, true);
        const int selCornerInt = qBound(0, settings.value("Desktop/SelectionCornerRadius", 5).toInt(), 16);
        const int badgeCornerInt = qBound(0, settings.value("Desktop/SelectionBadgeRadius", 11).toInt(), 11);
        const qreal borderWVal = qBound(0.0, settings.value("Desktop/SelectionBorderWidth", 1.0).toReal(), 3.0);
        const qreal radius = selCornerInt;
        const qreal bWidth = borderWVal;

        // 1. Apple 级光学高透质感填充
        QLinearGradient fillGrad(selectionRect.topLeft(), selectionRect.bottomRight());
        QColor topFill = fillColor;
        topFill.setAlpha(qBound(24, topFill.alpha() * 70 / 100, 110));
        QColor bottomFill = fillColor;
        bottomFill.setAlpha(qBound(16, bottomFill.alpha() * 45 / 100, 80));
        fillGrad.setColorAt(0.0, topFill);
        fillGrad.setColorAt(1.0, bottomFill);
        painter.setBrush(fillGrad);

        // 2. 选框边框 (支持 0px 无描边 ~ 3px)
        if (bWidth > 0.05) {
            QColor strokeColor = borderColor;
            strokeColor.setAlpha(qBound(110, strokeColor.alpha() * 95 / 100, 220));
            painter.setPen(QPen(strokeColor, bWidth));
            const qreal halfW = bWidth / 2.0;
            if (radius > 0.1) {
                painter.drawRoundedRect(QRectF(selectionRect).adjusted(halfW, halfW, -halfW, -halfW), radius, radius);
            } else {
                painter.drawRect(QRectF(selectionRect).adjusted(halfW, halfW, -halfW, -halfW));
            }
        } else {
            painter.setPen(Qt::NoPen);
            if (radius > 0.1) {
                painter.drawRoundedRect(QRectF(selectionRect), radius, radius);
            } else {
                painter.drawRect(QRectF(selectionRect));
            }
        }

        // 3. 极细顶边反射 (仅在有描边时渲染)
        if (bWidth > 0.05 && selectionRect.width() > 14 && selectionRect.height() > 10) {
            painter.setPen(QPen(QColor(255, 255, 255, 45), 1.0));
            const qreal inset = radius > 0.1 ? (radius + 1.0) : 1.0;
            painter.drawLine(QPointF(selectionRect.left() + inset, selectionRect.top() + bWidth / 2.0),
                             QPointF(selectionRect.right() - inset, selectionRect.top() + bWidth / 2.0));
        }

    } else {
        const qreal borderWVal = qBound(0.0, settings.value("Desktop/SelectionBorderWidth", 1.0).toReal(), 3.0);
        painter.fillRect(selectionRect, fillColor);
        if (borderWVal > 0.05) {
            painter.setPen(QPen(borderColor, borderWVal));
            painter.setBrush(Qt::NoBrush);
            const qreal halfW = borderWVal / 2.0;
            painter.drawRect(QRectF(selectionRect).adjusted(halfW, halfW, -halfW, -halfW));
        }
    }

    // 4. 独立开关控制的极简微胶囊徽章 (纯净数字，现代毛玻璃选框与经典选框均全面支持)
    if (showBadge) {
        painter.setRenderHint(QPainter::Antialiasing, true);
        const int badgeCornerInt = qBound(0, settings.value("Desktop/SelectionBadgeRadius", 11).toInt(), 11);
        const QString badgeText = QStringLiteral("2");
        QFont badgeFont = painter.font();
        badgeFont.setPixelSize(10);
        badgeFont.setWeight(QFont::Medium);
        painter.setFont(badgeFont);

        QFontMetrics fm(badgeFont);
        const int textW = fm.horizontalAdvance(badgeText);
        const int badgeH = 18;
        const int padH = 6;
        const int badgeW = qMax(18, textW + padH * 2);

        const qreal badgeCorner = qBound(0.0, static_cast<qreal>(badgeCornerInt), badgeH / 2.0);
        const qreal badgeX = selectionRect.right() - badgeW - 5;
        const qreal badgeY = selectionRect.bottom() - badgeH - 5;
        const QRectF badgeRect(badgeX, badgeY, badgeW, badgeH);

        // a. 极轻柔环境微阴影
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 40));
        painter.drawRoundedRect(badgeRect.translated(0, 1.0), badgeCorner, badgeCorner);

        // b. 半透明极简微磨砂黑底
        painter.setBrush(QColor(22, 24, 28, 205));

        // c. 0.75px 极细微透描边
        painter.setPen(QPen(QColor(255, 255, 255, 40), 0.75));
        painter.drawRoundedRect(badgeRect, badgeCorner, badgeCorner);

        // d. 纯白清晰数字 (无红点，无任何多余元素，纯净通透)
        painter.setPen(QColor(250, 252, 255));
        painter.drawText(badgeRect, Qt::AlignCenter, badgeText);
    }

    // 4. Subtle inner card border
    painter.setClipping(false);
    painter.setPen(QPen(QColor(0, 0, 0, 32), 1));
    painter.drawRoundedRect(bounds.adjusted(0, 0, -1, -1), radius, radius);

    painter.end();
    m_desktopSelectionPreview->setPixmap(canvas);
}
