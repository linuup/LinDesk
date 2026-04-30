#include "desktop/ListViewDebugPanel.h"
#include "app/AppFonts.h"
#include "desktop/LinDesktop.h"
#include "desktop/ListView.h"
#include "ui/QmlTestWindow.h"
#include "ui/SoftDesktopTextPreview.h"
#include "ui/SoftDesktopTextRenderer.h"
#include "ui/Fluent/FluentToggleSwitch.h"
#include "ui/Fluent/FluentCard.h"
#include "services/DebugInfoExporter.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>

#include <QListWidget>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QPainterPath>

// Custom delegate to draw the Fluent Design 2 style sidebar
#include <QTabBar>
#include <QStackedWidget>
#include <QPainterPath>

ListViewDebugPanel::ListViewDebugPanel(ListView* listView, QWidget* parent)
    : QWidget(parent), m_listView(listView)
{
    initUI();
}

void ListViewDebugPanel::initUI() {
    setWindowTitle("ListView Debug Console");
    setAttribute(Qt::WA_DeleteOnClose, false);
    setMinimumSize(820, 720);
    resize(920, 800);
    setStyleSheet("background-color: #ffffff;");

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // --- Header & Navigation ---
    auto* headerContainer = new QWidget(this);
    headerContainer->setFixedHeight(120);
    headerContainer->setStyleSheet("background-color: #f8f9fa; border-bottom: 1px solid #e5e7eb;");
    auto* headerLayout = new QVBoxLayout(headerContainer);
    headerLayout->setContentsMargins(32, 24, 32, 0);
    headerLayout->setSpacing(12);

    auto* titleLabel = new QLabel("Debug Console", headerContainer);
    titleLabel->setStyleSheet("font-size: 28px; font-weight: 600; color: #111827;");
    headerLayout->addWidget(titleLabel);

    m_tabBar = new QTabBar(headerContainer);
    m_tabBar->addTab("General Tools");
    m_tabBar->addTab("Layout Settings");
    m_tabBar->addTab("Text Diagnostics");
    m_tabBar->setDrawBase(false);
    m_tabBar->setStyleSheet(
        "QTabBar::tab { "
        "   height: 40px; min-width: 120px; "
        "   padding: 0 20px; margin-right: 12px; "
        "   background: transparent; border: none; "
        "   font-family: 'Microsoft YaHei UI'; font-size: 14px; font-weight: 500; color: #6b7280; "
        "} "
        "QTabBar::tab:hover { color: #111827; } "
        "QTabBar::tab:selected { "
        "   color: #0078d4; border-bottom: 3px solid #0078d4; "
        "   font-weight: 600; "
        "} "
    );
    headerLayout->addWidget(m_tabBar);
    mainLayout->addWidget(headerContainer);

    // --- Content Area ---
    m_contentStack = new QStackedWidget(this);
    m_contentStack->addWidget(wrapScrollablePage(createGeneralPage()));
    m_contentStack->addWidget(wrapScrollablePage(createLayoutPage()));
    m_contentStack->addWidget(wrapScrollablePage(createTextDiagnosticsPage()));
    mainLayout->addWidget(m_contentStack);

    connect(m_tabBar, &QTabBar::currentChanged, m_contentStack, &QStackedWidget::setCurrentIndex);
    m_tabBar->setCurrentIndex(0);

    // Initialize values
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
}

QWidget* ListViewDebugPanel::wrapScrollablePage(QWidget* content) {
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("background: #ffffff;");
    scroll->setWidget(content);
    return scroll;
}

