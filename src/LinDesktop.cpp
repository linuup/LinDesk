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
		SetParent((HWND)this->winId(), hwnd);
	}
}

void LinDesktop::mousePressEvent(QMouseEvent* event)
{
	m_startPos = event->pos();
	update();
}

void LinDesktop::mouseMoveEvent(QMouseEvent* event) {
	//qDebug() << "LinDesktop MouseMoveEvent Pos:" << event->pos();

	//qDebug() << "MouseMoveEvent:CurrentPoint:" << currentPoint;
	// 
	//update drawingMaskRect
	int start_x = m_startPos.x();
	int start_y = m_startPos.y();
	QRect rect(start_x, start_y, event->x() - start_x, event->y() - start_y);

	int rectWidth = abs(rect.width());
	int rectHeight = abs(rect.height());
	if (rectWidth == 0 && rectHeight == 0) {
		return;
	}

	// 只有当移动距离超过阈值（如3像素）时才显示选择框，防止单击时闪烁
	if (!selectArea->isVisible() && (rectWidth > 3 || rectHeight > 3)) {
		selectArea->show();
	}
	
	if (!selectArea->isVisible()) {
		return;
	}

	//矩形在左上角
	if (rect.width() < 0 && rect.height() < 0) {
		rect.setRect(start_x + rect.width(), start_y + rect.height(), rectWidth, rectHeight);
	}
	//矩形在右上角
	else if (rect.width() > 0 && rect.height() < 0) {
		rect.setRect(start_x, start_y - rectHeight, rectWidth, rectHeight);
	}//矩形在左下角
	else if (rect.width() < 0 && rect.height() > 0) {
		rect.setRect(start_x - rectWidth, start_y, rectWidth, rectHeight);

	}
	
	selectArea->rect = rect;
	//qDebug() << rect;
	listView->refreshCheckedItem(rect);
	// 只更新新旧选区重叠的区域
	rect = m_lastSelectRect.united(rect);

	update(rect); // 仅更新必要区域
	m_lastSelectRect = rect;

}
void LinDesktop::mouseReleaseEvent(QMouseEvent* event)
{

	//qDebug() << "ListView mouseReleaseEvent  Pos:" << event->pos();
	listView->DisableSelecting();
	m_startPos = QPoint(0, 0);
	selectArea->hide();
	//selecting = false;
	selectArea->rect = QRect();
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
    update();
}

void LinDesktop::contextMenuEvent(QContextMenuEvent* event)
{
}

bool LinDesktop::nativeEvent(const QByteArray& eventType, void* message, long* result)
{
	return QWidget::nativeEvent(eventType, message, result);
}
