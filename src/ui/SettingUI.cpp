#include "ui/SettingUI.h"
#include "box/LinBox.h"
#include "box/BoxManager.h"
#include "desktop/ListView.h"
#include <windows.h>
#include <winuser.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <QScreen>
#include <QApplication>
#include <QCoreApplication>
#include <QPainter>
#include <QFrame>
#include <QCheckBox>
#include "desktop/ListView.h"
#include "desktop/LinDesktop.h"
#include "ui/Fluent/FluentDialog.h"
#include <QProcess>
#include <QScrollArea>
#include <QDateTime>
#include <windowsx.h>
#include <QStackedWidget>
#include "plugins/PluginManager.h"
#include <QStyledItemDelegate>
#include <QPainterPath>
#include "ui/Fluent/FluentToggleSwitch.h"
#include "ui/Fluent/FluentButton.h"
#include "ui/Fluent/FluentComboBox.h"
#include "ui/Fluent/FluentCard.h"
#include "ui/Fluent/FluentColorWheel.h"
#include "app/AppFonts.h"
#include "app/LanguageManager.h"
#include "services/SysInfoUtil.h" // For ShortcutArrowManager
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QFileInfo>
#include <QComboBox>
#include <QLayoutItem>
#include <QFileDialog>
#include <QDesktopServices>
#include <QSignalBlocker>
#include <QUrl>
#include <QDir>
#include <QShowEvent>

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
        font.setFamily(AppFonts::uiFamily());
        font.setPixelSize(14);
        font.setWeight(isSelected ? QFont::Weight::Medium : QFont::Weight::Normal);
        painter->setFont(font);
        
        QRect textRect(opt.rect.left() + textLeftPadding, opt.rect.top(), 
                       opt.rect.width() - textLeftPadding - 16, itemHeight);
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, text);

        painter->restore();
    }
};

namespace {
QString settingsConfigPath()
{
    return QCoreApplication::applicationDirPath() + "/config.ini";
}

QColor readDesktopSelectionColor()
{
    QSettings settings(settingsConfigPath(), QSettings::IniFormat);
    const QColor color(settings.value("Desktop/SelectionRectangleColor", QStringLiteral("#64A0F0")).toString());
    return color.isValid() ? color : QColor(100, 160, 240);
}

QColor desktopSelectionFillColor(const QColor& baseColor)
{
    QColor fill = baseColor;
    fill.setAlpha(70);
    return fill;
}

QColor desktopSelectionBorderColor(const QColor& baseColor)
{
    QColor border = baseColor.darker(110);
    border.setAlpha(180);
    return border;
}
}

SettingUI::SettingUI(LinBox* targetBox, const QVector<LinBox*>& allBoxes, QWidget* parent)
    : FluentBaseWidget(parent), m_targetBox(targetBox), m_allBoxes(allBoxes)
{
    resize(720, 520);
    setMinimumSize(520, 340);
    setWindowTitle(tr("LinDesk"));
    setResizable(true);
    setWindowBtnMode(All); // Show Minimize, Maximize, and Close buttons

    // Add title bar text (left side of title bar) - Fluent Design Title
    m_titleBarTitle = new QLabel(tr("LinDesk"), this);
    m_titleBarTitle->setStyleSheet(
        "font-family: 'Microsoft YaHei UI'; "
        "font-size: 12px; font-weight: 600; letter-spacing: 0.02em; color: #1f2937; "
        "margin-left: 8px; margin-right: 20px;"
    );
    addTitleBarWidget(m_titleBarTitle);

    m_selectionPreviewColor = readDesktopSelectionColor();

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

    ensureTrayIcon();
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
        {0xE80F, tr("General")},
        {0xE7F4, tr("Desktop")},
        {0xEA37, tr("Box")},
        {0xE74C, tr("Plugins")},
        {0xE946, tr("About")}
    };

    for (const auto& tab : tabs) {
        QListWidgetItem* item = new QListWidgetItem(m_sidebar);
        item->setText(tab.text);
        item->setSizeHint(QSize(208, 44));
        item->setData(Qt::UserRole, QString(tab.icon));
        switch (m_sidebar->count() - 1) {
        case 0: m_generalTabItem = item; break;
        case 1: m_desktopTabItem = item; break;
        case 2: m_boxTabItem = item; break;
        case 3: m_pluginsTabItem = item; break;
        case 4: m_aboutTabItem = item; break;
        default: break;
        }
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

QString SettingUI::startupShortcutPath() const
{
    wchar_t startupDir[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_STARTUP, nullptr, SHGFP_TYPE_CURRENT, startupDir))) {
        return QString();
    }

    return QDir::fromNativeSeparators(QString::fromWCharArray(startupDir)) + "/LinDesk.lnk";
}

