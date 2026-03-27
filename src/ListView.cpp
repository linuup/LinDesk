#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDataStream>
#include <QFile>
#include <QSet>
#include <QCoreApplication>
#include "ListView.h"
#include "SettingUI.h"
#include <QPushButton>
#include <QtMath>
#include <QProcess>
#include <QStandardPaths>
#include <QGraphicsOpacityEffect>
#include "SystemApi/SystemApi.h"
#include <QDrag>
#include <QMimeData>
#include <QUrl>
#include <QKeyEvent>
#include "Fluent/FluentDialog.h"
#include "LinBox.h"
#include "Plugins/PluginManager.h"
#include "HotkeyManager.h"
// 静态成员初始化
ListView* ListView::m_instance = nullptr;
int ListItemData::s_targetIconSize = ListView::kMediumIconW;

ListView::ListView(QWidget* parent) : QWidget(parent) {
    setAcceptDrops(true);
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

    monitor = new DesktopInfoMonitor(this);
    startListeningSpecialIcon();

    // Initialize Plugin System & Hotkeys ASYNCHRONOUSLY to avoid recursion in constructor
    QTimer::singleShot(0, this, [this]() {
        PluginManager::instance()->loadPlugins();
        
        // Register a test hotkey for the Baidu OCR plugin
        // Alt + S = Sample OCR Trigger
        if (!PluginManager::instance()->allPlugins().isEmpty()) {
     // Register system-wide Hotkeys
    // Note: OCR and other plugins now register their own hotkeys in ocr_config.ini
    // and PluginManager handles the registration dynamically.
        }
    });

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
    m_desktopDir = desktopPath; // 保存到成员变量
    
    qDebug() << "监控桌面路径:" << desktopPath;
    
    // 注册自定义类型，允许跨线程队列信号传递
    qRegisterMetaType<FileSystemEventInfo>("FileSystemEventInfo");
    
    // 使用Lin目录监控器
    m_linMonitor = new LinDirectoryMonitor(desktopPath, false, this);
    connect(m_linMonitor, &LinDirectoryMonitor::fileSystemChanged, this, &ListView::onFileSystemChanged);
    m_linMonitor->startListening();

    rightMenu = RightMenu::getInstance();
    rightMenu->initializeCustomMenu();
    rightMenu->setHWND((HWND)this->winId());

    this->initialize();
    this->initRightMenuConnections();
    this->syncViewModeSizes(); // Call syncViewModeSizes after initialization

    setFocusPolicy(Qt::StrongFocus);
#ifdef QT_DEBUG
    m_persistencePath = QCoreApplication::applicationDirPath() + "/data.json";
#else
    m_persistencePath = QCoreApplication::applicationDirPath() + "/data.dat";
#endif
    
    // Defer loading boxes to ensure the event loop is running and windows can be properly shown/mapped
    QTimer::singleShot(100, [this](){
        this->loadBoxes(true);
        
        // Only show organize prompt AFTER boxes are loaded and persistence state is known from file
        if (!m_enablePersistence) {
            QTimer::singleShot(1000, [this]() {
                qDebug() << "[FluentDialog] Persistence is OFF after load, offering organization prompt...";
                FluentDialog* dlg = new FluentDialog(nullptr);
                dlg->setDialogTitle("桌面整理");
                dlg->setMessage("是否立即一键整理当前桌面的图标布局？\n（将开启自动布局并持久化存储）");
                dlg->addActionButton("立即整理", FluentDialog::ActionType::Primary, [this]() {
                    this->setPersistenceEnabled(true); 
                    this->onMenuRefresh();
                });
                dlg->addActionButton("取消", FluentDialog::ActionType::Close);
                dlg->show();
                dlg->raise();
                dlg->activateWindow();
            });
        }
        this->raise();
    });

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(150);
    connect(m_saveTimer, &QTimer::timeout, this, &ListView::saveBoxes);
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
    
    // Refresh our local snapshot of all system icon states before re-rendering
    if (monitor) {
        sysIconRegisterInfo = monitor->getCurrentIcons();
    }
    
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
    connect(monitor, &DesktopInfoMonitor::iconVisibilityChanged,
        this, &ListView::updateSystemIconVisibility);
}

bool ListView::nativeEvent(const QByteArray& eventType, void* message, long* result) {
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_HOTKEY) {
        handleHotkey(static_cast<int>(msg->wParam));
        return true; // We handled it
    }
    return QWidget::nativeEvent(eventType, message, result);
}

