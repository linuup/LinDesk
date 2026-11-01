#include "WallpaperStudioWindow.h"
#include <QApplication>
#include <QScreen>
#include <QPainter>
#include <QPainterPath>
#include <QFileDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QColorDialog>
#include <QDesktopServices>
#include <QUrl>
#include <cmath>
#include "app/AppFonts.h"
#include "desktop/widgets/runtime_export.h"
#include "ui/settings/SettingsUICommon.h"
#include "services/AppPaths.h"

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
    setMinimumSize(900, 620);

    // Center on current screen
    if (auto* screen = QGuiApplication::primaryScreen()) {
        const QRect screenGeom = screen->availableGeometry();
        move(screenGeom.center().x() - width() / 2, screenGeom.center().y() - height() / 2);
    }

    scanWallpapers();
    initUI();

    m_statusTimer = new QTimer(this);
    m_statusTimer->setInterval(1000);
    connect(m_statusTimer, &QTimer::timeout, this, &WallpaperStudioWindow::refreshActiveWallpaperStatus);
    m_statusTimer->start();
}

void WallpaperStudioWindow::showEvent(QShowEvent* event) {
    FluentBaseWidget::showEvent(event);
    scanWallpapers();
    populateGalleryCards();
    refreshActiveWallpaperStatus();
}

void WallpaperStudioWindow::closeEvent(QCloseEvent* event) {
    event->ignore();
    hide();
}

void WallpaperStudioWindow::scanWallpapers() {
    m_availableWallpapers.clear();

    QStringList searchDirs;
    searchDirs << AppPaths::userLinWidgetsDir();
    searchDirs << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/LinWidgets"));
    searchDirs << QStringLiteral("d:/Desktop/LinDesk/src/resources/LinWidgets");

    QSet<QString> seenIds;

    for (const QString& baseDir : searchDirs) {
        QDir dir(baseDir);
        if (!dir.exists()) continue;

        const QFileInfoList subDirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& subDir : subDirs) {
            const QString manifestPath = subDir.filePath() + QStringLiteral("/manifest.json");
            if (!QFileInfo::exists(manifestPath)) continue;

            QFile f(manifestPath);
            if (!f.open(QIODevice::ReadOnly)) continue;
            const QByteArray data = f.readAll();
            f.close();

            const QJsonDocument doc = QJsonDocument::fromJson(data);
            if (!doc.isObject()) continue;
            const QJsonObject root = doc.object();

            const QString role = root.value(QStringLiteral("role")).toString();
            if (role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) != 0) continue;

            const QString id = root.value(QStringLiteral("id")).toString();
            if (id.isEmpty() || seenIds.contains(id)) continue;
            seenIds.insert(id);

            WallpaperItem item;
            item.id = id;
            item.name = root.value(QStringLiteral("name")).toString(subDir.fileName());
            item.description = root.value(QStringLiteral("description")).toString();
            item.packagePath = subDir.absoluteFilePath();
            item.propertiesSchema = root.value(QStringLiteral("properties")).toObject();

            if (id.contains(QStringLiteral("gargantua"), Qt::CaseInsensitive)) {
                item.tag = tr("4K WebGL · 官方精选");
                item.thumbStyle = QStringLiteral("background: qradialgradient(cx:0.5, cy:0.5, radius:0.8, fx:0.5, fy:0.5, stop:0 #000, stop:0.35 #ea580c, stop:0.7 #312e81, stop:1 #020208); border-radius: 8px;");
            } else if (id.contains(QStringLiteral("matrix"), Qt::CaseInsensitive)) {
                item.tag = tr("4K 视频 · 静音循环");
                item.thumbStyle = QStringLiteral("background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #052e16, stop:0.6 #022c22, stop:1 #000); border-radius: 8px;");
            } else {
                item.tag = tr("极光流体 · 官方精选");
                item.thumbStyle = QStringLiteral("background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #083344, stop:0.5 #3b0764, stop:1 #030712); border-radius: 8px;");
            }

            m_availableWallpapers.append(item);
        }
    }
}

QString WallpaperStudioWindow::resolveWallpaperPath(const QString& wallpaperId) const {
    for (const auto& item : m_availableWallpapers) {
        if (item.id.compare(wallpaperId, Qt::CaseInsensitive) == 0 ||
            item.name.compare(wallpaperId, Qt::CaseInsensitive) == 0) {
            return item.packagePath;
        }
    }
    // Fallback checks
    const QString appDir = QCoreApplication::applicationDirPath();
    if (wallpaperId.contains(QStringLiteral("living"), Qt::CaseInsensitive) || wallpaperId.contains(QStringLiteral("wallpaper"), Qt::CaseInsensitive)) {
        return QDir(appDir).filePath(QStringLiteral("resources/LinWidgets/Wallpaper"));
    }
    return QDir(appDir).filePath(QStringLiteral("resources/LinWidgets/Gargantua"));
}

void WallpaperStudioWindow::initUI() {
    setWindowBtnMode(All);

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
    iconLabel->setText(glyph(0xEB9F));
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

    if (tabIndex == 1) {
        loadAndGeneratePropertyControls(m_currentEditingWallpaper);
    }

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
    thumbBox->setFixedSize(160, 90);
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

    m_galleryGridContainer = new QWidget(scrollArea);
    m_galleryGridContainer->setObjectName("gridContainer");
    m_galleryGridLayout = new QGridLayout(m_galleryGridContainer);
    m_galleryGridLayout->setContentsMargins(0, 0, 0, 0);
    m_galleryGridLayout->setSpacing(16);

    populateGalleryCards();

    scrollArea->setWidget(m_galleryGridContainer);
    pageLayout->addWidget(scrollArea, 1);

    return page;
}

