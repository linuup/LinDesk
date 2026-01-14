#include "ListView.h"
#include <QPushButton>
#include <QtMath>
#include "Monitor/DirectoryMonitor.h"
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

    monitor = new DesktopIconsMonitor(this);
    startListeningSpecialIcon();

    m_deskMonitor = new DirectoryMonitor(reinterpret_cast<HWND>(winId()),false, this);
    m_deskMonitor->startListening();
    //m_monitor->setDebounceInterval(800);
    //m_monitor->addWatchPath("D:/Desktop/", false);
    //m_monitor->start();

    rightMenu = RightMenu::getInstance();
    rightMenu->initializeCustomMenu();
    rightMenu->setHWND((HWND)this->winId());

    this->initialize();

    this->loadIconLayer();
    this->raise();

    QPushButton* goFirtstLayout = new QPushButton(this);
    goFirtstLayout->setText("First Floor");
    goFirtstLayout->move(600, 200);
    // 连接按钮的点击信号到槽函数
    connect(goFirtstLayout, &QPushButton::clicked, this, [this]() {
        hide();
        });

    QPushButton* refresh = new QPushButton(this);
    refresh->setText("Refresh Desktop");
    refresh->move(600, 100);
    // 连接按钮的点击信号到槽函数
    connect(refresh, &QPushButton::clicked, this, [this]() {
        this->sortItems(SortOrderType::SortByName_DESC);
        loadIconLayer();
        update();
        });
    
    

}
void ListView::startListeningSpecialIcon()
{
    // 开始监听
    if (monitor->startListening()) {
        sysIconRegisterInfo = monitor->getCurrentIcons();

        /*for (const auto& icon : monitor->getCurrentIcons()) {
            qDebug() << icon.name << ":" << (icon.isHidden ? "hiddening" : "showing");
        }*/

        // 设置回收站隐藏
        //monitor->setIconVisibility("{645FF040-5081-101B-9F08-00AA002F954E}", true);
    }
    // 连接信号
    connect(monitor, &DesktopIconsMonitor::iconVisibilityChanged,
        [](const QString& clsid, bool hidden, const QString& name) {
            qDebug() << "icon status:" << name << "->" << (hidden ? "hiddening" : "showing");
        });
}
ListView::~ListView() {
    // 清理 ListItem 对象
    //qDeleteAll(m_listItems);
}
ListItem* ListView::selectItemByPath(const QString path)
{
    for (auto it = m_allItems.begin(); it != m_allItems.end(); it++) {
        ListItem* item = *it;
        auto a = item->filePath();
        if (path == a) {
            return item;
        }
    }
    return nullptr;
}
void ListView::clearCheckedItem()
{
    for (auto* item : this->items()) {
        if (item) {
            item->unCheckSelf();
        }
        
    }
    m_selectedItems.clear();
}

void ListView::refreshCheckedItem(QRect rect)
{
    clearCheckedItem();
    auto items = this->items();
    // 遍历所有item，检查与选择矩形的相交关系

    for (auto* item : items) {
        if (!item) {
            continue;
        }
        // 获取item的矩形区域
        QRect itemRect = item->getChooseItemRect(); // 如果是QRectF需要转换

        // 检查是否相交
        if (itemRect.intersects(rect)) {
            m_selectedItems.append(item);
            item->checkSelf();
        }
    }

}





void ListView::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 绘制垂直条纹
    if (0) {
        int stripeWidth = 50; // 条纹宽度
        for (int x = 0; x < width(); x += stripeWidth * 2) {
            // 第一种颜色
            painter.fillRect(x, 0, stripeWidth, height(), QColor(60, 200, 171));
            // 第二种颜色（或透明）
            painter.fillRect(x + stripeWidth, 0, stripeWidth, height(), QColor(255, 255, 255));
        }
    }
    else {
        //绘制一个背景色
        //painter.fillRect(rect(), QColor(60, 200, 171)); // 浅蓝色背景
    }


    //qDebug() << "drawingMaskRect:" << this->drawingMaskRect;

}

