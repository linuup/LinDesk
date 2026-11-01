#include "ui/SettingUI.h"
#include "ui/settings/SettingsUICommon.h"
#include "ui/settings/navigation/FluentNavigationPanel.h"
#include "ui/settings/pages/GeneralSettingsPage.h"
#include "ui/settings/pages/DesktopSettingsPage.h"
#include "ui/settings/pages/ContextMenuSettingsPage.h"
#include "ui/settings/pages/BoxSettingsPage.h"
#include "ui/settings/pages/LinWidgetsSettingsPage.h"
#include "ui/settings/pages/PluginsSettingsPage.h"
#include "ui/settings/pages/AboutSettingsPage.h"
#include "ui/Fluent/SmoothScrollArea.h"
#include "app/AppFonts.h"
#include "services/AppPaths.h"
#include "box/LinBox.h"

#include <windows.h>
#include <QApplication>
#include <QCoreApplication>
#include <QSettings>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QEasingCurve>
#include <QScrollArea>
#include <QFontDatabase>

using namespace SettingsUICommon;

static void ensureSegoeFluentIconsLoaded()
{
    static bool loaded = false;
    if (loaded) return;
    loaded = true;
    QFontDatabase::addApplicationFont(QStringLiteral(":/Assets/Segoe Fluent Icons.ttf"));
}

SettingUI::SettingUI(LinBox* targetBox, const QVector<LinBox*>& allBoxes, QWidget* parent)
    : FluentBaseWidget(parent)
    , m_targetBox(targetBox)
{
    for (auto* b : allBoxes) {
        if (b) {
            m_allBoxes.append(b);
        }
    }

    setWindowTitle(tr("LinDesk Settings"));
    setWindowIcon(QIcon(":/Assets/icon.png"));
    setMinimumSize(780, 560);
    resize(920, 640);

    QSettings settings(settingsConfigPath(), QSettings::IniFormat);
    if (settings.contains("SettingUI/Size")) {
        resize(settings.value("SettingUI/Size").toSize().expandedTo(minimumSize()));
    }

    initUI();
    setupStyles();
}

void SettingUI::initUI()
{
    ensureSegoeFluentIconsLoaded();

    if (container()) {
        container()->setAttribute(Qt::WA_StyledBackground, true);
    }

    QHBoxLayout* mainLayout = new QHBoxLayout();
    centralLayout()->addLayout(mainLayout);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Left Navigation Panel (Windows 11 Fluent Design with Animated Indicator Pill)
    m_navPanel = new FluentNavigationPanel(container());
    m_navPanel->setFixedWidth(200);
    mainLayout->addWidget(m_navPanel);

    // Right Content Area
    QWidget* rightArea = new QWidget(container());
    rightArea->setObjectName("rightArea");
    rightArea->setAttribute(Qt::WA_StyledBackground, true);
    rightArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightArea);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    // Window controls header
    QWidget* windowControls = new QWidget(rightArea);
    windowControls->setObjectName("settingsWindowControls");
    windowControls->setFixedHeight(32);
    QHBoxLayout* windowControlsLayout = new QHBoxLayout(windowControls);
    windowControlsLayout->setContentsMargins(0, 0, 0, 0);
    windowControlsLayout->setSpacing(0);
    windowControlsLayout->addStretch(1);

    auto createWindowControlButton = [windowControls](const QString& text, const QString& objectName) {
        QPushButton* button = new QPushButton(text, windowControls);
        button->setObjectName(objectName);
        button->setFixedSize(45, 32);
        button->setCursor(Qt::ArrowCursor);
        return button;
    };

    m_settingsMinBtn = createWindowControlButton(QString::fromUtf8(u8"\uE921"), QStringLiteral("settingsMinBtn"));
    m_settingsMaxBtn = createWindowControlButton(QString::fromUtf8(u8"\uE922"), QStringLiteral("settingsMaxBtn"));
    m_settingsCloseBtn = createWindowControlButton(QString::fromUtf8(u8"\uE8BB"), QStringLiteral("settingsCloseBtn"));
    windowControlsLayout->addWidget(m_settingsMinBtn);
    windowControlsLayout->addWidget(m_settingsMaxBtn);
    windowControlsLayout->addWidget(m_settingsCloseBtn);

    connect(m_settingsMinBtn, &QPushButton::clicked, this, &QWidget::showMinimized);
    connect(m_settingsMaxBtn, &QPushButton::clicked, this, [this]() {
        if (windowState() & Qt::WindowMaximized) {
            setWindowState(Qt::WindowNoState);
        } else {
            setWindowState(Qt::WindowMaximized);
        }
        updateWindowControlButtons();
    });
    connect(m_settingsCloseBtn, &QPushButton::clicked, this, &QWidget::close);
    rightLayout->addWidget(windowControls);

    // Content stack with 7 pages
    m_contentStack = new QStackedWidget(container());
    m_contentStack->setObjectName("contentStack");
    m_contentStack->setAttribute(Qt::WA_StyledBackground, true);
    m_contentStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    for (int i = 0; i < 7; ++i) {
        QWidget* placeholder = new QWidget(container());
        m_contentStack->addWidget(placeholder);
    }
    rightLayout->addWidget(m_contentStack);
    mainLayout->addWidget(rightArea, 1);

    connect(m_navPanel, &FluentNavigationPanel::currentIndexChanged, this, &SettingUI::switchToPage);

    // Initial page
    const int initialRow = (m_targetBox ? 3 : 0);
    getOrCreatePage(initialRow);
    m_navPanel->setCurrentIndex(initialRow);
    m_contentStack->setCurrentIndex(initialRow);
}

