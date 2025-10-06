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
    setAttribute(Qt::WA_TransparentForMouseEvents); // 允许点击透过窗口
    setAttribute(Qt::WA_OpaquePaintEvent);  // 启用背景透明化


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
void LinDesktop::requestHoverEffect()
{
}
void LinDesktop::contextMenuEvent(QContextMenuEvent* event)
{
    
}
