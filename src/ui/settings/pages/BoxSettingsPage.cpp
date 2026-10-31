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

    // 1. Live Preview Panel
    QWidget* previewContainer = createPanel(this);
    m_boxPreviewContainer = previewContainer;
    previewContainer->setFixedHeight(220);
    previewContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    QVBoxLayout* previewLayout = new QVBoxLayout(previewContainer);
    previewLayout->setContentsMargins(22, 10, 22, 20);
    previewLayout->setSpacing(6);

    previewLayout->addWidget(createCardTitle(tr("Live Preview"), previewContainer));
    previewLayout->addWidget(createCardSubtitle(tr("See how your desktop box surface reacts to the current style."), previewContainer));

    m_previewBackground = new QLabel(previewContainer);
    m_previewBackground->setMinimumHeight(124);
    m_previewBackground->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_previewBackground->setScaledContents(false);
    m_previewBackground->setObjectName("previewSurface");
    m_previewBackground->setAlignment(Qt::AlignCenter);
    previewLayout->addWidget(m_previewBackground);

    layout->addWidget(previewContainer, 0);

    // 2. Box Style Panel
    QWidget* stylePanel = createPanel(this);
    stylePanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QVBoxLayout* styleLayout = new QVBoxLayout(stylePanel);
    styleLayout->setContentsMargins(22, 18, 22, 18);
    styleLayout->setSpacing(0);

    styleLayout->addWidget(createCardTitle(tr("Accent Color"), stylePanel));
    styleLayout->addSpacing(12);

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

void BoxSettingsPage::updatePreview()
{
    if (!m_previewBackground) return;

    QSize previewSize = m_previewBackground->contentsRect().size();
    if (previewSize.width() < 2 || previewSize.height() < 2) {
        previewSize = QSize(540, 202);
    }
    const int canvasW = previewSize.width();
    const int canvasH = previewSize.height();
    const QRect imageRect = centeredPreviewImageRect(previewSize);

    QPixmap canvas(canvasW, canvasH);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    QPainterPath clipPath;
    clipPath.addRoundedRect(imageRect.adjusted(0, 0, -1, -1), 10, 10);
    painter.setClipPath(clipPath);

    QPixmap bgPixmap(":/Assets/BoxPreview.png");
    if (!bgPixmap.isNull()) {
        painter.drawPixmap(imageRect, bgPixmap.scaled(imageRect.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    } else {
        QLinearGradient grad(imageRect.topLeft(), imageRect.bottomRight());
        grad.setColorAt(0, QColor(140, 205, 255));
        grad.setColorAt(0.52, QColor(102, 178, 226));
        grad.setColorAt(1, QColor(34, 118, 172));
        painter.fillRect(imageRect, grad);
    }

    const int boxMaxW = qMin(430, qMax(1, imageRect.width() - 24));
    const int boxMinW = qMin(220, boxMaxW);
    const int boxMaxH = qMin(160, qMax(1, imageRect.height() - 18));
    const int boxMinH = qMin(92, boxMaxH);
    const int boxW = qBound(boxMinW, imageRect.width() * 58 / 100, boxMaxW);
    const int boxH = qBound(boxMinH, imageRect.height() * 58 / 100, boxMaxH);
    const int boxX = imageRect.left() + (imageRect.width() - boxW) / 2;
    const int boxY = imageRect.top() + (imageRect.height() - boxH) / 2;
    const int titleBarH = 38;

    QRect boxRect(boxX, boxY, boxW, boxH);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(15, 23, 42, 54));
    painter.drawRoundedRect(boxRect.adjusted(0, 8, 0, 12), 12, 12);

    QColor boxColor = m_previewColor.isValid() ? m_previewColor : QColor(40, 40, 40);
    boxColor.setAlpha(m_previewGlass ? 156 : qBound(70, static_cast<int>(m_previewOpacity * 255), 220));
    painter.setBrush(boxColor);
    painter.drawRoundedRect(boxRect, 12, 12);

    QColor titleColor = boxColor.darker(128);
    titleColor.setAlpha(qBound(120, boxColor.alpha() + 34, 235));
    QPainterPath titlePath;
    titlePath.addRoundedRect(QRect(boxX, boxY, boxW, titleBarH), 12, 12);
    titlePath.addRect(boxX, boxY + titleBarH - 12, boxW, 12);
    painter.fillPath(titlePath, titleColor);

    painter.setPen(QPen(QColor(255, 255, 255, 70), 1));
    painter.drawRoundedRect(boxRect.adjusted(0, 0, -1, -1), 12, 12);

    QFont iconFont("Segoe Fluent Icons");
    if (iconFont.exactMatch() == false) {
        iconFont.setFamily("Segoe MDL2 Assets");
    }
    iconFont.setPixelSize(15);
    painter.setFont(iconFont);
    painter.setPen(QColor(255, 255, 255, 210));
    painter.drawText(QRect(boxX + 12, boxY + 7, 24, 24), Qt::AlignCenter, glyph(0xE70E));
    painter.drawText(QRect(boxX + boxW - 38, boxY + 7, 24, 24), Qt::AlignCenter, glyph(0xE712));

    painter.setPen(Qt::white);
    QFont titleFont(AppFonts::uiFamily(), 11, QFont::Bold);
    painter.setFont(titleFont);
    QFontMetrics fm(titleFont);
    QString title = tr("Preview Box");
    QString elidedTitle = fm.elidedText(title, Qt::ElideRight, boxW - 60);
    painter.drawText(QRect(boxX + 44, boxY + 7, boxW - 88, 24), Qt::AlignCenter, elidedTitle);

    painter.setPen(QColor(255, 255, 255, 200));
    iconFont.setPixelSize(18);
    painter.setFont(iconFont);

    const int iconSize = 48;
    const int iconSpacing = 62;
    const int startX = boxX + (boxW - iconSpacing * 3 - iconSize) / 2;
    const int iconY = boxY + titleBarH + (boxH - titleBarH - iconSize) / 2;

    QStringList iconSymbols = {glyph(0xE7C3), glyph(0xE8A5), glyph(0xE91B), glyph(0xE721)};
    for (int i = 0; i < iconSymbols.size(); ++i) {
        int ix = startX + i * iconSpacing;
        if (ix + iconSize <= boxX + boxW - 10) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 255, 255, 30));
            painter.drawRoundedRect(ix, iconY, iconSize, iconSize, 10, 10);
            painter.setPen(QColor(255, 255, 255, 200));
            painter.setFont(iconFont);
            painter.drawText(QRect(ix, iconY, iconSize, iconSize), Qt::AlignCenter, iconSymbols[i]);
        }
    }

    painter.end();
    m_previewBackground->setPixmap(canvas);
}