void ListView::contextMenuEvent(QContextMenuEvent* event)
{
    if (event->reason() == QContextMenuEvent::Mouse && this->rect().contains(event->pos())) {
        auto rightClickPoint = mapToGlobal(event->pos());
        auto items = this->items();
        //循环遍历所有图标，检查鼠标右键点击位置是否在某个图标的区域内
        for (auto it = items.begin(); it != items.end(); it++)
        {
            if (!it) {
                continue;
            }
            ListItem* item = *it;
            QRect itemRect = item->getItemRect();
            if (item->type() == ObjectType::Empty_ObjectType) {
                continue;
            }
            if (itemRect.contains(rightClickPoint)) {

                //qDebug() << "图标范围：" << itemRect;
                //qDebug() << "右键点击坐标：" << rightClickPoint;
                if (m_selectedItems.size() == 0) {
                    m_selectedItems.push_back(item);
                }
                // 鼠标右键点击位置在该图标区域内，显示该图标的右键菜单
                requestSystemRightMenu(item->getItemData().type(), rightClickPoint, m_selectedItems);
                return; // 退出循环，避免重复处理
            }
        }
        event->ignore();
        // 如果右键点击位置不在任何图标区域内，显示桌面的右键菜单
        requestSystemRightMenu(ObjectType::BackGround, rightClickPoint, m_selectedItems);
    }
}

void ListView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        clearCheckedItem();
        //Dragging Item
        //qDebug() << "ListView LeftClick Pos:" << event->pos();
        auto m_originalPos = event->pos();
        auto items = this->items();
        //循环遍历所有图标，检查鼠标右键点击位置是否在某个图标的区域内
        for (auto it = items.begin(); it != items.end(); it++)
        {
            if (!it) {
                continue;
            }
            ListItem* item = *it;
            QRect itemRect = item->getItemRect();
            if (item->type() == ObjectType::Empty_ObjectType) {
                continue;
            }
            if (itemRect.contains(m_originalPos)) {
                //qDebug() << item->baseName();
                m_draggedItem = item; // 记录当前被拖动的 ListItem 
                m_draggedItem->raise();
                m_dragOffset = event->pos() - item->pos();
                m_originalPos = event->pos();  // 记录按下时的位置
                m_isDragging = true;  // 开始拖动
                m_selectedItems.append(item);
                item->checkSelf();
                return; // 退出循环，避免重复处理
            }
        }
    }
    m_isSelecting = true; // 开始拖拽选择
    event->ignore(); // 如果没有点击在任何 ListItem 上，忽略事件
}

void ListView::mouseMoveEvent(QMouseEvent* event)
{
    //qDebug() << "ListView MouseMoveEvent Pos:" << event->pos();
    // handle dragging
    if (m_isDragging) {
        QPoint newPos = event->pos() - m_dragOffset;
        // 移动 ListItem 控件
        m_draggedItem->move(newPos);
        m_draggedItem->updateLocationInfo(newPos);
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
        //update();

        //currentHoverItemRect = QRect();
        //currentHoverItemIndex = -1;
    }


    event->ignore();

}

