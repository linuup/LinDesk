#include "desktop/LinDesktop.h"
#include "desktop/ListView.h"
#include "desktop/RenameManager.h"
#include "box/BoxManager.h"
#include "ui/Fluent/FluentDialog.h"
#include "ui/Fluent/FluentInfoTip.h"
#include <QLocalServer>
#include <QLocalSocket>
#include "services/AppPaths.h"
#include "services/DpiCoordinateHelper.h"
#include <QCoreApplication>
#include <QFutureWatcher>
#include <QImageReader>
#include <QtMath>
#include <QSettings>
#include <QAction>
#include <QMenu>
#include <QWindow>
#include <QDebug>
#include <psapi.h>


namespace {
QPointer<LinDesktop> g_desktopHostWindowMonitorOwner;

// 解码壁纸：若原图大于目标尺寸，解码阶段直接缩到"刚好覆盖目标"的大小，避免先解出整张原图（如 4K 约 33MB）造成内存峰值。
QImage loadWallpaperImageScaled(const QString& path, const QSize& targetSize)
{
    QImageReader reader(path);
    if (targetSize.isValid()) {
        const QSize original = reader.size();
        if (original.isValid() && !original.isEmpty()
            && (original.width() > targetSize.width() || original.height() > targetSize.height())) {
            const double scale = qMax(double(targetSize.width()) / original.width(),
                                      double(targetSize.height()) / original.height());
            if (scale < 1.0) {
                const QSize scaledSize(qMax(1, qCeil(original.width() * scale)),
                                       qMax(1, qCeil(original.height() * scale)));
                reader.setScaledSize(scaledSize);
            }
        }
    }
    return reader.read();
}

static const char* kSingleInstanceServerName = "LinDesk_SingleInstance_IPC";

static UINT getRestoreWindowMessage()
{
    static const UINT s_msg = RegisterWindowMessageW(L"LINDESK_IPC_RESTORE_MSG");
    return s_msg;
}

static UINT getShowTrayWindowMessage()
{
    static const UINT s_msg = RegisterWindowMessageW(L"LINDESK_IPC_SHOW_TRAY_MSG");
    return s_msg;
}

static void sendSingleInstanceCommand(const QString& cmd)
{
    // Channel 1: QLocalSocket Named Pipe
    QLocalSocket socket;
    socket.connectToServer(QString::fromLatin1(kSingleInstanceServerName));
    if (socket.waitForConnected(600)) {
        socket.write(cmd.toUtf8() + "\n");
        socket.flush();
        socket.waitForBytesWritten(600);
        socket.disconnectFromServer();
    }

    // Channel 2: Broadcast Windows Registered Message
    if (cmd == QStringLiteral("RESTORE")) {
        PostMessageW(HWND_BROADCAST, getRestoreWindowMessage(), 0, 0);
    } else if (cmd == QStringLiteral("SHOW_TRAY")) {
        PostMessageW(HWND_BROADCAST, getShowTrayWindowMessage(), 0, 0);
    }
}

void showAlreadyRunningDialogAndHandle()
{
    auto* dialog = new FluentDialog(nullptr);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    dialog->setDialogTitle(QCoreApplication::translate("LinDesktop", "LinDesk is Already Running"));
    dialog->setMessage(QCoreApplication::translate("LinDesktop",
        "LinDesk is already running in the background, but the desktop presentation may have been obscured by the Windows desktop or third-party software.\n\n"
        "• Click [Restore Desktop] to bring LinDesk back to the front.\n"
        "• Click [Cancel] to keep the current desktop and show the icon in the system tray."));

    auto handled = std::make_shared<bool>(false);

    dialog->addActionButton(
        QCoreApplication::translate("LinDesktop", "Cancel"),
        FluentDialog::ActionType::Secondary,
        [handled]() {
            if (!*handled) {
                *handled = true;
                sendSingleInstanceCommand(QStringLiteral("SHOW_TRAY"));
                QTimer::singleShot(80, QCoreApplication::instance(), &QCoreApplication::quit);
            }
        }
    );

    dialog->addActionButton(
        QCoreApplication::translate("LinDesktop", "Restore Desktop"),
        FluentDialog::ActionType::Primary,
        [handled]() {
            if (!*handled) {
                *handled = true;
                sendSingleInstanceCommand(QStringLiteral("RESTORE"));
                QTimer::singleShot(80, QCoreApplication::instance(), &QCoreApplication::quit);
            }
        }
    );

    QObject::connect(dialog, &QObject::destroyed, [handled]() {
        if (!*handled) {
            *handled = true;
            sendSingleInstanceCommand(QStringLiteral("SHOW_TRAY"));
            QTimer::singleShot(80, QCoreApplication::instance(), &QCoreApplication::quit);
        }
    });

    const QRect screenRect = QApplication::primaryScreen()
        ? QApplication::primaryScreen()->availableGeometry()
        : QRect(0, 0, 800, 600);
    dialog->show();
    const QPoint center = screenRect.center() - QPoint(dialog->width() / 2, dialog->height() / 2);
    dialog->move(center);
    dialog->raise();
    dialog->activateWindow();
}

void CALLBACK desktopHostWindowEventProc(HWINEVENTHOOK,
                                         DWORD event,
                                         HWND hwnd,
                                         LONG idObject,
                                         LONG idChild,
                                         DWORD,
                                         DWORD)
{
    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) {
        return;
    }

    if (g_desktopHostWindowMonitorOwner) {
        QMetaObject::invokeMethod(
            g_desktopHostWindowMonitorOwner,
            [owner = g_desktopHostWindowMonitorOwner, hwnd, event]() {
                if (owner) {
                    owner->handleDesktopHostWindowEvent(hwnd, event);
                }
            },
            Qt::QueuedConnection);
    }
}

QRect nativeWindowRect(HWND hwnd)
{
    RECT rect = {};
    if (!hwnd || !GetWindowRect(hwnd, &rect)) {
        return {};
    }

    return QRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
}

