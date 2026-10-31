#include "ui/settings/pages/BoxSettingsPage.h"
#include "ui/settings/SettingsUICommon.h"
#include "box/LinBox.h"
#include "box/BoxManager.h"
#include "desktop/ListView.h"
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

BoxSettingsPage::BoxSettingsPage(LinBox* targetBox, const QVector<LinBox*>& allBoxes, QWidget* parent)
    : QWidget(parent), m_targetBox(targetBox)
{
    setAllBoxes(allBoxes);
    initUI();
}

void BoxSettingsPage::setAllBoxes(const QVector<LinBox*>& boxes)
{
    m_allBoxes.clear();
    m_allBoxes.reserve(boxes.size());
    for (LinBox* box : boxes) {
        m_allBoxes.append(box);
    }
}

void BoxSettingsPage::setTargetBox(LinBox* target)
{
    m_targetBox = target;
    updateControlsFromTarget();
}

LinBox* BoxSettingsPage::controlSourceBox() const
{
    if (m_targetBox) {
        return m_targetBox.data();
    }
    for (const QPointer<LinBox>& boxPtr : m_allBoxes) {
        if (LinBox* box = boxPtr.data()) {
            return box;
        }
    }
    for (const QPointer<LinBox>& boxPtr : BoxManager::instance()->allBoxes()) {
        if (LinBox* box = boxPtr.data()) {
            return box;
        }
    }
    return nullptr;
}

void BoxSettingsPage::updateControlsFromTarget()
{
    LinBox* sourceBox = controlSourceBox();
    if (!sourceBox) {
        return;
    }
    if (m_opacitySlider) {
        m_opacitySlider->setValue(qBound(m_opacitySlider->minimum(),
                                         qRound(sourceBox->backgroundOpacity() * 100.0f),
                                         m_opacitySlider->maximum()));
        m_opacityLabelValue->setText(QString("%1%").arg(m_opacitySlider->value()));
    }
    if (m_glassToggle) {
        m_glassToggle->setChecked(sourceBox->glassEffect());
    }
    if (m_boxColorValueEdit) {
        m_boxColorValueEdit->setText(sourceBox->backgroundColor().name().toUpper());
    }
    updateColorPresetIndicators(m_boxColorPresetsRow, sourceBox->backgroundColor());

    m_previewColor = sourceBox->backgroundColor();
    m_previewOpacity = sourceBox->backgroundOpacity();
    m_previewGlass = sourceBox->glassEffect();
    updatePreview();
}

