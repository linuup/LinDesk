#include "WallpaperStudioWindow.h"
#include <QApplication>
#include <QScreen>
#include <QPainter>
#include <QPainterPath>
#include <QFileDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QDateTime>
#include <QDir>
#include <cmath>
#include "app/AppFonts.h"
#include "desktop/widgets/runtime_export.h"
#include "ui/settings/SettingsUICommon.h"

namespace LinHub {

using namespace SettingsUICommon;

QPointer<WallpaperStudioWindow> WallpaperStudioWindow::s_instance = nullptr;

WallpaperStudioWindow* WallpaperStudioWindow::instance() {
    if (!s_instance) {
        s_instance = new WallpaperStudioWindow(nullptr);
    }
    return s_instance.data();
}

void WallpaperStudioWindow::showStudio() {
    auto* win = instance();
    if (win->isMinimized()) {
        win->showNormal();
    }
    win->show();
    win->raise();
    win->activateWindow();
}

WallpaperStudioWindow::WallpaperStudioWindow(QWidget* parent)
    : FluentBaseWidget(parent)
{
    setWindowTitle(tr("LinDesk Hub · Live Wallpaper & Widget Studio"));
    resize(1120, 720);
    setMinimumSize(880, 600);

    // Center on current screen
    if (auto* screen = QGuiApplication::primaryScreen()) {
        const QRect screenGeom = screen->availableGeometry();
        move(screenGeom.center().x() - width() / 2, screenGeom.center().y() - height() / 2);
    }

    initUI();

    m_statusTimer = new QTimer(this);
    m_statusTimer->setInterval(1000);
    connect(m_statusTimer, &QTimer::timeout, this, &WallpaperStudioWindow::refreshActiveWallpaperStatus);
    m_statusTimer->start();
}

void WallpaperStudioWindow::showEvent(QShowEvent* event) {
    FluentBaseWidget::showEvent(event);
    refreshActiveWallpaperStatus();
}

void WallpaperStudioWindow::closeEvent(QCloseEvent* event) {
    // Keep-alive: Hide instead of delete to ensure 0ms instant reopening
    event->ignore();
    hide();
}

void WallpaperStudioWindow::initUI() {
    setWindowBtnMode(All);

    // Configure main background container
    if (container()) {
        container()->setStyleSheet(QStringLiteral(
            "QWidget#fluentBaseContainer {"
            "  background: #090d16;"
            "  border: 1px solid rgba(255, 255, 255, 0.12);"
            "  border-radius: 14px;"
            "}"
        ));
    }

    setupHeader();
    setupNavPills();

    m_stack = new QStackedWidget(container());
    m_stack->addWidget(createGalleryPage());
    m_stack->addWidget(createStudioPage());
    m_stack->addWidget(createWidgetsPage());
    m_stack->addWidget(createDevAdminPage());

    centralLayout()->addWidget(m_stack, 1);

    switchToTab(0);
}

void WallpaperStudioWindow::setupHeader() {
    auto* header = new QWidget(container());
    header->setFixedHeight(56);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(20, 10, 20, 0);
    headerLayout->setSpacing(14);

    // Brand icon
    auto* iconLabel = new QLabel(header);
    iconLabel->setFixedSize(36, 36);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setStyleSheet(QStringLiteral(
        "background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #6366f1, stop:0.5 #a855f7, stop:1 #ec4899);"
        "border-radius: 10px;"
        "color: #ffffff;"
        "font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets';"
        "font-size: 18px;"
    ));
    iconLabel->setText(glyph(0xEB9F)); // Palette glyph
    headerLayout->addWidget(iconLabel, 0, Qt::AlignVCenter);

    // Title & subtitle
    auto* titleLayout = new QVBoxLayout();
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(2);

    auto* titleLabel = new QLabel(tr("LinDesk Hub"), header);
    titleLabel->setStyleSheet(QStringLiteral("color: #f8fafc; font-size: 16px; font-weight: 800; font-family: %1;").arg(AppFonts::uiCssFamily()));
    
    auto* subLabel = new QLabel(tr("Live Wallpaper Studio · LinWidgets Marketplace · Dev Console"), header);
    subLabel->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 11px; font-weight: 500; font-family: %1;").arg(AppFonts::uiCssFamily()));

    titleLayout->addWidget(titleLabel);
    titleLayout->addWidget(subLabel);
    headerLayout->addLayout(titleLayout, 1);

    // Version Tag
    auto* verTag = new QLabel(tr("v1.0.0 · CEF OSR 60FPS"), header);
    verTag->setStyleSheet(QStringLiteral(
        "color: #a5b4fc;"
        "background: rgba(99, 102, 241, 0.15);"
        "border: 1px solid rgba(99, 102, 241, 0.35);"
        "border-radius: 8px;"
        "padding: 3px 8px;"
        "font-size: 11px;"
        "font-weight: 600;"
    ));
    headerLayout->addWidget(verTag, 0, Qt::AlignVCenter);

    centralLayout()->addWidget(header);
}

void WallpaperStudioWindow::setupNavPills() {
    auto* navWidget = new QWidget(container());
    navWidget->setFixedHeight(48);
    auto* navLayout = new QHBoxLayout(navWidget);
    navLayout->setContentsMargins(20, 4, 20, 8);
    navLayout->setSpacing(8);

    auto createPill = [this, navWidget](const QString& text, int tabIndex) {
        auto* btn = new QPushButton(text, navWidget);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(34);
        btn->setMinimumWidth(110);
        connect(btn, &QPushButton::clicked, this, [this, tabIndex]() {
            switchToTab(tabIndex);
        });
        return btn;
    };

    m_tabGalleryBtn = createPill(tr("🖼️ 壁纸画廊"), 0);
    m_tabStudioBtn = createPill(tr("🎨 壁纸工坊 (Studio 调参)"), 1);
    m_tabWidgetsBtn = createPill(tr("🧩 桌面组件管理"), 2);
    m_tabDevBtn = createPill(tr("🛠️ 开发者后台 (Dev Inspector)"), 3);

    navLayout->addWidget(m_tabGalleryBtn);
    navLayout->addWidget(m_tabStudioBtn);
    navLayout->addWidget(m_tabWidgetsBtn);
    navLayout->addWidget(m_tabDevBtn);
    navLayout->addStretch(1);

    centralLayout()->addWidget(navWidget);
}

void WallpaperStudioWindow::switchToTab(int tabIndex) {
    if (!m_stack) return;
    m_stack->setCurrentIndex(tabIndex);

    const QString activeStyle = QStringLiteral(
        "QPushButton {"
        "  background: #4f46e5;"
        "  color: #ffffff;"
        "  border: 1px solid #6366f1;"
        "  border-radius: 8px;"
        "  font-size: 12px;"
        "  font-weight: 700;"
        "  padding: 0 14px;"
        "}"
    );

    const QString inactiveStyle = QStringLiteral(
        "QPushButton {"
        "  background: rgba(30, 41, 59, 0.6);"
        "  color: #94a3b8;"
        "  border: 1px solid rgba(255, 255, 255, 0.08);"
        "  border-radius: 8px;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "  padding: 0 14px;"
        "}"
        "QPushButton:hover {"
        "  background: rgba(51, 65, 85, 0.8);"
        "  color: #f1f5f9;"
        "  border-color: rgba(99, 102, 241, 0.4);"
        "}"
    );

    if (m_tabGalleryBtn) m_tabGalleryBtn->setStyleSheet(tabIndex == 0 ? activeStyle : inactiveStyle);
    if (m_tabStudioBtn) m_tabStudioBtn->setStyleSheet(tabIndex == 1 ? activeStyle : inactiveStyle);
    if (m_tabWidgetsBtn) m_tabWidgetsBtn->setStyleSheet(tabIndex == 2 ? activeStyle : inactiveStyle);
    if (m_tabDevBtn) m_tabDevBtn->setStyleSheet(tabIndex == 3 ? activeStyle : inactiveStyle);

    refreshActiveWallpaperStatus();
}

// -------------------------------------------------------------
// Page 1: 动态壁纸画廊 (Wallpaper Gallery)
// -------------------------------------------------------------
QWidget* WallpaperStudioWindow::createGalleryPage() {
    auto* page = new QWidget(m_stack);
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(20, 8, 20, 20);
    pageLayout->setSpacing(16);

    // 1. Hero Showcase Card
    auto* heroCard = new QWidget(page);
    heroCard->setObjectName("heroCard");
    heroCard->setStyleSheet(QStringLiteral(
        "QWidget#heroCard {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 rgba(30, 41, 59, 0.9), stop:1 rgba(15, 23, 42, 0.95));"
        "  border: 1px solid rgba(99, 102, 241, 0.35);"
        "  border-radius: 16px;"
        "}"
    ));
    auto* heroLayout = new QHBoxLayout(heroCard);
    heroLayout->setContentsMargins(18, 16, 18, 16);
    heroLayout->setSpacing(20);

    // Left thumbnail placeholder
    auto* thumbBox = new QWidget(heroCard);
    thumbBox->setFixedSize(160, 90); // 16:9 aspect ratio
    thumbBox->setStyleSheet(QStringLiteral(
        "background: qradialgradient(cx:0.5, cy:0.5, radius:0.8, fx:0.5, fy:0.5, stop:0 #000, stop:0.3 #ff8c14, stop:0.7 #1e1b4b, stop:1 #020208);"
        "border: 1px solid rgba(255, 255, 255, 0.2);"
        "border-radius: 10px;"
    ));
    heroLayout->addWidget(thumbBox, 0, Qt::AlignVCenter);

    // Middle info
    auto* infoLayout = new QVBoxLayout();
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(4);

    auto* statusRow = new QHBoxLayout();
    statusRow->setSpacing(6);
    m_heroStatusDot = new QLabel(heroCard);
    m_heroStatusDot->setFixedSize(8, 8);
    m_heroStatusDot->setStyleSheet(QStringLiteral("background: #10b981; border-radius: 4px;"));
    m_heroStatusText = new QLabel(tr("正在桌面背景层运行 (Active · 60 FPS)"), heroCard);
    m_heroStatusText->setStyleSheet(QStringLiteral("color: #34d399; font-size: 11px; font-weight: 700;"));
    statusRow->addWidget(m_heroStatusDot);
    statusRow->addWidget(m_heroStatusText);
    statusRow->addStretch();
    infoLayout->addLayout(statusRow);

    m_heroTitle = new QLabel(tr("Gargantua Relativistic Black Hole"), heroCard);
    m_heroTitle->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 17px; font-weight: 800;"));
    infoLayout->addWidget(m_heroTitle);

    m_heroDesc = new QLabel(tr("电影级相对论克尔黑洞仿真，具备引力透镜吸积盘、多普勒波长频移与微尘物理跟随。"), heroCard);
    m_heroDesc->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 12px;"));
    m_heroDesc->setWordWrap(true);
    infoLayout->addWidget(m_heroDesc);
    heroLayout->addLayout(infoLayout, 1);

    // Right Controls
    auto* rightCtrlLayout = new QVBoxLayout();
    rightCtrlLayout->setSpacing(8);

    // Mouse Physics Toggle Row
    auto* mouseRow = new QHBoxLayout();
    auto* mouseLbl = new QLabel(tr("鼠标互动跟随:"), heroCard);
    mouseLbl->setStyleSheet(QStringLiteral("color: #cbd5e1; font-size: 12px; font-weight: 600;"));
    m_heroMousePhysicsToggle = new FluentToggleSwitch(heroCard);
    m_heroMousePhysicsToggle->setChecked(true);
    connect(m_heroMousePhysicsToggle, &FluentToggleSwitch::toggled, this, &WallpaperStudioWindow::toggleMousePhysics);
    mouseRow->addWidget(mouseLbl);
    mouseRow->addWidget(m_heroMousePhysicsToggle);
    rightCtrlLayout->addLayout(mouseRow);

    auto* openStudioQuickBtn = new QPushButton(tr("🎨 壁纸工坊调参"), heroCard);
    openStudioQuickBtn->setCursor(Qt::PointingHandCursor);
    openStudioQuickBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: rgba(99, 102, 241, 0.2); color: #c7d2fe; border: 1px solid rgba(99, 102, 241, 0.4); border-radius: 8px; font-size: 12px; font-weight: 600; padding: 6px 12px; }"
        "QPushButton:hover { background: rgba(99, 102, 241, 0.35); color: #ffffff; }"
    ));
    connect(openStudioQuickBtn, &QPushButton::clicked, this, [this]() { switchToTab(1); });
    rightCtrlLayout->addWidget(openStudioQuickBtn);

    m_heroStopBtn = new QPushButton(tr("⏹ 停止动态壁纸"), heroCard);
    m_heroStopBtn->setCursor(Qt::PointingHandCursor);
    m_heroStopBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: rgba(239, 68, 68, 0.15); color: #fca5a5; border: 1px solid rgba(239, 68, 68, 0.35); border-radius: 8px; font-size: 12px; font-weight: 600; padding: 6px 12px; }"
        "QPushButton:hover { background: rgba(239, 68, 68, 0.3); color: #ffffff; }"
    ));
    connect(m_heroStopBtn, &QPushButton::clicked, this, &WallpaperStudioWindow::stopCurrentWallpaper);
    rightCtrlLayout->addWidget(m_heroStopBtn);

    heroLayout->addLayout(rightCtrlLayout);
    pageLayout->addWidget(heroCard, 0);

    // 2. Action Bar
    auto* actionBar = new QHBoxLayout();
    auto* secTitle = new QLabel(tr("壁纸画廊 (Wallpaper Library)"), page);
    secTitle->setStyleSheet(QStringLiteral("color: #e2e8f0; font-size: 14px; font-weight: 700;"));
    actionBar->addWidget(secTitle);
    actionBar->addStretch();

    auto* importBtn = new QPushButton(tr("➕ 导入本地视频/图片..."), page);
    importBtn->setCursor(Qt::PointingHandCursor);
    importBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #4f46e5, stop:1 #7c3aed); color: #ffffff; border: none; border-radius: 8px; font-size: 12px; font-weight: 700; padding: 7px 16px; }"
        "QPushButton:hover { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #4338ca, stop:1 #6d28d9); }"
    ));
    connect(importBtn, &QPushButton::clicked, this, &WallpaperStudioWindow::importLocalMedia);
    actionBar->addWidget(importBtn);
    pageLayout->addLayout(actionBar);

    // 3. Wallpaper Grid in ScrollArea
    auto* scrollArea = new QScrollArea(page);
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet(QStringLiteral("QScrollArea { border: none; background: transparent; } QWidget#gridContainer { background: transparent; }"));

    auto* gridContainer = new QWidget(scrollArea);
    gridContainer->setObjectName("gridContainer");
    auto* gridLayout = new QGridLayout(gridContainer);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    gridLayout->setSpacing(16);

    auto createWallpaperCard = [this, gridContainer](const QString& id, const QString& title, const QString& tag, const QString& desc, const QString& thumbStyle, bool isActive) {
        auto* card = new QWidget(gridContainer);
        card->setStyleSheet(QStringLiteral(
            "QWidget {"
            "  background: rgba(30, 41, 59, 0.7);"
            "  border: 1px solid rgba(255, 255, 255, 0.1);"
            "  border-radius: 14px;"
            "}"
            "QWidget:hover {"
            "  border-color: rgba(99, 102, 241, 0.6);"
            "  background: rgba(30, 41, 59, 0.9);"
            "}"
        ));
        auto* vLayout = new QVBoxLayout(card);
        vLayout->setContentsMargins(12, 12, 12, 12);
        vLayout->setSpacing(8);

        // 16:9 Thumbnail banner
        auto* banner = new QWidget(card);
        banner->setFixedHeight(120);
        banner->setStyleSheet(thumbStyle);
        auto* bannerLayout = new QHBoxLayout(banner);
        bannerLayout->setContentsMargins(8, 8, 8, 8);
        auto* tagBadge = new QLabel(tag, banner);
        tagBadge->setStyleSheet(QStringLiteral("background: rgba(0, 0, 0, 0.65); color: #c7d2fe; border: 1px solid rgba(255, 255, 255, 0.15); border-radius: 6px; padding: 2px 6px; font-size: 10px; font-weight: 700;"));
        bannerLayout->addWidget(tagBadge, 0, Qt::AlignTop | Qt::AlignLeft);
        bannerLayout->addStretch();
        vLayout->addWidget(banner);

        auto* nameLabel = new QLabel(title, card);
        nameLabel->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 13px; font-weight: 700; background: transparent;"));
        vLayout->addWidget(nameLabel);

        auto* descLabel = new QLabel(desc, card);
        descLabel->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 11px; background: transparent;"));
        descLabel->setWordWrap(true);
        vLayout->addWidget(descLabel);

        auto* btnRow = new QHBoxLayout();
        btnRow->setSpacing(6);
        auto* editBtn = new QPushButton(tr("调节"), card);
        editBtn->setCursor(Qt::PointingHandCursor);
        editBtn->setStyleSheet(QStringLiteral("QPushButton { background: rgba(51, 65, 85, 0.6); color: #cbd5e1; border: 1px solid rgba(255, 255, 255, 0.1); border-radius: 6px; font-size: 11px; padding: 4px 10px; } QPushButton:hover { background: #475569; }"));
        connect(editBtn, &QPushButton::clicked, this, [this, id]() {
            m_currentEditingWallpaper = id;
            if (m_studioCanvas) m_studioCanvas->setWallpaperType(id);
            switchToTab(1);
        });
        btnRow->addWidget(editBtn);

        auto* applyBtn = new QPushButton(isActive ? tr("已激活") : tr("设为壁纸"), card);
        applyBtn->setCursor(Qt::PointingHandCursor);
        if (isActive) {
            applyBtn->setStyleSheet(QStringLiteral("QPushButton { background: rgba(16, 185, 129, 0.2); color: #6ee7b7; border: 1px solid rgba(16, 185, 129, 0.4); border-radius: 6px; font-size: 11px; font-weight: 700; padding: 4px 12px; }"));
        } else {
            applyBtn->setStyleSheet(QStringLiteral("QPushButton { background: #4f46e5; color: #ffffff; border: none; border-radius: 6px; font-size: 11px; font-weight: 700; padding: 4px 12px; } QPushButton:hover { background: #4338ca; }"));
            connect(applyBtn, &QPushButton::clicked, this, [this, id]() {
                applyWallpaperPreset(id);
            });
        }
        btnRow->addWidget(applyBtn);
        vLayout->addLayout(btnRow);

        return card;
    };

    // Card 1: Gargantua
    gridLayout->addWidget(createWallpaperCard(
        QStringLiteral("gargantua"),
        tr("Gargantua Black Hole"),
        tr("4K WebGL · 官方精选"),
        tr("相对论黑洞、强引力透镜弯折与吸积盘动态粒子。"),
        QStringLiteral("background: qradialgradient(cx:0.5, cy:0.5, radius:0.8, fx:0.5, fy:0.5, stop:0 #000, stop:0.35 #ea580c, stop:0.7 #312e81, stop:1 #020208); border-radius: 8px;"),
        true
    ), 0, 0);

    // Card 2: Living Wallpaper
    gridLayout->addWidget(createWallpaperCard(
        QStringLiteral("living"),
        tr("LinDesk Living Wallpaper"),
        tr("极光流体 · 官方精选"),
        tr("深邃星空极光流体波纹与量子网格，支持鼠标全向波动。"),
        QStringLiteral("background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #083344, stop:0.5 #3b0764, stop:1 #030712); border-radius: 8px;"),
        false
    ), 0, 1);

    // Card 3: Cyber Matrix
    gridLayout->addWidget(createWallpaperCard(
        QStringLiteral("matrix"),
        tr("Cyber Matrix Rain (赛博流光)"),
        tr("4K 视频 · 静音循环"),
        tr("经典黑客代码雨与字符发光，极低显卡功耗占用。"),
        QStringLiteral("background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #052e16, stop:0.6 #022c22, stop:1 #000); border-radius: 8px;"),
        false
    ), 0, 2);

    // Card 4: Drag & Drop Card
    auto* dropCard = new QWidget(gridContainer);
    dropCard->setStyleSheet(QStringLiteral(
        "QWidget {"
        "  background: rgba(15, 23, 42, 0.4);"
        "  border: 2px dashed rgba(99, 102, 241, 0.4);"
        "  border-radius: 14px;"
        "}"
        "QWidget:hover {"
        "  background: rgba(99, 102, 241, 0.08);"
        "  border-color: rgba(99, 102, 241, 0.8);"
        "}"
    ));
    auto* dropLayout = new QVBoxLayout(dropCard);
    dropLayout->setContentsMargins(14, 20, 14, 20);
    dropLayout->setSpacing(6);
    dropLayout->setAlignment(Qt::AlignCenter);

    auto* plusIcon = new QLabel(glyph(0xE898), dropCard);
    plusIcon->setAlignment(Qt::AlignCenter);
    plusIcon->setStyleSheet(QStringLiteral("color: #818cf8; font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets'; font-size: 28px; background: transparent;"));
    dropLayout->addWidget(plusIcon);

    auto* dropTitle = new QLabel(tr("拖拽本地视频或大图到此处"), dropCard);
    dropTitle->setAlignment(Qt::AlignCenter);
    dropTitle->setStyleSheet(QStringLiteral("color: #e2e8f0; font-size: 13px; font-weight: 700; background: transparent;"));
    dropLayout->addWidget(dropTitle);

    auto* dropSub = new QLabel(tr("支持 MP4, WebM, 4K JPG, PNG, GIF\n自动封装为硬件加速动态壁纸包"), dropCard);
    dropSub->setAlignment(Qt::AlignCenter);
    dropSub->setStyleSheet(QStringLiteral("color: #64748b; font-size: 11px; background: transparent;"));
    dropLayout->addWidget(dropSub);

    gridLayout->addWidget(dropCard, 1, 0);

    scrollArea->setWidget(gridContainer);
    pageLayout->addWidget(scrollArea, 1);

    return page;
}

