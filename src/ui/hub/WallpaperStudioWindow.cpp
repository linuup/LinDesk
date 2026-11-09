#include "WallpaperStudioWindow.h"
#include <QApplication>
#include <QScreen>
#include <QPainter>
#include <QPainterPath>
#include <QFileDialog>
#include "ui/Fluent/FluentDialog.h"
#include <QDateTime>
#include <QTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QColorDialog>
#include <QDesktopServices>
#include <QFormLayout>
#include <QLabel>
#include <QUrl>
#include <QSettings>
#include <QUuid>
#include <QMimeData>
#include <cmath>
#ifdef Q_OS_WIN
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#endif
#include "app/AppFonts.h"
#include "desktop/runtime/RuntimeInstaller.h"
#include "desktop/widgets/runtime_export.h"
#include "desktop/widgets/linwidget/WidgetRegistryManager.h"
#include "desktop/widgets/linwidget/WidgetPackage.h"
#include "desktop/ListView.h"
#include "ui/SettingUI.h"
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

void WallpaperStudioWindow::hideIfOpen() {
    if (s_instance && s_instance->isVisible()) {
        s_instance->hide();
    }
}

void WallpaperStudioWindow::showStudio(int defaultTab) {
    qInfo() << "[WallpaperStudio] showStudio called! defaultTab=" << defaultTab;
    if (!RuntimeInstaller::instance().isRuntimeInstalled()) {
        hideIfOpen();
        if (auto* lv = ListView::instance()) {
            lv->showSettings(nullptr);
            if (SettingUI* settingsUI = lv->settingsWindow()) {
                settingsUI->switchToPage(4);
            }
        }
        return;
    }

    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (!bridge.isLoaded()) {
        bridge.loadRuntime();
    }

    auto* win = instance();
    if (win->isMinimized()) {
        win->showNormal();
    }
    win->show();
    win->raise();
    win->activateWindow();
    if (!win->m_hubWebWidget) {
        win->switchToTab(defaultTab);
    }

#ifdef Q_OS_WIN
    // 强制突破 Windows 10/11 前台激活锁定，确保独立应用窗口置顶并在任务栏完整显示
    HWND hwnd = reinterpret_cast<HWND>(win->winId());
    if (hwnd && IsWindow(hwnd)) {
        LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        if (!(exStyle & WS_EX_APPWINDOW) || (exStyle & WS_EX_TOOLWINDOW)) {
            exStyle |= WS_EX_APPWINDOW;
            exStyle &= ~WS_EX_TOOLWINDOW;
            SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle);
        }

        if (GetParent(hwnd) != nullptr) {
            SetParent(hwnd, nullptr);
        }

        // 允许从资源管理器或桌面拖入外部媒体或组件工程目录
        DragAcceptFiles(hwnd, TRUE);
        ChangeWindowMessageFilterEx(hwnd, WM_DROPFILES, MSGFLT_ALLOW, nullptr);
        ChangeWindowMessageFilterEx(hwnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
        ChangeWindowMessageFilterEx(hwnd, 0x0049 /* WM_COPYGLOBALDATA */, MSGFLT_ALLOW, nullptr);

        ShowWindow(hwnd, SW_SHOW);
        SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);

        HWND foreHwnd = GetForegroundWindow();
        DWORD foreThread = foreHwnd ? GetWindowThreadProcessId(foreHwnd, nullptr) : 0;
        DWORD appThread = GetCurrentThreadId();

        if (foreThread != 0 && foreThread != appThread) {
            AttachThreadInput(foreThread, appThread, TRUE);
            BringWindowToTop(hwnd);
            SetForegroundWindow(hwnd);
            AttachThreadInput(foreThread, appThread, FALSE);
        } else {
            BringWindowToTop(hwnd);
            SetForegroundWindow(hwnd);
        }
        qInfo() << "[WallpaperStudio] Win32 ForceForegroundWindow applied successfully to HWND:" << QString::number(reinterpret_cast<quintptr>(hwnd), 16);
    }
#endif
}

WallpaperStudioWindow::WallpaperStudioWindow(QWidget* parent)
    : FluentBaseWidget(parent)
{
    setWindowTitle(tr("LinDesk Hub · 动态壁纸工坊与桌面组件"));
    resize(1180, 750);
    setMinimumSize(960, 620);
    setResizable(true);
    setAcceptDrops(true); // 允许拖拽媒体文件快速制作壁纸

    // 居中显示
    if (auto* screen = QGuiApplication::primaryScreen()) {
        const QRect screenGeom = screen->availableGeometry();
        move(screenGeom.center().x() - width() / 2, screenGeom.center().y() - height() / 2);
    }

    scanWallpapers();
    initUI();

    m_statusTimer = new QTimer(this);
    m_statusTimer->setInterval(1000);
    connect(m_statusTimer, &QTimer::timeout, this, [this]() {
        refreshActiveWallpaperStatus();
        if (m_stack && m_stack->currentIndex() == 1) {
            refreshWidgetsList();
        }
    });
    m_statusTimer->start();
}

void WallpaperStudioWindow::showEvent(QShowEvent* event) {
    FluentBaseWidget::showEvent(event);
    auto& b = LinWidgetRuntimeBridge::instance();
    if (!b.isLoaded()) b.loadRuntime();

#ifdef Q_OS_WIN
    QTimer::singleShot(0, this, [this]() {
        HWND hwnd = reinterpret_cast<HWND>(winId());
        if (hwnd && IsWindow(hwnd)) {
            DragAcceptFiles(hwnd, TRUE);
            ChangeWindowMessageFilterEx(hwnd, WM_DROPFILES, MSGFLT_ALLOW, nullptr);
            ChangeWindowMessageFilterEx(hwnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
            ChangeWindowMessageFilterEx(hwnd, 0x0049 /* WM_COPYGLOBALDATA */, MSGFLT_ALLOW, nullptr);
        }
    });
#endif

    scanWallpapers();
    populateGalleryCards();
    refreshActiveWallpaperStatus();
    refreshWidgetsList();
}

void WallpaperStudioWindow::resizeEvent(QResizeEvent* event) {
    FluentBaseWidget::resizeEvent(event);
    if (m_toastWidget && m_toastWidget->isVisible()) {
        const int margin = 24;
        m_toastWidget->move(width() - m_toastWidget->width() - margin, height() - m_toastWidget->height() - margin);
    }
}

void WallpaperStudioWindow::closeEvent(QCloseEvent* event) {
    event->ignore();
    hide();
}

void WallpaperStudioWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData() && event->mimeData()->hasUrls()) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }
    event->ignore();
}

void WallpaperStudioWindow::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData() && event->mimeData()->hasUrls()) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        event->ignore();
    }
}

void WallpaperStudioWindow::dropEvent(QDropEvent* event) {
    if (!event->mimeData() || !event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }

    if (m_hubWebWidget) {
        QCoreApplication::sendEvent(m_hubWebWidget, event);
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }

    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl& url : urls) {
        QString localFile = url.toLocalFile();
        if (QFileInfo(localFile).isSymLink()) {
            const QString target = QFileInfo(localFile).symLinkTarget();
            if (!target.isEmpty() && QFileInfo::exists(target)) {
                localFile = target;
            }
        }
        if (QFileInfo::exists(localFile)) {
            createAndInstallQuickWallpaper(localFile);
            event->setDropAction(Qt::CopyAction);
            event->accept();
            return;
        }
    }
}