void BoxSettingsPage::initUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(54, 34, 54, 36);
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignTop);

    QLabel* titleLabel = createPageTitle(tr("Box UI"), this);
    titleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(titleLabel, 0, Qt::AlignLeft);
    layout->addSpacing(8);

    // Box Style Panel (Compact Inline Preview)
    QWidget* stylePanel = createPanel(this);
    stylePanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QVBoxLayout* styleLayout = new QVBoxLayout(stylePanel);
    styleLayout->setContentsMargins(22, 18, 22, 18);
    styleLayout->setSpacing(0);

    styleLayout->addWidget(createCardTitle(tr("Box Appearance & Style"), stylePanel));
    styleLayout->addSpacing(4);
    styleLayout->addWidget(createCardSubtitle(tr("Preview and customize the theme color, opacity, and acrylic glass effect of your desktop boxes."), stylePanel));
    styleLayout->addSpacing(12);

    m_previewBackground = new QLabel(stylePanel);
    m_previewBackground->setFixedHeight(98);
    m_previewBackground->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_previewBackground->setAlignment(Qt::AlignCenter);
    m_previewBackground->setObjectName(QStringLiteral("previewSurface"));
    styleLayout->addWidget(m_previewBackground);
    styleLayout->addSpacing(14);

    QColor initialBoxColor = QColor("#63A8F5");
    if (LinBox* sourceBox = controlSourceBox()) {
        initialBoxColor = sourceBox->backgroundColor();
    } else if (BoxManager* bm = BoxManager::instance()) {
        initialBoxColor = bm->unifiedBackgroundColor();
    }

    QWidget* presetsRow = new QWidget(stylePanel);
    presetsRow->setObjectName("settingsInnerRow");
    presetsRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_boxColorPresetsRow = presetsRow;
    QHBoxLayout* presetsLayout = new QHBoxLayout(presetsRow);
    presetsLayout->setContentsMargins(12, 8, 12, 8);
    presetsLayout->setSpacing(8);
    QLabel* presetsLabel = new QLabel(tr("Presets"), presetsRow);
    presetsLabel->setObjectName("rowTitle");
    presetsLayout->addWidget(presetsLabel);
    presetsLayout->addStretch();

    m_boxColorValueEdit = new QLineEdit(presetsRow);
    m_boxColorValueEdit->setReadOnly(true);
    m_boxColorValueEdit->setAlignment(Qt::AlignCenter);
    m_boxColorValueEdit->setMinimumWidth(72);
    m_boxColorValueEdit->setMaximumWidth(84);
    m_boxColorValueEdit->setFixedHeight(30);
    m_boxColorValueEdit->setText(initialBoxColor.name().toUpper());
    m_boxColorValueEdit->setObjectName("valueEdit");
    presetsLayout->addWidget(m_boxColorValueEdit);

    auto applyBoxColor = [this](const QColor& color) {
        if (!color.isValid()) {
            return;
        }
        for (const QPointer<LinBox>& boxPtr : m_allBoxes) {
            if (LinBox* box = boxPtr.data()) {
                box->setBackgroundColor(color);
            }
        }
        if (BoxManager* bm = BoxManager::instance()) {
            bm->setAllBoxesBackgroundColor(color);
        }
        if (m_boxColorValueEdit) {
            m_boxColorValueEdit->setText(color.name().toUpper());
        }
        updateColorPresetIndicators(m_boxColorPresetsRow, color);
        m_previewColor = color;
        updatePreview();
        if (ListView* lv = ListView::instance()) {
            lv->requestSaveBoxes();
        }
    };

    const QVector<QColor> boxPresets = {
        QColor("#F0CF24"),
        QColor("#63A8F5"),
        QColor("#00C853"),
        QColor("#8B45E6"),
        QColor("#E94B98"),
        QColor("#FF8A24"),
        QColor("#AEB4BE")
    };
    for (const QColor& preset : boxPresets) {
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
        connect(button, &QPushButton::clicked, this, [applyBoxColor, preset]() {
            applyBoxColor(preset);
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
    updateColorPresetIndicators(presetsRow, initialBoxColor);

    connect(pickerButton, &QPushButton::clicked, this, [this, applyBoxColor]() {
        QColorDialog* dialog = new QColorDialog(m_previewColor, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(tr("Accent Color"));
        dialog->setOption(QColorDialog::NoButtons, true);
        connect(dialog, &QColorDialog::currentColorChanged, this, [applyBoxColor](const QColor& color) {
            applyBoxColor(color);
        });
        connect(dialog, &QColorDialog::colorSelected, this, [applyBoxColor](const QColor& color) {
            applyBoxColor(color);
        });
        dialog->open();
    });

    styleLayout->addWidget(presetsRow);
    styleLayout->addWidget(createDivider(stylePanel));

    // Opacity Slider
    m_opacitySlider = new QSlider(Qt::Horizontal, this);
    m_opacitySlider->setRange(5, 100);
    m_opacitySlider->setMinimumWidth(120);
    m_opacitySlider->setMaximumWidth(160);
    m_opacitySlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_opacityLabelValue = new QLabel(this);
    m_opacityLabelValue->setObjectName("valueLabel");
    m_opacityLabelValue->setFixedWidth(44);
    m_opacityLabelValue->setAlignment(Qt::AlignCenter);

    QWidget* opacityRow = new QWidget(stylePanel);
    opacityRow->setObjectName("settingsInnerRow");
    opacityRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QHBoxLayout* opacityLayout = new QHBoxLayout(opacityRow);
    opacityLayout->setContentsMargins(14, 8, 14, 8);
    opacityLayout->setSpacing(12);
    QVBoxLayout* opacityTextLayout = new QVBoxLayout();
    opacityTextLayout->setContentsMargins(0, 0, 0, 0);
    opacityTextLayout->setSpacing(2);
    QLabel* opacityTitle = new QLabel(tr("Background Opacity"), opacityRow);
    opacityTitle->setObjectName("rowTitle");
    opacityTitle->setWordWrap(true);
    opacityTitle->setMinimumWidth(0);
    opacityTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    QLabel* opacitySub = new QLabel(tr("Adjust the transparency of the box background."), opacityRow);
    opacitySub->setObjectName("rowSubtitle");
    opacitySub->setWordWrap(true);
    opacitySub->setMinimumWidth(0);
    opacitySub->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    opacityTextLayout->addWidget(opacityTitle);
    opacityTextLayout->addWidget(opacitySub);
    opacityLayout->addLayout(opacityTextLayout, 1);
    opacityLayout->addWidget(createGlyphLabel(glyph(0xEB48), opacityRow, 18, QColor("#5b6b86")));
    opacityLayout->addWidget(m_opacitySlider, 2);
    opacityLayout->addWidget(m_opacityLabelValue);

    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int v){
        for (const QPointer<LinBox>& boxPtr : m_allBoxes) {
            if (LinBox* box = boxPtr.data()) {
                box->setBackgroundOpacity(v / 100.0f);
            }
        }
        if (BoxManager* bm = BoxManager::instance()) {
            bm->setAllBoxesBackgroundOpacity(v / 100.0f);
        }
        m_opacityLabelValue->setText(QString("%1%").arg(v));
        m_previewOpacity = v / 100.0f;
        updatePreview();
        ListView* lv = ListView::instance();
        if (lv) lv->requestSaveBoxes();
    });
    styleLayout->addWidget(opacityRow);
    styleLayout->addWidget(createDivider(stylePanel));

    int initialOpacity = qRound(m_previewOpacity * 100.0f);
    if (LinBox* sourceBox = controlSourceBox()) {
        initialOpacity = qRound(sourceBox->backgroundOpacity() * 100.0f);
    } else if (BoxManager* bm = BoxManager::instance()) {
        initialOpacity = qRound(bm->unifiedBackgroundOpacity() * 100.0f);
    }
    initialOpacity = qBound(m_opacitySlider->minimum(), initialOpacity, m_opacitySlider->maximum());
    m_opacitySlider->setValue(initialOpacity);
    m_opacityLabelValue->setText(QString("%1%").arg(initialOpacity));

    // Glass Effect Toggle
    m_glassToggle = new FluentToggleSwitch(this);
    bool initialGlass = false;
    if (LinBox* sourceBox = controlSourceBox()) {
        initialGlass = sourceBox->glassEffect();
    } else if (BoxManager* bm = BoxManager::instance()) {
        initialGlass = bm->unifiedGlassEffect();
    }
    m_glassToggle->setChecked(initialGlass, false);

    connect(m_glassToggle, &FluentToggleSwitch::toggled, this, [this](bool v){
        for (const QPointer<LinBox>& boxPtr : m_allBoxes) {
            if (LinBox* box = boxPtr.data()) {
                box->setGlassEffect(v);
            }
        }
        if (BoxManager* bm = BoxManager::instance()) {
            bm->setAllBoxesGlassEffect(v);
        }
        m_previewGlass = v;
        updatePreview();
        ListView* lv = ListView::instance();
        if (lv) lv->requestSaveBoxes();
    });

    styleLayout->addWidget(createSwitchRow(glyph(0xE91B), tr("Acrylic Glass Effect"), tr("Enable real-time DWM background blur for a translucent look."), m_glassToggle, stylePanel));
    styleLayout->addWidget(createDivider(stylePanel));

    // Auto Peek Toggles
    QSettings boxSettings(AppPaths::configFilePath(), QSettings::IniFormat);
    bool initialAutoPeek = boxSettings.value(QStringLiteral("Box/AutoPeekOnHover"), true).toBool();
    bool initialAutoPeekInstant = boxSettings.value(QStringLiteral("Box/AutoPeekInstant"), false).toBool();

    FluentToggleSwitch* autoPeekToggle = new FluentToggleSwitch(this);
    autoPeekToggle->setChecked(initialAutoPeek, false);
    connect(autoPeekToggle, &FluentToggleSwitch::toggled, this, [](bool v){
        QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("Box/AutoPeekOnHover"), v);
        settings.sync();
        if (BoxManager* bm = BoxManager::instance()) {
            for (const auto& boxPtr : bm->allBoxes()) {
                if (LinBox* box = boxPtr.data()) {
                    box->setAutoPeekEnabled(v);
                }
            }
        }
    });
    styleLayout->addWidget(createSwitchRow(glyph(0xE7B3), tr("Auto-Peek on Hover"), tr("Automatically expand collapsed boxes when hovered, and roll up when the mouse leaves."), autoPeekToggle, stylePanel));
    styleLayout->addWidget(createDivider(stylePanel));

    FluentToggleSwitch* autoPeekInstantToggle = new FluentToggleSwitch(this);
    autoPeekInstantToggle->setChecked(initialAutoPeekInstant, false);
    connect(autoPeekInstantToggle, &FluentToggleSwitch::toggled, this, [](bool v){
        QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("Box/AutoPeekInstant"), v);
        settings.sync();
        if (BoxManager* bm = BoxManager::instance()) {
            for (const auto& boxPtr : bm->allBoxes()) {
                if (LinBox* box = boxPtr.data()) {
                    box->setAutoPeekInstant(v);
                }
            }
        }
    });
    styleLayout->addWidget(createSwitchRow(glyph(0xE823), tr("Instant Auto-Peek"), tr("Expand immediately on hover without a 150ms debounce delay."), autoPeekInstantToggle, stylePanel));

    layout->addWidget(stylePanel);

    if (LinBox* sourceBox = controlSourceBox()) {
        m_previewColor = sourceBox->backgroundColor();
        m_previewOpacity = sourceBox->backgroundOpacity();
        m_previewGlass = sourceBox->glassEffect();
    } else {
        m_previewColor = QColor(40, 40, 40);
    }
    updatePreview();

    layout->addStretch();
}