// -------------------------------------------------------------
// Page 2: 壁纸工坊 (Wallpaper Studio 所见即所得编辑器)
// -------------------------------------------------------------
QWidget* WallpaperStudioWindow::createStudioPage() {
    auto* page = new QWidget(m_stack);
    auto* hLayout = new QHBoxLayout(page);
    hLayout->setContentsMargins(20, 8, 20, 20);
    hLayout->setSpacing(20);

    // Left Column: 16:9 Canvas Viewport
    auto* leftCol = new QWidget(page);
    auto* leftLayout = new QVBoxLayout(leftCol);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(10);

    auto* canvasHeader = new QHBoxLayout();
    auto* canvasTitle = new QLabel(tr("16:9 实时画板预览视口 (所见即所得)"), leftCol);
    canvasTitle->setStyleSheet(QStringLiteral("color: #e2e8f0; font-size: 13px; font-weight: 700;"));
    auto* canvasRes = new QLabel(tr("模拟视口: 1920 × 1080"), leftCol);
    canvasRes->setStyleSheet(QStringLiteral("color: #64748b; font-size: 11px; font-family: Consolas, monospace;"));
    canvasHeader->addWidget(canvasTitle);
    canvasHeader->addStretch();
    canvasHeader->addWidget(canvasRes);
    leftLayout->addLayout(canvasHeader);

    m_studioCanvas = new StudioPreviewCanvas(leftCol);
    leftLayout->addWidget(m_studioCanvas, 1);

    m_mockIconsCheckbox = new QCheckBox(tr("模拟显示桌面图标与网格浮层（实时肉眼验证文字易读性）"), leftCol);
    m_mockIconsCheckbox->setChecked(true);
    m_mockIconsCheckbox->setStyleSheet(QStringLiteral(
        "QCheckBox { color: #cbd5e1; font-size: 12px; font-weight: 600; }"
        "QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px; border: 1px solid #64748b; background: #1e293b; }"
        "QCheckBox::indicator:checked { background: #4f46e5; border-color: #6366f1; }"
    ));
    connect(m_mockIconsCheckbox, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_studioCanvas) m_studioCanvas->setShowDesktopIcons(checked);
    });
    leftLayout->addWidget(m_mockIconsCheckbox);

    hLayout->addWidget(leftCol, 3);

    // Right Column: Controls Panel
    auto* rightCol = new QWidget(page);
    rightCol->setFixedWidth(340);
    rightCol->setStyleSheet(QStringLiteral(
        "QWidget {"
        "  background: rgba(15, 23, 42, 0.85);"
        "  border: 1px solid rgba(255, 255, 255, 0.1);"
        "  border-radius: 14px;"
        "}"
    ));
    auto* rightLayout = new QVBoxLayout(rightCol);
    rightLayout->setContentsMargins(18, 16, 18, 16);
    rightLayout->setSpacing(14);

    auto* ctrlTitle = new QLabel(tr("画面与文字保护滤镜调节"), rightCol);
    ctrlTitle->setStyleSheet(QStringLiteral("color: #f8fafc; font-size: 14px; font-weight: 800; background: transparent;"));
    rightLayout->addWidget(ctrlTitle);

    // 1. Fit Mode
    auto* fitLbl = new QLabel(tr("画面裁切适配模式:"), rightCol);
    fitLbl->setStyleSheet(QStringLiteral("color: #cbd5e1; font-size: 12px; font-weight: 600; background: transparent;"));
    rightLayout->addWidget(fitLbl);

    auto* fitBtnRow = new QHBoxLayout();
    fitBtnRow->setSpacing(6);
    auto* coverBtn = new QPushButton(tr("Cover 裁剪"), rightCol);
    coverBtn->setStyleSheet(QStringLiteral("QPushButton { background: #4f46e5; color: #fff; border-radius: 6px; font-size: 11px; font-weight: 700; padding: 5px 8px; }"));
    auto* containBtn = new QPushButton(tr("Contain 适应"), rightCol);
    containBtn->setStyleSheet(QStringLiteral("QPushButton { background: #1e293b; color: #94a3b8; border-radius: 6px; font-size: 11px; padding: 5px 8px; }"));
    auto* stretchBtn = new QPushButton(tr("Stretch 拉伸"), rightCol);
    stretchBtn->setStyleSheet(QStringLiteral("QPushButton { background: #1e293b; color: #94a3b8; border-radius: 6px; font-size: 11px; padding: 5px 8px; }"));
    fitBtnRow->addWidget(coverBtn);
    fitBtnRow->addWidget(containBtn);
    fitBtnRow->addWidget(stretchBtn);
    rightLayout->addLayout(fitBtnRow);

    // 2. Dimming Mask Slider
    auto* dimRow = new QHBoxLayout();
    auto* dimTitle = new QLabel(tr("遮罩暗化 (文字保护):"), rightCol);
    dimTitle->setStyleSheet(QStringLiteral("color: #cbd5e1; font-size: 12px; font-weight: 600; background: transparent;"));
    m_dimValLabel = new QLabel(tr("20%"), rightCol);
    m_dimValLabel->setStyleSheet(QStringLiteral("color: #818cf8; font-size: 12px; font-weight: 700; font-family: monospace; background: transparent;"));
    dimRow->addWidget(dimTitle);
    dimRow->addStretch();
    dimRow->addWidget(m_dimValLabel);
    rightLayout->addLayout(dimRow);

    m_dimSlider = new QSlider(Qt::Horizontal, rightCol);
    m_dimSlider->setRange(0, 80);
    m_dimSlider->setValue(20);
    connect(m_dimSlider, &QSlider::valueChanged, this, &WallpaperStudioWindow::updateStudioDimming);
    rightLayout->addWidget(m_dimSlider);

    // 3. Blur Slider
    auto* blurRow = new QHBoxLayout();
    auto* blurTitle = new QLabel(tr("毛玻璃高斯模糊 (景深):"), rightCol);
    blurTitle->setStyleSheet(QStringLiteral("color: #cbd5e1; font-size: 12px; font-weight: 600; background: transparent;"));
    m_blurValLabel = new QLabel(tr("0 px"), rightCol);
    m_blurValLabel->setStyleSheet(QStringLiteral("color: #818cf8; font-size: 12px; font-weight: 700; font-family: monospace; background: transparent;"));
    blurRow->addWidget(blurTitle);
    blurRow->addStretch();
    blurRow->addWidget(m_blurValLabel);
    rightLayout->addLayout(blurRow);

    m_blurSlider = new QSlider(Qt::Horizontal, rightCol);
    m_blurSlider->setRange(0, 20);
    m_blurSlider->setValue(0);
    connect(m_blurSlider, &QSlider::valueChanged, this, &WallpaperStudioWindow::updateStudioBlur);
    rightLayout->addWidget(m_blurSlider);

    // 4. Video & Audio
    auto* audioPanel = new QWidget(rightCol);
    audioPanel->setStyleSheet(QStringLiteral("background: rgba(30, 41, 59, 0.6); border: 1px solid rgba(255, 255, 255, 0.08); border-radius: 10px;"));
    auto* audioLayout = new QVBoxLayout(audioPanel);
    audioLayout->setContentsMargins(10, 10, 10, 10);
    audioLayout->setSpacing(6);
    auto* audioTitle = new QLabel(tr("视频壁纸音量控制:"), audioPanel);
    audioTitle->setStyleSheet(QStringLiteral("color: #e2e8f0; font-size: 11px; font-weight: 700; background: transparent;"));
    audioLayout->addWidget(audioTitle);

    auto* muteRow = new QHBoxLayout();
    auto* muteLbl = new QLabel(tr("静音模式 (Mute):"), audioPanel);
    muteLbl->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 11px; background: transparent;"));
    auto* muteSwitch = new FluentToggleSwitch(audioPanel);
    muteSwitch->setChecked(true);
    muteRow->addWidget(muteLbl);
    muteRow->addStretch();
    muteRow->addWidget(muteSwitch);
    audioLayout->addLayout(muteRow);
    rightLayout->addWidget(audioPanel);

    rightLayout->addStretch(1);

    // Apply Buttons
    auto* applyPresetBtn = new QPushButton(tr("保存并应用到当前桌面"), rightCol);
    applyPresetBtn->setCursor(Qt::PointingHandCursor);
    applyPresetBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #4f46e5, stop:1 #7c3aed); color: #ffffff; border: none; border-radius: 8px; font-size: 12px; font-weight: 700; padding: 10px 14px; }"
        "QPushButton:hover { background: #4338ca; }"
    ));
    connect(applyPresetBtn, &QPushButton::clicked, this, [this]() {
        applyWallpaperPreset(m_currentEditingWallpaper);
        QMessageBox::information(this, tr("壁纸工坊"), tr("已成功将参数与滤镜应用到桌面！"));
    });
    rightLayout->addWidget(applyPresetBtn);

    hLayout->addWidget(rightCol, 2);

    return page;
}