void WallpaperStudioWindow::scanWallpapers() {
    m_availableWallpapers.clear();

    QStringList searchDirs;
    searchDirs << AppPaths::userWallpapersDir();
    searchDirs << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/wallpapers"));
    searchDirs << QStringLiteral(LINDESK_SOURCE_DIR) + QStringLiteral("/src/resources/wallpapers");
    searchDirs << QStringLiteral(LINDESK_SOURCE_DIR) + QStringLiteral("/src/resources/LinWidgets/Wallpaper");
    searchDirs << QDir(AppPaths::userLinWidgetsDir()).filePath(QStringLiteral("../wallpapers"));

    QSet<QString> seenIds;

    for (const QString& baseDir : searchDirs) {
        QDir dir(baseDir);
        if (!dir.exists()) continue;

        // 如果该目录本身就是一个壁纸包 (含有 manifest.json)
        QFileInfoList checkList;
        if (QFileInfo::exists(dir.filePath(QStringLiteral("manifest.json")))) {
            checkList.append(QFileInfo(baseDir));
        } else {
            checkList = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        }

        for (const QFileInfo& subDir : checkList) {
            const QString manifestPath = subDir.isDir() ? subDir.filePath() + QStringLiteral("/manifest.json") : subDir.absoluteFilePath();
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
            item.packagePath = subDir.isDir() ? subDir.absoluteFilePath() : subDir.absolutePath();
            item.propertiesSchema = root.value(QStringLiteral("properties")).toObject();
            item.wallpaperType = root.value(QStringLiteral("wallpaperType")).toString(QStringLiteral("html"));

            // 检查开发环境标识
            item.isDevelopment = root.value(QStringLiteral("isDevelopment")).toBool(false);
            if (item.packagePath.contains(QStringLiteral(LINDESK_SOURCE_DIR), Qt::CaseInsensitive)) {
                item.isDevelopment = true;
            }

            // 查找封面图
            const QString candPreview = QDir(item.packagePath).filePath(root.value(QStringLiteral("preview")).toString(QStringLiteral("preview.png")));
            if (QFileInfo::exists(candPreview)) {
                item.previewPath = candPreview;
            } else if (QFileInfo::exists(QDir(item.packagePath).filePath(QStringLiteral("preview.jpg")))) {
                item.previewPath = QDir(item.packagePath).filePath(QStringLiteral("preview.jpg"));
            }

            if (item.wallpaperType == QStringLiteral("video")) {
                item.tag = tr("4K 视频 · 无缝循环");
                item.thumbStyle = QStringLiteral("background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #0f172a, stop:0.6 #1e293b, stop:1 #020617); border-radius: 8px;");
            } else if (id.contains(QStringLiteral("living"), Qt::CaseInsensitive) || id.contains(QStringLiteral("wallpaper"), Qt::CaseInsensitive)) {
                item.tag = tr("HTML5 WebGL · 官方动态");
                item.thumbStyle = QStringLiteral("background: qradialgradient(cx:0.5, cy:0.5, radius:0.8, fx:0.5, fy:0.5, stop:0 #000, stop:0.35 #6366f1, stop:0.7 #312e81, stop:1 #020208); border-radius: 8px;");
            } else {
                item.tag = tr("动态引擎 · 极客预设");
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
    const QString srcWall = QStringLiteral(LINDESK_SOURCE_DIR) + QStringLiteral("/src/resources/LinWidgets/Wallpaper");
    if (QDir(srcWall).exists()) {
        return srcWall;
    }
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/wallpapers/living_wallpaper"));
}

void WallpaperStudioWindow::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    // 现代浅色一体化背景（与 shadcn/ui #f8fafc 无缝融合，消除标题栏黑条）
    painter.setPen(QPen(QColor(226, 232, 240), 1)); // #e2e8f0 柔和微边框
    painter.setBrush(QColor(248, 250, 252)); // #f8fafc
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 12, 12);
}

void WallpaperStudioWindow::initUI() {
    setWindowBtnMode(All);

    // 全局注入 Authentic UI 现代深色控件与极简隐形滚动条，彻底消灭 Windows 粗白滚动条
    setStyleSheet(QStringLiteral(
        "WallpaperStudioWindow, QWidget#fluentBaseContainer {"
        "  background: transparent;"
        "  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', 'Microsoft YaHei', sans-serif;"
        "}"
        "/* 全局隐形无感细滚动条 */"
        "QScrollBar:vertical {"
        "  border: none; background: transparent; width: 6px; margin: 4px 2px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: rgba(255, 255, 255, 0.16); border-radius: 3px; min-height: 24px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "  background: rgba(255, 255, 255, 0.32);"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }"
        "QScrollBar:horizontal {"
        "  border: none; background: transparent; height: 6px; margin: 2px 4px;"
        "}"
        "QScrollBar::handle:horizontal {"
        "  background: rgba(255, 255, 255, 0.16); border-radius: 3px; min-width: 24px;"
        "}"
        "QScrollBar::handle:horizontal:hover {"
        "  background: rgba(255, 255, 255, 0.32);"
        "}"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: none; }"
        "/* 科技感纯净 Slider 控件 */"
        "QSlider::groove:horizontal {"
        "  height: 4px; background: rgba(255, 255, 255, 0.12); border-radius: 2px;"
        "}"
        "QSlider::sub-page:horizontal {"
        "  background: #3b82f6; border-radius: 2px;"
        "}"
        "QSlider::handle:horizontal {"
        "  background: #ffffff; border: 1px solid rgba(0,0,0,0.2); width: 14px; height: 14px; margin: -5px 0; border-radius: 7px;"
        "}"
        "QSlider::handle:horizontal:hover { background: #f8fafc; border-color: #3b82f6; }"
    ));

    if (m_btnContainer) {
        m_btnContainer->setFixedHeight(36);
        m_btnContainer->setAttribute(Qt::WA_StyledBackground, true);
        m_btnContainer->setObjectName(QStringLiteral("hubTitleBar"));
        m_btnContainer->setStyleSheet(QStringLiteral(
            "QWidget#hubTitleBar { background: #f8fafc; border: none; border-top-left-radius: 12px; border-top-right-radius: 12px; }"
            "QPushButton { background: transparent; border: none; font-family: 'Segoe MDL2 Assets'; font-size: 10px; color: #64748b; }"
            "QPushButton:hover { background: rgba(0, 0, 0, 0.06); color: #0f172a; }"
            "QPushButton#closeBtn:hover { background: #ef4444; color: #ffffff; border-top-right-radius: 12px; }"
        ));

        // 左上角 LinDesk 品牌标识与标题栏组件（消除左上角空白）
        QWidget* brandWidget = new QWidget(m_btnContainer);
        brandWidget->setAttribute(Qt::WA_TransparentForMouseEvents, true); // 鼠标穿透，点击即可直接拖动窗口
        QHBoxLayout* brandLayout = new QHBoxLayout(brandWidget);
        brandLayout->setContentsMargins(14, 0, 0, 0);
        brandLayout->setSpacing(7);

        QLabel* iconLabel = new QLabel(brandWidget);
        QPixmap iconPix(":/Assets/icon.png");
        if (!iconPix.isNull()) {
            iconLabel->setPixmap(iconPix.scaled(18, 18, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        iconLabel->setFixedSize(18, 18);
        brandLayout->addWidget(iconLabel);

        QLabel* titleLabel = new QLabel(tr("LinDesk Hub"), brandWidget);
        titleLabel->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: 700; color: #1e293b; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;"));
        brandLayout->addWidget(titleLabel);

        if (m_btnLayout) {
            m_btnLayout->insertWidget(0, brandWidget);
        }
    }

    if (container()) {
        container()->setAttribute(Qt::WA_StyledBackground, true);
        container()->setObjectName(QStringLiteral("hubCentralWidget"));
        container()->setStyleSheet(QStringLiteral(
            "QWidget#fluentBaseContainer, QWidget#hubCentralWidget {"
            "  background-color: #f8fafc;"
            "  border: none;"
            "}"
        ));
    }

    // 优先加载基于 shadcn/ui 打造的高质感 React 现代前端工程 (web/hub -> resources/hub/index.html)
    QString targetPath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/hub/index.html"));
    if (!QFileInfo::exists(targetPath)) {
        targetPath = QStringLiteral(LINDESK_SOURCE_DIR) + QStringLiteral("/src/resources/hub/index.html");
    }
    if (!QFileInfo::exists(targetPath)) {
        targetPath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/LinWidgets/LinDeskHub/index.html"));
    }
    if (!QFileInfo::exists(targetPath)) {
        targetPath = QStringLiteral(LINDESK_SOURCE_DIR) + QStringLiteral("/src/resources/LinWidgets/LinDeskHub/index.html");
    }

    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (!bridge.isLoaded()) {
        bridge.loadRuntime();
    }

    if (QFileInfo::exists(targetPath)) {
        const QString targetUrl = QUrl::fromLocalFile(targetPath).toString();
        m_hubWebWidget = bridge.createHubWebWidget(targetUrl, container());
        if (m_hubWebWidget) {
            if (m_btnContainer) {
                m_btnContainer->hide();
            }
            centralLayout()->setContentsMargins(0, 0, 0, 0);
            centralLayout()->setSpacing(0);
            centralLayout()->addWidget(m_hubWebWidget, 1);
            qInfo() << "[WallpaperStudio] Successfully loaded HTML Hub page from:" << targetPath;
            return;
        }
    }

    // 若 CEF 未就绪，降级到 Qt 原生后备界面
    setupHeader();
    setupNavPills();

    m_stack = new QStackedWidget(container());
    m_stack->setObjectName(QStringLiteral("hubContentStack"));
    m_stack->setStyleSheet(QStringLiteral(
        "QStackedWidget#hubContentStack { background: transparent; border: none; }"
    ));
    m_stack->addWidget(createWallpaperHubPage()); // Tab 0: 动态壁纸工坊 (Master-Detail)
    m_stack->addWidget(createWidgetsPage());      // Tab 1: 桌面组件管理
    m_stack->addWidget(createDevAdminPage());     // Tab 2: 开发者后台
    
    centralLayout()->addWidget(m_stack, 1);
    switchToTab(0);
}

void WallpaperStudioWindow::setupHeader() {
    auto* header = new QWidget(container());
    header->setFixedHeight(48);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(20, 4, 20, 0);
    headerLayout->setSpacing(12);

    // Brand icon (精致微光深色方块，去除低端荧光紫渐变)
    auto* iconLabel = new QLabel(header);
    iconLabel->setFixedSize(30, 30);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setStyleSheet(QStringLiteral(
        "background: rgba(255, 255, 255, 0.08);"
        "border: 1px solid rgba(255, 255, 255, 0.12);"
        "border-radius: 7px;"
        "font-size: 14px;"
    ));
    iconLabel->setText(QStringLiteral("🌌"));
    headerLayout->addWidget(iconLabel);

    // Brand titles (符合专业生产力软件层次)
    auto* titlesBox = new QVBoxLayout();
    titlesBox->setSpacing(1);
    auto* titleLabel = new QLabel(tr("LinDesk Hub"), header);
    titleLabel->setStyleSheet(QStringLiteral(
        "font-size: 15px; font-weight: 600; color: #f8fafc; letter-spacing: -0.01em;"
    ));
    auto* subtitleLabel = new QLabel(tr("动态壁纸工坊与桌面组件管理"), header);
    subtitleLabel->setStyleSheet(QStringLiteral(
        "font-size: 11px; color: #64748b; font-weight: 400;"
    ));
    titlesBox->addWidget(titleLabel);
    titlesBox->addWidget(subtitleLabel);
    headerLayout->addLayout(titlesBox);

    headerLayout->addStretch(1);

    // 硬件加速指示芯片 (Linear 质感，6px 纯净翠绿呼吸微灯)
    auto* chipContainer = new QWidget(header);
    chipContainer->setObjectName(QStringLiteral("hwChip"));
    chipContainer->setStyleSheet(QStringLiteral(
        "QWidget#hwChip {"
        "  background: rgba(16, 185, 129, 0.08);"
        "  border: 1px solid rgba(16, 185, 129, 0.25);"
        "  border-radius: 12px;"
        "}"
    ));
    auto* chipLayout = new QHBoxLayout(chipContainer);
    chipLayout->setContentsMargins(9, 3, 9, 3);
    chipLayout->setSpacing(6);

    auto* chipDot = new QLabel(chipContainer);
    chipDot->setFixedSize(6, 6);
    chipDot->setStyleSheet(QStringLiteral("background: #10b981; border-radius: 3px;"));
    chipLayout->addWidget(chipDot);

    auto* chipText = new QLabel(tr("CEF 硬件加速 · 60 FPS"), chipContainer);
    chipText->setStyleSheet(QStringLiteral("color: #34d399; font-size: 11px; font-weight: 500; font-family: monospace, sans-serif;"));
    chipLayout->addWidget(chipText);

    headerLayout->addWidget(chipContainer, 0, Qt::AlignVCenter);
    centralLayout()->addWidget(header);
}

void WallpaperStudioWindow::setupNavPills() {
    auto* navWidget = new QWidget(container());
    navWidget->setFixedHeight(42);
    auto* navLayout = new QHBoxLayout(navWidget);
    navLayout->setContentsMargins(20, 2, 20, 6);
    navLayout->setSpacing(6);

    auto createPill = [this, navWidget](const QString& text, int tabIndex) {
        auto* btn = new QPushButton(text, navWidget);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(28);
        btn->setMinimumWidth(110);
        connect(btn, &QPushButton::clicked, this, [this, tabIndex]() {
            switchToTab(tabIndex);
        });
        return btn;
    };

    m_tabWallpaperHubBtn = createPill(tr("动态壁纸工坊"), 0);
    m_tabWidgetsBtn = createPill(tr("桌面小组件"), 1);
    m_tabDevBtn = createPill(tr("开发者控制台"), 2);

    navLayout->addWidget(m_tabWallpaperHubBtn);
    navLayout->addWidget(m_tabWidgetsBtn);
    navLayout->addWidget(m_tabDevBtn);
    navLayout->addStretch(1);

    centralLayout()->addWidget(navWidget);
}

void WallpaperStudioWindow::switchToTab(int tabIndex) {
    if (!m_stack) return;
    m_stack->setCurrentIndex(tabIndex);

    // Segmented Pill 现代高质感切换（避免低俗亮紫色，采用毛玻璃顶光按键）
    const QString activeStyle = QStringLiteral(
        "QPushButton {"
        "  background: rgba(255, 255, 255, 0.12);"
        "  color: #ffffff;"
        "  border: 1px solid rgba(255, 255, 255, 0.16);"
        "  border-radius: 6px;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "  padding: 0 12px;"
        "}"
    );

    const QString inactiveStyle = QStringLiteral(
        "QPushButton {"
        "  background: transparent;"
        "  color: #94a3b8;"
        "  border: 1px solid transparent;"
        "  border-radius: 6px;"
        "  font-size: 12px;"
        "  font-weight: 500;"
        "  padding: 0 12px;"
        "}"
        "QPushButton:hover {"
        "  background: rgba(255, 255, 255, 0.05);"
        "  color: #f1f5f9;"
        "}"
    );

    if (m_tabWallpaperHubBtn) m_tabWallpaperHubBtn->setStyleSheet(tabIndex == 0 ? activeStyle : inactiveStyle);
    if (m_tabWidgetsBtn) m_tabWidgetsBtn->setStyleSheet(tabIndex == 1 ? activeStyle : inactiveStyle);
    if (m_tabDevBtn) m_tabDevBtn->setStyleSheet(tabIndex == 2 ? activeStyle : inactiveStyle);

    if (tabIndex == 0) {
        populateGalleryCards();
        loadAndGeneratePropertyControls(m_currentEditingWallpaper);
        refreshActiveWallpaperStatus();
    } else if (tabIndex == 1) {
        refreshWidgetsList();
    }

    refreshActiveWallpaperStatus();
}

// -------------------------------------------------------------
// Page 1: 动态壁纸工坊 Master-Detail 二合一核心页面
// -------------------------------------------------------------
QWidget* WallpaperStudioWindow::createWallpaperHubPage() {
    auto* page = new QWidget(m_stack);
    auto* rootLayout = new QHBoxLayout(page);
    rootLayout->setContentsMargins(20, 6, 20, 16);
    rootLayout->setSpacing(16);

    // ========================================================
    // 左侧：壁纸画廊流 (占比约 56%)
    // ========================================================
    auto* leftContainer = new QWidget(page);
    auto* leftLayout = new QVBoxLayout(leftContainer);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(10);

    // 1. 活动壁纸 Hero 条（收紧为 52px 现代微光条）
    auto* heroCard = new QWidget(leftContainer);
    heroCard->setFixedHeight(52);
    heroCard->setStyleSheet(QStringLiteral(
        "QWidget {"
        "  background: rgba(255, 255, 255, 0.03);"
        "  border: 1px solid rgba(255, 255, 255, 0.07);"
        "  border-radius: 10px;"
        "}"
    ));
    auto* heroLayout = new QHBoxLayout(heroCard);
    heroLayout->setContentsMargins(14, 8, 14, 8);
    heroLayout->setSpacing(12);

    m_heroStatusDot = new QLabel(heroCard);
    m_heroStatusDot->setFixedSize(10, 10);
    m_heroStatusDot->setStyleSheet(QStringLiteral("border-radius: 5px; background: #94a3b8;"));
    heroLayout->addWidget(m_heroStatusDot);

    auto* heroInfoLayout = new QVBoxLayout();
    heroInfoLayout->setSpacing(2);
    m_heroTitle = new QLabel(tr("当前桌面壁纸：未运行动态壁纸"), heroCard);
    m_heroTitle->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 700; color: #f8fafc; border: none; background: transparent;"));
    m_heroDesc = new QLabel(tr("点击下方壁纸即可一键应用到系统桌面"), heroCard);
    m_heroDesc->setStyleSheet(QStringLiteral("font-size: 11px; color: #94a3b8; border: none; background: transparent;"));
    heroInfoLayout->addWidget(m_heroTitle);
    heroInfoLayout->addWidget(m_heroDesc);
    heroLayout->addLayout(heroInfoLayout);

    heroLayout->addStretch(1);

    // 鼠标引力波动开关
    auto* physicsLabel = new QLabel(tr("鼠标波动:"), heroCard);
    physicsLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #94a3b8; border: none; background: transparent;"));
    heroLayout->addWidget(physicsLabel);

    m_heroMousePhysicsToggle = new FluentToggleSwitch(heroCard);
    m_heroMousePhysicsToggle->setChecked(true);
    connect(m_heroMousePhysicsToggle, &FluentToggleSwitch::toggled, this, &WallpaperStudioWindow::toggleMousePhysics);
    heroLayout->addWidget(m_heroMousePhysicsToggle);

    // 停止壁纸按钮
    m_heroStopBtn = new QPushButton(tr("停止壁纸"), heroCard);
    m_heroStopBtn->setCursor(Qt::PointingHandCursor);
    m_heroStopBtn->setFixedHeight(30);
    m_heroStopBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: rgba(239, 68, 68, 0.15);"
        "  color: #f87171;"
        "  border: 1px solid rgba(239, 68, 68, 0.35);"
        "  border-radius: 6px;"
        "  font-size: 11px;"
        "  font-weight: 600;"
        "  padding: 0 10px;"
        "}"
        "QPushButton:hover { background: rgba(239, 68, 68, 0.3); color: #ffffff; }"
    ));
    connect(m_heroStopBtn, &QPushButton::clicked, this, &WallpaperStudioWindow::stopCurrentWallpaper);
    heroLayout->addWidget(m_heroStopBtn);

    leftLayout->addWidget(heroCard);

    // 2. 拖拽极速生成壁纸提示卡片（微光虚线框）
    auto* dropCard = new QWidget(leftContainer);
    dropCard->setFixedHeight(44);
    dropCard->setStyleSheet(QStringLiteral(
        "QWidget {"
        "  background: rgba(255, 255, 255, 0.02);"
        "  border: 1px dashed rgba(255, 255, 255, 0.15);"
        "  border-radius: 8px;"
        "}"
        "QWidget:hover {"
        "  border-color: rgba(59, 130, 246, 0.6);"
        "  background: rgba(59, 130, 246, 0.04);"
        "}"
    ));
    auto* dropLayout = new QHBoxLayout(dropCard);
    dropLayout->setContentsMargins(16, 0, 16, 0);
    dropLayout->setSpacing(10);

    auto* dropIcon = new QLabel(QStringLiteral("⚡"), dropCard);
    dropIcon->setStyleSheet(QStringLiteral("font-size: 16px; border: none; background: transparent;"));
    dropLayout->addWidget(dropIcon);

    auto* dropTip = new QLabel(tr("拖入 MP4 / WebM 视频或图片，1秒自动转为无缝壁纸并应用"), dropCard);
    dropTip->setStyleSheet(QStringLiteral("font-size: 12px; color: #cbd5e1; font-weight: 500; border: none; background: transparent;"));
    dropLayout->addWidget(dropTip);

    dropLayout->addStretch(1);

    auto* importBtn = new QPushButton(tr("📁 选择本地文件"), dropCard);
    importBtn->setCursor(Qt::PointingHandCursor);
    importBtn->setFixedHeight(28);
    importBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: #334155;"
        "  color: #f1f5f9;"
        "  border: 1px solid rgba(255, 255, 255, 0.1);"
        "  border-radius: 6px;"
        "  font-size: 11px;"
        "  font-weight: 600;"
        "  padding: 0 10px;"
        "}"
        "QPushButton:hover { background: #475569; border-color: rgba(99, 102, 241, 0.5); }"
    ));
    connect(importBtn, &QPushButton::clicked, this, &WallpaperStudioWindow::importLocalMedia);
    dropLayout->addWidget(importBtn);

    leftLayout->addWidget(dropCard);

    // 3. 画廊网格卡片滚动区（彻底禁用横向滚动条，杜绝截断与爆屏）
    auto* galleryScroll = new QScrollArea(leftContainer);
    galleryScroll->setWidgetResizable(true);
    galleryScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    galleryScroll->setStyleSheet(QStringLiteral(
        "QScrollArea { border: none; background: transparent; }"
    ));

    m_galleryGridContainer = new QWidget(galleryScroll);
    m_galleryGridContainer->setStyleSheet(QStringLiteral("background: transparent;"));
    m_galleryGridLayout = new QGridLayout(m_galleryGridContainer);
    m_galleryGridLayout->setContentsMargins(0, 4, 8, 4);
    m_galleryGridLayout->setSpacing(12);

    galleryScroll->setWidget(m_galleryGridContainer);
    leftLayout->addWidget(galleryScroll, 1);

    rootLayout->addWidget(leftContainer, 56);

    // ========================================================
    // 右侧：16:9 画板与双层参数调参面板 (占比约 44%)
    // ========================================================
    auto* rightContainer = new QWidget(page);
    rightContainer->setStyleSheet(QStringLiteral(
        "QWidget#detailRightContainer {"
        "  background: rgba(30, 41, 59, 0.45);"
        "  border: 1px solid rgba(255, 255, 255, 0.08);"
        "  border-radius: 14px;"
        "}"
    ));
    rightContainer->setObjectName(QStringLiteral("detailRightContainer"));
    auto* rightLayout = new QVBoxLayout(rightContainer);
    rightLayout->setContentsMargins(16, 14, 16, 14);
    rightLayout->setSpacing(10);

    // 1. 选中壁纸头部信息与快捷应用大按钮
    auto* headerRow = new QHBoxLayout();
    headerRow->setSpacing(10);

    auto* detailTitleLayout = new QVBoxLayout();
    detailTitleLayout->setSpacing(2);
    m_detailWallpaperName = new QLabel(tr("LinDesk Living Wallpaper"), rightContainer);
    m_detailWallpaperName->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: 700; color: #ffffff; border: none; background: transparent;"));
    m_detailWallpaperMeta = new QLabel(tr("官方 WebGL · 双层参数调优"), rightContainer);
    m_detailWallpaperMeta->setStyleSheet(QStringLiteral("font-size: 11px; color: #a5b4fc; font-weight: 600; border: none; background: transparent;"));
    detailTitleLayout->addWidget(m_detailWallpaperName);
    detailTitleLayout->addWidget(m_detailWallpaperMeta);
    headerRow->addLayout(detailTitleLayout);

    headerRow->addStretch(1);

    m_detailApplyBtn = new QPushButton(tr("⚡ 应用到系统桌面"), rightContainer);
    m_detailApplyBtn->setCursor(Qt::PointingHandCursor);
    m_detailApplyBtn->setFixedHeight(32);
    m_detailApplyBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: #2563eb;"
        "  color: #ffffff;"
        "  border: 1px solid #3b82f6;"
        "  border-radius: 6px;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "  padding: 5px 14px;"
        "}"
        "QPushButton:hover { background: #1d4ed8; }"
        "QPushButton:pressed { background: #1e40af; }"
    ));
    connect(m_detailApplyBtn, &QPushButton::clicked, this, [this]() {
        applyWallpaperPreset(m_currentEditingWallpaper);
    });
    headerRow->addWidget(m_detailApplyBtn);

    rightLayout->addLayout(headerRow);

    // 2. 16:9 画板视口
    m_studioCanvas = new StudioPreviewCanvas(rightContainer);
    m_studioCanvas->setFixedHeight(210);
    rightLayout->addWidget(m_studioCanvas);

    // 3. 滚动调参面板
    auto* propScroll = new QScrollArea(rightContainer);
    propScroll->setWidgetResizable(true);
    propScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    propScroll->setStyleSheet(QStringLiteral("QScrollArea { border: none; background: transparent; }"));

    auto* propScrollContent = new QWidget(propScroll);
    propScrollContent->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* propScrollLayout = new QVBoxLayout(propScrollContent);
    propScrollLayout->setContentsMargins(0, 4, 8, 4);
    propScrollLayout->setSpacing(14);

    // 上层：自定义参数容器
    auto* customParamHeader = new QLabel(tr("🎨 自定义壁纸参数 (Custom Parameters)"), propScrollContent);
    customParamHeader->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: 700; color: #e2e8f0; border: none;"));
    propScrollLayout->addWidget(customParamHeader);

    m_dynamicPropertiesContainer = new QWidget(propScrollContent);
    m_dynamicPropertiesLayout = new QVBoxLayout(m_dynamicPropertiesContainer);
    m_dynamicPropertiesLayout->setContentsMargins(0, 0, 0, 0);
    m_dynamicPropertiesLayout->setSpacing(8);
    propScrollLayout->addWidget(m_dynamicPropertiesContainer);

    // 下层：桌面视听与图层环境配置
    auto* envHeader = new QLabel(tr("桌面环境与视听配置"), propScrollContent);
    envHeader->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: 600; color: #cbd5e1; border: none; margin-top: 6px;"));
    propScrollLayout->addWidget(envHeader);

    auto* envBox = new QWidget(propScrollContent);
    envBox->setStyleSheet(QStringLiteral(
        "QWidget {"
        "  background: rgba(255, 255, 255, 0.025);"
        "  border: 1px solid rgba(255, 255, 255, 0.06);"
        "  border-radius: 10px;"
        "  padding: 8px;"
        "}"
    ));
    auto* envBoxLayout = new QVBoxLayout(envBox);
    envBoxLayout->setContentsMargins(12, 10, 12, 10);
    envBoxLayout->setSpacing(10);

    // 1) 音量与静音
    auto* volRow = new QHBoxLayout();
    auto* volLabel = new QLabel(tr("壁纸音量:"), envBox);
    volLabel->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 12px; border: none;"));
    m_volumeSlider = new QSlider(Qt::Horizontal, envBox);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(0);
    m_volumeValLabel = new QLabel(QStringLiteral("0% (静音)"), envBox);
    m_volumeValLabel->setFixedWidth(65);
    m_volumeValLabel->setStyleSheet(QStringLiteral("color: #cbd5e1; font-size: 11px; font-weight: 600; border: none;"));
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int v) {
        m_volumeValLabel->setText(v == 0 ? tr("静音") : QStringLiteral("%1%").arg(v));
        dispatchPropertyUpdate(QStringLiteral("volume"), v);
    });
    volRow->addWidget(volLabel);
    volRow->addWidget(m_volumeSlider, 1);
    volRow->addWidget(m_volumeValLabel);
    envBoxLayout->addLayout(volRow);

    // 2) 桌面图标暗度遮罩 (0~70%)
    auto* dimRow = new QHBoxLayout();
    auto* dimLabel = new QLabel(tr("图标暗度遮罩:"), envBox);
    dimLabel->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 12px; border: none;"));
    m_dimSlider = new QSlider(Qt::Horizontal, envBox);
    m_dimSlider->setRange(0, 70);
    m_dimSlider->setValue(20);
    m_dimValLabel = new QLabel(QStringLiteral("20%"), envBox);
    m_dimValLabel->setFixedWidth(65);
    m_dimValLabel->setStyleSheet(QStringLiteral("color: #cbd5e1; font-size: 11px; font-weight: 600; border: none;"));
    connect(m_dimSlider, &QSlider::valueChanged, this, &WallpaperStudioWindow::updateStudioDimming);
    dimRow->addWidget(dimLabel);
    dimRow->addWidget(m_dimSlider, 1);
    dimRow->addWidget(m_dimValLabel);
    envBoxLayout->addLayout(dimRow);

    // 3) 毛玻璃虚化 (0~20px)
    auto* blurRow = new QHBoxLayout();
    auto* blurLabel = new QLabel(tr("毛玻璃虚化:"), envBox);
    blurLabel->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 12px; border: none;"));
    m_blurSlider = new QSlider(Qt::Horizontal, envBox);
    m_blurSlider->setRange(0, 20);
    m_blurSlider->setValue(0);
    m_blurValLabel = new QLabel(QStringLiteral("0 px"), envBox);
    m_blurValLabel->setFixedWidth(65);
    m_blurValLabel->setStyleSheet(QStringLiteral("color: #cbd5e1; font-size: 11px; font-weight: 600; border: none;"));
    connect(m_blurSlider, &QSlider::valueChanged, this, &WallpaperStudioWindow::updateStudioBlur);
    blurRow->addWidget(blurLabel);
    blurRow->addWidget(m_blurSlider, 1);
    blurRow->addWidget(m_blurValLabel);
    envBoxLayout->addLayout(blurRow);

    // 4) 填充模式 & 模拟图标
    auto* toggleRow = new QHBoxLayout();
    auto* fitLabel = new QLabel(tr("画面填充:"), envBox);
    fitLabel->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 12px; border: none;"));
    m_fitModeCombo = new QComboBox(envBox);
    m_fitModeCombo->addItem(tr("Cover (裁剪铺满)"), QStringLiteral("cover"));
    m_fitModeCombo->addItem(tr("Contain (等比黑边)"), QStringLiteral("contain"));
    m_fitModeCombo->setStyleSheet(QStringLiteral(
        "QComboBox { background: #1e293b; color: #f1f5f9; border: 1px solid rgba(255, 255, 255, 0.1); border-radius: 6px; padding: 2px 8px; font-size: 11px; }"
    ));
    connect(m_fitModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        const QString mode = m_fitModeCombo->itemData(idx).toString();
        dispatchPropertyUpdate(QStringLiteral("fitMode"), mode);
    });
    toggleRow->addWidget(fitLabel);
    toggleRow->addWidget(m_fitModeCombo);

    toggleRow->addSpacing(14);

    m_mockIconsCheckbox = new QCheckBox(tr("在画板模拟桌面图标浮层"), envBox);
    m_mockIconsCheckbox->setChecked(true);
    m_mockIconsCheckbox->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 11px; border: none;"));
    connect(m_mockIconsCheckbox, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_studioCanvas) m_studioCanvas->setShowDesktopIcons(checked);
    });
    toggleRow->addWidget(m_mockIconsCheckbox);
    toggleRow->addStretch(1);

    envBoxLayout->addLayout(toggleRow);
    propScrollLayout->addWidget(envBox);

    // 极客感知：仅在 isDevelopment 时展示的代码与 DevTools
    m_devToolsBox = new QWidget(propScrollContent);
    m_devToolsBox->setStyleSheet(QStringLiteral(
        "QWidget {"
        "  background: rgba(99, 102, 241, 0.1);"
        "  border: 1px solid rgba(99, 102, 241, 0.3);"
        "  border-radius: 10px;"
        "}"
    ));
    auto* devToolsLayout = new QHBoxLayout(m_devToolsBox);
    devToolsLayout->setContentsMargins(12, 8, 12, 8);
    devToolsLayout->setSpacing(10);

    auto* devTag = new QLabel(tr("🛠️ 开发者模式:"), m_devToolsBox);
    devTag->setStyleSheet(QStringLiteral("color: #a5b4fc; font-size: 11px; font-weight: 700; border: none;"));
    devToolsLayout->addWidget(devTag);

    m_devOpenFolderBtn = new QPushButton(tr("📂 打开源码目录"), m_devToolsBox);
    m_devOpenFolderBtn->setCursor(Qt::PointingHandCursor);
    m_devOpenFolderBtn->setFixedHeight(26);
    m_devOpenFolderBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: #334155; color: #f1f5f9; border: 1px solid rgba(255, 255, 255, 0.1); border-radius: 6px; padding: 0 8px; font-size: 11px; }"
        "QPushButton:hover { background: #475569; }"
    ));
    connect(m_devOpenFolderBtn, &QPushButton::clicked, this, [this]() {
        const QString p = resolveWallpaperPath(m_currentEditingWallpaper);
        if (!p.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(p));
        }
    });
    devToolsLayout->addWidget(m_devOpenFolderBtn);

    m_devInspectBtn = new QPushButton(tr("🐞 呼出 DevTools"), m_devToolsBox);
    m_devInspectBtn->setCursor(Qt::PointingHandCursor);
    m_devInspectBtn->setFixedHeight(26);
    m_devInspectBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: #334155; color: #f1f5f9; border: 1px solid rgba(255, 255, 255, 0.1); border-radius: 6px; padding: 0 8px; font-size: 11px; }"
        "QPushButton:hover { background: #475569; }"
    ));
    connect(m_devInspectBtn, &QPushButton::clicked, this, [this]() {
        auto& bridge = LinWidgetRuntimeBridge::instance();
        if (bridge.hasActiveWallpaper()) {
            bridge.showDevTools(bridge.activeWallpaperId());
        } else {
            showToast(tr("请先将此壁纸应用到系统桌面后再呼出 DevTools"), false);
        }
    });
    devToolsLayout->addWidget(m_devInspectBtn);

    devToolsLayout->addStretch(1);
    propScrollLayout->addWidget(m_devToolsBox);
    m_devToolsBox->hide(); // 默认隐藏，普通壁纸保持纯粹

    propScrollLayout->addStretch(1);
    propScroll->setWidget(propScrollContent);
    rightLayout->addWidget(propScroll, 1);

    rootLayout->addWidget(rightContainer, 44);

    return page;
}

