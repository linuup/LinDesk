#include "ListView.h"
#include <QPushButton>
#include <QtMath>
#include <QProcess>
#include <QProcess>
#include <QStandardPaths>
#include <QGraphicsOpacityEffect>
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
    this->initRightMenuConnections();

    m_iconShowRule.autoArrange = true;
    m_iconShowRule.gridAlignment = true;
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
    loadIconLayer();
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
    for (auto* item : m_selectedItems) {
        if (item) {
            item->unCheckSelf();
        }
    }
    m_selectedItems.clear();
}

void ListView::requestSystemRightMenu(ObjectType objType, QPoint point, const QVector<ListItem*> checkedItems)
{
    RightMenu* r = RightMenu::getInstance();
    if (checkedItems.size() == 1) {
        r->setShowType(objType);
        r->showMenu(point, checkedItems.at(0));
        return;
    }
    else if (objType == ObjectType::BackGround) {
        r->setShowType(ObjectType::BackGround);
        r->showMenu(point, nullptr);
        return;
    }
    else {
        r->setShowType(ObjectType::MultObject);
        r->showMultObjectMenu(point, checkedItems.toStdVector());
    }
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
        // 如果右键点击位置不在任何图标区域内，清除已有选中
        clearCheckedItem();
        // 显示桌面的右键菜单
        requestSystemRightMenu(ObjectType::BackGround, rightClickPoint, m_selectedItems);
    }
}

void ListView::mousePressEvent(QMouseEvent* event)
{
    auto clickPos = event->pos();
    auto items = this->items();
    ListItem* clickedItem = nullptr;

    // 查找击中的 item
    for (auto* item : items) {
            if (item && item->getItemRect().contains(mapToGlobal(clickPos))) {
                clickedItem = item;
                break;
            }
        }

    if (clickedItem && clickedItem->type() != ObjectType::Empty_ObjectType) {
            // 如果点击的项不在当前选中列表中，则清除之前选中并仅选中当前
            if (!m_selectedItems.contains(clickedItem)) {
                clearCheckedItem();
                m_selectedItems.append(clickedItem);
                clickedItem->checkSelf();
            }

            m_draggedItem = clickedItem;
            m_dragOffset = clickPos - clickedItem->pos();
            m_isDragging = true;

            // 清理旧的 ghost
            for (auto& ghost : m_ghosts) {
                if (ghost.widget) ghost.widget->deleteLater();
            }
            m_ghosts.clear();

            // 为所有选中的项创建 ghost 预览
            for (auto* item : m_selectedItems) {
                if (!item) continue;

                QLabel* ghostWidget = new QLabel(this);
                ghostWidget->setAttribute(Qt::WA_TransparentForMouseEvents);
                ghostWidget->setAttribute(Qt::WA_TranslucentBackground);
                ghostWidget->setWindowFlags(Qt::FramelessWindowHint);

                auto* opacityEffect = new QGraphicsOpacityEffect(ghostWidget);
                opacityEffect->setOpacity(0.7);
                ghostWidget->setGraphicsEffect(opacityEffect);

                // 抓取快照（临时取消选中以获得干净图标）
                bool wasSelected = item->isSelected;
                item->isSelected = false;
                item->update();
                QPixmap pixmap = item->grab();
                item->isSelected = wasSelected;
                item->update();

                ghostWidget->setPixmap(pixmap);
                ghostWidget->setFixedSize(item->size());
                
                // 记录相对于鼠标点击位置的偏移量
                QPoint offset = item->pos() - clickPos;
                ghostWidget->move(item->pos());
                ghostWidget->raise();
                ghostWidget->show();

                m_ghosts.append({ ghostWidget, item, offset });
            }

            return;
    }
    
    // 如果没有点击到任何 item，取消已选中的 item 选中效果
    clearCheckedItem();

    m_isSelecting = true;
    event->ignore();
}

void ListView::mouseMoveEvent(QMouseEvent* event)
{
    // handle multi-dragging
    if (m_isDragging && !m_ghosts.isEmpty()) {
        QPoint mousePos = event->pos();
        for (auto& ghost : m_ghosts) {
            if (ghost.widget) {
                ghost.widget->move(mousePos + ghost.offset);
            }
        }
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
        // 完成多项拖拽
        for (auto& ghost : m_ghosts) {
            if (ghost.widget && ghost.item) {
                QPoint finalPos = ghost.widget->pos();
                
                // 如果开启了网格对齐，则计算最近的网格点
                if (m_iconShowRule.gridAlignment) {
                    int gridX = qRound((double)(finalPos.x() - m_marginLeft) / (m_iconWidth + m_colSpacing));
                    int gridY = qRound((double)(finalPos.y() - m_marginTop) / (m_iconHeight + m_rowSpacing));
                    
                    finalPos.setX(m_marginLeft + gridX * (m_iconWidth + m_colSpacing));
                    finalPos.setY(m_marginTop + gridY * (m_iconHeight + m_rowSpacing));
                    
                    // 边界检查，防止图标超出屏幕
                    finalPos.setX(qMax(m_marginLeft, qMin(finalPos.x(), width() - m_iconWidth)));
                    finalPos.setY(qMax(m_marginTop, qMin(finalPos.y(), height() - m_iconHeight)));
                }

                ghost.item->move(finalPos);
                ghost.item->updateLocationInfo(finalPos);
                ghost.widget->hide();
                ghost.widget->deleteLater();
            }
        }
        m_ghosts.clear();
        
        m_isDragging = false;  // 结束拖动
        m_draggedItem = nullptr; // 清除被拖动的 ListItem
        m_startPos = QPoint();
        return;
    }
    event->ignore();

}