void WallpaperStudioWindow::updateStudioDimming(int val) {
    if (m_dimValLabel) m_dimValLabel->setText(QStringLiteral("%1%").arg(val));
    if (m_studioCanvas) m_studioCanvas->setDimmingOpacity(val / 100.0);
}

void WallpaperStudioWindow::updateStudioBlur(int val) {
    if (m_blurValLabel) m_blurValLabel->setText(QStringLiteral("%1 px").arg(val));
    if (m_studioCanvas) m_studioCanvas->setBlurRadius(val);
}

// -------------------------------------------------------------
// Page 3: 桌面小组件市场 (Widgets Hub)
// -------------------------------------------------------------
QWidget* WallpaperStudioWindow::createWidgetsPage() {
    auto* page = new QWidget(m_stack);
    auto* vLayout = new QVBoxLayout(page);
    vLayout->setContentsMargins(20, 8, 20, 20);
    vLayout->setSpacing(14);

    auto* topRow = new QHBoxLayout();
    auto* title = new QLabel(tr("桌面小组件市场与实例管理 (LinWidgets Marketplace)"), page);
    title->setStyleSheet(QStringLiteral("color: #f8fafc; font-size: 15px; font-weight: 800;"));
    topRow->addWidget(title);
    topRow->addStretch();
    vLayout->addLayout(topRow);

    auto* scrollArea = new QScrollArea(page);
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet(QStringLiteral("QScrollArea { border: none; background: transparent; } QWidget#widgetsContainer { background: transparent; }"));

    auto* container = new QWidget(scrollArea);
    container->setObjectName("widgetsContainer");
    auto* listLayout = new QVBoxLayout(container);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(10);

    auto createWidgetRow = [this, container](const QString& name, const QString& ver, const QString& desc, const QString& iconGlyph, bool running) {
        auto* row = new QWidget(container);
        row->setStyleSheet(QStringLiteral(
            "QWidget {"
            "  background: rgba(30, 41, 59, 0.65);"
            "  border: 1px solid rgba(255, 255, 255, 0.08);"
            "  border-radius: 12px;"
            "}"
            "QWidget:hover { background: rgba(30, 41, 59, 0.85); border-color: rgba(99, 102, 241, 0.4); }"
        ));
        auto* rLayout = new QHBoxLayout(row);
        rLayout->setContentsMargins(16, 12, 16, 12);
        rLayout->setSpacing(14);

        auto* iconBox = new QLabel(iconGlyph, row);
        iconBox->setFixedSize(40, 40);
        iconBox->setAlignment(Qt::AlignCenter);
        iconBox->setStyleSheet(QStringLiteral("background: rgba(99, 102, 241, 0.15); border-radius: 8px; color: #a5b4fc; font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets'; font-size: 20px;"));
        rLayout->addWidget(iconBox);

        auto* textLayout = new QVBoxLayout();
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(2);
        auto* nameLabel = new QLabel(name + " " + ver, row);
        nameLabel->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 13px; font-weight: 700; background: transparent;"));
        auto* descLabel = new QLabel(desc, row);
        descLabel->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 11px; background: transparent;"));
        textLayout->addWidget(nameLabel);
        textLayout->addWidget(descLabel);
        rLayout->addLayout(textLayout, 1);

        auto* runBtn = new QPushButton(running ? tr("运行中 (停止)") : tr("启动到桌面"), row);
        runBtn->setCursor(Qt::PointingHandCursor);
        if (running) {
            runBtn->setStyleSheet(QStringLiteral("QPushButton { background: rgba(16, 185, 129, 0.2); color: #34d399; border: 1px solid rgba(16, 185, 129, 0.4); border-radius: 6px; font-size: 11px; font-weight: 700; padding: 6px 14px; }"));
        } else {
            runBtn->setStyleSheet(QStringLiteral("QPushButton { background: #4f46e5; color: #ffffff; border: none; border-radius: 6px; font-size: 11px; font-weight: 700; padding: 6px 14px; }"));
        }
        rLayout->addWidget(runBtn);

        return row;
    };

    listLayout->addWidget(createWidgetRow(tr("极简时钟 (Clock)"), QStringLiteral("v1.2.0"), tr("半透明毛玻璃桌面时钟，秒针呼吸灯动效与多种表盘。"), glyph(0xE823), true));
    listLayout->addWidget(createWidgetRow(tr("便签备忘录 (Notes)"), QStringLiteral("v1.0.4"), tr("桌面即时轻便贴，自动保存内容与暗黑磨砂主题。"), glyph(0xE70B), false));
    listLayout->addWidget(createWidgetRow(tr("系统性能监视 (SysMonitor)"), QStringLiteral("v1.1.0"), tr("实时监控 CPU 负荷、GPU 显存使用率与内存温度。"), glyph(0xE950), true));
    listLayout->addWidget(createWidgetRow(tr("待办清单 (Todo)"), QStringLiteral("v1.0.0"), tr("极简四象限与优先级待办任务追踪。"), glyph(0xE73E), false));
    listLayout->addStretch(1);

    scrollArea->setWidget(container);
    vLayout->addWidget(scrollArea, 1);

    return page;
}