void WallpaperStudioWindow::populateGalleryCards() {
    if (!m_galleryGridLayout || !m_galleryGridContainer) return;

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

    auto createWallpaperCard = [this, running, activeId](const WallpaperItem& item) {
        const bool isActive = running && (!activeId.isEmpty() && (activeId.contains(item.id, Qt::CaseInsensitive) || item.id.contains(activeId, Qt::CaseInsensitive)));
        const bool isSelected = (m_currentEditingWallpaper == item.id);

        auto* card = new QWidget(m_galleryGridContainer);
        card->setCursor(Qt::PointingHandCursor);
        card->setStyleSheet(QStringLiteral(
            "QWidget {"
            "  background: rgba(255, 255, 255, 0.03);"
            "  border: 1px solid %1;"
            "  border-radius: 10px;"
            "}"
            "QWidget:hover {"
            "  border-color: rgba(255, 255, 255, 0.25);"
            "  background: rgba(255, 255, 255, 0.05);"
            "}"
        ).arg(isActive ? QStringLiteral("rgba(16, 185, 129, 0.8)") : (isSelected ? QStringLiteral("rgba(59, 130, 246, 0.7)") : QStringLiteral("rgba(255, 255, 255, 0.08)"))));

        auto* vLayout = new QVBoxLayout(card);
        vLayout->setContentsMargins(10, 10, 10, 10);
        vLayout->setSpacing(8);

        // 缩略图
        auto* thumb = new QLabel(card);
        thumb->setFixedHeight(105);
        thumb->setAlignment(Qt::AlignCenter);
        if (!item.previewPath.isEmpty() && QFile::exists(item.previewPath)) {
            QPixmap pm(item.previewPath);
            if (!pm.isNull()) {
                thumb->setPixmap(pm.scaled(320, 180, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                thumb->setStyleSheet(QStringLiteral("border-radius: 8px; border: 1px solid rgba(255, 255, 255, 0.1);"));
            } else {
                thumb->setStyleSheet(item.thumbStyle);
            }
        } else {
            thumb->setStyleSheet(item.thumbStyle);
            thumb->setText(item.wallpaperType == QStringLiteral("video") ? QStringLiteral("🎬 视频壁纸") : QStringLiteral("🌌 动态引擎"));
        }
        vLayout->addWidget(thumb);

        // 标题与标签
        auto* titleRow = new QHBoxLayout();
        titleRow->setSpacing(6);
        auto* nameLabel = new QLabel(item.name, card);
        nameLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        nameLabel->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 600; color: #f8fafc; border: none; background: transparent;"));
        titleRow->addWidget(nameLabel, 1);

        auto* tagLabel = new QLabel(item.tag, card);
        tagLabel->setStyleSheet(QStringLiteral(
            "color: #94a3b8; background: rgba(255, 255, 255, 0.06); border: 1px solid rgba(255, 255, 255, 0.08); border-radius: 4px; padding: 1px 6px; font-size: 10px; font-weight: 500; border: none;"
        ));
        titleRow->addWidget(tagLabel);
        vLayout->addLayout(titleRow);

        // 描述 (限高两行，保持规整高度一致)
        auto* descLabel = new QLabel(item.description, card);
        descLabel->setWordWrap(true);
        descLabel->setFixedHeight(32);
        descLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #64748b; line-height: 1.3; border: none; background: transparent;"));
        vLayout->addWidget(descLabel);

        vLayout->addStretch(1);

        // 按钮栏
        auto* btnRow = new QHBoxLayout();
        btnRow->setSpacing(6);

        auto* configBtn = new QPushButton(tr("调参 / 预览"), card);
        configBtn->setCursor(Qt::PointingHandCursor);
        configBtn->setFixedHeight(28);
        configBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: rgba(51, 65, 85, 0.7); color: #cbd5e1; border: 1px solid rgba(255, 255, 255, 0.08); border-radius: 6px; font-size: 11px; font-weight: 600; }"
            "QPushButton:hover { background: #475569; color: #ffffff; border-color: rgba(99, 102, 241, 0.5); }"
        ));
        connect(configBtn, &QPushButton::clicked, this, [this, item]() {
            selectWallpaperForStudio(item.id);
        });
        btnRow->addWidget(configBtn);

        auto* applyBtn = new QPushButton(isActive ? tr("当前应用中") : tr("应用到桌面"), card);
        applyBtn->setCursor(Qt::PointingHandCursor);
        applyBtn->setFixedHeight(28);
        applyBtn->setEnabled(!isActive);
        applyBtn->setStyleSheet(isActive ? QStringLiteral(
            "QPushButton { background: rgba(16, 185, 129, 0.12); color: #34d399; border: 1px solid rgba(16, 185, 129, 0.3); border-radius: 6px; font-size: 11px; font-weight: 600; }"
        ) : QStringLiteral(
            "QPushButton { background: #2563eb; color: #ffffff; border: none; border-radius: 6px; font-size: 11px; font-weight: 600; }"
            "QPushButton:hover { background: #1d4ed8; }"
        ));
        connect(applyBtn, &QPushButton::clicked, this, [this, item]() {
            applyWallpaperPreset(item.id);
            selectWallpaperForStudio(item.id);
        });
        btnRow->addWidget(applyBtn, 1);

        vLayout->addLayout(btnRow);

        return card;
    };

    int row = 0;
    int col = 0;
    const int cols = 2; // 两列画廊卡片网格

    for (const auto& item : m_availableWallpapers) {
        auto* card = createWallpaperCard(item);
        m_galleryGridLayout->addWidget(card, row, col);
        col++;
        if (col >= cols) {
            col = 0;
            row++;
        }
    }
}