void logDesktopDpiTrace(const char* source, QWidget* widget, const QRect& suggestedNativeRect = QRect())
{
    QScreen* screen = widget ? widget->screen() : nullptr;
    if (!screen) {
        screen = QApplication::primaryScreen();
    }

    const HWND hwnd = widget ? reinterpret_cast<HWND>(widget->winId()) : nullptr;
    qDebug() << "[DpiTrace][LinDesktop]"
             << "source=" << source
             << "qtGeometry=" << (widget ? widget->geometry() : QRect())
             << "qtFrameGeometry=" << (widget ? widget->frameGeometry() : QRect())
             << "nativeWindowRect=" << nativeWindowRect(hwnd)
             << "suggestedNativeRect=" << suggestedNativeRect
             << "nativeDpi=" << (hwnd ? GetDpiForWindow(hwnd) : 0)
             << "widgetDpr=" << (widget ? widget->devicePixelRatioF() : 0.0)
             << "screenDpr=" << (screen ? screen->devicePixelRatio() : 0.0)
             << "screenGeometry=" << (screen ? screen->geometry() : QRect())
             << "screenAvailable=" << (screen ? screen->availableGeometry() : QRect());
}
}

LinDesktop::LinDesktop(QWidget* parent)
    : QWidget(parent)
{
    m_singleInstanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\LinDesk_SingleInstance");
    if (!m_singleInstanceMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (m_singleInstanceMutex) {
            CloseHandle(m_singleInstanceMutex);
            m_singleInstanceMutex = nullptr;
        }
        m_isSecondaryInstance = true;
        QTimer::singleShot(0, []() {
            showAlreadyRunningDialogAndHandle();
        });
        return;
    }

    m_configPath = AppPaths::configFilePath();
    {
        QSettings settings(m_configPath, QSettings::IniFormat);
        m_isFirstLaunch = settings.value("Firslanuch", true).toBool();
    }

    setWindowTitle(tr("LinDesk"));
    setObjectName("LinDesk");
    setGeometry(QApplication::primaryScreen()->geometry());
    setFocusPolicy(Qt::StrongFocus);
    initializeTrayIcon();

    // setAttribute(Qt::WA_TranslucentBackground);

    m_transitionAnimation = new QVariantAnimation(this);
    m_transitionAnimation->setDuration(m_transitionDuration);
    m_transitionAnimation->setStartValue(0.0f);
    m_transitionAnimation->setEndValue(1.0f);
    m_transitionAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_transitionAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        setTransitionOpacity(value.toFloat());
    });
    connect(m_transitionAnimation, &QVariantAnimation::finished, this, [this]() {
        m_prevScaledBackImagePixmap = QPixmap();
        m_prevBackImagePixmap = QPixmap();
        m_transitionOpacity = 1.0f;
        update();
    });
    // Synchronously load wallpaper on cold startup so m_scaledBackImagePixmap is ready on first paint
    loadBackImageSync();

    listView = ListView::instance(this);
    listView->setGeometry(rect());
    listView->show();

    selectArea = new SelectArea(listView);
    selectArea->syncToRect(listView->rect());
    selectArea->lower();
    setFocusProxy(listView);

    embedToDesktop();
    m_shellHookMessage = RegisterWindowMessageW(L"SHELLHOOK");
    m_shellHookRegistered = RegisterShellHookWindow(reinterpret_cast<HWND>(winId())) != FALSE;
    qDebug() << "[LinDesktop] shell hook"
             << "registered=" << m_shellHookRegistered
             << "message=" << m_shellHookMessage
             << "hwnd=" << QString::number(static_cast<quintptr>(winId()), 16);
    startDesktopHostWindowMonitor();

    m_monitor = new DesktopInfoMonitor(this);
    m_monitor->startListening();
    connect(m_monitor, &DesktopInfoMonitor::wallpaperChanged, this, &LinDesktop::onWallpaperChanged);

    m_wallpaperLoadTimer = new QTimer(this);
    m_wallpaperLoadTimer->setSingleShot(true);
    m_wallpaperLoadTimer->setInterval(500);
    connect(m_wallpaperLoadTimer, &QTimer::timeout, this, &LinDesktop::loadBackImage);
    QTimer::singleShot(800, this, &LinDesktop::checkFirstLaunchPrompt);
    QTimer::singleShot(0, this, [this]() {
        if (windowHandle()) {
            m_windowScreenChangedConnection = connect(
                windowHandle(),
                &QWindow::screenChanged,
                this,
                [this](QScreen* screen) {
                    connectScreenSignals(screen);
                    handleDpiOrScreenMetricsChanged();
                });
        }
        connectScreenSignals(screen());
        handleDpiOrScreenMetricsChanged();
    });

    m_restoreWindowMessage = getRestoreWindowMessage();
    m_showTrayWindowMessage = getShowTrayWindowMessage();
#ifdef Q_OS_WIN
    if (HWND hwnd = reinterpret_cast<HWND>(winId())) {
        ChangeWindowMessageFilterEx(hwnd, m_restoreWindowMessage, MSGFLT_ALLOW, nullptr);
        ChangeWindowMessageFilterEx(hwnd, m_showTrayWindowMessage, MSGFLT_ALLOW, nullptr);
    }
#endif
    startSingleInstanceServer();
}

LinDesktop::~LinDesktop()
{
    if (m_shellHookRegistered) {
        DeregisterShellHookWindow(reinterpret_cast<HWND>(winId()));
    }
    stopDesktopHostWindowMonitor();

    if (m_ipcServer) {
        m_ipcServer->close();
        delete m_ipcServer;
        m_ipcServer = nullptr;
    }

    if (m_trayIcon) {
        m_trayIcon->hide();
    }

    if (m_singleInstanceMutex) {
        ReleaseMutex(m_singleInstanceMutex);
        CloseHandle(m_singleInstanceMutex);
        m_singleInstanceMutex = nullptr;
    }
}

void LinDesktop::setTransitionOpacity(float opacity)
{
    m_transitionOpacity = opacity;
    update();
}