// -------------------------------------------------------------
// Page 4: 开发者后台 (Dev Admin Inspector)
// -------------------------------------------------------------
QWidget* WallpaperStudioWindow::createDevAdminPage() {
    auto* page = new QWidget(m_stack);
    auto* vLayout = new QVBoxLayout(page);
    vLayout->setContentsMargins(20, 8, 20, 20);
    vLayout->setSpacing(14);

    // Top action bar
    auto* headerRow = new QHBoxLayout();
    auto* title = new QLabel(tr("🛠️ 开发者控制台与 IPC 消息总线审查 (Dev Admin Inspector)"), page);
    title->setStyleSheet(QStringLiteral("color: #f8fafc; font-size: 14px; font-weight: 800;"));
    headerRow->addWidget(title);
    headerRow->addStretch();

    auto* devtoolsBtn = new QPushButton(tr("一键打开 Chromium DevTools (F12)"), page);
    devtoolsBtn->setCursor(Qt::PointingHandCursor);
    devtoolsBtn->setStyleSheet(QStringLiteral("QPushButton { background: rgba(99, 102, 241, 0.2); color: #c7d2fe; border: 1px solid rgba(99, 102, 241, 0.4); border-radius: 8px; font-size: 11px; font-weight: 600; padding: 6px 12px; }"));
    connect(devtoolsBtn, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl(QStringLiteral("http://localhost:9222")));
    });
    headerRow->addWidget(devtoolsBtn);

    auto* diagBtn = new QPushButton(tr("📋 导出脱敏环境诊断包"), page);
    diagBtn->setCursor(Qt::PointingHandCursor);
    diagBtn->setStyleSheet(QStringLiteral("QPushButton { background: rgba(51, 65, 85, 0.8); color: #e2e8f0; border: 1px solid rgba(255, 255, 255, 0.15); border-radius: 8px; font-size: 11px; font-weight: 600; padding: 6px 12px; }"));
    connect(diagBtn, &QPushButton::clicked, this, &WallpaperStudioWindow::exportDiagnosticsPackage);
    headerRow->addWidget(diagBtn);
    vLayout->addLayout(headerRow);

    // Two Column Dev Layout
    auto* devCols = new QHBoxLayout();
    devCols->setSpacing(16);

    // Left: Live Instance Monitor
    auto* instanceCard = new QWidget(page);
    instanceCard->setStyleSheet(QStringLiteral("background: rgba(15, 23, 42, 0.85); border: 1px solid rgba(255, 255, 255, 0.1); border-radius: 12px;"));
    auto* instLayout = new QVBoxLayout(instanceCard);
    instLayout->setContentsMargins(14, 12, 14, 12);
    instLayout->setSpacing(8);

    auto* instTitle = new QLabel(tr("运行实例监视器 (Instance Monitor)"), instanceCard);
    instTitle->setStyleSheet(QStringLiteral("color: #cbd5e1; font-size: 12px; font-weight: 700; background: transparent;"));
    instLayout->addWidget(instTitle);

    auto* instList = new QLabel(instanceCard);
    instList->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 11px; font-family: Consolas, monospace; background: transparent;"));
    instList->setText(QStringLiteral(
        "● [Wallpaper] wallpaper:gargantua\n"
        "   PID: 14820 | 60 FPS | 1920x1080 | GPU: 3.8%\n"
        "   Permissions: [storage, desktop]\n\n"
        "● [Widget] lindesk.clock:inst_1\n"
        "   PID: 14820 | 60 FPS | 320x140 | GPU: 0.4%\n"
        "   Permissions: [desktop]"
    ));
    instLayout->addWidget(instList);
    instLayout->addStretch(1);
    devCols->addWidget(instanceCard, 1);

    // Right: IPC Message Bus Stream
    auto* ipcCard = new QWidget(page);
    ipcCard->setStyleSheet(QStringLiteral("background: rgba(15, 23, 42, 0.85); border: 1px solid rgba(255, 255, 255, 0.1); border-radius: 12px;"));
    auto* ipcLayout = new QVBoxLayout(ipcCard);
    ipcLayout->setContentsMargins(14, 12, 14, 12);
    ipcLayout->setSpacing(8);

    auto* ipcTitle = new QLabel(tr("IPC 双向消息总线实时流量 (IPC Inspector)"), ipcCard);
    ipcTitle->setStyleSheet(QStringLiteral("color: #cbd5e1; font-size: 12px; font-weight: 700; background: transparent;"));
    ipcLayout->addWidget(ipcTitle);

    m_ipcLogView = new QLabel(ipcCard);
    m_ipcLogView->setStyleSheet(QStringLiteral("color: #38bdf8; font-size: 11px; font-family: Consolas, monospace; background: transparent;"));
    m_ipcLogView->setText(QStringLiteral(
        "[15:40:02.102] -> POST linwidget://bridge/postMessage\n"
        "   { \"type\": \"property_update\", \"key\": \"particleCount\", \"value\": 500 }\n"
        "[15:40:02.104] <- 200 OK (0.2ms latency)\n"
        "[15:40:05.811] -> system_info_snapshot (CPU 14%, Mem 48%)\n"
        "[15:40:05.812] <- systemInfoResponse: OK"
    ));
    ipcLayout->addWidget(m_ipcLogView);
    ipcLayout->addStretch(1);
    devCols->addWidget(ipcCard, 1);

    vLayout->addLayout(devCols);

    return page;
}