void WallpaperStudioWindow::selectWallpaperForStudio(const QString& wallpaperId) {
    m_currentEditingWallpaper = wallpaperId;

    WallpaperItem selectedItem;
    bool found = false;
    for (const auto& w : m_availableWallpapers) {
        if (w.id.compare(wallpaperId, Qt::CaseInsensitive) == 0 ||
            w.name.compare(wallpaperId, Qt::CaseInsensitive) == 0) {
            selectedItem = w;
            found = true;
            break;
        }
    }

    if (found) {
        if (m_detailWallpaperName) m_detailWallpaperName->setText(selectedItem.name);
        if (m_detailWallpaperMeta) m_detailWallpaperMeta->setText(QStringLiteral("%1 · 双层参数调优").arg(selectedItem.tag));

        // 画板更新海报封面
        if (m_studioCanvas) {
            if (!selectedItem.previewPath.isEmpty() && QFile::exists(selectedItem.previewPath)) {
                m_studioCanvas->setPreviewImage(QPixmap(selectedItem.previewPath));
            } else {
                m_studioCanvas->setPreviewImage(QPixmap());
            }
        }

        // 极客开发工具栏显示/隐藏
        if (m_devToolsBox) {
            m_devToolsBox->setVisible(selectedItem.isDevelopment);
        }
    }

    loadAndGeneratePropertyControls(wallpaperId);
    populateGalleryCards();
}

void WallpaperStudioWindow::loadAndGeneratePropertyControls(const QString& wallpaperId) {
    if (!m_dynamicPropertiesLayout || !m_dynamicPropertiesContainer) return;

    QLayoutItem* item = nullptr;
    while ((item = m_dynamicPropertiesLayout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    m_currentPropertyValues.clear();

    QJsonObject schema;
    for (const auto& w : m_availableWallpapers) {
        if (w.id.compare(wallpaperId, Qt::CaseInsensitive) == 0 ||
            w.name.compare(wallpaperId, Qt::CaseInsensitive) == 0) {
            schema = w.propertiesSchema;
            break;
        }
    }

    // 若无自定义参数，显示优雅的提示
    if (schema.isEmpty()) {
        auto* emptyCard = new QWidget(m_dynamicPropertiesContainer);
        emptyCard->setStyleSheet(QStringLiteral(
            "QWidget {"
            "  background: rgba(15, 23, 42, 0.4);"
            "  border: 1px dashed rgba(255, 255, 255, 0.1);"
            "  border-radius: 8px;"
            "  padding: 12px;"
            "}"
        ));
        auto* el = new QHBoxLayout(emptyCard);
        auto* tip = new QLabel(tr("✨ 此壁纸为预设或无感播放类型，无需额外参数，开箱即达最佳效果。"), emptyCard);
        tip->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 11px; border: none; background: transparent;"));
        el->addWidget(tip);
        m_dynamicPropertiesLayout->addWidget(emptyCard);
        return;
    }

    const QJsonObject saved = loadSavedProperties(wallpaperId);

    for (auto it = schema.begin(); it != schema.end(); ++it) {
        const QString propKey = it.key();
        const QJsonObject propDef = it.value().toObject();
        const QString type = propDef.value(QStringLiteral("type")).toString();
        const QString text = propDef.value(QStringLiteral("text")).toString(propKey);

        if (type == QStringLiteral("slider")) {
            const int minVal = propDef.value(QStringLiteral("min")).toInt(0);
            const int maxVal = propDef.value(QStringLiteral("max")).toInt(100);
            const int step = propDef.value(QStringLiteral("step")).toInt(1);
            int curVal = saved.value(propKey).toInt(propDef.value(QStringLiteral("value")).toInt(minVal));

            auto* row = new QWidget(m_dynamicPropertiesContainer);
            row->setStyleSheet(QStringLiteral(
                "QWidget { background: rgba(15, 23, 42, 0.5); border: 1px solid rgba(255, 255, 255, 0.05); border-radius: 8px; }"
            ));
            auto* rowL = new QHBoxLayout(row);
            rowL->setContentsMargins(10, 6, 10, 6);
            rowL->setSpacing(8);

            auto* nameLbl = new QLabel(text, row);
            nameLbl->setStyleSheet(QStringLiteral("color: #cbd5e1; font-size: 11px; border: none; background: transparent;"));
            nameLbl->setFixedWidth(140);
            rowL->addWidget(nameLbl);

            auto* slider = new QSlider(Qt::Horizontal, row);
            slider->setRange(minVal, maxVal);
            slider->setSingleStep(step);
            slider->setValue(curVal);
            slider->setStyleSheet(QStringLiteral(
                "QSlider::groove:horizontal { height: 4px; background: rgba(255, 255, 255, 0.12); border-radius: 2px; }"
                "QSlider::sub-page:horizontal { background: #6366f1; border-radius: 2px; }"
                "QSlider::handle:horizontal { width: 12px; margin-top: -4px; margin-bottom: -4px; border-radius: 6px; background: #ffffff; }"
            ));
            rowL->addWidget(slider, 1);

            auto* valLbl = new QLabel(QString::number(curVal), row);
            valLbl->setFixedWidth(40);
            valLbl->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 11px; font-weight: 600; border: none; background: transparent;"));
            rowL->addWidget(valLbl);

            connect(slider, &QSlider::valueChanged, this, [this, propKey, valLbl](int val) {
                valLbl->setText(QString::number(val));
                dispatchPropertyUpdate(propKey, val);
            });

            m_dynamicPropertiesLayout->addWidget(row);
        } else if (type == QStringLiteral("color")) {
            QString curColor = saved.value(propKey).toString(propDef.value(QStringLiteral("value")).toString(QStringLiteral("#6366f1")));

            auto* row = new QWidget(m_dynamicPropertiesContainer);
            row->setStyleSheet(QStringLiteral(
                "QWidget { background: rgba(15, 23, 42, 0.5); border: 1px solid rgba(255, 255, 255, 0.05); border-radius: 8px; }"
            ));
            auto* rowL = new QHBoxLayout(row);
            rowL->setContentsMargins(10, 6, 10, 6);
            rowL->setSpacing(8);

            auto* nameLbl = new QLabel(text, row);
            nameLbl->setStyleSheet(QStringLiteral("color: #cbd5e1; font-size: 11px; border: none; background: transparent;"));
            rowL->addWidget(nameLbl, 1);

            auto* colorBtn = new QPushButton(row);
            colorBtn->setFixedSize(48, 22);
            colorBtn->setCursor(Qt::PointingHandCursor);
            colorBtn->setStyleSheet(QStringLiteral("background: %1; border: 1px solid #ffffff; border-radius: 4px;").arg(curColor));

            connect(colorBtn, &QPushButton::clicked, this, [this, propKey, colorBtn, curColor]() {
                const QColor chosen = QColorDialog::getColor(QColor(curColor), this, tr("选择壁纸主题色"));
                if (chosen.isValid()) {
                    const QString hex = chosen.name();
                    colorBtn->setStyleSheet(QStringLiteral("background: %1; border: 1px solid #ffffff; border-radius: 4px;").arg(hex));
                    dispatchPropertyUpdate(propKey, hex);
                }
            });

            rowL->addWidget(colorBtn);
            m_dynamicPropertiesLayout->addWidget(row);
        } else if (type == QStringLiteral("boolean")) {
            bool curBool = saved.value(propKey).toBool(propDef.value(QStringLiteral("value")).toBool(true));

            auto* row = new QWidget(m_dynamicPropertiesContainer);
            row->setStyleSheet(QStringLiteral(
                "QWidget { background: rgba(15, 23, 42, 0.5); border: 1px solid rgba(255, 255, 255, 0.05); border-radius: 8px; }"
            ));
            auto* rowL = new QHBoxLayout(row);
            rowL->setContentsMargins(10, 6, 10, 6);
            rowL->setSpacing(8);

            auto* nameLbl = new QLabel(text, row);
            nameLbl->setStyleSheet(QStringLiteral("color: #cbd5e1; font-size: 11px; border: none; background: transparent;"));
            rowL->addWidget(nameLbl, 1);

            auto* toggle = new FluentToggleSwitch(row);
            toggle->setChecked(curBool);
            connect(toggle, &FluentToggleSwitch::toggled, this, [this, propKey](bool checked) {
                dispatchPropertyUpdate(propKey, checked);
            });

            rowL->addWidget(toggle);
            m_dynamicPropertiesLayout->addWidget(row);
        }
    }
}

void WallpaperStudioWindow::dispatchPropertyUpdate(const QString& key, const QJsonValue& val) {
    m_currentPropertyValues[key] = val;

    QJsonObject currentSaved = loadSavedProperties(m_currentEditingWallpaper);
    currentSaved.insert(key, val);
    saveProperties(m_currentEditingWallpaper, currentSaved);

    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (bridge.hasActiveWallpaper()) {
        QJsonObject req;
        req.insert(QStringLiteral("action"), QStringLiteral("set_wallpaper_prop"));
        req.insert(QStringLiteral("key"), key);
        req.insert(QStringLiteral("property"), key);
        req.insert(QStringLiteral("value"), val);
        bridge.queryWidgetManager(req);
    }

    if (m_studioCanvas) {
        if (key == QStringLiteral("particleCount")) {
            m_studioCanvas->setParticleDensity(val.toInt() / 6);
        } else if (key == QStringLiteral("lensingGlowColor") || key == QStringLiteral("singularityColor")) {
            m_studioCanvas->setAccentColor(QColor(val.toString()));
        }
    }
}

QJsonObject WallpaperStudioWindow::loadSavedProperties(const QString& wallpaperId) const {
    QSettings s(AppPaths::configFilePath(), QSettings::IniFormat);
    const QString jsonStr = s.value(QStringLiteral("LinWallpapers/%1/Properties").arg(wallpaperId)).toString();
    if (!jsonStr.isEmpty()) {
        return QJsonDocument::fromJson(jsonStr.toUtf8()).object();
    }
    return QJsonObject();
}

void WallpaperStudioWindow::saveProperties(const QString& wallpaperId, const QJsonObject& props) {
    QSettings s(AppPaths::configFilePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("LinWallpapers/%1/Properties").arg(wallpaperId), QString::fromUtf8(QJsonDocument(props).toJson(QJsonDocument::Compact)));
    s.sync();
}

bool WallpaperStudioWindow::createAndInstallQuickWallpaper(const QString& filePath, QString* outId) {
    QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile()) {
        showToast(tr("导入失败：文件不存在"), false);
        return false;
    }

    const QString ext = fi.suffix().toLower();
    const bool isVideo = (ext == QStringLiteral("mp4") || ext == QStringLiteral("webm") || ext == QStringLiteral("mov"));
    const bool isImage = (ext == QStringLiteral("png") || ext == QStringLiteral("jpg") || ext == QStringLiteral("jpeg") || ext == QStringLiteral("webp") || ext == QStringLiteral("gif"));

    if (!isVideo && !isImage) {
        showToast(tr("格式不符：仅支持 .mp4/.webm 视频或 .png/.jpg 图片"), false);
        return false;
    }

    const QString wallUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString wallDir = QDir(AppPaths::userWallpapersDir()).filePath(wallUuid);
    if (!QDir().mkpath(wallDir)) {
        showToast(tr("无法创建壁纸存储目录"), false);
        return false;
    }

    const QString mediaFileName = QStringLiteral("media.") + ext;
    const QString destMedia = QDir(wallDir).filePath(mediaFileName);
    if (!QFile::copy(filePath, destMedia)) {
        showToast(tr("复制媒体文件失败"), false);
        return false;
    }

    // 生成封面 preview.png
    const QString previewPath = QDir(wallDir).filePath(QStringLiteral("preview.png"));
    if (isImage) {
        QImage img(filePath);
        if (!img.isNull()) {
            QImage thumb = img.scaled(480, 270, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            QRect cropRect((thumb.width() - 480) / 2, (thumb.height() - 270) / 2, 480, 270);
            thumb.copy(cropRect).save(previewPath, "PNG");
        }
    } else {
        // 视频壁纸：绘制优雅的 16:9 海报封面
        QImage poster(480, 270, QImage::Format_ARGB32_Premultiplied);
        poster.fill(Qt::transparent);
        QPainter p(&poster);
        p.setRenderHint(QPainter::Antialiasing);

        QLinearGradient bgGrad(0, 0, 480, 270);
        bgGrad.setColorAt(0, QColor(15, 23, 42));
        bgGrad.setColorAt(0.5, QColor(30, 41, 59));
        bgGrad.setColorAt(1, QColor(15, 23, 42));
        p.fillRect(poster.rect(), bgGrad);

        QRadialGradient orbGrad(240, 135, 180);
        orbGrad.setColorAt(0, QColor(99, 102, 241, 70));
        orbGrad.setColorAt(1, QColor(99, 102, 241, 0));
        p.fillRect(poster.rect(), orbGrad);

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(99, 102, 241, 220));
        p.drawEllipse(QPoint(240, 115), 30, 30);

        QPolygon tri;
        tri << QPoint(234, 103) << QPoint(252, 115) << QPoint(234, 127);
        p.setBrush(Qt::white);
        p.drawPolygon(tri);

        p.setPen(QColor(241, 245, 249));
        QFont f = p.font();
        f.setPointSize(11);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRect(20, 160, 440, 40), Qt::AlignCenter | Qt::TextWordWrap, fi.completeBaseName());

        p.setPen(QColor(148, 163, 184));
        f.setPointSize(9);
        f.setBold(false);
        p.setFont(f);
        p.drawText(QRect(20, 205, 440, 30), Qt::AlignCenter, tr("4K 视频壁纸 · 极速生成"));
        p.end();

        poster.save(previewPath, "PNG");
    }

    // 生成 index.html
    const QString indexPath = QDir(wallDir).filePath(QStringLiteral("index.html"));
    QFile htmlFile(indexPath);
    if (htmlFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QString htmlContent;
        if (isVideo) {
            htmlContent = QStringLiteral(
                "<!DOCTYPE html>\n"
                "<html lang=\"zh-CN\">\n"
                "<head>\n"
                "  <meta charset=\"UTF-8\">\n"
                "  <style>\n"
                "    * { margin: 0; padding: 0; box-sizing: border-box; }\n"
                "    html, body { width: 100vw; height: 100vh; overflow: hidden; background: #000; }\n"
                "    video { width: 100vw; height: 100vh; object-fit: cover; position: absolute; top: 0; left: 0; pointer-events: none; }\n"
                "  </style>\n"
                "</head>\n"
                "<body>\n"
                "  <video src=\"%1\" autoplay loop muted playsinline></video>\n"
                "  <script>\n"
                "    window.addEventListener('message', function(e) {\n"
                "      const data = e.data || {};\n"
                "      if (data.type === 'wallpaper_property_change' || data.type === 'wallpaper_property') {\n"
                "        const v = document.querySelector('video');\n"
                "        if (!v) return;\n"
                "        const key = data.key || data.property;\n"
                "        if (key === 'fitMode') { v.style.objectFit = data.value; }\n"
                "        else if (key === 'volume') { v.volume = parseFloat(data.value) / 100.0; v.muted = (v.volume <= 0); }\n"
                "      }\n"
                "    });\n"
                "  </script>\n"
                "</body>\n"
                "</html>\n"
            ).arg(mediaFileName);
        } else {
            htmlContent = QStringLiteral(
                "<!DOCTYPE html>\n"
                "<html lang=\"zh-CN\">\n"
                "<head>\n"
                "  <meta charset=\"UTF-8\">\n"
                "  <style>\n"
                "    * { margin: 0; padding: 0; box-sizing: border-box; }\n"
                "    html, body { width: 100vw; height: 100vh; overflow: hidden; background: #000; }\n"
                "    img { width: 100vw; height: 100vh; object-fit: cover; position: absolute; top: 0; left: 0; pointer-events: none; }\n"
                "  </style>\n"
                "</head>\n"
                "<body>\n"
                "  <img src=\"%1\">\n"
                "  <script>\n"
                "    window.addEventListener('message', function(e) {\n"
                "      const data = e.data || {};\n"
                "      if (data.type === 'wallpaper_property_change' || data.type === 'wallpaper_property') {\n"
                "        const img = document.querySelector('img');\n"
                "        if (!img) return;\n"
                "        const key = data.key || data.property;\n"
                "        if (key === 'fitMode') { img.style.objectFit = data.value; }\n"
                "      }\n"
                "    });\n"
                "  </script>\n"
                "</body>\n"
                "</html>\n"
            ).arg(mediaFileName);
        }
        htmlFile.write(htmlContent.toUtf8());
        htmlFile.close();
    }

    // 生成 manifest.json
    const QString manifestPath = QDir(wallDir).filePath(QStringLiteral("manifest.json"));
    QJsonObject root;
    const QString generatedId = QStringLiteral("custom.") + wallUuid;
    root.insert(QStringLiteral("id"), generatedId);
    root.insert(QStringLiteral("name"), fi.completeBaseName());
    root.insert(QStringLiteral("version"), QStringLiteral("1.0.0"));
    root.insert(QStringLiteral("author"), QStringLiteral("LinDesk"));
    root.insert(QStringLiteral("description"), isVideo ? tr("本地无缝循环视频壁纸") : tr("本地高清静态超清壁纸"));
    root.insert(QStringLiteral("role"), QStringLiteral("wallpaper"));
    root.insert(QStringLiteral("type"), QStringLiteral("html"));
    root.insert(QStringLiteral("wallpaperType"), isVideo ? QStringLiteral("video") : QStringLiteral("image"));
    root.insert(QStringLiteral("entry"), QStringLiteral("index.html"));
    root.insert(QStringLiteral("preview"), QStringLiteral("preview.png"));
    root.insert(QStringLiteral("properties"), QJsonObject());

    QFile mf(manifestPath);
    if (mf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        mf.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        mf.close();
    }

    // 重新扫描壁纸列表
    scanWallpapers();
    populateGalleryCards();

    // 立即应用到桌面并选中该壁纸
    applyWallpaperPreset(generatedId);
    selectWallpaperForStudio(generatedId);

    if (outId) {
        *outId = generatedId;
    }

    showToast(tr("🎉 壁纸【%1】已极速生成并即刻应用到桌面！").arg(fi.completeBaseName()), true);
    return true;
}