void LinDesktop::setTransitionDuration(int ms)
{
    m_transitionDuration = ms;
    if (m_transitionAnimation) {
        m_transitionAnimation->setDuration(ms);
    }
}

bool LinDesktop::showDefaultWallpaper() const
{
    QSettings settings(m_configPath, QSettings::IniFormat);
    return settings.value("General/ShowDefaultWallpaper", true).toBool();
}

void LinDesktop::setShowDefaultWallpaper(bool show)
{
    QSettings settings(m_configPath, QSettings::IniFormat);
    settings.setValue("General/ShowDefaultWallpaper", show);
    settings.sync();

    if (show) {
        loadBackImage();
    } else {
        m_backImagePixmap = QPixmap();
        m_scaledBackImagePixmap = QPixmap();
        m_prevBackImagePixmap = QPixmap();
        m_prevScaledBackImagePixmap = QPixmap();
        m_currentWallpaperPath.clear();
        m_pendingWallpaperPath.clear();
        m_fallbackWallpaperPath.clear();
        update();
    }
}

void LinDesktop::onWallpaperChanged()
{
    if (!showDefaultWallpaper()) {
        return;
    }
    qDebug() << "[LinDesktop] Wallpaper registry change detected, debouncing...";
    m_wallpaperLoadTimer->start();
}

void LinDesktop::loadBackImage()
{
    loadBackImageInternal(false);
}

void LinDesktop::loadBackImageSync()
{
    loadBackImageInternal(true);
}

void LinDesktop::loadBackImageInternal(bool sync)
{
    if (!showDefaultWallpaper()) {
        m_backImagePixmap = QPixmap();
        m_scaledBackImagePixmap = QPixmap();
        m_prevBackImagePixmap = QPixmap();
        m_prevScaledBackImagePixmap = QPixmap();
        m_currentWallpaperPath.clear();
        m_pendingWallpaperPath.clear();
        m_fallbackWallpaperPath.clear();
        update();
        return;
    }

    const QString path = SysInfoUtil::GetRobustWallpaperPath();
    qDebug() << "[LinDesktop] Loading wallpaper from:" << path << (sync ? "(sync cold start)" : "(async)");

    if (path.isEmpty()) {
        applyFallbackWallpaper(QString(), QStringLiteral("wallpaper path is empty"));
        return;
    }

    if (path == m_pendingWallpaperPath) {
        return;
    }

    const qreal dpr = qMax<qreal>(1.0, devicePixelRatioF());
    const QSize logicalWallpaperSize = size().isValid() && !size().isEmpty()
        ? size()
        : (QApplication::primaryScreen() ? QApplication::primaryScreen()->geometry().size() : QSize(1920, 1080));
    const QSize wallpaperTargetSize(qMax(1, qRound(logicalWallpaperSize.width() * dpr)),
                                   qMax(1, qRound(logicalWallpaperSize.height() * dpr)));

    if (sync) {
        m_pendingWallpaperPath.clear();
        const QImage image = loadWallpaperImageScaled(path, wallpaperTargetSize);
        if (image.isNull()) {
            applyFallbackWallpaper(path, QStringLiteral("failed to decode wallpaper"));
        } else {
            applyLoadedWallpaper(path, image);
        }
        return;
    }

    m_pendingWallpaperPath = path;
    const quint64 requestId = ++m_wallpaperLoadRequestId;
    auto* watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, requestId, path]() {
        const QImage image = watcher->result();
        watcher->deleteLater();

        if (requestId != m_wallpaperLoadRequestId) {
            return;
        }

        m_pendingWallpaperPath.clear();
        if (image.isNull()) {
            applyFallbackWallpaper(path, QStringLiteral("failed to decode wallpaper"));
            return;
        }

        applyLoadedWallpaper(path, image);
    });
    watcher->setFuture(QtConcurrent::run([path, wallpaperTargetSize]() {
        return loadWallpaperImageScaled(path, wallpaperTargetSize);
    }));
}

void LinDesktop::applyLoadedWallpaper(const QString& path, const QImage& image)
{
    if (image.isNull()) {
        return;
    }

    m_currentWallpaperPath = path;
    m_fallbackWallpaperPath.clear();

    if (!m_scaledBackImagePixmap.isNull()) {
        m_prevScaledBackImagePixmap = m_scaledBackImagePixmap;
    }

    m_backImagePixmap = QPixmap::fromImage(image);
    rebuildScaledWallpaperPixmaps();

    if (!m_prevScaledBackImagePixmap.isNull()) {
        m_transitionAnimation->stop();
        m_transitionAnimation->start();
    } else {
        m_prevScaledBackImagePixmap = QPixmap();
        m_prevBackImagePixmap = QPixmap();
        m_transitionOpacity = 1.0f;
        update();
    }
}

void LinDesktop::applyFallbackWallpaper(const QString& path, const QString& reason)
{
    const QString fallbackKey = path.isEmpty() ? QStringLiteral("__black_wallpaper__") : path;
    if (m_fallbackWallpaperPath == fallbackKey && !m_scaledBackImagePixmap.isNull()) {
        return;
    }

    qDebug() << "[LinDesktop] Using generated black wallpaper fallback:" << reason << "path=" << path;

    if (!m_scaledBackImagePixmap.isNull()) {
        m_prevScaledBackImagePixmap = m_scaledBackImagePixmap;
    }

    QPixmap fallback(1, 1);
    fallback.fill(Qt::black);
    m_backImagePixmap = fallback;
    m_currentWallpaperPath = fallbackKey;
    m_fallbackWallpaperPath = fallbackKey;
    rebuildScaledWallpaperPixmaps();

    if (!m_prevScaledBackImagePixmap.isNull()) {
        m_transitionAnimation->stop();
        m_transitionAnimation->start();
    } else {
        m_prevScaledBackImagePixmap = QPixmap();
        m_prevBackImagePixmap = QPixmap();
        m_transitionOpacity = 1.0f;
        update();
    }
}

