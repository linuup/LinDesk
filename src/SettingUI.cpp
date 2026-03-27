#include "SettingUI.h"
#include "LinBox.h"
#include "ListView.h"
#include <windows.h>
#include <winuser.h>
#include <QScreen>
#include <QApplication>
#include <QPainter>
#include <QFrame>
#include <QCheckBox>
#include "WinBlurHelper.h"
#include "ListView.h"
#include "LinDesktop.h"
#include "Fluent/FluentDialog.h"
#include <QProcess>
#include <QScrollArea>
#include <QDateTime>
#include <windowsx.h>
#include <QStackedWidget>
#include "Plugins/PluginManager.h"
#include <QStyledItemDelegate>
#include <QPainterPath>
#include "Fluent/FluentToggleSwitch.h"
#include "Fluent/FluentButton.h"
#include "Fluent/FluentComboBox.h"
#include "Fluent/FluentCard.h"
#include "Fluent/FluentColorWheel.h"
#include "SysInfoUtil.h" // For ShortcutArrowManager
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>

// Forward declaration for log redirection
static QPointer<QTextEdit> g_globalLogView;
static void globalLogHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
    if (!g_globalLogView) return;
    
    QString color = "#d4d4d4"; // Default Gray
    QString prefix;
    switch (type) {
    case QtDebugMsg:    prefix = "DEBUG"; color = "#729fcf"; break; // Cyanish
    case QtInfoMsg:     prefix = "INFO "; color = "#8ae234"; break; // Green
    case QtWarningMsg:  prefix = "WARN "; color = "#fce94f"; break; // Yellow
    case QtCriticalMsg: prefix = "CRIT "; color = "#ef2929"; break; // Red
    case QtFatalMsg:    prefix = "FATAL"; color = "#cc0000"; break; // Bright Red
    }

    QString time = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    QString logLine = QString("<span style='color:#888;'>[%1]</span> <b style='color:%2;'>%3</b> %4")
                      .arg(time).arg(color).arg(prefix).arg(msg.toHtmlEscaped());
    
    // Safety check for main thread
    QMetaObject::invokeMethod(g_globalLogView.data(), [logLine]() {
        g_globalLogView->append(logLine);
    }, Qt::QueuedConnection);
}
#include <QComboBox>

// Custom delegate to draw the Fluent-style selection indicator
class SidebarDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setRenderHint(QPainter::TextAntialiasing);

        bool isSelected = (opt.state & QStyle::State_Selected);
        bool isHovered = (opt.state & QStyle::State_MouseOver);

        // 1. Hover/Select Pill
        if (isSelected || isHovered) {
            QRect pillRect = opt.rect.adjusted(6, 2, -6, -2);
            painter->setBrush(isSelected ? QColor(0, 0, 0, 10) : QColor(0, 0, 0, 5));
            painter->setPen(Qt::NoPen);
            painter->drawRoundedRect(pillRect, 4, 4);
        }

        // 2. Focused Accent Bar (The hallmark of Fluent 2)
        if (isSelected) {
            painter->setBrush(QColor(0, 95, 184)); // BrandBlue / Accent
            painter->setPen(Qt::NoPen);
            // Height 16px, Width 3px, rounded edges
            QRect barRect(opt.rect.left() + 2, opt.rect.top() + (opt.rect.height() - 16) / 2, 3, 16);
            painter->drawRoundedRect(barRect, 1.5, 1.5);
        }

        // 3. Icon Rendering (Segoe Fluent Icons)
        QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        QRect iconRect(opt.rect.left() + 20, opt.rect.top() + (opt.rect.height() - 18) / 2, 18, 18);
        
        if (isSelected) {
            QPixmap pix = icon.pixmap(QSize(18, 18), QIcon::Normal, QIcon::On);
            // Slightly darken selecting icon for depth
            painter->drawPixmap(iconRect, pix);
        } else {
            painter->setOpacity(isHovered ? 1.0 : 0.7);
            icon.paint(painter, iconRect, Qt::AlignCenter);
            painter->setOpacity(1.0);
        }

        // 4. Typography
        painter->setPen(isSelected ? QColor(0, 0, 0) : QColor(48, 48, 48));
        
        QFont font = painter->font();
        font.setPixelSize(14);
        font.setWeight(isSelected ? QFont::DemiBold : QFont::Medium);
        painter->setFont(font);
        
        QRect textRect = opt.rect.adjusted(52, 0, -10, 0);
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, index.data(Qt::DisplayRole).toString());

        painter->restore();
    }
};

