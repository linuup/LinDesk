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
#include <QComboBox>

// Custom delegate to draw the Fluent Design 2 style sidebar
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

        // Fluent Design 2 Colors
        const QColor accentColor(0, 120, 212);  // Fluent Blue #0078d4
        const QColor selectedBg(243, 245, 249);
        const QColor hoverBg(246, 248, 250);
        const QColor textPrimary(31, 41, 55);
        const QColor textSecondary(75, 85, 99);
        const QColor iconSelected(0, 120, 212);
        const QColor iconNormal(75, 85, 99);
        const QColor iconHover(55, 65, 81);

        const int itemHeight = opt.rect.height();
        const int iconSize = 20;
        const int leftMargin = 10;
        const int iconLeftPadding = 22;
        const int textLeftPadding = iconLeftPadding + iconSize + 14;

        // Draw a soft rounded highlight similar to the mockup.
        if (isSelected || isHovered) {
            QRect bgRect = opt.rect.adjusted(leftMargin, 4, -leftMargin, -4);
            QPainterPath path;
            path.addRoundedRect(bgRect, 8, 8);
            
            if (isSelected) {
                painter->fillPath(path, selectedBg);
            } else if (isHovered) {
                painter->fillPath(path, hoverBg);
            }
        }

        // Accent rail for the active item.
        if (isSelected) {
            const int barWidth = 3;
            const int barHeight = itemHeight - 14;
            QRect barRect(opt.rect.left() + leftMargin + 1,
                         opt.rect.top() + (itemHeight - barHeight) / 2, 
                         barWidth, barHeight);
            painter->setBrush(accentColor);
            painter->setPen(Qt::NoPen);
            painter->drawRoundedRect(barRect, 1.5, 1.5);
        }

        // Draw the icon glyph directly to keep it sharp on every DPI scale.
        const QString iconGlyph = index.data(Qt::UserRole).toString();
        QRect iconRect(opt.rect.left() + iconLeftPadding, 
                       opt.rect.top() + (itemHeight - iconSize) / 2, 
                       iconSize, iconSize);

        if (!iconGlyph.isEmpty()) {
            QFont iconFont("Segoe Fluent Icons");
            if (!QFontInfo(iconFont).exactMatch()) {
                iconFont.setFamily("Segoe MDL2 Assets");
            }
            iconFont.setPixelSize(iconSize);
            iconFont.setWeight(QFont::DemiBold);
            iconFont.setBold(true);
            painter->setFont(iconFont);
            painter->setPen(isSelected ? iconSelected : (isHovered ? iconHover : iconNormal));
            painter->drawText(iconRect, Qt::AlignCenter, iconGlyph);
        }

        QString text = index.data(Qt::DisplayRole).toString();
        painter->setPen(isSelected ? accentColor : (isHovered ? textPrimary : textSecondary));
        
        QFont font = painter->font();
        font.setFamily("Segoe UI Variable Text");
        font.setPixelSize(14);
        font.setWeight(isSelected ? QFont::Weight::Medium : QFont::Weight::Normal);
        painter->setFont(font);
        
        QRect textRect(opt.rect.left() + textLeftPadding, opt.rect.top(), 
                       opt.rect.width() - textLeftPadding - 16, itemHeight);
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, text);

        painter->restore();
    }
};