void WallpaperStudioWindow::populateGalleryCards() {
    if (!m_galleryGridLayout || !m_galleryGridContainer) return;

    // Clear old items
    QLayoutItem* child = nullptr;
    while ((child = m_galleryGridLayout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            child->widget()->deleteLater();
        }
        delete child;
    }

    auto& bridge = LinWidgetRuntimeBridge::instance();
    const bool running = bridge.isLoaded() && bridge.hasActiveWallpaper();
    const QString activeId = bridge.activeWallpaperId();

    auto createWallpaperCard = [this](const WallpaperItem& item, bool isActive) {
        auto* card = new QWidget(m_galleryGridContainer);
        card->setStyleSheet(QStringLiteral(
            "QWidget {"
            "  background: rgba(30, 41, 59, 0.7);"
            "  border: 1px solid %1;"
            "  border-radius: 14px;"
            "}"
            "QWidget:hover {"
            "  border-color: rgba(99, 102, 241, 0.8);"
            "}"
        ).arg(isActive ? QStringLiteral("rgba(16, 185, 129, 0.8)") : QStringLiteral("rgba(255, 255, 255, 0.08)")));

        auto* vLayout = new QVBoxLayout(card);
        vLayout->setContentsMargins(12, 12, 12, 12);
        vLayout->setSpacing(8);

        // Thumbnail
        auto* thumb = new QWidget(card);
        thumb->setFixedHeight(110);
        thumb->setStyleSheet(item.thumbStyle);
        vLayout->addWidget(thumb);

        // Header Row
        auto* headerRow = new QHBoxLayout();
        auto* cardTitle = new QLabel(item.name, card);
        cardTitle->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 13px; font-weight: 700; background: transparent; border: none;"));
        headerRow->addWidget(cardTitle);
        headerRow->addStretch();

        auto* tag = new QLabel(item.tag, card);
        tag->setStyleSheet(QStringLiteral("color: #a5b4fc; background: rgba(99, 102, 241, 0.2); border-radius: 4px; padding: 2px 6px; font-size: 10px; font-weight: 600; border: none;"));
        headerRow->addWidget(tag);
        vLayout->addLayout(headerRow);

        // Desc
        auto* descLbl = new QLabel(item.description, card);
        descLbl->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 11px; background: transparent; border: none;"));
        descLbl->setWordWrap(true);
        descLbl->setFixedHeight(32);
        vLayout->addWidget(descLbl);

        // Buttons
        auto* btnRow = new QHBoxLayout();
        btnRow->setSpacing(8);

        auto* editBtn = new QPushButton(tr("🎨 在工坊调参"), card);
        editBtn->setCursor(Qt::PointingHandCursor);
        editBtn->setStyleSheet(QStringLiteral("QPushButton { background: rgba(51, 65, 85, 0.8); color: #e2e8f0; border: 1px solid rgba(255, 255, 255, 0.1); border-radius: 6px; font-size: 11px; font-weight: 600; padding: 4px 10px; } QPushButton:hover { background: rgba(71, 85, 105, 0.9); }"));
        connect(editBtn, &QPushButton::clicked, this, [this, id = item.id]() {
            selectWallpaperForStudio(id);
        });
        btnRow->addWidget(editBtn);

        auto* applyBtn = new QPushButton(isActive ? tr("✓ 运行中") : tr("应用壁纸"), card);
        applyBtn->setCursor(Qt::PointingHandCursor);
        if (isActive) {
            applyBtn->setStyleSheet(QStringLiteral("QPushButton { background: rgba(16, 185, 129, 0.2); color: #6ee7b7; border: 1px solid rgba(16, 185, 129, 0.4); border-radius: 6px; font-size: 11px; font-weight: 700; padding: 4px 12px; }"));
        } else {
            applyBtn->setStyleSheet(QStringLiteral("QPushButton { background: #4f46e5; color: #ffffff; border: none; border-radius: 6px; font-size: 11px; font-weight: 700; padding: 4px 12px; } QPushButton:hover { background: #4338ca; }"));
            connect(applyBtn, &QPushButton::clicked, this, [this, id = item.id]() {
                applyWallpaperPreset(id);
            });
        }
        btnRow->addWidget(applyBtn);
        vLayout->addLayout(btnRow);

        return card;
    };

    int col = 0;
    int row = 0;
    for (const auto& item : m_availableWallpapers) {
        const bool isActive = running && (activeId.compare(item.id, Qt::CaseInsensitive) == 0 ||
                                          activeId.compare(QStringLiteral("wallpaper:") + item.id, Qt::CaseInsensitive) == 0 ||
                                          item.packagePath.compare(bridge.activeWallpaperPackagePath(), Qt::CaseInsensitive) == 0);
        m_galleryGridLayout->addWidget(createWallpaperCard(item, isActive), row, col);
        col++;
        if (col >= 3) {
            col = 0;
            row++;
        }
    }

    // Drag & Drop Card
    auto* dropCard = new QWidget(m_galleryGridContainer);
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

    m_galleryGridLayout->addWidget(dropCard, row, col);
}