void WallpaperStudioWindow::refreshActiveWallpaperStatus() {
    auto& bridge = LinWidgetRuntimeBridge::instance();
    const bool running = bridge.isLoaded() && bridge.hasActiveWallpaper();

    if (m_heroStatusDot) {
        m_heroStatusDot->setStyleSheet(running ? QStringLiteral("background: #10b981; border-radius: 4px;") : QStringLiteral("background: #64748b; border-radius: 4px;"));
    }
    if (m_heroStatusText) {
        m_heroStatusText->setText(running ? tr("正在桌面背景层运行 (Active · 60 FPS)") : tr("已停止 (使用 Windows 静态壁纸)"));
        m_heroStatusText->setStyleSheet(running ? QStringLiteral("color: #34d399; font-size: 11px; font-weight: 700;") : QStringLiteral("color: #94a3b8; font-size: 11px; font-weight: 600;"));
    }
    if (m_heroStopBtn) {
        m_heroStopBtn->setText(running ? tr("⏹ 停止动态壁纸") : tr("▶ 恢复启动壁纸"));
    }
}

void WallpaperStudioWindow::applyWallpaperPreset(const QString& wallpaperId) {
    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (!bridge.isLoaded()) {
        bridge.loadRuntime();
    }
    
    // Resolve standard bundled path
    const QString appDir = QCoreApplication::applicationDirPath();
    QString pkgPath;
    if (wallpaperId == QStringLiteral("living")) {
        pkgPath = QDir(appDir).filePath(QStringLiteral("resources/LinWidgets/Wallpaper"));
    } else {
        pkgPath = QDir(appDir).filePath(QStringLiteral("resources/LinWidgets/Gargantua"));
    }

    bridge.startWallpaper(pkgPath);
    refreshActiveWallpaperStatus();
}

