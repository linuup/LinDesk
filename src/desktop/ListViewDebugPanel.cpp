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

namespace {
QString debugPanelStyleSheet()
{
    return QStringLiteral(
        "QWidget { background: #f5f7fb; }"
        "QScrollArea { background: transparent; border: none; }"
        "QScrollBar:vertical { background: transparent; width: 10px; margin: 4px; }"
        "QScrollBar::handle:vertical { background: rgba(31, 41, 55, 0.18); border-radius: 5px; min-height: 36px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; height: 0px; }"
        "QTabBar::tab {"
        "   height: 40px; min-width: 128px;"
        "   padding: 0 18px; margin-right: 10px;"
        "   background: transparent; border: none;"
        "   font-family: 'Microsoft YaHei UI'; font-size: 14px; font-weight: 500; color: #6b7280;"
        "}"
        "QTabBar::tab:hover { color: #111827; }"
        "QTabBar::tab:selected { color: #0f6cbd; border-bottom: 3px solid #0f6cbd; font-weight: 600; }");
}
}

ListViewDebugPanel::ListViewDebugPanel(ListView* listView, QWidget* parent)
    : QWidget(parent), m_listView(listView)
{
    initUI();
}

void ListViewDebugPanel::initUI() {
    setWindowTitle("ListView Debug Console");
    setAttribute(Qt::WA_DeleteOnClose, false);
    setMinimumSize(860, 720);
    resize(960, 800);
    setStyleSheet(debugPanelStyleSheet());

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto* headerContainer = new QWidget(this);
    headerContainer->setFixedHeight(136);
    headerContainer->setStyleSheet("background-color: #ffffff; border-bottom: 1px solid #e5e7eb;");
    auto* headerLayout = new QVBoxLayout(headerContainer);
    headerLayout->setContentsMargins(32, 22, 32, 12);
    headerLayout->setSpacing(8);

    auto* titleLabel = new QLabel("Debug Console", headerContainer);
    titleLabel->setStyleSheet(QStringLiteral(
        "font-family: %1; font-size: 28px; font-weight: 600; color: #111827;")
        .arg(AppFonts::uiCssFamily()));
    headerLayout->addWidget(titleLabel);

    auto* subtitleLabel = new QLabel("Inspect desktop behavior, tune layout values, and jump to app settings.", headerContainer);
    subtitleLabel->setStyleSheet(QStringLiteral(
        "font-family: %1; font-size: 13px; color: #6b7280;")
        .arg(AppFonts::uiCssFamily()));
    headerLayout->addWidget(subtitleLabel);

    m_tabBar = new QTabBar(headerContainer);
    m_tabBar->addTab("General Tools");
    m_tabBar->addTab("Layout Settings");
    m_tabBar->addTab("Text Diagnostics");
    m_tabBar->setDrawBase(false);
    headerLayout->addWidget(m_tabBar);
    mainLayout->addWidget(headerContainer);

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

QPushButton* ListViewDebugPanel::createActionButton(const QString& text, bool accent, bool destructive) const
{
    auto* button = new QPushButton(text);
    button->setFixedHeight(34);
    button->setMinimumWidth(88);

    QString background = QStringLiteral("#ffffff");
    QString foreground = QStringLiteral("#1f2937");
    QString border = QStringLiteral("rgba(15, 23, 42, 0.14)");
    QString hover = QStringLiteral("#f3f4f6");
    if (accent) {
        background = QStringLiteral("#0f6cbd");
        foreground = QStringLiteral("#ffffff");
        border = QStringLiteral("#0f6cbd");
        hover = QStringLiteral("#115ea3");
    } else if (destructive) {
        background = QStringLiteral("#d92d20");
        foreground = QStringLiteral("#ffffff");
        border = QStringLiteral("#d92d20");
        hover = QStringLiteral("#b42318");
    }

    button->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  min-width: 88px; padding: 0 14px; border-radius: 8px;"
        "  border: 1px solid %1; background: %2; color: %3;"
        "  font-family: %4; font-size: 13px; font-weight: 600;"
        "}"
        "QPushButton:hover { background: %5; }"
        "QPushButton:pressed { padding-top: 1px; }")
        .arg(border, background, foreground, AppFonts::uiCssFamily(), hover));
    return button;
}

QWidget* ListViewDebugPanel::createSliderSettingRow(const QString& labelText,
                                                    int minValue,
                                                    int maxValue,
                                                    QSlider*& slider,
                                                    QLabel*& valueLabel)
{
    auto* container = new QWidget();
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* label = new QLabel(labelText, container);
    label->setStyleSheet(QStringLiteral(
        "font-family: %1; font-size: 13px; font-weight: 600; color: #111827;")
        .arg(AppFonts::uiCssFamily()));
    layout->addWidget(label);

    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(12);

    slider = new QSlider(Qt::Horizontal, container);
    slider->setRange(minValue, maxValue);
    row->addWidget(slider, 1);

    valueLabel = new QLabel("0", container);
    valueLabel->setFixedWidth(36);
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    valueLabel->setStyleSheet(QStringLiteral(
        "font-family: %1; font-size: 12px; color: #6b7280;")
        .arg(AppFonts::uiCssFamily()));
    row->addWidget(valueLabel);

    layout->addLayout(row);
    return container;
}

QLabel* ListViewDebugPanel::createPageTitleLabel(const QString& text) const
{
    auto* label = new QLabel(text);
    label->setStyleSheet(QStringLiteral(
        "font-family: %1; font-size: 24px; font-weight: 600; color: #111827;")
        .arg(AppFonts::uiCssFamily()));
    return label;
}