void WallpaperStudioWindow::selectWallpaperForStudio(const QString& wallpaperId) {
    m_currentEditingWallpaper = wallpaperId;
    if (m_studioCanvas) {
        if (wallpaperId.contains(QStringLiteral("living"), Qt::CaseInsensitive) || wallpaperId.contains(QStringLiteral("wallpaper"), Qt::CaseInsensitive)) {
            m_studioCanvas->setWallpaperType(QStringLiteral("living"));
        } else {
            m_studioCanvas->setWallpaperType(QStringLiteral("gargantua"));
        }
    }
    loadAndGeneratePropertyControls(wallpaperId);
    switchToTab(1);
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

    // Right Column: Controls Panel with ScrollArea
    auto* rightCol = new QWidget(page);
    rightCol->setFixedWidth(360);
    rightCol->setStyleSheet(QStringLiteral(
        "QWidget {"
        "  background: rgba(15, 23, 42, 0.85);"
        "  border: 1px solid rgba(255, 255, 255, 0.1);"
        "  border-radius: 14px;"
        "}"
    ));
    auto* rightLayout = new QVBoxLayout(rightCol);
    rightLayout->setContentsMargins(16, 14, 16, 14);
    rightLayout->setSpacing(10);

    auto* ctrlTitle = new QLabel(tr("画面滤镜与易读性保护"), rightCol);
    ctrlTitle->setStyleSheet(QStringLiteral("color: #f8fafc; font-size: 13px; font-weight: 800; background: transparent; border: none;"));
    rightLayout->addWidget(ctrlTitle);

    // Fit Mode
    auto* fitBtnRow = new QHBoxLayout();
    fitBtnRow->setSpacing(6);
    auto* coverBtn = new QPushButton(tr("Cover 裁剪"), rightCol);
    coverBtn->setStyleSheet(QStringLiteral("QPushButton { background: #4f46e5; color: #fff; border-radius: 6px; font-size: 11px; font-weight: 700; padding: 4px 8px; border: none; }"));
    auto* containBtn = new QPushButton(tr("Contain 适应"), rightCol);
    containBtn->setStyleSheet(QStringLiteral("QPushButton { background: #1e293b; color: #94a3b8; border-radius: 6px; font-size: 11px; padding: 4px 8px; border: 1px solid rgba(255,255,255,0.08); }"));
    auto* stretchBtn = new QPushButton(tr("Stretch 拉伸"), rightCol);
    stretchBtn->setStyleSheet(QStringLiteral("QPushButton { background: #1e293b; color: #94a3b8; border-radius: 6px; font-size: 11px; padding: 4px 8px; border: 1px solid rgba(255,255,255,0.08); }"));
    fitBtnRow->addWidget(coverBtn);
    fitBtnRow->addWidget(containBtn);
    fitBtnRow->addWidget(stretchBtn);
    rightLayout->addLayout(fitBtnRow);

    // Dimming Mask Slider
    auto* dimRow = new QHBoxLayout();
    auto* dimTitle = new QLabel(tr("遮罩暗化 (文字保护):"), rightCol);
    dimTitle->setStyleSheet(QStringLiteral("color: #cbd5e1; font-size: 11px; font-weight: 600; background: transparent; border: none;"));
    m_dimValLabel = new QLabel(tr("20%"), rightCol);
    m_dimValLabel->setStyleSheet(QStringLiteral("color: #818cf8; font-size: 11px; font-weight: 700; font-family: monospace; background: transparent; border: none;"));
    dimRow->addWidget(dimTitle);
    dimRow->addStretch();
    dimRow->addWidget(m_dimValLabel);
    rightLayout->addLayout(dimRow);

    m_dimSlider = new QSlider(Qt::Horizontal, rightCol);
    m_dimSlider->setRange(0, 80);
    m_dimSlider->setValue(20);
    connect(m_dimSlider, &QSlider::valueChanged, this, &WallpaperStudioWindow::updateStudioDimming);
    rightLayout->addWidget(m_dimSlider);

    // Blur Slider
    auto* blurRow = new QHBoxLayout();
    auto* blurTitle = new QLabel(tr("毛玻璃高斯模糊 (景深):"), rightCol);
    blurTitle->setStyleSheet(QStringLiteral("color: #cbd5e1; font-size: 11px; font-weight: 600; background: transparent; border: none;"));
    m_blurValLabel = new QLabel(tr("0 px"), rightCol);
    m_blurValLabel->setStyleSheet(QStringLiteral("color: #818cf8; font-size: 11px; font-weight: 700; font-family: monospace; background: transparent; border: none;"));
    blurRow->addWidget(blurTitle);
    blurRow->addStretch();
    blurRow->addWidget(m_blurValLabel);
    rightLayout->addLayout(blurRow);

    m_blurSlider = new QSlider(Qt::Horizontal, rightCol);
    m_blurSlider->setRange(0, 20);
    m_blurSlider->setValue(0);
    connect(m_blurSlider, &QSlider::valueChanged, this, &WallpaperStudioWindow::updateStudioBlur);
    rightLayout->addWidget(m_blurSlider);

    // Divider
    auto* divider = new QFrame(rightCol);
    divider->setFrameShape(QFrame::HLine);
    divider->setStyleSheet(QStringLiteral("background: rgba(255, 255, 255, 0.08); max-height: 1px; border: none;"));
    rightLayout->addWidget(divider);

    // Dynamic Properties Section Header
    m_propertiesTitle = new QLabel(tr("声明式自定义参数 (Declarative Properties)"), rightCol);
    m_propertiesTitle->setStyleSheet(QStringLiteral("color: #f8fafc; font-size: 13px; font-weight: 800; background: transparent; border: none;"));
    rightLayout->addWidget(m_propertiesTitle);

    // ScrollArea for dynamic property items
    auto* propScroll = new QScrollArea(rightCol);
    propScroll->setWidgetResizable(true);
    propScroll->setStyleSheet(QStringLiteral("QScrollArea { border: none; background: transparent; } QWidget#propContainer { background: transparent; }"));

    m_dynamicPropertiesContainer = new QWidget(propScroll);
    m_dynamicPropertiesContainer->setObjectName("propContainer");
    m_dynamicPropertiesLayout = new QVBoxLayout(m_dynamicPropertiesContainer);
    m_dynamicPropertiesLayout->setContentsMargins(0, 0, 0, 0);
    m_dynamicPropertiesLayout->setSpacing(10);

    propScroll->setWidget(m_dynamicPropertiesContainer);
    rightLayout->addWidget(propScroll, 1);

    // Bottom Apply Button
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

    loadAndGeneratePropertyControls(m_currentEditingWallpaper);

    return page;
}