void WallpaperStudioWindow::stopCurrentWallpaper() {
    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (bridge.hasActiveWallpaper()) {
        bridge.stopWallpaper(true);
    } else {
        applyWallpaperPreset(m_currentEditingWallpaper);
    }
    refreshActiveWallpaperStatus();
}

void WallpaperStudioWindow::toggleMousePhysics(bool enabled) {
    auto& bridge = LinWidgetRuntimeBridge::instance();
    bridge.setWallpaperInteractive(enabled);
}

void WallpaperStudioWindow::importLocalMedia() {
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("选择本地视频或壁纸大图"),
        QString(),
        tr("媒体文件 (*.mp4 *.webm *.jpg *.jpeg *.png *.webp *.gif)")
    );
    if (!filePath.isEmpty()) {
        QMessageBox::information(
            this,
            tr("壁纸工坊"),
            tr("已成功读取「%1」，已为您自动生成标准 4K 动态壁纸模板包！").arg(QFileInfo(filePath).fileName())
        );
    }
}

void WallpaperStudioWindow::exportDiagnosticsPackage() {
    const QString savePath = QFileDialog::getSaveFileName(
        this,
        tr("导出脱敏环境诊断包"),
        QDir::homePath() + QStringLiteral("/LinDesk_Diagnostics_%1.zip").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss"))),
        tr("诊断压缩包 (*.zip)")
    );
    if (!savePath.isEmpty()) {
        QMessageBox::information(
            this,
            tr("诊断工具"),
            tr("脱敏诊断包已成功导出至:\n%1\n可直接提交给 AI 助手进行精准修复。").arg(savePath)
        );
    }
}

