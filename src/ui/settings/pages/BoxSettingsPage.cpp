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
#include <QLinearGradient>
#include <QImage>
#include <QPolygon>
#include <QSettings>
#include <QTimer>

using namespace SettingsUICommon;

namespace {

QImage fastBoxBlurImage(const QImage& source, int radius)
{
    if (source.isNull() || radius <= 0) {
        return source;
    }
    const QImage input = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int w = input.width();
    const int h = input.height();
    QImage temp(w, h, QImage::Format_ARGB32_Premultiplied);
    QImage output(w, h, QImage::Format_ARGB32_Premultiplied);

    // Horizontal pass
    for (int y = 0; y < h; ++y) {
        const QRgb* srcLine = reinterpret_cast<const QRgb*>(input.constScanLine(y));
        QRgb* dstLine = reinterpret_cast<QRgb*>(temp.scanLine(y));
        for (int x = 0; x < w; ++x) {
            int a = 0, r = 0, g = 0, b = 0;
            int count = 0;
            for (int dx = -radius; dx <= radius; ++dx) {
                const int nx = qBound(0, x + dx, w - 1);
                const QRgb pixel = srcLine[nx];
                a += qAlpha(pixel);
                r += qRed(pixel);
                g += qGreen(pixel);
                b += qBlue(pixel);
                ++count;
            }
            dstLine[x] = qRgba(r / count, g / count, b / count, a / count);
        }
    }

    // Vertical pass
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            int a = 0, r = 0, g = 0, b = 0;
            int count = 0;
            for (int dy = -radius; dy <= radius; ++dy) {
                const int ny = qBound(0, y + dy, h - 1);
                const QRgb pixel = reinterpret_cast<const QRgb*>(temp.constScanLine(ny))[x];
                a += qAlpha(pixel);
                r += qRed(pixel);
                g += qGreen(pixel);
                b += qBlue(pixel);
                ++count;
            }
            reinterpret_cast<QRgb*>(output.scanLine(y))[x] = qRgba(r / count, g / count, b / count, a / count);
        }
    }

    return output;
}

struct MockItemDef {
    enum Type { Doc, Folder, Image, Code, Archive };
    Type type;
    QString name;
};

} // namespace

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
    if (m_layeredDepthToggle) {
        m_layeredDepthToggle->setChecked(sourceBox->layeredZDepth());
    }
    if (m_adaptiveTintToggle) {
        m_adaptiveTintToggle->setChecked(sourceBox->adaptiveWallpaperTint());
    }
    if (m_dragPulseToggle) {
        m_dragPulseToggle->setChecked(sourceBox->activeDragPulse());
    }
    if (m_boxColorValueEdit) {
        m_boxColorValueEdit->setText(sourceBox->backgroundColor().name().toUpper());
    }
    updateColorPresetIndicators(m_boxColorPresetsRow, sourceBox->backgroundColor());

    m_previewColor = sourceBox->backgroundColor();
    m_previewOpacity = sourceBox->backgroundOpacity();
    m_previewGlass = sourceBox->glassEffect();
    m_previewLayeredDepth = sourceBox->layeredZDepth();
    m_previewAdaptiveTint = sourceBox->adaptiveWallpaperTint();
    m_previewDragPulse = sourceBox->activeDragPulse();
    updatePreview();
}