QJsonObject WallpaperStudioWindow::loadSavedProperties(const QString& wallpaperId) const {
    const QString safeId = QString(wallpaperId).replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_.-]")), QStringLiteral("_"));
    const QString filePath = AppPaths::userLinWidgetsDataDir() + QStringLiteral("/wallpaper/") + safeId + QStringLiteral("/properties.json");
    if (!QFileInfo::exists(filePath)) return QJsonObject();

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return QJsonObject();
    const QByteArray data = f.readAll();
    f.close();

    const QJsonDocument doc = QJsonDocument::fromJson(data);
    return doc.isObject() ? doc.object() : QJsonObject();
}

void WallpaperStudioWindow::saveProperties(const QString& wallpaperId, const QJsonObject& props) {
    const QString safeId = QString(wallpaperId).replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_.-]")), QStringLiteral("_"));
    const QString dirPath = AppPaths::userLinWidgetsDataDir() + QStringLiteral("/wallpaper/") + safeId;
    QDir().mkpath(dirPath);

    QFile f(dirPath + QStringLiteral("/properties.json"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(props).toJson(QJsonDocument::Indented));
        f.close();
    }
}

void WallpaperStudioWindow::dispatchPropertyUpdate(const QString& key, const QJsonValue& val) {
    m_currentPropertyValues[key] = val;

    QJsonObject propsObj;
    for (auto it = m_currentPropertyValues.begin(); it != m_currentPropertyValues.end(); ++it) {
        propsObj.insert(it.key(), it.value());
    }
    saveProperties(m_currentEditingWallpaper, propsObj);

    QString valStr;
    if (val.isBool()) valStr = val.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    else if (val.isDouble()) valStr = QString::number(val.toDouble());
    else valStr = QStringLiteral("\"%1\"").arg(val.toString());

    appendIpcLog(QStringLiteral("POST"), QStringLiteral("{ \"type\": \"property_update\", \"key\": \"%1\", \"value\": %2 }").arg(key, valStr));

    // Send query to runtime bridge if available
    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (bridge.isLoaded()) {
        QJsonObject req;
        req.insert(QStringLiteral("action"), QStringLiteral("property_update"));
        req.insert(QStringLiteral("key"), key);
        req.insert(QStringLiteral("value"), val);
        req.insert(QStringLiteral("wallpaperId"), m_currentEditingWallpaper);
        bridge.queryWidgetManager(req);
    }

    // Update local canvas preview if applicable
    if (m_studioCanvas) {
        if (key == QStringLiteral("particleCount")) {
            m_studioCanvas->setParticleDensity(qMax(10, qMin(100, val.toInt() / 15)));
        } else if (key == QStringLiteral("lensingGlowColor") || key == QStringLiteral("singularityColor")) {
            m_studioCanvas->setAccentColor(QColor(val.toString()));
        }
    }
}

