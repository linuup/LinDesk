#include "ListViewDebugPanel.h"
#include "LinDesktop.h"
#include "ListView.h"
#include "SoftDesktopTextPreview.h"
#include "Fluent/FluentToggleSwitch.h"
#include "Fluent/FluentCard.h"

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
    title->setStyleSheet(
        "font-family: 'Segoe UI Variable Display', 'Segoe UI Variable Text', 'Segoe UI', sans-serif; "
        "font-size: 28px; font-weight: 600; color: #1f2937; margin-bottom: 4px; letter-spacing: -0.01em;"
    );
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

    // ========== 2. Embedded Desktop Mode ==========
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

    // ========== 3. Refresh Desktop ==========
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

    // ========== 4. Persistence Toggle ==========
    m_persistToggle = new FluentToggleSwitch(scrollContent);
    m_persistToggle->setCursor(Qt::PointingHandCursor);
    m_persistToggle->setChecked(m_listView ? m_listView->persistenceEnabled() : true, false);

    connect(m_persistToggle, &FluentToggleSwitch::toggled, this, [this](bool checked){
        if (m_listView) m_listView->setPersistenceEnabled(checked);
    });
    root->addWidget(new FluentCard(u8"\uE71B", "Data Persistence", "Automatically load and save box and icon info from storage", m_persistToggle, scrollContent));

    // ========== 5. Row Spacing Slider ==========
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

    // ========== 6. Column Spacing Slider ==========
    root->addWidget(new QLabel("Column Spacing", scrollContent));
    auto* colLayout = new QHBoxLayout();
    m_colSlider = new QSlider(Qt::Horizontal, scrollContent);
    m_colSlider->setRange(0, 30);
    m_colValueLabel = new QLabel(scrollContent);
    m_colValueLabel->setFixedWidth(40);
    colLayout->addWidget(m_colSlider, 1);
    colLayout->addWidget(m_colValueLabel);
    root->addLayout(colLayout);

    // ========== 7. Desktop Text Blend Test ==========
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

    root->addStretch();

    // --- Finish scrollable area ---
    scrollArea->setWidget(scrollContent);
    outerLayout->addWidget(scrollArea);

    // --- Initialize slider values ---
    if (m_listView) {
        m_rowSlider->setValue(m_listView->rowSpacing());
        m_colSlider->setValue(m_listView->colSpacing());
    }
    updateLabels();

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
}

void ListViewDebugPanel::updateLabels()
{
    if (m_rowValueLabel) {
        m_rowValueLabel->setText(QString::number(m_rowSlider ? m_rowSlider->value() : 0));
    }
    if (m_colValueLabel) {
        m_colValueLabel->setText(QString::number(m_colSlider ? m_colSlider->value() : 0));
    }
}
