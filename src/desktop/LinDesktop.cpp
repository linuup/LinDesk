#include "desktop/LinDesktop.h"
#include "desktop/RenameManager.h"
#include "box/BoxManager.h"
#include "ui/Fluent/FluentDialog.h"
#include <QCoreApplication>
#include <QFutureWatcher>
#include <QSettings>
#include <QAction>
#include <QMenu>
#include <QWindow>

LinDesktop::LinDesktop(QWidget* parent)
    : QWidget(parent)
{
    m_singleInstanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\LinDesk_SingleInstance");
    if (!m_singleInstanceMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (m_singleInstanceMutex) {
            CloseHandle(m_singleInstanceMutex);
            m_singleInstanceMutex = nullptr;
        }
        QTimer::singleShot(0, QCoreApplication::instance(), &QCoreApplication::quit);
        return;
    }

    m_configPath = QCoreApplication::applicationDirPath() + "/config.ini";
    {
        QSettings settings(m_configPath, QSettings::IniFormat);
        m_isFirstLaunch = settings.value("Firslanuch", true).toBool();
    }

    setWindowTitle(tr("LinDesk"));
    setObjectName("LinDesk");
    setGeometry(QApplication::primaryScreen()->geometry());
    setFocusPolicy(Qt::StrongFocus);
    initializeTrayIcon();

    setAttribute(Qt::WA_TranslucentBackground);

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
    selectArea = new SelectArea(listView);
    selectArea->syncToRect(listView->rect());
    selectArea->lower();
    setFocusProxy(listView);

    embedToDesktop();

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

void LinDesktop::onWallpaperChanged()
{
    qDebug() << "[LinDesktop] Wallpaper registry change detected, debouncing...";
    m_wallpaperLoadTimer->start();
}

void LinDesktop::loadBackImage()
{
    const QString path = SysInfoUtil::GetRobustWallpaperPath();
    qDebug() << "[LinDesktop] Loading wallpaper from:" << path;

    if (path.isEmpty()) {
        qDebug() << "[LinDesktop] Wallpaper path is empty. Retrying in 2s...";
        QTimer::singleShot(2000, this, &LinDesktop::loadBackImage);
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
            qDebug() << "[LinDesktop] Failed to decode wallpaper asynchronously. Retrying in 2s...";
            QTimer::singleShot(2000, this, &LinDesktop::loadBackImage);
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
            listView->show();
        }
    });
    dlg->addActionButton(tr("Keep Windows Layout"), FluentDialog::ActionType::Secondary, [this, markHandled]() {
        listView->show();
        markHandled();
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
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
        setGeometry(QApplication::primaryScreen()->geometry());
        SetParent((HWND)this->winId(), hwnd);
        m_isEmbedded = true;
        show();
    }
}

void LinDesktop::unembedFromDesktop()
{
    SetParent((HWND)this->winId(), NULL);
    setWindowFlags(Qt::Window | Qt::CustomizeWindowHint | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint | Qt::WindowTitleHint);
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
    if (msg && msg->message == WM_DPICHANGED) {
        if (RECT* suggestedRect = reinterpret_cast<RECT*>(msg->lParam)) {
            setGeometry(QRect(
                suggestedRect->left,
                suggestedRect->top,
                suggestedRect->right - suggestedRect->left,
                suggestedRect->bottom - suggestedRect->top));
        }
        QTimer::singleShot(0, this, [this]() {
            connectScreenSignals(screen());
            handleDpiOrScreenMetricsChanged();
        });
        if (result) {
            *result = 0;
        }
        return true;
    }
    return QWidget::nativeEvent(eventType, message, result);
}