QWidget* ListViewDebugPanel::createGeneralPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(20);

    auto* title = new QLabel("General Tools");
    title->setStyleSheet("font-size: 24px; font-weight: 600; color: #111827;");
    layout->addWidget(title);

    auto* grid = new QGridLayout();
    grid->setSpacing(12);

    // Restart
    QPushButton* restartBtn = new QPushButton("Restart App");
    restartBtn->setFixedSize(100, 32);
    restartBtn->setStyleSheet("background: #dc2626; color: white; border-radius: 6px; font-weight: 600;");
    connect(restartBtn, &QPushButton::clicked, this, []() {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), QCoreApplication::arguments(), QCoreApplication::applicationDirPath());
        QCoreApplication::quit();
    });
    grid->addWidget(new FluentCard(u8"\uE777", "Restart", "Relaunch the application", restartBtn), 0, 0);

    // Windows Icons
    QPushButton* deskIconBtn = new QPushButton("Open");
    deskIconBtn->setFixedSize(80, 32);
    connect(deskIconBtn, &QPushButton::clicked, this, []() {
        QProcess::startDetached("rundll32.exe", QStringList() << "shell32.dll,Control_RunDLL" << "desk.cpl,,0");
    });
    grid->addWidget(new FluentCard(u8"\uE71D", "Desktop Icons", "Native OS icon settings", deskIconBtn), 0, 1);

    // QML Test
    QPushButton* qmlTestBtn = new QPushButton("Verify");
    qmlTestBtn->setFixedSize(80, 32);
    connect(qmlTestBtn, &QPushButton::clicked, this, []() {
        auto* w = new QmlTestWindow();
        w->show(); w->raise(); w->activateWindow();
    });
    grid->addWidget(new FluentCard(u8"\uE943", "QML Test", "Verify Qt Quick availability", qmlTestBtn), 1, 0);

    // Refresh
    QPushButton* refreshBtn = new QPushButton("Refresh");
    refreshBtn->setFixedSize(80, 32);
    connect(refreshBtn, &QPushButton::clicked, this, [this](){
        if (m_listView) m_listView->onMenuRefresh();
    });
    grid->addWidget(new FluentCard(u8"\uE72C", "Refresh Layer", "Resort icons and re-render", refreshBtn), 1, 1);

    // Persistence
    m_persistToggle = new FluentToggleSwitch();
    m_persistToggle->setChecked(m_listView ? m_listView->persistenceEnabled() : true, false);
    connect(m_persistToggle, &FluentToggleSwitch::toggled, this, [this](bool checked){
        if (m_listView) m_listView->setPersistenceEnabled(checked);
    });
    grid->addWidget(new FluentCard(u8"\uE71B", "Persistence", "Save box and icon data", m_persistToggle), 2, 0);

    // Export
    QPushButton* exportBtn = new QPushButton("Export");
    exportBtn->setFixedSize(80, 32);
    connect(exportBtn, &QPushButton::clicked, this, [this]() {
        QString path;
        if (DebugInfoExporter::exportToApplicationDir(m_listView, &path))
            QMessageBox::information(this, "Export", "Exported to " + path);
    });
    grid->addWidget(new FluentCard(u8"\uE8AB", "Export Debug", "Dump state to debug.json", exportBtn), 2, 1);

    layout->addLayout(grid);
    layout->addStretch();
    return page;
}

