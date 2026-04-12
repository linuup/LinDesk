#include "LinDesktop.h"

LinDesktop::LinDesktop(QWidget* parent)
	: QWidget(parent)
{
	setWindowTitle("LinDesk");
	setObjectName("LinDesk");
	setGeometry(QApplication::primaryScreen()->geometry());
	
	// 设置透明背景
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
    m_wallpaperLoadTimer->setInterval(500); // 500ms debounce
    connect(m_wallpaperLoadTimer, &QTimer::timeout, this, &LinDesktop::loadBackImage);
	
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
    // Store current as previous for transition
    if (!m_backImagePixmap.isNull()) {
        m_prevBackImagePixmap = m_backImagePixmap;
    }

    // Get path from API
    QString path = SysInfoUtil::GetWallpaperUsingAPI();
    qDebug() << "[LinDesktop] Loading wallpaper from:" << path;

    QImage newImage;
    if (path.isEmpty() || !newImage.load(path)) {
        // Fallback or retry? Registry might be ahead of the file system
        qDebug() << "[LinDesktop] Failed to load wallpaper. Retrying in 1s...";
        QTimer::singleShot(1000, this, &LinDesktop::loadBackImage);
        return;
    }

    // Success! Update current path
    m_currentWallpaperPath = path;

    // Store current as previous for transition
    if (!m_backImagePixmap.isNull()) {
        m_prevBackImagePixmap = m_backImagePixmap;
    }

    m_backImagePixmap = QPixmap::fromImage(newImage);

    // Trigger transition if we have a previous image
    if (!m_prevBackImagePixmap.isNull()) {
        m_transitionAnimation->stop();
        m_transitionAnimation->start();
    }
    else {
        m_transitionOpacity = 1.0f;
        update();
    }
}
void LinDesktop::requestHoverEffect()
{
}
void LinDesktop::embedToDesktop()
{
	HWND hwnd = CSystemApi::FindRealDesktop();
	if (hwnd) {
		setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
		
		// Ensure 1:1 with desktop size
		setGeometry(QApplication::primaryScreen()->geometry());
		
		SetParent((HWND)this->winId(), hwnd);
		m_isEmbedded = true;
		show();
	}
}

void LinDesktop::unembedFromDesktop()
{
	SetParent((HWND)this->winId(), NULL);
	
	// Restore standard window flags: Title bar + Taskbar icon
	setWindowFlags(Qt::Window | Qt::CustomizeWindowHint | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint | Qt::WindowTitleHint);
	
	m_isEmbedded = false;
	
	// If it was full screen, shrink it a bit so the title bar is easier to see/move
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

void LinDesktop::mouseMoveEvent(QMouseEvent* event) {
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
	QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    QRect targetRect = rect();

    // Draw previous wallpaper during transition (fading out)
    if (m_transitionOpacity < 1.0f && !m_prevBackImagePixmap.isNull()) {
        painter.setOpacity(1.0f - m_transitionOpacity);
        painter.drawPixmap(targetRect, m_prevBackImagePixmap.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    }

    // Draw current wallpaper with transition opacity
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
}

bool LinDesktop::nativeEvent(const QByteArray& eventType, void* message, long* result)
{
	return QWidget::nativeEvent(eventType, message, result);
}