void BoxSettingsPage::initUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(54, 34, 54, 36);
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignTop);

    QLabel* titleLabel = createPageTitle(tr("Box"), this);
    titleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(titleLabel, 0, Qt::AlignLeft);
    layout->addSpacing(8);

    // 1. Box Appearance & Theme Panel
    QWidget* stylePanel = createPanel(this);
    stylePanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QVBoxLayout* styleLayout = new QVBoxLayout(stylePanel);
    styleLayout->setContentsMargins(22, 18, 22, 18);
    styleLayout->setSpacing(0);

    styleLayout->addWidget(createCardTitle(tr("Appearance & Theme"), stylePanel));
    styleLayout->addSpacing(4);
    styleLayout->addWidget(createCardSubtitle(tr("Preview and customize the theme color, opacity, and acrylic glass effect of your desktop boxes."), stylePanel));
    styleLayout->addSpacing(12);

    m_previewBackground = new QLabel(stylePanel);
    m_previewBackground->setFixedHeight(128);
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
    QLabel* presetsLabel = new QLabel(tr("Theme Color"), presetsRow);
    presetsLabel->setObjectName("rowTitle");
    presetsLayout->addWidget(presetsLabel);
    presetsLayout->addStretch();

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

    presetsLayout->addSpacing(6);
    m_boxColorValueEdit = new QLineEdit(presetsRow);
    m_boxColorValueEdit->setReadOnly(true);
    m_boxColorValueEdit->setAlignment(Qt::AlignCenter);
    m_boxColorValueEdit->setMinimumWidth(72);
    m_boxColorValueEdit->setMaximumWidth(84);
    m_boxColorValueEdit->setFixedHeight(28);
    m_boxColorValueEdit->setText(initialBoxColor.name().toUpper());
    m_boxColorValueEdit->setObjectName("valueEdit");
    presetsLayout->addWidget(m_boxColorValueEdit);

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

    // Layered Depth Toggle
    m_layeredDepthToggle = new FluentToggleSwitch(this);
    bool initialLayeredDepth = true;
    if (LinBox* sourceBox = controlSourceBox()) {
        initialLayeredDepth = sourceBox->layeredZDepth();
    } else if (BoxManager* bm = BoxManager::instance()) {
        initialLayeredDepth = bm->unifiedLayeredZDepth();
    }
    m_layeredDepthToggle->setChecked(initialLayeredDepth, false);
    connect(m_layeredDepthToggle, &FluentToggleSwitch::toggled, this, [this](bool v){
        QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("Box/LayeredZDepth"), v);
        settings.sync();
        if (BoxManager* bm = BoxManager::instance()) {
            bm->setAllBoxesLayeredZDepth(v);
        }
        for (const auto& boxPtr : m_allBoxes) {
            if (LinBox* box = boxPtr.data()) {
                box->setLayeredZDepth(v);
            }
        }
        m_previewLayeredDepth = v;
        updatePreview();
        if (ListView* lv = ListView::instance()) {
            lv->requestSaveBoxes();
        }
    });
    styleLayout->addWidget(createSwitchRow(glyph(0xE91B), tr("Layered Glass Depth"), tr("Render high-specular titlebar and deep frosted body with 3D glass depth."), m_layeredDepthToggle, stylePanel));
    styleLayout->addWidget(createDivider(stylePanel));

    // Adaptive Tint Toggle
    m_adaptiveTintToggle = new FluentToggleSwitch(this);
    bool initialAdaptiveTint = false;
    if (LinBox* sourceBox = controlSourceBox()) {
        initialAdaptiveTint = sourceBox->adaptiveWallpaperTint();
    } else if (BoxManager* bm = BoxManager::instance()) {
        initialAdaptiveTint = bm->unifiedAdaptiveWallpaperTint();
    }
    m_adaptiveTintToggle->setChecked(initialAdaptiveTint, false);
    connect(m_adaptiveTintToggle, &FluentToggleSwitch::toggled, this, [this](bool v){
        QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("Box/AdaptiveWallpaperTint"), v);
        settings.sync();
        if (BoxManager* bm = BoxManager::instance()) {
            bm->setAllBoxesAdaptiveWallpaperTint(v);
        }
        for (const auto& boxPtr : m_allBoxes) {
            if (LinBox* box = boxPtr.data()) {
                box->setAdaptiveWallpaperTint(v);
            }
        }
        m_previewAdaptiveTint = v;
        updatePreview();
        if (ListView* lv = ListView::instance()) {
            lv->requestSaveBoxes();
        }
    });
    styleLayout->addWidget(createSwitchRow(glyph(0xE790), tr("Adaptive Wallpaper Color"), tr("Automatically extract and blend colors from the underlying desktop wallpaper."), m_adaptiveTintToggle, stylePanel));

    layout->addWidget(stylePanel, 0);
    layout->addSpacing(14);

    // 2. Box Behavior Panel
    QWidget* behaviorPanel = createPanel(this);
    behaviorPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QVBoxLayout* behaviorLayout = new QVBoxLayout(behaviorPanel);
    behaviorLayout->setContentsMargins(22, 18, 22, 18);
    behaviorLayout->setSpacing(0);

    behaviorLayout->addWidget(createCardTitle(tr("Box Behavior"), behaviorPanel));
    behaviorLayout->addSpacing(4);
    behaviorLayout->addWidget(createCardSubtitle(tr("Configure how boxes interact with mouse hover and collapsing."), behaviorPanel));
    behaviorLayout->addSpacing(12);

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
    behaviorLayout->addWidget(createSwitchRow(glyph(0xE7B3), tr("Auto-Peek on Hover"), tr("Automatically expand collapsed boxes when hovered, and roll up when the mouse leaves."), autoPeekToggle, behaviorPanel));
    behaviorLayout->addWidget(createDivider(behaviorPanel));

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
    behaviorLayout->addWidget(createSwitchRow(glyph(0xE823), tr("Instant Auto-Peek"), tr("Expand immediately on hover without a 150ms debounce delay."), autoPeekInstantToggle, behaviorPanel));
    behaviorLayout->addWidget(createDivider(behaviorPanel));

    // Active Drag Pulse Toggle
    m_dragPulseToggle = new FluentToggleSwitch(this);
    bool initialDragPulse = true;
    if (LinBox* sourceBox = controlSourceBox()) {
        initialDragPulse = sourceBox->activeDragPulse();
    } else if (BoxManager* bm = BoxManager::instance()) {
        initialDragPulse = bm->unifiedActiveDragPulse();
    }
    m_dragPulseToggle->setChecked(initialDragPulse, false);
    connect(m_dragPulseToggle, &FluentToggleSwitch::toggled, this, [this](bool v){
        QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("Box/ActiveDragPulse"), v);
        settings.sync();
        if (BoxManager* bm = BoxManager::instance()) {
            bm->setAllBoxesActiveDragPulse(v);
        }
        for (const auto& boxPtr : m_allBoxes) {
            if (LinBox* box = boxPtr.data()) {
                box->setActiveDragPulse(v);
            }
        }
        m_previewDragPulse = v;
        updatePreview();
    });
    behaviorLayout->addWidget(createSwitchRow(glyph(0xE896), tr("Active Drop Pulse & Glow"), tr("Display an ambient glowing border and magnetic pulse when dragging files over the box."), m_dragPulseToggle, behaviorPanel));

    layout->addWidget(behaviorPanel, 0);

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
        previewSize = QSize(qMax(280, width() - 88), 128);
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

    // 1. Wallpaper background with rounded clip
    QPainterPath clipPath;
    clipPath.addRoundedRect(bounds, radius, radius);
    painter.setClipPath(clipPath);

    QPixmap wp = getDesktopWallpaperThumbnail(previewSize, dpr);
    painter.drawPixmap(0, 0, wp);
    // Subtle darkening vignette for contrast
    painter.fillRect(bounds, QColor(0, 0, 0, 22));

    // 2. Realistic 1:1 Scale LinBox Geometry
    const int boxMarginX = 14;
    const int boxMarginY = 10;
    const int maxBoxW = 520;
    const int boxW = qBound(280, qMin(previewSize.width() - boxMarginX * 2, maxBoxW), previewSize.width() - 8);
    const int boxH = qMax(80, previewSize.height() - boxMarginY * 2);
    const int boxX = (previewSize.width() - boxW) / 2;
    const int boxY = boxMarginY;
    const QRect boxRect(boxX, boxY, boxW, boxH);
    const int boxRadius = 8;

    // Box background base color
    QColor baseColor = m_previewColor.isValid() ? m_previewColor : QColor(40, 40, 40);
    if (m_previewAdaptiveTint) {
        QColor adaptive = LinBox::extractWallpaperDominantColor(wp);
        if (adaptive.isValid()) {
            baseColor = adaptive;
        }
    }

    // Drop shadow & Ambient Glow Pulse
    painter.setPen(Qt::NoPen);
    if (m_previewDragPulse) {
        QColor haloColor = baseColor;
        haloColor.setAlpha(36);
        painter.setBrush(haloColor);
        painter.drawRoundedRect(boxRect.adjusted(-3, -3, 3, 3), boxRadius + 2, boxRadius + 2);
    }
    painter.setBrush(QColor(0, 0, 0, 30));
    painter.drawRoundedRect(QRect(boxX - 1, boxY + 3, boxW + 2, boxH + 2), boxRadius, boxRadius);
    painter.setBrush(QColor(0, 0, 0, 48));
    painter.drawRoundedRect(QRect(boxX, boxY + 2, boxW, boxH), boxRadius, boxRadius);

    if (m_previewGlass) {
        // True Acrylic Glass: crop wallpaper under box and apply smooth blur
        QImage wpImg = wp.toImage();
        const int pxBoxX = qRound(boxX * dpr);
        const int pxBoxY = qRound(boxY * dpr);
        const int pxBoxW = qRound(boxW * dpr);
        const int pxBoxH = qRound(boxH * dpr);
        const QRect pxBoxRect = QRect(pxBoxX, pxBoxY, pxBoxW, pxBoxH).intersected(QRect(0, 0, wpImg.width(), wpImg.height()));

        if (pxBoxRect.isValid() && !pxBoxRect.isEmpty()) {
            QImage subWp = wpImg.copy(pxBoxRect);
            const QSize downSize(qMax(16, subWp.width() / 4), qMax(12, subWp.height() / 4));
            QImage downSampled = subWp.scaled(downSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            QImage blurredSmall = fastBoxBlurImage(downSampled, 3);
            blurredSmall = fastBoxBlurImage(blurredSmall, 2);
            QImage blurredFinal = blurredSmall.scaled(pxBoxRect.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            QPixmap blurredPx = QPixmap::fromImage(blurredFinal);
            blurredPx.setDevicePixelRatio(dpr);

            QPainterPath boxClip;
            boxClip.addRoundedRect(boxRect, boxRadius, boxRadius);
            painter.save();
            painter.setClipPath(boxClip);
            painter.drawPixmap(boxX, boxY, blurredPx);

            // Acrylic Tint Overlay
            QColor acrylicTint = baseColor;
            acrylicTint.setAlpha(qBound(60, qRound(m_previewOpacity * 170.0f), 180));
            painter.fillRect(boxRect, acrylicTint);

            // Specular Reflection (Windows 11 Mica top reflection)
            QLinearGradient specular(boxX, boxY, boxX, boxY + boxH);
            specular.setColorAt(0.0, QColor(255, 255, 255, 42));
            specular.setColorAt(0.28, QColor(255, 255, 255, 12));
            specular.setColorAt(1.0, QColor(255, 255, 255, 0));
            painter.fillRect(boxRect, specular);
            painter.restore();
        }
    } else {
        // Standard translucent solid surface
        QColor solidBg = baseColor;
        solidBg.setAlpha(qBound(20, qRound(m_previewOpacity * 255.0f), 255));
        painter.setBrush(solidBg);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(boxRect, boxRadius, boxRadius);
    }

    // 3. LinBox Title Bar (Height: 28px)
    const int titleH = 28;
    QColor titleColor = baseColor.darker(118);
    int titleAlpha = m_previewGlass ? qBound(100, baseColor.alpha() + 50, 220)
                                    : qBound(60, qRound(m_previewOpacity * 255.0f) + 40, 255);
    titleColor.setAlpha(titleAlpha);

    QPainterPath titlePath;
    titlePath.addRoundedRect(QRect(boxX, boxY, boxW, titleH), boxRadius, boxRadius);
    titlePath.addRect(QRect(boxX, boxY + titleH - boxRadius, boxW, boxRadius));
    painter.fillPath(titlePath, titleColor);

    // Title separator hairline
    painter.setPen(QPen(QColor(255, 255, 255, 26), 1));
    painter.drawLine(boxX, boxY + titleH, boxX + boxW, boxY + titleH);

    // Left Collapse Chevron Button
    const int btnSize = 18;
    const int collapseBtnX = boxX + 6;
    const int collapseBtnY = boxY + (titleH - btnSize) / 2;
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 24));
    painter.drawRoundedRect(collapseBtnX, collapseBtnY, btnSize, btnSize, 4, 4);

    painter.setPen(QPen(Qt::white, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    const qreal ccx = collapseBtnX + btnSize / 2.0;
    const qreal ccy = collapseBtnY + btnSize / 2.0;
    QPainterPath chevronPath;
    chevronPath.moveTo(ccx - 3.5, ccy - 1.5);
    chevronPath.lineTo(ccx, ccy + 2.0);
    chevronPath.lineTo(ccx + 3.5, ccy - 1.5);
    painter.drawPath(chevronPath);

    // Right View Switch Button (Fluent 2x2 Grid)
    const int switchBtnX = boxX + boxW - 6 - btnSize;
    const int switchBtnY = collapseBtnY;
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 24));
    painter.drawRoundedRect(switchBtnX, switchBtnY, btnSize, btnSize, 4, 4);

    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    const int gSize = 3;
    const int gGap = 2;
    const int gx = switchBtnX + (btnSize - (gSize * 2 + gGap)) / 2;
    const int gy = switchBtnY + (btnSize - (gSize * 2 + gGap)) / 2;
    painter.drawRect(gx, gy, gSize, gSize);
    painter.drawRect(gx + gSize + gGap, gy, gSize, gSize);
    painter.drawRect(gx, gy + gSize + gGap, gSize, gSize);
    painter.drawRect(gx + gSize + gGap, gy + gSize + gGap, gSize, gSize);

    // Centered Title Text
    QString boxTitle = tr("LinBox");
    if (LinBox* sourceBox = controlSourceBox()) {
        if (!sourceBox->boxTitle().trimmed().isEmpty()) {
            boxTitle = sourceBox->boxTitle().trimmed();
        }
    }
    QFont titleFont(AppFonts::uiFamily(), 9, QFont::Bold);
    painter.setFont(titleFont);
    QFontMetrics titleFm(titleFont);
    const int maxTitleW = boxW - (btnSize + 14) * 2;
    QString elidedTitle = titleFm.elidedText(boxTitle, Qt::ElideRight, maxTitleW);
    const QRect titleRect(boxX + btnSize + 14, boxY, maxTitleW, titleH);

    painter.setPen(QColor(0, 0, 0, 130));
    painter.drawText(titleRect.translated(0, 1), Qt::AlignCenter, elidedTitle);
    painter.setPen(QColor(255, 255, 255, 240));
    painter.drawText(titleRect, Qt::AlignCenter, elidedTitle);

    // 4. Box Body Content (Desktop 1:1 Scale Icons)
    const QVector<MockItemDef> mockItems = {
        { MockItemDef::Doc,     tr("Report.docx") },
        { MockItemDef::Folder,  tr("Project") },
        { MockItemDef::Image,   tr("Photo.png") },
        { MockItemDef::Code,    QStringLiteral("App.cpp") },
        { MockItemDef::Archive, tr("Assets.zip") }
    };

    int itemCount = 3;
    if (boxW >= 430) {
        itemCount = 5;
    } else if (boxW >= 340) {
        itemCount = 4;
    }

    const int contentY = boxY + titleH;
    const int contentH = boxH - titleH;
    const int slotW = 66;
    const int slotH = 68;
    const int totalSlotsW = itemCount * slotW;
    const int slotSpacing = qBound(6, (boxW - totalSlotsW) / (itemCount + 1), 20);
    const int actualGroupW = itemCount * slotW + (itemCount - 1) * slotSpacing;
    const int startSlotX = boxX + (boxW - actualGroupW) / 2;
    const int itemBaseY = contentY + (contentH - slotH) / 2;

    QFont labelFont(AppFonts::uiFamily(), 8);
    labelFont.setPixelSize(10);
    painter.setFont(labelFont);
    QFontMetrics labelFm(labelFont);

    for (int i = 0; i < itemCount; ++i) {
        const MockItemDef& item = mockItems[i];
        const int sx = startSlotX + i * (slotW + slotSpacing);
        const int iconX = sx + (slotW - 32) / 2;
        const int iconY = itemBaseY + 4;
        const int labelY = itemBaseY + 42;

        if (item.type == MockItemDef::Doc) {
            // Word Document
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 30));
            painter.drawRoundedRect(iconX + 6, iconY + 3, 20, 26, 2, 2);

            painter.setBrush(QColor(248, 250, 252));
            painter.drawRoundedRect(iconX + 6, iconY + 2, 20, 26, 2, 2);

            QPainterPath headerPath;
            headerPath.addRoundedRect(QRect(iconX + 6, iconY + 2, 20, 8), 2, 2);
            headerPath.addRect(QRect(iconX + 6, iconY + 6, 20, 4));
            painter.fillPath(headerPath, QColor("#1976D2"));

            QPolygon foldPoly;
            foldPoly << QPoint(iconX + 21, iconY + 2) << QPoint(iconX + 26, iconY + 2) << QPoint(iconX + 26, iconY + 7);
            painter.setBrush(QColor("#90CAF9"));
            painter.drawPolygon(foldPoly);

            painter.setBrush(QColor(176, 190, 204));
            painter.drawRoundedRect(iconX + 9, iconY + 13, 14, 2, 1, 1);
            painter.drawRoundedRect(iconX + 9, iconY + 17, 11, 2, 1, 1);
            painter.drawRoundedRect(iconX + 9, iconY + 21, 13, 2, 1, 1);

            painter.setPen(QPen(QColor(0, 0, 0, 25), 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(iconX + 6, iconY + 2, 20, 26, 2, 2);
        } else if (item.type == MockItemDef::Folder) {
            // Fluent Modern Folder
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor("#D97706"));
            painter.drawRoundedRect(iconX + 3, iconY + 6, 12, 6, 2, 2);

            painter.setBrush(QColor(255, 255, 255, 220));
            painter.drawRoundedRect(iconX + 7, iconY + 7, 18, 6, 1, 1);

            QLinearGradient folderGrad(iconX + 2, iconY + 10, iconX + 2, iconY + 29);
            folderGrad.setColorAt(0.0, QColor("#FBBF24"));
            folderGrad.setColorAt(1.0, QColor("#D97706"));
            painter.setBrush(folderGrad);
            painter.drawRoundedRect(iconX + 2, iconY + 10, 28, 19, 3, 3);

            painter.setPen(QPen(QColor(255, 251, 235, 180), 1));
            painter.drawLine(iconX + 4, iconY + 11, iconX + 28, iconY + 11);
        } else if (item.type == MockItemDef::Image) {
            // Photo / Gallery Image
            painter.setPen(Qt::NoPen);
            QLinearGradient imgGrad(iconX + 4, iconY + 4, iconX + 28, iconY + 28);
            imgGrad.setColorAt(0.0, QColor("#4F46E5"));
            imgGrad.setColorAt(1.0, QColor("#06B6D4"));
            painter.setBrush(imgGrad);
            painter.drawRoundedRect(iconX + 4, iconY + 4, 24, 24, 4, 4);

            painter.setBrush(QColor("#FDE047"));
            painter.drawEllipse(iconX + 8, iconY + 8, 5, 5);

            QPolygon mtnPoly;
            mtnPoly << QPoint(iconX + 4, iconY + 28)
                    << QPoint(iconX + 13, iconY + 17)
                    << QPoint(iconX + 18, iconY + 23)
                    << QPoint(iconX + 23, iconY + 15)
                    << QPoint(iconX + 28, iconY + 28);
            painter.setBrush(QColor(255, 255, 255, 230));
            painter.drawPolygon(mtnPoly);

            painter.setPen(QPen(QColor(255, 255, 255, 60), 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(iconX + 4, iconY + 4, 24, 24, 4, 4);
        } else if (item.type == MockItemDef::Code) {
            // Code / Dev File
            painter.setPen(Qt::NoPen);
            QLinearGradient codeGrad(iconX + 4, iconY + 4, iconX + 28, iconY + 28);
            codeGrad.setColorAt(0.0, QColor("#334155"));
            codeGrad.setColorAt(1.0, QColor("#0F172A"));
            painter.setBrush(codeGrad);
            painter.drawRoundedRect(iconX + 4, iconY + 4, 24, 24, 4, 4);

            painter.setPen(QPen(QColor("#34D399"), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            QPainterPath codePath;
            codePath.moveTo(iconX + 10, iconY + 12); codePath.lineTo(iconX + 8, iconY + 16); codePath.lineTo(iconX + 10, iconY + 20);
            codePath.moveTo(iconX + 14, iconY + 21); codePath.lineTo(iconX + 18, iconY + 11);
            codePath.moveTo(iconX + 22, iconY + 12); codePath.lineTo(iconX + 24, iconY + 16); codePath.lineTo(iconX + 22, iconY + 20);
            painter.drawPath(codePath);

            painter.setPen(QPen(QColor(255, 255, 255, 40), 1));
            painter.drawRoundedRect(iconX + 4, iconY + 4, 24, 24, 4, 4);
        } else {
            // Zip / Archive File
            painter.setPen(Qt::NoPen);
            QLinearGradient zipGrad(iconX + 4, iconY + 4, iconX + 28, iconY + 28);
            zipGrad.setColorAt(0.0, QColor("#818CF8"));
            zipGrad.setColorAt(1.0, QColor("#4F46E5"));
            painter.setBrush(zipGrad);
            painter.drawRoundedRect(iconX + 4, iconY + 4, 24, 24, 4, 4);

            painter.setBrush(QColor("#E2E8F0"));
            painter.drawRect(iconX + 14, iconY + 6, 4, 20);
            painter.setBrush(QColor("#1E293B"));
            for (int z = 0; z < 4; ++z) {
                painter.drawRect(iconX + 14 + (z % 2 == 0 ? 0 : 2), iconY + 8 + z * 3, 2, 2);
            }

            painter.setPen(QPen(QColor(255, 255, 255, 50), 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(iconX + 4, iconY + 4, 24, 24, 4, 4);
        }

        // Item Label with Text Shadow
        QString elidedName = labelFm.elidedText(item.name, Qt::ElideRight, slotW - 4);
        painter.setPen(QColor(0, 0, 0, 180));
        painter.drawText(QRect(sx - 2, labelY + 1, slotW + 4, 16), Qt::AlignCenter, elidedName);
        painter.setPen(QColor(255, 255, 255, 235));
        painter.drawText(QRect(sx - 2, labelY, slotW + 4, 16), Qt::AlignCenter, elidedName);
    }

    // LinBox outer highlight border (with layered Z-Depth sheen & drag pulse)
    if (m_previewDragPulse) {
        QLinearGradient pulseBorder(boxX, boxY, boxX + boxW, boxY + boxH);
        pulseBorder.setColorAt(0.0, QColor(56, 189, 248, 200));
        pulseBorder.setColorAt(0.5, QColor(168, 85, 247, 180));
        pulseBorder.setColorAt(1.0, QColor(56, 189, 248, 200));
        painter.setPen(QPen(QBrush(pulseBorder), 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(boxRect.adjusted(0, 0, -1, -1), boxRadius, boxRadius);
    } else if (m_previewLayeredDepth) {
        QLinearGradient borderSheen(boxX, boxY, boxX + boxW, boxY + boxH);
        borderSheen.setColorAt(0.0, QColor(255, 255, 255, 160));
        borderSheen.setColorAt(0.4, QColor(255, 255, 255, 75));
        borderSheen.setColorAt(1.0, QColor(255, 255, 255, 130));
        painter.setPen(QPen(QBrush(borderSheen), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(boxRect.adjusted(0, 0, -1, -1), boxRadius, boxRadius);
    } else {
        painter.setPen(QPen(QColor(255, 255, 255, m_previewGlass ? 75 : 45), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(boxRect.adjusted(0, 0, -1, -1), boxRadius, boxRadius);
    }

    // Inner card border
    painter.setClipping(false);
    painter.setPen(QPen(QColor(0, 0, 0, 28), 1));
    painter.drawRoundedRect(bounds.adjusted(0, 0, -1, -1), radius, radius);

    painter.end();
    m_previewBackground->setPixmap(canvas);
}