void BoxSettingsPage::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updatePreview();
}

void BoxSettingsPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    QTimer::singleShot(0, this, [this]() {
        updatePreview();
    });
}

void BoxSettingsPage::updatePreview()
{
    if (!m_previewBackground) return;

    QSize previewSize = m_previewBackground->contentsRect().size();
    if (previewSize.width() < 10 || previewSize.height() < 10) {
        previewSize = QSize(qMax(280, width() - 88), 98);
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
    // Subtle darkening vignette
    painter.fillRect(bounds, QColor(0, 0, 0, 20));

    // 2. Miniature LinBox geometry
    const int boxW = qBound(220, previewSize.width() * 64 / 100, 360);
    const int boxH = 74;
    const int boxX = (previewSize.width() - boxW) / 2;
    const int boxY = (previewSize.height() - boxH) / 2;
    const QRect boxRect(boxX, boxY, boxW, boxH);
    const int boxRadius = 7;

    // Drop shadow
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 42));
    painter.drawRoundedRect(boxRect.translated(0, 3), boxRadius, boxRadius);

    // Box background
    QColor baseColor = m_previewColor.isValid() ? m_previewColor : QColor(40, 40, 40);
    int alpha = m_previewGlass ? 152 : qBound(30, qRound(m_previewOpacity * 255.0f), 245);
    baseColor.setAlpha(alpha);

    painter.setBrush(baseColor);
    painter.drawRoundedRect(boxRect, boxRadius, boxRadius);

    // If acrylic glass is enabled, overlay a soft glass reflection gradient
    if (m_previewGlass) {
        QLinearGradient glassTint(boxX, boxY, boxX, boxY + boxH);
        glassTint.setColorAt(0.0, QColor(255, 255, 255, 40));
        glassTint.setColorAt(0.4, QColor(255, 255, 255, 10));
        glassTint.setColorAt(1.0, QColor(255, 255, 255, 18));
        painter.setBrush(glassTint);
        painter.drawRoundedRect(boxRect, boxRadius, boxRadius);
    }

    // LinBox Title Bar
    const int titleH = 22;
    QColor titleColor = baseColor.darker(115);
    titleColor.setAlpha(qBound(110, baseColor.alpha() + 45, 245));
    QPainterPath titlePath;
    titlePath.addRoundedRect(QRect(boxX, boxY, boxW, titleH), boxRadius, boxRadius);
    titlePath.addRect(QRect(boxX, boxY + titleH - boxRadius, boxW, boxRadius));
    painter.fillPath(titlePath, titleColor);

    // Title separator line
    painter.setPen(QPen(QColor(255, 255, 255, 30), 1));
    painter.drawLine(boxX, boxY + titleH, boxX + boxW, boxY + titleH);

    // Title text
    QString boxTitle = tr("LinBox");
    if (LinBox* sourceBox = controlSourceBox()) {
        if (!sourceBox->boxTitle().trimmed().isEmpty()) {
            boxTitle = sourceBox->boxTitle().trimmed();
        }
    }
    QFont titleFont(AppFonts::uiFamily(), 9, QFont::Bold);
    painter.setFont(titleFont);
    painter.setPen(QColor(255, 255, 255, 225));
    QFontMetrics fm(titleFont);
    QString elidedTitle = fm.elidedText(boxTitle, Qt::ElideRight, boxW - 55);
    painter.drawText(QRect(boxX + 10, boxY, boxW - 55, titleH), Qt::AlignVCenter | Qt::AlignLeft, elidedTitle);

    // Title bar mini buttons (chevron and view switch)
    painter.setPen(QColor(255, 255, 255, 160));
    QFont btnFont("Segoe Fluent Icons");
    if (!btnFont.exactMatch()) btnFont.setFamily("Segoe MDL2 Assets");
    btnFont.setPixelSize(10);
    painter.setFont(btnFont);
    painter.drawText(QRect(boxX + boxW - 36, boxY, 16, titleH), Qt::AlignCenter, glyph(0xE712)); // view switch
    painter.drawText(QRect(boxX + boxW - 18, boxY, 14, titleH), Qt::AlignCenter, glyph(0xE70E)); // chevron

    // Box content area (3 miniature items cleanly arranged)
    const int contentY = boxY + titleH + (boxH - titleH - 36) / 2;
    const int slotW = 34;
    const int slotSpacing = 14;
    const int totalSlotsW = slotW * 3 + slotSpacing * 2;
    const int startSlotX = boxX + (boxW - totalSlotsW) / 2;

    for (int i = 0; i < 3; ++i) {
        const int sx = startSlotX + i * (slotW + slotSpacing);
        painter.setPen(Qt::NoPen);
        if (i == 0) {
            painter.setBrush(QColor(80, 160, 245, 200)); // Document
        } else if (i == 1) {
            painter.setBrush(QColor(250, 190, 60, 210)); // Folder
        } else {
            painter.setBrush(QColor(80, 210, 140, 200)); // App/Media
        }
        painter.drawRoundedRect(sx + 7, contentY + 3, 20, 18, 3, 3);
        painter.setBrush(QColor(255, 255, 255, 160));
        painter.drawRoundedRect(sx + 3, contentY + 25, 28, 5, 2, 2);
    }

    // Box outer border
    painter.setPen(QPen(QColor(255, 255, 255, 55), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(boxRect.adjusted(0, 0, -1, -1), boxRadius, boxRadius);

    // Inner card border
    painter.setClipping(false);
    painter.setPen(QPen(QColor(0, 0, 0, 32), 1));
    painter.drawRoundedRect(bounds.adjusted(0, 0, -1, -1), radius, radius);

    painter.end();
    m_previewBackground->setPixmap(canvas);
}
