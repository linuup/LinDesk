#include "ListView.h"
#include <QPushButton>
#include <QtMath>
#include <QProcess>
#include <QProcess>
#include <QStandardPaths>
#include "SystemApi/SystemApi.h"
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

    //m_deskMonitor = new DirectoryMonitor(reinterpret_cast<HWND>(winId()),false, this);
    //m_deskMonitor->startListening();
    
    // 获取真实的桌面路径
    QString desktopPath;
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &path))) {
        desktopPath = QString::fromWCharArray(path);
        CoTaskMemFree(path);
    } else {
        // 降级方案：使用QStandardPaths
        desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    }
    
    qDebug() << "监控桌面路径:" << desktopPath;
    
    // 注册自定义类型，允许跨线程队列信号传递
    qRegisterMetaType<FileSystemEventInfo>("FileSystemEventInfo");
    
    // 使用Lin目录监控器
    m_linMonitor = new LinDirectoryMonitor(desktopPath, false, this);
    connect(m_linMonitor, &LinDirectoryMonitor::fileSystemChanged, this, &ListView::onFileSystemChanged);
    m_linMonitor->startListening();
    
    //m_monitor->setDebounceInterval(800);
    //m_monitor->addWatchPath("D:/Desktop/", false);
    //m_monitor->start();

    rightMenu = RightMenu::getInstance();
    rightMenu->initializeCustomMenu();
    rightMenu->setHWND((HWND)this->winId());

    this->initialize();

    this->iconPosMode = IconPosMode::AlignIconToGrid;
    this->m_lastRightClickPos = QPoint(-1, -1);

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

    QPushButton* openDesktopIconSettings = new QPushButton(this);
    openDesktopIconSettings->setText("Desktop Icon Settings");
    openDesktopIconSettings->move(600, 300);

    // 连接按钮的点击信号到槽函数
    connect(openDesktopIconSettings, &QPushButton::clicked, this, []() {
        QProcess::startDetached("rundll32.exe", QStringList() << "shell32.dll,Control_RunDLL" << "desk.cpl,,5");
        });


}