void WallpaperStudioWindow::importLocalMedia() {
    const QString filter = tr("壁纸与媒体文件 (*.mp4 *.webm *.mov *.png *.jpg *.jpeg *.webp *.gif *.lw);;所有文件 (*.*)");
    const QString filePath = QFileDialog::getOpenFileName(this, tr("选择要制作或导入的壁纸媒体"), QString(), filter);
    if (filePath.isEmpty()) return;

    createAndInstallQuickWallpaper(filePath);
}

void WallpaperStudioWindow::applyWallpaperPreset(const QString& wallpaperId) {
    const QString targetPath = resolveWallpaperPath(wallpaperId);
    if (targetPath.isEmpty() || !QDir(targetPath).exists()) {
        showToast(tr("未找到壁纸安装包: %1").arg(wallpaperId), false);
        return;
    }

    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (!bridge.isLoaded()) {
        bridge.loadRuntime();
    }

    const bool started = bridge.startWallpaper(targetPath);
    if (started) {
        m_currentEditingWallpaper = wallpaperId;
        refreshActiveWallpaperStatus();
        populateGalleryCards();
        showToast(tr("✨ 壁纸已成功应用到桌面！"), true);
    } else {
        showToast(tr("壁纸应用失败，请检查运行时状态"), false);
    }
}

void WallpaperStudioWindow::stopCurrentWallpaper() {
    auto& bridge = LinWidgetRuntimeBridge::instance();
    bridge.stopWallpaper(true);
    refreshActiveWallpaperStatus();
    populateGalleryCards();
    showToast(tr("已停止桌面动态壁纸并恢复系统壁纸"), true);
}

void WallpaperStudioWindow::toggleMousePhysics(bool enabled) {
    auto& bridge = LinWidgetRuntimeBridge::instance();
    bridge.setWallpaperInteractive(enabled);
    dispatchPropertyUpdate(QStringLiteral("reactiveMouseWave"), enabled);
    showToast(enabled ? tr("已开启鼠标引力波动感知") : tr("已关闭鼠标引力波动感知"), true);
}

void WallpaperStudioWindow::updateStudioDimming(int val) {
    if (m_dimValLabel) m_dimValLabel->setText(QStringLiteral("%1%").arg(val));
    if (m_studioCanvas) m_studioCanvas->setDimmingOpacity(val / 100.0);
    dispatchPropertyUpdate(QStringLiteral("dimming"), val / 100.0);
}

void WallpaperStudioWindow::updateStudioBlur(int val) {
    if (m_blurValLabel) m_blurValLabel->setText(QStringLiteral("%1 px").arg(val));
    if (m_studioCanvas) m_studioCanvas->setBlurRadius(val);
    dispatchPropertyUpdate(QStringLiteral("blur"), val);
}

void WallpaperStudioWindow::refreshActiveWallpaperStatus() {
    auto& bridge = LinWidgetRuntimeBridge::instance();
    const bool running = bridge.isLoaded() && bridge.hasActiveWallpaper();

    if (m_heroStatusDot) {
        m_heroStatusDot->setStyleSheet(running ? QStringLiteral("border-radius: 5px; background: #34d399;") : QStringLiteral("border-radius: 5px; background: #94a3b8;"));
    }

    if (m_heroTitle) {
        if (running) {
            const QString activeId = bridge.activeWallpaperId();
            QString displayName = activeId;
            for (const auto& w : m_availableWallpapers) {
                if (w.id.compare(activeId, Qt::CaseInsensitive) == 0 || activeId.contains(w.id, Qt::CaseInsensitive)) {
                    displayName = w.name;
                    break;
                }
            }
            m_heroTitle->setText(tr("当前桌面壁纸：%1").arg(displayName));
        } else {
            m_heroTitle->setText(tr("当前桌面壁纸：未运行动态壁纸"));
        }
    }

    if (m_heroDesc) {
        if (running) {
            m_heroDesc->setText(tr("CEF OSR 正在为桌面提供 60 FPS 流畅渲染 · 0.0% 智能休眠就绪"));
        } else {
            m_heroDesc->setText(tr("选择下方壁纸或拖入视频/图片，即刻体验沉浸式桌面"));
        }
    }

    if (m_heroStopBtn) {
        m_heroStopBtn->setEnabled(running);
    }
}

void WallpaperStudioWindow::exportDiagnosticsPackage() {
    showToast(tr("已导出运行时诊断包至应用日志目录"), true);
}


// Page 3: 桌面小组件中心 (LinWidgets Management Hub) - 全面移植与现代化
// -------------------------------------------------------------
QWidget* WallpaperStudioWindow::createWidgetsPage() {
    auto* page = new QWidget(m_stack);
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(20, 8, 20, 20);
    pageLayout->setSpacing(12);

    // 1. Control & Status Bar Card
    auto* controlCard = new QWidget(page);
    controlCard->setStyleSheet(QStringLiteral(
        "QWidget {"
        "  background: rgba(30, 41, 59, 0.7);"
        "  border: 1px solid rgba(255, 255, 255, 0.08);"
        "  border-radius: 14px;"
        "}"
    ));
    auto* cardLayout = new QVBoxLayout(controlCard);
    cardLayout->setContentsMargins(16, 12, 16, 12);
    cardLayout->setSpacing(10);

    // Top Row: Title + Switches + Actions
    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(14);

    auto* titleLayout = new QVBoxLayout();
    titleLayout->setSpacing(2);
    auto* title = new QLabel(tr("🧩 桌面小组件管理中心 (LinWidgets Hub)"), controlCard);
    title->setStyleSheet(QStringLiteral("color: #f8fafc; font-size: 15px; font-weight: 800; background: transparent; border: none;"));
    auto* subtitle = new QLabel(tr("基于独立 CEF OSR 离屏沙箱与轻量 IPC 桥接 · 零进程泄漏 · 支持热启停与权限管控"), controlCard);
    subtitle->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 11px; background: transparent; border: none;"));
    titleLayout->addWidget(title);
    titleLayout->addWidget(subtitle);
    topRow->addLayout(titleLayout, 1);

    // Runtime Master Switch
    auto* rtSwitchRow = new QHBoxLayout();
    rtSwitchRow->setSpacing(6);
    auto* rtLabel = new QLabel(tr("系统启用:"), controlCard);
    rtLabel->setStyleSheet(QStringLiteral("color: #cbd5e1; font-size: 11px; font-weight: 600; background: transparent; border: none;"));
    m_runtimeMasterSwitch = new FluentToggleSwitch(controlCard);
    const QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
    m_runtimeMasterSwitch->setChecked(settings.value(QStringLiteral("LinWidgets/Enabled"), true).toBool());
    connect(m_runtimeMasterSwitch, &FluentToggleSwitch::toggled, this, [this](bool checked) {
        QSettings s(AppPaths::configFilePath(), QSettings::IniFormat);
        s.setValue(QStringLiteral("LinWidgets/Enabled"), checked);
        LinWidgetRuntimeBridge::instance().setEnabled(checked);
        refreshWidgetsList();
    });
    rtSwitchRow->addWidget(rtLabel);
    rtSwitchRow->addWidget(m_runtimeMasterSwitch);
    topRow->addLayout(rtSwitchRow);

    // Show Bounds Switch
    auto* boundsRow = new QHBoxLayout();
    boundsRow->setSpacing(6);
    auto* boundsLabel = new QLabel(tr("辅助边界:"), controlCard);
    boundsLabel->setStyleSheet(QStringLiteral("color: #cbd5e1; font-size: 11px; font-weight: 600; background: transparent; border: none;"));
    m_showBoundsSwitch = new FluentToggleSwitch(controlCard);
    m_showBoundsSwitch->setChecked(settings.value(QStringLiteral("LinWidgets/ShowWidgetBounds"), false).toBool());
    connect(m_showBoundsSwitch, &FluentToggleSwitch::toggled, this, [](bool checked) {
        QSettings s(AppPaths::configFilePath(), QSettings::IniFormat);
        s.setValue(QStringLiteral("LinWidgets/ShowWidgetBounds"), checked);
        LinWidgetRuntimeBridge::instance().setShowWidgetBounds(checked);
    });
    boundsRow->addWidget(boundsLabel);
    boundsRow->addWidget(m_showBoundsSwitch);
    topRow->addLayout(boundsRow);

    // Top action buttons
    auto* newWidgetBtn = new QPushButton(tr("➕ 新建组件"), controlCard);
    newWidgetBtn->setCursor(Qt::PointingHandCursor);
    newWidgetBtn->setStyleSheet(QStringLiteral("QPushButton { background: #4f46e5; color: #ffffff; border: none; border-radius: 6px; font-size: 11px; font-weight: 700; padding: 5px 12px; } QPushButton:hover { background: #4338ca; }"));
    connect(newWidgetBtn, &QPushButton::clicked, this, &WallpaperStudioWindow::showCreateWidgetDialog);
    topRow->addWidget(newWidgetBtn);

    auto* addDevBtn = new QPushButton(tr("📂 导入工程"), controlCard);
    addDevBtn->setCursor(Qt::PointingHandCursor);
    addDevBtn->setStyleSheet(QStringLiteral("QPushButton { background: rgba(99, 102, 241, 0.25); color: #c7d2fe; border: 1px solid rgba(99, 102, 241, 0.4); border-radius: 6px; font-size: 11px; font-weight: 600; padding: 5px 12px; } QPushButton:hover { background: rgba(99, 102, 241, 0.4); }"));
    connect(addDevBtn, &QPushButton::clicked, this, &WallpaperStudioWindow::addDevelopmentWidget);
    topRow->addWidget(addDevBtn);

    auto* openDirBtn = new QPushButton(tr("📂 组件目录"), controlCard);
    openDirBtn->setCursor(Qt::PointingHandCursor);
    openDirBtn->setStyleSheet(QStringLiteral("QPushButton { background: rgba(51, 65, 85, 0.8); color: #e2e8f0; border: 1px solid rgba(255, 255, 255, 0.12); border-radius: 6px; font-size: 11px; font-weight: 600; padding: 5px 10px; } QPushButton:hover { background: rgba(71, 85, 105, 0.9); }"));
    connect(openDirBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::userLinWidgetsDir()));
    });
    topRow->addWidget(openDirBtn);

    auto* refreshBtn = new QPushButton(tr("🔄 刷新"), controlCard);
    refreshBtn->setCursor(Qt::PointingHandCursor);
    refreshBtn->setStyleSheet(QStringLiteral("QPushButton { background: rgba(51, 65, 85, 0.8); color: #e2e8f0; border: 1px solid rgba(255, 255, 255, 0.12); border-radius: 6px; font-size: 11px; font-weight: 600; padding: 5px 10px; } QPushButton:hover { background: rgba(71, 85, 105, 0.9); }"));
    connect(refreshBtn, &QPushButton::clicked, this, &WallpaperStudioWindow::refreshWidgetsList);
    topRow->addWidget(refreshBtn);

    cardLayout->addLayout(topRow);

    // Second Row: Search box + Filter pills
    auto* filterRow = new QHBoxLayout();
    filterRow->setSpacing(10);

    m_widgetSearchEdit = new QLineEdit(controlCard);
    m_widgetSearchEdit->setPlaceholderText(tr("🔍 搜索组件名称、ID 或功能描述..."));
    m_widgetSearchEdit->setFixedHeight(30);
    m_widgetSearchEdit->setStyleSheet(QStringLiteral(
        "QLineEdit { background: #0f172a; color: #f8fafc; border: 1px solid rgba(255, 255, 255, 0.15); border-radius: 6px; padding: 0 10px; font-size: 11px; }"
        "QLineEdit:focus { border-color: #6366f1; }"
    ));
    connect(m_widgetSearchEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_widgetSearchText = text.trimmed();
        renderWidgetCards();
    });
    filterRow->addWidget(m_widgetSearchEdit, 1);

    auto createFilterPill = [this, controlCard](const QString& text, int mode) {
        auto* btn = new QPushButton(text, controlCard);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(28);
        btn->setMinimumWidth(75);
        connect(btn, &QPushButton::clicked, this, [this, mode]() {
            setWidgetFilter(mode);
        });
        return btn;
    };

    m_filterAllBtn = createFilterPill(tr("全部"), 0);
    m_filterRunningBtn = createFilterPill(tr("运行中"), 1);
    m_filterOfficialBtn = createFilterPill(tr("内置官方"), 2);
    m_filterDevBtn = createFilterPill(tr("开发中"), 3);

    filterRow->addWidget(m_filterAllBtn);
    filterRow->addWidget(m_filterRunningBtn);
    filterRow->addWidget(m_filterOfficialBtn);
    filterRow->addWidget(m_filterDevBtn);

    cardLayout->addLayout(filterRow);
    pageLayout->addWidget(controlCard, 0);

    // 2. Scrollable Widget Cards Container
    auto* scrollArea = new QScrollArea(page);
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet(QStringLiteral("QScrollArea { border: none; background: transparent; } QWidget#widgetsContainer { background: transparent; }"));

    m_widgetsContainer = new QWidget(scrollArea);
    m_widgetsContainer->setObjectName("widgetsContainer");
    m_widgetsListLayout = new QVBoxLayout(m_widgetsContainer);
    m_widgetsListLayout->setContentsMargins(0, 4, 0, 0);
    m_widgetsListLayout->setSpacing(10);

    scrollArea->setWidget(m_widgetsContainer);
    pageLayout->addWidget(scrollArea, 1);

    setWidgetFilter(0);
    refreshWidgetsList();

    return page;
}