bool SettingUI::isLaunchAtLoginEnabled() const
{
    const QString shortcutPath = startupShortcutPath();
    return !shortcutPath.isEmpty() && QFile::exists(shortcutPath);
}

bool SettingUI::setLaunchAtLoginEnabled(bool enabled)
{
    const QString shortcutPath = startupShortcutPath();
    if (shortcutPath.isEmpty()) {
        return false;
    }

    if (!enabled) {
        return !QFile::exists(shortcutPath) || QFile::remove(shortcutPath);
    }

    const QString exePath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString workingDir = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    const std::wstring shortcutPathW = QDir::toNativeSeparators(shortcutPath).toStdWString();
    const std::wstring exePathW = exePath.toStdWString();
    const std::wstring workingDirW = workingDir.toStdWString();

    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(initHr);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE) {
        return false;
    }

    IShellLinkW* shellLink = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&shellLink));
    if (FAILED(hr) || !shellLink) {
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return false;
    }

    shellLink->SetPath(exePathW.c_str());
    shellLink->SetWorkingDirectory(workingDirW.c_str());
    shellLink->SetIconLocation(exePathW.c_str(), 0);
    shellLink->SetDescription(L"Launch LinDesk at sign in");

    IPersistFile* persistFile = nullptr;
    hr = shellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persistFile));
    bool ok = false;
    if (SUCCEEDED(hr) && persistFile) {
        ok = SUCCEEDED(persistFile->Save(shortcutPathW.c_str(), TRUE));
        persistFile->Release();
    }

    shellLink->Release();
    if (shouldUninitialize) {
        CoUninitialize();
    }

    return ok;
}

bool SettingUI::shouldMinimizeToTray() const
{
    const QString configPath = QCoreApplication::applicationDirPath() + "/config.ini";
    QSettings settings(configPath, QSettings::IniFormat);
    return settings.value("General/MinimizeToTray", true).toBool();
}

void SettingUI::ensureTrayIcon()
{
    if (m_trayIcon || !QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }

    m_trayIcon = new QSystemTrayIcon(this);
    QIcon trayIcon = QApplication::windowIcon();
    if (trayIcon.isNull()) {
        trayIcon = QIcon(":/Assets/icon.png");
    }
    m_trayIcon->setIcon(trayIcon);
    m_trayIcon->setToolTip(tr("LinDesk Settings"));

    m_trayMenu = new QMenu(this);
    m_trayMenu->setStyleSheet(
        "QMenu {"
        " background: #ffffff;"
        " color: #1f2937;"
        " border: 1px solid rgba(0, 0, 0, 0.12);"
        " padding: 6px 0px;"
        " }"
        "QMenu::item {"
        " padding: 8px 18px;"
        " background: transparent;"
        " }"
        "QMenu::item:selected {"
        " background: #e8f3ff;"
        " color: #0f172a;"
        " }"
        "QMenu::separator {"
        " height: 1px;"
        " margin: 4px 10px;"
        " background: rgba(0, 0, 0, 0.08);"
        " }"
    );
    m_showTrayAction = m_trayMenu->addAction(tr("Show Settings"));
    m_exitTrayAction = m_trayMenu->addAction(tr("Exit"));
    m_trayIcon->setContextMenu(m_trayMenu);

    connect(m_showTrayAction, &QAction::triggered, this, &SettingUI::restoreFromTray);
    connect(m_exitTrayAction, &QAction::triggered, this, &SettingUI::exitFromTray);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            restoreFromTray();
        }
    });
}

void SettingUI::restoreFromTray()
{
    if (m_trayIcon) {
        m_trayIcon->hide();
    }

    if (windowState() & Qt::WindowMinimized) {
        setWindowState(windowState() & ~Qt::WindowMinimized);
    }

    show();
    showNormal();
    raise();
    activateWindow();
}

void SettingUI::exitFromTray()
{
    m_isQuittingFromTray = true;
    if (m_trayIcon) {
        m_trayIcon->hide();
    }
    QCoreApplication::quit();
}