QLabel* ListViewDebugPanel::createPageSubtitleLabel(const QString& text) const
{
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    label->setStyleSheet(QStringLiteral(
        "font-family: %1; font-size: 13px; color: #6b7280;")
        .arg(AppFonts::uiCssFamily()));
    return label;
}

QWidget* ListViewDebugPanel::wrapScrollablePage(QWidget* content) {
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("background: transparent;");
    scroll->setWidget(content);
    return scroll;
}

QWidget* ListViewDebugPanel::createGeneralPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(20);

    layout->addWidget(createPageTitleLabel("General Tools"));
    layout->addWidget(createPageSubtitleLabel("Common debug actions and quick entry points for desktop diagnostics."));

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(14);

    QPushButton* restartBtn = createActionButton("Restart", false, true);
    connect(restartBtn, &QPushButton::clicked, this, []() {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), QCoreApplication::arguments(), QCoreApplication::applicationDirPath());
        QCoreApplication::quit();
    });
    grid->addWidget(new FluentCard(u8"\uE777", "Restart", "Relaunch the application", restartBtn), 0, 0);

    QPushButton* settingsBtn = createActionButton("Open", true, false);
    connect(settingsBtn, &QPushButton::clicked, this, [this]() {
        if (m_listView) {
            m_listView->showSettings(nullptr);
        }
    });
    grid->addWidget(new FluentCard(u8"\uE713", "Settings", "Open the main settings window", settingsBtn), 0, 1);

    QPushButton* deskIconBtn = createActionButton("Open", false, false);
    connect(deskIconBtn, &QPushButton::clicked, this, []() {
        QProcess::startDetached("rundll32.exe", QStringList() << "shell32.dll,Control_RunDLL" << "desk.cpl,,0");
    });
    grid->addWidget(new FluentCard(u8"\uE71D", "Desktop Icons", "Native OS icon settings", deskIconBtn), 1, 0);

    QPushButton* qmlTestBtn = createActionButton("Verify", false, false);
    connect(qmlTestBtn, &QPushButton::clicked, this, []() {
        auto* w = new QmlTestWindow();
        w->show(); w->raise(); w->activateWindow();
    });
    grid->addWidget(new FluentCard(u8"\uE943", "QML Test", "Verify Qt Quick availability", qmlTestBtn), 1, 1);

    QPushButton* refreshBtn = createActionButton("Refresh", false, false);
    connect(refreshBtn, &QPushButton::clicked, this, [this](){
        if (m_listView) m_listView->onMenuRefresh();
    });
    grid->addWidget(new FluentCard(u8"\uE72C", "Refresh Layer", "Resort icons and re-render", refreshBtn), 2, 0);

    m_persistToggle = new FluentToggleSwitch();
    m_persistToggle->setChecked(m_listView ? m_listView->persistenceEnabled() : true, false);
    connect(m_persistToggle, &FluentToggleSwitch::toggled, this, [this](bool checked){
        if (m_listView) m_listView->setPersistenceEnabled(checked);
    });
    grid->addWidget(new FluentCard(u8"\uE71B", "Persistence", "Save box and icon data", m_persistToggle), 2, 1);

    QPushButton* exportBtn = createActionButton("Export", false, false);
    connect(exportBtn, &QPushButton::clicked, this, [this]() {
        QString path;
        if (DebugInfoExporter::exportToApplicationDir(m_listView, &path))
            QMessageBox::information(this, "Export", "Exported to " + path);
    });
    grid->addWidget(new FluentCard(u8"\uE8AB", "Export Debug", "Dump state to debug.json", exportBtn), 3, 0, 1, 2);

    layout->addLayout(grid);
    layout->addStretch();
    return page;
}

QWidget* ListViewDebugPanel::createLayoutPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(24);

    layout->addWidget(createPageTitleLabel("Layout & Behavior"));
    layout->addWidget(createPageSubtitleLabel("Adjust desktop spacing and desktop embedding behavior for visual debugging."));

    LinDesktop* desk = m_listView ? qobject_cast<LinDesktop*>(m_listView->parentWidget()) : nullptr;
    m_embedToggle = new FluentToggleSwitch();
    if (desk) m_embedToggle->setChecked(desk->isEmbedded(), false);
    connect(m_embedToggle, &FluentToggleSwitch::toggled, this, [desk](bool checked){
        if (desk) { if (checked) desk->embedToDesktop(); else desk->unembedFromDesktop(); }
    });
    layout->addWidget(new FluentCard(u8"\uE7F4", "Embedded Desktop Mode", "Attach window to WorkerW layer", m_embedToggle));

    layout->addWidget(createSliderSettingRow("Row Spacing", 0, 40, m_rowSlider, m_rowValueLabel));

    connect(m_rowSlider, &QSlider::valueChanged, this, [this](int v) {
        updateLabels();
        if (m_listView) m_listView->setDebugGridSpacing(v, m_colSlider->value());
    });

    layout->addWidget(createSliderSettingRow("Column Spacing", 0, 30, m_colSlider, m_colValueLabel));

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

    layout->addWidget(createPageTitleLabel("Text Rendering Diagnostics"));
    layout->addWidget(createPageSubtitleLabel("Preview the desktop text renderer and tune glow and shadow parameters."));

    m_textPreview = new SoftDesktopTextPreview();
    QFont previewFont("Segoe UI");
    previewFont.setPixelSize(36);
    m_textPreview->setPreviewFont(previewFont);
    m_textPreview->setText("New Text Document.txt");
    m_textPreview->setMinimumHeight(100);
    layout->addWidget(m_textPreview);

    auto addSlider = [&](const QString& label, int min, int max, QSlider*& slider, QLabel*& val) {
        layout->addWidget(createSliderSettingRow(label, min, max, slider, val));
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
