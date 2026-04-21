#include "desktop/ListViewDebugPanel.h"
#include "app/AppFonts.h"
#include "desktop/LinDesktop.h"
#include "desktop/ListView.h"
#include "ui/QmlTestWindow.h"
#include "ui/SoftDesktopTextPreview.h"
#include "ui/SoftDesktopTextRenderer.h"
#include "ui/Fluent/FluentToggleSwitch.h"
#include "ui/Fluent/FluentCard.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>

ListViewDebugPanel::ListViewDebugPanel(ListView* listView, QWidget* parent)
    : QWidget(parent),
      m_listView(listView)
{
    setWindowTitle("ListView Debug");
    setAttribute(Qt::WA_DeleteOnClose, false);
    setMinimumSize(420, 620);

    // --- Scrollable root ---
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setStyleSheet("QScrollArea { background: transparent; border: none; }");

    auto* scrollContent = new QWidget();
    auto* root = new QVBoxLayout(scrollContent);
    root->setContentsMargins(35, 25, 35, 25);
    root->setSpacing(12);

    // --- Page Title ---
    auto* title = new QLabel("UI Tests & Benchmarks", scrollContent);
    title->setStyleSheet(QStringLiteral(
        "font-family: %1; "
        "font-size: 28px; font-weight: 600; color: #1f2937; margin-bottom: 4px; letter-spacing: -0.01em;"
    ).arg(AppFonts::uiCssFamily()));
    root->addWidget(title);

    // ========== 1. Open Windows Desktop Icon Settings ==========
    QPushButton* deskIconBtn = new QPushButton("Open Settings", scrollContent);
    deskIconBtn->setFixedSize(120, 32);
    deskIconBtn->setCursor(Qt::PointingHandCursor);
    deskIconBtn->setStyleSheet("background: #0078d4; border: none; border-radius: 6px; font-weight: 500; color: #ffffff;");
    connect(deskIconBtn, &QPushButton::clicked, this, []() {
        QProcess::startDetached("rundll32.exe", QStringList() << "shell32.dll,Control_RunDLL" << "desk.cpl,,0");
    });
    root->addWidget(new FluentCard(u8"\uE71D", "Desktop Icon Settings", "Access the native Windows dialog to control which system icons appear on your desktop", deskIconBtn, scrollContent));

    // ========== 2. Open QML Test ==========
    QPushButton* qmlTestBtn = new QPushButton("Open QML Test", scrollContent);
    qmlTestBtn->setFixedSize(140, 32);
    qmlTestBtn->setCursor(Qt::PointingHandCursor);
    qmlTestBtn->setStyleSheet("background: #ffffff; border: 1px solid #e5e7eb; border-radius: 6px; font-weight: 500; color: #0078d4;");
    connect(qmlTestBtn, &QPushButton::clicked, this, []() {
        auto* window = new QmlTestWindow();
        window->show();
        window->raise();
        window->activateWindow();
    });
    root->addWidget(new FluentCard(u8"\uE943", "QML Test Page", "Open a small Qt Quick test window to verify QML is available inside the project", qmlTestBtn, scrollContent));

    // ========== 3. Embedded Desktop Mode ==========
    LinDesktop* desk = m_listView ? qobject_cast<LinDesktop*>(m_listView->parentWidget()) : nullptr;

    m_embedToggle = new FluentToggleSwitch(scrollContent);
    m_embedToggle->setCursor(Qt::PointingHandCursor);
    if (desk) m_embedToggle->setChecked(desk->isEmbedded(), false);

    connect(m_embedToggle, &FluentToggleSwitch::toggled, this, [desk](bool checked){
        if (desk) {
            if (checked) desk->embedToDesktop();
            else desk->unembedFromDesktop();
        }
    });

    root->addWidget(new FluentCard(u8"\uE7F4", "Embedded Desktop Mode", "Attach window to the Windows worker window layer", m_embedToggle, scrollContent));

    // ========== 4. Refresh Desktop ==========
    QPushButton* refreshBtn = new QPushButton("Refresh", scrollContent);
    refreshBtn->setFixedSize(80, 32);
    refreshBtn->setCursor(Qt::PointingHandCursor);
    refreshBtn->setStyleSheet("background: #ffffff; border: 1px solid #e5e7eb; border-radius: 6px; font-weight: 500; color: #0078d4;");
    connect(refreshBtn, &QPushButton::clicked, this, [this](){
        if (m_listView) {
            m_listView->onMenuRefresh();
        }
    });

    root->addWidget(new FluentCard(u8"\uE72C", "Refresh Layer", "Resort icons and re-render the desktop icon layout", refreshBtn, scrollContent));

    // ========== 5. Persistence Toggle ==========
    m_persistToggle = new FluentToggleSwitch(scrollContent);
    m_persistToggle->setCursor(Qt::PointingHandCursor);
    m_persistToggle->setChecked(m_listView ? m_listView->persistenceEnabled() : true, false);

    connect(m_persistToggle, &FluentToggleSwitch::toggled, this, [this](bool checked){
        if (m_listView) m_listView->setPersistenceEnabled(checked);
    });
    root->addWidget(new FluentCard(u8"\uE71B", "Data Persistence", "Automatically load and save box and icon info from storage", m_persistToggle, scrollContent));

    // ========== 6. Row Spacing Slider ==========
    root->addSpacing(6);
    root->addWidget(new QLabel("Row Spacing", scrollContent));
    auto* rowLayout = new QHBoxLayout();
    m_rowSlider = new QSlider(Qt::Horizontal, scrollContent);
    m_rowSlider->setRange(0, 40);
    m_rowValueLabel = new QLabel(scrollContent);
    m_rowValueLabel->setFixedWidth(40);
    rowLayout->addWidget(m_rowSlider, 1);
    rowLayout->addWidget(m_rowValueLabel);
    root->addLayout(rowLayout);

    // ========== 7. Column Spacing Slider ==========
    root->addWidget(new QLabel("Column Spacing", scrollContent));
    auto* colLayout = new QHBoxLayout();
    m_colSlider = new QSlider(Qt::Horizontal, scrollContent);
    m_colSlider->setRange(0, 30);
    m_colValueLabel = new QLabel(scrollContent);
    m_colValueLabel->setFixedWidth(40);
    colLayout->addWidget(m_colSlider, 1);
    colLayout->addWidget(m_colValueLabel);
    root->addLayout(colLayout);

    // ========== 8. Desktop Text Blend Test ==========
    auto* previewTitle = new QLabel("Desktop Text Blend Test", scrollContent);
    previewTitle->setStyleSheet("font-size: 14px; font-weight: 600; color: #111827; margin-top: 6px;");
    root->addWidget(previewTitle);

    auto* previewDesc = new QLabel("Preview a softer Windows-style icon label effect with dark inner and outer blending.", scrollContent);
    previewDesc->setWordWrap(true);
    previewDesc->setStyleSheet("font-size: 12px; color: #6b7280; margin-bottom: 2px;");
    root->addWidget(previewDesc);

    m_textPreview = new SoftDesktopTextPreview(scrollContent);
    QFont previewFont("Segoe UI");
    previewFont.setPixelSize(36);
    m_textPreview->setPreviewFont(previewFont);
    m_textPreview->setText("New Text Document.txt");
    root->addWidget(m_textPreview);

    auto addPreviewSlider = [scrollContent, root](const QString& title,
                                                  int minValue,
                                                  int maxValue,
                                                  QSlider*& sliderOut,
                                                  QLabel*& valueLabelOut) {
        auto* titleLabel = new QLabel(title, scrollContent);
        titleLabel->setStyleSheet("font-size: 12px; color: #111827;");
        root->addWidget(titleLabel);

        auto* layout = new QHBoxLayout();
        sliderOut = new QSlider(Qt::Horizontal, scrollContent);
        sliderOut->setRange(minValue, maxValue);
        valueLabelOut = new QLabel(scrollContent);
        valueLabelOut->setFixedWidth(44);
        valueLabelOut->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        layout->addWidget(sliderOut, 1);
        layout->addWidget(valueLabelOut);
        root->addLayout(layout);
    };

    addPreviewSlider("Large Glow Radius", 0, 16, m_largeGlowSlider, m_largeGlowValueLabel);
    addPreviewSlider("Tight Glow Radius", 0, 12, m_tightGlowSlider, m_tightGlowValueLabel);
    addPreviewSlider("Glow Alpha", 0, 255, m_glowAlphaSlider, m_glowAlphaValueLabel);
    addPreviewSlider("Inner Shadow Alpha", 0, 255, m_innerAlphaSlider, m_innerAlphaValueLabel);
    addPreviewSlider("Glow Offset X", -8, 8, m_glowOffsetXSlider, m_glowOffsetXValueLabel);
    addPreviewSlider("Glow Offset Y", -8, 8, m_glowOffsetYSlider, m_glowOffsetYValueLabel);
    addPreviewSlider("Inner Offset X", -8, 8, m_innerOffsetXSlider, m_innerOffsetXValueLabel);
    addPreviewSlider("Inner Offset Y", -8, 8, m_innerOffsetYSlider, m_innerOffsetYValueLabel);

    root->addStretch();

    // --- Finish scrollable area ---
    scrollArea->setWidget(scrollContent);
    outerLayout->addWidget(scrollArea);

    // --- Initialize slider values ---
    if (m_listView) {
        m_rowSlider->setValue(m_listView->rowSpacing());
        m_colSlider->setValue(m_listView->colSpacing());
    }
    m_largeGlowSlider->setValue(6);
    m_tightGlowSlider->setValue(2);
    m_glowAlphaSlider->setValue(210);
    m_innerAlphaSlider->setValue(155);
    m_glowOffsetXSlider->setValue(0);
    m_glowOffsetYSlider->setValue(1);
    m_innerOffsetXSlider->setValue(0);
    m_innerOffsetYSlider->setValue(1);
    updateLabels();
    applyTextPreviewOptions();

    connect(m_rowSlider, &QSlider::valueChanged, this, [this](int value) {
        updateLabels();
        if (m_listView) {
            m_listView->setDebugGridSpacing(value, m_colSlider->value());
        }
    });

    connect(m_colSlider, &QSlider::valueChanged, this, [this](int value) {
        updateLabels();
        if (m_listView) {
            m_listView->setDebugGridSpacing(m_rowSlider->value(), value);
        }
    });

    const auto bindPreviewSlider = [this](QSlider* slider) {
        connect(slider, &QSlider::valueChanged, this, [this](int) {
            updateLabels();
            applyTextPreviewOptions();
        });
    };

    bindPreviewSlider(m_largeGlowSlider);
    bindPreviewSlider(m_tightGlowSlider);
    bindPreviewSlider(m_glowAlphaSlider);
    bindPreviewSlider(m_innerAlphaSlider);
    bindPreviewSlider(m_glowOffsetXSlider);
    bindPreviewSlider(m_glowOffsetYSlider);
    bindPreviewSlider(m_innerOffsetXSlider);
    bindPreviewSlider(m_innerOffsetYSlider);
}

