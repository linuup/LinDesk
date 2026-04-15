#include "LinDesktop.h"
#include "Fluent/FluentDialog.h"
#include <QCoreApplication>
#include <QSettings>

LinDesktop::LinDesktop(QWidget* parent)
    : QWidget(parent)
{
    m_configPath = QCoreApplication::applicationDirPath() + "/config.ini";
    {
        QSettings settings(m_configPath, QSettings::IniFormat);
        m_isFirstLaunch = settings.value("Firslanuch", true).toBool();
    }

    setWindowTitle("LinDesk");
    setObjectName("LinDesk");
    setGeometry(QApplication::primaryScreen()->geometry());

    setAttribute(Qt::WA_TranslucentBackground);

    m_transitionAnimation = new QVariantAnimation(this);
    m_transitionAnimation->setDuration(m_transitionDuration);
    m_transitionAnimation->setStartValue(0.0f);
    m_transitionAnimation->setEndValue(1.0f);
    connect(m_transitionAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        setTransitionOpacity(value.toFloat());
    });

    loadBackImage();
    selectArea = new SelectArea(this);
    listView = ListView::instance(this);

    embedToDesktop();

    m_monitor = new DesktopInfoMonitor(this);
    m_monitor->startListening();
    connect(m_monitor, &DesktopInfoMonitor::wallpaperChanged, this, &LinDesktop::onWallpaperChanged);

    m_wallpaperLoadTimer = new QTimer(this);
    m_wallpaperLoadTimer->setSingleShot(true);
    m_wallpaperLoadTimer->setInterval(500);
    connect(m_wallpaperLoadTimer, &QTimer::timeout, this, &LinDesktop::loadBackImage);
    QTimer::singleShot(800, this, &LinDesktop::checkFirstLaunchPrompt);
}

LinDesktop::~LinDesktop()
{
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
    if (!m_backImagePixmap.isNull()) {
        m_prevBackImagePixmap = m_backImagePixmap;
    }

    QString path = SysInfoUtil::GetRobustWallpaperPath();
    qDebug() << "[LinDesktop] Loading wallpaper from:" << path;

    QImage newImage;
    if (path.isEmpty() || !newImage.load(path)) {
        qDebug() << "[LinDesktop] Failed to load wallpaper. Retrying in 2s...";
        // If it's the first load or we have no image, keep retrying.
        // If we already have an image, we can afford to wait.
        QTimer::singleShot(2000, this, &LinDesktop::loadBackImage);
        return;
    }

    m_currentWallpaperPath = path;

    if (!m_backImagePixmap.isNull()) {
        m_prevBackImagePixmap = m_backImagePixmap;
    }

    m_backImagePixmap = QPixmap::fromImage(newImage);

    if (!m_prevBackImagePixmap.isNull()) {
        m_transitionAnimation->stop();
        m_transitionAnimation->start();
    } else {
        m_transitionOpacity = 1.0f;
        update();
    }
}

void LinDesktop::requestHoverEffect()
{
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
    dlg->setDialogTitle(QString::fromUtf8("桌面整理"));
    dlg->setMessage(QString::fromUtf8("检测到这是第一次启动，是否立即整理桌面，还是保持原系统布局？"));
    listView->hide();
    dlg->addActionButton(QString::fromUtf8("整理桌面"), FluentDialog::ActionType::Primary, [this, markHandled]() {
        markHandled();
        if (listView) {
            listView->sortItems(ListView::SortOrderType::SortByName_ASC);
            listView->updateRightMenuState();
            listView->loadAutoLayout();
            listView->update();
            listView->requestSaveBoxes();
            listView->show();
        }
    });
    dlg->addActionButton(QString::fromUtf8("保持原系统布局"), FluentDialog::ActionType::Secondary, [this, markHandled]() {
        listView->show();
        markHandled();
    });
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
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
    }
    QWidget::mousePressEvent(event);
}

void LinDesktop::mouseMoveEvent(QMouseEvent* event)
{
    if (!listView || !listView->isBackgroundSelectionActive()) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    QRect rect;
    const bool shouldShowSelection = listView->updateBackgroundSelection(event->pos(), &rect);

    selectArea->syncToParent();
    selectArea->setSelectionRect(rect);

    if (shouldShowSelection) {
        if (!selectArea->isVisible()) {
            selectArea->show();
        }
        selectArea->raise();
    } else if (selectArea->isVisible()) {
        selectArea->hide();
    }

    m_lastSelectRect = rect;
    QWidget::mouseMoveEvent(event);
}

void LinDesktop::mouseReleaseEvent(QMouseEvent* event)
{
    if (listView) {
        listView->endBackgroundSelection();
    }
    selectArea->hide();
    selectArea->setSelectionRect(QRect());
    m_lastSelectRect = QRect();
    QWidget::mouseReleaseEvent(event);
}





void LinDesktop::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    QRect targetRect = rect();

    if (m_transitionOpacity < 1.0f && !m_prevBackImagePixmap.isNull()) {
        painter.setOpacity(1.0f - m_transitionOpacity);
        painter.drawPixmap(targetRect, m_prevBackImagePixmap.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    }

    if (!m_backImagePixmap.isNull()) {
        painter.setOpacity(m_transitionOpacity);
        painter.drawPixmap(targetRect, m_backImagePixmap.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    }
}

void LinDesktop::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (selectArea) {
        selectArea->syncToParent();
    }
    update();
}

void LinDesktop::contextMenuEvent(QContextMenuEvent* event)
{
    Q_UNUSED(event);
}

bool LinDesktop::nativeEvent(const QByteArray& eventType, void* message, long* result)
{
    return QWidget::nativeEvent(eventType, message, result);
}