SettingUI::SettingUI(LinBox* targetBox, const QVector<LinBox*>& allBoxes, QWidget* parent)
    : FluentBaseWidget(parent), m_targetBox(targetBox), m_allBoxes(allBoxes)
{
    resize(720, 520);
    setMinimumSize(520, 340);
    setWindowTitle("LinDesk");
    setResizable(true);
    setWindowBtnMode(All); // Show Minimize, Maximize, and Close buttons

    // Add title bar text (left side of title bar) - Fluent Design Title
    QLabel* titleBarTitle = new QLabel("LinDesk", this);
    titleBarTitle->setStyleSheet(
        "font-family: 'Segoe UI Variable Display', 'Segoe UI Variable Text', 'Segoe UI', sans-serif; "
        "font-size: 12px; font-weight: 600; letter-spacing: 0.02em; color: #1f2937; "
        "margin-left: 8px; margin-right: 20px;"
    );
    addTitleBarWidget(titleBarTitle);

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
    sidebarContainer->setMinimumWidth(220);
    sidebarContainer->setMaximumWidth(280);
    QVBoxLayout* sidebarLayout = new QVBoxLayout(sidebarContainer);
    sidebarLayout->setContentsMargins(16, 14, 16, 18);
    sidebarLayout->setSpacing(0);
    sidebarLayout->setAlignment(Qt::AlignTop);

    // Sidebar Branding Header (minimal spacer to align with title bar)
    QWidget* headerSpacer = new QWidget(sidebarContainer);
    headerSpacer->setFixedHeight(18);
    sidebarLayout->addWidget(headerSpacer);

    m_sidebar = new QListWidget(sidebarContainer);
    m_sidebar->setObjectName("sidebar");
    m_sidebar->setFrameShape(QFrame::NoFrame);
    m_sidebar->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    struct TabInfo { char16_t icon; QString text; };
    QVector<TabInfo> tabs = {
        {0xE80F, "General"},
        {0xE7F4, "Desktop"},
        {0xEA37, "Box"},
        {0xE74C, "Plugins"},
        {0xE946, "About"}
    };

    for (const auto& tab : tabs) {
        QListWidgetItem* item = new QListWidgetItem(m_sidebar);
        item->setText(tab.text);
        item->setSizeHint(QSize(208, 44));
        item->setData(Qt::UserRole, QString(tab.icon));
    }

    m_sidebar->setItemDelegate(new SidebarDelegate(m_sidebar));
    m_sidebar->setSpacing(6);
    sidebarLayout->addWidget(m_sidebar);

    // --- Content Area (Right) ---
    QWidget* rightArea = new QWidget(container());
    rightArea->setObjectName("rightArea");
    QVBoxLayout* rightLayout = new QVBoxLayout(rightArea);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    m_contentStack = new QStackedWidget(container());
    m_contentStack->addWidget(wrapScrollablePage(createGeneralPage()));
    m_contentStack->addWidget(wrapScrollablePage(createDesktopPage()));
    m_contentStack->addWidget(wrapScrollablePage(createGridPage()));
    m_contentStack->addWidget(createPluginsPage());
    m_contentStack->addWidget(wrapScrollablePage(createAboutPage()));

    rightLayout->addWidget(m_contentStack);

    // Apply proportional stretch (2:7) for modern Fluent feel
    mainLayout->addWidget(sidebarContainer, 2);
    mainLayout->addWidget(rightArea, 7);

    connect(m_sidebar, &QListWidget::currentRowChanged, m_contentStack, &QStackedWidget::setCurrentIndex);
    m_sidebar->setCurrentRow(0); // Default to General as requested
}