void ListViewDebugPanel::updateLabels()
{
    if (m_rowValueLabel) {
        m_rowValueLabel->setText(QString::number(m_rowSlider ? m_rowSlider->value() : 0));
    }
    if (m_colValueLabel) {
        m_colValueLabel->setText(QString::number(m_colSlider ? m_colSlider->value() : 0));
    }
    if (m_largeGlowValueLabel) {
        m_largeGlowValueLabel->setText(QString::number(m_largeGlowSlider ? m_largeGlowSlider->value() : 0));
    }
    if (m_tightGlowValueLabel) {
        m_tightGlowValueLabel->setText(QString::number(m_tightGlowSlider ? m_tightGlowSlider->value() : 0));
    }
    if (m_glowAlphaValueLabel) {
        m_glowAlphaValueLabel->setText(QString::number(m_glowAlphaSlider ? m_glowAlphaSlider->value() : 0));
    }
    if (m_innerAlphaValueLabel) {
        m_innerAlphaValueLabel->setText(QString::number(m_innerAlphaSlider ? m_innerAlphaSlider->value() : 0));
    }
    if (m_glowOffsetXValueLabel) {
        m_glowOffsetXValueLabel->setText(QString::number(m_glowOffsetXSlider ? m_glowOffsetXSlider->value() : 0));
    }
    if (m_glowOffsetYValueLabel) {
        m_glowOffsetYValueLabel->setText(QString::number(m_glowOffsetYSlider ? m_glowOffsetYSlider->value() : 0));
    }
    if (m_innerOffsetXValueLabel) {
        m_innerOffsetXValueLabel->setText(QString::number(m_innerOffsetXSlider ? m_innerOffsetXSlider->value() : 0));
    }
    if (m_innerOffsetYValueLabel) {
        m_innerOffsetYValueLabel->setText(QString::number(m_innerOffsetYSlider ? m_innerOffsetYSlider->value() : 0));
    }
}

