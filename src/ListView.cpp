#include "ListView.h"
#include <QPushButton>
#include <QtMath>
// 静态成员初始化
ListView* ListView::m_instance = nullptr;

ListView::ListView(QWidget* parent) : QWidget(parent) {
    // 设置桌面背景图片
    //SystemParametersInfo(SPI_SETDESKWALLPAPER, 0, (PVOID)imagePath.toStdString().c_str(), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    
    
    // 设置无边框窗口
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowSystemMenuHint);

    // 设置透明背景
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    // 设置透明并允许透过窗口看到后面的内容
    //setAttribute(Qt::WA_TransparentForMouseEvents); // 允许点击透过窗口
    //setAttribute(Qt::WA_OpaquePaintEvent);  // 启用背景透明化

    setGeometry(QApplication::primaryScreen()->geometry());
    //setAutoFillBackground(true);
    

    rightMenu = RightMenu::getInstance();
    rightMenu->setHWND((HWND)this->winId());

    // 按y展开，到底部开始第二列
    loadIconLayer();

    qDebug() << m_items.size();
    
    
    this->raise();
    
    QPushButton* b = new QPushButton(this);
    b->setText("First Floor");
    b->move(200, 200);
    // 连接按钮的点击信号到槽函数
    connect(b, &QPushButton::clicked, this, [this](){
        hide();
    });

}

ListView::~ListView() {
    // 清理 ListItem 对象
    //qDeleteAll(m_listItems);
}



void ListView::paintEvent(QPaintEvent* event)
{
    qDebug() << "=====================ListView Painter";
    //QPainter painter(this);
    //painter.fillRect(0,600,1920,1080, QColor(60, 200, 171)); // Clear with transparent color
    //
    ////qDebug() << "MouseMoveEvent:Drawing";
    //QPainter q(this);
    ////q.fillRect(0, 0, 1920, 1080, QColor(228, 171, 171, 255));
    //QPainter qPainter_item(this);
    //if (m_items.size() <= 0) {
    //    return;
    //}
    // 绘制父控件的内容
    /*if (parentWidget()) {
        QPixmap pixmap(parentWidget()->size());
        parentWidget()->render(&pixmap);
        painter.drawPixmap(0, 0, pixmap);
    }*/
    
    //qPainter_item.setRenderHint(QPainter::Antialiasing);  // 启用抗锯齿
    //for (auto it = m_items.begin(); it != m_items.end(); it++)
    //{
    //    ListItem* item = *it;
    //    QPoint pos = item->pos();
    //    QSize size = item->size();
    //    //qPainter_item.fillRect(item->rect(), Qt::yellow);

    //    // 绘制图标
    //    QPixmap pixmap = item->icon().pixmap(48, 48); // 获取48x48的图标
    //    int iconX = (size.width() - pixmap.width()) / 2; // 图标水平居中
    //    int iconY = 10; // 图标距离顶部10像素
    //    qPainter_item.drawPixmap(pos, pixmap);


    //    // 计算文字宽度并居中
    //    int textWidth = qPainter_item.fontMetrics().horizontalAdvance(item->baseName());
    //    int textX = (size.width() - textWidth) / 2; // 文本水平居中
    //    int textY = size.height() - 10; // 距离底部10像素
    //    // 绘制文字
    //    qPainter_item.setPen(Qt::white);
    //    //qPainter_item.drawText(10, size.height() - 10, "test");
    //    qPainter_item.drawText(pos, item->baseName());
    //    //qDebug() << "=====================" << item->baseName();
    //}
    /*if (selecting) {
        q.fillRect(drawingMaskRect, Qt::blue);
        update(drawingMaskRect);
    }*/



    //qDebug() << "drawingMaskRect:" << this->drawingMaskRect;

}