SettingUI::SettingUI(LinBox* targetBox, const QVector<LinBox*>& allBoxes, QWidget* parent)
    : FluentBaseWidget(parent), m_targetBox(targetBox), m_allBoxes(allBoxes)
{
    resize(720, 520);
    setMinimumSize(600, 400);
    setWindowTitle("LinDesk Global Settings");
    setResizable(true);
    setWindowBtnMode(All); // Show Minimize, Maximize, and Close buttons

    initUI();
    setupStyles();

    // 1. Persistence - Restore Window Size only
    QString configPath = QCoreApplication::applicationDirPath() + "/config.ini";
    QSettings settings(configPath, QSettings::IniFormat);
    if (settings.contains("SettingUI/Size")) {
        resize(settings.value("SettingUI/Size").toSize());
    } else {
        resize(720, 520); // Default
    }

    // Always center on screen or near box (don't persist position as requested)
    if (m_targetBox) {
        move(m_targetBox->mapToGlobal(QPoint(m_targetBox->width()/2 - width()/2, m_targetBox->height()/2 - height()/2)));
    }
}

void SettingUI::initUI() {
    QHBoxLayout* mainLayout = new QHBoxLayout();
    centralLayout()->addLayout(mainLayout);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // --- Sidebar (Left) ---
    QWidget* sidebarContainer = new QWidget(container());
    sidebarContainer->setObjectName("sidebarContainer");
    sidebarContainer->setMinimumWidth(200); // Adjusted width
    sidebarContainer->setMaximumWidth(260); // Adjusted width
    QVBoxLayout* sidebarLayout = new QVBoxLayout(sidebarContainer);
    sidebarLayout->setContentsMargins(0, 0, 0, 10);
    sidebarLayout->setSpacing(0);
    sidebarLayout->setAlignment(Qt::AlignTop);

    // Sidebar Branding Header
    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(16, 20, 16, 12);
    headerLayout->setSpacing(12);
    
    QLabel* appLogo = new QLabel(container());
    QPixmap logo(":/Assets/app_icon.png");
    appLogo->setPixmap(logo.scaled(24, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    appLogo->setFixedSize(24, 24);
    appLogo->setStyleSheet("background: transparent;");
    
    QLabel* appTitle = new QLabel("LinDesk", container());
    appTitle->setStyleSheet("font-family: 'Segoe UI Variable Display', 'Segoe UI', sans-serif; font-size: 14px; font-weight: 600; color: #1c1c1c;"); // Adjusted font size and weight
    
    headerLayout->addWidget(appLogo);
    headerLayout->addWidget(appTitle);
    headerLayout->addStretch();
    sidebarLayout->addLayout(headerLayout);

    m_sidebar = new QListWidget(container());
    m_sidebar->setObjectName("sidebar");
    m_sidebar->setFrameShape(QFrame::NoFrame);
    m_sidebar->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    struct TabInfo { char16_t icon; QString text; };
    QVector<TabInfo> tabs = {
        {0xE80F, "General"},
        {0xE7F4, "Desktop"},
        {0xEA37, "Box"},
        {0xEBE8, "Tests"},
        {0xE74C, "Plugins"},
        {0xE756, "Console"},
        {0xE946, "About"}
    };

    for (const auto& tab : tabs) {
        QListWidgetItem* item = new QListWidgetItem(m_sidebar);
        item->setText(tab.text);
        item->setSizeHint(QSize(200, 36)); 

        // Create a high-quality Fluent icon
        QPixmap pix(48, 48);
        pix.setDevicePixelRatio(2.0);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.setRenderHint(QPainter::Antialiasing);
        
        // Use Segoe Fluent Icons with proper fallback
        QFont f("Segoe Fluent Icons");
        if (!QFontInfo(f).exactMatch()) f.setFamily("Segoe MDL2 Assets");
        f.setPixelSize(18); 
        p.setFont(f);
        p.setPen(QColor(28, 28, 28));
        p.drawText(QRect(0,0,24,24), Qt::AlignCenter, QString(tab.icon));
        item->setIcon(QIcon(pix));
    }

    m_sidebar->setItemDelegate(new SidebarDelegate(m_sidebar));
    m_sidebar->setIconSize(QSize(24, 24));
    m_sidebar->setSpacing(4);
    sidebarLayout->addWidget(m_sidebar);

    // --- Content Area (Right) ---
    QWidget* rightArea = new QWidget(container());
    rightArea->setObjectName("rightArea");
    QVBoxLayout* rightLayout = new QVBoxLayout(rightArea);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    m_contentStack = new QStackedWidget(container());
    m_contentStack->addWidget(createGeneralPage());
    m_contentStack->addWidget(createDesktopPage());
    m_contentStack->addWidget(createGridPage());
    m_contentStack->addWidget(createComponentsPage());
    m_contentStack->addWidget(createPluginsPage());
    m_contentStack->addWidget(createConsolePage());
    m_contentStack->addWidget(createAboutPage());

    rightLayout->addWidget(m_contentStack);

    // Apply proportional stretch (2:7) for modern Fluent feel
    mainLayout->addWidget(sidebarContainer, 2);
    mainLayout->addWidget(rightArea, 7);

    connect(m_sidebar, &QListWidget::currentRowChanged, m_contentStack, &QStackedWidget::setCurrentIndex);
    m_sidebar->setCurrentRow(0); // Default to General as requested
}

QWidget* SettingUI::createGeneralPage() {
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(35, 25, 35, 25);
    layout->setSpacing(12);

    QString configPath = QCoreApplication::applicationDirPath() + "/config.ini";
    QSettings settings(configPath, QSettings::IniFormat);

    QLabel* title = new QLabel("General Settings", page);
    title->setObjectName("pageTitle");
    layout->addWidget(title);

    // 1. Auto Start
    FluentToggleSwitch* autoStart = new FluentToggleSwitch(page);
    autoStart->setChecked(settings.value("General/AutoStart", true).toBool());
    connect(autoStart, &FluentToggleSwitch::toggled, this, [configPath](bool checked){
        QSettings s(configPath, QSettings::IniFormat);
        s.setValue("General/AutoStart", checked);
    });
    layout->addWidget(new FluentCard(u8"\uE7E6", "Launch at Login", "Automatically start LinDesk when you sign in to Windows", autoStart, page));

    // 2. Tray Only
    FluentToggleSwitch* trayOnly = new FluentToggleSwitch(page);
    trayOnly->setChecked(settings.value("General/MinimizeToTray", true).toBool());
    connect(trayOnly, &FluentToggleSwitch::toggled, this, [configPath](bool checked){
        QSettings s(configPath, QSettings::IniFormat);
        s.setValue("General/MinimizeToTray", checked);
    });
    layout->addWidget(new FluentCard(u8"\uE921", "Minimize to Tray", "Keep LinDesk running in the system tray when window is closed", trayOnly, page));

    // 3. Language
    FluentComboBox* langBox = new FluentComboBox(page);
    langBox->addItem("English (United States)");
    langBox->addItem("Chinese (Simplified)");
    langBox->setCurrentIndex(settings.value("General/Language", 0).toInt());
    langBox->setMinimumWidth(180);
    connect(langBox, QOverload<int>::of(&FluentComboBox::currentIndexChanged), this, [configPath](int index){
        QSettings s(configPath, QSettings::IniFormat);
        s.setValue("General/Language", index);
    });
    layout->addWidget(new FluentCard(u8"\uE774", "Display Language", "Choose which language is used in the interface", langBox, page));

    layout->addStretch();
    return page;
}

QWidget* SettingUI::createDesktopPage() {
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(35, 25, 35, 25);
    layout->setSpacing(12);

    QLabel* title = new QLabel("Desktop Customization", page);
    title->setObjectName("pageTitle");
    layout->addWidget(title);

    // Shortcut Arrow Toggle
    FluentToggleSwitch* arrowToggle = new FluentToggleSwitch(page);
    
    // Simplification: Always use the instance state as the source of truth
    ListView* lv = ListView::instance();
    bool showArrow = lv ? lv->shortcutArrowVisible() : true;
    
    arrowToggle->setChecked(showArrow);
    
    connect(arrowToggle, &FluentToggleSwitch::toggled, this, [](bool checked){
        ListView* lv = ListView::instance();
        if (lv) lv->setShortcutArrowVisible(checked);
    });

    layout->addWidget(new FluentCard(u8"\uE724", "Shortcut Arrows", "Toggle visibility of the small arrow on desktop shortcut icons", arrowToggle, page));

    // Double Click to Hide Icons
    FluentToggleSwitch* hideToggle = new FluentToggleSwitch(page);
    bool doubleClickHide = lv ? lv->isDoubleClickHideIconsEnabled() : true;
    hideToggle->setChecked(doubleClickHide);
    connect(hideToggle, &FluentToggleSwitch::toggled, this, [](bool checked){
        ListView* lv = ListView::instance();
        if (lv) lv->setDoubleClickHideIconsEnabled(checked);
    });
    layout->addWidget(new FluentCard(u8"\uE949", "Double Click to Hide", "Quickly show or hide all desktop icons by double-clicking on empty spaces", hideToggle, page));

    layout->addStretch();
    return page;
}

QWidget* SettingUI::createGridPage() {
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(35, 25, 35, 25);
    layout->setSpacing(8);

    QLabel* title = new QLabel("Grid Appearance", page);
    title->setObjectName("pageTitle");
    layout->addWidget(title);
    layout->addSpacing(10);

    // 1. Color Selection
    QWidget* colorActionSide = new QWidget(page);
    QHBoxLayout* colorActionLayout = new QHBoxLayout(colorActionSide);
    colorActionLayout->setContentsMargins(0, 0, 0, 0);
    colorActionLayout->setSpacing(15);

    QLabel* hexLabel = new QLabel("#0078D4", colorActionSide);
    hexLabel->setStyleSheet("color: rgba(0,0,0,0.45); font-family: 'Segoe UI Mono'; font-size: 13px;");
    
    FluentColorWheel* colorWheel = new FluentColorWheel(colorActionSide);
    if (m_targetBox) {
        colorWheel->setColor(m_targetBox->backgroundColor());
        hexLabel->setText(m_targetBox->backgroundColor().name().toUpper());
    }

    connect(colorWheel, &FluentColorWheel::colorChanged, this, [hexLabel, this](const QColor& c){
        for(auto b : m_allBoxes) b->setBackgroundColor(c); 
        hexLabel->setText(c.name().toUpper());
        ListView* lv = ListView::instance();
        if (lv) lv->requestSaveBoxes();
    });

    colorActionLayout->addWidget(hexLabel);
    colorActionLayout->addWidget(colorWheel);
    
    FluentCard* colorCard = new FluentCard(u8"\uE790", "Accent Color", "Define the primary theme color for your desktop boxes", colorActionSide, page);
    colorCard->setFixedHeight(120); // Give extra room for the wheel
    layout->addWidget(colorCard);

    // 2. Transparency Slider
    m_opacitySlider = new QSlider(Qt::Horizontal, page);
    m_opacitySlider->setRange(5, 100);
    m_opacitySlider->setFixedWidth(150);
    if (m_targetBox) m_opacitySlider->setValue(m_targetBox->backgroundOpacity() * 100);
    
    m_opacityLabelValue = new QLabel("50%", page);
    m_opacityLabelValue->setFixedWidth(35);
    m_opacityLabelValue->setStyleSheet("color: #616161; font-weight: 500;");

    QWidget* opacActionSide = new QWidget(page);
    QHBoxLayout* opacActionLayout = new QHBoxLayout(opacActionSide);
    opacActionLayout->addWidget(m_opacitySlider);
    opacActionLayout->addWidget(m_opacityLabelValue);

    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int v){
        for(auto b : m_allBoxes) b->setBackgroundOpacity(v/100.0f);
        m_opacityLabelValue->setText(QString("%1%").arg(v));
        ListView* lv = ListView::instance();
        if (lv) lv->requestSaveBoxes();
    });

    layout->addWidget(new FluentCard(u8"\uE7E6", "Background Opacity", "Adjust the semi-transparency level of the grid surface", opacActionSide, page));

    // 3. Acrylic Toggle
    m_glassToggle = new FluentToggleSwitch(page);
    if (m_targetBox) m_glassToggle->setChecked(m_targetBox->glassEffect(), false);
    
    connect(m_glassToggle, &FluentToggleSwitch::toggled, this, [this](bool v){ 
        for(auto b : m_allBoxes) b->setGlassEffect(v); 
        ListView* lv = ListView::instance();
        if (lv) lv->requestSaveBoxes();
    });

    layout->addWidget(new FluentCard(u8"\uE91B", "Acrylic Glass Effect", "Enable real-time DWM background blur for a translucent look", m_glassToggle, page));

    layout->addStretch();
    return page;
}