void ListView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        ListItem* hitItem = nullptr;
        auto items = this->items();
        for (auto* item : items) {
            if (item && item->isVisible() && item->getItemRect().contains(mapToGlobal(event->pos()))) {
                hitItem = item;
                break;
            }
        }
        
        if (hitItem) {
            qDebug() << "Double-clicking to open:" << hitItem->filePath();
            hitItem->openItem();

            
        } else {
            // 双击空白处：现有逻辑是切换桌面图标显示
            onMenuShowDesktopIcons();
        }
    }
    QWidget::mouseDoubleClickEvent(event);
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
    if (m_iconShowRule.autoArrange) {
        loadAutoLayout();
    } else if (m_iconShowRule.gridAlignment) {
        loadGridLayout();
    }
    
    
    // Ensure visibility
    for (auto* item : m_items) {
        if (!item->isVisible() && m_iconShowRule.showDesktopIcons) {
            item->show();
        }
    }
}

void ListView::loadAutoLayout()
{
    int Icon_X = m_marginLeft;
    int Icon_Y = m_marginTop;
    
    for (auto it = m_items.begin(); it != m_items.end(); it++) {
        ListItem* item = *it;
        item->setItemSize(m_iconWidth, m_iconHeight);
        item->initData();
        
        if (item->pos() != QPoint(Icon_X, Icon_Y)) {
             item->updateLocationInfo(QPoint(Icon_X, Icon_Y));
             item->move(Icon_X, Icon_Y);
        }
        
        Icon_Y = Icon_Y + m_iconHeight + m_rowSpacing;
        if (Icon_Y + m_iconHeight > height()) {
            Icon_X = Icon_X + m_iconWidth + m_colSpacing;
            Icon_Y = m_marginTop;
        }
    }
    
}