void LinDesktop::rebuildScaledWallpaperPixmaps()
{
    const QSize targetLogicalSize = size();
    if (!targetLogicalSize.isValid()) {
        m_scaledBackImagePixmap = QPixmap();
        m_prevScaledBackImagePixmap = QPixmap();
        m_backImagePixmap = QPixmap();
        m_prevBackImagePixmap = QPixmap();
        return;
    }

    const qreal dpr = qMax<qreal>(1.0, devicePixelRatioF());
    const QSize targetPhysicalSize(qMax(1, qRound(targetLogicalSize.width() * dpr)),
                                  qMax(1, qRound(targetLogicalSize.height() * dpr)));

    // 若当前尺寸已匹配且已经生成过 scaled pixmap，无需重复重构
    if (!m_scaledBackImagePixmap.isNull() && m_scaledBackImagePixmap.size() == targetPhysicalSize && qFuzzyCompare(m_scaledBackImagePixmap.devicePixelRatio(), dpr) && m_backImagePixmap.isNull()) {
        return;
    }

    // 若原图已释放但窗口发生 resize，从当前壁纸路径重新按需读取原图
    if (m_backImagePixmap.isNull() && !m_currentWallpaperPath.isEmpty()) {
        if (m_fallbackWallpaperPath == m_currentWallpaperPath) {
            QPixmap fallback(1, 1);
            fallback.fill(Qt::black);
            m_backImagePixmap = fallback;
        } else {
            const QImage img = loadWallpaperImageScaled(m_currentWallpaperPath, targetPhysicalSize);
            if (!img.isNull()) {
                m_backImagePixmap = QPixmap::fromImage(img);
            }
        }
    }

    const auto buildFittedPixmap = [targetPhysicalSize, dpr](const QPixmap& source) {
        if (source.isNull()) {
            return QPixmap();
        }
        if (source.size() == QSize(1, 1)) {
            QPixmap fitted(targetPhysicalSize);
            fitted.fill(source.toImage().pixelColor(0, 0));
            if (dpr > 1.0) fitted.setDevicePixelRatio(dpr);
            return fitted;
        }

        QPixmap fitted(targetPhysicalSize);
        fitted.fill(Qt::transparent);

        QPainter painter(&fitted);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        QPixmap scaled = source.scaled(targetPhysicalSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const int x = (targetPhysicalSize.width() - scaled.width()) / 2;
        const int y = (targetPhysicalSize.height() - scaled.height()) / 2;
        painter.drawPixmap(x, y, scaled);
        if (dpr > 1.0) {
            fitted.setDevicePixelRatio(dpr);
        }
        return fitted;
    };

    if (!m_backImagePixmap.isNull()) {
        m_scaledBackImagePixmap = buildFittedPixmap(m_backImagePixmap);
        // 生成 m_scaledBackImagePixmap 之后释放 m_backImagePixmap 原图，避免常驻占用多份显存/内存
        m_backImagePixmap = QPixmap();
        m_prevBackImagePixmap = QPixmap();
    }
}

void LinDesktop::requestHoverEffect()
{
}

void LinDesktop::connectScreenSignals(QScreen* screen)
{
    if (m_connectedScreen == screen) {
        return;
    }

    if (m_connectedScreen) {
        disconnect(m_screenGeometryChangedConnection);
        disconnect(m_screenDpiChangedConnection);
    }

    m_connectedScreen = screen ? screen : QApplication::primaryScreen();
    if (!m_connectedScreen) {
        return;
    }

    m_screenGeometryChangedConnection = connect(
        m_connectedScreen,
        &QScreen::geometryChanged,
        this,
        [this](const QRect&) {
            handleDpiOrScreenMetricsChanged();
        });
    m_screenDpiChangedConnection = connect(
        m_connectedScreen,
        &QScreen::logicalDotsPerInchChanged,
        this,
        [this](qreal) {
            handleDpiOrScreenMetricsChanged();
        });
}

void LinDesktop::handleDpiOrScreenMetricsChanged()
{
    QScreen* activeScreen = screen();
    if (!activeScreen && windowHandle()) {
        activeScreen = windowHandle()->screen();
    }
    if (!activeScreen) {
        activeScreen = QApplication::primaryScreen();
    }
    if (!activeScreen) {
        return;
    }

    const QRect targetGeometry = m_isEmbedded
        ? activeScreen->geometry()
        : geometry();

    if (m_isEmbedded && geometry() != targetGeometry) {
        setGeometry(targetGeometry);
    }

    if (listView) {
        listView->setGeometry(rect());
        listView->refreshForDpiChange();
    }

    rebuildScaledWallpaperPixmaps();
    if (selectArea) {
        selectArea->syncToRect(listView ? listView->rect() : rect());
    }
    update();
}

void LinDesktop::clearSelectionOverlay()
{
    if (!selectArea) {
        return;
    }

    const QRect previousRect = selectArea->selectionRect();
    selectArea->clearAndHide();
    if (!previousRect.isNull()) {
        if (listView) {
            const QRect dirtyRect = previousRect.adjusted(-2, -2, 2, 2);
            listView->update(dirtyRect);
            const QPoint topLeft = listView->mapTo(this, dirtyRect.topLeft());
            update(QRect(topLeft, dirtyRect.size()));
        } else {
            update(previousRect.adjusted(-2, -2, 2, 2));
        }
    }
}

void LinDesktop::checkFirstLaunchPrompt()
{
    if (m_firstLaunchPromptShown || !m_isFirstLaunch) {
        return;
    }

    m_firstLaunchPromptShown = true;

    auto markHandled = [this]() {
        QSettings writeSettings(m_configPath, QSettings::IniFormat);
        writeSettings.setValue("Firslanuch", false);
        writeSettings.sync();
        m_isFirstLaunch = false;
    };

    FluentDialog* dlg = new FluentDialog(nullptr);
    dlg->setDialogTitle(tr("Desktop Organization"));
    dlg->setMessage(tr("This appears to be the first launch. Do you want to organize the desktop now or keep the current Windows layout?"));
    listView->hide();
    dlg->addActionButton(tr("Organize Desktop"), FluentDialog::ActionType::Primary, [this, markHandled]() {
        markHandled();
        if (listView) {
            listView->sortItems(ListView::SortOrderType::SortByName_ASC);
            listView->updateRightMenuState();
            listView->applyAutoArrangeLayout(false, false);
            listView->update();
            listView->requestSaveBoxes();
            listView->createRecentModifiedItemsBox();
            listView->show();
        }
    });
    dlg->addActionButton(tr("Keep Windows Layout"), FluentDialog::ActionType::Secondary, [this, markHandled]() {
        markHandled();
        if (listView) {
            listView->show();
            listView->createRecentModifiedItemsBox();
        }
    });
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void LinDesktop::initializeTrayIcon()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }

    m_trayIcon = new QSystemTrayIcon(this);
    QIcon trayIcon = QApplication::windowIcon();
    if (trayIcon.isNull()) {
        trayIcon = QIcon(":/Assets/icon.png");
    }
    m_trayIcon->setIcon(trayIcon);
    m_trayIcon->setToolTip(tr("LinDesk"));

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
    m_showAction = m_trayMenu->addAction(tr("Show LinDesk"));
    m_exitAction = m_trayMenu->addAction(tr("Exit"));
    m_trayIcon->setContextMenu(m_trayMenu);

    connect(m_showAction, &QAction::triggered, this, &LinDesktop::restoreFromTray);
    connect(m_exitAction, &QAction::triggered, this, &LinDesktop::exitFromTray);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            restoreFromTray();
        }
    });
}