QWidget* SettingUI::createComponentsPage() {
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(35, 25, 35, 25);
    layout->setSpacing(15);

    QLabel* title = new QLabel("UI Tests & Benchmarks", page);
    title->setObjectName("pageTitle");
    layout->addWidget(title);

    // 1. Open Windows Desktop Icon Settings
    QPushButton* deskIconBtn = new QPushButton("Open Settings", page);
    deskIconBtn->setFixedSize(120, 32);
    deskIconBtn->setCursor(Qt::PointingHandCursor);
    deskIconBtn->setStyleSheet("background: #0078d4; border: none; border-radius: 4px; font-weight: 600; color: #ffffff;");
    connect(deskIconBtn, &QPushButton::clicked, this, []() {
        QProcess::startDetached("rundll32.exe", QStringList() << "shell32.dll,Control_RunDLL" << "desk.cpl,,0");
    });
    layout->addWidget(new FluentCard(u8"\uE71D", "Desktop Icon Settings", "Access the native Windows dialog to control which system icons appear on your desktop", deskIconBtn, page));

    // 2. Embedded Desktop Mode
    ListView* lv = ListView::instance();
    LinDesktop* desk = lv ? qobject_cast<LinDesktop*>(lv->parentWidget()) : nullptr;
    
    FluentToggleSwitch* embedToggle = new FluentToggleSwitch(page);
    embedToggle->setObjectName("glassToggle");
    embedToggle->setCursor(Qt::PointingHandCursor);
    if (desk) embedToggle->setChecked(desk->isEmbedded(), false);
    
    connect(embedToggle, &FluentToggleSwitch::toggled, this, [desk](bool checked){
        if (desk) {
            if (checked) desk->embedToDesktop();
            else desk->unembedFromDesktop();
        }
    });

    layout->addWidget(new FluentCard(u8"\uE7F4", "Embedded Desktop Mode", "Attach window to the Windows worker window layer", embedToggle, page));

    // 3. Refresh Desktop
    QPushButton* refreshBtn = new QPushButton("Refresh", page);
    refreshBtn->setFixedSize(80, 32);
    refreshBtn->setCursor(Qt::PointingHandCursor);
    refreshBtn->setStyleSheet("background: #ffffff; border: 1px solid #d0d0d0; border-radius: 4px; font-weight: 600; color: #0078d4;");
    connect(refreshBtn, &QPushButton::clicked, this, [](){
        ListView* lv = ListView::instance();
        if (lv) {
            lv->onMenuRefresh();
        }
    });

    layout->addWidget(new FluentCard(u8"\uE72C", "Refresh Layer", "Resort icons and re-render the desktop icon layout", refreshBtn, page));

    // 4. Persistence Toggle
    FluentToggleSwitch* persistToggle = new FluentToggleSwitch(page);
    persistToggle->setObjectName("glassToggle"); // Switch style
    persistToggle->setCursor(Qt::PointingHandCursor);
    persistToggle->setChecked(lv ? lv->persistenceEnabled() : true, false);
    
    connect(persistToggle, &FluentToggleSwitch::toggled, this, [](bool checked){
        ListView* lv = ListView::instance();
        if (lv) lv->setPersistenceEnabled(checked);
    });
    layout->addWidget(new FluentCard(u8"\uE71B", "Data Persistence", "Automatically load and save box and icon info from storage", persistToggle, page));

    layout->addStretch();
    return page;
}