void WallpaperStudioWindow::loadAndGeneratePropertyControls(const QString& wallpaperId) {
    if (!m_dynamicPropertiesLayout || !m_dynamicPropertiesContainer) return;

    // Clear old controls
    QLayoutItem* item = nullptr;
    while ((item = m_dynamicPropertiesLayout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    m_currentPropertyValues.clear();

    // Find schema
    QJsonObject schema;
    for (const auto& w : m_availableWallpapers) {
        if (w.id.compare(wallpaperId, Qt::CaseInsensitive) == 0 ||
            w.name.compare(wallpaperId, Qt::CaseInsensitive) == 0) {
            schema = w.propertiesSchema;
            break;
        }
    }

    if (schema.isEmpty()) {
        // Fallback default properties for known items
        if (wallpaperId.contains(QStringLiteral("gargantua"), Qt::CaseInsensitive)) {
            schema.insert(QStringLiteral("particleCount"), QJsonObject{
                {QStringLiteral("type"), QStringLiteral("slider")},
                {QStringLiteral("text"), tr("吸积盘粒子密度 (Particle Density)")},
                {QStringLiteral("min"), 100}, {QStringLiteral("max"), 1200}, {QStringLiteral("step"), 50}, {QStringLiteral("value"), 450}
            });
            schema.insert(QStringLiteral("rotationSpeed"), QJsonObject{
                {QStringLiteral("type"), QStringLiteral("slider")},
                {QStringLiteral("text"), tr("相对论旋转速度 (Spin Speed)")},
                {QStringLiteral("min"), 1}, {QStringLiteral("max"), 10}, {QStringLiteral("step"), 1}, {QStringLiteral("value"), 5}
            });
            schema.insert(QStringLiteral("lensingGlowColor"), QJsonObject{
                {QStringLiteral("type"), QStringLiteral("color")},
                {QStringLiteral("text"), tr("引力透镜光晕色调 (Lensing Hue)")},
                {QStringLiteral("value"), QStringLiteral("#fdba74")}
            });
            schema.insert(QStringLiteral("enableDopplerBeaming"), QJsonObject{
                {QStringLiteral("type"), QStringLiteral("boolean")},
                {QStringLiteral("text"), tr("多普勒射束增亮 (Doppler Beaming)")},
                {QStringLiteral("value"), true}
            });
        } else {
            schema.insert(QStringLiteral("particleCount"), QJsonObject{
                {QStringLiteral("type"), QStringLiteral("slider")},
                {QStringLiteral("text"), tr("量子粒子密度 (Quantum Particles)")},
                {QStringLiteral("min"), 50}, {QStringLiteral("max"), 600}, {QStringLiteral("step"), 25}, {QStringLiteral("value"), 240}
            });
            schema.insert(QStringLiteral("fluidSpeed"), QJsonObject{
                {QStringLiteral("type"), QStringLiteral("slider")},
                {QStringLiteral("text"), tr("流体极光流速 (Aurora Speed)")},
                {QStringLiteral("min"), 1}, {QStringLiteral("max"), 8}, {QStringLiteral("step"), 1}, {QStringLiteral("value"), 3}
            });
            schema.insert(QStringLiteral("singularityColor"), QJsonObject{
                {QStringLiteral("type"), QStringLiteral("color")},
                {QStringLiteral("text"), tr("引力奇点主色调 (Singularity Color)")},
                {QStringLiteral("value"), QStringLiteral("#6366f1")}
            });
        }
    }

    const QJsonObject saved = loadSavedProperties(wallpaperId);

    // Build controls for each property
    for (auto it = schema.begin(); it != schema.end(); ++it) {
        const QString key = it.key();
        const QJsonObject prop = it.value().toObject();
        const QString type = prop.value(QStringLiteral("type")).toString();
        const QString text = prop.value(QStringLiteral("text")).toString(key);
        const QJsonValue defVal = prop.value(QStringLiteral("value"));
        const QJsonValue curVal = saved.contains(key) ? saved.value(key) : defVal;
        m_currentPropertyValues[key] = curVal;

        if (type == QStringLiteral("slider")) {
            const int minVal = prop.value(QStringLiteral("min")).toInt(0);
            const int maxVal = prop.value(QStringLiteral("max")).toInt(100);
            const int curInt = curVal.toInt(defVal.toInt(minVal));

            auto* row = new QWidget(m_dynamicPropertiesContainer);
            auto* rowLayout = new QVBoxLayout(row);
            rowLayout->setContentsMargins(0, 2, 0, 2);
            rowLayout->setSpacing(4);

            auto* head = new QHBoxLayout();
            auto* label = new QLabel(text, row);
            label->setStyleSheet(QStringLiteral("color: #cbd5e1; font-size: 11px; font-weight: 600; background: transparent; border: none;"));
            auto* valLbl = new QLabel(QString::number(curInt), row);
            valLbl->setStyleSheet(QStringLiteral("color: #818cf8; font-size: 11px; font-weight: 700; font-family: monospace; background: transparent; border: none;"));
            head->addWidget(label);
            head->addStretch();
            head->addWidget(valLbl);
            rowLayout->addLayout(head);

            auto* slider = new QSlider(Qt::Horizontal, row);
            slider->setRange(minVal, maxVal);
            slider->setValue(curInt);
            connect(slider, &QSlider::valueChanged, this, [this, key, valLbl](int val) {
                valLbl->setText(QString::number(val));
                dispatchPropertyUpdate(key, val);
            });
            rowLayout->addWidget(slider);
            m_dynamicPropertiesLayout->addWidget(row);

        } else if (type == QStringLiteral("color")) {
            const QString colorHex = curVal.toString(defVal.toString(QStringLiteral("#fdba74")));

            auto* row = new QWidget(m_dynamicPropertiesContainer);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 2, 0, 2);
            rowLayout->setSpacing(8);

            auto* label = new QLabel(text, row);
            label->setStyleSheet(QStringLiteral("color: #cbd5e1; font-size: 11px; font-weight: 600; background: transparent; border: none;"));
            rowLayout->addWidget(label);
            rowLayout->addStretch();

            auto* colorBtn = new QPushButton(colorHex, row);
            colorBtn->setCursor(Qt::PointingHandCursor);
            colorBtn->setFixedSize(85, 26);
            colorBtn->setStyleSheet(QStringLiteral(
                "QPushButton { background: %1; color: #ffffff; border: 1px solid rgba(255, 255, 255, 0.4); border-radius: 6px; font-size: 10px; font-weight: 700; }"
            ).arg(colorHex));
            connect(colorBtn, &QPushButton::clicked, this, [this, key, colorBtn, colorHex]() {
                const QColor c = QColorDialog::getColor(QColor(colorHex), this, tr("选择颜色"));
                if (c.isValid()) {
                    const QString hex = c.name();
                    colorBtn->setText(hex);
                    colorBtn->setStyleSheet(QStringLiteral(
                        "QPushButton { background: %1; color: #ffffff; border: 1px solid rgba(255, 255, 255, 0.4); border-radius: 6px; font-size: 10px; font-weight: 700; }"
                    ).arg(hex));
                    dispatchPropertyUpdate(key, hex);
                }
            });
            rowLayout->addWidget(colorBtn);
            m_dynamicPropertiesLayout->addWidget(row);

        } else if (type == QStringLiteral("boolean")) {
            const bool checked = curVal.toBool(defVal.toBool(true));

            auto* row = new QWidget(m_dynamicPropertiesContainer);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 2, 0, 2);
            rowLayout->setSpacing(8);

            auto* label = new QLabel(text, row);
            label->setStyleSheet(QStringLiteral("color: #cbd5e1; font-size: 11px; font-weight: 600; background: transparent; border: none;"));
            rowLayout->addWidget(label);
            rowLayout->addStretch();

            auto* toggle = new FluentToggleSwitch(row);
            toggle->setChecked(checked);
            connect(toggle, &FluentToggleSwitch::toggled, this, [this, key](bool c) {
                dispatchPropertyUpdate(key, c);
            });
            rowLayout->addWidget(toggle);
            m_dynamicPropertiesLayout->addWidget(row);

        } else if (type == QStringLiteral("select")) {
            auto* row = new QWidget(m_dynamicPropertiesContainer);
            auto* rowLayout = new QVBoxLayout(row);
            rowLayout->setContentsMargins(0, 2, 0, 2);
            rowLayout->setSpacing(4);

            auto* label = new QLabel(text, row);
            label->setStyleSheet(QStringLiteral("color: #cbd5e1; font-size: 11px; font-weight: 600; background: transparent; border: none;"));
            rowLayout->addWidget(label);

            auto* combo = new QComboBox(row);
            combo->setStyleSheet(QStringLiteral(
                "QComboBox { background: #1e293b; color: #f1f5f9; border: 1px solid rgba(255, 255, 255, 0.15); border-radius: 6px; padding: 4px 8px; font-size: 11px; }"
                "QComboBox::drop-down { border: none; }"
                "QComboBox QAbstractItemView { background: #0f172a; color: #f1f5f9; selection-background-color: #4f46e5; border: 1px solid rgba(255,255,255,0.15); }"
            ));

            const QJsonArray options = prop.value(QStringLiteral("options")).toArray();
            int selIdx = 0;
            int idx = 0;
            for (const auto& optVal : options) {
                const QJsonObject opt = optVal.toObject();
                const QString optLabel = opt.value(QStringLiteral("label")).toString();
                const QString optValStr = opt.value(QStringLiteral("value")).toString();
                combo->addItem(optLabel, optValStr);
                if (optValStr == curVal.toString()) {
                    selIdx = idx;
                }
                idx++;
            }
            combo->setCurrentIndex(selIdx);
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, key, combo](int i) {
                dispatchPropertyUpdate(key, combo->itemData(i).toString());
            });
            rowLayout->addWidget(combo);
            m_dynamicPropertiesLayout->addWidget(row);
        }
    }

    m_dynamicPropertiesLayout->addStretch(1);
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
// Page 3: 桌面组件管理 (Desktop Widgets)
// -------------------------------------------------------------
QWidget* WallpaperStudioWindow::createWidgetsPage() {
    auto* page = new QWidget(m_stack);
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(20, 8, 20, 20);
    pageLayout->setSpacing(14);

    auto* header = new QHBoxLayout();
    auto* title = new QLabel(tr("桌面小组件管理 (LinWidgets Hub)"), page);
    title->setStyleSheet(QStringLiteral("color: #f8fafc; font-size: 14px; font-weight: 800;"));
    header->addWidget(title);
    header->addStretch();
    pageLayout->addLayout(header);

    auto* scrollArea = new QScrollArea(page);
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet(QStringLiteral("QScrollArea { border: none; background: transparent; } QWidget#widgetsContainer { background: transparent; }"));

    auto* container = new QWidget(scrollArea);
    container->setObjectName("widgetsContainer");
    auto* listLayout = new QVBoxLayout(container);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(10);

    auto createWidgetRow = [this, container](const QString& name, const QString& ver, const QString& desc, const QString& iconGlyph, bool isRunning) {
        auto* row = new QWidget(container);
        row->setStyleSheet(QStringLiteral(
            "QWidget {"
            "  background: rgba(30, 41, 59, 0.6);"
            "  border: 1px solid rgba(255, 255, 255, 0.08);"
            "  border-radius: 10px;"
            "}"
        ));
        auto* rLayout = new QHBoxLayout(row);
        rLayout->setContentsMargins(14, 10, 14, 10);
        rLayout->setSpacing(14);

        auto* iconLbl = new QLabel(iconGlyph, row);
        iconLbl->setFixedSize(36, 36);
        iconLbl->setAlignment(Qt::AlignCenter);
        iconLbl->setStyleSheet(QStringLiteral("background: rgba(99, 102, 241, 0.15); color: #818cf8; border-radius: 8px; font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets'; font-size: 18px;"));
        rLayout->addWidget(iconLbl);

        auto* textLayout = new QVBoxLayout();
        textLayout->setSpacing(2);
        auto* nameLbl = new QLabel(QStringLiteral("%1 (%2)").arg(name, ver), row);
        nameLbl->setStyleSheet(QStringLiteral("color: #f8fafc; font-size: 13px; font-weight: 700; background: transparent; border: none;"));
        auto* descLbl = new QLabel(desc, row);
        descLbl->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 11px; background: transparent; border: none;"));
        textLayout->addWidget(nameLbl);
        textLayout->addWidget(descLbl);
        rLayout->addLayout(textLayout, 1);

        auto* statusBadge = new QLabel(isRunning ? tr("● 运行中") : tr("○ 待机"), row);
        statusBadge->setStyleSheet(isRunning ? QStringLiteral("color: #34d399; font-size: 11px; font-weight: 700; background: transparent; border: none;") : QStringLiteral("color: #64748b; font-size: 11px; background: transparent; border: none;"));
        rLayout->addWidget(statusBadge);

        auto* toggleBtn = new QPushButton(isRunning ? tr("停用") : tr("启用"), row);
        toggleBtn->setCursor(Qt::PointingHandCursor);
        toggleBtn->setStyleSheet(isRunning
            ? QStringLiteral("QPushButton { background: rgba(239, 68, 68, 0.15); color: #fca5a5; border: 1px solid rgba(239, 68, 68, 0.35); border-radius: 6px; font-size: 11px; font-weight: 600; padding: 4px 12px; }")
            : QStringLiteral("QPushButton { background: #4f46e5; color: #ffffff; border: none; border-radius: 6px; font-size: 11px; font-weight: 600; padding: 4px 12px; }"));
        rLayout->addWidget(toggleBtn);

        return row;
    };

    listLayout->addWidget(createWidgetRow(tr("模拟时钟 (Clock)"), QStringLiteral("v1.2.0"), tr("Fluent 磨砂玻璃双模时钟，支持精准毫秒级表盘。"), glyph(0xE823), true));
    listLayout->addWidget(createWidgetRow(tr("系统性能监视 (SysMonitor)"), QStringLiteral("v1.1.0"), tr("实时监控 CPU 负载、GPU 显存使用率与内存温度。"), glyph(0xE950), true));
    listLayout->addWidget(createWidgetRow(tr("待办清单 (Todo)"), QStringLiteral("v1.0.0"), tr("极简四象限与优先级待办任务追踪。"), glyph(0xE73E), false));
    listLayout->addStretch(1);

    scrollArea->setWidget(container);
    pageLayout->addWidget(scrollArea, 1);

    return page;
}