QWidget* SettingUI::wrapScrollablePage(QWidget* content) {
    if (!content) {
        return new QWidget(container());
    }

    QScrollArea* scroll = new QScrollArea(container());
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setStyleSheet("QScrollArea { background: transparent; border: none; }");

    content->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    scroll->setWidget(content);
    return scroll;
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

    QLabel* title = new QLabel("Box UI", page);
    title->setObjectName("pageTitle");
    layout->addWidget(title);
    layout->addSpacing(10);

    // Preview Box - New design with BoxPreview.png background and box overlay
    QWidget* previewContainer = new QWidget(page);
    previewContainer->setFixedHeight(200);
    previewContainer->setStyleSheet("background: #f8f9fa; border-radius: 8px;");
    
    QVBoxLayout* previewLayout = new QVBoxLayout(previewContainer);
    previewLayout->setContentsMargins(16, 12, 16, 12);
    previewLayout->setSpacing(8);
    
    QLabel* previewLabel = new QLabel("Preview", previewContainer);
    previewLabel->setStyleSheet("font-size: 12px; font-weight: 600; color: #6b7280;");
    previewLayout->addWidget(previewLabel);
    
    // Center container for the preview
    QWidget* previewCenter = new QWidget(previewContainer);
    previewCenter->setFixedHeight(145);
    QHBoxLayout* previewCenterLayout = new QHBoxLayout(previewCenter);
    previewCenterLayout->setContentsMargins(0, 0, 0, 0);
    previewCenterLayout->setAlignment(Qt::AlignCenter);
    
    // Custom preview widget that draws BoxPreview.png and box overlay
    m_previewBackground = new QLabel(previewCenter);
    m_previewBackground->setFixedSize(350, 140);
    m_previewBackground->setScaledContents(false);
    m_previewBackground->setStyleSheet("border-radius: 12px;");
    
    // Load and set the BoxPreview.png using Qt resource system
    QPixmap bgPixmap(":/Assets/BoxPreview.png");
    if (!bgPixmap.isNull()) {
        m_previewBackground->setPixmap(bgPixmap.scaled(350, 140, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    }
    m_previewBackground->setAlignment(Qt::AlignCenter);
    
    previewCenterLayout->addWidget(m_previewBackground);
    previewLayout->addWidget(previewCenter);
    
    // Add preview to layout at TOP (not inside a card)
    layout->addWidget(previewContainer);

    // 1. Color Selection
    QWidget* colorActionSide = new QWidget(page);
    QHBoxLayout* colorActionLayout = new QHBoxLayout(colorActionSide);
    colorActionLayout->setContentsMargins(0, 0, 0, 0);
    colorActionLayout->setSpacing(16);

    FluentColorWheel* colorWheel = new FluentColorWheel(colorActionSide);

    QLineEdit* colorValueEdit = new QLineEdit(colorActionSide);
    colorValueEdit->setReadOnly(true);
    colorValueEdit->setAlignment(Qt::AlignCenter);
    colorValueEdit->setFixedWidth(112);
    colorValueEdit->setFixedHeight(34);
    colorValueEdit->setStyleSheet(
        "QLineEdit {"
        " background: #ffffff;"
        " border: 1px solid rgba(0, 0, 0, 0.14);"
        " border-radius: 8px;"
        " color: #374151;"
        " font-family: 'Segoe UI Mono', 'Consolas';"
        " font-size: 13px;"
        " padding: 0 10px;"
        "}"
    );

    QVBoxLayout* colorValueLayout = new QVBoxLayout();
    colorValueLayout->setContentsMargins(0, 8, 0, 0);
    colorValueLayout->setSpacing(8);
    colorValueLayout->addWidget(colorValueEdit, 0, Qt::AlignTop);
    colorValueLayout->addStretch();

    if (m_targetBox) {
        colorWheel->setColor(m_targetBox->backgroundColor());
        colorValueEdit->setText(m_targetBox->backgroundColor().name().toUpper());
    } else {
        colorValueEdit->setText(colorWheel->color().name().toUpper());
    }

    connect(colorWheel, &FluentColorWheel::colorChanged, this, [colorValueEdit, this](const QColor& c){
        for(auto b : m_allBoxes) b->setBackgroundColor(c); 
        colorValueEdit->setText(c.name().toUpper());
        m_previewColor = c;  // Sync preview color
        updatePreviewStyle();
        ListView* lv = ListView::instance();
        if (lv) lv->requestSaveBoxes();
    });

    colorActionLayout->addWidget(colorWheel);
    colorActionLayout->addLayout(colorValueLayout);
    
    FluentCard* colorCard = new FluentCard(u8"\uE790", "Accent Color", "Define the primary theme color for your desktop boxes", colorActionSide, page);
    colorCard->setFixedHeight(220);
    layout->addWidget(colorCard);

    // 2. Transparency Slider (create first for preview connections)
    m_opacitySlider = new QSlider(Qt::Horizontal, page);
    m_opacitySlider->setRange(5, 100);
    m_opacitySlider->setFixedWidth(150);
    if (m_targetBox) m_opacitySlider->setValue(m_targetBox->backgroundOpacity() * 100);
    
    m_opacityLabelValue = new QLabel("50%", page);
    m_opacityLabelValue->setFixedWidth(35);
    m_opacityLabelValue->setStyleSheet("color: #6b7280; font-weight: 500;");

    QWidget* opacActionSide = new QWidget(page);
    QHBoxLayout* opacActionLayout = new QHBoxLayout(opacActionSide);
    opacActionLayout->addWidget(m_opacitySlider);
    opacActionLayout->addWidget(m_opacityLabelValue);

    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int v){
        for(auto b : m_allBoxes) b->setBackgroundOpacity(v/100.0f);
        m_opacityLabelValue->setText(QString("%1%").arg(v));
        m_previewOpacity = v / 100.0f;  // Sync preview opacity
        updatePreviewStyle();
        ListView* lv = ListView::instance();
        if (lv) lv->requestSaveBoxes();
    });

    layout->addWidget(new FluentCard(u8"\uE7E6", "Background Opacity", "Adjust the semi-transparency level of the grid surface", opacActionSide, page));

    // 3. Acrylic Toggle
    m_glassToggle = new FluentToggleSwitch(page);
    if (m_targetBox) m_glassToggle->setChecked(m_targetBox->glassEffect(), false);
    
    connect(m_glassToggle, &FluentToggleSwitch::toggled, this, [this](bool v){ 
        for(auto b : m_allBoxes) b->setGlassEffect(v); 
        m_previewGlass = v;  // Sync preview glass
        updatePreviewStyle();
        ListView* lv = ListView::instance();
        if (lv) lv->requestSaveBoxes();
    });

    layout->addWidget(new FluentCard(u8"\uE91B", "Acrylic Glass Effect", "Enable real-time DWM background blur for a translucent look", m_glassToggle, page));

    // Initial preview style sync
    if (m_targetBox) {
        m_previewColor = m_targetBox->backgroundColor();
        m_previewOpacity = m_targetBox->backgroundOpacity();
        m_previewGlass = m_targetBox->glassEffect();
    } else {
        m_previewColor = QColor(40, 40, 40);
    }
    updatePreviewStyle();

    layout->addStretch();
    return page;
}