bool LinDesktop::shouldMinimizeToTray() const
{
    QSettings settings(m_configPath, QSettings::IniFormat);
    return settings.value("General/MinimizeToTray", true).toBool();
}

void LinDesktop::restoreFromTray()
{
    selfHealAndRestoreDesktop();
    if (m_trayIcon && isVisible()) {
        m_trayIcon->hide();
    }
}

void LinDesktop::exitFromTray()
{
    m_isQuittingFromTray = true;
    if (m_trayIcon) {
        m_trayIcon->hide();
    }
    QCoreApplication::quit();
}

void LinDesktop::embedToDesktop()
{
    HWND hwnd = CSystemApi::FindRealDesktop();
    if (hwnd) {
        logDesktopDpiTrace("embedBefore", this);
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground, false);
        setGeometry(QApplication::primaryScreen()->geometry());
        
        HWND hostWindow = (HWND)this->winId();
        SetParent(hostWindow, hwnd);

        // Strip WS_EX_LAYERED to prevent child window rendering issues in Qt
        LONG_PTR exStyle = GetWindowLongPtr(hostWindow, GWL_EXSTYLE);
        exStyle &= ~WS_EX_LAYERED;
        SetWindowLongPtr(hostWindow, GWL_EXSTYLE, exStyle);

        m_isEmbedded = true;
        show();
        logDesktopDpiTrace("embedAfter", this);
        forceDesktopRepaint();
    }
}