QWidget* ListViewDebugPanel::createLayoutPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(24);

    auto* title = new QLabel("Layout & Behavior");
    title->setStyleSheet("font-size: 24px; font-weight: 600; color: #111827;");
    layout->addWidget(title);

    // Embedded Mode
    LinDesktop* desk = m_listView ? qobject_cast<LinDesktop*>(m_listView->parentWidget()) : nullptr;
    m_embedToggle = new FluentToggleSwitch();
    if (desk) m_embedToggle->setChecked(desk->isEmbedded(), false);
    connect(m_embedToggle, &FluentToggleSwitch::toggled, this, [desk](bool checked){
        if (desk) { if (checked) desk->embedToDesktop(); else desk->unembedFromDesktop(); }
    });
    layout->addWidget(new FluentCard(u8"\uE7F4", "Embedded Desktop Mode", "Attach window to WorkerW layer", m_embedToggle));

    // Row Spacing
    auto* rowBox = new QWidget();
    auto* rowL = new QVBoxLayout(rowBox);
    rowL->setContentsMargins(0,0,0,0);
    rowL->addWidget(new QLabel("Row Spacing"));
    auto* rowSL = new QHBoxLayout();
    m_rowSlider = new QSlider(Qt::Horizontal);
    m_rowSlider->setRange(0, 40);
    m_rowValueLabel = new QLabel("0");
    rowSL->addWidget(m_rowSlider, 1);
    rowSL->addWidget(m_rowValueLabel);
    rowL->addLayout(rowSL);
    layout->addWidget(rowBox);

    connect(m_rowSlider, &QSlider::valueChanged, this, [this](int v) {
        updateLabels();
        if (m_listView) m_listView->setDebugGridSpacing(v, m_colSlider->value());
    });

    // Col Spacing
    auto* colBox = new QWidget();
    auto* colL = new QVBoxLayout(colBox);
    colL->setContentsMargins(0,0,0,0);
    colL->addWidget(new QLabel("Column Spacing"));
    auto* colSL = new QHBoxLayout();
    m_colSlider = new QSlider(Qt::Horizontal);
    m_colSlider->setRange(0, 30);
    m_colValueLabel = new QLabel("0");
    colSL->addWidget(m_colSlider, 1);
    colSL->addWidget(m_colValueLabel);
    colL->addLayout(colSL);
    layout->addWidget(colBox);

    connect(m_colSlider, &QSlider::valueChanged, this, [this](int v) {
        updateLabels();
        if (m_listView) m_listView->setDebugGridSpacing(m_rowSlider->value(), v);
    });

    layout->addStretch();
    return page;
}

QWidget* ListViewDebugPanel::createTextDiagnosticsPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(16);

    auto* title = new QLabel("Text Rendering Diagnostics");
    title->setStyleSheet("font-size: 24px; font-weight: 600; color: #111827;");
    layout->addWidget(title);

    m_textPreview = new SoftDesktopTextPreview();
    QFont previewFont("Segoe UI");
    previewFont.setPixelSize(36);
    m_textPreview->setPreviewFont(previewFont);
    m_textPreview->setText("New Text Document.txt");
    m_textPreview->setMinimumHeight(100);
    layout->addWidget(m_textPreview);

    auto addSlider = [&](const QString& label, int min, int max, QSlider*& slider, QLabel*& val) {
        layout->addWidget(new QLabel(label));
        auto* hl = new QHBoxLayout();
        slider = new QSlider(Qt::Horizontal);
        slider->setRange(min, max);
        val = new QLabel("0");
        val->setFixedWidth(30);
        hl->addWidget(slider, 1);
        hl->addWidget(val);
        layout->addLayout(hl);

        connect(slider, &QSlider::valueChanged, this, [this](int){ 
            updateLabels(); 
            applyTextPreviewOptions(); 
        });
    };

    addSlider("Large Glow Radius", 0, 16, m_largeGlowSlider, m_largeGlowValueLabel);
    addSlider("Tight Glow Radius", 0, 12, m_tightGlowSlider, m_tightGlowValueLabel);
    addSlider("Glow Alpha", 0, 255, m_glowAlphaSlider, m_glowAlphaValueLabel);
    addSlider("Inner Shadow Alpha", 0, 255, m_innerAlphaSlider, m_innerAlphaValueLabel);
    addSlider("Glow Offset X", -8, 8, m_glowOffsetXSlider, m_glowOffsetXValueLabel);
    addSlider("Glow Offset Y", -8, 8, m_glowOffsetYSlider, m_glowOffsetYValueLabel);
    addSlider("Inner Offset X", -8, 8, m_innerOffsetXSlider, m_innerOffsetXValueLabel);
    addSlider("Inner Offset Y", -8, 8, m_innerOffsetYSlider, m_innerOffsetYValueLabel);

    layout->addStretch();
    return page;
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
