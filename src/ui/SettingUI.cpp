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
    setMinimumSize(820, 580);
    resize(960, 660);

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

    // Left Navigation Panel (Windows 11 WinUI 3 NavigationView)
    m_navPanel = new FluentNavigationPanel(container());
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
        button->setFixedSize(46, 32);
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

    if (m_navPanel && m_navPanel->currentIndex() != index) {
        m_navPanel->setCurrentIndex(index);
    }
    if (index == 4 && m_linWidgetsPage) {
        m_linWidgetsPage->reloadPage();
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
        page = wrapScrollablePage(pluginsPage);
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
    scroll->setStyleSheet("QScrollArea { background-color: #f3f3f3; border: none; }");

    if (scroll->viewport()) {
        scroll->viewport()->setAutoFillBackground(true);
        QPalette p = scroll->viewport()->palette();
        p.setColor(QPalette::Window, QColor(0xf3, 0xf3, 0xf3));
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

void SettingUI::restoreAndActivate()
{
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
    const bool minimizeToTray = settings.value("General/MinimizeToTray", true).toBool();
    if (forceQuit || !minimizeToTray) {
        if (QCoreApplication::instance()) {
            QCoreApplication::instance()->setProperty("LinDeskForceQuit", true);
        }
        event->accept();
        QWidget::closeEvent(event);
        if (!forceQuit) {
            QCoreApplication::quit();
        }
        return;
    }

    // 开启「最小化到托盘」时：关闭设置窗口仅隐藏窗口，保持 LinDesk 在系统托盘运行
    event->ignore();
    hide();
}

void SettingUI::changeEvent(QEvent* event)
{
    FluentBaseWidget::changeEvent(event);
    if (event) {
        if (event->type() == QEvent::WindowStateChange) {
            updateWindowControlButtons();
            setupStyles();
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
    const bool maximized = (windowState() & Qt::WindowMaximized);
    const QString containerBorder = maximized ? QStringLiteral("none") : QStringLiteral("1px solid #d6d6d6");
    const QString containerRadius = maximized ? QStringLiteral("0px") : QStringLiteral("8px");

    if (container()) {
        container()->setStyleSheet(QString(
            "QWidget#fluentBaseContainer {"
            "  background-color: #f3f3f3;"
            "  border: %1;"
            "  border-radius: %2;"
            "}").arg(containerBorder, containerRadius));
    }

    setStyleSheet(
        "SettingUI, QWidget#rightArea { "
        "   background-color: transparent; "
        "   border: none; "
        "   color: #1b1b1b; "
        "   font-family: '" + AppFonts::uiFamily() + "', 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI'; "
        "} "
        "QWidget#fluentBaseContainer { "
        "   background-color: #f3f3f3; "
        "   border: " + containerBorder + "; "
        "   border-radius: " + containerRadius + "; "
        "} "
        "SettingUI { "
        "   background-color: transparent; "
        "} "
        "QWidget#rightArea { "
        "   background: #f3f3f3; "
        "   border-top-right-radius: " + containerRadius + "; "
        "   border-bottom-right-radius: " + containerRadius + "; "
        "} "
        "QStackedWidget#contentStack { "
        "   background: #f3f3f3; "
        "   border: none; "
        "} "
        "QWidget#settingsWindowControls { "
        "   background: transparent; "
        "} "
        "QPushButton#settingsMinBtn, QPushButton#settingsMaxBtn, QPushButton#settingsCloseBtn { "
        "   background: transparent; border: none; font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets'; font-size: 10px; color: #1b1b1b; "
        "} "
        "QPushButton#settingsCloseBtn { border-top-right-radius: " + containerRadius + "; } "
        "QPushButton#settingsMinBtn:hover, QPushButton#settingsMaxBtn:hover { background: rgba(0, 0, 0, 0.06); } "
        "QPushButton#settingsCloseBtn:hover { background: #c42b1c; color: white; border-top-right-radius: " + containerRadius + "; } "
        "QScrollBar:vertical { "
        "   border: none; background: transparent; width: 6px; margin: 8px 3px 8px 0; "
        "} "
        "QScrollBar::handle:vertical { "
        "   background: rgba(0, 0, 0, 0.22); border-radius: 3px; min-height: 24px; "
        "} "
        "QScrollBar::handle:vertical:hover { background: rgba(0, 0, 0, 0.38); } "
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; } "
        "QLabel#pageTitle { font-size: 24px; font-weight: 600; color: #1b1b1b; margin-bottom: 0; letter-spacing: 0; } "
        "QLabel#pageSubtitle { font-size: 13px; font-weight: 400; color: #5c5c5c; margin-bottom: 0; } "
        "QLabel#cardTitle { font-size: 14px; font-weight: 600; color: #1b1b1b; } "
        "QLabel#cardSubtitle { font-size: 12px; font-weight: 400; color: #5c5c5c; } "
        "QLabel#rowTitle { font-size: 14px; font-weight: 500; color: #1b1b1b; } "
        "QLabel#rowSubtitle { font-size: 12px; font-weight: 400; color: #5c5c5c; } "
        "QLabel#valueLabel { color: #5c5c5c; font-size: 13px; font-weight: 600; } "
        "QFrame#shellExcludeBody { "
        "   background: #f9f9f9; "
        "   border: 1px solid #e5e5e5; "
        "   border-radius: 8px; "
        "} "
        "QWidget#shellExcludeTypePanel { background: transparent; border: none; } "
        "QWidget#shellExcludeDetail { background: transparent; border: none; } "
        "QWidget#shellExcludePane { background: transparent; border: none; } "
        "QWidget#shellExcludeTransferPane { background: transparent; border: none; } "
        "QWidget#shellExcludeActionPane { background: transparent; border: none; } "
        "QLabel#shellExcludeGroupTitle { "
        "   color: #5c5c5c; "
        "   font-size: 12px; "
        "   font-weight: 600; "
        "   padding: 2px 4px; "
        "} "
        "QListWidget#shellExcludeTypeList { "
        "   background: #ffffff; "
        "   border: 1px solid #e5e5e5; "
        "   border-radius: 6px; "
        "   padding: 4px; "
        "   outline: none; "
        "} "
        "QListWidget#shellExcludeTypeList::item { "
        "   min-height: 32px; "
        "   padding: 0 10px; "
        "   border-radius: 4px; "
        "   color: #1b1b1b; "
        "   font-size: 13px; "
        "   font-weight: 500; "
        "} "
        "QListWidget#shellExcludeTypeList::item:hover { background: rgba(0, 0, 0, 0.04); } "
        "QListWidget#shellExcludeTypeList::item:selected { "
        "   background: rgba(0, 0, 0, 0.06); "
        "   color: #0067c0; "
        "   font-weight: 600; "
        "} "
        "QListWidget#shellExcludeTreeList { "
        "   background: #ffffff; "
        "   border: 1px solid #e5e5e5; "
        "   border-radius: 6px; "
        "   padding: 4px; "
        "   outline: none; "
        "} "
        "QListWidget#shellExcludeTreeList::item { "
        "   background: transparent; "
        "   border: none; "
        "   padding: 0; "
        "} "
        "QListWidget#shellExcludeTreeList::item:selected { "
        "   background: rgba(0, 0, 0, 0.05); "
        "   border-radius: 4px; "
        "} "
        "QListWidget#shellExcludeTreeList::item:hover { "
        "   background: rgba(0, 0, 0, 0.03); "
        "   border-radius: 4px; "
        "} "
        "QLabel#shellExcludeEmptyLabel { "
        "   color: #5c5c5c; "
        "   font-size: 13px; "
        "   font-weight: 400; "
        "   padding: 28px; "
        "} "
        "QWidget#shellExcludeRow { "
        "   background: transparent; "
        "   border: none; "
        "   border-radius: 4px; "
        "} "
        "QWidget#shellExcludeRow:hover { "
        "   background: rgba(0, 0, 0, 0.03); "
        "   border: none; "
        "} "
        "QWidget#shellExcludeRow[selected=\"true\"] { "
        "   background: rgba(0, 0, 0, 0.05); "
        "   border: none; "
        "} "
        "QLabel#shellExcludeRowIcon { "
        "   background: transparent; "
        "   border: none; "
        "} "
        "QLabel#shellExcludeBranchIcon { "
        "   background: transparent; "
        "   border: none; "
        "   color: #5c5c5c; "
        "   font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets'; "
        "   font-size: 11px; "
        "} "
        "QLabel#shellExcludeRowTitle { font-size: 13px; font-weight: 500; color: #1b1b1b; } "
        "QLabel#shellExcludeRowSubtitle { font-size: 12px; font-weight: 400; color: #5c5c5c; } "
        "QLabel#homeTileTitle { font-size: 14px; font-weight: 600; color: #1b1b1b; } "
        "QLabel#homeTileSubtitle { font-size: 12px; font-weight: 400; color: #5c5c5c; } "
        "QLabel#pluginName { font-size: 15px; font-weight: 600; color: #1b1b1b; } "
        "QLabel#pluginDescription { font-size: 12px; font-weight: 400; color: #5c5c5c; } "
        "QLabel#badgeLabel { font-size: 12px; font-weight: 600; } "
        "QLabel#learnMoreLabel { color: #0067c0; font-size: 13px; font-weight: 600; font-family: 'Segoe Fluent Icons', 'Segoe UI'; } "
        "QLabel#cardLabel { font-size: 13px; font-weight: 500; color: #1b1b1b; } "
        "QWidget#homeTile { "
        "   background: #ffffff; "
        "   border: 1px solid #e5e5e5; "
        "   border-radius: 8px; "
        "} "
        "QWidget#homeTile:hover { "
        "   background: #fafafa; "
        "   border-color: #d6d6d6; "
        "} "
        "QWidget#quickAction { "
        "   background: transparent; "
        "   border: none; "
        "} "
        "QWidget#settingsPanel { "
        "   background: #ffffff; "
        "   border: 1px solid #e5e5e5; "
        "   border-radius: 8px; "
        "} "
        "QWidget#settingsInnerRow { "
        "   background: transparent; "
        "   border: none; "
        "} "
        "QWidget#settingsRow { "
        "   background: transparent; "
        "   border: none; "
        "} "
        "QFrame#softDivider { "
        "   background: #ebebeb; "
        "   border: none; "
        "} "
        "QFrame#verticalDivider { "
        "   background: #ebebeb; "
        "   border: none; "
        "   min-width: 1px; "
        "   max-width: 1px; "
        "} "
        "QLabel#pluginLargeIcon { "
        "   background: #eff6fc; "
        "   border: none; "
        "   border-radius: 8px; "
        "   color: #0067c0; "
        "   min-width: 44px; "
        "   min-height: 44px; "
        "} "
        "QWidget#pluginFeatureBox { "
        "   background: #f9f9f9; "
        "   border: 1px solid #e5e5e5; "
        "   border-radius: 8px; "
        "} "
        "QWidget#pluginFeature { background: transparent; border: none; } "
        "QWidget#comingSoonPanel { "
        "   background: #ffffff; "
        "   border: 1px solid #e5e5e5; "
        "   border-radius: 8px; "
        "} "
        "QLabel#previewSurface { "
        "   background: #f3f3f3; "
        "   border-radius: 8px; "
        "} "
        "QLineEdit#valueEdit {"
        "   background: #fbfbfb;"
        "   border: 1px solid #d6d6d6;"
        "   border-radius: 6px;"
        "   color: #1b1b1b;"
        "   font-family: 'Consolas', 'Segoe UI Mono';"
        "   font-size: 12px;"
        "   font-weight: 600;"
        "   padding: 0 8px;"
        "} "
        "QPushButton#colorPickerButton {"
        "   color: white;"
        "   border-radius: 6px;"
        "   border: 2px solid rgba(255,255,255,0.96);"
        "   font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets';"
        "   font-size: 14px;"
        "   background: qconicalgradient(cx:0.5, cy:0.5, angle:90, stop:0 #ff005c, stop:0.16 #ff8a00, stop:0.32 #ffe500, stop:0.48 #00d86f, stop:0.64 #00a2ff, stop:0.82 #7c3cff, stop:1 #ff005c);"
        "} "
        "QPushButton#colorPickerButton:hover { border: 2px solid #0067c0; } "
        "QPushButton#pluginSettingsButton { "
        "   background: #fbfbfb; "
        "   border: 1px solid #d6d6d6; "
        "   border-bottom: 1px solid #c2c2c2; "
        "   border-radius: 6px; "
        "   color: #1b1b1b; "
        "   font-size: 13px; "
        "   font-weight: 500; "
        "   padding: 0 14px; "
        "} "
        "QPushButton#pluginSettingsButton:hover { background: #f3f3f3; border-color: #c8c8c8; } "
        "QPushButton#pluginSettingsButton:pressed { background: #ebebeb; color: #5c5c5c; } "
        "QPushButton#updatePrimaryButton { "
        "   background: #0067c0; "
        "   border: 1px solid #005fb8; "
        "   border-radius: 6px; "
        "   color: #ffffff; "
        "   font-size: 13px; "
        "   font-weight: 600; "
        "   padding: 0 16px; "
        "} "
        "QPushButton#updatePrimaryButton:hover { background: #1975c5; border-color: #146bb8; } "
        "QPushButton#updatePrimaryButton:pressed { background: #005299; } "
        "QPushButton#updatePrimaryButton:disabled { background: #e5e5e5; border-color: #d6d6d6; color: #9e9e9e; } "
        "QProgressBar#updateProgressBar { "
        "   background: #e5e5e5; "
        "   border: none; "
        "   border-radius: 3px; "
        "   height: 6px; "
        "   text-align: center; "
        "   color: transparent; "
        "} "
        "QProgressBar#updateProgressBar::chunk { "
        "   background: #0067c0; "
        "   border-radius: 3px; "
        "} "
        "QSlider::groove:horizontal { height: 4px; background: #d6d6d6; border-radius: 2px; } "
        "QSlider::sub-page:horizontal { height: 4px; background: #0067c0; border-radius: 2px; } "
        "QSlider::handle:horizontal { background: #0067c0; border: 3px solid #ffffff; width: 14px; height: 14px; margin: -6px 0; border-radius: 8px; } "
    );
}