void ListView::updateSystemIconVisibility(const QString& clsid, bool hidden)
{
    qDebug() << "updateSystemIconVisibility:" << clsid << "->" << (hidden ? "hiding" : "showing");
    
    // 规范化CLSID格式（移除可能存在的::前缀）
    QString normalizedClsid = clsid;
    normalizedClsid.replace("::", "");
    
    if (hidden) {
        // 需要隐藏图标：从显示列表m_items中移除
        for (auto it = m_items.begin(); it != m_items.end(); ) {
            ListItem* item = *it;
            if (item) {
                QString itemClsid = item->getItemData().getCLSID();
                if (itemClsid.replace("::", "") == normalizedClsid) {
                    qDebug() << "Hiding item:" << item->baseName();
                    item->hide(); // 隐藏控件
                    it = m_items.erase(it); // 从显示列表中移除
                    // 注意：不从m_allItems中移除，因为它是总列表
                    continue; // 继续下一个循环或者直接返回，这里用continue
                }
            }
            ++it;
        }
    }
    else {
        // 需要显示图标：检查是否已在显示列表中
        bool alreadyVisible = false;
        for (const auto& item : m_items) {
             if (item) {
                QString itemClsid = item->getItemData().getCLSID();
                if (itemClsid.replace("::", "") == normalizedClsid) {
                    alreadyVisible = true;
                    break;
                }
             }
        }
        
        if (!alreadyVisible) {
            // 从隐藏列表或总列表中查找并添加到显示列表
            // 首先检查m_hiddenIcons（专门存放系统图标的备用列表）
            bool found = false;
            for (auto* item : m_hiddenIcons) {
                if (item) {
                     QString itemClsid = item->getItemData().getCLSID();
                     if (itemClsid.replace("::", "") == normalizedClsid) {
                        qDebug() << "Restoring item from hidden icons:" << item->baseName();
                        m_items.push_back(item);
                        item->show();
                        found = true;
                        break;
                     }
                }
            }
            
            // 如果在hiddenIcons没找到，尝试在allItems中找（作为兜底）
            if (!found) {
                for (auto* item : m_allItems) {
                    if (item) {
                         QString itemClsid = item->getItemData().getCLSID();
                         if (itemClsid.replace("::", "") == normalizedClsid) {
                            // 确保不重复添加
                            if (!m_items.contains(item)) {
                                qDebug() << "Restoring item from all items:" << item->baseName();
                                m_items.push_back(item);
                                item->show();
                                found = true;
                            }
                            break;
                         }
                    }
                }
            }
        }
    }
    
    // 重新通过排序强制刷新顺序（系统图标通常有固定顺序，这里简单按当前规则重排）
    // 如果需要保持特定顺序，可能需要更复杂的插入逻辑
    //sortItems(m_sortType);
    
    // 重新布局和绘制
    //loadIconLayer();
    update();
    repaint();
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
        this, &ListView::updateSystemIconVisibility);
}
ListView::~ListView() {
    // 清理 ListItem 对象
    //qDeleteAll(m_listItems);
}
ListItem* ListView::selectItemByPath(const QString path)
{
    for (auto it = m_allItems.begin(); it != m_allItems.end(); it++) {
        ListItem* item = *it;
        if (item->isReady) {
            if (path == item->filePath()) {
                return item;
            }
        }
    }
    return nullptr;
}
void ListView::clearCheckedItem()
{
    auto& itemList = this->items();

    // 使用迭代器遍历，允许在遍历时删除
    for (auto it = itemList.begin(); it != itemList.end(); ) {
        if (*it == nullptr) {
            // 删除空指针，erase返回下一个有效迭代器
            it = itemList.erase(it);
            ++it;
            continue;
        }else if (*it) {
            (*it)->unCheckSelf();
            ++it;
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
        this->m_lastRightClickPos = event->pos();
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
        /*qDebug() << "ListView mouseReleaseEvent  Pos:" << event->pos();
        qDebug() << "Selected Item" << m_selectedItems.size();*/
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
    // 初始位置
    int Icon_X = m_marginLeft;   // 第一列的 X 坐标
    int Icon_Y = m_marginTop;   // 第一行的 Y 坐标

    //load UI Layout
    // 遍历列和行来布局图标
    for (auto it = m_items.begin(); it != m_items.end(); it++) {
        ListItem* item = *it;
        //qDebug() << item->baseName();
        item->setItemSize(m_iconWidth, m_iconHeight);

        item->initData();
        
        // Only move if position changed
        if (item->pos() != QPoint(Icon_X, Icon_Y)) {
             item->updateLocationInfo(QPoint(Icon_X, Icon_Y));
             item->move(Icon_X, Icon_Y);
        }
        
        if (!item->isVisible()) {
            item->show();
        }
        // 更新 Y 坐标，增加图标高度和列间距
        Icon_Y = Icon_Y + m_iconHeight + m_rowSpacing;

        // 如果超出窗口高度，切换到下一列，Y 坐标重置为顶部
        if (Icon_Y + m_iconHeight > height()) {
            // 切换到下一列
            Icon_X = Icon_X + m_iconWidth + m_colSpacing;
            Icon_Y = m_marginTop;  // 重置 Y 坐标为顶部
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
    if (!item) return;
    
    QPoint pos = generateItemPosition();
    item->setItemSize(m_iconWidth, m_iconHeight);
    item->updateLocationInfo(pos);
    
    m_items.push_back(item);
    m_allItems.push_back(item);
    
    item->show();
}

void ListView::removeItem(ListItem* item)
{
    if (item && item->isReady) {
        qDebug() << item->filePath();
        // 从所有相关数组中移除item
        m_items.removeOne(item);
        m_allItems.removeOne(item);
        m_selectedItems.removeOne(item);
        
        // 确保item被完全隐藏
        item->setVisible(false);
        item->hide();
        
        // 设置父控件为nullptr，确保item不再受父控件管理
        item->setParent(nullptr);
        
        item->isReady = false;
        item->deleteLater();
    }
    
    // 强制更新界面
    update();
    repaint();
    
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
    // 如果是由UI操作触发的（右键菜单新建），直接使用鼠标右键位置
    if (m_lastRightClickPos != QPoint(-1, -1)) {
        QPoint pos = m_lastRightClickPos;
        // 重置 m_lastRightClickPos，防止非UI创建的文件也使用该坐标
        m_lastRightClickPos = QPoint(-1, -1);
        return pos;
    }

    // Grid模式或非UI触发的UnGrid模式：寻找第一个可用的网格位置
    int x = m_marginLeft;
    int y = m_marginTop;

    while (true) {
        bool occupied = false;
        QRect currentRect(x, y, m_iconWidth, m_iconHeight);

        for (auto* item : m_items) {
            if (item && item->isVisible()) {
                // 检查网格区域是否被占用
                // 这里使用简单的包含中心点的检查，或者矩形相交
                if (currentRect.intersects(item->getItemRect())) {
                    occupied = true;
                    break;
                }
            }
        }

        if (!occupied) {
            return QPoint(x, y);
        }

        // 移动到下一个网格
        y += m_iconHeight + m_rowSpacing;
        if (y + m_iconHeight > height()) {
            y = m_marginTop;
            x += m_iconWidth + m_colSpacing;
        }

        // 安全检查，防止无限循环（超出屏幕宽度）
        if (x > width()) {
            break;
        }
    }

    return QPoint(0, 0); // 兜底返回
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

bool ListView::nativeEvent(const QByteArray & eventType, void* message, long* result) {
    MSG* msg = static_cast<MSG*>(message);

    // 处理自定义的Shell通知消息
    if (msg->message == WM_USER + 52) {
        qDebug() << "Received received WM_SHELLNOTIFY (WM_USER + 52)";
        handleShellNotification(msg->wParam, msg->lParam);
        if (result) *result = 0;
        return true;
    }

    return QWidget::nativeEvent(eventType, message, result);
}

void ListView::handleShellNotification(WPARAM wParam, LPARAM lParam) {
    LPITEMIDLIST* ppidl = nullptr;
    LONG lEvent = 0;

    HANDLE hNotifyLock = SHChangeNotification_Lock(
        reinterpret_cast<HANDLE>(wParam),
        static_cast<DWORD>(lParam),
        &ppidl,
        &lEvent
    );

    if (hNotifyLock) {
        //qDebug() << "SHChangeNotification_Lock success. Event ID:" << lEvent;
        processShellEvent(lEvent, ppidl);
        SHChangeNotification_Unlock(hNotifyLock);
    } else {
        qDebug() << "SHChangeNotification_Lock failed.";
    }
}

void ListView::processShellEvent(LONG lEvent, LPITEMIDLIST * ppidl) {
    FileSystemEvent eventDescription;
    QString filePath;
    ListItem* item;
    wchar_t path[MAX_PATH] = { 0 };
    // Extract file path from PIDL
    if (ppidl && ppidl[0]) {
        if (SHGetPathFromIDListW(ppidl[0], path)) {
            filePath = QString::fromWCharArray(path);
            //qDebug() << "Event Path:" << filePath;
        } else {
            qDebug() << "SHGetPathFromIDListW failed for event:" << lEvent;
        }
    } else {
        qDebug() << "PIDL is null for event:" << lEvent;
    }
    
    //qDebug() << "Processing Event:" << lEvent << "Path:" << filePath;

    // Map event codes to descriptions
    switch (lEvent) {
    case SHCNE_RENAMEITEM:
        eventDescription = FileSystemEvent::FileRenamed;
        {
             wchar_t newPath[MAX_PATH] = { 0 };
             QString newFilePath;
             if (ppidl && ppidl[1] && SHGetPathFromIDListW(ppidl[1], newPath)) {
                 newFilePath = QString::fromWCharArray(newPath);
                 qDebug() << "Rename Event OldPath:" << filePath << " NewPath:" << newFilePath;
             }
             
             item = selectItemByPath(filePath);
             if (item) {
                 // Check if new path is still on desktop
                 // We can simply check if the parent directory is the same
                 QFileInfo fileInfo(newFilePath);
                 QFileInfo oldFileInfo(filePath);
                 
                 // If the new file path is valid and sits in the same directory (or we monitor recursive?)
                 // DirectoryMonitor is recursive=false by default in ListView.cpp: new DirectoryMonitor(..., false, ...)
                 // So we assume if it stays in the monitored directory, it's a rename.
                 // If it moved out (e.g. to Recycle Bin), it might have a different path.
                 // Actually, Recycle Bin path is complex.
                 // A simple check: does the new file exist? 
                 // And is it in the desktop folder?
                 // Since we don't have the desktop path handy in a variable, we can compare parent paths.
                 // If moved to Recycle Bin, the path usually changes to something like C:\$Recycle.Bin\...
                 
                 if (!newFilePath.isEmpty() && fileInfo.absolutePath() == oldFileInfo.absolutePath()) {
                     // Rename within desktop
                     qDebug() << "Item renamed on desktop.";
                     // Update item data
                     // Create new data
                     ListItemData* data = new ListItemData(CSystemApi::CreateIconItem(newFilePath.replace("/", "\\").toStdWString().c_str()));
                     item->setItemData(*data);
                     item->initData(); // Re-init with new data
                     item->update();
                     // Maybe need to resort?
                     // sortItems(m_sortType); // This might be heavy? But valid.
                 } else {
                     // Moved away (deleted)
                     qDebug() << "Item moved away (deleted):" << filePath;
                     this->removeItem(item);
                 }
             }
        }
        break;
    case SHCNE_CREATE:
        eventDescription = FileSystemEvent::FileCreated;
        item = new ListItem(filePath, this);
        this->addItem(item);
        break;
    case SHCNE_DELETE:
        eventDescription = FileSystemEvent::FileDeleted;
        item = selectItemByPath(filePath);
        if (item) {
            this->removeItem(item);
        }
        break;
    case SHCNE_MKDIR:
        eventDescription = FileSystemEvent::DirectoryCreated;
        item = new ListItem(filePath, this);
        this->addItem(item);
        break;
    case SHCNE_RMDIR:
        eventDescription = FileSystemEvent::DirectoryDeleted;
        item = selectItemByPath(filePath);
        if (item) {
            this->removeItem(item);
        }
        break;
    case SHCNE_RENAMEFOLDER:
        eventDescription = FileSystemEvent::DirectoryRenamed;
        // Handle directory rename similar to file rename
         {
             wchar_t newPath[MAX_PATH] = { 0 };
             QString newFilePath;
             if (ppidl && ppidl[1] && SHGetPathFromIDListW(ppidl[1], newPath)) {
                 newFilePath = QString::fromWCharArray(newPath);
             }
             item = selectItemByPath(filePath);
             if (item) {
                 QFileInfo fileInfo(newFilePath);
                 QFileInfo oldFileInfo(filePath);
                 if (!newFilePath.isEmpty() && fileInfo.absolutePath() == oldFileInfo.absolutePath()) {
                     ListItemData* data = new ListItemData(CSystemApi::CreateIconItem(newFilePath.replace("/", "\\").toStdWString().c_str()));
                     item->setItemData(*data);
                     item->initData();
                     item->update();
                 } else {
                     this->removeItem(item);
                 }
             }
         }
        break;
    case SHCNE_UPDATEITEM:
        eventDescription = FileSystemEvent::FileUpdated;
        item = selectItemByPath(filePath);
        if (item) {
             // Reload data
             ListItemData* data = new ListItemData(CSystemApi::CreateIconItem(filePath.replace("/", "\\").toStdWString().c_str()));
             item->setItemData(*data);
             item->initData();
             item->update();
        }
        break;
    case SHCNE_UPDATEDIR:
        eventDescription = FileSystemEvent::DirectoryUpdated;
        // Refresh list because directory content changed
        refreshView();
        break;
    case SHCNE_ATTRIBUTES:
        eventDescription = FileSystemEvent::FileAttributesChanged;
        break;
    case SHCNE_MEDIAREMOVED:
        eventDescription = FileSystemEvent::MediaRemoved;
        break;
    case SHCNE_DRIVEADD:
        eventDescription = FileSystemEvent::DriveAdded;
        break;
    case SHCNE_DRIVEREMOVED:
        eventDescription = FileSystemEvent::DriveRemoved;
        break;
    case SHCNE_ASSOCCHANGED:
        eventDescription = FileSystemEvent::FileAssociationChanged;
        break;
    case SHCNE_SERVERDISCONNECT:
        eventDescription = FileSystemEvent::ServerDisconnected;
        break;
    default:
        eventDescription = FileSystemEvent(0);
    }

    // Log the event
    QString timestamp = QDateTime::currentDateTime().toString("[hh:mm:ss.zzz]");
    QString logMessage = QString("%1 %2").arg(timestamp).arg(m_deskMonitor->eventToString(eventDescription));

    if (!filePath.isEmpty()) {
        logMessage += QString(": %1").arg(filePath);
    }

    qDebug() << "[Desktop Folder Monitor]" << logMessage << QDateTime::currentDateTime();
}

void ListView::refreshView() {
    qDebug() << "Refreshing View...";
    // Clear current items?
    // initialize() reloads from scratch.
    // Need to clear existing items first to avoid duplicates or leaks if initialize doesn't clear.
    
    // Check initialize implementation.
    // It creates new ListItems and adds them.
    // So we must clear old items first.
    
    // removeItem deletes item.
    // We can iterate and remove all.
    // Or just clear m_items and m_allItems and delete widgets.
    
    // Better: use qDeleteAll(m_allItems); m_allItems.clear(); m_items.clear();
    // But m_items is subset.
    // m_allItems owns the pointers (created in initialize).
    
    // Let's look at ListView::~ListView()
    // //qDeleteAll(m_listItems); is commented out.
    // But we should clean up.
    
    for(auto item : m_allItems) {
        if(item) {
            item->deleteLater();
        }
    }
    m_allItems.clear();
    m_items.clear();
    m_selectedItems.clear();
    m_hiddenIcons.clear(); // Clear hidden icons too
    
    this->initialize();
    this->loadIconLayer();
}

// 文件系统变化事件处理
void ListView::onFileSystemChanged(const FileSystemEventInfo& eventInfo)
{
    // 规范化路径（统一用反斜杠，去掉末尾斜杠）
    auto normalizePath = [](const QString& p) -> QString {
        QString s = QDir::toNativeSeparators(p);
        while (s.endsWith('\\') || s.endsWith('/'))
            s.chop(1);
        return s;
    };

    const QString newPath = normalizePath(eventInfo.newPath);
    const QString oldPath = normalizePath(eventInfo.oldPath);

    qDebug() << "[onFileSystemChanged]"
             << "type:" << (int)eventInfo.eventType
             << "isDir:" << eventInfo.isDirectory
             << "old:" << oldPath
             << "new:" << newPath;

    // 重新布局并刷新（不调用 sortItems 避免方向反转 bug）
    auto relayout = [this]() {
        loadIconLayer();
        update();
        repaint();
    };

    // 按当前排序规则重排（直接调用 sortByName 避免 sortItems 的方向反转 bug）
    auto resort = [this]() {
        bool ascending = (m_sortType == SortOrderType::SortByName_ASC ||
                          m_sortType == SortOrderType::SortBySize_ASC ||
                          m_sortType == SortOrderType::SortByType_ASC ||
                          m_sortType == SortOrderType::SortByDateModified_ASC);
        sortByName(ascending);
    };

    switch (eventInfo.eventType) {

    // ── 创建（文件 / 文件夹） ──────────────────────────────────────────
    case FileSystemEvent::FileCreated:
    case FileSystemEvent::DirectoryCreated:
    {
        if (newPath.isEmpty()) {
            qDebug() << "[onFileSystemChanged] FileCreated: empty path, skip";
            break;
        }
        // 防止重复添加
        if (selectItemByPath(newPath)) {
            qDebug() << "[onFileSystemChanged] already exists, skip:" << newPath;
            break;
        }
        qDebug() << "[onFileSystemChanged] Creating item:" << newPath;
        ListItem* item = new ListItem(newPath, this);
        addItem(item);
        break;
    }

    // ── 删除（文件 / 文件夹） ──────────────────────────────────────────
    case FileSystemEvent::FileDeleted:
    case FileSystemEvent::DirectoryDeleted:
    {
        // 删除事件：oldPath 优先，其次 newPath
        const QString& deletePath = !oldPath.isEmpty() ? oldPath : newPath;
        if (deletePath.isEmpty()) {
            qDebug() << "[onFileSystemChanged] FileDeleted: empty path, skip";
            break;
        }
        ListItem* item = selectItemByPath(deletePath);
        if (item) {
            qDebug() << "[onFileSystemChanged] Removing item:" << deletePath;
            removeItem(item);   // removeItem 内部已调用 update()/repaint()
        } else {
            qDebug() << "[onFileSystemChanged] Item not found for deletion:" << deletePath;
        }
        break;
    }

    // ── 重命名（文件 / 文件夹） ───────────────────────────────────────
    case FileSystemEvent::FileRenamed:
    case FileSystemEvent::DirectoryRenamed:
    {
        if (oldPath.isEmpty() || newPath.isEmpty()) {
            qDebug() << "[onFileSystemChanged] Rename: empty path, skip";
            break;
        }

        ListItem* item = selectItemByPath(oldPath);

        if (item) {
            // 判断是否仍在同一父目录（同目录重命名）
            const QString oldDir = QFileInfo(oldPath).absolutePath();
            const QString newDir = QFileInfo(newPath).absolutePath();

            if (oldDir == newDir) {
                // ── 同目录重命名：更新 item 数据 ──
                qDebug() << "[onFileSystemChanged] Rename in-place:" << oldPath << "->" << newPath;
                ListItemData* data = new ListItemData(
                    CSystemApi::CreateIconItem(newPath.toStdWString().c_str())
                );
                item->setItemData(*data);
                delete data;
                item->initData();
                item->update();
                resort();
                relayout();
            } else {
                // ── 移出桌面目录：视为删除 ──
                qDebug() << "[onFileSystemChanged] Item moved out, removing:" << oldPath;
                removeItem(item);
            }
        } else {
            // 找不到旧项 → 可能是从外部移入桌面，视为新建
            if (!selectItemByPath(newPath)) {
                qDebug() << "[onFileSystemChanged] Item moved in, creating:" << newPath;
                ListItem* newItem = new ListItem(newPath, this);
                addItem(newItem);
            }
        }
        break;
    }

    // ── 内容修改（文件） ──────────────────────────────────────────────
    case FileSystemEvent::FileUpdated:
    case FileSystemEvent::FileAttributesChanged:
    {
        const QString& updatePath = !newPath.isEmpty() ? newPath : oldPath;
        if (updatePath.isEmpty()) break;

        ListItem* item = selectItemByPath(updatePath);
        if (item) {
            qDebug() << "[onFileSystemChanged] Updating item:" << updatePath;
            ListItemData* data = new ListItemData(
                CSystemApi::CreateIconItem(updatePath.toStdWString().c_str())
            );
            item->setItemData(*data);
            delete data;
            // 只刷新该 item 自身，不重新布局其他图标
            item->initData();
            item->update();
            // 注意：不调用 relayout()，避免其他图标位置改变
        }
        break;
    }

    // ── 文件夹内容变化（子项增删） ────────────────────────────────────
    case FileSystemEvent::DirectoryUpdated:
    {
        // 桌面文件夹本身内容变化时，不需要额外处理
        // 因为子项的增删会触发独立的 FileCreated/FileDeleted 事件
        qDebug() << "[onFileSystemChanged] DirectoryUpdated (no-op):" << newPath;
        break;
    }

    default:
        qDebug() << "[onFileSystemChanged] Unhandled event type:" << (int)eventInfo.eventType;
        break;
    }
}