void SettingUI::switchToPage(int index)
{
    if (index < 0 || index >= 7) {
        return;
    }
    QWidget* page = getOrCreatePage(index);
    if (!page) {
        return;
    }

    if (m_contentStack->currentIndex() == index) {
        return;
    }

    m_contentStack->setCurrentIndex(index);
}

QWidget* SettingUI::getOrCreatePage(int index)
{
    if (index < 0 || index >= 7) {
        return nullptr;
    }
    if (m_pageCache[index]) {
        return m_pageCache[index];
    }

    QWidget* page = nullptr;
    switch (index) {
    case 0: {
        auto* genPage = new GeneralSettingsPage(this);
        connect(genPage, &GeneralSettingsPage::requestNavigate, this, [this](int targetIndex) {
            if (m_navPanel) {
                m_navPanel->setCurrentIndex(targetIndex);
            }
            switchToPage(targetIndex);
        });
        m_generalPage = genPage;
        page = wrapScrollablePage(genPage);
        break;
    }
    case 1: {
        auto* deskPage = new DesktopSettingsPage(this);
        m_desktopPage = deskPage;
        page = wrapScrollablePage(deskPage);
        break;
    }
    case 2: {
        auto* menuPage = new ContextMenuSettingsPage(this);
        m_contextMenuPage = menuPage;
        page = wrapScrollablePage(menuPage);
        break;
    }
    case 3: {
        QVector<LinBox*> rawBoxes;
        rawBoxes.reserve(m_allBoxes.size());
        for (const auto& boxPtr : m_allBoxes) {
            if (boxPtr) {
                rawBoxes.append(boxPtr.data());
            }
        }
        auto* boxPage = new BoxSettingsPage(m_targetBox.data(), rawBoxes, this);
        m_boxPage = boxPage;
        page = wrapScrollablePage(boxPage);
        break;
    }
    case 4: {
        auto* linWidgetsPage = new LinWidgetsSettingsPage(this);
        m_linWidgetsPage = linWidgetsPage;
        page = wrapScrollablePage(linWidgetsPage);
        break;
    }
    case 5: {
        auto* pluginsPage = new PluginsSettingsPage(this);
        m_pluginsPage = pluginsPage;
        page = pluginsPage; // PluginsSettingsPage handles its own scroll container
        break;
    }
    case 6: {
        auto* aboutPage = new AboutSettingsPage(this);
        m_aboutPage = aboutPage;
        page = wrapScrollablePage(aboutPage);
        break;
    }
    default:
        break;
    }

    if (page) {
        m_pageCache[index] = page;
        QWidget* oldPlaceholder = m_contentStack->widget(index);
        m_contentStack->removeWidget(oldPlaceholder);
        m_contentStack->insertWidget(index, page);
        if (oldPlaceholder) {
            oldPlaceholder->deleteLater();
        }
    }

    return page;
}