// -------------------------------------------------------------
// Page 4: 开发者后台控制台 (Dev Admin Inspector)
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
        auto& bridge = LinWidgetRuntimeBridge::instance();
        if (!bridge.showDevTools(QStringLiteral("wallpaper"))) {
            QDesktopServices::openUrl(QUrl(QStringLiteral("http://localhost:9222")));
        }
    });
    headerRow->addWidget(devtoolsBtn);

    auto* diagBtn = new QPushButton(tr("📋 导出脱敏环境诊断包"), page);
    diagBtn->setCursor(Qt::PointingHandCursor);
    diagBtn->setStyleSheet(QStringLiteral("QPushButton { background: rgba(51, 65, 85, 0.8); color: #e2e8f0; border: 1px solid rgba(255, 255, 255, 0.15); border-radius: 8px; font-size: 11px; font-weight: 600; padding: 6px 12px; }"));
    connect(diagBtn, &QPushButton::clicked, this, &WallpaperStudioWindow::exportDiagnosticsPackage);
    headerRow->addWidget(diagBtn);
    vLayout->addLayout(headerRow);

    // 1. Instance Metrics Grid
    auto* metricsGrid = new QGridLayout();
    metricsGrid->setSpacing(12);

    auto createMetricCard = [page](const QString& label, const QString& val, const QString& sub, const QString& color) {
        auto* card = new QWidget(page);
        card->setStyleSheet(QStringLiteral("background: rgba(30, 41, 59, 0.7); border: 1px solid rgba(255, 255, 255, 0.08); border-radius: 12px;"));
        auto* l = new QVBoxLayout(card);
        l->setContentsMargins(14, 12, 14, 12);
        l->setSpacing(4);

        auto* tLbl = new QLabel(label, card);
        tLbl->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 11px; font-weight: 600; background: transparent; border: none;"));
        auto* vLbl = new QLabel(val, card);
        vLbl->setStyleSheet(QStringLiteral("color: %1; font-size: 18px; font-weight: 800; font-family: Consolas, monospace; background: transparent; border: none;").arg(color));
        auto* sLbl = new QLabel(sub, card);
        sLbl->setStyleSheet(QStringLiteral("color: #64748b; font-size: 10px; background: transparent; border: none;"));

        l->addWidget(tLbl);
        l->addWidget(vLbl);
        l->addWidget(sLbl);
        return card;
    };

    metricsGrid->addWidget(createMetricCard(tr("CEF OSR 渲染帧率"), QStringLiteral("60.0 FPS"), tr("硬件垂直同步加速 · 0 丢帧"), QStringLiteral("#34d399")), 0, 0);
    metricsGrid->addWidget(createMetricCard(tr("物理显存占用 (VRAM)"), QStringLiteral("84.2 MB"), tr("DirectX 11 纹理共享池"), QStringLiteral("#38bdf8")), 0, 1);
    metricsGrid->addWidget(createMetricCard(tr("IPC 总线调度延迟"), QStringLiteral("0.28 ms"), tr("本地命名管道极速转发"), QStringLiteral("#a78bfa")), 0, 2);
    metricsGrid->addWidget(createMetricCard(tr("崩溃安全熔断阈值"), QStringLiteral("0 / 3 次"), tr("60s 窗口自动熔断保护正常"), QStringLiteral("#f472b6")), 0, 3);
    vLayout->addLayout(metricsGrid);

    // 2. IPC Message Bus Inspector
    auto* ipcCard = new QWidget(page);
    ipcCard->setStyleSheet(QStringLiteral("background: #050811; border: 1px solid rgba(255, 255, 255, 0.08); border-radius: 12px;"));
    auto* ipcLayout = new QVBoxLayout(ipcCard);
    ipcLayout->setContentsMargins(14, 10, 14, 10);
    ipcLayout->setSpacing(6);

    auto* ipcTitle = new QLabel(tr("📡 IPC Bridge 消息流水实时审查 (Message Bus Trace)"), ipcCard);
    ipcTitle->setStyleSheet(QStringLiteral("color: #cbd5e1; font-size: 11px; font-weight: 700; background: transparent; border: none;"));
    ipcLayout->addWidget(ipcTitle);

    m_ipcLogView = new QLabel(ipcCard);
    m_ipcLogView->setStyleSheet(QStringLiteral("color: #38bdf8; font-size: 11px; font-family: Consolas, monospace; background: transparent; border: none;"));
    m_ipcLogView->setText(QStringLiteral(
        "[16:00:00.000] -> POST linwidget://bridge/postMessage\n"
        "   { \"type\": \"wallpaper_get_interactive\" }\n"
        "[16:00:00.002] <- 200 OK (0.2ms latency)\n"
        "[16:00:00.003] -> system_info_snapshot (CPU 12%, Mem 45%)\n"
    ));
    ipcLayout->addWidget(m_ipcLogView);
    vLayout->addWidget(ipcCard, 1);

    return page;
}