QWidget* SettingUI::createAboutPage() {
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(30, 20, 30, 20);
    layout->setAlignment(Qt::AlignCenter);

    QLabel* icon = new QLabel(u8"\uE713", page);
    icon->setStyleSheet("font-family: 'Segoe MDL2 Assets'; font-size: 64px; color: #0078d4;");
    layout->addWidget(icon, 0, Qt::AlignCenter);

    QLabel* name = new QLabel("LinDesk", page);
    name->setStyleSheet("font-size: 24px; font-weight: bold; color: #000000;");
    layout->addWidget(name, 0, Qt::AlignCenter);

    layout->addWidget(new QLabel("Version 1.0.0 Stable", page), 0, Qt::AlignCenter);
    layout->addSpacing(20);
    layout->addWidget(new QLabel("Powered by Antigravity Design System", page), 0, Qt::AlignCenter);
    
    layout->addStretch();
    return page;
}

QWidget* SettingUI::createPluginsPage() {
    QWidget* page = new QWidget();
    QVBoxLayout* mainLayout = new QVBoxLayout(page);
    mainLayout->setContentsMargins(40, 40, 40, 40);
    mainLayout->setSpacing(24);

    QLabel* title = new QLabel("Extending Your Universe", page);
    title->setObjectName("pageTitle");
    mainLayout->addWidget(title);

    QScrollArea* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("background: transparent;");
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // Remove unwanted horizontal scroll

    QWidget* listContainer = new QWidget();
    QVBoxLayout* listLayout = new QVBoxLayout(listContainer);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(12);

    auto allPlugins = PluginManager::instance()->allPlugins();
    
    if (allPlugins.isEmpty()) {
        QLabel* emptyMsg = new QLabel("No plugins found in /plugins directory.", page);
        emptyMsg->setStyleSheet("color: #888; font-style: italic;");
        listLayout->addWidget(emptyMsg);
    } else {
        for (auto* plugin : allPlugins) {
            if (!plugin) continue;

            FluentToggleSwitch* toggle = new FluentToggleSwitch(page);
            toggle->setChecked(PluginManager::instance()->isPluginEnabled(plugin->pluginId()), false);
            QString pId = plugin->pluginId();
            connect(toggle, &FluentToggleSwitch::toggled, this, [pId](bool checked) {
                PluginManager::instance()->setPluginEnabled(pId, checked);
            });

            FluentButton* settingsBtn = new FluentButton("设置", page);
            settingsBtn->setFixedSize(60, 28);
            settingsBtn->setAccent(false); // Use neutral style
            connect(settingsBtn, &QPushButton::clicked, this, [plugin]() {
                plugin->onSettingsRequested();
                PluginManager::instance()->registerPluginHotkey(plugin);
            });

            FluentCard* pCard = new FluentCard(u8"\uE74C", plugin->pluginName(), plugin->pluginDescription(), nullptr, listContainer);
            pCard->addActionWidget(toggle);
            pCard->addActionWidget(settingsBtn);
            
            listLayout->addWidget(pCard);
        }
    }

    listLayout->addStretch();
    scroll->setWidget(listContainer);
    mainLayout->addWidget(scroll);

    return page;
}