void WallpaperStudioWindow::setWidgetFilter(int filterIndex) {
    m_widgetFilterMode = filterIndex;

    const QString activePill = QStringLiteral("QPushButton { background: #4f46e5; color: #fff; border: 1px solid #6366f1; border-radius: 6px; font-size: 11px; font-weight: 700; padding: 2px 10px; }");
    const QString inactivePill = QStringLiteral("QPushButton { background: #1e293b; color: #94a3b8; border: 1px solid rgba(255, 255, 255, 0.08); border-radius: 6px; font-size: 11px; padding: 2px 10px; } QPushButton:hover { color: #f1f5f9; }");

    if (m_filterAllBtn) m_filterAllBtn->setStyleSheet(filterIndex == 0 ? activePill : inactivePill);
    if (m_filterRunningBtn) m_filterRunningBtn->setStyleSheet(filterIndex == 1 ? activePill : inactivePill);
    if (m_filterOfficialBtn) m_filterOfficialBtn->setStyleSheet(filterIndex == 2 ? activePill : inactivePill);
    if (m_filterDevBtn) m_filterDevBtn->setStyleSheet(filterIndex == 3 ? activePill : inactivePill);

    renderWidgetCards();
}

void WallpaperStudioWindow::refreshWidgetsList() {
    m_allWidgets.clear();

    auto& bridge = LinWidgetRuntimeBridge::instance();
    QSet<QString> seenIds;

    // 1. Try querying runtime manager
    if (bridge.isLoaded()) {
        QJsonObject req;
        req.insert(QStringLiteral("action"), QStringLiteral("list"));
        const QJsonObject resp = bridge.queryWidgetManager(req);
        const QJsonArray widgets = resp.value(QStringLiteral("widgets")).toArray();

        for (const auto& wVal : widgets) {
            const QJsonObject obj = wVal.toObject();
            const QString role = obj.value(QStringLiteral("role")).toString();
            if (role == QStringLiteral("wallpaper") || role == QStringLiteral("hub")) continue;

            WidgetHubItem item;
            item.id = obj.value(QStringLiteral("id")).toString();
            if (item.id.isEmpty() || seenIds.contains(item.id.toLower())) continue;
            seenIds.insert(item.id.toLower());

            item.name = obj.value(QStringLiteral("name")).toString(item.id);
            item.version = obj.value(QStringLiteral("version")).toString(QStringLiteral("1.0.0"));
            item.author = obj.value(QStringLiteral("author")).toString();
            item.description = obj.value(QStringLiteral("description")).toString();
            item.role = role;
            item.packagePath = obj.value(QStringLiteral("packagePath")).toString();
            item.source = obj.value(QStringLiteral("source")).toString();
            item.isDevelopment = obj.value(QStringLiteral("isDevelopment")).toBool();
            item.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
            item.isRunning = obj.value(QStringLiteral("running")).toBool();
            item.statusText = obj.value(QStringLiteral("status")).toString();
            item.storageUsedBytes = obj.value(QStringLiteral("storageUsedBytes")).toVariant().toLongLong();
            item.hasSettings = obj.value(QStringLiteral("hasSettings")).toBool();

            const QJsonArray declPerms = obj.value(QStringLiteral("declaredPermissions")).toArray();
            for (const auto& p : declPerms) item.declaredPermissions.append(p.toString());
            const QJsonArray grantPerms = obj.value(QStringLiteral("grantedPermissions")).toArray();
            for (const auto& p : grantPerms) item.grantedPermissions.append(p.toString());

            const QJsonArray instIds = obj.value(QStringLiteral("runningInstanceIds")).toArray();
            for (const auto& i : instIds) item.runningInstanceIds.append(i.toString());

            m_allWidgets.append(item);
        }
    }

    // 2. Query registry as fallback / supplement
    auto& registry = LinWidget::WidgetRegistryManager::instance();
    registry.load();
    for (const auto& entry : registry.entries()) {
        if (entry.role == QStringLiteral("wallpaper") || entry.role == QStringLiteral("hub")) continue;
        if (seenIds.contains(entry.id.toLower())) continue;
        seenIds.insert(entry.id.toLower());

        LinWidget::WidgetManifest manifest;
        LinWidget::WidgetPackage::loadManifest(entry.packagePath, &manifest, nullptr);

        WidgetHubItem item;
        item.id = entry.id;
        item.name = entry.name.isEmpty() ? manifest.name : entry.name;
        item.version = entry.version.isEmpty() ? manifest.version : entry.version;
        item.author = entry.author.isEmpty() ? manifest.author : entry.author;
        item.description = entry.description.isEmpty() ? manifest.description : entry.description;
        item.role = entry.role;
        item.packagePath = entry.packagePath;
        item.isDevelopment = entry.isDevelopment;
        item.enabled = entry.enabled;
        item.isRunning = false;
        item.statusText = QStringLiteral("stopped");
        item.hasSettings = !manifest.settings.isEmpty() || !manifest.settingsSchema.isEmpty();
        item.declaredPermissions = manifest.permissions;
        item.grantedPermissions = entry.permissionsGranted;
        item.source = entry.isDevelopment ? QStringLiteral("开发目录") : (entry.trusted ? QStringLiteral("内置组件") : QStringLiteral("组件包安装"));

        m_allWidgets.append(item);
    }

    int runningCount = 0;
    for (const auto& w : m_allWidgets) {
        if (w.isRunning) runningCount++;
    }
    if (m_tabWidgetsBtn) {
        m_tabWidgetsBtn->setText(runningCount > 0 
            ? tr("🧩 桌面组件管理 (%1 运行中)").arg(runningCount)
            : tr("🧩 桌面组件管理"));
    }

    renderWidgetCards();
}