QWidget* SettingUI::wrapScrollablePage(QWidget* content)
{
    if (!content) {
        return new QWidget(container());
    }

    SmoothScrollArea* scroll = new SmoothScrollArea(container());
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(0);
    scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setStyleSheet("QScrollArea { background-color: #fbfcfe; border: none; }");

    if (scroll->viewport()) {
        scroll->viewport()->setAutoFillBackground(true);
        QPalette p = scroll->viewport()->palette();
        p.setColor(QPalette::Window, QColor(251, 252, 254));
        scroll->viewport()->setPalette(p);
    }

    content->setMinimumWidth(0);
    content->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::MinimumExpanding);
    if (QLayout* contentLayout = content->layout()) {
        contentLayout->setSizeConstraint(QLayout::SetNoConstraint);
    }
    scroll->setWidget(content);
    return scroll;
}

void SettingUI::setTargetBox(LinBox* target)
{
    m_targetBox = target;
    if (m_boxPage) {
        m_boxPage->setTargetBox(target);
    }
}

bool SettingUI::shouldMinimizeToTray() const
{
    const QString configPath = settingsConfigPath();
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
    restoreAndActivate();
}

void SettingUI::exitFromTray()
{
    m_isQuittingFromTray = true;
    if (m_trayIcon) {
        m_trayIcon->hide();
    }
    QCoreApplication::quit();
}

void SettingUI::restoreAndActivate()
{
    if (m_trayIcon) {
        m_trayIcon->hide();
    }

    setWindowState(windowState() & ~Qt::WindowMinimized);
    if (!isVisible()) {
        show();
    }
    showNormal();
    raise();
    activateWindow();
#ifdef Q_OS_WIN
    if (const HWND hwnd = reinterpret_cast<HWND>(winId())) {
        ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
    }
#endif
}

void SettingUI::closeEvent(QCloseEvent* event)
{
    QString configPath = settingsConfigPath();
    QSettings settings(configPath, QSettings::IniFormat);
    settings.setValue("SettingUI/Size", size());
    settings.sync();

    const bool forceQuit = QCoreApplication::instance() &&
        QCoreApplication::instance()->property("LinDeskForceQuit").toBool();
    if (forceQuit || m_isQuittingFromTray) {
        event->accept();
        QWidget::closeEvent(event);
        return;
    }

    if (shouldMinimizeToTray()) {
        ensureTrayIcon();
        if (m_trayIcon) {
            event->ignore();
            hide();
            m_trayIcon->show();
            if (!m_hasShownTrayHint) {
                m_trayIcon->showMessage(tr("LinDesk"), tr("Settings is still available in the system tray."), QSystemTrayIcon::Information, 3000);
                m_hasShownTrayHint = true;
            }
            return;
        }
    }

    event->ignore();
    showMinimized();
}

void SettingUI::changeEvent(QEvent* event)
{
    FluentBaseWidget::changeEvent(event);
    if (event) {
        if (event->type() == QEvent::WindowStateChange) {
            updateWindowControlButtons();
        } else if (event->type() == QEvent::LanguageChange) {
            setWindowTitle(tr("LinDesk Settings"));
            if (m_navPanel) m_navPanel->updateTranslations();
            if (m_generalPage) m_generalPage->updateTranslations();
            if (m_desktopPage) m_desktopPage->updateTranslations();
            if (m_contextMenuPage) m_contextMenuPage->updateTranslations();
            if (m_boxPage) m_boxPage->updateTranslations();
            if (m_linWidgetsPage) m_linWidgetsPage->updateTranslations();
            if (m_pluginsPage) m_pluginsPage->updateTranslations();
            if (m_aboutPage) m_aboutPage->updateTranslations();
        }
    }
}

void SettingUI::showEvent(QShowEvent* event)
{
    if (m_trayIcon) {
        m_trayIcon->hide();
    }
    FluentBaseWidget::showEvent(event);
}

void SettingUI::resizeEvent(QResizeEvent* event)
{
    FluentBaseWidget::resizeEvent(event);
}

void SettingUI::updateWindowControlButtons()
{
    if (m_settingsMaxBtn) {
        m_settingsMaxBtn->setText((windowState() & Qt::WindowMaximized)
            ? QString::fromUtf8(u8"\uE923")
            : QString::fromUtf8(u8"\uE922"));
    }
}