QWidget* SettingUI::createConsolePage() {
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(35, 25, 35, 25);
    layout->setSpacing(15);

    QLabel* title = new QLabel("Developer System Console", page);
    title->setObjectName("pageTitle");
    layout->addWidget(title);

    m_logView = new QTextEdit(page);
    m_logView->setObjectName("consoleView");
    m_logView->setReadOnly(true);
    m_logView->setStyleSheet(
        "background-color: #1e1e1e; " // Dark coding background
        "color: #d4d4d4; "           // VS Code-style color
        "font-family: 'Consolas', 'Courier New', monospace; "
        "font-size: 12px; "
        "border-radius: 8px; "
        "padding: 10px; "
    );
    
    // Set global pointer for redirection
    g_globalLogView = m_logView;
    qInstallMessageHandler(globalLogHandler);

    layout->addWidget(m_logView);

    QHBoxLayout* controls = new QHBoxLayout();
    QPushButton* clearBtn = new QPushButton("Clear Logs", page);
    clearBtn->setStyleSheet("background: #333; color: white; padding: 5px 15px; border-radius: 4px;");
    connect(clearBtn, &QPushButton::clicked, m_logView, &QTextEdit::clear);
    
    controls->addStretch();
    controls->addWidget(clearBtn);
    layout->addLayout(controls);

    return page;
}