void ListView::loadGridLayout()
{
    for (auto* item : m_items) {
        item->setItemSize(m_iconWidth, m_iconHeight);
        item->initData();

        QPoint p = item->pos();
        // 对齐到最近的网格点
        int gridX = qRound((double)(p.x() - m_marginLeft) / (m_iconWidth + m_colSpacing));
        int gridY = qRound((double)(p.y() - m_marginTop) / (m_iconHeight + m_rowSpacing));
        
        int snappedX = m_marginLeft + gridX * (m_iconWidth + m_colSpacing);
        int snappedY = m_marginTop + gridY * (m_iconHeight + m_rowSpacing);
        
        // 边界检查
        snappedX = qMax(m_marginLeft, qMin(snappedX, width() - m_iconWidth));
        snappedY = qMax(m_marginTop, qMin(snappedY, height() - m_iconHeight));

        if (item->pos() != QPoint(snappedX, snappedY)) {
            item->updateLocationInfo(QPoint(snappedX, snappedY));
            item->move(snappedX, snappedY);
        }
    }
}
ListView::SortOrderType ListView::getOppositeDirection() {

    switch (m_iconShowRule.sortType) {
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
    if (m_iconShowRule.sortType == order) {
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
    m_iconShowRule.sortType = order;
}

void ListView::sortByName(bool ascending)
{
    QCollator collator;
    collator.setNumericMode(true);          // 启用数字排序
    collator.setCaseSensitivity(Qt::CaseInsensitive); // 不区分大小写
    collator.setIgnorePunctuation(true);    // 忽略标点符号
    // 对于中文环境，设置本地化
    collator.setLocale(QLocale::system());

    std::sort(m_items.begin(), m_items.end(), [&](ListItem* a, ListItem* b) {
        auto getPriority = [](ListItem* item) {
            if (item->type() == ObjectType::SpecialFolder) {
                QString clsid = item->getItemData().getCLSID();
                if (clsid == APP_USER_FOLDER_CLS) return 1;
                if (clsid == APP_MY_COMPUTER_CLS) return 2;
                if (clsid == APP_NETWORK_CLS) return 3;
                if (clsid == APP_RECYCLE_BIN_CLS) return 4;
                if (clsid == APP_CONTROL_PANEL_CLS) return 5;
                return 6;
            }
            return 100;
        };

        int prioA = getPriority(a);
        int prioB = getPriority(b);

        if (prioA != prioB) {
            // 升序时，优先级小的排在前面（1, 2, 3, 100）
            // 降序时，优先级大的排在前面（100, 3, 2, 1）
            return ascending ? (prioA < prioB) : (prioA > prioB);
        }

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
    
    if (m_iconShowRule.showDesktopIcons) {
        item->show();
    } else {
        item->hide();
    }
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
    //MSG* msg = static_cast<MSG*>(message);

    //// 处理自定义的Shell通知消息
    //if (msg->message == WM_USER + 52) {
    //    qDebug() << "Received received WM_SHELLNOTIFY (WM_USER + 52)";
    //    handleShellNotification(msg->wParam, msg->lParam);
    //    if (result) *result = 0;
    //    return true;
    //}

    return QWidget::nativeEvent(eventType, message, result);
}


void ListView::refreshView() {
    qDebug() << "Refreshing View...";

    // 2. 清理旧项目
    for(auto item : m_allItems) {
        if(item) {
            item->deleteLater();
        }
    }
    m_allItems.clear();
    m_items.clear();
    m_selectedItems.clear();
    m_hiddenIcons.clear(); 
    
    // 3. 重新加载数据
    this->initialize();
    // 5. 执行布局分发
    this->loadIconLayer();
}

void ListView::initRightMenuConnections() {
    RightMenu* menu = RightMenu::getInstance();

    // View sub-menu connections
    connect(menu, &RightMenu::LargeIconsTriggered, this, &ListView::onMenuLargeIcons);
    connect(menu, &RightMenu::MediumIconsTriggered, this, &ListView::onMenuMediumIcons);
    connect(menu, &RightMenu::SmallIconsTriggered, this, &ListView::onMenuSmallIcons);
    connect(menu, &RightMenu::AutoArrangeIconsTriggered, this, &ListView::onMenuAutoArrangeIcons);
    connect(menu, &RightMenu::AlignIconsToGridTriggered, this, &ListView::onMenuAlignIconsToGrid);
    connect(menu, &RightMenu::ShowDesktopIconsTriggered, this, &ListView::onMenuShowDesktopIcons);

    // Sort sub-menu connections
    connect(menu, &RightMenu::sortByNameTriggered, this, &ListView::onMenuSortByName);
    connect(menu, &RightMenu::sortBySizeTriggered, this, &ListView::onMenuSortBySize);
    connect(menu, &RightMenu::sortByTypeTriggered, this, &ListView::onMenuSortByType);
    connect(menu, &RightMenu::sortByDateTriggered, this, &ListView::onMenuSortByDate);

    // Other actions
    connect(menu, &RightMenu::refreshTriggered, this, &ListView::onMenuRefresh);
    connect(menu, &RightMenu::pasteTriggered, this, &ListView::onMenuPaste);
    connect(menu, &RightMenu::pasteShortcutTriggered, this, &ListView::onMenuPasteShortcut);
    connect(menu, &RightMenu::undoDeleteTriggered, this, &ListView::onMenuUndoDelete);

    //init view sub-menu check states
    switch (m_iconShowRule.viewMode) {
    case ViewMode::LargeIcon:
        menu->setLargeIconsChecked(true);
        break;
    case ViewMode::MediumIcon:
        menu->setMediumIconsChecked(true);
        break;
    case ViewMode::SmallIcon:
        menu->setSmallIconsChecked(true);
        break;
    default:
        menu->setMediumIconsChecked(true);
    }
    
    if(m_iconShowRule.autoArrange) {
        menu->setAutoArrangeIconsChecked(true);
    } else {
        menu->setAutoArrangeIconsChecked(false);
    }
    if(m_iconShowRule.gridAlignment) {
        menu->setAlignIconsToGridChecked(true);
    } else {
        menu->setAlignIconsToGridChecked(false);
    }
    if(m_iconShowRule.showDesktopIcons) {
        menu->setShowDesktopIconsChecked(true);
    } else {
        menu->setShowDesktopIconsChecked(false);
    }
}

void ListView::onMenuLargeIcons() {
    qDebug() << "ListView: Large Icons triggered";
}

void ListView::onMenuMediumIcons() {
    qDebug() << "ListView: Medium Icons triggered";
}

void ListView::onMenuSmallIcons() {
    qDebug() << "ListView: Small Icons triggered";
}

void ListView::onMenuAutoArrangeIcons() {
    qDebug() << "ListView: Auto Arrange Icons triggered";
    m_iconShowRule.autoArrange = !m_iconShowRule.autoArrange;
    loadIconLayer();
    update();
}

void ListView::onMenuAlignIconsToGrid() {
    qDebug() << "ListView: Align Icons to Grid triggered";
    m_iconShowRule.gridAlignment = !m_iconShowRule.gridAlignment;
    loadIconLayer();
    update();
}

void ListView::onMenuShowDesktopIcons() {
    qDebug() << "ListView: Show Desktop Icons triggered";
    if(m_iconShowRule.showDesktopIcons) {
        m_iconShowRule.showDesktopIcons = false;
        this->rightMenu->setShowDesktopIconsChecked(false);
        for (auto item : m_items) {
            item->hide();
        }
    } else {
        m_iconShowRule.showDesktopIcons = true;
        this->rightMenu->setShowDesktopIconsChecked(true);
        for (auto item : m_items) {
            item->show();
        }
    }
}

void ListView::onMenuSortByName() {
    qDebug() << "ListView: Sort By Name triggered";
    this->sortItems(SortOrderType::SortByName_ASC);
    loadIconLayer();
    update();
}

void ListView::onMenuSortBySize() {
    qDebug() << "ListView: Sort By Size triggered";
    this->sortItems(SortOrderType::SortBySize_ASC);
    loadIconLayer();
    update();
}

void ListView::onMenuSortByType() {
    qDebug() << "ListView: Sort By Type triggered";
    this->sortItems(SortOrderType::SortByType_ASC);
    loadIconLayer();
    update();
}

void ListView::onMenuSortByDate() {
    qDebug() << "ListView: Sort By Date triggered";
    this->sortItems(SortOrderType::SortByDateModified_ASC);
    loadIconLayer();
    update();
}

void ListView::onMenuRefresh() {
    qDebug() << "ListView: Refresh triggered";
    refreshView(); // 使用已有的刷新函数
}

void ListView::onMenuPaste() {
    qDebug() << "ListView: Paste triggered";
}

void ListView::onMenuPasteShortcut() {
    qDebug() << "ListView: Paste Shortcut triggered";
}

void ListView::onMenuUndoDelete() {
    qDebug() << "ListView: Undo Delete triggered";
}
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

    // 重新布局并刷新
    auto relayout = [this]() {
        loadIconLayer();
        update();
        repaint();
    };

    // 按当前排序规则重排
    auto resort = [this]() {
        bool ascending = (
            m_iconShowRule.sortType == SortOrderType::SortByName_ASC ||
            m_iconShowRule.sortType == SortOrderType::SortBySize_ASC ||
            m_iconShowRule.sortType == SortOrderType::SortByType_ASC ||
            m_iconShowRule.sortType == SortOrderType::SortByDateModified_ASC
            );
        sortByName(ascending);
    };

    switch (eventInfo.eventType) {
    case FileSystemEvent::FileCreated:
    case FileSystemEvent::DirectoryCreated:
    {
        if (newPath.isEmpty()) break;
        if (selectItemByPath(newPath)) break;
        
        qDebug() << "[onFileSystemChanged] Creating item:" << newPath;
        ListItem* item = new ListItem(newPath, this);
        addItem(item);
        break;
    }
    case FileSystemEvent::FileDeleted:
    case FileSystemEvent::DirectoryDeleted:
    {
        const QString& deletePath = !oldPath.isEmpty() ? oldPath : newPath;
        if (deletePath.isEmpty()) break;
        
        ListItem* item = selectItemByPath(deletePath);
        if (item) {
            removeItem(item);
        }
        break;
    }
    case FileSystemEvent::FileRenamed:
    case FileSystemEvent::DirectoryRenamed:
    {
        if (oldPath.isEmpty() || newPath.isEmpty()) break;

        ListItem* item = selectItemByPath(oldPath);
        if (item) {
            const QString oldDir = QFileInfo(oldPath).absolutePath();
            const QString newDir = QFileInfo(newPath).absolutePath();

            if (oldDir == newDir) {
                ListItemData* data = new ListItemData(CSystemApi::CreateIconItem(newPath.toStdWString().c_str()));
                item->setItemData(*data);
                delete data;
                item->initData();
                item->update();
                resort();
                relayout();
            } else {
                removeItem(item);
            }
        } else if (!selectItemByPath(newPath)) {
            ListItem* newItem = new ListItem(newPath, this);
            addItem(newItem);
        }
        break;
    }
    case FileSystemEvent::FileUpdated:
    case FileSystemEvent::FileAttributesChanged:
    {
        const QString& updatePath = !newPath.isEmpty() ? newPath : oldPath;
        if (updatePath.isEmpty()) break;

        ListItem* item = selectItemByPath(updatePath);
        if (item) {
            ListItemData* data = new ListItemData(CSystemApi::CreateIconItem(updatePath.toStdWString().c_str()));
            item->setItemData(*data);
            delete data;
            item->initData();
            item->update();
        }
        break;
    }
    default:
        break;
    }
}