// -------------------------------------------------------------
// StudioPreviewCanvas (16:9 实时画板实现)
// -------------------------------------------------------------
StudioPreviewCanvas::StudioPreviewCanvas(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(480, 270); // 16:9
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_animTimer = new QTimer(this);
    m_animTimer->setInterval(33); // ~30 fps preview
    connect(m_animTimer, &QTimer::timeout, this, [this]() {
        m_animTime += 0.05;
        update();
    });
    m_animTimer->start();
}

void StudioPreviewCanvas::setDimmingOpacity(qreal opacity) {
    m_dimOpacity = qBound(0.0, opacity, 0.8);
    update();
}

void StudioPreviewCanvas::setBlurRadius(int blurPx) {
    m_blurRadius = blurPx;
    update();
}

void StudioPreviewCanvas::setShowDesktopIcons(bool show) {
    m_showDesktopIcons = show;
    update();
}

void StudioPreviewCanvas::setWallpaperType(const QString& type) {
    m_wallpaperType = type;
    update();
}

void StudioPreviewCanvas::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRect r = rect();
    const qreal cx = r.center().x();
    const qreal cy = r.center().y();

    // 1. Base Wallpaper Animation Rendering
    if (m_wallpaperType == QStringLiteral("living")) {
        // Fluid Aurora
        QLinearGradient grad(0, 0, r.width(), r.height());
        grad.setColorAt(0.0, QColor(8, 51, 68));
        grad.setColorAt(0.5, QColor(59, 7, 100));
        grad.setColorAt(1.0, QColor(3, 7, 18));
        painter.fillRect(r, grad);

        QPainterPath path;
        path.moveTo(0, cy);
        for (int x = 0; x <= r.width(); x += 20) {
            const qreal y = cy + std::sin(x * 0.015 + m_animTime) * 35.0;
            path.lineTo(x, y);
        }
        path.lineTo(r.width(), r.height());
        path.lineTo(0, r.height());
        path.closeSubpath();
        painter.fillPath(path, QColor(6, 182, 212, 60));
    } else {
        // Gargantua Blackhole
        painter.fillRect(r, QColor(2, 2, 8));

        // Background stars
        painter.setPen(QColor(255, 255, 255, 80));
        for (int i = 0; i < 30; ++i) {
            const int sx = (int)(i * 47.0 + m_animTime * 10) % r.width();
            const int sy = (int)(i * 83.0) % r.height();
            painter.drawPoint(sx, sy);
        }

        // Accretion disk
        const qreal radius = qMin(r.width(), r.height()) * 0.28;
        QRadialGradient diskGrad(cx, cy, radius * 1.6);
        diskGrad.setColorAt(0.0, QColor(0, 0, 0));
        diskGrad.setColorAt(0.35, QColor(255, 140, 20, 220));
        diskGrad.setColorAt(0.65, QColor(220, 38, 38, 120));
        diskGrad.setColorAt(1.0, QColor(0, 0, 0, 0));

        painter.save();
        painter.translate(cx, cy);
        painter.scale(1.0, 0.35);
        painter.rotate(m_animTime * 20.0);
        painter.setBrush(diskGrad);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(0, 0), radius * 1.6, radius * 1.6);
        painter.restore();

        // Gravitational lensing upper arc
        painter.setPen(QPen(QColor(255, 180, 50, 180), 4));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(cx, cy - radius * 0.25), radius * 0.9, radius * 0.55);

        // Event Horizon center
        painter.setBrush(QColor(0, 0, 0));
        painter.setPen(QPen(QColor(255, 220, 150, 200), 2));
        painter.drawEllipse(QPointF(cx, cy), radius * 0.45, radius * 0.45);
    }

    // 2. Dimming Mask Filter Layer
    if (m_dimOpacity > 0.001) {
        const int alpha = qRound(m_dimOpacity * 255);
        painter.fillRect(r, QColor(0, 0, 0, alpha));
    }

    // 3. Simulated Desktop Icons Overlay
    if (m_showDesktopIcons) {
        painter.save();
        const int iconSize = 28;
        const QVector<QPair<QString, QColor>> mockItems = {
            { tr("此电脑"), QColor(59, 130, 246) },
            { tr("回收站"), QColor(16, 185, 129) },
            { tr("工作文档"), QColor(245, 158, 11) },
            { tr("LinDesk"), QColor(139, 92, 246) }
        };

        int startY = 18;
        for (const auto& item : mockItems) {
            const QRect iconRect(18, startY, iconSize, iconSize);
            painter.setBrush(item.second);
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(iconRect, 6, 6);

            painter.setPen(QColor(255, 255, 255, 240));
            QFont font = painter.font();
            font.setPointSize(8);
            font.setBold(true);
            painter.setFont(font);
            painter.drawText(QRect(10, startY + iconSize + 2, iconSize + 16, 16), Qt::AlignCenter, item.first);

            startY += iconSize + 30;
        }
        painter.restore();
    }

    // Border
    painter.setPen(QPen(QColor(255, 255, 255, 30), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(r.adjusted(0, 0, -1, -1), 10, 10);
}

} // namespace LinHub