void ListView::contextMenuEvent(QContextMenuEvent* event)
{
    if (event->reason() == QContextMenuEvent::Mouse && this->rect().contains(event->pos())) {
        auto pointRight = mapToGlobal(event->pos());
        auto items = this->items();
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

void ListView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {

		//Dragging Item
        qDebug() << "ListView LeftClick Pos:" << event->pos();
        auto m_dragm_startPos = event->pos();
        auto items = this->items();
        //循环遍历所有图标，检查鼠标右键点击位置是否在某个图标的区域内
        for (auto it = items.begin(); it != items.end(); it++)
        {
            ListItem* item = *it;
            QRect itemRect = item->rect();
            if (item->type() == ObjectType::SpecialFolder || ObjectType::Empty_ObjectType) {
                return;
            }
            if (itemRect.contains(m_dragm_startPos)) {
                /*qDebug() << "图标范围：" << itemRect;
                qDebug() << "左键点击坐标：" << m_dragm_startPos;*/
                m_draggedItem = item; // 记录当前被拖动的 ListItem 

                m_dragm_startPos = event->pos();  // 记录按下时的位置
                m_isDragging = true;  // 开始拖动
                return; // 退出循环，避免重复处理
            }
        }
        m_isSelecting = true; // 开始拖拽选择
        event->ignore(); // 如果没有点击在任何 ListItem 上，忽略事件
		

    }
}

void ListView::mouseMoveEvent(QMouseEvent* event)
{
	//qDebug() << "ListView MouseMoveEvent Pos:" << event->pos();
    // handle dragging
    if (m_isDragging) {
        // 移动 ListItem 控件
        m_draggedItem->move(event->pos().x(), event->pos().y());
        return;
    }

    // draw mouse hover effect
    if (!this->m_isSelecting) {
        auto currentPoint = event->pos();
        // 如果鼠标悬浮的位置没变化，直接返回
        if (currentHoverItemRect.contains(currentPoint)) {

            //previousHoverItemIndex = currentHoverItemIndex;
            //qDebug() << "CurrentHoverItemIndex:" << currentHoverItemIndex;
            return; // 不进行重复更新
        }

        //&& currentHoverItemIndex == previousHoverItemIndex
        //currentHoverItemIndex = -1;
        update();
        auto items = this->items();
        for (auto it = items.begin(); it != items.end(); it++)
        {
            ListItem* item = *it;
            QRect itemRect = item->rect();
            if (item->type() == ObjectType::SpecialFolder || ObjectType::Empty_ObjectType) {
                return;
            }
            if (itemRect.contains(currentPoint)) {

                // 鼠标右键点击位置在该图标区域内，显示该图标的右键菜单
                currentHoverItemIndex = std::distance(items.begin(), it);
                currentHoverItemRect = itemRect;

                qDebug() << "MouseMoveEvent:Hovering on item[Index:" << currentHoverItemIndex << "]" << item->baseName();
                // 使用 QRegion 来更新特定区域
                QRegion region(currentHoverItemRect);
                update(region);  // 仅更新悬浮项区域
                return; // 退出循环，避免重复处理
            }
        }
        currentHoverItemRect = QRect();
        currentHoverItemIndex = -1;
    }


    event->ignore();

}

void ListView::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_isDragging) {
        qDebug() << "ListView mouseReleaseEvent  Pos:" << event->pos();
        m_isDragging = false;  // 结束拖动
        m_draggedItem = nullptr; // 清除被拖动的 ListItem
        m_startPos = QPoint();
        return;
    }
    event->ignore();
    
}





void ListView::enterEvent(QEvent* event)
{

    /*auto currentPoint = mapToGlobal(QCursor::pos());
    qDebug() << "MouseMoveEvent:CurrentPoint:" << currentPoint;*/
}

ListView* ListView::instance(QWidget* parent) {
    // 如果实例不存在，则创建新的实例
    if (!m_instance) {
        m_instance = new ListView(parent);
    }
    return m_instance;
}

std::vector<ListItemData> ListView::loadAllItemsData()
{
    std::vector<ListItemData> allItemsData;
    CSystemApi c;
    std::vector<IconItem> items = c.GetDesktopPIDLItems();
    for (const IconItem item : items) {
        ListItemData* data = new ListItemData(item);
        ////data.setIconItem(item);
        allItemsData.push_back(*data);
    }
    return allItemsData;
}

//计算每个控件的纵向位置（y），并检查是否超出了窗口的高度。
//一旦超出窗口高度，切换到下一列，并重置纵向位置（y）为新的列开始位置。
void ListView::loadIconLayer()
{
    QVector<ListItem*> listItems;
    vector<ListItemData> allDatas = loadAllItemsData();
    // 行、列总数量
    int row = 12;
    int col = 24;
    int rowSpacing = 10; // 每行之间的间距
    int colSpacing = 10; // 每列之间的间距
    int iconWidth = 76;  // 图标宽度
    int iconHeight = 88; // 图标高度

    // 初始位置
    int Icon_X = 2;   // 第一列的 X 坐标
    int Icon_Y = 8;   // 第一行的 Y 坐标

    // 定义文件索引
    int index = 0;
    // 遍历列和行来布局图标
    //迭代器遍历
    for (auto it = allDatas.begin(); it != allDatas.end(); it++)
    {
        ListItem* listItem = new ListItem(this);
        listItem->setParent(this);
        listItem->setItemSize(iconWidth, iconHeight);
        listItem->setItemPos(Icon_X, Icon_Y);
        listItem->setItemData(std::move(*it));
        listItem->setObjectName(QUuid::createUuid().toString(QUuid::WithoutBraces));
        listItem->initData();
        
        listItem->move(Icon_X, Icon_Y);
        listItem->resize(iconWidth, iconHeight);
		listItem->show();
        //listItem->update();
        listItems.push_back(listItem);
        // 更新 Y 坐标，增加图标高度和列间距
        Icon_Y = Icon_Y + iconHeight + rowSpacing;

        // 如果超出窗口高度，切换到下一列，Y 坐标重置为顶部
        if (Icon_Y + iconHeight > height()) {
            // 切换到下一列
            Icon_X = Icon_X + iconWidth + colSpacing;
            Icon_Y = 8;  // 重置 Y 坐标为顶部
        }
    }
    m_items = listItems;
}

void ListView::addItem(ListItem* item)
{
}

void ListView::setSelectedItem(ListItem* item)
{
}

ListItem* ListView::selectedItem() const
{
    return nullptr;
}

void ListView::clearSelection()
{
}

bool ListView::isItemSelected(ListItem* item) const
{
    return false;
}

QVector<ListItem*> ListView::selectedItems() const
{
    return QVector<ListItem*>();
}

void ListView::setMultiSelection(bool enabled)
{
}

bool ListView::isMultiSelection() const
{
    return false;
}

void ListView::setBackgroundImage(const QString& imagePath)
{
}

QString ListView::backgroundImage() const
{
    return QString();
}