void SettingUI::setupStyles() {
    setStyleSheet(
        "QWidget { "
        "   background-color: transparent; "
        "   border: none; "
        "   color: #1a1a1a; "
        "   font-family: 'Segoe UI Variable Text', 'Segoe UI', sans-serif; "
        "} "
        "QWidget#fluentBaseContainer { "
        "   background-color: transparent; "
        "   border: none; "
        "} "
        "SettingUI { "
        "   background-color: #f3f3f3; " 
        "   border: 1px solid rgba(0, 0, 0, 0.1); "
        "   border-radius: 12px; "
        "} "
        "QWidget#sidebarContainer { "
        "   background-color: #f9f9f9; "
        "   border-right: 1px solid rgba(0, 0, 0, 0.05); "
        "   border-bottom-left-radius: 11px; "
        "} "
        "QWidget#rightArea { "
        "   background-color: #ffffff; " 
        "   border-bottom-right-radius: 11px; "
        "} "
        "QListWidget#sidebar { "
        "   background: transparent; "
        "   outline: none; "
        "   margin: 0; "
        "   margin-top: 5px; "
        "   background: transparent; "
        "   padding-left: 20px; "  // Adjusted for icon/text balance
        "} "
        "QListWidget#sidebar::item { "
        "   height: 36px; "
        "   border: none; "
        "   background: transparent; "
        "} "
        "QListWidget#sidebar::item:hover { background: transparent; } "
        "QListWidget#sidebar::item:selected { background: transparent; } "
        "QScrollBar:vertical { "
        "   border: none; background: transparent; width: 4px; margin: 0; "
        "} "
        "QScrollBar::handle:vertical { "
        "   background: rgba(0, 0, 0, 0.12); border-radius: 2px; min-height: 20px; "
        "} "
        "QScrollBar::handle:vertical:hover { background: rgba(0, 0, 0, 0.2); } "
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; } "
        "QLabel#pageTitle { font-family: 'Segoe UI Variable Display', 'Segoe UI', sans-serif; font-size: 28px; font-weight: 600; color: #1a1a1a; margin-bottom: 2px; } "
        "QLabel#cardLabel { font-size: 13px; font-weight: 500; color: #1a1a1a; } "
        "QWidget#settingCard { "
        "   background: #ffffff; "
        "   border: 1px solid rgba(0, 0, 0, 0.06); "
        "   border-bottom: 1px solid rgba(0, 0, 0, 0.12); " // Subtle elevation shadow
        "   border-radius: 8px; "
        "} "
        "QPushButton#closeBtn { background: transparent; border: none; font-family: 'Segoe MDL2 Assets'; font-size: 10px; border-radius: 0 11px 0 0; } "
        "QPushButton#closeBtn:hover { background: #c42b1c; color: white; } "
        "QSlider::groove:horizontal { height: 4px; background: rgba(0, 0, 0, 0.1); border-radius: 2px; } "
        "QSlider::handle:horizontal { background: #005fb8; border: 3px solid #ffffff; width: 14px; height: 14px; margin: -8px 0; border-radius: 10px; } "
        "QCheckBox::indicator { "
        "   width: 40px; "      // Matches Fluent Switch size better
        "   height: 20px; "
        "   background: #666666; "
        "   border: 1px solid #666666; "
        "   border-radius: 10px; "
        "} "
        "QCheckBox::indicator:checked { "
        "   background-color: #005fb8; "
        "   border-color: #005fb8; "
        "} "
    );
}

// Legacy mouse events moved to base class logic

// Legacy mouse events removed - handled by FluentBaseWidget

void SettingUI::setTargetBox(LinBox* target) {
    m_targetBox = target;
    if (m_targetBox) {
        updateControlsFromTarget();
    }
}

void SettingUI::updateControlsFromTarget() {
    if (!m_targetBox) return;
    if (m_opacitySlider) {
        m_opacitySlider->setValue((int)(m_targetBox->backgroundOpacity() * 100));
        m_opacityLabelValue->setText(QString("%1%").arg(m_opacitySlider->value()));
    }
    if (m_glassToggle) {
        m_glassToggle->setChecked(m_targetBox->glassEffect());
    }
}

void SettingUI::closeEvent(QCloseEvent* event) {
    // 2. Persistence - Save Window Size only
    QString configPath = QCoreApplication::applicationDirPath() + "/config.ini";
    QSettings settings(configPath, QSettings::IniFormat);
    settings.setValue("SettingUI/Size", size());
    settings.sync();

    this->hide();
    event->ignore(); // Prevent destruction
}

// Legacy paint event removed - handled by FluentBaseWidget

// NativeEvent has been gracefully moved to FluentBaseWidget