void ListViewDebugPanel::applyTextPreviewOptions()
{
    if (!m_textPreview) {
        return;
    }

    SoftDesktopTextRenderer::Options options = m_textPreview->rendererOptions();
    options.textColor = QColor(255, 255, 255);
    options.glowColor = QColor(0, 0, 0, m_glowAlphaSlider ? m_glowAlphaSlider->value() : 210);
    options.innerShadowColor = QColor(0, 0, 0, m_innerAlphaSlider ? m_innerAlphaSlider->value() : 155);
    options.largeGlowRadius = m_largeGlowSlider ? m_largeGlowSlider->value() : 6;
    options.tightGlowRadius = m_tightGlowSlider ? m_tightGlowSlider->value() : 2;
    options.glowOffset = QPoint(m_glowOffsetXSlider ? m_glowOffsetXSlider->value() : 0,
                                m_glowOffsetYSlider ? m_glowOffsetYSlider->value() : 1);
    options.innerShadowOffset = QPoint(m_innerOffsetXSlider ? m_innerOffsetXSlider->value() : 0,
                                       m_innerOffsetYSlider ? m_innerOffsetYSlider->value() : 1);
    options.lineSpacingAdjust = -2;
    options.alignment = Qt::AlignHCenter | Qt::AlignVCenter;
    m_textPreview->setRendererOptions(options);
}
