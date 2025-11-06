#include "LinDesktop.h"
LinDesktop::LinDesktop(QWidget* parent)
{
	setWindowTitle("LinDesk");
	setObjectName("LinDesk");
	setGeometry(QApplication::primaryScreen()->geometry());
	// 设置无边框窗口
	//setWindowFlags(Qt::FramelessWindowHint | Qt::WindowSystemMenuHint);

	// 设置透明背景
	setAttribute(Qt::WA_TranslucentBackground);
	//setMouseTracking(true); // 启用鼠标追踪
	// 设置透明并允许透过窗口看到后面的内容
	//setAttribute(Qt::WA_TransparentForMouseEvents); // 允许点击透过窗口
	setAttribute(Qt::WA_OpaquePaintEvent);  // 启用背景透明化
	//embedToDesktop();
	backImageLabel = new QLabel(this);
	//loadBackImagePixmap();
	//backImageLabel->setPixmap(m_backImagePixmap);
	//backImageLabel->hide();

	selectArea = new SelectArea(this);
	listView = ListView::instance(this);

	listView->show();


	//// Win32 代码中创建一个文本框
	//// 创建 Win32 编辑控件
	//HWND hwndEdit = CreateWindowEx(0, L"EDIT", L"",
	//    WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
	//    10, 10, 200, 30, (HWND)winId(), NULL, GetModuleHandle(NULL), NULL);
	//Qwindows a= QWindow::fromWinId(reinterpret_cast<WId>(hwndEdit));
	//// 创建 Qt 窗口容器，将 Win32 控件嵌入
	//QWidget* win32Widget = QWidget::createWindowContainer(createWin32Window(), this);
}

LinDesktop::~LinDesktop()
{

}
void LinDesktop::loadBackImagePixmap()
{
	// 加载图片
	QString path = SysInfoUtil::GetDesktopWallpaperPath();
	// 加载原始图片
	QImage originalImage(path);

	QRect applicationRect = this->geometry();
	// 缩放到目标尺寸
	QImage scaledImage = originalImage.scaled(applicationRect.width(), applicationRect.height(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
	QPixmap pixmap = QPixmap::fromImage(scaledImage);
	if (!pixmap.isNull()) {
		// 将图片转换为QBrush用于背景填充
		QBrush background(pixmap);
		setAutoFillBackground(false); // 允许自动填充背景
		setBackgroundRole(QPalette::Window); // 设置背景角色
		QPalette palette;
		palette.setBrush(QPalette::Window, background);
		setPalette(palette);
	}
	m_backImagePixmap = pixmap;
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
	selectArea->show();
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
	if (rect.width() == 0 || rect.height() == 0) {
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
	listView->refreshCheckedItem(rect);
	selectArea->rect = rect;
	qDebug() << rect;
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

	//qDebug() << "=====================LinDesktop Painter";
	QPainter painter(this);

	// 绘制垂直条纹
	int stripeWidth = 50; // 条纹宽度
	if (0) {
		for (int x = 0; x < width(); x += stripeWidth * 2) {
			// 第一种颜色
			painter.fillRect(x, 0, stripeWidth, height(), QColor(171, 100, 192));
			// 第二种颜色（或透明）
			painter.fillRect(x + stripeWidth, 0, stripeWidth, height(), QColor(255, 255, 255));
		}
	}
	else {
		backImageLabel->show();
	}

};

void LinDesktop::contextMenuEvent(QContextMenuEvent* event)
{

}