void SettingUI::setupStyles()
{
    setStyleSheet(
        "SettingUI, QWidget#rightArea { "
        "   background-color: transparent; "
        "   border: none; "
        "   color: #142033; "
        "   font-family: '" + AppFonts::uiFamily() + "', 'Segoe UI', 'Microsoft YaHei UI'; "
        "} "
        "QWidget#fluentBaseContainer { "
        "   background-color: #f9fbff; "
        "   border: 1px solid #cfd8e6; "
        "   border-radius: 12px; "
        "} "
        "SettingUI { "
        "   background-color: transparent; "
        "} "
        "QWidget#rightArea { "
        "   background: #fbfcfe; "
        "   border-top-right-radius: 12px; "
        "   border-bottom-right-radius: 12px; "
        "} "
        "QStackedWidget#contentStack { "
        "   background: #fbfcfe; "
        "   border: none; "
        "} "
        "QWidget#settingsWindowControls { "
        "   background: transparent; "
        "} "
        "QPushButton#settingsMinBtn, QPushButton#settingsMaxBtn, QPushButton#settingsCloseBtn { "
        "   background: transparent; border: none; font-family: 'Segoe MDL2 Assets'; font-size: 10px; color: #26324a; "
        "} "
        "QPushButton#settingsCloseBtn { border-top-right-radius: 12px; } "
        "QPushButton#settingsMinBtn:hover, QPushButton#settingsMaxBtn:hover { background: rgba(33, 45, 69, 0.07); } "
        "QPushButton#settingsCloseBtn:hover { background: #dc2626; color: white; border-top-right-radius: 12px; } "
        "QScrollBar:vertical { "
        "   border: none; background: transparent; width: 6px; margin: 12px 4px 12px 0; "
        "} "
        "QScrollBar::handle:vertical { "
        "   background: rgba(64, 76, 96, 0.18); border-radius: 3px; min-height: 24px; "
        "} "
        "QScrollBar::handle:vertical:hover { background: rgba(64, 76, 96, 0.30); } "
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; } "
        "QLabel#pageTitle { font-size: 26px; font-weight: 800; color: #101827; margin-bottom: 0; letter-spacing: 0; } "
        "QLabel#pageSubtitle { font-size: 15px; font-weight: 500; color: #66738c; margin-bottom: 8px; } "
        "QLabel#cardTitle { font-size: 16px; font-weight: 800; color: #101827; } "
        "QLabel#cardSubtitle { font-size: 14px; font-weight: 500; color: #63708a; } "
        "QLabel#rowTitle { font-size: 14px; font-weight: 800; color: #142033; } "
        "QLabel#rowSubtitle { font-size: 13px; font-weight: 500; color: #63708a; } "
        "QLabel#valueLabel { color: #63708a; font-size: 14px; font-weight: 700; } "
        "QFrame#shellExcludeBody { "
        "   background: rgba(247, 250, 255, 0.82); "
        "   border: 1px solid rgba(207, 216, 230, 0.82); "
        "   border-radius: 10px; "
        "} "
        "QWidget#shellExcludeTypePanel { background: transparent; border: none; } "
        "QWidget#shellExcludeDetail { background: transparent; border: none; } "
        "QWidget#shellExcludePane { background: transparent; border: none; } "
        "QWidget#shellExcludeTransferPane { background: transparent; border: none; } "
        "QWidget#shellExcludeActionPane { background: transparent; border: none; } "
        "QLabel#shellExcludeGroupTitle { "
        "   color: #5e6c84; "
        "   font-size: 12px; "
        "   font-weight: 800; "
        "   padding: 2px 4px; "
        "} "
        "QListWidget#shellExcludeTypeList { "
        "   background: rgba(255, 255, 255, 0.72); "
        "   border: 1px solid rgba(214, 223, 236, 0.76); "
        "   border-radius: 8px; "
        "   padding: 6px; "
        "   outline: none; "
        "} "
        "QListWidget#shellExcludeTypeList::item { "
        "   min-height: 34px; "
        "   padding: 0 10px; "
        "   border-radius: 6px; "
        "   color: #28344a; "
        "   font-size: 13px; "
        "   font-weight: 700; "
        "} "
        "QListWidget#shellExcludeTypeList::item:hover { background: #eef6ff; } "
        "QListWidget#shellExcludeTypeList::item:selected { "
        "   background: #e6f1ff; "
        "   color: #005fb8; "
        "} "
        "QListWidget#shellExcludeTreeList { "
        "   background: rgba(255, 255, 255, 0.74); "
        "   border: 1px solid rgba(214, 223, 236, 0.76); "
        "   border-radius: 8px; "
        "   padding: 6px; "
        "   outline: none; "
        "} "
        "QListWidget#shellExcludeTreeList::item { "
        "   background: transparent; "
        "   border: none; "
        "   padding: 0; "
        "} "
        "QListWidget#shellExcludeTreeList::item:selected { "
        "   background: rgba(0, 120, 212, 0.10); "
        "   border-radius: 5px; "
        "} "
        "QListWidget#shellExcludeTreeList::item:hover { "
        "   background: rgba(238, 246, 255, 0.92); "
        "   border-radius: 5px; "
        "} "
        "QLabel#shellExcludeEmptyLabel { "
        "   color: #63708a; "
        "   font-size: 13px; "
        "   font-weight: 600; "
        "   padding: 28px; "
        "} "
        "QWidget#shellExcludeRow { "
        "   background: transparent; "
        "   border: none; "
        "   border-radius: 5px; "
        "} "
        "QWidget#shellExcludeRow:hover { "
        "   background: rgba(238, 246, 255, 0.92); "
        "   border: none; "
        "} "
        "QWidget#shellExcludeRow[selected=\"true\"] { "
        "   background: rgba(0, 120, 212, 0.10); "
        "   border: none; "
        "} "
        "QLabel#shellExcludeRowIcon { "
        "   background: transparent; "
        "   border: none; "
        "} "
        "QLabel#shellExcludeBranchIcon { "
        "   background: transparent; "
        "   border: none; "
        "   color: #7a879b; "
        "   font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets'; "
        "   font-size: 11px; "
        "   font-weight: 700; "
        "} "
        "QLabel#shellExcludeRowTitle { font-size: 13px; font-weight: 600; color: #142033; } "
        "QLabel#shellExcludeRowSubtitle { font-size: 12px; font-weight: 600; color: #63708a; } "
        "QLabel#homeTileTitle { font-size: 15px; font-weight: 800; color: #101827; } "
        "QLabel#homeTileSubtitle { font-size: 13px; font-weight: 500; color: #56647d; } "
        "QLabel#pluginName { font-size: 19px; font-weight: 800; color: #101827; } "
        "QLabel#pluginDescription { font-size: 14px; font-weight: 500; color: #63708a; } "
        "QLabel#badgeLabel { font-size: 12px; font-weight: 700; } "
        "QLabel#learnMoreLabel { color: #0078d4; font-size: 13px; font-weight: 700; font-family: 'Segoe Fluent Icons', 'Segoe UI'; } "
        "QLabel#cardLabel { font-size: 13px; font-weight: 500; color: #1f2937; } "
        "QWidget#homeTile { "
        "   background: rgba(255, 255, 255, 0.86); "
        "   border: 1px solid rgba(203, 213, 229, 0.86); "
        "   border-radius: 10px; "
        "} "
        "QWidget#quickAction { "
        "   background: rgba(255, 255, 255, 0.52); "
        "   border: none; "
        "} "
        "QWidget#settingsPanel { "
        "   background: rgba(255, 255, 255, 0.94); "
        "   border: 1px solid rgba(203, 213, 229, 0.82); "
        "   border-radius: 13px; "
        "} "
        "QWidget#settingsInnerRow { "
        "   background: rgba(255, 255, 255, 0.62); "
        "   border: none; "
        "} "
        "QWidget#settingsRow { "
        "   background: rgba(255, 255, 255, 0.58); "
        "   border: none; "
        "} "
        "QFrame#softDivider { "
        "   background: rgba(203, 213, 229, 0.68); "
        "   border: none; "
        "} "
        "QFrame#verticalDivider { "
        "   background: rgba(203, 213, 229, 0.72); "
        "   border: none; "
        "   min-width: 1px; "
        "   max-width: 1px; "
        "} "
        "QLabel#pluginLargeIcon { "
        "   background: rgba(255, 255, 255, 0.92); "
        "   border: 1px solid #dbe4f1; "
        "   border-radius: 12px; "
        "   color: #0078d4; "
        "   min-width: 74px; "
        "   min-height: 74px; "
        "} "
        "QWidget#pluginFeatureBox { "
        "   background: rgba(255, 255, 255, 0.76); "
        "   border: 1px solid rgba(203, 213, 229, 0.72); "
        "   border-radius: 10px; "
        "} "
        "QWidget#pluginFeature { background: transparent; border: none; } "
        "QWidget#comingSoonPanel { "
        "   background: rgba(255, 255, 255, 0.44); "
        "   border: 1px dashed #c8d3e3; "
        "   border-radius: 12px; "
        "} "
        "QLabel#previewSurface { "
        "   background: #edf4ff; "
        "   border-radius: 10px; "
        "} "
        "QLineEdit#valueEdit {"
        "   background: #ffffff;"
        "   border: 1px solid #d2dbe8;"
        "   border-radius: 7px;"
        "   color: #28344a;"
        "   font-family: 'Consolas', 'Segoe UI Mono';"
        "   font-size: 13px;"
        "   font-weight: 700;"
        "   padding: 0 8px;"
        "} "
        "QPushButton#colorPickerButton {"
        "   color: white;"
        "   border-radius: 7px;"
        "   border: 2px solid rgba(255,255,255,0.96);"
        "   font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets';"
        "   font-size: 15px;"
        "   background: qconicalgradient(cx:0.5, cy:0.5, angle:90, stop:0 #ff005c, stop:0.16 #ff8a00, stop:0.32 #ffe500, stop:0.48 #00d86f, stop:0.64 #00a2ff, stop:0.82 #7c3cff, stop:1 #ff005c);"
        "} "
        "QPushButton#colorPickerButton:hover { border: 2px solid #0078d4; } "
        "QPushButton#pluginSettingsButton { "
        "   background: #ffffff; "
        "   border: 1px solid #cfd8e6; "
        "   border-radius: 8px; "
        "   color: #111827; "
        "   font-size: 13px; "
        "   font-weight: 700; "
        "} "
        "QPushButton#pluginSettingsButton:hover { background: #f5f9ff; border-color: #a9c7ef; } "
        "QTabWidget#linWidgetsTabs::pane { "
        "   border: none; "
        "   background: transparent; "
        "} "
        "QTabWidget#linWidgetsTabs QTabBar::tab { "
        "   height: 36px; "
        "   min-width: 88px; "
        "   padding: 0 18px; "
        "   margin-right: 8px; "
        "   background: rgba(255, 255, 255, 0.44); "
        "   border: 1px solid rgba(214, 223, 236, 0.64); "
        "   border-radius: 18px; "
        "   color: #66738c; "
        "   font-size: 13px; "
        "   font-weight: 800; "
        "} "
        "QTabWidget#linWidgetsTabs QTabBar::tab:hover { background: #f7fbff; color: #142033; } "
        "QTabWidget#linWidgetsTabs QTabBar::tab:selected { "
        "   background: #eaf3ff; "
        "   border-color: #cfe2ff; "
        "   color: #0078d4; "
        "} "
        "QListWidget#linWidgetSettingsList { "
        "   background: transparent; "
        "   border: none; "
        "   padding: 0; "
        "   outline: none; "
        "} "
        "QListWidget#linWidgetSettingsList::item { "
        "   background: transparent; "
        "   border: none; "
        "   padding: 0; "
        "   margin: 5px 0; "
        "} "
        "QListWidget#linWidgetSettingsList::item:hover { background: transparent; } "
        "QListWidget#linWidgetSettingsList::item:selected { "
        "   background: transparent; "
        "} "
        "QWidget#linWidgetStoreRow { "
        "   background: rgba(255, 255, 255, 0.92); "
        "   border: 1px solid rgba(225, 232, 242, 0.86); "
        "   border-radius: 14px; "
        "} "
        "QWidget#linWidgetStoreRow:hover { background: #f7fbff; border-color: #cfe2ff; } "
        "QPushButton#updatePrimaryButton { "
        "   background: #0078d4; "
        "   border: 1px solid #0078d4; "
        "   border-radius: 8px; "
        "   color: #ffffff; "
        "   font-size: 13px; "
        "   font-weight: 800; "
        "   padding: 0 16px; "
        "} "
        "QPushButton#updatePrimaryButton:hover { background: #0b86e8; border-color: #0b86e8; } "
        "QPushButton#updatePrimaryButton:disabled { background: #d5dde9; border-color: #d5dde9; color: #7d8798; } "
        "QProgressBar#updateProgressBar { "
        "   background: #e6edf6; "
        "   border: none; "
        "   border-radius: 5px; "
        "   height: 10px; "
        "   text-align: center; "
        "   color: transparent; "
        "} "
        "QProgressBar#updateProgressBar::chunk { "
        "   background: #0078d4; "
        "   border-radius: 5px; "
        "} "
        "QSlider::groove:horizontal { height: 5px; background: #d8dee8; border-radius: 3px; } "
        "QSlider::sub-page:horizontal { height: 5px; background: #0078d4; border-radius: 3px; } "
        "QSlider::handle:horizontal { background: #0078d4; border: 3px solid #ffffff; width: 15px; height: 15px; margin: -7px 0; border-radius: 9px; } "
        "QCheckBox::indicator { "
        "   width: 40px; "
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