void LinDesktop::startDesktopHostWindowMonitor()
{
    if (!m_desktopHostWindowEventHooks.isEmpty()) {
        return;
    }

    g_desktopHostWindowMonitorOwner = this;
    const DWORD events[] = {
        EVENT_OBJECT_CREATE,
        EVENT_OBJECT_SHOW,
        EVENT_OBJECT_HIDE,
        EVENT_OBJECT_DESTROY,
        EVENT_OBJECT_REORDER,
        EVENT_OBJECT_PARENTCHANGE,
        EVENT_SYSTEM_FOREGROUND
    };

    for (DWORD event : events) {
        HWINEVENTHOOK hook = SetWinEventHook(
            event,
            event,
            nullptr,
            desktopHostWindowEventProc,
            0,
            0,
            WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        if (hook) {
            m_desktopHostWindowEventHooks.append(hook);
        }
    }

    rescanDesktopHostChildren();
}

void LinDesktop::stopDesktopHostWindowMonitor()
{
    for (HWINEVENTHOOK hook : m_desktopHostWindowEventHooks) {
        if (hook) {
            UnhookWinEvent(hook);
        }
    }
    m_desktopHostWindowEventHooks.clear();

    if (g_desktopHostWindowMonitorOwner == this) {
        g_desktopHostWindowMonitorOwner.clear();
    }
}

void LinDesktop::handleDesktopHostWindowEvent(HWND hwnd, DWORD event)
{
    if (hwnd && isOwnLinWidgetRuntimeWindowOrDescendant(hwnd)) {
        return;
    }

    if (event == EVENT_OBJECT_REORDER ||
        event == EVENT_OBJECT_SHOW ||
        event == EVENT_OBJECT_HIDE ||
        event == EVENT_SYSTEM_FOREGROUND) {
        scheduleLinWidgetLayerRecovery();
    }

    if (event == EVENT_OBJECT_DESTROY || event == EVENT_OBJECT_HIDE || event == EVENT_OBJECT_PARENTCHANGE) {
        scheduleDesktopHostChildrenRescan();
        return;
    }

    if (!hwnd || isWindowUnderDesktopHost(hwnd) || isDesktopHostWindow(hwnd) || hwnd == reinterpret_cast<HWND>(winId())) {
        scheduleDesktopHostChildrenRescan();
    }
}

void LinDesktop::scheduleLinWidgetLayerRecovery()
{
    if (!listView || m_linWidgetLayerRecoveryPending) {
        return;
    }

    m_linWidgetLayerRecoveryPending = true;
    const int delays[] = { 50, 200, 600, 1200 };
    for (int delay : delays) {
        QTimer::singleShot(delay, this, [this, delay]() {
            if (listView) {
                listView->syncLinWidgetLayer();
            }
            if (delay == 1200) {
                m_linWidgetLayerRecoveryPending = false;
            }
        });
    }
}

void LinDesktop::scheduleDesktopHostChildrenRescan()
{
    if (m_desktopHostRescanPending) {
        m_desktopHostRescanAgain = true;
        return;
    }

    m_desktopHostRescanPending = true;
    QTimer::singleShot(100, this, [this]() {
        m_desktopHostRescanPending = false;
        rescanDesktopHostChildren();
        if (m_desktopHostRescanAgain) {
            m_desktopHostRescanAgain = false;
            scheduleDesktopHostChildrenRescan();
        }
    });
}

void LinDesktop::rescanDesktopHostChildren()
{
    HWND conflictHwnd = findThirdPartyDesktopHostChild();
    const bool hasConflict = conflictHwnd != nullptr;
    if (m_hasDesktopHostConflict == hasConflict && m_desktopHostConflictHwnd == conflictHwnd) {
        return;
    }

    const bool hadConflict = m_hasDesktopHostConflict;
    m_hasDesktopHostConflict = hasConflict;
    m_desktopHostConflictHwnd = conflictHwnd;

    if (hasConflict) {
        qWarning() << "[LinDesktopHostMonitor] third-party window under desktop host"
                   << "hwnd=" << conflictHwnd
                   << "class=" << windowClassName(conflictHwnd);
        showDesktopHostConflictDialog(conflictHwnd);
    } else {
        m_desktopHostConflictDialogShown = false;
        qWarning() << "[LinDesktopHostMonitor] desktop host conflict cleared";
        if (hadConflict) {
            repairDesktopHostPresentation();
        }
    }
}

void LinDesktop::showDesktopHostConflictDialog(HWND conflictHwnd)
{
    if (m_desktopHostConflictDialogShown || m_desktopHostConflictDialog) {
        return;
    }

    m_desktopHostConflictDialogShown = true;
    FluentDialog* dialog = new FluentDialog(nullptr);
    m_desktopHostConflictDialog = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->setDialogTitle(tr("Desktop Manager Conflict Detected"));

    const QString className = windowClassName(conflictHwnd);
    const QString detail = className.isEmpty()
        ? tr("LinDesk detected another desktop management window under the Windows desktop host.")
        : tr("LinDesk detected another desktop management window under the Windows desktop host: %1.").arg(className);
    dialog->setMessage(detail + QStringLiteral("\n\n") +
        tr("Please close the third-party desktop management software to avoid desktop display conflicts."));
    dialog->addActionButton(tr("OK"), FluentDialog::ActionType::Primary);
    connect(dialog, &QObject::destroyed, this, [this]() {
        m_desktopHostConflictDialog = nullptr;
    });
    dialog->show();
}

void LinDesktop::startSingleInstanceServer()
{
    QLocalServer::removeServer(QString::fromLatin1(kSingleInstanceServerName));
    m_ipcServer = new QLocalServer(this);
    connect(m_ipcServer, &QLocalServer::newConnection, this, [this]() {
        while (m_ipcServer && m_ipcServer->hasPendingConnections()) {
            QLocalSocket* socket = m_ipcServer->nextPendingConnection();
            connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
                const QByteArray data = socket->readAll();
                const QString command = QString::fromUtf8(data).trimmed();
                handleSingleInstanceCommand(command);
            });
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    m_ipcServer->listen(QString::fromLatin1(kSingleInstanceServerName));
}

void LinDesktop::handleSingleInstanceCommand(const QString& command)
{
    qWarning() << "[LinDesktop] Received single instance command:" << command;
    if (command == QStringLiteral("RESTORE")) {
        // 1. 打开托盘图标
        if (m_trayIcon) {
            m_trayIcon->show();
        }

        // 2. 执行桌面自愈重挂载
        selfHealAndRestoreDesktop();

        // 3. 当程序显示出来时，自动恢复为用户设置的隐藏或显示状态
        if (m_trayIcon && isVisible()) {
            m_trayIcon->hide();
        }
    } else if (command == QStringLiteral("SHOW_TRAY")) {
        // 取消恢复桌面：保持当前桌面状态，把托盘打开以供随时操作
        if (m_trayIcon) {
            m_trayIcon->show();
            if (!m_hasShownTrayHint) {
                m_trayIcon->showMessage(
                    tr("LinDesk"),
                    tr("LinDesk is still running in the background. Right-click here to restore or exit."),
                    QSystemTrayIcon::Information,
                    3000
                );
                m_hasShownTrayHint = true;
            }
        }
    }
}

void LinDesktop::selfHealAndRestoreDesktop()
{
    HWND selfHwnd = reinterpret_cast<HWND>(winId());
    if (!selfHwnd || !IsWindow(selfHwnd)) {
        return;
    }

    qWarning() << "[LinDesktop] executing selfHealAndRestoreDesktop";

    if (windowState() & Qt::WindowMinimized) {
        setWindowState(windowState() & ~Qt::WindowMinimized);
    }

    HWND realDesktop = CSystemApi::FindRealDesktop();
    if (realDesktop && IsWindow(realDesktop)) {
        HWND currentParent = GetParent(selfHwnd);
        if (currentParent != realDesktop) {
            qWarning() << "[LinDesktop] Desktop host parent changed from" << currentParent << "to" << realDesktop << ", re-embedding...";
            SetParent(selfHwnd, realDesktop);

            LONG_PTR exStyle = GetWindowLongPtr(selfHwnd, GWL_EXSTYLE);
            exStyle &= ~WS_EX_LAYERED;
            SetWindowLongPtr(selfHwnd, GWL_EXSTYLE, exStyle);
            m_isEmbedded = true;
        }
    }

    setGeometry(QApplication::primaryScreen()->geometry());
    SetWindowPos(
        selfHwnd,
        HWND_TOP,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_FRAMECHANGED
    );

    show();
    showNormal();
    raise();
    activateWindow();

    if (listView) {
        listView->setGeometry(rect());
        listView->show();
        listView->raise();
        listView->update();
    }
    forceDesktopRepaint();

    qWarning() << "[LinDesktop] selfHealAndRestoreDesktop completed successfully";
}

void LinDesktop::repairDesktopHostPresentation()
{
    selfHealAndRestoreDesktop();
}

HWND LinDesktop::findThirdPartyDesktopHostChild() const
{
    QVector<HWND> hosts;
    if (HWND progman = FindWindow(_T("progman"), _T("program manager"))) {
        hosts.append(progman);
    }

    EnumWindows([](HWND hwnd, LPARAM lparam) -> BOOL {
        auto* hosts = reinterpret_cast<QVector<HWND>*>(lparam);
        wchar_t className[256] = {};
        if (GetClassNameW(hwnd, className, static_cast<int>(std::size(className))) &&
            _wcsicmp(className, L"WorkerW") == 0) {
            hosts->append(hwnd);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&hosts));

    for (HWND host : hosts) {
        QVector<HWND> pendingChildren;
        for (HWND child = GetWindow(host, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            pendingChildren.append(child);
        }

        while (!pendingChildren.isEmpty()) {
            HWND child = pendingChildren.takeLast();
            if (isCandidateThirdPartyWindow(child)) {
                return child;
            }

            for (HWND grandChild = GetWindow(child, GW_CHILD); grandChild; grandChild = GetWindow(grandChild, GW_HWNDNEXT)) {
                pendingChildren.append(grandChild);
            }
        }
    }

    return nullptr;
}

bool LinDesktop::isDesktopHostWindow(HWND hwnd) const
{
    if (!hwnd || !IsWindow(hwnd)) {
        return false;
    }

    const QString className = windowClassName(hwnd);
    return className.compare(QStringLiteral("WorkerW"), Qt::CaseInsensitive) == 0 ||
        className.compare(QStringLiteral("Progman"), Qt::CaseInsensitive) == 0;
}

bool LinDesktop::isWindowUnderDesktopHost(HWND hwnd) const
{
    for (HWND parent = GetParent(hwnd); parent; parent = GetParent(parent)) {
        if (isDesktopHostWindow(parent)) {
            return true;
        }
    }
    return false;
}

bool LinDesktop::isOwnProcessWindow(HWND hwnd) const
{
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    return processId == GetCurrentProcessId();
}

bool LinDesktop::isOwnLinWidgetRuntimeWindow(HWND hwnd) const
{
    const QString className = windowClassName(hwnd);
    return className.startsWith(QStringLiteral("Qt5152"), Qt::CaseInsensitive) &&
        className.contains(QStringLiteral("QWindow"), Qt::CaseInsensitive);
}

bool LinDesktop::isOwnLinWidgetRuntimeWindowOrDescendant(HWND hwnd) const
{
    for (HWND current = hwnd; current && IsWindow(current); current = GetParent(current)) {
        if (isOwnProcessWindow(current) || isOwnLinWidgetRuntimeWindow(current)) {
            return true;
        }

        if (isDesktopHostWindow(current)) {
            break;
        }
    }
    return false;
}

bool LinDesktop::isCandidateThirdPartyWindow(HWND hwnd) const
{
    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd) || isOwnLinWidgetRuntimeWindowOrDescendant(hwnd)) {
        return false;
    }

    const QString className = windowClassName(hwnd);
    if (className.compare(QStringLiteral("SHELLDLL_DefView"), Qt::CaseInsensitive) == 0 ||
        className.compare(QStringLiteral("SysListView32"), Qt::CaseInsensitive) == 0) {
        return false;
    }

    RECT rect = {};
    return GetWindowRect(hwnd, &rect) && !IsRectEmpty(&rect);
}

QString LinDesktop::windowClassName(HWND hwnd) const
{
    if (!hwnd || !IsWindow(hwnd)) {
        return QString();
    }

    wchar_t className[256] = {};
    if (!GetClassNameW(hwnd, className, static_cast<int>(std::size(className)))) {
        return QString();
    }
    return QString::fromWCharArray(className);
}

QString LinDesktop::windowProcessImageName(HWND hwnd) const
{
    if (!hwnd || !IsWindow(hwnd)) {
        return QString();
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId == 0) {
        return QString();
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, processId);
    if (!process) {
        process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    }
    if (!process) {
        return QString();
    }

    wchar_t path[MAX_PATH] = {};
    QString result;
    if (GetModuleFileNameExW(process, nullptr, path, static_cast<DWORD>(std::size(path)))) {
        result = QFileInfo(QString::fromWCharArray(path)).fileName();
    }
    CloseHandle(process);
    return result;
}

void LinDesktop::forceDesktopRepaint()
{
    HWND selfHwnd = reinterpret_cast<HWND>(winId());
    if (!selfHwnd || !IsWindow(selfHwnd)) {
        return;
    }

    RedrawWindow(selfHwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    if (listView) {
        listView->update();
    }
    update();
}

void LinDesktop::unembedFromDesktop()
{
    SetParent((HWND)this->winId(), NULL);
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    //setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAutoFillBackground(false);
    m_isEmbedded = false;

    QRect screenGeom = QApplication::primaryScreen()->availableGeometry();
    resize(screenGeom.width() * 0.8, screenGeom.height() * 0.8);
    move(screenGeom.center() - rect().center());

    show();
}

void LinDesktop::mousePressEvent(QMouseEvent* event)
{
    if (listView) {
        listView->clearWidgetFocus();
    }
    if (event->button() == Qt::LeftButton && listView) {
        listView->activateWindow();
        listView->setFocus(Qt::MouseFocusReason);
        listView->beginBackgroundSelection(event->pos());
        m_lastSelectRect = QRect();
        if (!mouseGrabber()) {
            grabMouse();
        }
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void LinDesktop::keyPressEvent(QKeyEvent* event)
{
    if (listView) {
        QApplication::sendEvent(listView, event);
        if (event->isAccepted()) {
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

void LinDesktop::keyReleaseEvent(QKeyEvent* event)
{
    if (listView) {
        QApplication::sendEvent(listView, event);
        if (event->isAccepted()) {
            return;
        }
    }
    QWidget::keyReleaseEvent(event);
}

void LinDesktop::mouseMoveEvent(QMouseEvent* event)
{
    if (!listView || !listView->isBackgroundSelectionActive()) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    QRect rect;
    listView->updateBackgroundSelection(event->pos(), &rect);
    m_lastSelectRect = rect;
    event->accept();
}

void LinDesktop::mouseReleaseEvent(QMouseEvent* event)
{
    if (listView && listView->isBackgroundSelectionActive()) {
        clearSelectionOverlay();
        listView->endBackgroundSelection();
        if (mouseGrabber() == this) {
            releaseMouse();
        }
        m_lastSelectRect = QRect();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void LinDesktop::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);

    if (!showDefaultWallpaper()) {
        painter.fillRect(rect(), Qt::transparent);
        return;
    }

    if (m_transitionOpacity < 1.0f && !m_prevScaledBackImagePixmap.isNull()) {
        painter.setOpacity(1.0f - m_transitionOpacity);
        painter.drawPixmap(QPoint(0, 0), m_prevScaledBackImagePixmap);
    }

    if (!m_scaledBackImagePixmap.isNull()) {
        painter.setOpacity(m_transitionOpacity);
        painter.drawPixmap(QPoint(0, 0), m_scaledBackImagePixmap);
    } else {
        painter.fillRect(rect(), Qt::black);
    }
}

void LinDesktop::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (listView) {
        listView->setGeometry(rect());
    }
    rebuildScaledWallpaperPixmaps();
    if (selectArea) {
        selectArea->syncToRect(listView ? listView->rect() : rect());
    }
    update();
}

void LinDesktop::closeEvent(QCloseEvent* event)
{
    const bool forceQuit = QCoreApplication::instance() &&
        QCoreApplication::instance()->property("LinDeskForceQuit").toBool();
    if (!forceQuit && !m_isQuittingFromTray && shouldMinimizeToTray() && m_trayIcon) {
        if (!m_trayIcon->isVisible()) {
            m_trayIcon->show();
        }

        hide();
        event->ignore();

        if (!m_hasShownTrayHint) {
            m_trayIcon->showMessage(tr("LinDesk"), tr("LinDesk is still running in the system tray."), QSystemTrayIcon::Information, 2500);
            m_hasShownTrayHint = true;
        }
        return;
    }

    QWidget::closeEvent(event);
}

void LinDesktop::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::WindowStateChange && !m_isQuittingFromTray && shouldMinimizeToTray() && m_trayIcon) {
        if (isMinimized()) {
            QTimer::singleShot(0, this, [this]() {
                if (!isMinimized()) {
                    return;
                }

                if (!m_trayIcon->isVisible()) {
                    m_trayIcon->show();
                }

                hide();
                setWindowState(windowState() & ~Qt::WindowMinimized);

                if (!m_hasShownTrayHint) {
                    m_trayIcon->showMessage(tr("LinDesk"), tr("LinDesk is still running in the system tray."), QSystemTrayIcon::Information, 2500);
                    m_hasShownTrayHint = true;
                }
            });
        } else if (isVisible() && m_trayIcon->isVisible()) {
            m_trayIcon->hide();
        }
    }

    QWidget::changeEvent(event);
}

void LinDesktop::contextMenuEvent(QContextMenuEvent* event)
{
    Q_UNUSED(event);
}

bool LinDesktop::nativeEvent(const QByteArray& eventType, void* message, long* result)
{
    MSG* msg = static_cast<MSG*>(message);
    if (msg && m_restoreWindowMessage != 0 && msg->message == m_restoreWindowMessage) {
        handleSingleInstanceCommand(QStringLiteral("RESTORE"));
        if (result) *result = 0;
        return true;
    }
    if (msg && m_showTrayWindowMessage != 0 && msg->message == m_showTrayWindowMessage) {
        handleSingleInstanceCommand(QStringLiteral("SHOW_TRAY"));
        if (result) *result = 0;
        return true;
    }
    if (msg && m_shellHookMessage != 0 && msg->message == m_shellHookMessage) {
        const WPARAM shellEvent = msg->wParam;
        if (shellEvent == HSHELL_WINDOWACTIVATED ||
            shellEvent == HSHELL_RUDEAPPACTIVATED ||
            shellEvent == HSHELL_REDRAW ||
            shellEvent == HSHELL_WINDOWREPLACED ||
            shellEvent == HSHELL_WINDOWREPLACING) {
            HWND eventHwnd = reinterpret_cast<HWND>(msg->lParam);
            if (eventHwnd && isOwnLinWidgetRuntimeWindowOrDescendant(eventHwnd)) {
                return QWidget::nativeEvent(eventType, message, result);
            }
            scheduleLinWidgetLayerRecovery();
        }
        return QWidget::nativeEvent(eventType, message, result);
    }
    if (msg && msg->message == WM_DPICHANGED) {
        QRect suggestedNativeRect;
        if (RECT* suggestedRect = reinterpret_cast<RECT*>(msg->lParam)) {
            suggestedNativeRect = QRect(
                suggestedRect->left,
                suggestedRect->top,
                suggestedRect->right - suggestedRect->left,
                suggestedRect->bottom - suggestedRect->top);
            logDesktopDpiTrace("wmDpiChangedBefore", this, suggestedNativeRect);
            const UINT newDpi = HIWORD(msg->wParam);
            const QRect logicalGeometry = DpiCoordinateHelper::nativeRectToQtGlobalRectForDpi(*suggestedRect, newDpi);
            setGeometry(logicalGeometry);
        }
        QTimer::singleShot(0, this, [this]() {
            connectScreenSignals(screen());
            handleDpiOrScreenMetricsChanged();
            logDesktopDpiTrace("wmDpiChangedAfter", this);
        });
        if (result) {
            *result = 0;
        }
        return true;
    }
    return QWidget::nativeEvent(eventType, message, result);
}