QWidget* SettingUI::createAboutPage() {
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(30, 20, 30, 20);
    layout->setAlignment(Qt::AlignCenter);

    QLabel* icon = new QLabel(u8"\uE713", page);
    icon->setStyleSheet(
        "font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets'; "
        "font-size: 72px; "
        "color: #0078d4; "
        "background: transparent;"
    );
    layout->addWidget(icon, 0, Qt::AlignCenter);

    QLabel* name = new QLabel("LinDesk", page);
    name->setStyleSheet(
        "font-family: 'Segoe UI Variable Display', 'Segoe UI', sans-serif; "
        "font-size: 28px; "
        "font-weight: 600; "
        "color: #1f2937; "
        "letter-spacing: -0.01em;"
    );
    layout->addWidget(name, 0, Qt::AlignCenter);

    QLabel* version = new QLabel("Version 1.0.0 Stable", page);
    version->setStyleSheet(
        "font-family: 'Segoe UI Variable Text', 'Segoe UI', sans-serif; "
        "font-size: 13px; "
        "color: #6b7280;"
    );
    layout->addWidget(version, 0, Qt::AlignCenter);
    
    layout->addSpacing(20);
    
    QLabel* tagline = new QLabel("Powered by Antigravity Design System", page);
    tagline->setStyleSheet(
        "font-family: 'Segoe UI Variable Text', 'Segoe UI', sans-serif; "
        "font-size: 12px; "
        "color: #9ca3af;"
    );
    layout->addWidget(tagline, 0, Qt::AlignCenter);
    
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
        emptyMsg->setStyleSheet("color: #9ca3af; font-style: italic;");
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

void SettingUI::setupStyles() {
    setStyleSheet(
        "QWidget { "
        "   background-color: transparent; "
        "   border: none; "
        "   color: #1f2937; "
        "   font-family: 'Segoe UI Variable Text', 'Segoe UI', sans-serif; "
        "} "
        "QWidget#fluentBaseContainer { "
        "   background-color: transparent; "
        "   border: none; "
        "} "
        "SettingUI { "
        "   background-color: #f3f4f6; " 
        "   border: 1px solid rgba(0, 0, 0, 0.08); "
        "   border-radius: 12px; "
        "} "
        "QWidget#sidebarContainer { "
        "   background-color: #f8f9fa; "
        "   border-bottom-left-radius: 11px; "
        "} "
        "QWidget#rightArea { "
        "   background-color: #ffffff; " 
        "   border-bottom-right-radius: 11px; "
        "} "
        "QListWidget#sidebar { "
        "   background: #f9fafb; "
        "   outline: none; "
        "   border: 1px solid rgba(0, 0, 0, 0.08); "
        "   border-radius: 22px; "
        "   padding: 14px 0; "
        "} "
        "QListWidget#sidebar::item { "
        "   height: 44px; "
        "   border: none; "
        "   background: transparent; "
        "   padding: 0; "
        "   margin: 2px 10px; "
        "} "
        "QListWidget#sidebar::item:hover { background: transparent; } "
        "QListWidget#sidebar::item:selected { background: transparent; } "
        "QListWidget#sidebar::item:focus { background: transparent; outline: none; } "
        "QScrollBar:vertical { "
        "   border: none; background: transparent; width: 4px; margin: 12px 4px 12px 0; "
        "} "
        "QScrollBar::handle:vertical { "
        "   background: rgba(0, 0, 0, 0.12); border-radius: 2px; min-height: 20px; "
        "} "
        "QScrollBar::handle:vertical:hover { background: rgba(0, 0, 0, 0.2); } "
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; } "
        "QLabel#pageTitle { font-family: 'Segoe UI Variable Display', 'Segoe UI Variable Text', 'Segoe UI', sans-serif; font-size: 28px; font-weight: 600; color: #1f2937; margin-bottom: 4px; letter-spacing: -0.01em; } "
        "QLabel#cardLabel { font-size: 13px; font-weight: 500; color: #1f2937; } "
        "QWidget#settingCard { "
        "   background: #ffffff; "
        "   border: 1px solid rgba(0, 0, 0, 0.06); "
        "   border-bottom: 1px solid rgba(0, 0, 0, 0.12); " // Subtle elevation shadow
        "   border-radius: 8px; "
        "} "
        "QPushButton#closeBtn { background: transparent; border: none; font-family: 'Segoe MDL2 Assets'; font-size: 10px; } "
        "QPushButton#closeBtn:hover { background: #c42b1c; color: white; } "
        "QSlider::groove:horizontal { height: 4px; background: rgba(0, 0, 0, 0.1); border-radius: 2px; } "
        "QSlider::handle:horizontal { background: #0078d4; border: 3px solid #ffffff; width: 14px; height: 14px; margin: -8px 0; border-radius: 10px; } "
        "QCheckBox::indicator { "
        "   width: 40px; "      // Matches Fluent Switch size better
        "   height: 20px; "
        "   background: #6b7280; "
        "   border: 1px solid #6b7280; "
        "   border-radius: 10px; "
        "} "
        "QCheckBox::indicator:checked { "
        "   background-color: #0078d4; "
        "   border-color: #0078d4; "
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
    // Sync preview state from target box
    m_previewColor = m_targetBox->backgroundColor();
    m_previewOpacity = m_targetBox->backgroundOpacity();
    m_previewGlass = m_targetBox->glassEffect();
    updatePreviewStyle();
}

void SettingUI::updatePreviewStyle() {
    if (!m_previewBackground) return;
    
    int r = m_previewColor.red();
    int g = m_previewColor.green();
    int b = m_previewColor.blue();
    int a = m_previewGlass ? 1 : (int)(m_previewOpacity * 255);
    
    if (a < 5) a = 20;
    
    // Create a new pixmap with BoxPreview.png as background and draw box on top
    QPixmap canvas(280, 130);
    canvas.fill(Qt::transparent);
    
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    
    // Draw background image using Qt resource system
    QPixmap bgPixmap(":/Assets/BoxPreview.png");
    if (!bgPixmap.isNull()) {
        painter.drawPixmap(0, 0, bgPixmap.scaled(280, 130, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    } else {
        // Fallback: draw gradient background
        QLinearGradient grad(0, 0, 280, 130);
        grad.setColorAt(0, QColor(45, 55, 72));
        grad.setColorAt(1, QColor(30, 35, 45));
        painter.fillRect(canvas.rect(), grad);
    }
    
    // Calculate box dimensions and position
    const int boxW = 200;
    const int boxH = 120;
    const int boxX = (280 - boxW) / 2;
    const int boxY = (130 - boxH) / 2;
    const int titleBarH = 28;
    const int borderWidth = 4;
    
    QRect boxRect(boxX, boxY, boxW, boxH);
    
    if (m_previewGlass) {
        // Glass effect - semi-transparent dark overlay with blur hint
        QColor glassColor(0, 0, 0, 180);
        
        // Draw title bar (solid dark)
        painter.fillRect(boxRect.adjusted(0, 0, 0, -boxH + titleBarH), glassColor);
        
        // Draw content area (semi-transparent)
        QColor contentColor(0, 0, 0, 120);
        painter.fillRect(boxRect.adjusted(0, titleBarH, 0, 0), contentColor);
        
        // Draw border
        painter.setPen(QPen(QColor(255, 255, 255, 80), 1));
        painter.drawRect(boxRect.adjusted(0, 0, -1, -1));
    } else {
        // Solid color with opacity
        QColor bgColor(r, g, b, a);
        
        // Draw title bar
        QPainterPath titlePath;
        titlePath.addRoundedRect(boxX, boxY, boxW, titleBarH, 4, 4);
        // Cover bottom corners
        titlePath.addRect(boxX, boxY + titleBarH - 4, boxW, 4);
        painter.fillPath(titlePath, bgColor);
        
        // Draw content area with rounded bottom corners
        QPainterPath contentPath;
        contentPath.addRoundedRect(boxX, boxY + titleBarH - 4, boxW, boxH - titleBarH + 4, 4, 4);
        // Cover top corners
        contentPath.addRect(boxX, boxY + titleBarH - 4, boxW, 4);
        painter.fillPath(contentPath, bgColor);
        
        // Draw border
        painter.setPen(QPen(QColor(255, 255, 255, 50), 1));
        painter.drawRoundedRect(boxRect.adjusted(0, 0, -1, -1), 4, 4);
        
        // Draw shadow effect (simple drop shadow)
        QPixmap shadowCanvas(280, 130);
        shadowCanvas.fill(Qt::transparent);
        QPainter shadowPainter(&shadowCanvas);
        shadowPainter.setRenderHint(QPainter::Antialiasing);
        shadowPainter.setPen(Qt::NoPen);
        shadowPainter.setBrush(QColor(0, 0, 0, 40));
        shadowPainter.drawRoundedRect(boxRect.adjusted(2, 4, -1, 1), 6, 6);
        shadowPainter.end();
        
        // Composite shadow first, then the box
        painter.drawPixmap(0, 0, shadowCanvas);
        
        // Redraw the box on top
        shadowPainter.begin(&canvas);
        shadowPainter.setRenderHint(QPainter::Antialiasing);
        shadowPainter.setPen(Qt::NoPen);
        shadowPainter.setBrush(QColor(0, 0, 0, 40));
        shadowPainter.drawRoundedRect(boxRect.adjusted(2, 4, -1, 1), 6, 6);
        shadowPainter.end();
        
        // Redraw title bar
        QPainterPath titlePath2;
        titlePath2.addRoundedRect(boxX, boxY, boxW, titleBarH, 4, 4);
        titlePath2.addRect(boxX, boxY + titleBarH - 4, boxW, 4);
        painter.fillPath(titlePath2, bgColor);
        
        // Redraw content area
        QPainterPath contentPath2;
        contentPath2.addRoundedRect(boxX, boxY + titleBarH - 4, boxW, boxH - titleBarH + 4, 4, 4);
        contentPath2.addRect(boxX, boxY + titleBarH - 4, boxW, 4);
        painter.fillPath(contentPath2, bgColor);
        
        // Redraw border
        painter.setPen(QPen(QColor(255, 255, 255, 50), 1));
        painter.drawRoundedRect(boxRect.adjusted(0, 0, -1, -1), 4, 4);
    }
    
    // Draw title bar icons
    painter.setPen(QColor(255, 255, 255, 180));
    QFont iconFont("Segoe Fluent Icons");
    if (!QFontInfo(iconFont).exactMatch()) {
        iconFont.setFamily("Segoe MDL2 Assets");
    }
    iconFont.setPixelSize(12);
    painter.setFont(iconFont);
    
    // Collapse icon
    painter.drawText(QRect(boxX + 6, boxY + 2, 24, 24), Qt::AlignCenter, u8"\uE70E");
    // View icon  
    painter.drawText(QRect(boxX + boxW - 30, boxY + 2, 24, 24), Qt::AlignCenter, u8"\uE8EF");
    
    // Title text
    painter.setPen(Qt::white);
    QFont titleFont("Segoe UI", 9, QFont::Bold);
    painter.setFont(titleFont);
    QFontMetrics fm(titleFont);
    QString title = "Preview Box";
    QString elidedTitle = fm.elidedText(title, Qt::ElideRight, boxW - 60);
    painter.drawText(QRect(boxX + 30, boxY + 4, boxW - 60, 20), Qt::AlignCenter, elidedTitle);
    
    // Draw sample icons in content area
    painter.setPen(QColor(255, 255, 255, 200));
    iconFont.setPixelSize(18);
    painter.setFont(iconFont);
    
    const int iconSize = 36;
    const int iconSpacing = 48;
    const int startX = boxX + 10;
    const int iconY = boxY + titleBarH + 10;
    
    QStringList iconSymbols = {u8"\uE7C3", u8"\uE8A5", u8"\uE721"};
    for (int i = 0; i < iconSymbols.size(); ++i) {
        int ix = startX + i * iconSpacing;
        if (ix + iconSize <= boxX + boxW - 10) {
            // Icon background
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 255, 255, 30));
            painter.drawRoundedRect(ix, iconY, iconSize, iconSize, 6, 6);
            
            // Icon symbol
            painter.setPen(QColor(255, 255, 255, 200));
            painter.setFont(iconFont);
            painter.drawText(QRect(ix, iconY, iconSize, iconSize), Qt::AlignCenter, iconSymbols[i]);
        }
    }
    
    painter.end();
    
    // Set the updated pixmap
    m_previewBackground->setPixmap(canvas);
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