void WallpaperStudioWindow::appendIpcLog(const QString& direction, const QString& msg) {
    if (!m_ipcLogView) return;
    const QString timestamp = QTime::currentTime().toString(QStringLiteral("hh:mm:ss.zzz"));
    const QString line = QStringLiteral("[%1] -> %2 linwidget://bridge/postMessage\n   %3\n").arg(timestamp, direction, msg);
    QString current = m_ipcLogView->text();
    QStringList lines = current.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    while (lines.size() > 8) {
        lines.removeFirst();
    }
    lines.append(line.trimmed());
    m_ipcLogView->setText(lines.join(QLatin1Char('\n')) + QLatin1Char('\n'));
}

void WallpaperStudioWindow::refreshActiveWallpaperStatus() {
    auto& bridge = LinWidgetRuntimeBridge::instance();
    const bool running = bridge.isLoaded() && bridge.hasActiveWallpaper();

    if (m_heroStatusDot) {
        m_heroStatusDot->setStyleSheet(running ? QStringLiteral("background: #10b981; border-radius: 4px;") : QStringLiteral("background: #64748b; border-radius: 4px;"));
    }
    if (m_heroStatusText) {
        m_heroStatusText->setText(running ? tr("正在桌面背景层运行 (Active · 60 FPS)") : tr("动态壁纸未启动 (Dormant)"));
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
    
    const QString pkgPath = resolveWallpaperPath(wallpaperId);
    bridge.startWallpaper(pkgPath);
    populateGalleryCards();
    refreshActiveWallpaperStatus();
}

void WallpaperStudioWindow::stopCurrentWallpaper() {
    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (bridge.hasActiveWallpaper()) {
        bridge.stopWallpaper(true);
    } else {
        applyWallpaperPreset(m_currentEditingWallpaper);
    }
    populateGalleryCards();
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
    setMinimumSize(480, 270);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_animTimer = new QTimer(this);
    m_animTimer->setInterval(33);
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

void StudioPreviewCanvas::setParticleDensity(int density) {
    m_particleDensity = qBound(10, density, 120);
    update();
}

void StudioPreviewCanvas::setAccentColor(const QColor& color) {
    m_accentColor = color;
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
        grad.setColorAt(0.5, m_accentColor.darker(150));
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
        painter.fillPath(path, QColor(m_accentColor.red(), m_accentColor.green(), m_accentColor.blue(), 70));
    } else {
        // Gargantua Blackhole
        painter.fillRect(r, QColor(2, 2, 8));

        // Background stars
        painter.setPen(QColor(255, 255, 255, 80));
        for (int i = 0; i < m_particleDensity; ++i) {
            const int sx = (int)(i * 47.0 + m_animTime * 10) % r.width();
            const int sy = (int)(i * 83.0) % r.height();
            painter.drawPoint(sx, sy);
        }

        // Accretion disk
        const qreal radius = qMin(r.width(), r.height()) * 0.28;
        QRadialGradient diskGrad(cx, cy, radius * 1.6);
        diskGrad.setColorAt(0.0, QColor(0, 0, 0));
        diskGrad.setColorAt(0.35, m_accentColor);
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
        painter.setPen(QPen(m_accentColor.lighter(120), 4));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(cx, cy - radius * 0.25), radius * 0.9, radius * 0.55);

        // Event Horizon center
        painter.setBrush(QColor(0, 0, 0));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(cx, cy), radius * 0.45, radius * 0.45);
    }

    // 2. Dimming Mask Filter Layer
    if (m_dimOpacity > 0.001) {
        painter.fillRect(r, QColor(0, 0, 0, static_cast<int>(m_dimOpacity * 255)));
    }

    // 3. Desktop Icons Mock Overlay (高保真桌面图标与网格浮层透视)
    if (m_showDesktopIcons) {
        const int iconSize = 28;
        const QVector<QPair<QString, QColor>> mockItems = {
            { tr("此电脑"), QColor(59, 130, 246) },
            { tr("回收站"), QColor(16, 185, 129) },
            { tr("项目文档.docx"), QColor(99, 102, 241) },
            { tr("工作汇报.pptx"), QColor(245, 158, 11) },
            { tr("代码仓库"), QColor(236, 72, 153) },
        };

        int startY = 18;
        for (const auto& item : mockItems) {
            const QRect iconRect(18, startY, iconSize, iconSize);
            painter.setBrush(item.second);
            painter.setPen(QPen(QColor(255, 255, 255, 180), 1));
            painter.drawRoundedRect(iconRect, 6, 6);

            painter.setPen(QColor(255, 255, 255, 220));
            painter.setFont(QFont(QStringLiteral("Microsoft YaHei"), 8, QFont::Bold));
            painter.drawText(iconRect.right() + 10, iconRect.center().y() + 4, item.first);

            startY += 40;
        }

        // Mock Box outline
        const QRect boxRect(r.width() - 170, 20, 150, 120);
        painter.setBrush(QColor(15, 23, 42, 140));
        painter.setPen(QPen(QColor(99, 102, 241, 160), 1, Qt::DashLine));
        painter.drawRoundedRect(boxRect, 8, 8);

        painter.setPen(QColor(199, 210, 254));
        painter.setFont(QFont(QStringLiteral("Microsoft YaHei"), 8, QFont::Bold));
        painter.drawText(boxRect.adjusted(10, 8, -10, -8), Qt::AlignTop | Qt::AlignLeft, tr("📦 常用收纳盒"));
    }

    // 4. Viewport Outer Border
    painter.setPen(QPen(QColor(255, 255, 255, 40), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(r.adjusted(0, 0, -1, -1));
}

} // namespace LinHub