void ListView::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_selectedItems.size() != 0) {
        qDebug() << "ListView mouseReleaseEvent  Pos:" << event->pos();
        qDebug() << "Selected Item" << m_selectedItems.size();
    }

    if (m_isDragging) {
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

void ListView::initialize()
{
    QVector<ListItem*> listItems;
    vector<ListItemData> allDatas = loadAllItemsData();

    //load default Items
    for (auto it = allDatas.begin(); it != allDatas.end(); it++)
    {
        ListItem* listItem = new ListItem(this);
        listItem->setParent(this);
        listItem->setItemData(std::move(*it));
        listItem->setObjectName(QUuid::createUuid().toString(QUuid::WithoutBraces));

        //listItem->update();
        // qDebug() << listItem->baseName();
        if (it->type() != ObjectType::SpecialFolder && listItem->getItemData().fullName() != "desktop.ini") {
            listItems.push_back(listItem);
        }
        else {
            m_hiddenIcons.push_back(listItem);
        }
        m_allItems.push_back(listItem);
    }

    //load System Items
    for (auto it = sysIconRegisterInfo.begin(); it != sysIconRegisterInfo.end(); it++) {
        if (!it->isHidden) {
            for (auto s_item = m_hiddenIcons.begin(); s_item != m_hiddenIcons.end(); s_item++) {
                ListItem* listItem = *s_item;
                auto clsid = listItem->getItemData().getCLSID();
                if (clsid.replace("::", "") == it->clsid) {
                    listItems.push_back(listItem);
                }
            }
        }
    }
    m_items = listItems;

}

//计算每个控件的纵向位置（y），并检查是否超出了窗口的高度。
//一旦超出窗口高度，切换到下一列，并重置纵向位置（y）为新的列开始位置。
void ListView::loadIconLayer()
{
    // 行、列总数量
    int row = 12;
    int col = 24;
    int rowSpacing = 8; // 每行之间的间距
    int colSpacing = 8; // 每列之间的间距
    int iconWidth = 76;  // 图标宽度
    int iconHeight = 78; // 图标高度

    // 初始位置
    int marginTop = 6;
    int marginLeft = 2;
    int Icon_X = marginLeft;   // 第一列的 X 坐标
    int Icon_Y = marginTop;   // 第一行的 Y 坐标

    // 定义文件索引
    int index = 0;
    //load UI Layout
    // 遍历列和行来布局图标
    for (auto it = m_items.begin(); it != m_items.end(); it++) {
        ListItem* item = *it;
        //qDebug() << item->baseName();
        item->setItemSize(iconWidth, iconHeight);

        item->initData();
        item->updateLocationInfo(QPoint(Icon_X, Icon_Y));
        item->move(Icon_X, Icon_Y);
        item->show();
        // 更新 Y 坐标，增加图标高度和列间距
        Icon_Y = Icon_Y + iconHeight + rowSpacing;

        // 如果超出窗口高度，切换到下一列，Y 坐标重置为顶部
        if (Icon_Y + iconHeight > height()) {
            // 切换到下一列
            Icon_X = Icon_X + iconWidth + colSpacing;
            Icon_Y = marginTop;  // 重置 Y 坐标为顶部
        }
    }

}
ListView::SortOrderType ListView::getOppositeDirection() {

    switch (m_sortType) {
    case SortOrderType::SortByName_ASC:
        return SortOrderType::SortByName_DESC;
    case SortOrderType::SortByName_DESC:
        return SortOrderType::SortByName_ASC;
    case SortOrderType::SortBySize_ASC:
        return SortOrderType::SortBySize_DESC;
    case SortOrderType::SortBySize_DESC:
        return SortOrderType::SortBySize_ASC;
    case SortOrderType::SortByType_ASC:
        return SortOrderType::SortByType_DESC;
    case SortOrderType::SortByType_DESC:
        return SortOrderType::SortByType_ASC;
    case SortOrderType::SortByDateModified_ASC:
        return SortOrderType::SortByDateModified_DESC;
    case SortOrderType::SortByDateModified_DESC:
        return SortOrderType::SortByDateModified_ASC;
    default:
        return SortOrderType::SortByName_ASC;
    }
}
void ListView::sortItems(SortOrderType order)
{
    if (m_sortType == order) {
        order = getOppositeDirection();
    }
    switch (order) {
    case SortOrderType::SortByName_ASC:
        sortByName(true);
        break;
    case SortOrderType::SortByName_DESC:
        sortByName(false);
        break;
    case SortOrderType::SortBySize_ASC:
        break;
    case SortOrderType::SortBySize_DESC:
        break;
    case SortOrderType::SortByType_ASC:
        break;
    case SortOrderType::SortByType_DESC:
        break;
    case SortOrderType::SortByDateModified_ASC:
        break;
    case SortOrderType::SortByDateModified_DESC:
        break;
    default:
        sortByName(true);
    }
    m_sortType = order;
}

void ListView::sortByName(bool ascending)
{
    QCollator collator;
    collator.setNumericMode(true);          // 启用数字排序
    collator.setCaseSensitivity(Qt::CaseInsensitive); // 不区分大小写
    collator.setIgnorePunctuation(true);    // 忽略标点符号
    // 对于中文环境，设置本地化
    collator.setLocale(QLocale::system());

    std::sort(m_items.begin(), m_items.end(), [&collator, ascending](ListItem* a, ListItem* b) {
        // 文件夹优先
        /*if (a->isDirectory() != b->isDirectory()) {
            return a->isDirectory() > b->isDirectory();
        }*/
        int result = collator.compare(a->getSortName(), b->getSortName());
        return ascending ? (result < 0) : (result > 0);
        });
}

void ListView::addItem(ListItem* item)
{
    m_items.push_back(item);
    m_allItems.push_back(item);
}

void ListView::removeItem(ListItem* item)
{
    if (item) {
        item->hide();
        item->deleteLater();
        item = nullptr;
    }
    
}

void ListView::setSelectedItem(ListItem* item)
{
}

ListItem* ListView::selectedItem() const
{
    return nullptr;
}

/// <summary>
/// 生成规则
/// 1.unGrid模式
/// UI创建Item,直接提供鼠标右键坐标
/// 非UI创建，使用Grid创建规则
/// 2.Grid模式
/// 查询格子布局每个区域是否有文件存在，如果不存在检索空格子，使用第一个格子坐标
/// </summary>
/// <returns></returns>
QPoint ListView::generateItemPosition()
{

    return QPoint();
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
    return m_selectedItems;
}

bool ListView::isMultiSelection() const
{
    return false;
}

