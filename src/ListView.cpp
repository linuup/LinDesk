#include "ListView.h"
#include <QPushButton>
// 静态成员初始化
ListView* ListView::m_instance = nullptr;

ListView::ListView(QWidget* parent) : QWidget(parent) {
    // 设置桌面背景图片
    //SystemParametersInfo(SPI_SETDESKWALLPAPER, 0, (PVOID)imagePath.toStdString().c_str(), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    setWindowTitle("LinDesk");
    setObjectName("LinDesk");
    setMouseTracking(true);
    // 设置无边框窗口
    //setWindowFlags(Qt::FramelessWindowHint | Qt::WindowSystemMenuHint);

    // 设置透明背景
    //setAttribute(Qt::WA_TranslucentBackground);

    // 设置透明并允许透过窗口看到后面的内容
    //setAttribute(Qt::WA_TransparentForMouseEvents); // 允许点击透过窗口
    //setAttribute(Qt::WA_OpaquePaintEvent);  // 启用背景透明化

    setGeometry(QApplication::primaryScreen()->geometry());
    
    // 按y展开，到底部开始第二列
    loadIconLayer();

    qDebug() << m_items.size();
    selectArea = new SelectArea(this);
    rightMenu = RightMenu::getInstance();
    rightMenu->setHWND((HWND)this->winId());
    update();
	//QPushButton* b = new QPushButton(this);
	//b->setText("加载图标");
	//b->move(200, 200);
 //   // 连接按钮的点击信号到槽函数
 //   connect(b, &QPushButton::clicked, this, [this](){
 //       m_items.clear();
 //       loadIconLayer();
 //       update();
	//});

}

ListView::~ListView() {
    // 清理 ListItem 对象
    //qDeleteAll(m_listItems);
}



void ListView::paintEvent(QPaintEvent* event)
{
    //qDebug() << "MouseMoveEvent:Drawing";
    QPainter q(this);
    q.fillRect(0, 0, 1920, 1080, Qt::darkBlue);
    QPainter qPainter_item(this);
    if (m_items.size() <= 0) {
        return;
    }
    // 设置蓝色边框的颜色和宽度
    //QPen pen;
    //if (m_itemData.specialFolderType() == SpecialFolders::UnknownFolder) {
    //    pen.setColor(Qt::red);  // 设置边框颜色为蓝色
    //}
    //else {
    //    pen.setColor(Qt::blue);  // 设置边框颜色为蓝色
    //}
    if (currentHoverItemIndex != -1) {
        q.setRenderHint(QPainter::Antialiasing);  // 开启抗锯齿

        // 设置背景颜色
        q.setBrush(QBrush(QColor(224, 224, 224, 0.1)));  // rgba(224, 224, 224, 0.3)

        // 设置边框颜色（如果需要边框）
        q.setPen(Qt::NoPen);  // 如果不需要边框的话，使用 NoPen

        // 绘制圆角矩形
        
        q.drawRoundedRect(this->currentHoverItemRect, 5, 5);  // x, y, width, height, border radius X, border radius Y
    }
    qPainter_item.setRenderHint(QPainter::Antialiasing);  // 启用抗锯齿
    for(auto it = m_items.begin();it!=m_items.end();it++)
    {
        ListItem* item = *it;
		QPoint pos = item->pos();
		QSize size = item->size();
        //qPainter_item.fillRect(item->rect(), Qt::yellow);

        // 绘制图标
        QPixmap pixmap = item->icon().pixmap(48, 48); // 获取48x48的图标
        int iconX = (size.width() - pixmap.width()) / 2; // 图标水平居中
        int iconY = 10; // 图标距离顶部10像素
        qPainter_item.drawPixmap(pos, pixmap);


        // 计算文字宽度并居中
        int textWidth = qPainter_item.fontMetrics().horizontalAdvance(item->baseName());
        int textX = (size.width() - textWidth) / 2; // 文本水平居中
        int textY = size.height() - 10; // 距离底部10像素
        // 绘制文字
		qPainter_item.setPen(Qt::white);
        //qPainter_item.drawText(10, size.height() - 10, "test");
        qPainter_item.drawText(pos, item->baseName());
        //qDebug() << "=====================" << item->baseName();
	}
    if (choosingFlag) {
        q.fillRect(drawingMaskRect, Qt::blue);
        update(drawingMaskRect);
    }
    
    
    
    //qDebug() << "drawingMaskRect:" << this->drawingMaskRect;

}



void ListView::contextMenuEvent(QContextMenuEvent* event)
{
    if (event->reason() == QContextMenuEvent::Mouse && this->rect().contains(event->pos())) {
        auto pointRight =  mapToGlobal(event->pos());
		//循环遍历所有图标，检查鼠标右键点击位置是否在某个图标的区域内
		for (auto it = m_items.begin(); it != m_items.end(); it++)
		{
			ListItem* item = *it;
			QRect itemRect = item->rect();
            if (item->type() == ObjectType::SpecialFolder || ObjectType::Empty_ObjectType) {
                return;
            }
			if (itemRect.contains(pointRight)) {
                qDebug() << "图标范围："<< itemRect;
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

void ListView::mouseMoveEvent(QMouseEvent* event)
{
    /*currentHoverItemIndex = -1;
    update(currentHoverItemRect);*/
    // draw mouse hover effect
    if (!this->choosingFlag) {
        auto currentPoint = event->pos();
        if (!currentHoverItemRect.contains(currentPoint)) {
            currentHoverItemIndex = -1;
            update();
            for (auto it = m_items.begin(); it != m_items.end(); it++)
            {
                ListItem* item = *it;
                QRect itemRect = item->rect();
                if (item->type() == ObjectType::SpecialFolder || ObjectType::Empty_ObjectType) {
                    return;
                }
                if (itemRect.contains(currentPoint)) {
                    // 鼠标右键点击位置在该图标区域内，显示该图标的右键菜单

                    currentHoverItemIndex = std::distance(m_items.begin(), it);
                    currentHoverItemRect = itemRect;
                    //qDebug() << "MouseMoveEvent:Hovering on item[Index:"<<currentHoverItemIndex <<"]" << item->baseName();
                    update(currentHoverItemRect);
                    return; // 退出循环，避免重复处理
                }
            }
        }
    }
   
	
    //qDebug() << "MouseMoveEvent:CurrentPoint:" << currentPoint;
	//update drawingMask 
    if (event->buttons() && Qt::LeftButton) {
        int start_x = startPos.x();
        int start_y = startPos.y();

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
		//drawingMaskRect = rect;
        selectArea->rect = rect;
        selectArea->resize(rect.width(), rect.height());
        selectArea->move(rect.x(), rect.y());
        update(rect);
    }
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
        ListItem* listItem = new ListItem();
        listItem->setItemSize(iconWidth, iconHeight);
        listItem->setItemPos(Icon_X, Icon_Y);
        listItem->setItemData(std::move(*it));
        listItem->setObjectName(QUuid::createUuid().toString(QUuid::WithoutBraces));
        listItem->initData();
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