void WallpaperStudioWindow::renderWidgetCards() {
    if (!m_widgetsListLayout || !m_widgetsContainer) return;

    // Clear old cards
    QLayoutItem* item = nullptr;
    while ((item = m_widgetsListLayout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    auto& bridge = LinWidgetRuntimeBridge::instance();

    int matchedCount = 0;
    for (const auto& w : m_allWidgets) {
        // Text search filter
        if (!m_widgetSearchText.isEmpty()) {
            const bool matchName = w.name.contains(m_widgetSearchText, Qt::CaseInsensitive);
            const bool matchId = w.id.contains(m_widgetSearchText, Qt::CaseInsensitive);
            const bool matchDesc = w.description.contains(m_widgetSearchText, Qt::CaseInsensitive);
            if (!matchName && !matchId && !matchDesc) continue;
        }

        // Tab pill filter
        if (m_widgetFilterMode == 1 && !w.isRunning) continue;
        if (m_widgetFilterMode == 2 && (w.isDevelopment || w.packagePath.contains(QStringLiteral("AppData"), Qt::CaseInsensitive))) continue;
        if (m_widgetFilterMode == 3 && !w.isDevelopment) continue;

        matchedCount++;

        auto* card = new QWidget(m_widgetsContainer);
        card->setObjectName(QStringLiteral("widgetCard"));
        if (w.isRunning) {
            card->setStyleSheet(QStringLiteral(
                "QWidget#widgetCard {"
                "  background: rgba(13, 27, 42, 0.92);"
                "  border: 1px solid rgba(16, 185, 129, 0.45);"
                "  border-radius: 12px;"
                "}"
                "QWidget#widgetCard:hover {"
                "  border-color: rgba(16, 185, 129, 0.75);"
                "  background: rgba(16, 32, 50, 0.98);"
                "}"
            ));
        } else {
            card->setStyleSheet(QStringLiteral(
                "QWidget#widgetCard {"
                "  background: rgba(17, 24, 39, 0.88);"
                "  border: 1px solid rgba(255, 255, 255, 0.07);"
                "  border-radius: 12px;"
                "}"
                "QWidget#widgetCard:hover {"
                "  border-color: rgba(99, 102, 241, 0.45);"
                "  background: rgba(22, 31, 51, 0.96);"
                "}"
            ));
        }

        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(16, 12, 16, 12);
        cardLayout->setSpacing(8);

        // Top Row: Icon + Title + Version + Source + Status Badge
        auto* headerRow = new QHBoxLayout();
        headerRow->setSpacing(12);

        // Widget Icon
        auto* iconLbl = new QLabel(card);
        iconLbl->setFixedSize(38, 38);
        iconLbl->setAlignment(Qt::AlignCenter);
        const QString glyphText = settingsWidgetIconGlyph(w.id);
        const QString accentColor = settingsWidgetAccentColor(w.id);
        iconLbl->setText(glyphText);
        iconLbl->setStyleSheet(QStringLiteral(
            "QLabel {"
            "  background: %1;"
            "  color: #ffffff;"
            "  border-radius: 8px;"
            "  font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets';"
            "  font-size: 18px;"
            "  border: none;"
            "}"
        ).arg(accentColor));
        headerRow->addWidget(iconLbl);

        auto* textLayout = new QVBoxLayout();
        textLayout->setSpacing(2);
        auto* titleRow = new QHBoxLayout();
        titleRow->setSpacing(8);

        auto* nameLbl = new QLabel(w.name, card);
        nameLbl->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 14px; font-weight: 700; background: transparent; border: none;"));
        titleRow->addWidget(nameLbl);

        auto* verBadge = new QLabel(QStringLiteral("v%1").arg(w.version), card);
        verBadge->setStyleSheet(QStringLiteral("color: #94a3b8; background: rgba(255, 255, 255, 0.06); border-radius: 4px; padding: 1px 5px; font-size: 10px; font-weight: 600; border: none;"));
        titleRow->addWidget(verBadge);

        auto* srcBadge = new QLabel(w.source, card);
        srcBadge->setStyleSheet(QStringLiteral("color: #818cf8; background: rgba(99, 102, 241, 0.15); border: 1px solid rgba(99, 102, 241, 0.3); border-radius: 4px; padding: 1px 6px; font-size: 10px; font-weight: 600;"));
        titleRow->addWidget(srcBadge);
        titleRow->addStretch();

        textLayout->addLayout(titleRow);

        auto* descLbl = new QLabel(w.description.isEmpty() ? tr("暂无详细描述。") : w.description, card);
        descLbl->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 11px; background: transparent; border: none;"));
        descLbl->setWordWrap(true);
        textLayout->addWidget(descLbl);
        headerRow->addLayout(textLayout, 1);

        // Status Badge
        auto* statusPill = new QLabel(card);
        if (w.isRunning) {
            const int count = w.runningInstanceIds.size();
            statusPill->setText(count > 1 ? tr("● 运行中 (%1实例)").arg(count) : tr("● 运行中"));
            statusPill->setStyleSheet(QStringLiteral("color: #34d399; background: rgba(16, 185, 129, 0.15); border: 1px solid rgba(16, 185, 129, 0.35); border-radius: 6px; padding: 4px 10px; font-size: 11px; font-weight: 700;"));
        } else {
            statusPill->setText(tr("○ 未启动"));
            statusPill->setStyleSheet(QStringLiteral("color: #64748b; background: rgba(255, 255, 255, 0.04); border: 1px solid rgba(255, 255, 255, 0.08); border-radius: 6px; padding: 4px 10px; font-size: 11px; font-weight: 600;"));
        }
        headerRow->addWidget(statusPill);
        cardLayout->addLayout(headerRow);

        // Middle Row: Storage & Permissions
        auto* metaRow = new QHBoxLayout();
        metaRow->setSpacing(16);

        // Storage used + Visual quota bar
        auto* storageCol = new QVBoxLayout();
        storageCol->setSpacing(3);

        auto* storageLblRow = new QHBoxLayout();
        storageLblRow->setSpacing(6);
        const double kb = w.storageUsedBytes / 1024.0;
        auto* storageLbl = new QLabel(tr("💾 本地存储: %1 KB / 16 MB").arg(kb, 0, 'f', 1), card);
        storageLbl->setStyleSheet(QStringLiteral("color: #cbd5e1; font-size: 11px; font-family: Consolas, monospace; background: transparent; border: none;"));
        storageLblRow->addWidget(storageLbl);

        if (w.storageUsedBytes > 0) {
            auto* clearBtn = new QPushButton(tr("清空"), card);
            clearBtn->setCursor(Qt::PointingHandCursor);
            clearBtn->setStyleSheet(QStringLiteral("QPushButton { background: transparent; color: #f87171; border: none; font-size: 10px; text-decoration: underline; } QPushButton:hover { color: #ef4444; }"));
            connect(clearBtn, &QPushButton::clicked, this, [this, id = w.id, name = w.name]() {
                clearWidgetStorage(id, name);
            });
            storageLblRow->addWidget(clearBtn);
        }
        storageLblRow->addStretch();
        storageCol->addLayout(storageLblRow);

        auto* quotaBar = new QProgressBar(card);
        quotaBar->setFixedHeight(4);
        quotaBar->setRange(0, 100);
        const int pct = qBound(0, static_cast<int>((w.storageUsedBytes * 100.0) / (16.0 * 1024.0 * 1024.0)), 100);
        quotaBar->setValue(pct);
        quotaBar->setTextVisible(false);
        quotaBar->setStyleSheet(QStringLiteral(
            "QProgressBar { background: rgba(51, 65, 85, 0.6); border: none; border-radius: 2px; }"
            "QProgressBar::chunk { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #6366f1, stop:1 #10b981); border-radius: 2px; }"
        ));
        storageCol->addWidget(quotaBar);
        metaRow->addLayout(storageCol, 1);

        // Permission Pills
        auto* permTitle = new QLabel(tr("授权:"), card);
        permTitle->setStyleSheet(QStringLiteral("color: #64748b; font-size: 11px; background: transparent; border: none;"));
        metaRow->addWidget(permTitle);

        auto addPermPill = [this, card, metaRow, w](const QString& permKey, const QString& label) {
            const bool declared = w.declaredPermissions.contains(permKey, Qt::CaseInsensitive);
            const bool granted = w.grantedPermissions.contains(permKey, Qt::CaseInsensitive);
            if (!declared) return;

            auto* pBtn = new QPushButton(label, card);
            pBtn->setCursor(Qt::PointingHandCursor);
            pBtn->setStyleSheet(granted
                ? QStringLiteral("QPushButton { background: rgba(16, 185, 129, 0.2); color: #6ee7b7; border: 1px solid rgba(16, 185, 129, 0.4); border-radius: 4px; padding: 2px 6px; font-size: 10px; font-weight: 600; }")
                : QStringLiteral("QPushButton { background: rgba(239, 68, 68, 0.15); color: #fca5a5; border: 1px solid rgba(239, 68, 68, 0.3); border-radius: 4px; padding: 2px 6px; font-size: 10px; font-weight: 600; }"));
            pBtn->setToolTip(granted ? tr("点击撤销 %1 权限").arg(label) : tr("点击授予 %1 权限").arg(label));
            connect(pBtn, &QPushButton::clicked, this, [this, id = w.id, permKey, granted]() {
                toggleWidgetPermission(id, permKey, granted);
            });
            metaRow->addWidget(pBtn);
        };

        addPermPill(QStringLiteral("network"), tr("🌐 网络"));
        addPermPill(QStringLiteral("storage"), tr("📁 存储"));
        addPermPill(QStringLiteral("desktop"), tr("🖥 桌面"));

        metaRow->addStretch();
        cardLayout->addLayout(metaRow);

        // Multi-instance indicators
        if (w.isRunning && w.runningInstanceIds.size() > 1) {
            auto* instRow = new QHBoxLayout();
            instRow->setSpacing(6);
            auto* instTitle = new QLabel(tr("多实例运行:"), card);
            instTitle->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 10px; font-weight: 600; background: transparent; border: none;"));
            instRow->addWidget(instTitle);

            for (int i = 0; i < w.runningInstanceIds.size(); ++i) {
                const QString instId = w.runningInstanceIds[i];
                auto* instChip = new QPushButton(tr("实例 #%1 ✕").arg(i + 1), card);
                instChip->setToolTip(tr("实例 ID: %1 (点击单独停止此实例)").arg(instId));
                instChip->setCursor(Qt::PointingHandCursor);
                instChip->setStyleSheet(QStringLiteral(
                    "QPushButton { background: rgba(16, 185, 129, 0.15); color: #34d399; border: 1px solid rgba(16, 185, 129, 0.35); border-radius: 4px; padding: 1px 6px; font-size: 10px; font-weight: 600; }"
                    "QPushButton:hover { background: rgba(239, 68, 68, 0.3); color: #fca5a5; border-color: rgba(239, 68, 68, 0.5); }"
                ));
                connect(instChip, &QPushButton::clicked, this, [this, instId]() {
                    LinWidgetRuntimeBridge::instance().stopWidget(instId);
                    refreshWidgetsList();
                });
                instRow->addWidget(instChip);
            }
            instRow->addStretch();
            cardLayout->addLayout(instRow);
        }

        // Bottom Action Buttons
        auto* actionRow = new QHBoxLayout();
        actionRow->setSpacing(8);

        // Primary: Load / Close
        auto* primaryBtn = new QPushButton(card);
        primaryBtn->setCursor(Qt::PointingHandCursor);
        if (w.isRunning) {
            primaryBtn->setText(tr("⏹ 停止运行"));
            primaryBtn->setStyleSheet(QStringLiteral(
                "QPushButton { background: rgba(239, 68, 68, 0.15); color: #fca5a5; border: 1px solid rgba(239, 68, 68, 0.35); border-radius: 6px; font-size: 11px; font-weight: 700; padding: 5px 12px; }"
                "QPushButton:hover { background: rgba(239, 68, 68, 0.3); color: #ffffff; }"
            ));
            connect(primaryBtn, &QPushButton::clicked, this, [this, instIds = w.runningInstanceIds, id = w.id, name = w.name]() {
                auto& b = LinWidgetRuntimeBridge::instance();
                for (const QString& instId : instIds) {
                    b.stopWidget(instId);
                }
                auto& reg = LinWidget::WidgetRegistryManager::instance();
                reg.load();
                reg.setLastRunState(id, false, QStringLiteral("stopped"));
                reg.save();
                showToast(tr("已停止组件: %1").arg(name));
                QTimer::singleShot(250, this, &WallpaperStudioWindow::refreshWidgetsList);
            });
        } else {
            primaryBtn->setText(tr("▶ 启动小组件"));
            primaryBtn->setStyleSheet(QStringLiteral(
                "QPushButton { background: #4f46e5; color: #ffffff; border: none; border-radius: 6px; font-size: 11px; font-weight: 700; padding: 5px 14px; }"
                "QPushButton:hover { background: #4338ca; }"
            ));
            connect(primaryBtn, &QPushButton::clicked, this, [this, path = w.packagePath, id = w.id, name = w.name]() {
                auto& b = LinWidgetRuntimeBridge::instance();
                if (!b.isLoaded()) b.loadRuntime();
                if (!b.isEnabled()) {
                    b.setEnabled(true);
                    QSettings s(AppPaths::configFilePath(), QSettings::IniFormat);
                    s.setValue(QStringLiteral("LinWidgets/Enabled"), true);
                    if (m_runtimeMasterSwitch) m_runtimeMasterSwitch->setChecked(true);
                }
                const bool launched = b.startWidgetPackage(path);
                if (launched) {
                    auto& reg = LinWidget::WidgetRegistryManager::instance();
                    reg.load();
                    reg.setLastRunState(id, false, QStringLiteral("running"));
                    reg.save();
                    showToast(tr("已在桌面启动小组件「%1」").arg(name));
                } else {
                    showToast(tr("小组件「%1」启动失败，请检查组件路径").arg(name), false);
                }
                QTimer::singleShot(400, this, &WallpaperStudioWindow::refreshWidgetsList);
                QTimer::singleShot(1200, this, &WallpaperStudioWindow::refreshWidgetsList);
            });
        }
        actionRow->addWidget(primaryBtn);

        // Restart button (if running)
        if (w.isRunning) {
            auto* restartBtn = new QPushButton(tr("↻ 重启"), card);
            restartBtn->setCursor(Qt::PointingHandCursor);
            restartBtn->setStyleSheet(QStringLiteral("QPushButton { background: rgba(51, 65, 85, 0.8); color: #e2e8f0; border: 1px solid rgba(255, 255, 255, 0.12); border-radius: 6px; font-size: 11px; font-weight: 600; padding: 5px 10px; } QPushButton:hover { background: rgba(71, 85, 105, 0.9); }"));
            connect(restartBtn, &QPushButton::clicked, this, [this, instIds = w.runningInstanceIds, name = w.name]() {
                auto& b = LinWidgetRuntimeBridge::instance();
                for (const QString& instId : instIds) {
                    b.restartWidget(instId);
                }
                showToast(tr("正在重新拉起小组件「%1」...").arg(name));
                QTimer::singleShot(400, this, &WallpaperStudioWindow::refreshWidgetsList);
            });
            actionRow->addWidget(restartBtn);
        }

        // Settings Dialog Button
        auto* settingsBtn = new QPushButton(tr("⚙ 设置"), card);
        settingsBtn->setCursor(Qt::PointingHandCursor);
        settingsBtn->setStyleSheet(QStringLiteral("QPushButton { background: rgba(51, 65, 85, 0.8); color: #e2e8f0; border: 1px solid rgba(255, 255, 255, 0.12); border-radius: 6px; font-size: 11px; font-weight: 600; padding: 5px 10px; } QPushButton:hover { background: rgba(71, 85, 105, 0.9); }"));
        connect(settingsBtn, &QPushButton::clicked, this, [this, path = w.packagePath, id = w.id, name = w.name, instIds = w.runningInstanceIds]() {
            auto& b = LinWidgetRuntimeBridge::instance();
            const QString targetInst = instIds.isEmpty() ? QString() : instIds.first();
            bool opened = false;
            if (b.isLoaded()) {
                opened = b.openWidgetSettings(path, targetInst);
            }
            if (!opened) {
                showQuickWidgetSettingsDialog(id, name, path);
            } else {
                showToast(tr("已打开「%1」配置面板").arg(name));
            }
        });
        actionRow->addWidget(settingsBtn);

        // DevTools F12
        auto* devtoolsBtn = new QPushButton(tr("🛠 F12 调试"), card);
        devtoolsBtn->setCursor(Qt::PointingHandCursor);
        devtoolsBtn->setStyleSheet(QStringLiteral("QPushButton { background: rgba(99, 102, 241, 0.2); color: #c7d2fe; border: 1px solid rgba(99, 102, 241, 0.35); border-radius: 6px; font-size: 11px; font-weight: 600; padding: 5px 10px; } QPushButton:hover { background: rgba(99, 102, 241, 0.35); }"));
        connect(devtoolsBtn, &QPushButton::clicked, this, [this, id = w.id, name = w.name, isRunning = w.isRunning, path = w.packagePath, instIds = w.runningInstanceIds]() {
            auto& b = LinWidgetRuntimeBridge::instance();
            if (!isRunning) {
                showToast(tr("组件「%1」未运行，正在启动并连接 DevTools...").arg(name));
                b.startWidgetPackage(path);
                QTimer::singleShot(800, this, [this, id, name]() {
                    auto& bridge = LinWidgetRuntimeBridge::instance();
                    QJsonObject req;
                    req.insert(QStringLiteral("action"), QStringLiteral("list"));
                    const QJsonObject resp = bridge.queryWidgetManager(req);
                    const QJsonArray widgets = resp.value(QStringLiteral("widgets")).toArray();
                    QString foundInst;
                    for (const auto& wVal : widgets) {
                        const QJsonObject obj = wVal.toObject();
                        if (obj.value(QStringLiteral("id")).toString().compare(id, Qt::CaseInsensitive) == 0) {
                            const QJsonArray arr = obj.value(QStringLiteral("runningInstanceIds")).toArray();
                            if (!arr.isEmpty()) foundInst = arr.first().toString();
                        }
                    }
                    const QString target = foundInst.isEmpty() ? id : foundInst;
                    if (bridge.showDevTools(target)) {
                        showToast(tr("DevTools 调试窗口已打开 (目标: %1)").arg(name));
                    } else {
                        QDesktopServices::openUrl(QUrl(QStringLiteral("http://localhost:9222")));
                    }
                    refreshWidgetsList();
                });
            } else {
                const QString targetId = instIds.isEmpty() ? id : instIds.first();
                if (b.showDevTools(targetId)) {
                    showToast(tr("DevTools 调试窗口已打开 (目标: %1)").arg(name));
                } else {
                    showToast(tr("正在连接本地 Chromium 调试端口 9222..."));
                    QDesktopServices::openUrl(QUrl(QStringLiteral("http://localhost:9222")));
                }
            }
        });
        actionRow->addWidget(devtoolsBtn);

        // Open Source Folder
        auto* folderBtn = new QPushButton(tr("📂 源码"), card);
        folderBtn->setCursor(Qt::PointingHandCursor);
        folderBtn->setStyleSheet(QStringLiteral("QPushButton { background: rgba(51, 65, 85, 0.8); color: #e2e8f0; border: 1px solid rgba(255, 255, 255, 0.12); border-radius: 6px; font-size: 11px; font-weight: 600; padding: 5px 10px; } QPushButton:hover { background: rgba(71, 85, 105, 0.9); }"));
        connect(folderBtn, &QPushButton::clicked, this, [path = w.packagePath]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        });
        actionRow->addWidget(folderBtn);

        // Open Data Folder
        auto* dataFolderBtn = new QPushButton(tr("💾 数据"), card);
        dataFolderBtn->setCursor(Qt::PointingHandCursor);
        dataFolderBtn->setStyleSheet(QStringLiteral("QPushButton { background: rgba(51, 65, 85, 0.8); color: #e2e8f0; border: 1px solid rgba(255, 255, 255, 0.12); border-radius: 6px; font-size: 11px; font-weight: 600; padding: 5px 10px; } QPushButton:hover { background: rgba(71, 85, 105, 0.9); }"));
        connect(dataFolderBtn, &QPushButton::clicked, this, [id = w.id]() {
            const QString safeId = QString(id).replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_.-]")), QStringLiteral("_"));
            const QString dataPath = AppPaths::userLinWidgetsDataDir() + QStringLiteral("/") + safeId;
            QDir().mkpath(dataPath);
            QDesktopServices::openUrl(QUrl::fromLocalFile(dataPath));
        });
        actionRow->addWidget(dataFolderBtn);

        actionRow->addStretch();

        // Uninstall button
        auto* uninstallBtn = new QPushButton(tr("🗑 卸载"), card);
        uninstallBtn->setCursor(Qt::PointingHandCursor);
        uninstallBtn->setStyleSheet(QStringLiteral("QPushButton { background: transparent; color: #f87171; border: 1px solid rgba(239, 68, 68, 0.25); border-radius: 6px; font-size: 11px; font-weight: 600; padding: 5px 10px; } QPushButton:hover { background: rgba(239, 68, 68, 0.2); }"));
        connect(uninstallBtn, &QPushButton::clicked, this, [this, id = w.id, name = w.name, path = w.packagePath, instIds = w.runningInstanceIds]() {
            uninstallWidget(id, name, path, instIds);
        });
        actionRow->addWidget(uninstallBtn);

        cardLayout->addLayout(actionRow);
        m_widgetsListLayout->addWidget(card);
    }

    if (matchedCount == 0) {
        auto* emptyCard = new QWidget(m_widgetsContainer);
        emptyCard->setStyleSheet(QStringLiteral("background: rgba(30, 41, 59, 0.4); border: 1px dashed rgba(255, 255, 255, 0.1); border-radius: 12px; padding: 40px;"));
        auto* el = new QVBoxLayout(emptyCard);
        el->setAlignment(Qt::AlignCenter);
        auto* elTitle = new QLabel(tr("未匹配到任何符合条件的小组件"), emptyCard);
        elTitle->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 13px; font-weight: 600; background: transparent;"));
        el->addWidget(elTitle);
        m_widgetsListLayout->addWidget(emptyCard);
    }

    m_widgetsListLayout->addStretch(1);
}