void ListView::handleHotkey(int id) {
    QString actionId = HotkeyManager::instance()->getActionId(id);
    if (!actionId.isEmpty()) {
        qDebug() << "[ListView] Dispatching Hotkey ID:" << id << "to Action:" << actionId;
        PluginManager::instance()->triggerPlugin(actionId);
    }
}
ListView::~ListView() {
    saveBoxes();
    // 清理 ListItem 对象
    //qDeleteAll(m_listItems);
}
ListItem* ListView::selectItemByPath(const QString path)
{
    if (path.isEmpty()) return nullptr;
    QString target = QDir::toNativeSeparators(path).toLower();
    while (target.endsWith('\\')) target.chop(1);

    for (auto* item : m_allItems) {
        if (item) {
            if (item->normalizedPath() == target) {
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

        // Since ListItems now intercept their own contextMenuEvent and emit signals,
        // this handler is only reached if the background (desktop) is clicked.
        
        // 如果右键点击位置不在任何图标区域内，清除已有选中
        clearCheckedItem();
        // 显示桌面的右键菜单
        requestSystemRightMenu(ObjectType::BackGround, rightClickPoint, m_selectedItems);
    }
}

void ListView::mousePressEvent(QMouseEvent* event)
{
    auto clickPos = event->pos();

    // Since ListItems now intercept their own mousePressEvent and emit signals,
    // this handler is only reached if the background (desktop) is clicked.
    
    // If no item was clicked, clear selected items
    clearCheckedItem();
    this->setFocus();

    m_isSelecting = true;
    m_selectionStart = clickPos;
    m_selectionRect = QRect(m_selectionStart, QSize(0, 0));
    
    update();
    event->ignore();
}

void ListView::mouseMoveEvent(QMouseEvent* event)
{
    // handle multi-dragging
    if (m_isDragging && !m_ghosts.isEmpty()) {
        QPoint mousePos = event->globalPos();
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
    // No-op for Item-based drags, as QDropEvent now handles them
    if (m_isDragging) {
        m_isDragging = false;
        m_draggedItem = nullptr;
    }
    
    // Clear selection state if we were selecting background
    if (m_isSelecting) {
        m_isSelecting = false;
        update();
    }
    event->ignore();
}

void ListView::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void ListView::dragLeaveEvent(QDragLeaveEvent* event)
{
    if (m_dragTip) m_dragTip->hide();
    event->accept();
}

void ListView::dragMoveEvent(QDragMoveEvent* event)
{
    if (!event->mimeData()->hasUrls()) {
        if (m_dragTip) m_dragTip->hide();
        return;
    }
    event->acceptProposedAction();

    // 1. Find if we are hovering over an item
    ListItem* hitItem = nullptr;
    for (auto* item : m_items) {
        if (item && item->isVisible() && item->geometry().contains(event->pos()) && !m_selectedItems.contains(item)) {
            hitItem = item;
            break;
        }
    }

    bool isCtrl = (event->keyboardModifiers() & Qt::ControlModifier);

    if (hitItem) {
        // --- 悬浮在图标上的逻辑 ---
        if (!m_dragTip) {
            m_dragTip = new QLabel(this);
            m_dragTip->setStyleSheet(
                "QLabel {"
                "  background-color: rgba(255, 255, 255, 235);" // Premium Light Acrylic
                "  color: #202020;"
                "  border: 1px solid rgba(0, 0, 0, 40);"
                "  border-radius: 8px;"
                "  padding: 10px 16px;"
                "  font-size: 13px;"
                "  font-family: 'Segoe UI Variable Display', 'Segoe UI', sans-serif;"
                "}"
            );
            m_dragTip->setAttribute(Qt::WA_TransparentForMouseEvents);
            QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(m_dragTip);
            shadow->setBlurRadius(20);
            shadow->setColor(QColor(0, 0, 0, 80)); 
            shadow->setOffset(0, 4);
            m_dragTip->setGraphicsEffect(shadow);
        }

        QString clsid = hitItem->getItemData().getCLSID();
        bool isRecycleBin = (clsid == APP_RECYCLE_BIN_CLS);
        bool isPC = (clsid == APP_MY_COMPUTER_CLS);
        bool isNetwork = (clsid == APP_NETWORK_CLS);
        bool isControlPanel = (clsid == APP_CONTROL_PANEL_CLS);
        bool isFolder = (hitItem->type() == ObjectType::Folder_ObjectType || hitItem->type() == ObjectType::SpecialFolder);

        if (isPC || isNetwork || isControlPanel) {
            m_dragTip->setText("<b style='color:#ff4d4d'>Operation Forbidden</b>");
            m_dragTip->adjustSize();
            m_dragTip->move(event->pos() + QPoint(24, 24));
            m_dragTip->show();
            m_dragTip->raise();
            event->setDropAction(Qt::IgnoreAction);
            event->accept();
            return;
        }

        QString actionKeyword;
        QString highlightColor = "#70a1ff"; // Fluent Blue accent

        if (isRecycleBin) {
            actionKeyword = "Move to";
            highlightColor = "#ff4d4d"; // Red for Delete/Recycle Bin
        } else if (isFolder) {
            actionKeyword = isCtrl ? "Copy to" : "Move to";
        } else {
            actionKeyword = "Open with";
        }

        QString htmlText = QString("<b style='color:%1'>%2</b> <span>%3</span>")
            .arg(highlightColor).arg(actionKeyword).arg(hitItem->baseName());

        m_dragTip->setText(htmlText);
        m_dragTip->adjustSize();
        m_dragTip->move(event->pos() + QPoint(24, 24));
        m_dragTip->show();
        m_dragTip->raise();
        event->accept();
    } 
    else {
        // --- 悬浮在桌面背景上的逻辑 ---
        if (isCtrl) {
            if (!m_dragTip) {
                m_dragTip = new QLabel(this);
                m_dragTip->setStyleSheet(
                    "QLabel {"
                    "  background-color: rgba(255, 255, 255, 235);" 
                    "  color: #202020;"
                    "  border: 1px solid rgba(0, 0, 0, 40);"
                    "  border-radius: 8px;"
                    "  padding: 10px 16px;"
                    "  font-size: 13px;"
                    "  font-family: 'Segoe UI Variable Display', 'Segoe UI', sans-serif;"
                    "}"
                );
                m_dragTip->setAttribute(Qt::WA_TransparentForMouseEvents);
                QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(m_dragTip);
                shadow->setBlurRadius(20);
                shadow->setColor(QColor(0, 0, 0, 80)); 
                shadow->setOffset(0, 4);
                m_dragTip->setGraphicsEffect(shadow);
            }
            m_dragTip->setText("<b style='color:#70a1ff'>Copy to</b> <span>Desktop</span>");
            m_dragTip->adjustSize();
            m_dragTip->move(event->pos() + QPoint(24, 24));
            m_dragTip->show();
            m_dragTip->raise();
        } else {
            if (m_dragTip) m_dragTip->hide();
        }
    }
}

void ListView::dropEvent(QDropEvent* event)
{
    if (m_dragTip) m_dragTip->hide();

    ListItem* hitItem = nullptr;
    if (m_iconShowRule.gridAlignment) {
        for (auto* item : m_items) {
            if (item && item->isVisible() && item->geometry().contains(event->pos()) && !m_selectedItems.contains(item)) {
                hitItem = item;
                break;
            }
        }
    }

    const QMimeData* mime = event->mimeData();
    if (!mime->hasUrls()) return;
    QList<QUrl> urls = mime->urls();

    // Drop onto an APP/File: Open with it; Drop onto Folder: Copy to it
    if (hitItem && !m_selectedItems.contains(hitItem)) {
        QString targetPath = hitItem->filePath();
        QString clsid = hitItem->getItemData().getCLSID();

        // Forbidden targets
        if (clsid == APP_MY_COMPUTER_CLS || clsid == APP_NETWORK_CLS || clsid == APP_CONTROL_PANEL_CLS) {
            event->ignore();
            return;
        }

        QStringList args;
        std::vector<std::wstring> srcPaths;

        for (const QUrl& url : urls) {
            QString localPath = QDir::toNativeSeparators(url.toLocalFile());
            if (!localPath.isEmpty()) {
                args << "\"" + localPath + "\"";
                srcPaths.push_back(localPath.toStdWString());
            }
        }
        
        bool isCtrl = event->keyboardModifiers() & Qt::ControlModifier;

        if (srcPaths.empty()) return;

        bool isRecycleBin = (hitItem->getItemData().getCLSID() == APP_RECYCLE_BIN_CLS);

        if (isRecycleBin) {
            // Move to Recycle Bin
            CSystemApi::DeleteFiles(srcPaths, true, true); // true = recycle, true = confirm
            event->acceptProposedAction();
            return;
        } else if (hitItem->type() == ObjectType::Folder_ObjectType || hitItem->type() == ObjectType::SpecialFolder) {
            // Copy files to target folder
            CSystemApi::CopyFiles(srcPaths, targetPath.toStdWString());
            event->acceptProposedAction();
            return;
        } else {
            // Open with target app
            qDebug() << "Dropping files onto app:" << targetPath << "with args:" << args;
            std::wstring wApp = QDir::toNativeSeparators(targetPath).toStdWString();
            std::wstring wArgs = args.join(" ").toStdWString();
            
            HINSTANCE result = ShellExecuteW(NULL, L"open", wApp.c_str(), wArgs.c_str(), NULL, SW_SHOWNORMAL);
            if ((INT_PTR)result > 32) {
                event->acceptProposedAction();
                return;
            }
        }
    }

    if (urls.isEmpty()) return;

    // Handle dropping onto the desktop background (no hitItem)
    if (!hitItem) {
        bool isCtrl = (event->keyboardModifiers() & Qt::ControlModifier);
        
        // If it's an external drop or a forced copy via Ctrl
        if (event->source() != this || isCtrl) {
            std::vector<std::wstring> srcPaths;
            for (const QUrl& url : urls) {
                QString localPath = QDir::toNativeSeparators(url.toLocalFile());
                if (!localPath.isEmpty()) srcPaths.push_back(localPath.toStdWString());
            }

            if (!srcPaths.empty()) {
                // If it's an external source, standard Windows behavior is to copy/move to desktop dir
                // With Ctrl, we force COPY.
                CSystemApi::CopyFiles(srcPaths, QDir::toNativeSeparators(m_desktopDir).toStdWString());
                event->acceptProposedAction();
                return;
            }
        }
    }

    if (event->source() == this) {
        // --- 内部拖拽落点逻辑 ---
        QPoint dropPos = event->pos(); // 鼠标当前的坐标（即 dragHotSpot 对应的位置）
        
        if (!m_selectedItems.isEmpty()) {
            int strideX = m_iconWidth + m_colSpacing;
            int strideY = m_iconHeight + m_rowSpacing;

            // 1. 计算鼠标位移量 (releasePos - grabPos)
            QPoint displacement = dropPos - m_startPos;

            // 预先计算所有项的目标位置，并检查是否可放置
            QMap<ListItem*, QPoint> targetPositions;
            bool canPlaceAll = true;

            for (auto* item : m_selectedItems) {
                if (!item) continue;

                QPoint itemIntendedTopLeft;

                // 2. 根据位移计算所有项的新“意图”坐标 (需要独立处理跨域拖拽坐标)
                if (item->parentWidget() != this && item->parentWidget() != nullptr) {
                    // 它目前属于 Box，所以 mapToGlobal 获取其实际全局坐标
                    QPoint globalInitialTopLeft = item->mapToGlobal(QPoint(0,0));
                    QPoint initialTopLeftOnDesktop = this->mapFromGlobal(globalInitialTopLeft);
                    itemIntendedTopLeft = initialTopLeftOnDesktop + displacement;
                } else {
                    // 原生桌面图标
                    itemIntendedTopLeft = item->pos() + displacement;
                }

                QPoint targetPos;

                if (m_iconShowRule.gridAlignment) {
                    // Grid Mode: Calculate nearest grid cell
                    int gridX = qRound((double)(itemIntendedTopLeft.x() - m_marginLeft) / strideX);
                    int gridY = qRound((double)(itemIntendedTopLeft.y() - m_marginTop) / strideY);

                    targetPos.setX(m_marginLeft + gridX * strideX);
                    targetPos.setY(m_marginTop + gridY * strideY);

                    // 碰撞检测：检查该网格是否已被“非当前选中”项占用 (仅在网格模式下强制)
                    for (auto* other : m_items) {
                        if (other && other->isVisible() && !m_selectedItems.contains(other)) {
                            if (other->pos() == targetPos) {
                                canPlaceAll = false;
                                break;
                            }
                        }
                    }
                } else {
                    // Free Mode: Use exact floating position
                    targetPos = itemIntendedTopLeft;
                }

                // 边界检查 (无论是哪种模式都保留，防止图标飞出屏幕)
                targetPos.setX(qMax(m_marginLeft, qMin(targetPos.x(), width() - m_iconWidth)));
                targetPos.setY(qMax(m_marginTop, qMin(targetPos.y(), height() - m_iconHeight)));

                if (!canPlaceAll) break;
                targetPositions.insert(item, targetPos);
            }

            // 如果全部检查通过，执行移动
            if (canPlaceAll) {
                for (auto it = targetPositions.begin(); it != targetPositions.end(); ++it) {
                    ListItem* item = it.key();
                    
                    // Moving from BOX to DESKTOP?
                    if (item && item->parentWidget() != this && item->parentWidget() != nullptr) {
                        qDebug() << "Moving item out of box:" << item->baseName();
                        
                        // 1. Clear item from Box (trace up the tree to find LinBox due to scroll areas)
                        QWidget* p = item->parentWidget();
                        LinBox* box = nullptr;
                        while (p) {
                            box = qobject_cast<LinBox*>(p);
                            if (box) break;
                            p = p->parentWidget();
                        }
                        if (box) {
                            box->removeListItem(item);
                        }

                        // 2. Reparent to Desktop
                        item->setBoxId("");
                        item->setParent(this);
                        
                        // 3. Restore icon layout properties for Desktop and apply GRID/FREE target position
                        item->setLayoutMode(ListItem::LayoutMode::IconMode);
                        item->setItemSize(m_iconWidth, m_iconHeight);
                        item->move(it.value()); // <--- Key fix: uses proper grid location instead of outPos
                        item->updateLocationInfo(it.value());
                        
                        // 4. Ensure it's in the correct visible pools
                        if (!m_items.contains(item)) {
                            m_items.push_back(item);
                        }
                        if (m_iconShowRule.showDesktopIcons) {
                            item->show();
                        }
                        item->raise();
                        
                        // IMPORTANT: Do NOT delete the item. 
                        // We use a single unified instance across Box and Desktop.
                    } else {
                        item->move(it.value());
                        item->updateLocationInfo(it.value());
                        item->raise();
                    }
                }
            }
        }
        clearCheckedItem();
    } else {
        // --- 外部拖入逻辑 ---
        QString desktopDir = m_desktopDir;
        std::vector<std::wstring> srcPaths;
        for (const QUrl& url : urls) {
            QString local = url.toLocalFile();
            if (!local.isEmpty() && QDir::toNativeSeparators(local) != QDir::toNativeSeparators(desktopDir)) {
                srcPaths.push_back(QDir::toNativeSeparators(local).toStdWString());
            }
        }
        
        if (!srcPaths.empty()) {
            qDebug() << "External drop into Desktop:" << desktopDir;
            // 调用 Shell API 进行拷贝。 保持 confirm 为 true。
            CSystemApi::CopyFiles(srcPaths, desktopDir.toStdWString());
        }
    }
    if (m_dragTip) m_dragTip->hide();
    event->acceptProposedAction();
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
            // 双击空白处：如果开启了该功能，则切换桌面图标显示
            if (m_iconShowRule.doubleClickHideIcons) {
                onMenuShowDesktopIcons();
            }
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

void ListView::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete) {
        bool permanently = (event->modifiers() & Qt::ShiftModifier);
        onMenuDelete(permanently);
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void ListView::enterEvent(QEvent* event)
{

    /*auto currentPoint = mapToGlobal(QCursor::pos());
    qDebug() << "MouseMoveEvent:CurrentPoint:" << currentPoint;*/
}

ListView* ListView::instance(QWidget* parent) {
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
    // === 同步 Windows 桌面排列设置 ===
    bool osAutoArrange = false;
    bool osSnapToGrid = false;
    CSystemApi::GetDesktopConfigFlags(osAutoArrange, osSnapToGrid);
    m_iconShowRule.autoArrange = osAutoArrange;
    m_iconShowRule.gridAlignment = osSnapToGrid;
    syncViewModeSizes(); // Sync initial sizes to member variables
    qDebug() << "Synced Windows Desktop Layout Rules: AutoArrange=" << osAutoArrange << ", SnapToGrid=" << osSnapToGrid;
    // ===================================

    QVector<ListItem*> listItems;
    vector<ListItemData> allDatas = loadAllItemsData();

    for (auto it = allDatas.begin(); it != allDatas.end(); it++)
    {
        ListItem* listItem = new ListItem(this);
        listItem->setParent(this);
        listItem->setItemData(std::move(*it));
        listItem->setObjectName(QUuid::createUuid().toString(QUuid::WithoutBraces));
        
        // 从 Windows 底层 API 读取的原生坐标同步到桌面元素中
        QPoint nativePos(it->itemData().position.x, it->itemData().position.y);
        qDebug() << "Native position for" << it->itemData().name.c_str() << ":" << nativePos;

        // 如果 nativePos 有效（非纯0），赋予初始位置
        if (nativePos.x() != 0 || nativePos.y() != 0) {
            listItem->updateLocationInfo(nativePos);
            listItem->move(nativePos);
        }
        setupItemConnections(listItem);
 
        // Mutual exclusion: check if this path is already in a box
        QString boxId = getBoxIdForItem(listItem->filePath());
        if (!boxId.isEmpty()) {
            listItem->setBoxId(boxId);
            listItem->hide(); // Strictly hide from desktop
        }

        if (it->type() != ObjectType::SpecialFolder && listItem->getItemData().fullName() != "desktop.ini") {
            if (listItem->boxId().isEmpty()) {
                listItems.push_back(listItem);
            }
        }
        else {
            m_hiddenIcons.push_back(listItem);
        }
        m_allItems.push_back(listItem);
    }

    //load System Items
    if (monitor) {
        sysIconRegisterInfo = monitor->getCurrentIcons();
    }
    for (auto it = sysIconRegisterInfo.begin(); it != sysIconRegisterInfo.end(); it++) {
        if (!it->isHidden) {
            for (auto s_item = m_hiddenIcons.begin(); s_item != m_hiddenIcons.end(); s_item++) {
                ListItem* listItem = *s_item;
                auto clsid = listItem->getItemData().getCLSID();
                if (clsid.replace("::", "") == it->clsid) {
                    // Mutual exclusion for system icons
                    if (listItem->boxId().isEmpty()) {
                        listItems.push_back(listItem);
                    }
                }
            }
        }
    }
    m_items = listItems;

    // Mutual exclusion: remove items that belong to a Box
    QMutableVectorIterator<ListItem*> boxFilter(m_items);
    while (boxFilter.hasNext()) {
        ListItem* item = boxFilter.next();
        if (item) {
            // 1. Filter out system-hidden icons (Special folders that shouldn't show)
            if (m_hiddenIcons.contains(item)) {
                bool isSystemShown = false;
                QString itemClsid = item->getItemData().getCLSID();
                QString tmp = itemClsid.replace("::", "");
                for (const auto& info : sysIconRegisterInfo) {
                    if (info.clsid == itemClsid && !info.isHidden) {
                        isSystemShown = true;
                        break;
                    }
                }
                if (!isSystemShown) {
                    item->hide();
                    boxFilter.remove();
                    continue;
                }
            }

            // 2. Filter out items that belong to a Box
            QString boxId = getBoxIdForItem(item->filePath());
            item->setBoxId(boxId); // Update fresh box state
            
            if (!boxId.isEmpty()) {
                item->hide();
                boxFilter.remove();
            }
        }
    }

    // --- Compact Native Layout Algorithm (Fill the gaps) ---
    // 1. Gather all unique physical coordinates from the entire system desktop
    QVector<QPoint> nativeSlots;
    for (const auto& data : allDatas) {
        QPoint p(data.itemData().position.x, data.itemData().position.y);
        if (p.x() != 0 || p.y() != 0) {
            if (!nativeSlots.contains(p)) {
                nativeSlots.append(p);
            }
        }
    }

    // 2. Sort the physical slots (Column-major: Left->Right, Top->Bottom)
    std::sort(nativeSlots.begin(), nativeSlots.end(), [](const QPoint& a, const QPoint& b){
        if (qAbs(a.x() - b.x()) > 10) return a.x() < b.x();
        return a.y() < b.y();
    });

    // 3. Sort the currently visible items by their original coordinates to maintain order
    std::sort(m_items.begin(), m_items.end(), [](ListItem* a, ListItem* b){
        QPoint pa = a->getLocationInfo();
        QPoint pb = b->getLocationInfo();
        if (qAbs(pa.x() - pb.x()) > 10) return pa.x() < pb.x();
        return pa.y() < pb.y();
    });

    // 4. Sequentially assign the tightly packed slots back to the visible items
    qDebug() << "=== Compacted Native Layout ===";
    for (int i = 0; i < m_items.size() && i < nativeSlots.size(); ++i) {
        m_items[i]->updateLocationInfo(nativeSlots[i]);
        m_items[i]->move(nativeSlots[i]);
        qDebug() << QString("Item(%1) compacted to Position:(%2, %3) - Name: %4")
                    .arg(i)
                    .arg(nativeSlots[i].x())
                    .arg(nativeSlots[i].y())
                    .arg(m_items[i]->getItemData().fullName());
    }
    qDebug() << "===============================";
}

void ListView::loadIconLayer()
{
    qDebug() << "[ListView] Starting reload. Master list size:" << m_allItems.size();
    // Start fresh from all icons, then filter those belonging to boxes
    m_items = m_allItems;
    QMutableVectorIterator<ListItem*> it(m_items);
    while (it.hasNext()) {
        ListItem* item = it.next();
        if (item) {
            // 1. Filter out system-hidden icons (Special folders that shouldn't show)
            if (m_hiddenIcons.contains(item)) {
                bool isSystemShown = false;
                QString itemClsid = item->getItemData().getCLSID();
                QString tmp = itemClsid.replace("::", "");
                for (const auto& info : sysIconRegisterInfo) {
                    if (info.clsid == itemClsid && !info.isHidden) {
                        isSystemShown = true;
                        break;
                    }
                }
                if (!isSystemShown) {
                    item->hide();
                    it.remove();
                    continue;
                }
            }

            // 2. Filter out items that belong to a Box
            QString boxId = getBoxIdForItem(item->filePath());
            item->setBoxId(boxId); // Update fresh box state
            
            if (!boxId.isEmpty()) {
                item->hide();
                it.remove();
            }
        }
    }
    
    qDebug() << "[ListView] After box filtering, icons shown on desktop:" << m_items.size();
    if (m_iconShowRule.autoArrange) {
        qDebug() << "[ListView] Applying auto-layout (Stable Categorized).";
        this->performSort(m_iconShowRule.sortType); // Use non-toggling version for stability
        loadAutoLayout();
    } else if (m_enablePersistence) { // Strictly respect current memory positions if persistence is on
        qDebug() << "[ListView] Applying persistent layout.";
        loadPersistentLayout();
    } else if (m_iconShowRule.gridAlignment) {
        qDebug() << "[ListView] Applying grid layout.";
        loadGridLayout();
    } else {
        qDebug() << "[ListView] No layout rule active? Defaulting to grid.";
        loadGridLayout();
    }
    
    
    // Ensure visibility and stacking
    for (auto* item : m_items) {
        if (!item) continue;
        if (m_iconShowRule.showDesktopIcons) {
            item->show();
            item->raise();
        } else {
            item->hide();
        }
    }

    // Refresh all boxes to ensured their icons are positioned and visible
    for (auto* box : m_boxes) {
        if (box) {
            box->refreshLayout();
            box->raise();
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
        if (Icon_Y > m_marginTop && Icon_Y + m_iconHeight > height() - m_bottomPadding) {
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

        QPoint p = item->getLocationInfo(); // Rely strictly on explicitly calculated memory coordinates
        // Zero-coordinate detection: assign a slot if the item is "homeless"
        if (p == QPoint(0, 0)) {
            p = generateItemPosition();
            item->move(p);
        }

        if (m_iconShowRule.gridAlignment) {
            // 对齐到最近的网格点
            int gridX = qRound((double)(p.x() - m_marginLeft) / (m_iconWidth + m_colSpacing));
            int gridY = qRound((double)(p.y() - m_marginTop) / (m_iconHeight + m_rowSpacing));
            
            // Collision detection: Check if this grid slot is already taken
            static QSet<quint64> occupiedSlots;
            if (item == m_items.first()) occupiedSlots.clear(); // Reset on new run

            // If collision occurs, find an available slot (search down the column, then next column)
            while (occupiedSlots.contains(((quint64)(uint)gridX << 32) | (quint64)(uint)gridY)) {
                gridY++;
                if (gridY > 0 && m_marginTop + gridY * (m_iconHeight + m_rowSpacing) > height() - m_bottomPadding - m_iconHeight) {
                    gridY = 0;
                    gridX++;
                }
            }
            occupiedSlots.insert(((quint64)(uint)gridX << 32) | (quint64)(uint)gridY);

            int snappedX = m_marginLeft + gridX * (m_iconWidth + m_colSpacing);
            int snappedY = m_marginTop + gridY * (m_iconHeight + m_rowSpacing);
            
            // 边界检查
            snappedX = qMax(m_marginLeft, qMin(snappedX, width() - m_iconWidth));
            snappedY = qMax(m_marginTop, qMin(snappedY, height() - m_bottomPadding - m_iconHeight));

            if (item->pos() != QPoint(snappedX, snappedY)) {
                item->updateLocationInfo(QPoint(snappedX, snappedY));
                item->move(snappedX, snappedY);
            }
        } else {
            // 非网格对齐状态下，完全遵循原生/初始化时算出的精准坐标 (不进行干预)
            if (item->pos() != p) {
                item->updateLocationInfo(p);
                item->move(p);
            }
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
    case SortOrderType::SortByCategorized:
        return SortOrderType::SortByCategorized;
    case SortOrderType::Default:
        return SortOrderType::SortByName_ASC; // Or stay at Default
    default:
        return SortOrderType::SortByName_ASC;
    }
}
void ListView::sortItems(SortOrderType order)
{
    // Filter out items that are inside a Box (Mutual Exclusion)
    auto filterIt = std::remove_if(m_items.begin(), m_items.end(), [](ListItem* item) {
        return !item || !item->boxId().isEmpty();
    });
    m_items.erase(filterIt, m_items.end());

    if (m_iconShowRule.sortType == order) {
        order = getOppositeDirection();
    }
    
    performSort(order);
}

void ListView::performSort(SortOrderType order)
{
    switch (order) {
    case SortOrderType::Default:
        // Use Categorized sort if auto-arrange is on, otherwise do nothing
        if (m_iconShowRule.autoArrange) {
            sortByCategorized();
        }
        break;
    case SortOrderType::SortByName_ASC:
        sortByName(true);
        break;
    case SortOrderType::SortByName_DESC:
        sortByName(false);
        break;
    case SortOrderType::SortBySize_ASC:
        sortBySize(true);
        break;
    case SortOrderType::SortBySize_DESC:
        sortBySize(false);
        break;
    case SortOrderType::SortByType_ASC:
        sortByType(true);
        break;
    case SortOrderType::SortByType_DESC:
        sortByType(false);
        break;
    case SortOrderType::SortByDateModified_ASC:
        sortByDate(true);
        break;
    case SortOrderType::SortByDateModified_DESC:
        sortByDate(false);
        break;
    case SortOrderType::SortByCategorized:
        sortByCategorized();
        break;
    default:
        // Do nothing for unknown states
        break;
    }
    m_iconShowRule.sortType = order;
}

void ListView::sortByCategorized()
{
    // Ensure box-contained items are excluded
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(), [](ListItem* item) {
        return !item || !item->boxId().isEmpty();
    }), m_items.end());

    std::sort(m_items.begin(), m_items.end(), [](ListItem* a, ListItem* b) {
        // 1. Definition of priority groups
        auto getPriority = [](ListItem* item) -> int {
            if (item->type() == ObjectType::SpecialFolder) return 0; // System Folders
            if (item->fileInfo().isDir()) return 1;                  // Regular Folders
            
            QString ext = item->fileInfo().suffix().toLower();
            if (ext == "exe" || ext == "lnk" || ext == "url") return 2; // Apps
            
            static const QStringList docExts = {"txt", "pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "md", "zip", "rar", "7z"};
            if (docExts.contains(ext)) return 3;                     // Common Docs
            
            return 4;                                               // Miscellaneous
        };

        int pA = getPriority(a);
        int pB = getPriority(b);
        if (pA != pB) return pA < pB;

        // 2. Secondary sort: Alphabetical (Natural sort order)
        return a->baseName().toLower() < b->baseName().toLower();
    });
}

void ListView::sortByName(bool ascending)
{
    // Ensure box-contained items are excluded
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(), [](ListItem* item) {
        return !item || !item->boxId().isEmpty();
    }), m_items.end());

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

void ListView::sortBySize(bool ascending)
{
    // Ensure box-contained items are excluded
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(), [](ListItem* item) {
        return !item || !item->boxId().isEmpty();
    }), m_items.end());

    std::sort(m_items.begin(), m_items.end(), [ascending](ListItem* a, ListItem* b) {
        // 特殊文件夹（系统图标）始终排在最前，内部顺序保持不变
        bool aIsSpecial = (a->type() == ObjectType::SpecialFolder);
        bool bIsSpecial = (b->type() == ObjectType::SpecialFolder);
        if (aIsSpecial != bIsSpecial)
            return aIsSpecial;

        // 文件夹没有"大小"，统一视为 -1，排在普通文件前面（升序）
        bool aIsDir = a->fileInfo().isDir();
        bool bIsDir = b->fileInfo().isDir();
        if (aIsDir != bIsDir)
            return ascending ? aIsDir : !aIsDir;

        qint64 sizeA = a->fileInfo().size();
        qint64 sizeB = b->fileInfo().size();
        if (sizeA != sizeB)
            return ascending ? (sizeA < sizeB) : (sizeA > sizeB);

        // 大小相同时按名称升序兜底
        return a->baseName().toLower() < b->baseName().toLower();
    });
}

void ListView::sortByType(bool ascending)
{
    // Ensure box-contained items are excluded
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(), [](ListItem* item) {
        return !item || !item->boxId().isEmpty();
    }), m_items.end());

    std::sort(m_items.begin(), m_items.end(), [ascending](ListItem* a, ListItem* b) {
        bool aIsSpecial = (a->type() == ObjectType::SpecialFolder);
        bool bIsSpecial = (b->type() == ObjectType::SpecialFolder);
        if (aIsSpecial != bIsSpecial)
            return aIsSpecial;

        // 按文件扩展名排序；文件夹扩展名视为空字符串（排最前）
        QString extA = a->fileInfo().isDir() ? QString() : a->fileInfo().suffix().toLower();
        QString extB = b->fileInfo().isDir() ? QString() : b->fileInfo().suffix().toLower();
        if (extA != extB)
            return ascending ? (extA < extB) : (extA > extB);

        // 同类型时按名称升序兜底
        return a->baseName().toLower() < b->baseName().toLower();
    });
}

void ListView::sortByDate(bool ascending)
{
    // Ensure box-contained items are excluded
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(), [](ListItem* item) {
        return !item || !item->boxId().isEmpty();
    }), m_items.end());

    std::sort(m_items.begin(), m_items.end(), [ascending](ListItem* a, ListItem* b) {
        bool aIsSpecial = (a->type() == ObjectType::SpecialFolder);
        bool bIsSpecial = (b->type() == ObjectType::SpecialFolder);
        if (aIsSpecial != bIsSpecial)
            return aIsSpecial;

        QDateTime dateA = a->fileInfo().lastModified();
        QDateTime dateB = b->fileInfo().lastModified();
        if (dateA != dateB)
            return ascending ? (dateA < dateB) : (dateA > dateB);

        // 时间相同时按名称升序兜底
        return a->baseName().toLower() < b->baseName().toLower();
    });
}

