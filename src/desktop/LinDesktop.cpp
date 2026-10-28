#include "desktop/LinDesktop.h"
#include "desktop/ListView.h"
#include "desktop/widgets/linwidget/WidgetManager.h"
#include "desktop/RenameManager.h"
#include "box/BoxManager.h"
#include "ui/Fluent/FluentDialog.h"
#include "ui/Fluent/FluentInfoTip.h"
#include "services/AppPaths.h"
#include "services/DpiCoordinateHelper.h"
#include <QCoreApplication>
#include <QFutureWatcher>
#include <QSettings>
#include <QAction>
#include <QMenu>
#include <QWindow>
#include <QDebug>
#include <psapi.h>


namespace {
QPointer<LinDesktop> g_desktopHostWindowMonitorOwner;

void showAlreadyRunningTipAndQuit()
{
    constexpr int kTipVisibleMs = 2600;
    constexpr int kQuitDelayMs = 2900;

    FluentInfoTip* tip = new FluentInfoTip(nullptr);
    tip->setText(QCoreApplication::translate("LinDesktop", "LinDesk is already running."));
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, tip, &QObject::deleteLater);

    const QRect screenRect = QApplication::primaryScreen()
        ? QApplication::primaryScreen()->availableGeometry()
        : QRect(0, 0, 800, 600);
    tip->showCentered(screenRect, kTipVisibleMs);
    QTimer::singleShot(kQuitDelayMs, QCoreApplication::instance(), &QCoreApplication::quit);
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
        QTimer::singleShot(0, []() {
            showAlreadyRunningTipAndQuit();
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
    loadBackImage();
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
}

LinDesktop::~LinDesktop()
{
    if (m_shellHookRegistered) {
        DeregisterShellHookWindow(reinterpret_cast<HWND>(winId()));
    }
    stopDesktopHostWindowMonitor();

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
    qDebug() << "[LinDesktop] Loading wallpaper from:" << path;

    if (path.isEmpty()) {
        applyFallbackWallpaper(QString(), QStringLiteral("wallpaper path is empty"));
        return;
    }

    if (path == m_pendingWallpaperPath) {
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
    watcher->setFuture(QtConcurrent::run([path]() {
        QImage image;
        image.load(path);
        return image;
    }));
}

void LinDesktop::applyLoadedWallpaper(const QString& path, const QImage& image)
{
    if (image.isNull()) {
        return;
    }

    m_currentWallpaperPath = path;
    m_fallbackWallpaperPath.clear();

    if (!m_backImagePixmap.isNull()) {
        m_prevBackImagePixmap = m_backImagePixmap;
        m_prevScaledBackImagePixmap = m_scaledBackImagePixmap;
    }

    m_backImagePixmap = QPixmap::fromImage(image);
    rebuildScaledWallpaperPixmaps();

    if (!m_prevScaledBackImagePixmap.isNull()) {
        m_transitionAnimation->stop();
        m_transitionAnimation->start();
    } else {
        m_transitionOpacity = 1.0f;
        update();
    }
}

void LinDesktop::applyFallbackWallpaper(const QString& path, const QString& reason)
{
    const QString fallbackKey = path.isEmpty() ? QStringLiteral("__black_wallpaper__") : path;
    if (m_fallbackWallpaperPath == fallbackKey && !m_backImagePixmap.isNull()) {
        return;
    }

    qDebug() << "[LinDesktop] Using generated black wallpaper fallback:" << reason << "path=" << path;

    if (!m_backImagePixmap.isNull()) {
        m_prevBackImagePixmap = m_backImagePixmap;
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
        m_transitionOpacity = 1.0f;
        update();
    }
}

void LinDesktop::rebuildScaledWallpaperPixmaps()
{
    const QSize targetSize = size();
    if (!targetSize.isValid()) {
        m_scaledBackImagePixmap = QPixmap();
        m_prevScaledBackImagePixmap = QPixmap();
        return;
    }

    const auto buildFittedPixmap = [targetSize](const QPixmap& source) {
        if (source.isNull()) {
            return QPixmap();
        }
        if (source.size() == QSize(1, 1)) {
            QPixmap fitted(targetSize);
            fitted.fill(source.toImage().pixelColor(0, 0));
            return fitted;
        }

        QPixmap fitted(targetSize);
        fitted.fill(Qt::transparent);

        QPainter painter(&fitted);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        QPixmap scaled = source.scaled(targetSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const int x = (targetSize.width() - scaled.width()) / 2;
        const int y = (targetSize.height() - scaled.height()) / 2;
        painter.drawPixmap(x, y, scaled);
        return fitted;
    };

    m_scaledBackImagePixmap = buildFittedPixmap(m_backImagePixmap);
    if (!m_prevBackImagePixmap.isNull()) {
        m_prevScaledBackImagePixmap = buildFittedPixmap(m_prevBackImagePixmap);
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

void LinDesktop::repairDesktopHostPresentation()
{
    HWND selfHwnd = reinterpret_cast<HWND>(winId());
    if (!selfHwnd || !IsWindow(selfHwnd)) {
        return;
    }

    qWarning() << "[LinDesktopHostMonitor] repairing desktop host presentation"
               << "self=" << selfHwnd
               << "parent=" << GetParent(selfHwnd)
               << "parentClass=" << windowClassName(GetParent(selfHwnd));

    SetWindowPos(
        selfHwnd,
        HWND_TOP,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_FRAMECHANGED);

    RedrawWindow(selfHwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    update();
    if (listView) {
        listView->update();
    }

    qWarning() << "[LinDesktopHostMonitor] desktop host presentation repaired"
               << "self=" << selfHwnd
               << "parent=" << GetParent(selfHwnd)
               << "parentClass=" << windowClassName(GetParent(selfHwnd));
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
    if (event->button() == Qt::LeftButton && listView) {
        listView->beginBackgroundSelection(event->pos());
        m_lastSelectRect = QRect();
        listView->setFocus(Qt::MouseFocusReason);
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
    QWidget::keyPressEvent(event);
}

void LinDesktop::mouseMoveEvent(QMouseEvent* event)
{
    if (!listView || !listView->isBackgroundSelectionActive()) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    QRect rect;
    const bool shouldShowSelection = listView->updateBackgroundSelection(event->pos(), &rect);

    if (selectArea) {
        selectArea->setSelectionRect(rect);
    }

    if (shouldShowSelection) {
        if (!selectArea->isVisible()) {
            selectArea->show();
        }
        selectArea->lower();
    } else {
        clearSelectionOverlay();
    }

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