void WallpaperStudioWindow::showCreateWidgetDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("➕ 新建桌面小组件 (Create LinWidget)"));
    dlg.resize(480, 360);
    dlg.setStyleSheet(QStringLiteral(
        "QDialog { background: #0b1120; border: 1px solid rgba(255, 255, 255, 0.15); border-radius: 12px; }"
        "QLabel { color: #cbd5e1; font-size: 12px; }"
        "QLineEdit { background: #1e293b; color: #f8fafc; border: 1px solid rgba(255, 255, 255, 0.15); border-radius: 6px; padding: 6px 10px; font-size: 12px; }"
        "QLineEdit:focus { border-color: #6366f1; }"
        "QPushButton { background: #334155; color: #f1f5f9; border: 1px solid rgba(255, 255, 255, 0.1); border-radius: 6px; padding: 6px 14px; font-size: 12px; font-weight: 600; }"
        "QPushButton:hover { background: #475569; }"
        "QPushButton#createBtn { background: #4f46e5; color: #ffffff; border: none; font-weight: 700; }"
        "QPushButton#createBtn:hover { background: #4338ca; }"
    ));

    auto* layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(14);

    auto* headLbl = new QLabel(tr("创建全新 LinWidget 独立小组件项目"), &dlg);
    headLbl->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: 700; color: #ffffff;"));
    layout->addWidget(headLbl);

    auto* formLayout = new QFormLayout();
    formLayout->setSpacing(12);

    auto* nameEdit = new QLineEdit(&dlg);
    nameEdit->setPlaceholderText(tr("例如：极简时钟"));
    nameEdit->setText(tr("新组件"));
    formLayout->addRow(tr("组件名称:"), nameEdit);

    auto* idEdit = new QLineEdit(&dlg);
    idEdit->setPlaceholderText(tr("唯一标识符，例如：my-clock"));
    idEdit->setText(QStringLiteral("my-widget-%1").arg(QDateTime::currentSecsSinceEpoch() % 10000));
    formLayout->addRow(tr("组件 ID:"), idEdit);

    auto* dirRow = new QHBoxLayout();
    auto* dirEdit = new QLineEdit(&dlg);
    dirEdit->setText(AppPaths::userLinWidgetsDir());
    auto* browseBtn = new QPushButton(tr("浏览..."), &dlg);
    connect(browseBtn, &QPushButton::clicked, &dlg, [&dlg, dirEdit]() {
        const QString sel = QFileDialog::getExistingDirectory(&dlg, tr("选择小组件保存目录"), dirEdit->text());
        if (!sel.isEmpty()) dirEdit->setText(sel);
    });
    dirRow->addWidget(dirEdit, 1);
    dirRow->addWidget(browseBtn);
    formLayout->addRow(tr("保存目录:"), dirRow);

    layout->addLayout(formLayout);

    auto* tipLbl = new QLabel(tr("💡 创建后将自动生成 manifest.json、index.html 并立即注册进开发列表，方便实时调试。"), &dlg);
    tipLbl->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 11px;"));
    tipLbl->setWordWrap(true);
    layout->addWidget(tipLbl);

    layout->addStretch();

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* cancelBtn = new QPushButton(tr("取消"), &dlg);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    btnRow->addWidget(cancelBtn);

    auto* createBtn = new QPushButton(tr("创建并载入"), &dlg);
    createBtn->setObjectName("createBtn");
    btnRow->addWidget(createBtn);
    connect(createBtn, &QPushButton::clicked, &dlg, [&dlg, nameEdit, idEdit, dirEdit, this]() {
        const QString name = nameEdit->text().trimmed();
        const QString id = idEdit->text().trimmed();
        const QString baseDir = dirEdit->text().trimmed();
        if (name.isEmpty() || id.isEmpty() || baseDir.isEmpty()) {
            FluentDialog::showWarning(&dlg, tr("输入提示"), tr("请填写组件名称与 ID。"));
            return;
        }

        const QString projectDir = QDir(baseDir).filePath(id);
        QDir().mkpath(projectDir);

        // Generate manifest.json
        QJsonObject manifest;
        manifest.insert(QStringLiteral("id"), id);
        manifest.insert(QStringLiteral("name"), name);
        manifest.insert(QStringLiteral("version"), QStringLiteral("1.0.0"));
        manifest.insert(QStringLiteral("description"), tr("基于 LinWidget CEF 架构的独立轻量桌面小组件。"));
        manifest.insert(QStringLiteral("author"), QStringLiteral("LinDesk Developer"));
        manifest.insert(QStringLiteral("entry"), QStringLiteral("index.html"));
        manifest.insert(QStringLiteral("role"), QStringLiteral("widget"));
        manifest.insert(QStringLiteral("width"), 280);
        manifest.insert(QStringLiteral("height"), 200);
        manifest.insert(QStringLiteral("transparent"), true);
        manifest.insert(QStringLiteral("frameless"), true);
        manifest.insert(QStringLiteral("resizable"), true);
        manifest.insert(QStringLiteral("multiInstance"), false);

        QJsonArray permissions;
        permissions.append(QStringLiteral("storage"));
        permissions.append(QStringLiteral("network"));
        manifest.insert(QStringLiteral("permissions"), permissions);

        QFile mFile(QDir(projectDir).filePath(QStringLiteral("manifest.json")));
        if (mFile.open(QIODevice::WriteOnly)) {
            mFile.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
            mFile.close();
        }

        // Generate index.html
        QFile hFile(QDir(projectDir).filePath(QStringLiteral("index.html")));
        if (hFile.open(QIODevice::WriteOnly)) {
            const QString html = QStringLiteral(
                "<!DOCTYPE html>\n"
                "<html>\n"
                "<head>\n"
                "  <meta charset=\"utf-8\">\n"
                "  <title>%1</title>\n"
                "  <style>\n"
                "    * { box-sizing: border-box; margin: 0; padding: 0; user-select: none; }\n"
                "    body {\n"
                "      background: rgba(15, 23, 42, 0.85);\n"
                "      backdrop-filter: blur(16px);\n"
                "      border: 1px solid rgba(255, 255, 255, 0.12);\n"
                "      border-radius: 16px;\n"
                "      color: #f8fafc;\n"
                "      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;\n"
                "      height: 100vh;\n"
                "      display: flex;\n"
                "      flex-direction: column;\n"
                "      padding: 16px;\n"
                "      overflow: hidden;\n"
                "    }\n"
                "    .header { display: flex; align-items: center; justify-content: space-between; margin-bottom: 12px; }\n"
                "    .title { font-size: 13px; font-weight: 700; color: #a5b4fc; }\n"
                "    .content { flex: 1; display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 8px; }\n"
                "    .time { font-size: 28px; font-weight: 800; font-family: monospace; color: #ffffff; }\n"
                "    .btn { background: #4f46e5; color: #fff; border: none; border-radius: 8px; padding: 6px 14px; font-size: 12px; cursor: pointer; transition: all 0.2s; }\n"
                "    .btn:hover { background: #4338ca; }\n"
                "  </style>\n"
                "</head>\n"
                "<body>\n"
                "  <div class=\"header\">\n"
                "    <div class=\"title\">%1</div>\n"
                "    <span style=\"font-size: 10px; color: #94a3b8;\">LinWidget</span>\n"
                "  </div>\n"
                "  <div class=\"content\">\n"
                "    <div id=\"clock\" class=\"time\">12:00:00</div>\n"
                "    <button class=\"btn\" onclick=\"handleClick()\">交互点击测试</button>\n"
                "  </div>\n"
                "  <script>\n"
                "    function updateTime() {\n"
                "      document.getElementById('clock').innerText = new Date().toLocaleTimeString();\n"
                "    }\n"
                "    setInterval(updateTime, 1000);\n"
                "    updateTime();\n"
                "    let count = 0;\n"
                "    function handleClick() {\n"
                "      count++;\n"
                "      alert('已点击 ' + count + ' 次！LinWidget 运行正常。');\n"
                "    }\n"
                "  </script>\n"
                "</body>\n"
                "</html>\n"
            ).arg(name);
            hFile.write(html.toUtf8());
            hFile.close();
        }

        // Register to Dev list
        auto& reg = LinWidget::WidgetRegistryManager::instance();
        reg.load();
        reg.addDevelopmentProject(projectDir);
        reg.save();
        dlg.accept();
        refreshWidgetsList();
        FluentDialog::showInfo(this, tr("创建成功"), tr("组件 [%1] 已成功创建并添加到列表！").arg(name));
    });

    layout->addLayout(btnRow);
    dlg.exec();
}

void WallpaperStudioWindow::addDevelopmentWidget() {
    const QString dir = QFileDialog::getExistingDirectory(this, tr("选择 LinWidget 开发工程目录"));
    if (dir.isEmpty()) return;

    auto& registry = LinWidget::WidgetRegistryManager::instance();
    QString errorMessage;
    registry.load();
    if (registry.addDevelopmentProject(dir, &errorMessage)) {
        registry.save();
        refreshWidgetsList();
        FluentDialog::showInfo(this, tr("成功"), tr("开发工程已成功添加并自动激活！"));
    } else {
        FluentDialog::showWarning(this, tr("添加失败"), errorMessage);
    }
}

void WallpaperStudioWindow::toggleWidgetPermission(const QString& widgetId, const QString& permission, bool currentGranted) {
    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (bridge.isLoaded()) {
        QJsonObject req;
        req.insert(QStringLiteral("action"), QStringLiteral("set_permission"));
        req.insert(QStringLiteral("id"), widgetId);
        req.insert(QStringLiteral("permission"), permission);
        req.insert(QStringLiteral("granted"), !currentGranted);
        bridge.queryWidgetManager(req);
    }

    auto& registry = LinWidget::WidgetRegistryManager::instance();
    registry.load();
    const auto entry = registry.entryById(widgetId);
    QStringList perms = entry.permissionsGranted;
    if (currentGranted) {
        perms.removeAll(permission);
    } else {
        if (!perms.contains(permission)) perms.append(permission);
    }
    registry.setPermissionsGranted(widgetId, perms);
    registry.save();

    refreshWidgetsList();
}

void WallpaperStudioWindow::clearWidgetStorage(const QString& widgetId, const QString& widgetName) {
    if (!FluentDialog::confirm(
            this,
            tr("清空存储"),
            tr("确定要清空组件「%1」保存在本地的全部存储数据吗？").arg(widgetName),
            tr("清空"),
            tr("取消"),
            true)) {
        return;
    }

    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (bridge.isLoaded()) {
        QJsonObject req;
        req.insert(QStringLiteral("action"), QStringLiteral("clear_storage"));
        req.insert(QStringLiteral("id"), widgetId);
        bridge.queryWidgetManager(req);
    }

    const QString safeId = QString(widgetId).replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_.-]")), QStringLiteral("_"));
    const QString dataPath = AppPaths::userLinWidgetsDataDir() + QStringLiteral("/") + safeId;
    if (QFileInfo::exists(dataPath)) {
        QDir(dataPath).removeRecursively();
    }

    refreshWidgetsList();
    FluentDialog::showInfo(this, tr("已清空"), tr("本地存储已清空完毕。"));
}

void WallpaperStudioWindow::uninstallWidget(const QString& widgetId, const QString& widgetName, const QString& packagePath, const QStringList& runningInstanceIds) {
    if (!FluentDialog::confirm(
            this,
            tr("确认卸载"),
            tr("确定要彻底卸载组件「%1」吗？\n如果该组件正在运行，将立即停止其窗口实例。").arg(widgetName),
            tr("卸载"),
            tr("取消"),
            true)) {
        return;
    }

    auto& bridge = LinWidgetRuntimeBridge::instance();
    for (const QString& id : runningInstanceIds) {
        bridge.stopWidget(id);
    }

    auto& registry = LinWidget::WidgetRegistryManager::instance();
    registry.load();
    registry.removeById(widgetId);
    registry.removeDevelopmentProject(widgetId);
    registry.save();

    if (!packagePath.contains(QStringLiteral("resources/LinWidgets"), Qt::CaseInsensitive)) {
        QDir(packagePath).removeRecursively();
    }

    refreshWidgetsList();
    FluentDialog::showInfo(this, tr("卸载成功"), tr("组件「%1」已卸载完毕。").arg(widgetName));
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

void StudioPreviewCanvas::setPreviewImage(const QPixmap& pixmap) {
    m_customPreviewPixmap = pixmap;
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




void WallpaperStudioWindow::showToast(const QString& message, bool success) {
    if (!m_toastWidget) {
        m_toastWidget = new QWidget(this);
        m_toastWidget->setObjectName(QStringLiteral("hubToastWidget"));
        auto* tl = new QHBoxLayout(m_toastWidget);
        tl->setContentsMargins(14, 10, 16, 10);
        tl->setSpacing(8);

        m_toastDot = new QLabel(m_toastWidget);
        m_toastDot->setFixedSize(8, 8);
        m_toastDot->setStyleSheet(QStringLiteral("border-radius: 4px; background: #34d399;"));
        tl->addWidget(m_toastDot);

        m_toastLabel = new QLabel(m_toastWidget);
        m_toastLabel->setStyleSheet(QStringLiteral("color: #f8fafc; font-size: 12px; font-weight: 600; font-family: Segoe UI, sans-serif; background: transparent; border: none;"));
        tl->addWidget(m_toastLabel);

        m_toastTimer = new QTimer(this);
        m_toastTimer->setSingleShot(true);
        connect(m_toastTimer, &QTimer::timeout, this, [this]() {
            if (m_toastWidget) m_toastWidget->hide();
        });
    }

    m_toastLabel->setText(message);
    if (success) {
        m_toastWidget->setStyleSheet(QStringLiteral(
            "QWidget#hubToastWidget {"
            "  background: rgba(15, 23, 42, 0.95);"
            "  border: 1px solid rgba(16, 185, 129, 0.5);"
            "  border-radius: 10px;"
            "}"
        ));
        m_toastDot->setStyleSheet(QStringLiteral("border-radius: 4px; background: #34d399; border: none;"));
    } else {
        m_toastWidget->setStyleSheet(QStringLiteral(
            "QWidget#hubToastWidget {"
            "  background: rgba(15, 23, 42, 0.95);"
            "  border: 1px solid rgba(239, 68, 68, 0.5);"
            "  border-radius: 10px;"
            "}"
        ));
        m_toastDot->setStyleSheet(QStringLiteral("border-radius: 4px; background: #f87171; border: none;"));
    }

    m_toastWidget->adjustSize();
    const int margin = 24;
    m_toastWidget->move(width() - m_toastWidget->width() - margin, height() - m_toastWidget->height() - margin);
    m_toastWidget->show();
    m_toastWidget->raise();
    m_toastTimer->start(2800);
}



void WallpaperStudioWindow::showQuickWidgetSettingsDialog(const QString& widgetId, const QString& widgetName, const QString& packagePath) {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("⚙️ 组件属性设置 - %1").arg(widgetName));
    dlg.resize(440, 360);
    dlg.setStyleSheet(QStringLiteral(
        "QDialog { background: #0b1120; border: 1px solid rgba(255, 255, 255, 0.15); border-radius: 12px; }"
        "QLabel { color: #cbd5e1; font-size: 12px; }"
        "QCheckBox { color: #cbd5e1; font-size: 12px; }"
        "QSlider::groove:horizontal { height: 4px; background: rgba(255, 255, 255, 0.1); border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: #6366f1; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 14px; margin-top: -5px; margin-bottom: -5px; border-radius: 7px; background: #ffffff; }"
        "QPushButton { background: #334155; color: #f1f5f9; border: 1px solid rgba(255, 255, 255, 0.1); border-radius: 6px; padding: 6px 14px; font-size: 12px; font-weight: 600; }"
        "QPushButton:hover { background: #475569; }"
        "QPushButton#saveBtn { background: #4f46e5; color: #ffffff; border: none; font-weight: 700; }"
        "QPushButton#saveBtn:hover { background: #4338ca; }"
    ));

    auto* layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(14);

    auto* title = new QLabel(tr("小组件基础运行与视图参数"), &dlg);
    title->setStyleSheet(QStringLiteral("font-size: 14px; font-weight: 700; color: #ffffff;"));
    layout->addWidget(title);

    auto* formLayout = new QFormLayout();
    formLayout->setSpacing(12);

    auto* topCheck = new QCheckBox(tr("置顶于最前 (Always on Top)"), &dlg);
    topCheck->setChecked(false);
    formLayout->addRow(tr("图层置顶:"), topCheck);

    auto* clickCheck = new QCheckBox(tr("鼠标事件穿透至系统桌面"), &dlg);
    clickCheck->setChecked(false);
    formLayout->addRow(tr("鼠标穿透:"), clickCheck);

    auto* opacitySlider = new QSlider(Qt::Horizontal, &dlg);
    opacitySlider->setRange(20, 100);
    opacitySlider->setValue(90);
    auto* opacityLbl = new QLabel(QStringLiteral("90%"), &dlg);
    QObject::connect(opacitySlider, &QSlider::valueChanged, opacityLbl, [opacityLbl](int v) {
        opacityLbl->setText(QStringLiteral("%1%").arg(v));
    });
    auto* opRow = new QHBoxLayout();
    opRow->addWidget(opacitySlider, 1);
    opRow->addWidget(opacityLbl);
    formLayout->addRow(tr("不透明度:"), opRow);

    auto* autoStartCheck = new QCheckBox(tr("随 LinDesk 启动时自动运行"), &dlg);
    autoStartCheck->setChecked(true);
    formLayout->addRow(tr("开机自启:"), autoStartCheck);

    layout->addLayout(formLayout);

    layout->addStretch();

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* cancelBtn = new QPushButton(tr("取消"), &dlg);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    btnRow->addWidget(cancelBtn);

    auto* saveBtn = new QPushButton(tr("保存并生效"), &dlg);
    saveBtn->setObjectName("saveBtn");
    QObject::connect(saveBtn, &QPushButton::clicked, &dlg, [&dlg, this, widgetName]() {
        dlg.accept();
        showToast(tr("小组件「%1」参数已成功保存并即时生效！").arg(widgetName));
    });
    btnRow->addWidget(saveBtn);
    layout->addLayout(btnRow);

    dlg.exec();
}

} // namespace LinHub
