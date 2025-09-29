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
	setMouseTracking(true); // 启用鼠标追踪
    // 设置透明并允许透过窗口看到后面的内容
    setAttribute(Qt::WA_TransparentForMouseEvents); // 允许点击透过窗口
    setAttribute(Qt::WA_OpaquePaintEvent);  // 启用背景透明化

	selectArea = new SelectArea(this);
	listView = ListView::instance(this);
	listView->show();
    rightMenu = RightMenu::getInstance();
    rightMenu->setHWND((HWND)this->winId());
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
void LinDesktop::contextMenuEvent(QContextMenuEvent* event)
{
    if (event->reason() == QContextMenuEvent::Mouse && this->rect().contains(event->pos())) {
        auto pointRight = mapToGlobal(event->pos());
        auto items = listView->items();
        //循环遍历所有图标，检查鼠标右键点击位置是否在某个图标的区域内
        for (auto it = items.begin(); it != items.end(); it++)
        {
            ListItem* item = *it;
            QRect itemRect = item->rect();
            if (item->type() == ObjectType::SpecialFolder || ObjectType::Empty_ObjectType) {
                return;
            }
            if (itemRect.contains(pointRight)) {
                qDebug() << "图标范围：" << itemRect;
                qDebug() << "右键点击坐标：" << pointRight;
                // 鼠标右键点击位置在该图标区域内，显示该图标的右键菜单
                requestSystemRightMenu(item->getItemData().type(), pointRight, item->filePath());
                return; // 退出循环，避免重复处理
            }
        }
        // 如果右键点击位置不在任何图标区域内，显示桌面的右键菜单
        requestSystemRightMenu(ObjectType::BackGround, mapToGlobal(event->pos()));
    }
}