QWidget* SettingUI::createGeneralPage() {
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(35, 25, 35, 25);
    layout->setSpacing(12);

    QString configPath = QCoreApplication::applicationDirPath() + "/config.ini";
    QSettings settings(configPath, QSettings::IniFormat);

    m_generalPageTitle = new QLabel(tr("General Settings"), page);
    m_generalPageTitle->setObjectName("pageTitle");
    layout->addWidget(m_generalPageTitle);

    // 1. Auto Start
    m_autoStartToggle = new FluentToggleSwitch(page);
    const bool autoStartEnabled = isLaunchAtLoginEnabled();
    m_autoStartToggle->setChecked(autoStartEnabled);
    settings.setValue("General/AutoStart", autoStartEnabled);
    connect(m_autoStartToggle, &FluentToggleSwitch::toggled, this, [this, configPath](bool checked){
        if (!setLaunchAtLoginEnabled(checked)) {
            if (m_autoStartToggle) {
                const QSignalBlocker blocker(m_autoStartToggle);
                m_autoStartToggle->setChecked(!checked);
            }
            FluentDialog::showInfo(this, tr("Launch at Login"), tr("LinDesk could not update the startup shortcut."));
            return;
        }

        QSettings s(configPath, QSettings::IniFormat);
        s.setValue("General/AutoStart", checked);
    });
    m_autoStartCard = new FluentCard(u8"\uE7E6", tr("Launch at Login"), tr("Automatically start LinDesk when you sign in to Windows"), m_autoStartToggle, page);
    layout->addWidget(m_autoStartCard);

    // 2. Tray Only
    FluentToggleSwitch* trayOnly = new FluentToggleSwitch(page);
    trayOnly->setChecked(settings.value("General/MinimizeToTray", true).toBool());
    connect(trayOnly, &FluentToggleSwitch::toggled, this, [configPath](bool checked){
        QSettings s(configPath, QSettings::IniFormat);
        s.setValue("General/MinimizeToTray", checked);
    });
    m_trayOnlyCard = new FluentCard(u8"\uE921", tr("Minimize to Tray"), tr("Keep LinDesk running in the system tray when window is closed"), trayOnly, page);
    layout->addWidget(m_trayOnlyCard);

    // 3. Language
    m_languageCombo = new FluentComboBox(page);
    m_languageCombo->addItem(tr("English (United States)"), QStringLiteral("en_US"));
    m_languageCombo->addItem(tr("Chinese (Simplified)"), QStringLiteral("zh_CN"));
    const QString configuredLanguage = LanguageManager::configuredLanguageCode();
    int languageIndex = m_languageCombo->findData(configuredLanguage);
    if (languageIndex < 0) {
        languageIndex = 0;
    }
    m_languageCombo->setCurrentIndex(languageIndex);
    m_languageCombo->setMinimumWidth(180);
    connect(m_languageCombo, QOverload<int>::of(&FluentComboBox::currentIndexChanged), this, [configPath](int index){
        Q_UNUSED(configPath);
        const QString languageCode = index == 1 ? QStringLiteral("zh_CN") : QStringLiteral("en_US");
        LanguageManager::instance().setLanguage(*qApp, languageCode);
    });
    m_languageCard = new FluentCard(u8"\uE774", tr("Display Language"), tr("Choose which language is used in the interface. Restart LinDesk to apply changes."), m_languageCombo, page);
    layout->addWidget(m_languageCard);

    layout->addStretch();
    return page;
}