void ListView::addItem(ListItem* item, bool refresh)
{
    if (!item) return;
    
    // Mutual exclusion: If this file is already in a Box, don't show on desktop
    QString bId = getBoxIdForItem(item->filePath());
    if (!bId.isEmpty()) {
        item->setBoxId(bId);
    }

    if (!item->boxId().isEmpty()) {
        item->hide();
        m_allItems.push_back(item);
        return;
    }

    QPoint pos = generateItemPosition();
    item->setItemSize(m_iconWidth, m_iconHeight);
    item->updateLocationInfo(pos);
    setupItemConnections(item);
    
    m_items.push_back(item);
    m_allItems.push_back(item);
    
    if (m_iconShowRule.showDesktopIcons) {
        item->show();
    } else {
        item->hide();
    }

    if (refresh) {
        loadIconLayer();
        update();
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
        
        // 确保从所属的 Box 中也移除该 item (防止悬空指针)
        if (!item->boxId().isEmpty()) {
            for (auto* box : m_boxes) {
                if (box && box->boxId() == item->boxId()) {
                    box->removeListItem(item);
                    break;
                }
            }
        }

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
            if (item && item->boxId().isEmpty() && (item->pos().x() > 0 || item->pos().y() > 0)) {
                // If the item has already been assigned a position (not 0,0), respect its occupation
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
        if (y > m_marginTop && y + m_iconHeight > height() - m_bottomPadding) {
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
    connect(menu, &RightMenu::DoubleClickedHideIconsTriggered, this, &ListView::onMenuDoubleClickHideIcons);

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
    connect(menu, &RightMenu::createBoxTriggered, this, &ListView::onMenuCreateBox);
    connect(menu, &RightMenu::linDeskTriggered, this, &ListView::onMenuLinDesk);
    connect(menu, &RightMenu::quitAppTriggered, this, &ListView::onMenuQuitApp);
    
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
    if(m_iconShowRule.doubleClickHideIcons) {
        menu->setDoubleClickedHideIconsChecked(true);
    } else {
        menu->setDoubleClickedHideIconsChecked(false);
    }

    // init sort-by sub-menu check state
    switch (m_iconShowRule.sortType) {
    case SortOrderType::SortByName_ASC:
    case SortOrderType::SortByName_DESC:
        menu->setSortByNameChecked(true);
        break;
    case SortOrderType::SortBySize_ASC:
    case SortOrderType::SortBySize_DESC:
        menu->setSortBySizeChecked(true);
        break;
    case SortOrderType::SortByType_ASC:
    case SortOrderType::SortByType_DESC:
        menu->setSortByTypeChecked(true);
        break;
    case SortOrderType::SortByDateModified_ASC:
    case SortOrderType::SortByDateModified_DESC:
        menu->setSortByDateChecked(true);
        break;
    default:
        menu->setSortByNameChecked(true); // 默认选中名称
        break;
    }
}

void ListView::syncViewModeSizes() {
    switch (m_iconShowRule.viewMode) {
        case ViewMode::LargeIcon:
            m_iconWidth  = kLargeIconW;
            m_iconHeight = kLargeIconH;
            m_rowSpacing = kLargeSpacing;
            m_colSpacing = kLargeSpacing;
            ListItemData::s_targetIconSize = kLargeIconW;
            if (rightMenu) rightMenu->setLargeIconsChecked(true);
            break;
        case ViewMode::MediumIcon:
            m_iconWidth  = kMediumIconW;
            m_iconHeight = kMediumIconH;
            m_rowSpacing = kMediumSpacing;
            m_colSpacing = kMediumSpacing;
            ListItemData::s_targetIconSize = kMediumIconW;
            if (rightMenu) rightMenu->setMediumIconsChecked(true);
            break;
        case ViewMode::SmallIcon:
            m_iconWidth = kSmallIconW;
            m_iconHeight = kSmallIconH;
            m_rowSpacing = kSmallSpacing;
            m_colSpacing = kSmallSpacing;
            ListItemData::s_targetIconSize = kSmallIconW;
            if (rightMenu) rightMenu->setSmallIconsChecked(true);
            break;
    }
}

void ListView::onMenuLargeIcons() {
    qDebug() << "ListView: Large Icons triggered";
    if (m_iconShowRule.viewMode == ViewMode::LargeIcon) return;
    m_iconShowRule.viewMode = ViewMode::LargeIcon;
    syncViewModeSizes();
    loadIconLayer();
    update();
    requestSaveBoxes();
}

void ListView::onMenuMediumIcons() {
    qDebug() << "ListView: Medium Icons triggered";
    if (m_iconShowRule.viewMode == ViewMode::MediumIcon) return;
    m_iconShowRule.viewMode = ViewMode::MediumIcon;
    syncViewModeSizes();
    loadIconLayer();
    update();
    requestSaveBoxes();
}

void ListView::onMenuSmallIcons() {
    qDebug() << "ListView: Small Icons triggered";
    if (m_iconShowRule.viewMode == ViewMode::SmallIcon) return;
    m_iconShowRule.viewMode = ViewMode::SmallIcon;
    syncViewModeSizes();
    loadIconLayer();
    update();
    requestSaveBoxes();
}

void ListView::onMenuAutoArrangeIcons() {
    qDebug() << "ListView: Auto Arrange Icons triggered";
    m_iconShowRule.autoArrange = rightMenu->isAutoArrangeIconsChecked();
    
    if (m_iconShowRule.autoArrange) {
        // Automatically default to Categorized sort when entering Auto-Arrange mode
        m_iconShowRule.sortType = SortOrderType::Default;
    }
    
    loadIconLayer();
    updateRightMenuState(); // Sync menu checkboxes
    update();
    requestSaveBoxes();
}

void ListView::onMenuAlignIconsToGrid() {
    qDebug() << "ListView: Align Icons to Grid triggered";
    m_iconShowRule.gridAlignment = rightMenu->isAlignIconsToGridChecked();
    
    if (m_iconShowRule.gridAlignment) {
        loadGridLayout();
    }
    update();
    requestSaveBoxes();
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
    requestSaveBoxes();
}

void ListView::onMenuDoubleClickHideIcons() {
    qDebug() << "ListView: Double Click To Hide Icons triggered";
    m_iconShowRule.doubleClickHideIcons = !m_iconShowRule.doubleClickHideIcons;
    this->rightMenu->setDoubleClickedHideIconsChecked(m_iconShowRule.doubleClickHideIcons);
    requestSaveBoxes();
}

void ListView::setDoubleClickHideIconsEnabled(bool enabled) {
    m_iconShowRule.doubleClickHideIcons = enabled;
    requestSaveBoxes();
}

bool ListView::isDoubleClickHideIconsEnabled() const {
    return m_iconShowRule.doubleClickHideIcons;
}

void ListView::onMenuLinDesk() {
    showSettings(nullptr);
}

#include "SettingUI_GL.h"

void ListView::showSettings(LinBox* target) {
    if (m_settingsWindow) {
        m_settingsWindow->setTargetBox(target);
        m_settingsWindow->show();
        m_settingsWindow->raise();
        m_settingsWindow->activateWindow();
        return;
    }
    QVector<LinBox*> all = boxes();
    m_settingsWindow = new SettingUI(target, all);
    m_settingsWindow->show();
}

void ListView::showSettingsGL(LinBox* target) {
    if (m_settingsWindowGL) {
        m_settingsWindowGL->setTargetBox(target);
        m_settingsWindowGL->show();
        m_settingsWindowGL->raise();
        m_settingsWindowGL->activateWindow();
        return;
    }
    QVector<LinBox*> all = boxes();
    m_settingsWindowGL = new SettingUI_GL(target, all);
    m_settingsWindowGL->show();
}

void ListView::onMenuSortByName() {
    qDebug() << "ListView: Sort By Name triggered";
    this->sortItems(SortOrderType::SortByName_ASC);
    updateRightMenuState();
    loadAutoLayout(); // Force re-layout and update memory positions after sorting
    update();
    requestSaveBoxes();
}

void ListView::onMenuSortBySize() {
    qDebug() << "ListView: Sort By Size triggered";
    this->sortItems(SortOrderType::SortBySize_ASC);
    updateRightMenuState();
    loadAutoLayout();
    update();
    requestSaveBoxes();
}

void ListView::onMenuSortByType() {
    qDebug() << "ListView: Sort By Type triggered";
    this->sortItems(SortOrderType::SortByType_ASC);
    updateRightMenuState();
    loadAutoLayout();
    update();
    requestSaveBoxes();
}

void ListView::onMenuSortByDate() {
    qDebug() << "ListView: Sort By Date triggered";
    this->sortItems(SortOrderType::SortByDateModified_ASC);
    updateRightMenuState();
    loadAutoLayout();
    update();
    requestSaveBoxes();
}

void ListView::onMenuRefresh() {
    // 1. Start Fake Animation: Hide the entire desktop layer
    this->hide();

    // 2. Delay the actual logical refresh
    QTimer::singleShot(10, this, [this]() {
        this->show(); // Bring desktop back
        
        // --- Original Refresh Logic ---
        if (m_enablePersistence) {
            for (auto* item : m_allItems) {
                if (!item) continue;
                QString normPath = QDir::toNativeSeparators(item->filePath()).toLower();
                while (normPath.endsWith('\\')) normPath.chop(1);
                if (item->pos() != QPoint(0, 0)) {
                    m_savedIconPositions[normPath] = { item->x(), item->y(), item->boxId() };
                }
            }
        }

        qDebug() << "Comprehensive State Sync (Mirroring Initialize)...";
        
        if (monitor) {
            monitor->refreshIconsState();
            sysIconRegisterInfo = monitor->getCurrentIcons();
        }

        QVector<ListItem*> desktopVisible;
        m_hiddenIcons.clear(); 

        for (auto* item : m_allItems) {
            if (!item) continue;
            QString boxId = getBoxIdForItem(item->filePath());
            item->setBoxId(boxId);

            bool isVisibleSpecial = false;
            bool isSpecial = (item->getItemData().type() == ObjectType::SpecialFolder ||
                              SystemFolderCLSIDs.contains(item->getItemData().getCLSID()) ||
                              item->getItemData().fullName() == "desktop.ini" ||
                              item->filePath().startsWith("::"));
            
            bool isOsHidden = item->fileInfo().isHidden();

            if (isSpecial) {
                QString itemClsid = item->getItemData().getCLSID();
                itemClsid.replace("::", "").replace("{", "").replace("}", "");
                bool systemSaysVisible = false;
                bool foundInRegistry = false;

                for (const auto& info : sysIconRegisterInfo) {
                    QString sysClsidStr = info.clsid;
                    sysClsidStr.replace("{", "").replace("}", "");
                    if (sysClsidStr.toLower() == itemClsid.toLower()) {
                        foundInRegistry = true;
                        if (!info.isHidden) {
                            systemSaysVisible = true;
                        }
                        break;
                    }
                }
                isVisibleSpecial = systemSaysVisible;
            }

            if (!boxId.isEmpty()) {
                if (item->parent() == this) item->hide();
                m_hiddenIcons.push_back(item);
            } else if (isOsHidden && !isSpecial) {
                item->hide();
                m_hiddenIcons.push_back(item);
            } else if (isSpecial) {
                if (isVisibleSpecial) {
                    desktopVisible.push_back(item);
                    if (m_iconShowRule.showDesktopIcons) item->show();
                } else {
                    item->hide();
                    m_hiddenIcons.push_back(item);
                }
            } else {
                desktopVisible.push_back(item);
                if (m_iconShowRule.showDesktopIcons) item->show();
            }
        }

        m_items = desktopVisible;
        update();

        // Ensure boxes are re-shown
        for (auto* box : m_boxes) {
            if (box) {
                box->show();
                box->raise();
            }
        }
    });
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
        // Use the current sortType from m_iconShowRule
        this->sortItems(m_iconShowRule.sortType);
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
            setupItemConnections(newItem); // Connect newly created files to the signaling system. Riverside.
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
    }
    
    // Ensure all LinBoxes stay above desktop items
    for (auto* box : m_boxes) {
        if (box) box->raise();
    }
}

void ListView::onMenuQuitApp() {
    qDebug() << "LinDesk: Global Quit Requested...";
    
    // Close the settings window if open
    if (m_settingsWindow) {
        m_settingsWindow->close();
    }

    // Close all remaining windows (boxes, etc.)
    QApplication::closeAllWindows();
    
    // Final quit
    qApp->quit();
}

void ListView::onMenuCreateBox()
{
	LinBox* box = new LinBox(this);
	box->move(m_lastRightClickPos);
	
    // Inherit parameters from the last created box if available
    if (!m_boxes.isEmpty()) {
        LinBox* templateBox = m_boxes.last();
        box->setBackgroundColor(templateBox->backgroundColor());
        box->setBackgroundOpacity(templateBox->backgroundOpacity());
        box->setGlassEffect(templateBox->glassEffect());
    } else {
        // Default values when no config exists
        box->setGlassEffect(false); // Default to NO glass as requested
        box->setBackgroundOpacity(0.4f);
    }
    
	setupBoxConnections(box);

	box->show();
	box->raise(); // Ensure new box is on top
	m_boxes.append(box);
	
	requestSaveBoxes(); // Save newly created box
}

void ListView::setupItemConnections(ListItem* item, LinBox* box)
{
	if (!item) return;
	
	connect(item, &ListItem::itemClicked, this, [this, box](ListItem* i, QMouseEvent* e) {
		onItemRelayClicked(box, i, e);
	}, Qt::UniqueConnection);
	connect(item, &ListItem::itemMoved, this, [this, box](ListItem* i, QMouseEvent* e) {
		onItemRelayMoved(box, i, e);
	}, Qt::UniqueConnection);
	connect(item, &ListItem::itemReleased, this, [this, box](ListItem* i, QMouseEvent* e) {
		onItemRelayReleased(box, i, e);
	}, Qt::UniqueConnection);
	connect(item, &ListItem::itemDoubleClicked, this, [this, box](ListItem* i, QMouseEvent* e) {
		onItemRelayDoubleClicked(box, i, e);
	}, Qt::UniqueConnection);
	connect(item, &ListItem::itemContextMenuRequested, this, [this, box](ListItem* i, const QPoint& p) {
		onItemRelayContextMenuRequested(box, i, p);
	}, Qt::UniqueConnection);
}

void ListView::onItemRelayClicked(LinBox* box, ListItem* item, QMouseEvent* event)
{
	if (!item) return;

	if (event->button() == Qt::LeftButton) {
		// Ensure focus is on the correct window to receive Delete shortcuts
		if (box) box->setFocus();
		else this->setFocus();

		Qt::KeyboardModifiers modifiers = QApplication::keyboardModifiers();
		
		if (modifiers & Qt::ControlModifier) {
			// Ctrl: 切换单个 item 选中状态
			if (m_selectedItems.contains(item)) {
				m_selectedItems.removeOne(item);
				item->unCheckSelf();
			} else {
				m_selectedItems.append(item);
				item->checkSelf();
			}
		} else if (modifiers & Qt::ShiftModifier) {
			// Shift: 追加选中
			if (!m_selectedItems.contains(item)) {
				m_selectedItems.append(item);
				item->checkSelf();
			}
		} else {
			// 无修饰键：
			// 如果点击的 item 已经在多选集合里，保留整个选中组（准备多选拖动）
			// 否则清空选中，只选这一个
			if (!m_selectedItems.contains(item)) {
				clearCheckedItem();
				m_selectedItems.append(item);
				item->checkSelf();
			}
			// else: item 已在选中列表，选中状态不变，继续持有整个多选组拖动
		}

		// Start Dragging
		m_draggedItem = item;
		QPoint globalPos = item->mapToGlobal(event->pos());
		QPoint clickPosInListView = this->mapFromGlobal(globalPos);
		
		// Use global mapping to ensure correct coordinates for items in top-level Box windows
		QPoint globalItemTopLeft = item->mapToGlobal(QPoint(0,0));
		QPoint itemPosOnListView = this->mapFromGlobal(globalItemTopLeft);
		
		m_dragOffset = clickPosInListView - itemPosOnListView;
		m_isDragging = true;
        m_startPos = clickPosInListView; // 记录开始位置以判断拖出阈值
	}
}

void ListView::onItemRelayMoved(LinBox* box, ListItem* item, QMouseEvent* event)
{
	if (m_isDragging) {
		QPoint globalPos = item->mapToGlobal(event->pos());
		QPoint mousePos = this->mapFromGlobal(globalPos);
        
        // 检查启动 QDrag (外部拖出) 的阈值
        if ((mousePos - m_startPos).manhattanLength() > QApplication::startDragDistance()) {
            
            // For internal tracking, create visual ghosts only when actually moving
            if (m_ghosts.isEmpty()) {
                createDragGhosts(m_startPos);
            } else {
                // Update ghosts while moving within or over the Box
                for (auto& ghost : m_ghosts) {
                    if (ghost.widget) {
                        // Use global mouse position for top-level ghost windows
                        ghost.widget->move(globalPos + ghost.offset);
                    }
                }
            }

            // 准备拖出数据
            QMimeData* mimeData = new QMimeData();
            QList<QUrl> urls;
            for (auto* it : m_selectedItems) {
                urls << QUrl::fromLocalFile(it->filePath());
            }
            mimeData->setUrls(urls);
            
            QDrag* drag = new QDrag(this);
            drag->setMimeData(mimeData);
            
            // --- 多图标拖拽视觉效果增强 ---
            if (!m_selectedItems.isEmpty()) {
                // 1. 计算所有选中项的联合包围盒 (使用全局屏幕坐标，防止Box内部局部坐标污染)
                QRect globalBounding;
                for (auto* it : m_selectedItems) {
                    QRect globalRect(it->mapToGlobal(QPoint(0,0)), it->size());
                    globalBounding = globalBounding.united(globalRect);
                }

                // 2. 创建一个足够大的透明 Pixmap 来承载所有图标
                QPixmap fullPix(globalBounding.size());
                fullPix.fill(Qt::transparent);
                QPainter painter(&fullPix);
                painter.setRenderHint(QPainter::Antialiasing);

                for (auto* it : m_selectedItems) {
                    // 暂时取消选中和悬浮状态以抓取干净的图像
                    bool wasSelected = it->isSelected;
                    bool wasHovered = it->isHovered;
                    it->isSelected = false;
                    it->isHovered = false;

                    // 将 item 渲染到 Pixmap 的相对位置（全局位置相对于全局包围盒左上角）
                    QPoint globalItemPos = it->mapToGlobal(QPoint(0,0));
                    it->render(&painter, globalItemPos - globalBounding.topLeft());

                    // 恢复状态
                    it->isSelected = wasSelected;
                    it->isHovered = wasHovered;
                }
                painter.end();

                drag->setPixmap(fullPix);
                
                // 3. 设置热点：用户最初点击点在全局包围盒内的精确相对坐标
                // m_startPos 需要先转回全局坐标，才能与 globalBounding 正确计算
                QPoint globalStartPos = this->mapToGlobal(m_startPos);
                QPoint hotSpot = globalStartPos - globalBounding.topLeft();
                drag->setHotSpot(hotSpot);
            }
            
            // 开始拖拽前隐藏 ghosts (因为 QDrag 会接管)
            for (auto& ghost : m_ghosts) {
                if (ghost.widget) ghost.widget->hide();
            }
            
            // 执行拖拽 (这会阻塞直到释放)
            Qt::DropAction dropAction = drag->exec(Qt::CopyAction | Qt::MoveAction);
            
            // 拖拽结束后的清理
            m_isDragging = false;
            m_draggedItem = nullptr;
            for (auto& ghost : m_ghosts) {
                if (ghost.widget) ghost.widget->deleteLater();
            }
            m_ghosts.clear();
            
            return;
        }

		for (auto& ghost : m_ghosts) {
			if (ghost.widget) {
				ghost.widget->move(mousePos + ghost.offset);
			}
		}
	}
}

void ListView::onItemRelayReleased(LinBox* box, ListItem* item, QMouseEvent* event)
{
	if (m_isDragging) {
		bool hasCollision = false;
		QVector<QPoint> targetPositions;

		// 1. Calculate target positions and check collisions for desktop icons
		if (!box) {
			if (m_iconShowRule.gridAlignment) {
				for (const auto& ghost : m_ghosts) {
					if (ghost.widget && ghost.item) {
						QPoint pos = ghost.widget->pos();
						int gridX = qRound((double)(pos.x() - m_marginLeft) / (m_iconWidth + m_colSpacing));
						int gridY = qRound((double)(pos.y() - m_marginTop) / (m_iconHeight + m_rowSpacing));
						
						QPoint snappedPos;
						snappedPos.setX(m_marginLeft + gridX * (m_iconWidth + m_colSpacing));
						snappedPos.setY(m_marginTop + gridY * (m_iconHeight + m_rowSpacing));
						snappedPos.setX(qMax(m_marginLeft, qMin(snappedPos.x(), width() - m_iconWidth)));
						snappedPos.setY(qMax(m_marginTop, qMin(snappedPos.y(), height() - m_iconHeight)));
						
						targetPositions.append(snappedPos);

						for (auto* otherItem : m_items) {
							if (otherItem && otherItem->isVisible() && !m_selectedItems.contains(otherItem)) {
								if (otherItem->pos() == snappedPos) {
									hasCollision = true;
									break;
								}
							}
						}
					} else {
						targetPositions.append(QPoint(-1, -1));
					}
					if (hasCollision) break;
				}
			}
		}

		// 2. Apply movement
		int idx = 0;
		for (auto& ghost : m_ghosts) {
			if (ghost.widget && ghost.item) {
				if (!hasCollision) {
					QPoint finalPos = ghost.widget->pos();
					if (!box && m_iconShowRule.gridAlignment) {
						finalPos = targetPositions[idx];
					}
					
					// Only move if it's a desktop icon (parent is ListView)
					// Items in box stay where they are (refreshLayout handles them)
					if (!box) {
						ghost.item->move(finalPos);
						ghost.item->updateLocationInfo(finalPos);
					}
				}
				ghost.widget->hide();
				ghost.widget->deleteLater();
			}
			idx++;
		}

		m_ghosts.clear();
		m_isDragging = false;
		m_draggedItem = nullptr;
		
		// Snap back to grid in LinBox if applicable
		if (box) {
			box->refreshLayout();
		}
	}
}

void ListView::createDragGhosts(const QPoint& clickPos)
{
	// 清理旧的 ghost
	for (auto& ghost : m_ghosts) {
		if (ghost.widget) ghost.widget->deleteLater();
	}
	m_ghosts.clear();

	for (auto* item : m_selectedItems) {
		if (!item) continue;

		// Use top-level Window flags so the ghost stays above both the Desktop and the Box window.
		// Qt::Tool prevents taskbar entry.
		QLabel* ghostWidget = new QLabel(nullptr); 
		ghostWidget->setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput);
		ghostWidget->setAttribute(Qt::WA_TranslucentBackground);
		ghostWidget->setAttribute(Qt::WA_NoSystemBackground);
		ghostWidget->setStyleSheet("background: transparent;");

		// 抓取快照（临时取消选中以获得干净图标）
		bool wasSelected = item->isSelected;
		item->isSelected = false;
		item->update();
		QPixmap pixmap = item->grab();
		item->isSelected = wasSelected;
		item->update();

		ghostWidget->setPixmap(pixmap);
		ghostWidget->setFixedSize(item->size());
		
		// Use global coordinates directly since the ghosts are now top-level windows
		QPoint globalItemPos = item->mapToGlobal(QPoint(0,0));
		
		// Map the initial ListView-relative click position back to absolute screen coordinates
		QPoint globalClickPos = this->mapToGlobal(clickPos); 
		
		// The offset keeps the cursor at the exact relative spot over the icon it was clicked on
		QPoint offset = globalItemPos - globalClickPos;
		
		// Snap immediately to the current cursor position to avoid a 1-frame jump
		QPoint currentGlobalMousePos = QCursor::pos();
		ghostWidget->move(currentGlobalMousePos + offset);
		ghostWidget->show();

		m_ghosts.append({ ghostWidget, item, offset });
	}
}

void ListView::onItemRelayDoubleClicked(LinBox* box, ListItem* item, QMouseEvent* event)
{
	if (item) {
		item->openItem();
	}
}

void ListView::onItemRelayContextMenuRequested(LinBox* box, ListItem* item, const QPoint& globalPos)
{
	if (!item) return;

	// Ensure item is selected for context menu
	if (!m_selectedItems.contains(item)) {
		clearCheckedItem();
		m_selectedItems.append(item);
		item->checkSelf();
	}

	requestSystemRightMenu(item->type(), globalPos, m_selectedItems);
}
void ListView::onMenuDelete(bool permanently)
{
    if (m_selectedItems.isEmpty()) return;

    std::vector<std::wstring> paths;
    for (auto* item : m_selectedItems) {
        if (item->type() == ObjectType::SpecialFolder) continue;
        paths.push_back(QDir::toNativeSeparators(item->filePath()).toStdWString());
    }

    if (paths.empty()) return;

    // SHFileOperation will show confirmation window
    CSystemApi::DeleteFiles(paths, !permanently, true);
}

void ListView::saveBoxes() {
    QFile file(m_persistencePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "Failed to open persistence file for writing:" << m_persistencePath;
        return;
    }

#ifdef QT_DEBUG
    // Force sync item states
    for (auto* item : m_allItems) {
        if (!item) continue;
        bool found = false;
        for (auto* box : m_boxes) {
            if (box && box->items().contains(item)) {
                item->setBoxId(box->boxId());
                found = true;
                break;
            }
        }
        if (!found) item->setBoxId("");
    }

    QJsonObject rootObj;
    QJsonObject settings;
    settings["persistence"] = m_enablePersistence;
    
    QJsonObject menu;
    menu["viewMode"] = (int)m_iconShowRule.viewMode;
    menu["sortType"] = (int)m_iconShowRule.sortType;
    menu["iconPosMode"] = (int)m_iconShowRule.iconPosMode;
    menu["autoArrange"] = m_iconShowRule.autoArrange;
    menu["gridAlignment"] = m_iconShowRule.gridAlignment;
    menu["showDesktopIcons"] = m_iconShowRule.showDesktopIcons;
    menu["doubleClickHideIcons"] = m_iconShowRule.doubleClickHideIcons;
    settings["menu"] = menu;
    settings["showShortcutArrow"] = m_iconShowRule.showShortCutArrow;
    
    rootObj["Settings"] = settings;

    if (m_enablePersistence) {
        QJsonArray boxArray;
        for (auto* box : m_boxes) {
            if (!box) continue;
            QJsonObject b;
            b["id"] = box->boxId();
            b["title"] = box->boxTitle();
            b["x"] = box->x();
            b["y"] = box->y();
            b["w"] = box->width();
            b["h"] = box->isCollapsed() ? box->originalHeight() : box->height();
            b["collapsed"] = box->isCollapsed();
            b["showType"] = (int)box->showType();
            b["color"] = box->backgroundColor().name();
            b["opacity"] = qRound(box->backgroundOpacity() * 100);
            b["glass"] = box->glassEffect();
            QJsonArray files;
            for (auto* item : box->items()) files.append(item->filePath());
            b["files"] = files;
            boxArray.append(b);
        }
        rootObj["box-datas"] = boxArray;

        QJsonArray desktopArray;
        for (auto* item : m_allItems) {
            if (!item) continue;
            QJsonObject iconObj;
            iconObj["path"] = item->filePath();
            iconObj["x"] = item->x();
            iconObj["y"] = item->y();
            if (!item->boxId().isEmpty()) iconObj["boxId"] = item->boxId();
            desktopArray.append(iconObj);
        }
        rootObj["desktop-datas"] = desktopArray;
    }
    
    // showShortcutArrow is now inside Settings

    file.write(QJsonDocument(rootObj).toJson());
#else
    QDataStream out(&file);
    out << m_enablePersistence;
    
    // Header settings (Always load first)
    out << (int)m_iconShowRule.viewMode << (int)m_iconShowRule.sortType 
        << (int)m_iconShowRule.iconPosMode << m_iconShowRule.autoArrange 
        << m_iconShowRule.gridAlignment << m_iconShowRule.showDesktopIcons 
        << m_iconShowRule.doubleClickHideIcons << m_iconShowRule.showShortCutArrow;

    if (m_enablePersistence) {
        out << (int)m_boxes.size();
        for (auto* box : m_boxes) {
            if (!box) continue;
            out << box->boxId() << box->boxTitle() << box->geometry() 
                << box->isCollapsed() << box->originalHeight() << (int)box->showType() 
                << box->backgroundColor() << (double)box->backgroundOpacity() << box->glassEffect();
            QStringList files;
            for (auto* item : box->items()) files << item->filePath();
            out << files;
        }

        out << (int)m_allItems.size();
        for (auto* item : m_allItems) {
            if (!item) continue;
            out << item->filePath() << item->x() << item->y() << (item->boxId().isEmpty() ? QString("") : item->boxId());
        }
    }
#endif
    file.close();
    qDebug() << "Saved boxes to:" << m_persistencePath;
}

void ListView::requestSaveBoxes() {
    if (m_saveTimer) {
        m_saveTimer->start(); // Restart timer
    }
}

void ListView::loadBoxes(bool autoUpdateState) {
    QFile file(m_persistencePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
         qDebug() << "No persistence file found at:" << m_persistencePath;
         loadIconLayer(); // Ensure desktop initializes even without a save file
         update();
         return;
    }

    m_savedIconPositions.clear();

#ifdef QT_DEBUG
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject rootObj = doc.object();
    
    if (rootObj.contains("Settings") || rootObj.contains("settings")) {
        QString key = rootObj.contains("Settings") ? "Settings" : "settings";
        QJsonObject s = rootObj[key].toObject();
        bool fileState = s["persistence"].toBool(true);
        if (autoUpdateState) m_enablePersistence = fileState;
        
        if (s.contains("showShortcutArrow")) {
            m_iconShowRule.showShortCutArrow = s["showShortcutArrow"].toBool();
        }

        if (s.contains("menu")) {
            QJsonObject m = s["menu"].toObject();
            if (m.contains("viewMode")) m_iconShowRule.viewMode = (ViewMode)m["viewMode"].toInt();
            if (m.contains("sortType")) m_iconShowRule.sortType = (SortOrderType)m["sortType"].toInt(static_cast<int>(SortOrderType::Default)); // Default to Default
            if (m.contains("iconPosMode")) m_iconShowRule.iconPosMode = (IconPosMode)m["iconPosMode"].toInt();
            syncViewModeSizes(); // Apply loaded ViewMode to width/height/spacing variables
            
            m_iconShowRule.autoArrange = m["autoArrange"].toBool(m_iconShowRule.autoArrange);
            m_iconShowRule.gridAlignment = m["gridAlignment"].toBool(m_iconShowRule.gridAlignment);
            m_iconShowRule.showDesktopIcons = m["showDesktopIcons"].toBool(m_iconShowRule.showDesktopIcons);
            m_iconShowRule.doubleClickHideIcons = m["doubleClickHideIcons"].toBool(m_iconShowRule.doubleClickHideIcons);
            qDebug() << "[ListView] Loaded JSON settings. Persistence:" << m_enablePersistence << "ShowIcons:" << m_iconShowRule.showDesktopIcons;
        }
    }
    
    updateRightMenuState();

    if (!m_enablePersistence) {
        file.close();
        loadIconLayer();
        return;
    }

    QJsonArray boxArray = rootObj["box-datas"].toArray();
    for (int i = 0; i < boxArray.size(); ++i) {
        QJsonObject b = boxArray[i].toObject();
        QStringList files;
        QJsonArray fArr = b["files"].toArray();
        for (int j = 0; j < fArr.size(); ++j) files << fArr[j].toString();
        
        QRect geom(b["x"].toInt(), b["y"].toInt(), b["w"].toInt(), b["h"].toInt());
        QColor color = QColor(b["color"].toString());
        float opacity = b["opacity"].toDouble() / 100.0f;
        if (opacity <= 0) opacity = 0.4f;
        bool glass = b.contains("glass") ? b["glass"].toBool() : true;
        QString boxId = b.contains("id") ? b["id"].toString() : QUuid::createUuid().toString();
 
        createBoxFromSavedData(b["title"].toString(), geom, b["collapsed"].toBool(), b["h"].toInt(), b["showType"].toInt(), files, color, opacity, glass, boxId, false);
    }

    QJsonArray desktopArray = rootObj["desktop-datas"].toArray();
    for (int i = 0; i < desktopArray.size(); ++i) {
        QJsonObject d = desktopArray[i].toObject();
        QString path = QDir::toNativeSeparators(d["path"].toString()).toLower();
        m_savedIconPositions[path] = { d["x"].toInt(), d["y"].toInt(), d["boxId"].toString() };
    }
#else
    QDataStream in(&file);
    bool fileState;
    in >> fileState;
    if (autoUpdateState) m_enablePersistence = fileState;
    
    // Load show rule state from binary (Always read header)
    int vMode, sType, pMode;
    in >> vMode >> sType >> pMode >> m_iconShowRule.autoArrange >> m_iconShowRule.gridAlignment >> m_iconShowRule.showDesktopIcons >> m_iconShowRule.doubleClickHideIcons >> m_iconShowRule.showShortCutArrow;
    m_iconShowRule.viewMode = (ViewMode)vMode;
    m_iconShowRule.sortType = (SortOrderType)sType;
    m_iconShowRule.iconPosMode = (IconPosMode)pMode;

    // Finalize global settings before potentially returning early for data
    updateRightMenuState();

    if (!m_enablePersistence) {
        file.close();
        loadIconLayer(); 
        return;
    }
    int count; in >> count;
    for (int i = 0; i < count; ++i) {
        QString boxId; QString title; QRect geom; bool collapsed; int origH; int showType; QStringList files;
        QColor color; double opacity; bool glass;
        in >> boxId >> title >> geom >> collapsed >> origH >> showType >> color >> opacity >> glass >> files;
        createBoxFromSavedData(title, geom, collapsed, origH, showType, files, color, (float)opacity, glass, boxId, false);

    }

    int iconCount; in >> iconCount;
    for (int i = 0; i < iconCount; ++i) {
        QString path; int x; int y; QString boxId;
        in >> path >> x >> y >> boxId;
        QString normPath = QDir::toNativeSeparators(path).toLower();
        m_savedIconPositions[normPath] = { x, y, boxId };
    }
#endif
    file.close();
    
    // Batch update after all boxes loaded
    loadIconLayer();
    update();
    requestSaveBoxes();

    qDebug() << "Loaded. Persistence:" << m_enablePersistence;
    updateRightMenuState();
    loadIconLayer();
}

void ListView::setupBoxConnections(LinBox* box) {
    if (!box) return;
    connect(box, &LinBox::itemRelayClicked, this, &ListView::onItemRelayClicked);
    connect(box, &LinBox::itemRelayMoved, this, &ListView::onItemRelayMoved);
    connect(box, &LinBox::itemRelayReleased, this, &ListView::onItemRelayReleased);
    connect(box, &LinBox::itemRelayDoubleClicked, this, &ListView::onItemRelayDoubleClicked);
    connect(box, &LinBox::itemRelayContextMenuRequested, this, &ListView::onItemRelayContextMenuRequested);
    connect(box, &LinBox::geometryChanged, this, &ListView::requestSaveBoxes);
    connect(box, &LinBox::destroyRequested, this, &ListView::onMenuDestroyBox);
    connect(box, &LinBox::customRequested, this, &ListView::onMenuCustomBox);
}

void ListView::createBoxFromSavedData(const QString& title, const QRect& geom, bool collapsed, int origH, int showType, const QStringList& files, const QColor& color, float opacity, bool glass, const QString& explicitId, bool refresh) {
    LinBox* box = new LinBox(this);
    if (explicitId.isEmpty()) {
        box->setBoxId(QUuid::createUuid().toString());
    } else {
        box->setBoxId(explicitId);
    }
    
    if (!title.isEmpty()) box->setBoxTitle(title);
    
    if (color.isValid()) box->setBackgroundColor(color);
    box->setBackgroundOpacity(opacity);
    box->setGlassEffect(glass);
    
    setupBoxConnections(box);
    
    for (const QString& path : files) {
        if (path.trimmed().isEmpty()) continue; // Skip empty/placeholder strings
        
        if (QFile::exists(path) || path.startsWith("::")) {
            // Find existing desktop item or create new if not found
            ListItem* item = selectItemByPath(path);
            
            if (item) {
                // Reuse existing widget and move into box
                item->setBoxId(box->boxId());
                item->hide();
                m_items.removeOne(item); // Remove from desktop visibly
                setupItemConnections(item, box);
                box->addListItem(item, false);
            } else {
                // Create brand new
                item = new ListItem(path, box);
                item->setBoxId(box->boxId());
                setupItemConnections(item, box);
                box->addListItem(item, false);
                m_allItems.push_back(item); 
            }
        }
    }
    
    box->setShowType((LinBox::BoxShowType)showType);
    box->setGeometry(geom);
    if (collapsed) box->toggleCollapse();

    box->show();
    box->setGlassEffect(glass); // Apply blur AFTER window is fully visible to prevent black screen
    box->refreshLayout(); // Force initial tight packing
    box->raise();
    m_boxes.append(box);

    if (refresh) {
        loadIconLayer();
        update();
        requestSaveBoxes();
    }
}

void ListView::onMenuDestroyBox(LinBox* box) {
    if (!box) return;
    
    // Mutual exclusion: Restore items to desktop
    QString boxId = box->boxId();
    for (auto* item : m_allItems) {
        if (item && item->boxId() == boxId) {
            item->setBoxId("");
            item->setParent(this);
            if (!m_items.contains(item)) {
                m_items.push_back(item);
            }
            if (m_iconShowRule.showDesktopIcons) {
                item->show();
            }
        }
    }
    
    m_boxes.removeAll(box);
    box->deleteLater();
    requestSaveBoxes(); 
    loadIconLayer(); // Refresh desktop layout
}

void ListView::onMenuCustomBox(LinBox* box) {
    // Placeholder for color customizations
    qDebug() << "Customize box requested:" << box->boxTitle();
}

void ListView::moveItemToBox(const QString& filePath, LinBox* targetBox, bool refresh) {
    if (!targetBox || filePath.isEmpty()) return;
    
    QString targetPath = QDir::toNativeSeparators(filePath).toLower();
    while (targetPath.endsWith('\\')) targetPath.chop(1);

    // 1. Check if it's already in THIS target box
    for (auto* item : targetBox->items()) {
        if (item && item->normalizedPath() == targetPath) return;
    }

    // 2. Find and RE-USE existing item from everywhere (Desktop or other boxes)
    ListItem* existingItem = selectItemByPath(filePath);
    
    if (existingItem) {
        // Redraw old location on Desktop to avoid ghosting before reparenting
        update(existingItem->geometry());

        // Correct parent tracking: if it is ALREADY in a box (even this one), remove it first
        QWidget* p = existingItem->parentWidget();
        LinBox* oldBox = nullptr;
        while (p) {
            oldBox = qobject_cast<LinBox*>(p);
            if (oldBox) break;
            p = p->parentWidget();
        }
        if (oldBox) {
            oldBox->removeListItem(existingItem);
        }
        
        // Update state
        existingItem->setBoxId(targetBox->boxId()); 
        
        // Ensure it's not in the desktop visible pool
        m_items.removeOne(existingItem);
        
        // Signals and container move
        setupItemConnections(existingItem, targetBox);
        targetBox->addListItem(existingItem, refresh);
    } 
    else {
        // Redraw target box area just in case
        update(targetBox->geometry());

        // Fallback for external drops
        ListItem* boxItem = new ListItem(targetPath, targetBox);
        boxItem->setBoxId(targetBox->boxId());
        m_allItems.push_back(boxItem); 
        setupItemConnections(boxItem, targetBox);
        targetBox->addListItem(boxItem, refresh);
    }
    
    if (refresh) {
        if (m_enablePersistence) {
            loadPersistentLayout();
        } else if (m_iconShowRule.autoArrange) {
            loadAutoLayout();
        } else {
            loadGridLayout();
        }
        update();
        requestSaveBoxes();
    }
    
    qDebug() << "Moved item to box (re-parented):" << filePath << "->" << targetBox->boxTitle();
}

bool ListView::isItemInAnyBox(const QString& filePath) const {
    return !getBoxIdForItem(filePath).isEmpty();
}

QString ListView::getBoxIdForItem(const QString& filePath) const {
    if (filePath.isEmpty()) return QString();
    
    QString target = QDir::toNativeSeparators(filePath).toLower();
    while (target.endsWith('\\')) target.chop(1);

    for (auto* box : m_boxes) {
        if (!box) continue;
        for (auto* item : box->items()) {
            if (item) {
                QString itemPath = QDir::toNativeSeparators(item->filePath()).toLower();
                while (itemPath.endsWith('\\')) itemPath.chop(1);
                if (itemPath == target) {
                    return box->boxId();
                }
            }
        }
    }
    return QString();
}

void ListView::setShortcutArrowVisible(bool visible) {
    m_iconShowRule.showShortCutArrow = visible;
    
    // Immediately refresh all items to show/hide the arrow
    for (auto* item : m_allItems) {
        if (item) item->initData();
    }
    
    requestSaveBoxes();
}

void ListView::loadPersistentLayout() {
    for (auto* item : m_items) {
        if (!item) continue;

        // Ensure item is initialized even if position is missing
        item->setItemSize(m_iconWidth, m_iconHeight);
        item->initData();

        QString normPath = QDir::toNativeSeparators(item->filePath()).toLower();
        while (normPath.endsWith('\\')) normPath.chop(1);

        if (m_savedIconPositions.contains(normPath)) {
            DesktopIconInfo info = m_savedIconPositions[normPath];
            item->move(info.x, info.y);
            item->updateLocationInfo(QPoint(info.x, info.y));
        } else {
            // Check if it already has a perfectly valid compacted coordinate from initialize()
            QPoint p = item->getLocationInfo();
            if (p != QPoint(0, 0)) {
                item->move(p);
            } else {
                // Truly homeless icon: assign a fresh spot
                p = generateItemPosition();
                item->move(p);
                item->updateLocationInfo(p);
            }
        }
    }
}
void ListView::setPersistenceEnabled(bool enabled) {
    if (m_enablePersistence == enabled) return;
    m_enablePersistence = enabled;
    
    if (m_enablePersistence) {
        loadBoxes(false); // Don't let the file turn us back off!
        loadIconLayer();
    } else {
        for (auto* box : m_boxes) {
            if (!box) continue;
            QVector<ListItem*> boxItems = box->items();
            for (auto* item : boxItems) {
                if (item) {
                    item->setBoxId("");
                    item->setParent(this);
                    item->show();
                    if (!m_items.contains(item)) m_items.push_back(item);
                }
            }
            box->deleteLater();
        }
        m_boxes.clear();
        loadIconLayer();
    }
    
    // Always commit state change to file
    saveBoxes();

    if (!enabled) {
        FluentDialog* dlg = new FluentDialog(nullptr);
        dlg->setDialogTitle("桌面整理");
        dlg->setMessage("是否立即一键整理当前桌面的图标布局？");
        dlg->addActionButton("立即整理", FluentDialog::ActionType::Primary, [this]() {
            this->setPersistenceEnabled(true); // Undo the disable action if they want an immediate organize/save
            this->onMenuRefresh();
        });
        dlg->addActionButton("取消", FluentDialog::ActionType::Close);
        dlg->show();
        dlg->raise();
        dlg->activateWindow();
    }
}

void ListView::updateRightMenuState() {
    if (!rightMenu) return;
    rightMenu->setLargeIconsChecked(m_iconShowRule.viewMode == ViewMode::LargeIcon);
    rightMenu->setMediumIconsChecked(m_iconShowRule.viewMode == ViewMode::MediumIcon);
    rightMenu->setSmallIconsChecked(m_iconShowRule.viewMode == ViewMode::SmallIcon);
    
    rightMenu->setShowDesktopIconsChecked(m_iconShowRule.showDesktopIcons);
    rightMenu->setAutoArrangeIconsChecked(m_iconShowRule.autoArrange);
    rightMenu->setAlignIconsToGridChecked(m_iconShowRule.gridAlignment);
    rightMenu->setDoubleClickedHideIconsChecked(m_iconShowRule.doubleClickHideIcons);
    
    if (m_iconShowRule.sortType == SortOrderType::Default) {
        rightMenu->setSortByNameChecked(false);
        rightMenu->setSortBySizeChecked(false);
        rightMenu->setSortByTypeChecked(false);
        rightMenu->setSortByDateChecked(false);
    } else {
        rightMenu->setSortByNameChecked(m_iconShowRule.sortType == SortOrderType::SortByName_ASC || m_iconShowRule.sortType == SortOrderType::SortByName_DESC);
        rightMenu->setSortBySizeChecked(m_iconShowRule.sortType == SortOrderType::SortBySize_ASC || m_iconShowRule.sortType == SortOrderType::SortBySize_DESC);
        rightMenu->setSortByTypeChecked(m_iconShowRule.sortType == SortOrderType::SortByType_ASC || m_iconShowRule.sortType == SortOrderType::SortByType_DESC);
        rightMenu->setSortByDateChecked(m_iconShowRule.sortType == SortOrderType::SortByDateModified_ASC || m_iconShowRule.sortType == SortOrderType::SortByDateModified_DESC);
    }
}