QWidget* SettingUI::createDesktopPage() {
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(35, 25, 35, 25);
    layout->setSpacing(12);

    m_desktopPageTitle = new QLabel(tr("Desktop Customization"), page);
    m_desktopPageTitle->setObjectName("pageTitle");
    layout->addWidget(m_desktopPageTitle);

    QWidget* previewContainer = new QWidget(page);
    previewContainer->setFixedHeight(138);
    previewContainer->setStyleSheet("background: #f8f9fa; border-radius: 8px;");

    QVBoxLayout* previewLayout = new QVBoxLayout(previewContainer);
    previewLayout->setContentsMargins(16, 12, 16, 12);
    previewLayout->setSpacing(8);

    QLabel* previewTitle = new QLabel(tr("Preview"), previewContainer);
    previewTitle->setStyleSheet("font-size: 12px; font-weight: 600; color: #6b7280;");
    previewLayout->addWidget(previewTitle);

    m_desktopSelectionPreview = new QLabel(previewContainer);
    m_desktopSelectionPreview->setMinimumHeight(88);
    m_desktopSelectionPreview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_desktopSelectionPreview->setAlignment(Qt::AlignCenter);
    m_desktopSelectionPreview->setStyleSheet("border-radius: 10px; background: transparent;");
    previewLayout->addWidget(m_desktopSelectionPreview);

    layout->addWidget(previewContainer);

    QWidget* selectionActionSide = new QWidget(page);
    QHBoxLayout* selectionActionLayout = new QHBoxLayout(selectionActionSide);
    selectionActionLayout->setContentsMargins(0, 0, 0, 0);
    selectionActionLayout->setSpacing(16);

    m_selectionColorWheel = new FluentColorWheel(selectionActionSide);
    m_selectionColorWheel->setColor(m_selectionPreviewColor);

    m_selectionColorValueEdit = new QLineEdit(selectionActionSide);
    m_selectionColorValueEdit->setReadOnly(true);
    m_selectionColorValueEdit->setAlignment(Qt::AlignCenter);
    m_selectionColorValueEdit->setFixedWidth(112);
    m_selectionColorValueEdit->setFixedHeight(34);
    m_selectionColorValueEdit->setText(m_selectionPreviewColor.name().toUpper());
    m_selectionColorValueEdit->setStyleSheet(
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

    QVBoxLayout* selectionValueLayout = new QVBoxLayout();
    selectionValueLayout->setContentsMargins(0, 0, 0, 0);
    selectionValueLayout->setSpacing(0);
    selectionValueLayout->addStretch();
    selectionValueLayout->addWidget(m_selectionColorValueEdit, 0, Qt::AlignVCenter);
    selectionValueLayout->addStretch();

    selectionActionLayout->addWidget(m_selectionColorWheel);
    selectionActionLayout->addLayout(selectionValueLayout);
    selectionActionLayout->addStretch();

    connect(m_selectionColorWheel, &FluentColorWheel::colorChanged, this, [this](const QColor& color){
        m_selectionPreviewColor = color;
        if (m_selectionColorValueEdit) {
            m_selectionColorValueEdit->setText(color.name().toUpper());
        }
        QSettings settings(settingsConfigPath(), QSettings::IniFormat);
        settings.setValue("Desktop/SelectionRectangleColor", color.name());
        settings.sync();
        updateDesktopSelectionPreview();
    });

    QWidget* selectionColorSection = new QWidget(page);
    QVBoxLayout* selectionColorSectionLayout = new QVBoxLayout(selectionColorSection);
    selectionColorSectionLayout->setContentsMargins(8, 4, 8, 4);
    selectionColorSectionLayout->setSpacing(8);

    m_selectionColorSectionTitle = new QLabel(tr("Selection Rectangle Color"), selectionColorSection);
    m_selectionColorSectionTitle->setStyleSheet(
        "font-family: 'Microsoft YaHei UI';"
        "font-size: 14px;"
        "font-weight: 600;"
        "color: #1f2937;"
    );
    selectionColorSectionLayout->addWidget(m_selectionColorSectionTitle);

    m_selectionColorSectionSubtitle = new QLabel(tr("Adjust the drag selection color and preview the result in real time"), selectionColorSection);
    m_selectionColorSectionSubtitle->setWordWrap(true);
    m_selectionColorSectionSubtitle->setStyleSheet(
        "font-family: 'Microsoft YaHei UI';"
        "font-size: 12px;"
        "color: #616161;"
    );
    selectionColorSectionLayout->addWidget(m_selectionColorSectionSubtitle);
    selectionColorSectionLayout->addWidget(selectionActionSide);
    layout->addWidget(selectionColorSection);
    updateDesktopSelectionPreview();

    FluentToggleSwitch* arrowToggle = new FluentToggleSwitch(page);

    ListView* lv = ListView::instance();
    bool showArrow = lv ? lv->shortcutArrowVisible() : true;

    arrowToggle->setChecked(showArrow);

    connect(arrowToggle, &FluentToggleSwitch::toggled, this, [](bool checked){
        ListView* lv = ListView::instance();
        if (lv) lv->setShortcutArrowVisible(checked);
    });

    m_shortcutArrowCard = new FluentCard(u8"\uE724", tr("Shortcut Arrows"), tr("Toggle visibility of the small arrow on desktop shortcut icons"), arrowToggle, page);
    layout->addWidget(m_shortcutArrowCard);

    FluentToggleSwitch* hideToggle = new FluentToggleSwitch(page);
    bool doubleClickHide = lv ? lv->isDoubleClickHideIconsEnabled() : true;
    hideToggle->setChecked(doubleClickHide);
    connect(hideToggle, &FluentToggleSwitch::toggled, this, [](bool checked){
        ListView* lv = ListView::instance();
        if (lv) lv->setDoubleClickHideIconsEnabled(checked);
    });
    m_hideIconsCard = new FluentCard(u8"\uE949", tr("Double Click to Hide"), tr("Quickly show or hide all desktop icons by double-clicking on empty spaces"), hideToggle, page);
    layout->addWidget(m_hideIconsCard);

    layout->addStretch();
    return page;
}

QWidget* SettingUI::createGridPage() {
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(35, 25, 35, 25);
    layout->setSpacing(8);

    m_boxPageTitle = new QLabel(tr("Box UI"), page);
    m_boxPageTitle->setObjectName("pageTitle");
    layout->addWidget(m_boxPageTitle);
    layout->addSpacing(10);

    // Preview Box - New design with BoxPreview.png background and box overlay
    QWidget* previewContainer = new QWidget(page);
    previewContainer->setFixedHeight(200);
    previewContainer->setStyleSheet("background: #f8f9fa; border-radius: 8px;");
    
    QVBoxLayout* previewLayout = new QVBoxLayout(previewContainer);
    previewLayout->setContentsMargins(16, 12, 16, 12);
    previewLayout->setSpacing(8);
    
    m_previewLabel = new QLabel(tr("Preview"), previewContainer);
    m_previewLabel->setStyleSheet("font-size: 12px; font-weight: 600; color: #6b7280;");
    previewLayout->addWidget(m_previewLabel);
    
    // Preview surface fills the available preview area.
    m_previewBackground = new QLabel(previewContainer);
    m_previewBackground->setMinimumHeight(145);
    m_previewBackground->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_previewBackground->setScaledContents(false);
    m_previewBackground->setStyleSheet("border-radius: 12px;");
    m_previewBackground->setAlignment(Qt::AlignCenter);
    previewLayout->addWidget(m_previewBackground);
    
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
    colorValueLayout->setContentsMargins(0, 0, 0, 0);
    colorValueLayout->setSpacing(0);
    colorValueLayout->addStretch();
    colorValueLayout->addWidget(colorValueEdit, 0, Qt::AlignVCenter);
    colorValueLayout->addStretch();

    if (LinBox* sourceBox = controlSourceBox()) {
        colorWheel->setColor(sourceBox->backgroundColor());
        colorValueEdit->setText(sourceBox->backgroundColor().name().toUpper());
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

    QWidget* boxColorSection = new QWidget(page);
    QVBoxLayout* boxColorSectionLayout = new QVBoxLayout(boxColorSection);
    boxColorSectionLayout->setContentsMargins(8, 4, 8, 4);
    boxColorSectionLayout->setSpacing(8);

    m_boxColorSectionTitle = new QLabel(tr("Accent Color"), boxColorSection);
    m_boxColorSectionTitle->setStyleSheet(
        "font-family: 'Microsoft YaHei UI';"
        "font-size: 14px;"
        "font-weight: 600;"
        "color: #1f2937;"
    );
    boxColorSectionLayout->addWidget(m_boxColorSectionTitle);

    m_boxColorSectionSubtitle = new QLabel(tr("Define the primary theme color for your desktop boxes"), boxColorSection);
    m_boxColorSectionSubtitle->setWordWrap(true);
    m_boxColorSectionSubtitle->setStyleSheet(
        "font-family: 'Microsoft YaHei UI';"
        "font-size: 12px;"
        "color: #616161;"
    );
    boxColorSectionLayout->addWidget(m_boxColorSectionSubtitle);
    boxColorSectionLayout->addWidget(colorActionSide);
    layout->addWidget(boxColorSection);

    // 2. Transparency Slider (create first for preview connections)
    m_opacitySlider = new QSlider(Qt::Horizontal, page);
    m_opacitySlider->setRange(5, 100);
    m_opacitySlider->setFixedWidth(150);

    m_opacityLabelValue = new QLabel(page);
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

    m_opacityCard = new FluentCard(u8"\uE7E6", tr("Background Opacity"), tr("Adjust the semi-transparency level of the grid surface"), opacActionSide, page);
    layout->addWidget(m_opacityCard);

    int initialOpacity = qRound(m_previewOpacity * 100.0f);
    if (LinBox* sourceBox = controlSourceBox()) {
        initialOpacity = qRound(sourceBox->backgroundOpacity() * 100.0f);
    }
    initialOpacity = qBound(m_opacitySlider->minimum(), initialOpacity, m_opacitySlider->maximum());
    m_opacitySlider->setValue(initialOpacity);
    m_opacityLabelValue->setText(QString("%1%").arg(initialOpacity));

    // 3. Acrylic Toggle
    m_glassToggle = new FluentToggleSwitch(page);
    if (LinBox* sourceBox = controlSourceBox()) m_glassToggle->setChecked(sourceBox->glassEffect(), false);
    
    connect(m_glassToggle, &FluentToggleSwitch::toggled, this, [this](bool v){ 
        for(auto b : m_allBoxes) b->setGlassEffect(v); 
        m_previewGlass = v;  // Sync preview glass
        updatePreviewStyle();
        ListView* lv = ListView::instance();
        if (lv) lv->requestSaveBoxes();
    });

    m_glassCard = new FluentCard(u8"\uE91B", tr("Acrylic Glass Effect"), tr("Enable real-time DWM background blur for a translucent look"), m_glassToggle, page);
    layout->addWidget(m_glassCard);

    // Initial preview style sync
    if (LinBox* sourceBox = controlSourceBox()) {
        m_previewColor = sourceBox->backgroundColor();
        m_previewOpacity = sourceBox->backgroundOpacity();
        m_previewGlass = sourceBox->glassEffect();
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

    m_aboutNameLabel = new QLabel(tr("LinDesk"), page);
    m_aboutNameLabel->setStyleSheet(
        "font-family: 'Microsoft YaHei UI'; "
        "font-size: 28px; "
        "font-weight: 600; "
        "color: #1f2937; "
        "letter-spacing: -0.01em;"
    );
    layout->addWidget(m_aboutNameLabel, 0, Qt::AlignCenter);

    m_aboutVersionLabel = new QLabel(tr("Version 1.0.0 Stable"), page);
    m_aboutVersionLabel->setStyleSheet(
        "font-family: 'Microsoft YaHei UI'; "
        "font-size: 13px; "
        "color: #6b7280;"
    );
    layout->addWidget(m_aboutVersionLabel, 0, Qt::AlignCenter);
    
    layout->addSpacing(20);
    
    m_aboutTaglineLabel = new QLabel(tr("Powered by Antigravity Design System"), page);
    m_aboutTaglineLabel->setStyleSheet(
        "font-family: 'Microsoft YaHei UI'; "
        "font-size: 12px; "
        "color: #9ca3af;"
    );
    layout->addWidget(m_aboutTaglineLabel, 0, Qt::AlignCenter);
    
    layout->addStretch();
    return page;
}

QWidget* SettingUI::createPluginsPage() {
    QWidget* page = new QWidget();
    QVBoxLayout* mainLayout = new QVBoxLayout(page);
    mainLayout->setContentsMargins(40, 40, 40, 40);
    mainLayout->setSpacing(24);

    m_pluginsPageTitle = new QLabel(tr("Extending Your Universe"), page);
    m_pluginsPageTitle->setObjectName("pageTitle");
    mainLayout->addWidget(m_pluginsPageTitle);

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
        m_pluginsEmptyLabel = new QLabel(tr("No plugins found in /plugins directory."), page);
        m_pluginsEmptyLabel->setStyleSheet("color: #9ca3af; font-style: italic;");
        listLayout->addWidget(m_pluginsEmptyLabel);
    } else {
        for (auto* plugin : allPlugins) {
            if (!plugin) continue;

            FluentToggleSwitch* toggle = new FluentToggleSwitch(page);
            toggle->setChecked(PluginManager::instance()->isPluginEnabled(plugin->pluginId()), false);
            QString pId = plugin->pluginId();
            connect(toggle, &FluentToggleSwitch::toggled, this, [pId](bool checked) {
                PluginManager::instance()->setPluginEnabled(pId, checked);
            });

            FluentButton* settingsBtn = new FluentButton(tr("Settings"), page);
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
        "   font-family: 'Microsoft YaHei UI'; "
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
        "QLabel#pageTitle { font-family: 'Microsoft YaHei UI'; font-size: 28px; font-weight: 600; color: #1f2937; margin-bottom: 4px; letter-spacing: -0.01em; } "
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
    updateControlsFromTarget();
}

void SettingUI::updateControlsFromTarget() {
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
    // Sync preview state from target box
    m_previewColor = sourceBox->backgroundColor();
    m_previewOpacity = sourceBox->backgroundOpacity();
    m_previewGlass = sourceBox->glassEffect();
    updatePreviewStyle();
}

LinBox* SettingUI::controlSourceBox() const {
    if (m_targetBox) {
        return m_targetBox;
    }

    for (LinBox* box : m_allBoxes) {
        if (box) {
            return box;
        }
    }
    return nullptr;
}

void SettingUI::updateDesktopSelectionPreview()
{
    if (!m_desktopSelectionPreview) {
        return;
    }

    const QSize previewSize = m_desktopSelectionPreview->size().expandedTo(QSize(220, 88));
    QPixmap canvas(previewSize);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    QLinearGradient background(0, 0, previewSize.width(), previewSize.height());
    background.setColorAt(0.0, QColor(245, 248, 252));
    background.setColorAt(1.0, QColor(233, 239, 247));
    painter.fillRect(canvas.rect(), background);

    const QVector<QRect> iconRects = {
        QRect(24, 18, 26, 26),
        QRect(68, 24, 26, 26),
        QRect(122, 16, 26, 26),
        QRect(164, 28, 26, 26)
    };
    painter.setPen(Qt::NoPen);
    for (const QRect& iconRect : iconRects) {
        painter.setBrush(QColor(255, 255, 255, 145));
        painter.drawRoundedRect(iconRect, 7, 7);
    }

    const QColor fillColor = desktopSelectionFillColor(m_selectionPreviewColor);
    const QColor borderColor = desktopSelectionBorderColor(m_selectionPreviewColor);
    const QRect selectionRect(52, 14, 110, 54);
    painter.fillRect(selectionRect, fillColor);
    painter.setPen(QPen(borderColor, 1));
    painter.drawRect(selectionRect.adjusted(0, 0, -1, -1));

    painter.end();
    m_desktopSelectionPreview->setPixmap(canvas);
}

void SettingUI::updatePreviewStyle() {
    if (!m_previewBackground) return;

    const QSize previewSize = m_previewBackground->size().expandedTo(QSize(320, 145));
    const int canvasW = previewSize.width();
    const int canvasH = previewSize.height();
    
    int r = m_previewColor.red();
    int g = m_previewColor.green();
    int b = m_previewColor.blue();
    int a = m_previewGlass ? 1 : (int)(m_previewOpacity * 255);
    
    if (a < 5) a = 20;
    
    // Create a new pixmap with BoxPreview.png as background and draw box on top
    QPixmap canvas(canvasW, canvasH);
    canvas.fill(Qt::transparent);
    
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    
    // Draw background image using Qt resource system
    QPixmap bgPixmap(":/Assets/BoxPreview.png");
    if (!bgPixmap.isNull()) {
        painter.drawPixmap(0, 0, bgPixmap.scaled(canvasW, canvasH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    } else {
        // Fallback: draw gradient background
        QLinearGradient grad(0, 0, canvasW, canvasH);
        grad.setColorAt(0, QColor(45, 55, 72));
        grad.setColorAt(1, QColor(30, 35, 45));
        painter.fillRect(canvas.rect(), grad);
    }
    
    // Keep the preview box itself at a fixed size while the background fills
    // the whole preview surface.
    const int boxW = 200;
    const int boxH = 120;
    const int boxX = (canvasW - boxW) / 2;
    const int boxY = (canvasH - boxH) / 2;
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
        QPixmap shadowCanvas(canvasW, canvasH);
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
    QFont titleFont(AppFonts::uiFamily(), 9, QFont::Bold);
    painter.setFont(titleFont);
    QFontMetrics fm(titleFont);
    QString title = tr("Preview Box");
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

    const bool forceQuit = QCoreApplication::instance() &&
        QCoreApplication::instance()->property("LinDeskForceQuit").toBool();
    if (!forceQuit && !m_isQuittingFromTray && shouldMinimizeToTray() && m_trayIcon) {
        event->ignore();
        hide();
        m_trayIcon->show();
        if (!m_hasShownTrayHint) {
            m_trayIcon->showMessage(tr("LinDesk"), tr("Settings is still available in the system tray."), QSystemTrayIcon::Information, 3000);
            m_hasShownTrayHint = true;
        }
        return;
    }

    this->hide();
    event->ignore(); // Prevent destruction
}

void SettingUI::changeEvent(QEvent* event) {
    if (event && event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }

    FluentBaseWidget::changeEvent(event);
}

void SettingUI::showEvent(QShowEvent* event) {
    if (m_trayIcon) {
        m_trayIcon->hide();
    }
    FluentBaseWidget::showEvent(event);
}

void SettingUI::retranslateUi() {
    setWindowTitle(tr("LinDesk"));
    if (m_titleBarTitle) m_titleBarTitle->setText(tr("LinDesk"));

    if (m_generalTabItem) m_generalTabItem->setText(tr("General"));
    if (m_desktopTabItem) m_desktopTabItem->setText(tr("Desktop"));
    if (m_boxTabItem) m_boxTabItem->setText(tr("Box"));
    if (m_pluginsTabItem) m_pluginsTabItem->setText(tr("Plugins"));
    if (m_aboutTabItem) m_aboutTabItem->setText(tr("About"));

    if (m_generalPageTitle) m_generalPageTitle->setText(tr("General Settings"));
    if (m_desktopPageTitle) m_desktopPageTitle->setText(tr("Desktop Customization"));
    if (m_boxPageTitle) m_boxPageTitle->setText(tr("Box UI"));
    if (m_pluginsPageTitle) m_pluginsPageTitle->setText(tr("Extending Your Universe"));
    if (m_pluginsEmptyLabel) m_pluginsEmptyLabel->setText(tr("No plugins found in /plugins directory."));
    if (m_aboutNameLabel) m_aboutNameLabel->setText(tr("LinDesk"));
    if (m_aboutVersionLabel) m_aboutVersionLabel->setText(tr("Version 1.0.0 Stable"));
    if (m_aboutTaglineLabel) m_aboutTaglineLabel->setText(tr("Powered by Antigravity Design System"));
    if (m_previewLabel) m_previewLabel->setText(tr("Preview"));

    if (m_autoStartCard) {
        m_autoStartCard->setTitle(tr("Launch at Login"));
        m_autoStartCard->setSubtitle(tr("Automatically start LinDesk when you sign in to Windows"));
    }
    if (m_trayOnlyCard) {
        m_trayOnlyCard->setTitle(tr("Minimize to Tray"));
        m_trayOnlyCard->setSubtitle(tr("Keep LinDesk running in the system tray when window is closed"));
    }
    if (m_trayIcon) {
        m_trayIcon->setToolTip(tr("LinDesk Settings"));
    }
    if (m_showTrayAction) {
        m_showTrayAction->setText(tr("Show Settings"));
    }
    if (m_exitTrayAction) {
        m_exitTrayAction->setText(tr("Exit"));
    }
    if (m_languageCard) {
        m_languageCard->setTitle(tr("Display Language"));
        m_languageCard->setSubtitle(tr("Choose which language is used in the interface. Restart LinDesk to apply changes."));
    }
    if (m_selectionColorSectionTitle) m_selectionColorSectionTitle->setText(tr("Selection Rectangle Color"));
    if (m_selectionColorSectionSubtitle) m_selectionColorSectionSubtitle->setText(tr("Adjust the drag selection color and preview the result in real time"));
    if (m_shortcutArrowCard) {
        m_shortcutArrowCard->setTitle(tr("Shortcut Arrows"));
        m_shortcutArrowCard->setSubtitle(tr("Toggle visibility of the small arrow on desktop shortcut icons"));
    }
    if (m_hideIconsCard) {
        m_hideIconsCard->setTitle(tr("Double Click to Hide"));
        m_hideIconsCard->setSubtitle(tr("Quickly show or hide all desktop icons by double-clicking on empty spaces"));
    }
    if (m_boxColorSectionTitle) m_boxColorSectionTitle->setText(tr("Accent Color"));
    if (m_boxColorSectionSubtitle) m_boxColorSectionSubtitle->setText(tr("Define the primary theme color for your desktop boxes"));
    if (m_opacityCard) {
        m_opacityCard->setTitle(tr("Background Opacity"));
        m_opacityCard->setSubtitle(tr("Adjust the semi-transparency level of the grid surface"));
    }
    if (m_glassCard) {
        m_glassCard->setTitle(tr("Acrylic Glass Effect"));
        m_glassCard->setSubtitle(tr("Enable real-time DWM background blur for a translucent look"));
    }
    if (m_languageCombo) {
        const QString currentCode = m_languageCombo->currentData().toString();
        m_languageCombo->blockSignals(true);
        m_languageCombo->clear();
        m_languageCombo->addItem(tr("English (United States)"), QStringLiteral("en_US"));
        m_languageCombo->addItem(tr("Chinese (Simplified)"), QStringLiteral("zh_CN"));
        int languageIndex = m_languageCombo->findData(currentCode);
        if (languageIndex < 0) {
            languageIndex = 0;
        }
        m_languageCombo->setCurrentIndex(languageIndex);
        m_languageCombo->blockSignals(false);
    }
    updatePreviewStyle();
}

// Legacy paint event removed - handled by FluentBaseWidget

// NativeEvent has been gracefully moved to FluentBaseWidget






