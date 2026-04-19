#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDataStream>
#include <QFile>
#include <QSet>
#include <QCoreApplication>
#include <QSettings>
#include "desktop/ListView.h"
#include "desktop/ListViewSelectionController.h"
#include "desktop/ListViewSelectionHelper.h"
#include "desktop/ListViewMappedBoxHelper.h"
#include "models/DesktopQuadTree.h"
#include "ui/SettingUI.h"
#include <QPushButton>
#include <QtMath>
#include <QProcess>
#include <QStandardPaths>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QClipboard>
#include "SystemApi/SystemApi.h"
#include <QDrag>
#include <QMimeData>
#include <QUrl>
#include <QKeyEvent>
#include <QRegularExpression>
#include <QFileInfo>
#include <QDir>
#include <QFileDialog>
#include <QFont>
#include <QFontMetrics>
#include <shobjidl.h>
#include "../OS/SystemApi/resources.h"
#include "ui/Fluent/FluentDialog.h"
#include "ui/Fluent/FluentInfoTip.h"
#include "desktop/ListViewFileOpsHelper.h"
#include "desktop/ListViewSelectionSnapshot.h"
#include "box/LinBox.h"
#include "plugins/PluginManager.h"
#include "services/HotkeyManager.h"
#include "box/BoxManager.h"
#include "desktop/ListViewDebugPanel.h"

namespace {
QString normalizeDesktopPath(const QString& path)
{
    QString normalized = QDir::toNativeSeparators(path);
    while (normalized.endsWith('\\') || normalized.endsWith('/')) {
        normalized.chop(1);
    }
    return normalized;
}

QString normalizeMappedFolderPathForLookup(const QString& path)
{
    QString normalized = QDir::toNativeSeparators(QDir(path).absolutePath());
    if (normalized.length() == 2 && normalized[1] == ':') {
        normalized += '\\';
    } else if (normalized.length() > 3) {
        while (normalized.endsWith('\\') || normalized.endsWith('/')) {
            normalized.chop(1);
        }
    }
    return normalized;
}

int iconCellHeightForTwoTextLines(int iconSide)
{
    QFont labelFont;
    labelFont.setFamily("Microsoft YaHei UI");
    labelFont.setPointSize(9);
    labelFont.setWeight(QFont::Normal);

    const QFontMetrics metrics(labelFont);
    const int twoLineTextHeight = metrics.lineSpacing();
    return iconSide + twoLineTextHeight;
}

quint64 packGridCell(int gridX, int gridY)
{
    return ((quint64)(uint)gridX << 32) | (quint64)(uint)gridY;
}

QPoint pointToGridCell(const QPoint& pos, int marginLeft, int marginTop, int strideX, int strideY)
{
    const int gridX = qMax(0, qRound((double)(pos.x() - marginLeft) / qMax(1, strideX)));
    const int gridY = qMax(0, qRound((double)(pos.y() - marginTop) / qMax(1, strideY)));
    return QPoint(gridX, gridY);
}

QPoint gridCellToPoint(int gridX, int gridY, int marginLeft, int marginTop, int strideX, int strideY)
{
    return QPoint(marginLeft + gridX * strideX, marginTop + gridY * strideY);
}

int maxGridRowsForHeight(int viewHeight, int marginTop, int bottomPadding, int iconHeight, int strideY)
{
    int rows = 0;
    int gridY = 0;
    while (marginTop + gridY * strideY <= viewHeight - bottomPadding - iconHeight) {
        ++rows;
        ++gridY;
    }
    return qMax(1, rows);
}

int maxGridColsForWidth(int viewWidth, int marginLeft, int iconWidth, int strideX)
{
    int cols = 0;
    int gridX = 0;
    while (marginLeft + gridX * strideX <= viewWidth - iconWidth) {
        ++cols;
        ++gridX;
    }
    return qMax(1, cols);
}

QPoint gridIndexToCell(int linearIndex, int maxRows)
{
    const int safeRows = qMax(1, maxRows);
    return QPoint(linearIndex / safeRows, linearIndex % safeRows);
}

int pointToGridLinearIndex(const QPoint& pos, int marginLeft, int marginTop, int strideX, int strideY, int maxRows)
{
    const int safeStrideX = qMax(1, strideX);
    const int safeStrideY = qMax(1, strideY);
    const int safeRows = qMax(1, maxRows);

    int gridX = qMax(0, qFloor(static_cast<double>(pos.x() - marginLeft) / safeStrideX));
    int gridY = qMax(0, qFloor(static_cast<double>(pos.y() - marginTop) / safeStrideY));

    gridX += gridY / safeRows;
    gridY %= safeRows;
    return gridX * safeRows + gridY;
}

bool isItemFromMappedBox(ListItem* item)
{
    if (!item || item->boxId().isEmpty()) {
        return false;
    }

    if (LinBox* sourceBox = BoxManager::instance()->findBoxById(item->boxId())) {
        return sourceBox->isFolderMapped();
    }
    return false;
}

bool draggedItemsContainMappedSource(const QVector<ListItem*>& items)
{
    for (ListItem* item : items) {
        if (isItemFromMappedBox(item)) {
            return true;
        }
    }
    return false;
}

}

// 闈欐€佹垚鍛樺垵濮嬪寲
ListView* ListView::m_instance = nullptr;
int ListItemData::s_targetIconSize = ListView::kMediumIconW;

ListView::ListView(QWidget* parent) : QWidget(parent),
    m_lastRightClickPos(-1, -1)  // 鍒濆鍖栧彸閿偣鍑讳綅缃负鏃犳晥鍊?
{
    setAcceptDrops(true);
    // 璁剧疆妗岄潰鑳屾櫙鍥剧墖
    //SystemParametersInfo(SPI_SETDESKWALLPAPER, 0, (PVOID)imagePath.toStdString().c_str(), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);


    // 璁剧疆鏃犺竟妗嗙獥鍙?
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowSystemMenuHint);

    // 璁剧疆閫忔槑鑳屾櫙
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    // 璁剧疆閫忔槑骞跺厑璁搁€忚繃绐楀彛鐪嬪埌鍚庨潰鐨勫唴瀹?
    //setAttribute(Qt::WA_TransparentForMouseEvents); // 鍏佽鐐瑰嚮閫忚繃绐楀彛
    //setAttribute(Qt::WA_OpaquePaintEvent);  // 鍚敤鑳屾櫙閫忔槑鍖?

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
    
    // 鑾峰彇鐪熷疄鐨勬闈㈣矾寰?
    QString desktopPath;
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &path))) {
        desktopPath = QString::fromWCharArray(path);
        CoTaskMemFree(path);
    } else {
        // 闄嶇骇鏂规锛氫娇鐢≦StandardPaths
        desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    }
    m_desktopDir = desktopPath; // 淇濆瓨鍒版垚鍛樺彉閲?
    
    qDebug() << "鐩戞帶妗岄潰璺緞:" << desktopPath;
    
    // 娉ㄥ唽鑷畾涔夌被鍨嬶紝鍏佽璺ㄧ嚎绋嬮槦鍒椾俊鍙蜂紶閫?
    qRegisterMetaType<FileSystemEventInfo>("FileSystemEventInfo");
    
    // 浣跨敤Lin鐩綍鐩戞帶鍣?
    m_linMonitor = new LinDirectoryMonitor(desktopPath, false, this);
    connect(m_linMonitor, &LinDirectoryMonitor::fileSystemChanged, this, &ListView::onFileSystemChanged);
    m_linMonitor->startListening();

    rightMenu = RightMenu::getInstance();
    rightMenu->initializeCustomMenu();
    rightMenu->setHWND((HWND)this->winId());

    // Set persistence path first so we can load viewMode before creating items
    setFocusPolicy(Qt::StrongFocus);
    m_pendingShellNewTargetTimer = new QTimer(this);
    m_pendingShellNewTargetTimer->setSingleShot(true);
    m_pendingShellNewTargetTimer->setInterval(15000);
    connect(m_pendingShellNewTargetTimer, &QTimer::timeout, this, &ListView::clearPendingShellNewTargetBox);
#ifdef QT_DEBUG
    m_persistencePath = QCoreApplication::applicationDirPath() + "/data.json";
#else
    m_persistencePath = QCoreApplication::applicationDirPath() + "/data.dat";
#endif
    
    // Load viewMode from config BEFORE creating any ListItem objects
    // This ensures ListItemData::s_targetIconSize is set correctly

    this->initialize();
    this->initRightMenuConnections();
    this->syncViewModeSizes(); // Call syncViewModeSizes after initialization
    
    // Initialize BoxManager
    BoxManager::instance(this);
    BoxManager::instance()->setPersistencePath(m_persistencePath);
    connect(BoxManager::instance(), &BoxManager::saveRequested, this, &ListView::saveBoxes);
    
    // Defer loading boxes to ensure the event loop is running and windows can be properly shown/mapped
    QTimer::singleShot(100, [this](){
        this->loadBoxes(true);
        const QString configPath = QCoreApplication::applicationDirPath() + "/config.ini";
        QSettings settings(configPath, QSettings::IniFormat);
        const bool isFirstLaunch = settings.value("Firslanuch", true).toBool();
        if (isFirstLaunch) {
            this->setPersistenceEnabled(true); 
            settings.setValue("Firslanuch", false);
            settings.sync();
        }
        this->onMenuRefresh();
        this->raise();

        // Open debug panel on startup
        auto* debugPanel = new ListViewDebugPanel(this);
        debugPanel->show();
    });
}

void ListView::updateSystemIconVisibility(const QString& clsid, bool hidden)
{
    qDebug() << "updateSystemIconVisibility:" << clsid << "->" << (hidden ? "hiding" : "showing");
    
    // 瑙勮寖鍖朇LSID鏍煎紡锛堢Щ闄ゅ彲鑳藉瓨鍦ㄧ殑::鍓嶇紑锛?
    QString normalizedClsid = clsid;
    normalizedClsid.replace("::", "");
    
    if (hidden) {
        // 闇€瑕侀殣钘忓浘鏍囷細浠庢樉绀哄垪琛╩_items涓Щ闄?
        for (auto it = m_items.begin(); it != m_items.end(); ) {
            ListItem* item = *it;
            if (item) {
                QString itemClsid = item->getItemData().getCLSID();
                if (itemClsid.replace("::", "") == normalizedClsid) {
                    qDebug() << "Hiding item:" << item->baseName();
                    item->hide(); // 闅愯棌鎺т欢
                    it = m_items.erase(it); // 浠庢樉绀哄垪琛ㄤ腑绉婚櫎
                    // 娉ㄦ剰锛氫笉浠巑_allItems涓Щ闄わ紝鍥犱负瀹冩槸鎬诲垪琛?
                    continue; // 缁х画涓嬩竴涓惊鐜垨鑰呯洿鎺ヨ繑鍥烇紝杩欓噷鐢╟ontinue
                }
            }
            ++it;
        }
    }
    else {
        // 闇€瑕佹樉绀哄浘鏍囷細妫€鏌ユ槸鍚﹀凡鍦ㄦ樉绀哄垪琛ㄤ腑
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
            // 浠庨殣钘忓垪琛ㄦ垨鎬诲垪琛ㄤ腑鏌ユ壘骞舵坊鍔犲埌鏄剧ず鍒楄〃
            // 棣栧厛妫€鏌_hiddenIcons锛堜笓闂ㄥ瓨鏀剧郴缁熷浘鏍囩殑澶囩敤鍒楄〃锛?
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
            
            // 濡傛灉鍦╤iddenIcons娌℃壘鍒帮紝灏濊瘯鍦╝llItems涓壘锛堜綔涓哄厹搴曪級
            if (!found) {
                for (auto* item : m_allItems) {
                    if (item) {
                         QString itemClsid = item->getItemData().getCLSID();
                         if (itemClsid.replace("::", "") == normalizedClsid) {
                            // 纭繚涓嶉噸澶嶆坊鍔?
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
    
    // 閲嶆柊閫氳繃鎺掑簭寮哄埗鍒锋柊椤哄簭锛堢郴缁熷浘鏍囬€氬父鏈夊浐瀹氶『搴忥紝杩欓噷绠€鍗曟寜褰撳墠瑙勫垯閲嶆帓锛?
    // 濡傛灉闇€瑕佷繚鎸佺壒瀹氶『搴忥紝鍙兘闇€瑕佹洿澶嶆潅鐨勬彃鍏ラ€昏緫
    //sortItems(m_sortType);
    
    // Refresh our local snapshot of all system icon states before re-rendering
    if (monitor) {
        sysIconRegisterInfo = monitor->getCurrentIcons();
    }
    
    // 閲嶆柊甯冨眬鍜岀粯鍒?
    loadIconLayer();
    update();
    repaint();
}
void ListView::startListeningSpecialIcon()
{
    // 寮€濮嬬洃鍚?
    if (monitor->startListening()) {
        sysIconRegisterInfo = monitor->getCurrentIcons();

        /*for (const auto& icon : monitor->getCurrentIcons()) {
            qDebug() << icon.name << ":" << (icon.isHidden ? "hiddening" : "showing");
        }*/

        // 璁剧疆鍥炴敹绔欓殣钘?
        //monitor->setIconVisibility("{645FF040-5081-101B-9F08-00AA002F954E}", true);
    }
    // 杩炴帴淇″彿
    connect(monitor, &DesktopInfoMonitor::iconVisibilityChanged,
        this, &ListView::updateSystemIconVisibility);
}

bool ListView::nativeEvent(const QByteArray& eventType, void* message, long* result) {
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_HOTKEY) {
        handleHotkey(static_cast<int>(msg->wParam));
        return true; // We handled it
    }
    if (msg->message == WM_LINDESK_START_INLINE_RENAME) {
        if (m_selectedItems.size() == 1 && m_selectedItems.first()) {
            RenameManager::instance()->startRename(m_selectedItems.first());
            if (result) {
                *result = 0;
            }
            return true;
        }
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
    
    // Clean up drag tip
    if (m_dragTip) {
        delete m_dragTip;
        m_dragTip = nullptr;
    }

    if (m_infoTip) {
        delete m_infoTip;
        m_infoTip = nullptr;
    }
    
    // Clean up ghost widgets
    for (auto& ghost : m_ghosts) {
        if (ghost.widget) {
            ghost.widget->deleteLater();
        }
    }
    m_ghosts.clear();
    
    // Delete all ListItems in m_allItems
    qDeleteAll(m_allItems);
    m_allItems.clear();
    m_items.clear();
    m_selectedItems.clear();
}
ListItem* ListView::selectItemByPath(const QString path)
{
    if (path.isEmpty()) return nullptr;
    QString target = QDir::toNativeSeparators(path);
    while (target.endsWith('\\')) target.chop(1);

    for (auto* item : m_allItems) {
        if (item) {
            QString tmp = item->normalizedPath();
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
    
    // Also clear selection state for items inside boxes
    BoxManager* bm = BoxManager::instance();
    if (bm) {
        for (const auto& box : bm->allBoxes()) {
            if (box) {
                for (auto* item : box->items()) {
                    if (item && item->isSelected) {
                        item->isSelected = false;
                        item->update();
                    }
                }
            }
        }
    }
}

void ListView::clearSelectionState()
{
    ListViewSelectionController::clearSelection(*this);
}

void ListView::setSelectedItems(const QVector<ListItem*>& items, bool expandSingle)
{
    applySelectionState(items, items.isEmpty() ? nullptr : items.last(), expandSingle);
}

QVector<ListItem*> ListView::selectableItemsForHost(LinBox* box) const
{
    return ListViewSelectionHelper::selectableItemsForHost(*this, box);
}

void ListView::applySelectionState(const QVector<ListItem*>& items, ListItem* focusedItem, bool expandSingle)
{
    ListViewSelectionController::applySelection(*this, items, focusedItem, expandSingle);
}

void ListView::setFocusedItem(ListItem* item)
{
    if (m_focusedItem) {
        m_focusedItem->setFocused(false);
    }
    m_focusedItem = item;
    if (m_focusedItem) {
        m_focusedItem->setFocused(true);
        m_focusedItem->raise();
    }
}

void ListView::toggleItemSelection(ListItem* item)
{
    ListViewSelectionController::toggleSelection(*this, item);
}

void ListView::selectRangeToItem(ListItem* item, LinBox* box)
{
    ListViewSelectionController::selectRange(*this, item, box);
}

void ListView::moveFocusInHost(LinBox* box, FocusMoveDirection direction, bool extendSelection, bool focusOnly)
{
    QVector<ListItem*> orderedItems = selectableItemsForHost(box);
    if (orderedItems.isEmpty()) {
        return;
    }

    ListItem* targetItem = ListViewSelectionHelper::findDirectionalFocusCandidate(
        orderedItems,
        m_focusedItem,
        direction);

    if (!targetItem && m_focusedItem) {
        const int currentIndex = orderedItems.indexOf(m_focusedItem);
        if (currentIndex >= 0) {
            const int delta = (direction == FocusMoveDirection::Left || direction == FocusMoveDirection::Up) ? -1 : 1;
            const int fallbackIndex = qBound(0, currentIndex + delta, orderedItems.size() - 1);
            targetItem = orderedItems[fallbackIndex];
        }
    }

    if (!targetItem) {
        return;
    }

    if (focusOnly) {
        setFocusedItem(targetItem);
        return;
    }

    if (extendSelection) {
        if (!m_anchorItem) {
            m_anchorItem = m_focusedItem ? m_focusedItem.data() : targetItem;
        }
        selectRangeToItem(targetItem, box);
        return;
    }

    applySelectionState({targetItem}, targetItem, true);
    m_anchorItem = targetItem;
}

void ListView::clearCutMarks()
{
    ListViewSelectionController::clearCutMarks(*this);
}

void ListView::applyCutMarks(const QVector<ListItem*>& items)
{
    ListViewSelectionController::applyCutMarks(*this, items);
}

QStringList ListView::selectedItemPaths() const
{
    QStringList paths;
    for (ListItem* item : m_selectedItems) {
        if (item && !item->normalizedPath().isEmpty()) {
            paths.append(item->normalizedPath());
        }
    }
    return paths;
}

QString ListView::focusedItemPath() const
{
    return m_focusedItem ? m_focusedItem->normalizedPath() : QString();
}

QString ListView::anchorItemPath() const
{
    return m_anchorItem ? m_anchorItem->normalizedPath() : QString();
}

void ListView::restoreSelectionStateByPaths(const QStringList& selectedPaths, const QString& focusedPath, const QString& anchorPath, bool expandSingle)
{
    QVector<ListItem*> restoredItems;
    restoredItems.reserve(selectedPaths.size());
    for (const QString& path : selectedPaths) {
        if (ListItem* item = selectItemByPath(path)) {
            restoredItems.append(item);
        }
    }

    ListItem* focused = focusedPath.isEmpty() ? nullptr : selectItemByPath(focusedPath);
    applySelectionState(restoredItems, focused, expandSingle);
    m_anchorItem = anchorPath.isEmpty() ? nullptr : selectItemByPath(anchorPath);
}

void ListView::writeSelectionToClipboard(bool cutOperation)
{
    if (m_selectedItems.isEmpty()) {
        return;
    }

    QList<QUrl> urls;
    for (ListItem* item : m_selectedItems) {
        if (!item) {
            continue;
        }
        if (item->type() == ObjectType::SpecialFolder) {
            continue;
        }
        const QString filePath = item->filePath();
        if (filePath.isEmpty() || filePath.startsWith("::")) {
            continue;
        }
        urls.append(QUrl::fromLocalFile(filePath));
    }

    if (urls.isEmpty()) {
        return;
    }

    ListViewFileOpsHelper::writeUrlsToClipboard(urls, cutOperation);
    if (cutOperation) {
        applyCutMarks(m_selectedItems);
    } else {
        clearCutMarks();
    }
}

void ListView::armPendingShellNewTargetBox(LinBox* box)
{
    m_pendingShellNewTargetBox = box;
    if (m_pendingShellNewTargetTimer) {
        m_pendingShellNewTargetTimer->start();
    }
}

void ListView::clearPendingShellNewTargetBox()
{
    m_pendingShellNewTargetBox.clear();
    if (m_pendingShellNewTargetTimer) {
        m_pendingShellNewTargetTimer->stop();
    }
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
    const QVector<ListItem*> candidates = queryItemsInRect(rect);
    QSet<ListItem*> nextSelection;

    for (auto* item : candidates) {
        if (!item) {
            continue;
        }
        const QRect itemRect = item->getChooseItemRect();
        if (itemRect.intersects(rect)) {
            nextSelection.insert(item);
        }
    }

    QSet<ListItem*> previousSelection;
    for (auto* item : m_selectedItems) {
        if (item) {
            previousSelection.insert(item);
        }
    }

    for (auto* item : m_selectedItems) {
        if (item && !nextSelection.contains(item)) {
            item->unCheckSelf();
        }
    }

    QVector<ListItem*> updatedSelection;
    updatedSelection.reserve(nextSelection.size());

    for (auto* item : candidates) {
        if (!item || !nextSelection.contains(item)) {
            continue;
        }
        if (!previousSelection.contains(item)) {
            item->checkSelf();
        }
        updatedSelection.append(item);
    }

    m_selectedItems = updatedSelection;
}

void ListView::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 缁樺埗鍨傜洿鏉＄汗
    if (0) {
        int stripeWidth = 50; // 鏉＄汗瀹藉害
        for (int x = 0; x < width(); x += stripeWidth * 2) {
            // 绗竴绉嶉鑹?
            painter.fillRect(x, 0, stripeWidth, height(), QColor(60, 200, 171));
            // 绗簩绉嶉鑹诧紙鎴栭€忔槑锛?
            painter.fillRect(x + stripeWidth, 0, stripeWidth, height(), QColor(255, 255, 255));
        }
    }
    else {
        //缁樺埗涓€涓儗鏅壊
        //painter.fillRect(rect(), QColor(60, 200, 171)); // 娴呰摑鑹茶儗鏅?
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
        
        // 濡傛灉鍙抽敭鐐瑰嚮浣嶇疆涓嶅湪浠讳綍鍥炬爣鍖哄煙鍐咃紝娓呴櫎宸叉湁閫変腑
        clearCheckedItem();
        // 鏄剧ず妗岄潰鐨勫彸閿彍鍗?
        requestSystemRightMenu(ObjectType::BackGround, rightClickPoint, m_selectedItems);
    }
}

void ListView::mousePressEvent(QMouseEvent* event)
{
    RenameManager::instance()->cancelRename();
    beginBackgroundSelection(event->pos());
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

    if (m_isSelecting) {
        updateBackgroundSelection(event->pos());
        event->ignore();
        return;
    }

    // draw mouse hover effect
    if (!this->m_isSelecting) {
        auto currentPoint = event->pos();

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
        endBackgroundSelection();
    }
    event->ignore();
}

void ListView::beginBackgroundSelection(const QPoint& startPos)
{
    clearCheckedItem();
    setFocus();
    rebuildSpatialIndex();

    m_isSelecting = true;
    m_startPos = startPos;
    m_selectionStart = startPos;
    m_selectionRect = QRect(startPos, QSize(0, 0));
}

bool ListView::updateBackgroundSelection(const QPoint& currentPos, QRect* selectionRect)
{
    if (!m_isSelecting) {
        return false;
    }

    const QRect rect = QRect(m_selectionStart, currentPos).normalized();
    const bool shouldShowSelection = rect.width() > 3 || rect.height() > 3;
    m_selectionRect = rect;

    if (shouldShowSelection) {
        refreshCheckedItem(rect);
    } else {
        clearCheckedItem();
    }

    if (selectionRect) {
        *selectionRect = rect;
    }

    return shouldShowSelection;
}

void ListView::endBackgroundSelection()
{
    m_isSelecting = false;
    m_startPos = QPoint();
    m_selectionStart = QPoint();
    m_selectionRect = QRect();
}

void ListView::rebuildSpatialIndex()
{
    if (!m_spatialIndex) {
        m_spatialIndex = std::make_unique<DesktopQuadTree>(rect());
    }
    m_spatialIndex->setBounds(rect());
    m_spatialIndex->rebuild(m_items);
}

QVector<ListItem*> ListView::queryItemsInRect(const QRect& rect) const
{
    if (!m_spatialIndex) {
        QVector<ListItem*> fallback;
        for (auto* item : m_items) {
            if (item && item->isVisible() && item->boxId().isEmpty()) {
                fallback.append(item);
            }
        }
        return fallback;
    }

    return m_spatialIndex->query(rect);
}

void ListView::dragEnterEvent(QDragEnterEvent* event)
{
    RenameManager::instance()->cancelRename();
    if (event->mimeData()->hasUrls()) {
        if (event->source() != this) {
            const QStringList draggedPaths = ListViewFileOpsHelper::draggedLocalFilePaths(event->mimeData());
            if (shouldCreateShortcutForExternalDrop(draggedPaths)) {
                event->setDropAction(Qt::LinkAction);
            } else if (event->keyboardModifiers() & Qt::ControlModifier) {
                event->setDropAction(Qt::CopyAction);
            } else {
                event->setDropAction(Qt::MoveAction);
            }
            event->accept();
            return;
        }
        if (event->keyboardModifiers() & Qt::ControlModifier) {
            event->setDropAction(Qt::CopyAction);
        } else {
            event->setDropAction(Qt::MoveAction);
        }
        event->accept();
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
    const QStringList draggedPaths = ListViewFileOpsHelper::draggedLocalFilePaths(event->mimeData());
    const bool preferShortcutForExternal = (event->source() != this) && shouldCreateShortcutForExternalDrop(draggedPaths);
    if (preferShortcutForExternal) {
        event->setDropAction(Qt::LinkAction);
    } else if (event->keyboardModifiers() & Qt::ControlModifier) {
        event->setDropAction(Qt::CopyAction);
    } else {
        event->setDropAction(Qt::MoveAction);
    }
    event->accept();

    const bool isInternalDrag = (event->source() == this);

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
        if (isInternalDrag && m_draggedItem) {
            QString draggedClsid = m_draggedItem->getItemData().getCLSID();
            bool isDraggedPC = (draggedClsid == APP_MY_COMPUTER_CLS);
            bool isDraggedNetwork = (draggedClsid == APP_NETWORK_CLS);
            bool isDraggedControlPanel = (draggedClsid == APP_CONTROL_PANEL_CLS);
            bool isDraggedControlPanel1 = (draggedClsid == APP_CONTROL_PANEL1_CLS);
            bool isDraggedRecycleBin = (draggedClsid == APP_RECYCLE_BIN_CLS && m_draggedItem->filePath() == "::{645FF040-5081-101B-9F08-00AA002F954E}");
            bool isLinux = (draggedClsid == APP_LINUX_CLS);
            bool isDraggedSystemIcon = isDraggedPC || isDraggedNetwork || isDraggedControlPanel || isDraggedControlPanel1 || isDraggedRecycleBin || isLinux;

            if (isDraggedSystemIcon) {
                ensureDragTip();
                m_dragTip->setText("<b style='color:#ff4d4d'>Operation Forbidden</b>");
                m_dragTip->adjustSize();
                m_dragTip->move(event->pos() + QPoint(24, 24));
                m_dragTip->show();
                m_dragTip->raise();
                event->setDropAction(Qt::IgnoreAction);
                event->accept();
                return;
            }
        }

        ensureDragTip();

        QString clsid = hitItem->getItemData().getCLSID();
        bool isRecycleBin = (clsid == APP_RECYCLE_BIN_CLS);
        bool isPC = (clsid == APP_MY_COMPUTER_CLS);
        bool isNetwork = (clsid == APP_NETWORK_CLS);
        bool isControlPanel = (clsid == APP_CONTROL_PANEL_CLS);
        bool isFolder = (hitItem->type() == ObjectType::Folder_ObjectType ||
                         hitItem->type() == ObjectType::SpecialFolder ||
                         hitItem->type() == ObjectType::DiskLetter);

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
        QString highlightColor = "#70a1ff";
        const bool isDraggedDiskLetter = (m_draggedItem && m_draggedItem->type() == ObjectType::DiskLetter);
        QString htmlText;

        if (isRecycleBin) {
            actionKeyword = "Move to";
            highlightColor = "#ff4d4d";
            htmlText = QString("<b style='color:%1'>%2</b> <span>%3</span>")
                .arg(highlightColor, actionKeyword, hitItem->baseName());
        } else if (isFolder && preferShortcutForExternal) {
            actionKeyword = "Create shortcut in";
            htmlText = QString("<b style='color:%1'>%2</b> <span>%3</span>")
                .arg(highlightColor, actionKeyword, hitItem->baseName());
        } else if (isFolder && isDraggedDiskLetter) {
            htmlText = QString("<b style='color:%1'>鍦?/b> <span>%2</span> <b style='color:%1'>閲屽垱寤洪摼鎺?/b>")
                .arg(highlightColor, hitItem->baseName());
        } else if (isFolder) {
            actionKeyword = isCtrl ? "Copy to" : "Move to";
            htmlText = QString("<b style='color:%1'>%2</b> <span>%3</span>")
                .arg(highlightColor, actionKeyword, hitItem->baseName());
        } else {
            actionKeyword = "Open with";
            htmlText = QString("<b style='color:%1'>%2</b> <span>%3</span>")
                .arg(highlightColor, actionKeyword, hitItem->baseName());
        }

        m_dragTip->setText(htmlText);
        m_dragTip->adjustSize();
        m_dragTip->move(event->pos() + QPoint(24, 24));
        m_dragTip->show();
        m_dragTip->raise();
        event->accept();
    } else {
        if (isCtrl) {
            ensureDragTip();
            const QString htmlText = QString("<b style='color:#70a1ff'>Copy to</b> <span>Desktop</span>");
            m_dragTip->setText(htmlText);
            m_dragTip->adjustSize();
            m_dragTip->move(event->pos() + QPoint(24, 24));
            m_dragTip->show();
            m_dragTip->raise();
        } else if (m_dragTip) {
            m_dragTip->hide();
        }
    }
}

void ListView::dropEvent(QDropEvent* event)
{
    RenameManager::instance()->cancelRename();
    if (m_dragTip) m_dragTip->hide();

    ListItem* hitItem = nullptr;
    for (auto* item : m_items) {
        if (item && item->isVisible() && item->geometry().contains(event->pos()) && !m_selectedItems.contains(item)) {
            hitItem = item;
            break;
        }
    }

    const QMimeData* mime = event->mimeData();
    if (!mime->hasUrls()) return;
    const QStringList draggedPaths = ListViewFileOpsHelper::draggedLocalFilePaths(mime);

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

        for (const QString& localPath : draggedPaths) {
            if (!localPath.isEmpty()) {
                args << "\"" + localPath + "\"";
                srcPaths.push_back(localPath.toStdWString());
            }
        }
        
        // 鏍规嵁閿洏淇グ绗﹀垽鏂槸澶嶅埗杩樻槸绉诲姩锛圕trl=澶嶅埗, 鏃犱慨楗扮=绉诲姩锛?
        bool isCopyAction = (event->keyboardModifiers() & Qt::ControlModifier);
        const bool preferShortcutForExternal = (event->source() != this) && shouldCreateShortcutForExternalDrop(draggedPaths);

        if (srcPaths.empty()) return;

        bool isRecycleBin = (hitItem->getItemData().getCLSID() == APP_RECYCLE_BIN_CLS);

        if (isRecycleBin) {
            // Move to Recycle Bin
            CSystemApi::DeleteFiles(srcPaths, true, true); // true = recycle, true = confirm
            event->accept();
            return;
        } else if (hitItem->type() == ObjectType::Folder_ObjectType ||
                   hitItem->type() == ObjectType::SpecialFolder ||
                   hitItem->type() == ObjectType::DiskLetter) {
            // 鏍规嵁閿洏淇グ绗﹂€夋嫨澶嶅埗鎴栫Щ鍔ㄦ枃浠跺埌鐩爣鏂囦欢澶?
            if (preferShortcutForExternal) {
                if (createShortcutsInFolder(draggedPaths, targetPath)) {
                    event->setDropAction(Qt::LinkAction);
                    event->accept();
                } else {
                    event->ignore();
                }
                return;
            }
            if (isCopyAction) {
                CSystemApi::CopyFiles(srcPaths, targetPath.toStdWString());
            } else {
                CSystemApi::MoveFiles(srcPaths, targetPath.toStdWString());
            }
            event->accept();
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

    if (draggedPaths.isEmpty()) return;

    // Handle dropping onto the desktop background (no hitItem)
    if (!hitItem) {
        // 鏍规嵁閿洏淇グ绗﹀垽鏂槸澶嶅埗杩樻槸绉诲姩锛圕trl=澶嶅埗, 鏃犱慨楗扮=绉诲姩锛?
        bool isCopyAction = (event->keyboardModifiers() & Qt::ControlModifier);
        
        // 澶栭儴鎷栨斁鎴栧己鍒跺鍒?
        if (event->source() != this || isCopyAction) {
            if (!m_iconShowRule.autoArrange) {
                for (const QString& localPath : draggedPaths) {
                    if (localPath.isEmpty()) {
                        continue;
                    }
                    const QString reservedPath = ListViewFileOpsHelper::uniquePasteDestinationPath(localPath, m_desktopDir);
                    reserveCreatedItemPosition(reservedPath, event->pos());
                }
            }

            std::vector<std::wstring> srcPaths;
            for (const QString& localPath : draggedPaths) {
                if (!localPath.isEmpty()) srcPaths.push_back(localPath.toStdWString());
            }

            if (!srcPaths.empty()) {
                // 鏍规嵁閿洏淇グ绗﹂€夋嫨澶嶅埗鎴栫Щ鍔?
                bool result;
                if (isCopyAction) {
                    result = ListViewFileOpsHelper::copyFilesToDirectoryWithRule(draggedPaths, m_desktopDir);
                } else {
                    result = CSystemApi::MoveFiles(srcPaths, QDir::toNativeSeparators(m_desktopDir).toStdWString());
                }
                event->accept();
                return;
            }
        }
    }

    if (event->source() == this) {
        const bool copyMode = (event->keyboardModifiers() & Qt::ControlModifier);
        const bool hasMappedSourceItems = draggedItemsContainMappedSource(m_selectedItems);
        if (hasMappedSourceItems) {
            bool ok = false;
            if (copyMode) {
                ok = ListViewFileOpsHelper::copyFilesToDirectoryWithRule(draggedPaths, m_desktopDir);
            } else {
                std::vector<std::wstring> srcPaths;
                for (const QString& localPath : draggedPaths) {
                    if (!localPath.isEmpty()) {
                        srcPaths.push_back(QDir::toNativeSeparators(localPath).toStdWString());
                    }
                }
                if (!srcPaths.empty()) {
                    ok = CSystemApi::MoveFiles(srcPaths, QDir::toNativeSeparators(m_desktopDir).toStdWString());
                }
            }

            if (ok) {
                clearCheckedItem();
                requestSaveBoxes();
                event->accept();
            } else {
                event->ignore();
            }
            return;
        }

        // --- ???????? ---
        QPoint dropPos = event->pos(); // ????????? dragHotSpot ??????
        
        if (!m_selectedItems.isEmpty()) {
            int strideX = m_iconWidth + m_colSpacing;
            int strideY = m_iconHeight + m_rowSpacing;

            // 1. 璁＄畻榧犳爣浣嶇Щ閲?(releasePos - grabPos)
            QPoint displacement = dropPos - m_startPos;

            // 棰勫厛璁＄畻鎵€鏈夐」鐨勭洰鏍囦綅缃紝骞舵鏌ユ槸鍚﹀彲鏀剧疆
            QMap<ListItem*, QPoint> targetPositions;
            bool canPlaceAll = true;

            for (auto* item : m_selectedItems) {
                if (!item) continue;

                QPoint itemIntendedTopLeft;

                // 2. 鏍规嵁浣嶇Щ璁＄畻鎵€鏈夐」鐨勬柊鈥滄剰鍥锯€濆潗鏍?(闇€瑕佺嫭绔嬪鐞嗚法鍩熸嫋鎷藉潗鏍?
                if (item->parentWidget() != this && item->parentWidget() != nullptr) {
                    // 瀹冪洰鍓嶅睘浜?Box锛屾墍浠?mapToGlobal 鑾峰彇鍏跺疄闄呭叏灞€鍧愭爣
                    QPoint globalInitialTopLeft = item->mapToGlobal(QPoint(0,0));
                    QPoint initialTopLeftOnDesktop = this->mapFromGlobal(globalInitialTopLeft);
                    itemIntendedTopLeft = initialTopLeftOnDesktop + displacement;
                } else {
                    // 鍘熺敓妗岄潰鍥炬爣
                    itemIntendedTopLeft = item->pos() + displacement;
                }

                QPoint targetPos;

                if (m_iconShowRule.gridAlignment) {
                    // Grid Mode: Calculate nearest grid cell
                    int gridX = qRound((double)(itemIntendedTopLeft.x() - m_marginLeft) / strideX);
                    int gridY = qRound((double)(itemIntendedTopLeft.y() - m_marginTop) / strideY);

                    targetPos.setX(m_marginLeft + gridX * strideX);
                    targetPos.setY(m_marginTop + gridY * strideY);

                    // 纰版挒妫€娴嬶細妫€鏌ヨ缃戞牸鏄惁宸茶鈥滈潪褰撳墠閫変腑鈥濋」鍗犵敤 (浠呭湪缃戞牸妯″紡涓嬪己鍒?
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

                // 杈圭晫妫€鏌?(鏃犺鏄摢绉嶆ā寮忛兘淇濈暀锛岄槻姝㈠浘鏍囬鍑哄睆骞?
                targetPos.setX(qMax(m_marginLeft, qMin(targetPos.x(), width() - m_iconWidth)));
                targetPos.setY(qMax(m_marginTop, qMin(targetPos.y(), height() - m_iconHeight)));

                if (!canPlaceAll) break;
                targetPositions.insert(item, targetPos);
            }

            // 濡傛灉鍏ㄩ儴妫€鏌ラ€氳繃锛屾墽琛岀Щ鍔?
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
            // 淇濆瓨鍥炬爣浣嶇疆鍒伴厤缃枃浠?
            requestSaveBoxes();
        }
        clearCheckedItem();
    } else {
        // --- 澶栭儴鎷栧叆閫昏緫 ---
        QString desktopDir = m_desktopDir;
        std::vector<std::wstring> srcPaths;
        for (const QString& local : draggedPaths) {
            if (!local.isEmpty() && QDir::toNativeSeparators(local) != QDir::toNativeSeparators(desktopDir)) {
                srcPaths.push_back(QDir::toNativeSeparators(local).toStdWString());
            }
        }
        
        if (!srcPaths.empty()) {
            qDebug() << "External drop into Desktop:" << desktopDir;
            // 璋冪敤 Shell API 杩涜鎷疯礉銆?淇濇寔 confirm 涓?true銆?
            CSystemApi::CopyFiles(srcPaths, desktopDir.toStdWString());
        }
    }
    if (m_dragTip) m_dragTip->hide();
    event->acceptProposedAction();
}

void ListView::mouseDoubleClickEvent(QMouseEvent* event)
{
    RenameManager::instance()->cancelRename();
    if (event->button() == Qt::LeftButton) {
        // ListItem handles item double click itself and emits itemDoubleClicked.
        // Only keep the desktop-background double click behavior here.
        const bool hitVisibleItem = std::any_of(m_items.begin(), m_items.end(), [event](ListItem* item) {
            return item && item->isVisible() && item->geometry().contains(event->pos());
        });

        if (!hitVisibleItem && m_iconShowRule.doubleClickHideIcons) {
            onMenuShowDesktopIcons();
            event->accept();
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

void ListView::keyPressEvent(QKeyEvent* event)
{
    const bool extendSelection = event->modifiers() & Qt::ShiftModifier;
    const bool ctrlPressed = event->modifiers() & Qt::ControlModifier;
    const bool shortcutPaste = ctrlPressed && (event->modifiers() & Qt::ShiftModifier) && event->key() == Qt::Key_V;

    if (event->matches(QKeySequence::SelectAll)) {
        applySelectionState(selectableItemsForHost(nullptr), m_focusedItem, false);
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Copy)) {
        writeSelectionToClipboard(false);
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Cut)) {
        writeSelectionToClipboard(true);
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Paste)) {
        clearCutMarks();
        onMenuPaste();
        event->accept();
        return;
    }
    if (shortcutPaste) {
        onMenuPasteShortcut();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        clearSelectionState();
        RenameManager::instance()->cancelRename();
        event->accept();
        return;
    }
    if ((event->modifiers() & Qt::AltModifier) && event->key() == Qt::Key_Return) {
        ListItem* target = m_focusedItem ? m_focusedItem.data() : (m_selectedItems.isEmpty() ? nullptr : m_selectedItems.first());
        if (target && !target->filePath().isEmpty()) {
            CSystemApi::File_RightMenuOperations(reinterpret_cast<const TCHAR*>(QDir::toNativeSeparators(target->filePath()).utf16()), Z_PROPERTIES);
            event->accept();
            return;
        }
    }
    if (ctrlPressed && event->key() == Qt::Key_Space) {
        if (m_focusedItem) {
            toggleItemSelection(m_focusedItem);
            if (!m_anchorItem) {
                m_anchorItem = m_focusedItem;
            }
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        ListItem* target = m_focusedItem ? m_focusedItem.data() : (m_selectedItems.isEmpty() ? nullptr : m_selectedItems.first());
        if (target) {
            clearCutMarks();
            target->openItem();
            event->accept();
            return;
        }
    }
    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Up) {
        moveFocusInHost(nullptr,
            event->key() == Qt::Key_Left ? FocusMoveDirection::Left : FocusMoveDirection::Up,
            extendSelection,
            ctrlPressed);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Right || event->key() == Qt::Key_Down) {
        moveFocusInHost(nullptr,
            event->key() == Qt::Key_Right ? FocusMoveDirection::Right : FocusMoveDirection::Down,
            extendSelection,
            ctrlPressed);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_F2) {
        if (m_selectedItems.size() == 1) {
            RenameManager::instance()->startRename(m_selectedItems.first());
            return;
        }
    }
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

void ListView::initialize()
{
    // === 鍚屾 Windows 妗岄潰鎺掑垪璁剧疆 ===
    bool osAutoArrange = false;
    bool osSnapToGrid = false;
    CSystemApi::GetDesktopConfigFlags(osAutoArrange, osSnapToGrid);
    m_iconShowRule.autoArrange = osAutoArrange;
    m_iconShowRule.gridAlignment = osSnapToGrid;
    syncViewModeSizes(); // Sync initial sizes to member variables
    qDebug() << "Synced Windows Desktop Layout Rules: AutoArrange=" << osAutoArrange << ", SnapToGrid=" << osSnapToGrid;
    // ===================================

    QVector<ListItem*> listItems;
    QVector<ListItem*> itemsWithoutNativePos;
    vector<ListItemData> allDatas = DesktopItemLoader().loadAllItems();

    m_hiddenIcons.clear();
    if (monitor) {
        sysIconRegisterInfo = monitor->getCurrentIcons();
    }

    auto normalizeClsid = [](QString clsid) {
        return clsid.replace("::", "").replace("{", "").replace("}", "").toLower();
    };

    for (auto it = allDatas.begin(); it != allDatas.end(); it++)
    {
        ListItem* listItem = new ListItem(this);
        listItem->setParent(this);
        listItem->setItemData(std::move(*it));
        listItem->setObjectName(QUuid::createUuid().toString(QUuid::WithoutBraces));
        
        QPoint nativePos(it->itemData().position.x, it->itemData().position.y);
        qDebug() << "Native position for" << it->itemData().name.c_str() << ":" << nativePos;

        if (nativePos.x() != 0 || nativePos.y() != 0) {
            listItem->updateLocationInfo(nativePos);
            listItem->move(nativePos);
        }
        setupItemConnections(listItem);
 
        const QString boxId = getBoxIdForItem(listItem->filePath());
        if (!boxId.isEmpty()) {
            listItem->setBoxId(boxId);
        }

        const bool isSpecial = (it->type() == ObjectType::SpecialFolder) ||
                               SystemFolderCLSIDs.contains(listItem->getItemData().getCLSID()) ||
                               listItem->getItemData().fullName() == "desktop.ini" ||
                               listItem->filePath().startsWith("::");
        const bool isOsHidden = listItem->fileInfo().isHidden();
        const bool isAppHiddenShellFolder = DesktopItemLoader::shouldTreatAsHiddenShellFolder(listItem->getItemData());

        bool isVisibleSpecial = false;
        if (isSpecial) {
            const QString itemClsid = normalizeClsid(listItem->getItemData().getCLSID());
            for (const auto& info : sysIconRegisterInfo) {
                if (normalizeClsid(info.clsid) == itemClsid) {
                    isVisibleSpecial = !info.isHidden;
                    break;
                }
            }
        }

        const bool shouldShowOnDesktop = !isAppHiddenShellFolder &&
            boxId.isEmpty() &&
            ((isSpecial && isVisibleSpecial) || (!isSpecial && !isOsHidden));

        if (shouldShowOnDesktop) {
            listItems.push_back(listItem);
            if (listItem->type() == ObjectType::DiskLetter) {
                itemsWithoutNativePos.push_back(listItem);
            }
        } else {
            listItem->hide();
            m_hiddenIcons.push_back(listItem);
        }
        m_allItems.push_back(listItem);
    }

    m_items = listItems;

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

    // 3. Split visible items into those with native coordinates and those without.
    QVector<ListItem*> itemsWithNativePos;
    QSet<ListItem*> noNativePosSet(itemsWithoutNativePos.begin(), itemsWithoutNativePos.end());
    for (auto* item : m_items) {
        if (!item) continue;
        if (!noNativePosSet.contains(item)) {
            itemsWithNativePos.push_back(item);
        }
    }

    // 4. Sort only the items with native coordinates to maintain order.
    std::sort(itemsWithNativePos.begin(), itemsWithNativePos.end(), [](ListItem* a, ListItem* b){
        QPoint pa = a->getLocationInfo();
        QPoint pb = b->getLocationInfo();
        if (qAbs(pa.x() - pb.x()) > 10) return pa.x() < pb.x();
        return pa.y() < pb.y();
    });

    // 5. Sequentially assign the tightly packed slots back to items that actually had native positions.
    qDebug() << "=== Compacted Native Layout ===";
    for (int i = 0; i < itemsWithNativePos.size() && i < nativeSlots.size(); ++i) {
        itemsWithNativePos[i]->updateLocationInfo(nativeSlots[i]);
        itemsWithNativePos[i]->move(nativeSlots[i]);
        qDebug() << QString("Item(%1) compacted to Position:(%2, %3) - Name: %4")
                    .arg(i)
                    .arg(nativeSlots[i].x())
                    .arg(nativeSlots[i].y())
                    .arg(itemsWithNativePos[i]->getItemData().fullName());
    }

    // 6. Items without native positions (for example drive letters on Win11) are placed afterwards
    // so they do not consume compacted desktop slots and overlap coordinate-backed items.
    for (auto* item : itemsWithoutNativePos) {
        if (!item || !m_items.contains(item)) continue;
        const QPoint generatedPos = generateItemPosition();
        item->updateLocationInfo(generatedPos);
        item->move(generatedPos);
        qDebug() << QString("Deferred item placed at Position:(%1, %2) - Name: %3")
                    .arg(generatedPos.x())
                    .arg(generatedPos.y())
                    .arg(item->getItemData().fullName());
    }
    qDebug() << "===============================";
}

void ListView::loadIconLayer()
{
    qDebug() << "[ListView] Starting reload. Master list size:" << m_allItems.size();
    const bool useConfigState = m_forceConfigLayout || m_hasLoadedConfigLayout;
    qDebug() << "[ListView] After render filtering, item count in m_items:" << m_items.size();
    if (m_iconShowRule.autoArrange && !useConfigState) {
        qDebug() << "[ListView] Applying auto-layout (Stable Categorized).";
        this->performSort(m_iconShowRule.sortType); // Use non-toggling version for stability
        loadAutoLayout();
    }else if (m_enablePersistence || useConfigState) { // Prefer config-loaded positions when a config layout exists
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
    for (auto& box : BoxManager::instance()->allBoxes()) {
        if (box) {
            box->refreshLayout();
            box->raise();
        }
    }

    BoxManager::instance()->raiseAllBoxes();
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

    rebuildSpatialIndex();
}

void ListView::loadGridLayout()
{
    const int strideX = m_iconWidth + m_colSpacing;
    const int strideY = m_iconHeight + m_rowSpacing;
    const int maxRows = maxGridRowsForHeight(height(), m_marginTop, m_bottomPadding, m_iconHeight, strideY);
    const int maxCols = maxGridColsForWidth(width(), m_marginLeft, m_iconWidth, strideX);
    const int visibleCapacity = qMax(1, maxCols * maxRows);
    QSet<quint64> occupiedSlots;
    int overflowIndex = 0;

    for (auto* item : m_items) {
        item->setItemSize(m_iconWidth, m_iconHeight);
        item->initData();

        QPoint p = item->getLocationInfo(); // Rely strictly on explicitly calculated memory coordinates
        if (p == QPoint(0, 0)) {
            p = generateItemPosition();
            item->move(p);
        }

        if (m_iconShowRule.gridAlignment) {
            const int requestedIndex = pointToGridLinearIndex(p, m_marginLeft, m_marginTop, strideX, strideY, maxRows);
            int gridX = 0;
            int gridY = 0;

            if (occupiedSlots.size() < visibleCapacity) {
                for (int step = 0; step < visibleCapacity; ++step) {
                    const int linearIndex = (requestedIndex + step) % visibleCapacity;
                    const QPoint candidateCell = gridIndexToCell(linearIndex, maxRows);
                    const quint64 slotKey = packGridCell(candidateCell.x(), candidateCell.y());
                    if (!occupiedSlots.contains(slotKey)) {
                        gridX = candidateCell.x();
                        gridY = candidateCell.y();
                        occupiedSlots.insert(slotKey);
                        break;
                    }
                }
            } else {
                const QPoint overflowCell = gridIndexToCell(overflowIndex % visibleCapacity, maxRows);
                gridX = overflowCell.x();
                gridY = overflowCell.y();
                ++overflowIndex;
            }

            const QPoint snappedPoint = gridCellToPoint(gridX, gridY, m_marginLeft, m_marginTop, strideX, strideY);
            const int snappedX = snappedPoint.x();
            const int snappedY = snappedPoint.y();

            if (item->pos() != QPoint(snappedX, snappedY)) {
                item->updateLocationInfo(QPoint(snappedX, snappedY));
                item->move(snappedX, snappedY);
            }
        } else {
            if (item->pos() != p) {
                item->updateLocationInfo(p);
                item->move(p);
            }
        }
    }

    rebuildSpatialIndex();
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
            if (item->type() == ObjectType::DiskLetter || item->fileInfo().isDir()) return 1; // Directory-like items
            
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
    collator.setNumericMode(true);          // 鍚敤鏁板瓧鎺掑簭
    collator.setCaseSensitivity(Qt::CaseInsensitive); // 涓嶅尯鍒嗗ぇ灏忓啓
    collator.setIgnorePunctuation(true);    // 蹇界暐鏍囩偣绗﹀彿
    // 瀵逛簬涓枃鐜锛岃缃湰鍦板寲
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
            // 鍗囧簭鏃讹紝浼樺厛绾у皬鐨勬帓鍦ㄥ墠闈紙1, 2, 3, 100锛?
            // 闄嶅簭鏃讹紝浼樺厛绾уぇ鐨勬帓鍦ㄥ墠闈紙100, 3, 2, 1锛?
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
        // 鐗规畩鏂囦欢澶癸紙绯荤粺鍥炬爣锛夊缁堟帓鍦ㄦ渶鍓嶏紝鍐呴儴椤哄簭淇濇寔涓嶅彉
        bool aIsSpecial = (a->type() == ObjectType::SpecialFolder);
        bool bIsSpecial = (b->type() == ObjectType::SpecialFolder);
        if (aIsSpecial != bIsSpecial)
            return aIsSpecial;

        // 鏂囦欢澶规病鏈?澶у皬"锛岀粺涓€瑙嗕负 -1锛屾帓鍦ㄦ櫘閫氭枃浠跺墠闈紙鍗囧簭锛?
        bool aIsDir = a->fileInfo().isDir();
        bool bIsDir = b->fileInfo().isDir();
        if (aIsDir != bIsDir)
            return ascending ? aIsDir : !aIsDir;

        qint64 sizeA = a->fileInfo().size();
        qint64 sizeB = b->fileInfo().size();
        if (sizeA != sizeB)
            return ascending ? (sizeA < sizeB) : (sizeA > sizeB);

        // 澶у皬鐩稿悓鏃舵寜鍚嶇О鍗囧簭鍏滃簳
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

        // 鎸夋枃浠舵墿灞曞悕鎺掑簭锛涙枃浠跺す鎵╁睍鍚嶈涓虹┖瀛楃涓诧紙鎺掓渶鍓嶏級
        const bool aIsDirLike = (a->type() == ObjectType::DiskLetter) || a->fileInfo().isDir();
        const bool bIsDirLike = (b->type() == ObjectType::DiskLetter) || b->fileInfo().isDir();
        QString extA = aIsDirLike ? QString() : a->fileInfo().suffix().toLower();
        QString extB = bIsDirLike ? QString() : b->fileInfo().suffix().toLower();
        if (extA != extB)
            return ascending ? (extA < extB) : (extA > extB);

        // 鍚岀被鍨嬫椂鎸夊悕绉板崌搴忓厹搴?
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

        // 鏃堕棿鐩稿悓鏃舵寜鍚嶇О鍗囧簭鍏滃簳
        return a->baseName().toLower() < b->baseName().toLower();
    });
}

void ListView::addItem(ListItem* item, bool refresh)
{
    if (!item) return;

    QPoint pos;
    if (!takeReservedCreatedItemPosition(item->filePath(), pos)) {
        pos = generateItemPosition();
    }
    item->setItemSize(m_iconWidth, m_iconHeight);
    item->updateLocationInfo(pos);
    
    setupItemConnections(item);
    
    // 鎵€鏈夋枃浠堕兘鍔犲叆 m_items 鍜?m_allItems
    m_items.push_back(item);
    m_allItems.push_back(item);
    
    item->show();
    rebuildSpatialIndex();
}

void ListView::addItemNoDelete(ListItem* item)
{
    if (!item) return;
    
    // Clear box id - item is returning to desktop
    item->setBoxId(QString());
    
    // Ensure parent is ListView for proper positioning
    item->setParent(this);
    
    // Check if already in m_allItems
    if (!m_allItems.contains(item)) {
        m_allItems.push_back(item);
    }

    m_hiddenIcons.removeOne(item);
    
    // Check if already in m_items
    if (!m_items.contains(item)) {
        QPoint pos = generateItemPosition();
        item->setItemSize(m_iconWidth, m_iconHeight);
        item->updateLocationInfo(pos);
        setupItemConnections(item);
        m_items.push_back(item);
    }
    
    // Ensure item is visible on desktop
    if (m_iconShowRule.showDesktopIcons) {
        item->show();
    }

    rebuildSpatialIndex();
    update();
}

void ListView::removeItem(ListItem* item)
{
    if (item && item->isReady) {
        qDebug() << item->filePath();
        // 浠庢墍鏈夌浉鍏虫暟缁勪腑绉婚櫎item
        m_items.removeOne(item);
        m_allItems.removeOne(item);
        m_selectedItems.removeOne(item);
        
        // 纭繚浠庢墍灞炵殑 Box 涓篃绉婚櫎璇?item (闃叉鎮┖鎸囬拡)
        if (!item->boxId().isEmpty()) {
            LinBox* box = BoxManager::instance()->findBoxById(item->boxId());
            if (box) {
                box->removeListItem(item);
            }
        }

        // 纭繚item琚畬鍏ㄩ殣钘?
        item->setVisible(false);
        item->hide();
        
        // 璁剧疆鐖舵帶浠朵负nullptr锛岀‘淇漣tem涓嶅啀鍙楃埗鎺т欢绠＄悊
        item->setParent(nullptr);
        
        item->isReady = false;
        item->deleteLater();
    }
    
    // 寮哄埗鏇存柊鐣岄潰
    rebuildSpatialIndex();
    update();
    repaint();
    
}

void ListView::removeItemNoDelete(ListItem* item)
{
    if (item && item->isReady) {
        qDebug() << "[removeItemNoDelete]" << item->filePath();
        m_selectedItems.removeOne(item);
        m_items.removeOne(item);
        if (!m_hiddenIcons.contains(item)) {
            m_hiddenIcons.push_back(item);
        }
          
        // Keep the item in global render collections. Box only stores a reference.
        item->setVisible(false);
        item->hide();
    }
    
    // 寮哄埗鏇存柊鐣岄潰
    rebuildSpatialIndex();
    update();
    repaint();
}

ListItem* ListView::findItemByPath(const QString& filePath) const {
    if (filePath.isEmpty()) return nullptr;
    
    QString target = QDir::toNativeSeparators(filePath);
    while (target.endsWith('\\')) target.chop(1);
    
    for (auto* item : m_allItems) {
        if (!item || !item->isReady) continue;
        QString itemPath = QDir::toNativeSeparators(item->filePath());
        while (itemPath.endsWith('\\')) itemPath.chop(1);
        if (itemPath == target) {
            return item;
        }
    }
    return nullptr;
}

QPoint ListView::resolveCreatePosition(const QPoint& requestedPos) const
{
    QPoint pos = requestedPos;
    if (pos == QPoint(-1, -1)) {
        pos = const_cast<ListView*>(this)->generateItemPosition();
    }

    if (m_iconShowRule.gridAlignment) {
        const int strideX = m_iconWidth + m_colSpacing;
        const int strideY = m_iconHeight + m_rowSpacing;
        const int maxRows = maxGridRowsForHeight(height(), m_marginTop, m_bottomPadding, m_iconHeight, strideY);
        const int maxCols = maxGridColsForWidth(width(), m_marginLeft, m_iconWidth, strideX);
        const int visibleCapacity = qMax(1, maxCols * maxRows);
        const int requestedIndex = pointToGridLinearIndex(pos, m_marginLeft, m_marginTop, strideX, strideY, maxRows);
        int gridX = 0;
        int gridY = 0;

        QSet<quint64> occupiedSlots;
        int desktopItemCount = 0;
        for (auto* item : m_items) {
            if (!item || !item->boxId().isEmpty()) continue;
            ++desktopItemCount;
            const QPoint gridCell = pointToGridCell(item->pos(), m_marginLeft, m_marginTop, strideX, strideY);
            occupiedSlots.insert(packGridCell(qBound(0, gridCell.x(), maxCols - 1), qBound(0, gridCell.y(), maxRows - 1)));
        }
        for (auto it = m_pendingCreatedItemPositions.constBegin(); it != m_pendingCreatedItemPositions.constEnd(); ++it) {
            ++desktopItemCount;
            const QPoint gridCell = pointToGridCell(it.value(), m_marginLeft, m_marginTop, strideX, strideY);
            occupiedSlots.insert(packGridCell(qBound(0, gridCell.x(), maxCols - 1), qBound(0, gridCell.y(), maxRows - 1)));
        }

        if (occupiedSlots.size() < visibleCapacity) {
            for (int offset = 0; offset < visibleCapacity; ++offset) {
                const int cellIndex = (requestedIndex + offset) % visibleCapacity;
                const QPoint visibleCell = gridIndexToCell(cellIndex, maxRows);
                const quint64 slotKey = packGridCell(visibleCell.x(), visibleCell.y());
                if (occupiedSlots.contains(slotKey)) {
                    continue;
                }
                gridX = visibleCell.x();
                gridY = visibleCell.y();
                return gridCellToPoint(gridX, gridY, m_marginLeft, m_marginTop, strideX, strideY);
            }
        }

        const QPoint overflowCell = gridIndexToCell(desktopItemCount % visibleCapacity, maxRows);
        return gridCellToPoint(overflowCell.x(), overflowCell.y(), m_marginLeft, m_marginTop, strideX, strideY);
    }

    pos.setX(qMax(m_marginLeft, qMin(pos.x(), width() - m_iconWidth)));
    pos.setY(qMax(m_marginTop, qMin(pos.y(), height() - m_bottomPadding - m_iconHeight)));

    const int step = 16;
    const int minX = m_marginLeft;
    const int minY = m_marginTop;
    const int maxX = qMax(minX, width() - m_iconWidth);
    const int maxY = qMax(minY, height() - m_bottomPadding - m_iconHeight);
    QSet<quint64> visitedPositions;

    while (true) {
        QRect candidateRect(pos, QSize(m_iconWidth, m_iconHeight));
        bool occupied = false;

        for (auto* item : m_items) {
            if (!item || !item->boxId().isEmpty()) continue;
            if (candidateRect.intersects(item->geometry())) {
                occupied = true;
                break;
            }
        }

        if (!occupied) {
            for (auto it = m_pendingCreatedItemPositions.constBegin(); it != m_pendingCreatedItemPositions.constEnd(); ++it) {
                QRect pendingRect(it.value(), QSize(m_iconWidth, m_iconHeight));
                if (candidateRect.intersects(pendingRect)) {
                    occupied = true;
                    break;
                }
            }
        }

        if (!occupied) {
            return pos;
        }

        const quint64 visitKey = packGridCell(pos.x(), pos.y());
        if (visitedPositions.contains(visitKey)) {
            return pos;
        }
        visitedPositions.insert(visitKey);

        int nextX = pos.x();
        int nextY = pos.y() + step;
        if (nextY > maxY) {
            nextY = minY;
            nextX += step;
        }
        if (nextX > maxX) {
            nextX = minX;
            nextY = minY;
        }
        pos = QPoint(nextX, nextY);
    }
}

void ListView::reserveCreatedItemPosition(const QString& filePath, const QPoint& requestedPos)
{
    const QString normalizedPath = normalizeDesktopPath(filePath);
    if (normalizedPath.isEmpty()) return;
    m_pendingCreatedItemPositions[normalizedPath] = resolveCreatePosition(requestedPos);
}

bool ListView::takeReservedCreatedItemPosition(const QString& filePath, QPoint& posOut)
{
    const QString normalizedPath = normalizeDesktopPath(filePath);
    auto it = m_pendingCreatedItemPositions.find(normalizedPath);
    if (it == m_pendingCreatedItemPositions.end()) {
        return false;
    }

    posOut = it.value();
    m_pendingCreatedItemPositions.erase(it);
    return true;
}

bool ListView::handlePendingShellCreatedItem(const QString& newPath)
{
    if (!m_pendingShellNewTargetBox) {
        return false;
    }

    QPointer<LinBox> targetBox = m_pendingShellNewTargetBox;
    clearPendingShellNewTargetBox();
    if (!targetBox) {
        return false;
    }

    if (targetBox->isFolderMapped()) {
        const QString mappedFolderPath = targetBox->mappedFolderPath();
        if (mappedFolderPath.isEmpty() || targetBox->mappedFolderState() != LinBox::MappedFolderState::Normal) {
            return false;
        }

        const QString destinationPath = ListViewFileOpsHelper::uniquePasteDestinationPath(newPath, mappedFolderPath);
        if (!ListViewFileOpsHelper::movePathToExactDestination(newPath, destinationPath)) {
            return false;
        }

        if (ListItem* createdItem = selectItemByPath(newPath)) {
            m_hiddenIcons.removeOne(createdItem);
            m_items.removeOne(createdItem);
            m_allItems.removeOne(createdItem);
            m_selectedItems.removeOne(createdItem);
            createdItem->deleteLater();
        }

        loadMappedFolderItemsForBox(targetBox);
        targetBox->refreshLayout();
        targetBox->raise();
        update();
        return true;
    }

    moveItemToBox(newPath, targetBox, false, -1);
    targetBox->refreshLayout();
    targetBox->raise();
    update();
    return true;
}

/// <summary>
/// 鐢熸垚瑙勫垯
/// 1.unGrid妯″紡
/// UI鍒涘缓Item,鐩存帴鎻愪緵榧犳爣鍙抽敭鍧愭爣
/// 闈濽I鍒涘缓锛屼娇鐢℅rid鍒涘缓瑙勫垯
/// 2.Grid妯″紡
/// 鏌ヨ鏍煎瓙甯冨眬姣忎釜鍖哄煙鏄惁鏈夋枃浠跺瓨鍦紝濡傛灉涓嶅瓨鍦ㄦ绱㈢┖鏍煎瓙锛屼娇鐢ㄧ涓€涓牸瀛愬潗鏍?
/// </summary>
/// <returns></returns>
QPoint ListView::generateItemPosition()
{
    qDebug() << "[generateItemPosition] gridAlignment=" << m_iconShowRule.gridAlignment 
             << "m_lastRightClickPos=" << m_lastRightClickPos;
    
    // 闈濭rid甯冨眬涓旂敱UI鎿嶄綔瑙﹀彂鐨勶紙鍙抽敭鑿滃崟鏂板缓锛夛紝鐩存帴浣跨敤榧犳爣鍙抽敭浣嶇疆
    if (!m_iconShowRule.gridAlignment && m_lastRightClickPos != QPoint(-1, -1)) {
        QPoint pos = m_lastRightClickPos;
        qDebug() << "[generateItemPosition] Using right-click position:" << pos;
        // 閲嶇疆 m_lastRightClickPos锛岄槻姝㈤潪UI鍒涘缓鐨勬枃浠朵篃浣跨敤璇ュ潗鏍?
        m_lastRightClickPos = QPoint(-1, -1);
        return pos;
    }
    
    qDebug() << "[generateItemPosition] Using grid layout position";

    const int strideX = m_iconWidth + m_colSpacing;
    const int strideY = m_iconHeight + m_rowSpacing;
    const int maxRows = maxGridRowsForHeight(height(), m_marginTop, m_bottomPadding, m_iconHeight, strideY);
    const int maxCols = maxGridColsForWidth(width(), m_marginLeft, m_iconWidth, strideX);
    const int visibleCapacity = qMax(1, maxCols * maxRows);
    QSet<quint64> occupiedSlots;
    int desktopItemCount = 0;

    for (auto* item : m_items) {
        if (item && item->boxId().isEmpty() && (item->pos().x() > 0 || item->pos().y() > 0)) {
            ++desktopItemCount;
            const QPoint gridCell = pointToGridCell(item->pos(), m_marginLeft, m_marginTop, strideX, strideY);
            occupiedSlots.insert(packGridCell(qBound(0, gridCell.x(), maxCols - 1), qBound(0, gridCell.y(), maxRows - 1)));
        }
    }
    for (auto it = m_pendingCreatedItemPositions.constBegin(); it != m_pendingCreatedItemPositions.constEnd(); ++it) {
        ++desktopItemCount;
        const QPoint gridCell = pointToGridCell(it.value(), m_marginLeft, m_marginTop, strideX, strideY);
        occupiedSlots.insert(packGridCell(qBound(0, gridCell.x(), maxCols - 1), qBound(0, gridCell.y(), maxRows - 1)));
    }

    if (occupiedSlots.size() < visibleCapacity) {
        for (int gridX = 0; gridX < maxCols; ++gridX) {
            for (int gridY = 0; gridY < maxRows; ++gridY) {
                if (occupiedSlots.contains(packGridCell(gridX, gridY))) {
                    continue;
                }

                return gridCellToPoint(gridX, gridY, m_marginLeft, m_marginTop, strideX, strideY);
            }
        }
    }

    const QPoint overflowCell = gridIndexToCell(desktopItemCount % visibleCapacity, maxRows);
    return gridCellToPoint(overflowCell.x(), overflowCell.y(), m_marginLeft, m_marginTop, strideX, strideY);

    return QPoint(0, 0); // 鍏滃簳杩斿洖
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
    connect(menu, &RightMenu::createBoxTriggered, this, &ListView::onMenuCreateBox);
    connect(menu, &RightMenu::mapFolderTriggered, this, &ListView::onMenuMapFolder);
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
        menu->setSortByNameChecked(true); // 榛樿閫変腑鍚嶇О
        break;
    }
}

void ListView::syncViewModeSizes() {
    switch (m_iconShowRule.viewMode) {
        case ViewMode::LargeIcon:
            m_iconWidth  = kLargeIconW;
            m_rowSpacing = kLargeRowSpacing;
            m_colSpacing = kLargeColSpacing;
            ListItemData::s_targetIconSize = kLargeIconW;
            m_iconHeight = iconCellHeightForTwoTextLines(kLargeIconW - 4);
            if (rightMenu) rightMenu->setLargeIconsChecked(true);
            break;
        case ViewMode::MediumIcon:
            m_iconWidth  = kMediumIconW;
            m_iconHeight = iconCellHeightForTwoTextLines(kMediumIconW - 4);
            m_rowSpacing = kMediumRowSpacing;
            m_colSpacing = kMediumColSpacing;
            ListItemData::s_targetIconSize = kMediumIconW;
            if (rightMenu) rightMenu->setMediumIconsChecked(true);
            break;
        case ViewMode::SmallIcon:
            m_iconWidth = kSmallIconW;
            m_iconHeight = iconCellHeightForTwoTextLines(kSmallIconW - 4);
            m_rowSpacing = kSmallRowSpacing;
            m_colSpacing = kSmallColSpacing;
            ListItemData::s_targetIconSize = kSmallIconW;
            if (rightMenu) rightMenu->setSmallIconsChecked(true);
            break;
    }
}

QPoint ListView::remapPositionForViewMetrics(const QPoint& pos, int oldIconWidth, int oldIconHeight, int oldRowSpacing, int oldColSpacing) const
{
    if (pos == QPoint(0, 0)) {
        return pos;
    }

    const int oldStrideX = qMax(1, oldIconWidth + oldColSpacing);
    const int oldStrideY = qMax(1, oldIconHeight + oldRowSpacing);
    const int newStrideX = qMax(1, m_iconWidth + m_colSpacing);
    const int newStrideY = qMax(1, m_iconHeight + m_rowSpacing);

    QPoint mappedPos = pos;
    if (m_iconShowRule.gridAlignment) {
        const QPoint gridCell = pointToGridCell(pos, m_marginLeft, m_marginTop, oldStrideX, oldStrideY);
        mappedPos = gridCellToPoint(gridCell.x(), gridCell.y(), m_marginLeft, m_marginTop, newStrideX, newStrideY);
    } else {
        const double logicalX = static_cast<double>(pos.x() - m_marginLeft) / oldStrideX;
        const double logicalY = static_cast<double>(pos.y() - m_marginTop) / oldStrideY;
        mappedPos.setX(m_marginLeft + qRound(logicalX * newStrideX));
        mappedPos.setY(m_marginTop + qRound(logicalY * newStrideY));
    }

    mappedPos.setX(qMax(m_marginLeft, qMin(mappedPos.x(), width() - m_iconWidth)));
    mappedPos.setY(qMax(m_marginTop, qMin(mappedPos.y(), height() - m_bottomPadding - m_iconHeight)));
    return mappedPos;
}

void ListView::applyViewModeMetricsChange(int oldIconWidth, int oldIconHeight, int oldRowSpacing, int oldColSpacing)
{
    if (oldIconWidth == m_iconWidth &&
        oldIconHeight == m_iconHeight &&
        oldRowSpacing == m_rowSpacing &&
        oldColSpacing == m_colSpacing) {
        return;
    }

    for (ListItem* item : m_allItems) {
        if (!item || !item->boxId().isEmpty()) {
            continue;
        }

        const QPoint currentPos = item->getLocationInfo();
        if (currentPos == QPoint(0, 0)) {
            continue;
        }

        item->updateLocationInfo(remapPositionForViewMetrics(currentPos, oldIconWidth, oldIconHeight, oldRowSpacing, oldColSpacing));
    }

    for (auto it = m_savedIconPositions.begin(); it != m_savedIconPositions.end(); ++it) {
        if (!it.value().boxId.isEmpty()) {
            continue;
        }

        const QPoint currentPos(it.value().x, it.value().y);
        const QPoint mappedPos = remapPositionForViewMetrics(currentPos, oldIconWidth, oldIconHeight, oldRowSpacing, oldColSpacing);
        it.value().x = mappedPos.x();
        it.value().y = mappedPos.y();
    }

    for (auto it = m_pendingCreatedItemPositions.begin(); it != m_pendingCreatedItemPositions.end(); ++it) {
        it.value() = remapPositionForViewMetrics(it.value(), oldIconWidth, oldIconHeight, oldRowSpacing, oldColSpacing);
    }
}

void ListView::setDebugGridSpacing(int rowSpacing, int colSpacing)
{
    m_rowSpacing = rowSpacing;
    m_colSpacing = colSpacing;
    loadIconLayer();
    update();
}

void ListView::onMenuLargeIcons() {
    qDebug() << "ListView: Large Icons triggered";
    if (m_iconShowRule.viewMode == ViewMode::LargeIcon) return;
    const int oldIconWidth = m_iconWidth;
    const int oldIconHeight = m_iconHeight;
    const int oldRowSpacing = m_rowSpacing;
    const int oldColSpacing = m_colSpacing;
    m_iconShowRule.viewMode = ViewMode::LargeIcon;
    syncViewModeSizes();
    applyViewModeMetricsChange(oldIconWidth, oldIconHeight, oldRowSpacing, oldColSpacing);
    loadIconLayer();
    update();
    requestSaveBoxes();
}

void ListView::onMenuMediumIcons() {
    qDebug() << "ListView: Medium Icons triggered";
    if (m_iconShowRule.viewMode == ViewMode::MediumIcon) return;
    const int oldIconWidth = m_iconWidth;
    const int oldIconHeight = m_iconHeight;
    const int oldRowSpacing = m_rowSpacing;
    const int oldColSpacing = m_colSpacing;
    m_iconShowRule.viewMode = ViewMode::MediumIcon;
    syncViewModeSizes();
    applyViewModeMetricsChange(oldIconWidth, oldIconHeight, oldRowSpacing, oldColSpacing);
    loadIconLayer();
    update();
    requestSaveBoxes();
}

void ListView::onMenuSmallIcons() {
    qDebug() << "ListView: Small Icons triggered";
    if (m_iconShowRule.viewMode == ViewMode::SmallIcon) return;
    const int oldIconWidth = m_iconWidth;
    const int oldIconHeight = m_iconHeight;
    const int oldRowSpacing = m_rowSpacing;
    const int oldColSpacing = m_colSpacing;
    m_iconShowRule.viewMode = ViewMode::SmallIcon;
    syncViewModeSizes();
    applyViewModeMetricsChange(oldIconWidth, oldIconHeight, oldRowSpacing, oldColSpacing);
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
        for (const QPointer<LinBox>& box : BoxManager::instance()->allBoxes()) {
            if (box) {
                box->hide();
            }
        }
    } else {
        m_iconShowRule.showDesktopIcons = true;
        this->rightMenu->setShowDesktopIconsChecked(true);
        for (auto item : m_items) {
            item->show();
        }
        for (const QPointer<LinBox>& box : BoxManager::instance()->allBoxes()) {
            if (box) {
                box->show();
            }
        }
        BoxManager::instance()->raiseAllBoxes();
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
                if (!item || item->filePath().isEmpty()) continue;
                QString normPath = QDir::toNativeSeparators(item->filePath());
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
            if (!item || item->filePath().isEmpty()) continue;
            
            // Refresh icon for each item
            item->refreshIcon();
            
            QString boxId = getBoxIdForItem(item->filePath());
            item->setBoxId(boxId);

            bool isVisibleSpecial = false;
            bool isSpecial = (item->getItemData().type() == ObjectType::SpecialFolder ||
                              SystemFolderCLSIDs.contains(item->getItemData().getCLSID()) ||
                              item->getItemData().fullName() == "desktop.ini" ||
                              item->filePath().startsWith("::"));
            
            bool isOsHidden = item->fileInfo().isHidden();
            const bool isAppHiddenShellFolder = DesktopItemLoader::shouldTreatAsHiddenShellFolder(item->getItemData());

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

            if (isAppHiddenShellFolder) {
                item->hide();
                m_hiddenIcons.push_back(item);
            } else if (!boxId.isEmpty()) {
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

        // Refresh icons for items in boxes and ensure boxes are re-shown
        for (const auto& box : BoxManager::instance()->allBoxes()) {
            if (box) {
                for (auto* item : box->items()) {
                    if (item) item->refreshIcon();
                }
                box->refreshLayout();
                box->show();
                box->raise();
            }
        }
    });
}

void ListView::onMenuPaste() {
    const QStringList filePaths = ListViewFileOpsHelper::clipboardLocalFilePaths();
    if (filePaths.isEmpty()) {
        qDebug() << "ListView: Paste skipped, clipboard has no local files";
        return;
    }

    const bool isCutOperation = ListViewFileOpsHelper::clipboardContainsCutItems();
    bool ok = true;

    if (isCutOperation) {
        std::vector<std::wstring> srcPaths;
        srcPaths.reserve(filePaths.size());
        for (const QString& filePath : filePaths) {
            srcPaths.push_back(QDir::toNativeSeparators(filePath).toStdWString());
        }

        const std::wstring desktopDir = QDir::toNativeSeparators(m_desktopDir).toStdWString();
        ok = CSystemApi::MoveFiles(srcPaths, desktopDir);
    } else {
        ok = ListViewFileOpsHelper::copyFilesToDirectoryWithRule(filePaths, m_desktopDir);
    }

    qDebug() << "ListView: Paste triggered" << (isCutOperation ? "[move]" : "[copy]") << "result:" << ok;
    if (ok) {
        if (isCutOperation) {
            clearCutMarks();
            QGuiApplication::clipboard()->clear();
        }
    }
}

void ListView::onMenuPasteShortcut() {
    const QStringList filePaths = ListViewFileOpsHelper::clipboardLocalFilePaths();
    if (filePaths.isEmpty()) {
        qDebug() << "ListView: Paste Shortcut skipped, clipboard has no local files";
        return;
    }

    int createdCount = 0;
    for (const QString& filePath : filePaths) {
        const QString shortcutPath = ListViewFileOpsHelper::uniqueShortcutPathForTarget(filePath, m_desktopDir);
        if (ListViewFileOpsHelper::createShellShortcut(filePath, shortcutPath)) {
            ++createdCount;
        }
    }

    qDebug() << "ListView: Paste Shortcut triggered, created:" << createdCount << "of" << filePaths.size();
}

void ListView::onFileSystemChanged(const FileSystemEventInfo& eventInfo)
{
    // 瑙勮寖鍖栬矾寰勶紙缁熶竴鐢ㄥ弽鏂滄潬锛屽幓鎺夋湯灏炬枩鏉狅級
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

    // 閲嶆柊甯冨眬骞跺埛鏂?
    auto relayout = [this]() {
        loadIconLayer();
        update();
        repaint();
    };

    // 鎸夊綋鍓嶆帓搴忚鍒欓噸鎺?
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

        auto boxIt = m_pendingCreatedItemBoxes.find(newPath);
        if (boxIt != m_pendingCreatedItemBoxes.end()) {
            QPointer<LinBox> targetBox = boxIt.value();
            m_pendingCreatedItemBoxes.erase(boxIt);
            const bool hasPendingInsertIndex = m_pendingCreatedItemInsertIndices.contains(newPath);
            const int insertIndex = hasPendingInsertIndex ? m_pendingCreatedItemInsertIndices.take(newPath) : -1;
            if (targetBox) {
                moveItemToBox(newPath, targetBox, false, insertIndex);
                targetBox->refreshLayout();
                targetBox->raise();
                if (m_iconShowRule.autoArrange) {
                    loadIconLayer();
                } else {
                    update();
                }
            }
        }
        else {
            handlePendingShellCreatedItem(newPath);
        }
        
        // 淇濆瓨鎸佷箙鍖栨枃浠讹紝璁板綍鏂版枃浠朵綅缃?
        requestSaveBoxes();
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
            
            // 淇濆瓨鎸佷箙鍖栨枃浠讹紝绉婚櫎宸插垹闄ゆ枃浠惰褰?
            requestSaveBoxes();
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
                const ListItemData data = DesktopItemLoader().loadItem(newPath);
                item->setItemData(data);
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

        auto boxIt = m_pendingCreatedItemBoxes.find(newPath);
        if (boxIt != m_pendingCreatedItemBoxes.end()) {
            QPointer<LinBox> targetBox = boxIt.value();
            m_pendingCreatedItemBoxes.erase(boxIt);
            const bool hasPendingInsertIndex = m_pendingCreatedItemInsertIndices.contains(newPath);
            const int insertIndex = hasPendingInsertIndex ? m_pendingCreatedItemInsertIndices.take(newPath) : -1;
            if (targetBox) {
                moveItemToBox(newPath, targetBox, false, insertIndex);
                targetBox->refreshLayout();
                targetBox->raise();
                if (m_iconShowRule.autoArrange) {
                    loadIconLayer();
                } else {
                    update();
                }
            }
        }
        else {
            handlePendingShellCreatedItem(newPath);
        }
        
        // 淇濆瓨鎸佷箙鍖栨枃浠讹紝鏇存柊閲嶅懡鍚嶆枃浠惰褰?
        requestSaveBoxes();
        break;
    }
    case FileSystemEvent::FileUpdated:
    case FileSystemEvent::FileAttributesChanged:
    {
        const QString& updatePath = !newPath.isEmpty() ? newPath : oldPath;
        if (updatePath.isEmpty()) break;

        ListItem* item = selectItemByPath(updatePath);
        if (item) {
            const ListItemData data = DesktopItemLoader().loadItem(updatePath);
            item->setItemData(data);
            item->initData();
            item->update();
        }
        break;
    }
    }
    
    // Ensure all LinBoxes stay above desktop items
    for (const auto& box : BoxManager::instance()->allBoxes()) {
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
    BoxManager* manager = BoxManager::instance();
	LinBox* box = manager->createBox(m_lastRightClickPos, this);
    
    // Connect box signals to ListView
    manager->setupConnections(box, this);
}

void ListView::onMenuMapFolder()
{
    const QString selectedDir = QFileDialog::getExistingDirectory(
        this,
        tr("Select Folder to Map"),
        QDir::homePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (selectedDir.isEmpty()) {
        return;
    }

    const QString normalizedPath = normalizeMappedFolderPathForLookup(selectedDir);
    if (findMappedFolderBox(normalizedPath)) {
        showTransientTip(tr("This folder is already on the desktop."), 2600);
        return;
    }

    BoxManager* manager = BoxManager::instance();
	LinBox* box = manager->createBox(m_lastRightClickPos, this);
    manager->setupConnections(box, this);

    box->setBoxTitle(QString());
    box->setMappedFolderPath(normalizedPath);
    loadMappedFolderItemsForBox(box);
    box->refreshLayout();
    box->raise();
    requestSaveBoxes();
}

QVector<LinBox*> ListView::boxes() const {
    QVector<LinBox*> result;
    for (const QPointer<LinBox>& box : BoxManager::instance()->allBoxes()) {
        if (box) result.append(box.data());
    }
    return result;
}

void ListView::setupItemConnections(ListItem* item, LinBox* box)
{
	if (!item) return;
	
	// 鍏堟柇寮€璇?item 鐨勬墍鏈変俊鍙疯繛鎺ワ紝闃叉閲嶅杩炴帴
	disconnect(item, &ListItem::itemClicked, this, nullptr);
	disconnect(item, &ListItem::itemMoved, this, nullptr);
	disconnect(item, &ListItem::itemReleased, this, nullptr);
	disconnect(item, &ListItem::itemDoubleClicked, this, nullptr);
	disconnect(item, &ListItem::itemContextMenuRequested, this, nullptr);
	
	connect(item, &ListItem::itemClicked, this, [this, box](ListItem* i, QMouseEvent* e) {
		onItemRelayClicked(box, i, e);
	});
	connect(item, &ListItem::itemMoved, this, [this, box](ListItem* i, QMouseEvent* e) {
		onItemRelayMoved(box, i, e);
	});
	connect(item, &ListItem::itemReleased, this, [this, box](ListItem* i, QMouseEvent* e) {
		onItemRelayReleased(box, i, e);
	});
	connect(item, &ListItem::itemDoubleClicked, this, [this, box](ListItem* i, QMouseEvent* e) {
		onItemRelayDoubleClicked(box, i, e);
	});
	connect(item, &ListItem::itemContextMenuRequested, this, [this, box](ListItem* i, const QPoint& p) {
		onItemRelayContextMenuRequested(box, i, p);
	});
}

void ListView::onItemRelayClicked(LinBox* box, ListItem* item, QMouseEvent* event)
{
	if (!item) return;

    const ListItemData itemData = item->getItemData();
    qDebug().noquote()
        << QString("[ItemClick] name=\"%1\" path=\"%2\" clsid=\"%3\" type=%4 boxId=\"%5\" normalizedPath=\"%6\"")
            .arg(item->baseName(),
                 item->filePath(),
                 itemData.getCLSID(),
                 QString::number(static_cast<int>(item->type())),
                 item->boxId(),
                 item->normalizedPath());

	if (event->button() == Qt::LeftButton) {
		// Ensure focus is on the correct window to receive Delete shortcuts
		if (box) box->setFocus();
		else this->setFocus();

		Qt::KeyboardModifiers modifiers = QApplication::keyboardModifiers();
		
		if (modifiers & Qt::ControlModifier) {
			// Ctrl: 鍒囨崲鍗曚釜 item 閫変腑鐘舵€?
			toggleItemSelection(item);
            m_wasSelectedBeforeClick = false; // Multi-select cancels rename timing
		} else if (modifiers & Qt::ShiftModifier) {
			// Shift: 杩藉姞閫変腑
			selectRangeToItem(item, box);
            m_wasSelectedBeforeClick = false;
		} else {
			// 鏃犱慨楗伴敭锛?
			// 濡傛灉鐐瑰嚮鐨?item 宸茬粡鍦ㄥ閫夐泦鍚堥噷锛屼繚鐣欐暣涓€変腑缁勶紙鍑嗗澶氶€夋嫋鍔級
			// 鍚﹀垯娓呯┖閫変腑锛屽彧閫夎繖涓€涓?
			if (!m_selectedItems.contains(item)) {
                RenameManager::instance()->cancelRename(); // New selection cancels rename
				applySelectionState({item}, item, true);
                m_anchorItem = item;
                m_wasSelectedBeforeClick = false;
			} else {
                m_focusedItem = item;
                item->setFullNameExpanded(true);
                item->raise();
                m_wasSelectedBeforeClick = (m_selectedItems.size() == 1);
            }
		}

        // Windows Rename Rhythm: 
        // If single item was already selected, and we click it again after a delay
        if (m_wasSelectedBeforeClick && m_lastClickedItem == item && m_selectedItems.size() == 1) {
            if (m_clickTimer.isValid() && m_clickTimer.elapsed() > 500 && m_clickTimer.elapsed() < 5000) {
                // Trigger rename
                RenameManager::instance()->startRename(item);
                m_isDragging = false; 
                return;
            }
        }

        m_lastClickedItem = item;
        m_clickTimer.restart();
        if (!(modifiers & Qt::ControlModifier) && !(modifiers & Qt::ShiftModifier)) {
            m_anchorItem = item;
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
        m_startPos = clickPosInListView; // 璁板綍寮€濮嬩綅缃互鍒ゆ柇鎷栧嚭闃堝€?
	}
}

void ListView::resetRenameClickState()
{
    m_lastClickedItem = nullptr;
    m_wasSelectedBeforeClick = false;
    m_clickTimer.invalidate();
}

void ListView::onItemRelayMoved(LinBox* box, ListItem* item, QMouseEvent* event)
{
    RenameManager::instance()->cancelRename(); // Any move cancels rename
    resetRenameClickState();
	if (m_isDragging) {
		QPoint globalPos = item->mapToGlobal(event->pos());
		QPoint mousePos = this->mapFromGlobal(globalPos);
        
        // 妫€鏌ュ惎鍔?QDrag (澶栭儴鎷栧嚭) 鐨勯槇鍊?
        if ((mousePos - m_startPos).manhattanLength() > QApplication::startDragDistance()) {
       
            // For internal tracking, create visual ghosts only when actually moving
            if (m_ghosts.isEmpty()) {
                createDragGhosts(m_startPos);
            } else {
                // Update ghosts while moving within or over the Box
                for (auto& ghost : m_ghosts) {
                    if (ghost.widget) {
                        ghost.widget->move(globalPos + ghost.offset);
                    }
                }
            }

            // 鍑嗗鎷栧嚭鏁版嵁
            QMimeData* mimeData = new QMimeData();
            QList<QUrl> urls;
            for (auto* it : m_selectedItems) {
                urls << QUrl::fromLocalFile(it->filePath());
            }
            mimeData->setUrls(urls);
            
            QDrag* drag = new QDrag(this);
            drag->setMimeData(mimeData);
            
            // --- 澶氬浘鏍囨嫋鎷借瑙夋晥鏋滃寮?---
            if (!m_selectedItems.isEmpty()) {
                // 1. 璁＄畻鎵€鏈夐€変腑椤圭殑鑱斿悎鍖呭洿鐩?(浣跨敤鍏ㄥ眬灞忓箷鍧愭爣锛岄槻姝ox鍐呴儴灞€閮ㄥ潗鏍囨薄鏌?
                QRect globalBounding;
                for (auto* it : m_selectedItems) {
                    QRect globalRect(it->mapToGlobal(QPoint(0,0)), it->size());
                    globalBounding = globalBounding.united(globalRect);
                }

                // 2. 鍒涘缓涓€涓冻澶熷ぇ鐨勯€忔槑 Pixmap 鏉ユ壙杞芥墍鏈夊浘鏍?
                QPixmap fullPix(globalBounding.size());
                fullPix.fill(Qt::transparent);
                QPainter painter(&fullPix);
                painter.setRenderHint(QPainter::Antialiasing);

                for (auto* it : m_selectedItems) {
                    // 鏆傛椂鍙栨秷閫変腑鍜屾偓娴姸鎬佷互鎶撳彇骞插噣鐨勫浘鍍?
                    bool wasSelected = it->isSelected;
                    bool wasHovered = it->isHovered;
                    it->isSelected = false;
                    it->isHovered = false;

                    // 灏?item 娓叉煋鍒?Pixmap 鐨勭浉瀵逛綅缃紙鍏ㄥ眬浣嶇疆鐩稿浜庡叏灞€鍖呭洿鐩掑乏涓婅锛?
                    QPoint globalItemPos = it->mapToGlobal(QPoint(0,0));
                    it->render(&painter, globalItemPos - globalBounding.topLeft());

                    // 鎭㈠鐘舵€?
                    it->isSelected = wasSelected;
                    it->isHovered = wasHovered;
                }
                painter.end();

                drag->setPixmap(fullPix);
                
                // 3. 璁剧疆鐑偣锛氱敤鎴锋渶鍒濈偣鍑荤偣鍦ㄥ叏灞€鍖呭洿鐩掑唴鐨勭簿纭浉瀵瑰潗鏍?
                // m_startPos 闇€瑕佸厛杞洖鍏ㄥ眬鍧愭爣锛屾墠鑳戒笌 globalBounding 姝ｇ‘璁＄畻
                QPoint globalStartPos = this->mapToGlobal(m_startPos);
                QPoint hotSpot = globalStartPos - globalBounding.topLeft();
                drag->setHotSpot(hotSpot);
            }
            
            // 寮€濮嬫嫋鎷藉墠闅愯棌 ghosts (鍥犱负 QDrag 浼氭帴绠?
            for (auto& ghost : m_ghosts) {
                if (ghost.widget) ghost.widget->hide();
            }
            
            // 鎵ц鎷栨嫿 (杩欎細闃诲鐩村埌閲婃斁)
            Qt::DropAction dropAction = drag->exec(Qt::CopyAction | Qt::MoveAction);
            
            // 鎷栨嫿缁撴潫鍚庣殑娓呯悊
            m_isDragging = false;
            m_draggedItem = nullptr;
            for (auto& ghost : m_ghosts) {
                if (ghost.widget) ghost.widget->deleteLater();
            }
            m_ghosts.clear();
            
            // 淇濆瓨鍥炬爣浣嶇疆鍒伴厤缃枃浠讹紙鎷栨嫿缁撴潫鍚庨渶瑕佷繚瀛橈級
            requestSaveBoxes();
            
            return;
        }

		for (auto& ghost : m_ghosts) {
			if (ghost.widget) {
				ghost.widget->move(globalPos + ghost.offset);
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
					QPoint finalPos = this->mapFromGlobal(ghost.widget->pos());
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
		
		// 淇濆瓨鍥炬爣浣嶇疆鍒伴厤缃枃浠?
		requestSaveBoxes();
	}
}

void ListView::createDragGhosts(const QPoint& clickPos)
{
	// 娓呯悊鏃х殑 ghost
	for (auto& ghost : m_ghosts) {
		if (ghost.widget) ghost.widget->deleteLater();
	}
	m_ghosts.clear();

	for (auto* item : m_selectedItems) {
		if (!item) continue;

		QLabel* ghostWidget = new QLabel(nullptr);
        ghostWidget->setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowTransparentForInput);
		ghostWidget->setAttribute(Qt::WA_TranslucentBackground);
		ghostWidget->setAttribute(Qt::WA_NoSystemBackground);
        ghostWidget->setAttribute(Qt::WA_TransparentForMouseEvents);
		ghostWidget->setStyleSheet("background: transparent;");

		// 鎶撳彇蹇収锛堜复鏃跺彇娑堥€変腑浠ヨ幏寰楀共鍑€鍥炬爣锛?
		bool wasSelected = item->isSelected;
		item->isSelected = false;
		item->update();
		QPixmap pixmap = item->grab();
		item->isSelected = wasSelected;
		item->update();

		ghostWidget->setPixmap(pixmap);
		ghostWidget->setFixedSize(item->size());
		
		QPoint globalItemPos = item->mapToGlobal(QPoint(0,0));
		QPoint globalClickPos = this->mapToGlobal(clickPos);
		QPoint offset = globalItemPos - globalClickPos;
		
		ghostWidget->move(QCursor::pos() + offset);
		ghostWidget->show();
        if (HWND hwnd = reinterpret_cast<HWND>(ghostWidget->winId())) {
            SetWindowPos(
                hwnd,
                HWND_NOTOPMOST,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW
            );
        }

		m_ghosts.append({ ghostWidget, item, offset });
	}
}

void ListView::onItemRelayDoubleClicked(LinBox* box, ListItem* item, QMouseEvent* event)
{
    RenameManager::instance()->cancelRename(); // Double click cancels rename
	if (item) {
		item->openItem();
	}
}

void ListView::onItemRelayContextMenuRequested(LinBox* box, ListItem* item, const QPoint& globalPos)
{
    RenameManager::instance()->cancelRename(); // Context menu cancels rename
	if (!item) return;

	// Ensure item is selected for context menu
	if (!m_selectedItems.contains(item)) {
		applySelectionState({item}, item, true);
        m_anchorItem = item;
	} else {
        setFocusedItem(item);
	}

	requestSystemRightMenu(item->type(), globalPos, m_selectedItems);
}

void ListView::ensureDragTip() {
    if (!m_dragTip) {
        m_dragTip = new QLabel(this);
        m_dragTip->setTextFormat(Qt::RichText);
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
        m_dragTip->hide();
    }
}

void ListView::ensureInfoTip()
{
    if (!m_infoTip) {
        m_infoTip = new FluentInfoTip(this);
        m_infoTip->hide();
    }
}

void ListView::showTransientTip(const QString& message, int autoHideMs)
{
    ensureInfoTip();
    m_infoTip->setText(message);
    const QRect globalRect(mapToGlobal(QPoint(0, 0)), size());
    m_infoTip->showCentered(globalRect, autoHideMs);
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
    if (m_isLoadingPersistence) {
        return;
    }

    LayoutPersistenceService persistenceService;
    LayoutPersistenceState state;
    state.enablePersistence = m_enablePersistence;
    state.viewMode = (int)m_iconShowRule.viewMode;
    state.sortType = (int)m_iconShowRule.sortType;
    state.iconPosMode = (int)m_iconShowRule.iconPosMode;
    state.autoArrange = m_iconShowRule.autoArrange;
    state.gridAlignment = m_iconShowRule.gridAlignment;
    state.showDesktopIcons = m_iconShowRule.showDesktopIcons;
    state.doubleClickHideIcons = m_iconShowRule.doubleClickHideIcons;
    state.showShortCutArrow = m_iconShowRule.showShortCutArrow;
    state.iconPositions = persistenceService.collectIconPositions(
        m_allItems,
        BoxManager::instance()->allBoxes()
    );

    m_savedIconPositions = persistenceService.toSavedIconPositions(state.iconPositions);
    persistenceService.save(m_persistencePath, state, m_allItems);
}

void ListView::requestSaveBoxes() {
    if (m_isLoadingPersistence) {
        return;
    }
    BoxManager::instance()->requestSave();
}



void ListView::applyBoxAssignmentsFromConfig(const QVector<QMap<QString, QVariant>>& boxData) {
    qDebug() << "[applyBoxAssignmentsFromConfig] Processing" << boxData.size() << "boxes";
    QSet<LinBox*> boxesToRefresh;
    
    for (const auto& data : boxData) {
        const QString boxId = data.value("id").toString();
        LinBox* targetBox = BoxManager::instance()->findBoxById(boxId);
        if (!targetBox) {
            qWarning() << "[applyBoxAssignmentsFromConfig] Box not found:" << boxId;
            continue;
        }

        if (targetBox->isFolderMapped()) {
            continue;
        }
        
        const QStringList files = data.value("files").toStringList();
        qDebug() << "[applyBoxAssignmentsFromConfig] Box" << boxId << "has" << files.size() << "files";
        
        for (const QString& path : files) {
            ListItem* item = selectItemByPath(path);
            if (!item) {
                qWarning() << "[applyBoxAssignmentsFromConfig] Item not found:" << path;
                continue;
            }
            
            qDebug() << "[applyBoxAssignmentsFromConfig] Moving item to box:" << item->baseName() 
                     << "from parent:" << item->parentWidget() 
                     << "to box:" << targetBox;
            
            // 璁剧疆 boxId 骞跺皢 item 瀹為檯娣诲姞鍒?box 涓?
            item->setBoxId(boxId);
            item->hide();
            
            // 浣跨敤 addListItem 鏉ユ坊鍔狅紝瀹冧細姝ｇ‘璁剧疆鐖剁獥鍙ｄ负 m_contentArea
            setupItemConnections(item, targetBox);
            targetBox->addListItem(item, false);
            boxesToRefresh.insert(targetBox);
            
            qDebug() << "[applyBoxAssignmentsFromConfig] Item moved, new parent:" << item->parentWidget();
        }
    }

    for (LinBox* box : boxesToRefresh) {
        if (!box) continue;
        box->refreshLayout();
        box->raise();
    }
}

QStringList ListView::mappedFolderEntryPaths(const QString& folderPath) const {
    return ListViewMappedBoxHelper::mappedFolderEntryPaths(folderPath);
}

void ListView::clearMappedFolderItemsForBox(LinBox* box) {
    ListViewMappedBoxHelper::clearMappedFolderItemsForBox(*this, box);
}

void ListView::loadMappedFolderItemsForBox(LinBox* box) {
    ListViewMappedBoxHelper::loadMappedFolderItemsForBox(*this, box);
}

void ListView::applySavedPositionsToItems(const QMap<QString, QMap<QString, QVariant>>& iconPositions) {
    for (auto it = iconPositions.begin(); it != iconPositions.end(); ++it) {
        ListItem* item = selectItemByPath(it.key());
        if (!item) {
            qWarning() << "[ListView] No ListItem matched config path:" << it.key();
            continue;
        }
        const QMap<QString, QVariant>& posInfo = it.value();
        const QString boxIdFromConfig = posInfo.value("boxId").toString();
        const QString resolvedBoxId = !boxIdFromConfig.isEmpty() ? boxIdFromConfig : item->boxId();
        item->setBoxId(resolvedBoxId);
        item->updateLocationInfo(QPoint(posInfo.value("x").toInt(), posInfo.value("y").toInt()));
    }
}

void ListView::loadBoxes(bool autoUpdateState) {
    const ListViewSelectionSnapshot selectionSnapshot = ListViewSelectionSnapshot::capture(*this);

    m_isLoadingPersistence = true;
    m_forceConfigLayout = false;
    m_hasLoadedConfigLayout = false;

    QVector<LinBox*> existingBoxes;
    for (const QPointer<LinBox>& box : BoxManager::instance()->allBoxes()) {
        if (box) existingBoxes.append(box.data());
    }
    for (auto* box : existingBoxes) {
        if (box) {
            if (box->isFolderMapped()) {
                clearMappedFolderItemsForBox(box);
            }
            for (auto* item : box->items()) {
                if (item) {
                    item->setBoxId(QString());
                    item->show();
                    if (!m_items.contains(item)) m_items.push_back(item);
                }
            }
            box->deleteLater();
        }
    }
    BoxManager::instance()->clearBoxes();
    m_mappedBoxFilePaths.clear();
    for (auto* item : m_allItems) {
        if (item) {
            item->setBoxId("");
        }
    }

    LayoutPersistenceService persistenceService;
    LayoutPersistenceState state;
    state.enablePersistence = m_enablePersistence;
    state.viewMode = (int)m_iconShowRule.viewMode;
    state.sortType = (int)m_iconShowRule.sortType;
    state.iconPosMode = (int)m_iconShowRule.iconPosMode;
    state.autoArrange = m_iconShowRule.autoArrange;
    state.gridAlignment = m_iconShowRule.gridAlignment;
    state.showDesktopIcons = m_iconShowRule.showDesktopIcons;
    state.doubleClickHideIcons = m_iconShowRule.doubleClickHideIcons;
    state.showShortCutArrow = m_iconShowRule.showShortCutArrow;

    persistenceService.load(m_persistencePath, autoUpdateState, state);

    m_enablePersistence = state.enablePersistence;
    m_iconShowRule.viewMode = (ViewMode)state.viewMode;
    m_iconShowRule.sortType = (SortOrderType)state.sortType;
    m_iconShowRule.iconPosMode = (IconPosMode)state.iconPosMode;
    m_iconShowRule.autoArrange = state.autoArrange;
    m_iconShowRule.gridAlignment = state.gridAlignment;
    m_iconShowRule.showDesktopIcons = state.showDesktopIcons;
    m_iconShowRule.doubleClickHideIcons = state.doubleClickHideIcons;
    m_iconShowRule.showShortCutArrow = state.showShortCutArrow;

    QVector<QMap<QString, QVariant>>& boxData = state.boxData;
    QMap<QString, QMap<QString, QVariant>>& iconPositions = state.iconPositions;

    m_forceConfigLayout = !boxData.isEmpty() || !iconPositions.isEmpty();
    m_hasLoadedConfigLayout = m_forceConfigLayout;

    syncViewModeSizes();
    updateRightMenuState();

    for (const auto& data : boxData) {
        createBoxFromSavedData(
            data.value("title").toString(),
            QRect(data.value("x").toInt(), data.value("y").toInt(), 
                  data.value("w").toInt(), data.value("h").toInt()),
            data.value("collapsed").toBool(),
            data.value("h").toInt(),
            data.value("showType").toInt(),
            data.value("files").toStringList(),
            QColor(data.value("color").toString()),
            data.value("opacity").toDouble() / 100.0f,
            data.value("glass").toBool(),
            data.value("id").toString(),
            data.value("mappedFolderPath").toString(),
            false
        );
    }
    applyBoxAssignmentsFromConfig(boxData);
    applySavedPositionsToItems(iconPositions);

    m_savedIconPositions = persistenceService.toSavedIconPositions(iconPositions);

    loadIconLayer();
    m_forceConfigLayout = false;
    update();
    m_allItems;
    m_items;
    m_isLoadingPersistence = false;
    selectionSnapshot.restore(*this, false);

    qDebug() << "Loaded. Persistence:" << m_enablePersistence;
}

void ListView::createBoxFromSavedData(const QString& title, const QRect& geom, bool collapsed, int origH, int showType, const QStringList& files, const QColor& color, float opacity, bool glass, const QString& explicitId, const QString& mappedFolderPath, bool refresh) {
    LinBox* box = new LinBox(this);
    if (explicitId.isEmpty()) {
        box->setBoxId(QUuid::createUuid().toString());
    } else {
        box->setBoxId(explicitId);
    }
    
    if (!mappedFolderPath.isEmpty()) {
        if (title.isEmpty()) {
            box->setBoxTitle(QString());
        } else {
            box->setBoxTitle(title);
        }
        box->setMappedFolderPath(mappedFolderPath);
    } else if (!title.isEmpty()) {
        box->setBoxTitle(title);
    }
    
    if (color.isValid()) box->setBackgroundColor(color);
    box->setBackgroundOpacity(opacity);
    box->setGlassEffect(glass);
    
    // Connect to BoxManager and ListView
    BoxManager::instance()->setupConnections(box, this);
    BoxManager::instance()->addBox(box);
    
    box->setGeometry(geom);
    box->setShowType((LinBox::BoxShowType)showType);
    if (collapsed) box->toggleCollapse();

    box->show();
    box->setGlassEffect(glass); // Apply blur AFTER window is fully visible to prevent black screen
    if (box->isFolderMapped()) {
        loadMappedFolderItemsForBox(box);
    }
    box->refreshLayout(); // Force initial tight packing
    box->raise();

    if (refresh) {
        loadIconLayer();
        update();
        requestSaveBoxes();
    }
}

void ListView::onMenuDestroyBox(LinBox* box) {
    if (!box) return;

    if (box->isFolderMapped()) {
        if (LinDirectoryMonitor* monitor = box->mappedFolderMonitor()) {
            monitor->stopListening();
            box->setMappedFolderMonitor(nullptr);
        }
        clearMappedFolderItemsForBox(box);
        BoxManager::instance()->destroyBox(box, this);
        update();
        return;
    }

    RenameManager::instance()->cancelRename();
    resetRenameClickState();
    QVector<ListItem*> boxItems = box->items();
    for (auto* item : boxItems) {
        if (!item) continue;

        item->disconnect(this);
        item->disconnect(box);
        box->removeListItem(item, false);
        m_selectedItems.removeOne(item);
        item->unCheckSelf();
        item->isHovered = false;
        item->setFullNameExpanded(false);
        item->setExpandedPreviewText(QString());
        item->setBoxId("");
        item->setLayoutMode(ListItem::LayoutMode::IconMode);
        item->setParent(this);
        item->setItemSize(m_iconWidth, m_iconHeight);
        setupItemConnections(item);

        const QPoint desktopPos = generateItemPosition();
        item->updateLocationInfo(desktopPos);
        item->setBoxId("");

        if (!m_items.contains(item)) {
            m_items.push_back(item);
        }

        QString key = QDir::toNativeSeparators(item->filePath());
        while (key.endsWith('\\')) key.chop(1);
        m_savedIconPositions[key] = { desktopPos.x(), desktopPos.y(), "" };

        if (m_iconShowRule.showDesktopIcons) {
            item->show();
        }
    }
    
    BoxManager::instance()->destroyBox(box, this);
    update();
}

void ListView::onMenuCustomBox(LinBox* box) {
    // Placeholder for color customizations
    qDebug() << "Customize box requested:" << box->boxTitle();
}

void ListView::moveItemToBox(const QString& filePath, LinBox* targetBox, bool refresh, int insertIndex) {
    ListItem* item = findItemByPath(filePath);
    LinBox* sourceBox = nullptr;
    if (item && !item->boxId().isEmpty()) {
        sourceBox = BoxManager::instance()->findBoxById(item->boxId());
    }

    if (item) {
        removeItemNoDelete(item);
    }

    BoxManager::instance()->moveItemToBox(filePath, targetBox, this, insertIndex, false);

    if (sourceBox && sourceBox != targetBox) {
        sourceBox->refreshLayout();
        sourceBox->update();
    }

    if (targetBox) {
        targetBox->refreshLayout();
        targetBox->update();
        targetBox->raise();
    }
    
    if (refresh) {
        update();
        requestSaveBoxes();
    }
    
    qDebug() << "Moved item to box (re-parented):" << filePath << "->" << targetBox->boxTitle() << "index=" << insertIndex;
}

void ListView::moveItemsToBox(const QVector<ListItem*>& items, LinBox* targetBox, int insertIndex, bool refresh)
{
    if (!targetBox || items.isEmpty()) {
        return;
    }

    QVector<ListItem*> orderedItems;
    QSet<ListItem*> seenItems;
    QSet<LinBox*> sourceBoxes;
    for (ListItem* item : items) {
        if (!item || seenItems.contains(item)) {
            continue;
        }
        seenItems.insert(item);
        orderedItems.append(item);
        if (!item->boxId().isEmpty()) {
            if (LinBox* sourceBox = BoxManager::instance()->findBoxById(item->boxId())) {
                if (sourceBox != targetBox) {
                    sourceBoxes.insert(sourceBox);
                }
            }
        }
    }

    for (int i = 0; i < orderedItems.size(); ++i) {
        removeItemNoDelete(orderedItems[i]);
        BoxManager::instance()->moveItemToBox(orderedItems[i]->filePath(), targetBox, this, insertIndex + i, false);
    }

    for (LinBox* sourceBox : sourceBoxes) {
        if (!sourceBox) continue;
        sourceBox->refreshLayout();
        sourceBox->update();
    }

    targetBox->refreshLayout();
    targetBox->update();
    targetBox->raise();

    if (refresh) {
        update();
        requestSaveBoxes();
    }
}

QVector<ListItem*> ListView::orderedDraggedItemsForPaths(const QStringList& paths) const
{
    QVector<ListItem*> resolvedItems;
    QSet<ListItem*> seenItems;
    for (const QString& path : paths) {
        if (ListItem* item = findItemByPath(path)) {
            if (!seenItems.contains(item)) {
                seenItems.insert(item);
                resolvedItems.append(item);
            }
        }
    }

    if (resolvedItems.isEmpty()) {
        return resolvedItems;
    }

    QSet<QString> sourceBoxIds;
    bool hasDesktopItems = false;
    for (ListItem* item : resolvedItems) {
        if (!item) continue;
        if (item->boxId().isEmpty()) {
            hasDesktopItems = true;
        } else {
            sourceBoxIds.insert(item->boxId());
        }
    }

    QVector<ListItem*> orderedItems;
    if (sourceBoxIds.size() == 1 && !hasDesktopItems) {
        if (LinBox* sourceBox = BoxManager::instance()->findBoxById(*sourceBoxIds.begin())) {
            for (ListItem* item : sourceBox->items()) {
                if (seenItems.contains(item)) {
                    orderedItems.append(item);
                }
            }
        }
    } else if (sourceBoxIds.isEmpty() && hasDesktopItems) {
        for (ListItem* item : m_items) {
            if (seenItems.contains(item)) {
                orderedItems.append(item);
            }
        }
    }

    if (!orderedItems.isEmpty()) {
        return orderedItems;
    }
    return resolvedItems;
}

bool ListView::importPathsToBox(const QStringList& sourcePaths, LinBox* targetBox, bool copyMode, int insertIndex, bool preferShortcut)
{
    if (sourcePaths.isEmpty() || !targetBox) {
        return false;
    }

    bool ok = true;
    QStringList createdPaths;

    int nextInsertIndex = insertIndex;
    for (const QString& sourcePath : sourcePaths) {
        if (sourcePath.isEmpty()) {
            continue;
        }

        const QString destinationPath = ListViewFileOpsHelper::uniquePasteDestinationPath(sourcePath, m_desktopDir);
        const QString normalizedDestination = normalizeDesktopPath(destinationPath);

        bool singleOk = false;
        if (preferShortcut) {
            singleOk = ListViewFileOpsHelper::createShellShortcut(sourcePath, destinationPath);
        } else if (copyMode) {
            singleOk = ListViewFileOpsHelper::copyPathToExactDestination(sourcePath, destinationPath);
        } else {
            singleOk = ListViewFileOpsHelper::movePathToExactDestination(sourcePath, destinationPath);
        }

        if (singleOk) {
            createdPaths.append(normalizedDestination);
            m_pendingCreatedItemBoxes[normalizedDestination] = targetBox;
            if (nextInsertIndex >= 0) {
                m_pendingCreatedItemInsertIndices[normalizedDestination] = nextInsertIndex++;
            }
        } else {
            ok = false;
        }
    }

    return ok && !createdPaths.isEmpty();
}

QStringList ListView::resolveDraggedLocalPaths(const QMimeData* mimeData) const
{
    return ListViewFileOpsHelper::draggedLocalFilePaths(mimeData);
}

bool ListView::handleDroppedPathsOnItem(const QStringList& sourcePaths, ListItem* targetItem, bool copyMode, bool preferShortcut)
{
    if (!targetItem || sourcePaths.isEmpty()) {
        return false;
    }

    const QString targetPath = targetItem->filePath();
    const QString clsid = targetItem->getItemData().getCLSID();

    if (clsid == APP_MY_COMPUTER_CLS || clsid == APP_NETWORK_CLS || clsid == APP_CONTROL_PANEL_CLS) {
        return false;
    }

    std::vector<std::wstring> srcPaths;
    QStringList args;
    for (const QString& localPath : sourcePaths) {
        if (!localPath.isEmpty()) {
            const QString nativePath = QDir::toNativeSeparators(localPath);
            args << "\"" + nativePath + "\"";
            srcPaths.push_back(nativePath.toStdWString());
        }
    }

    if (srcPaths.empty()) {
        return false;
    }

    const bool isRecycleBin = (clsid == APP_RECYCLE_BIN_CLS);
    if (isRecycleBin) {
        CSystemApi::DeleteFiles(srcPaths, true, true);
        return true;
    }

    if (targetItem->type() == ObjectType::Folder_ObjectType ||
        targetItem->type() == ObjectType::SpecialFolder ||
        targetItem->type() == ObjectType::DiskLetter) {
        if (preferShortcut) {
            return createShortcutsInFolder(sourcePaths, targetPath);
        }
        if (copyMode) {
            return CSystemApi::CopyFiles(srcPaths, QDir::toNativeSeparators(targetPath).toStdWString());
        }
        return CSystemApi::MoveFiles(srcPaths, QDir::toNativeSeparators(targetPath).toStdWString());
    }

    std::wstring wApp = QDir::toNativeSeparators(targetPath).toStdWString();
    std::wstring wArgs = args.join(" ").toStdWString();
    HINSTANCE result = ShellExecuteW(NULL, L"open", wApp.c_str(), wArgs.c_str(), NULL, SW_SHOWNORMAL);
    return (INT_PTR)result > 32;
}

bool ListView::shouldCreateShortcutForExternalDrop(const QStringList& sourcePaths) const
{
    for (const QString& sourcePath : sourcePaths) {
        if (ListViewFileOpsHelper::shouldCreateShortcutForExternalPath(sourcePath)) {
            return true;
        }
    }
    return false;
}

bool ListView::createShortcutsInFolder(const QStringList& sourcePaths, const QString& targetFolder) const
{
    if (sourcePaths.isEmpty() || targetFolder.isEmpty()) {
        return false;
    }

    bool ok = true;
    for (const QString& sourcePath : sourcePaths) {
        if (sourcePath.isEmpty()) {
            continue;
        }
        const QString shortcutPath = ListViewFileOpsHelper::uniqueShortcutPathForTarget(sourcePath, targetFolder);
        ok = ListViewFileOpsHelper::createShellShortcut(sourcePath, shortcutPath) && ok;
    }

    return ok;
}

void ListView::reloadMappedFolderBox(LinBox* box)
{
    const ListViewSelectionSnapshot selectionSnapshot = ListViewSelectionSnapshot::capture(*this);
    loadMappedFolderItemsForBox(box);
    selectionSnapshot.restore(*this, false);
}

void ListView::clearMappedFolderBox(LinBox* box)
{
    clearMappedFolderItemsForBox(box);
}

bool ListView::isItemInAnyBox(const QString& filePath) const {
    return BoxManager::instance()->isItemInAnyBox(filePath);
}

QString ListView::getBoxIdForItem(const QString& filePath) const {
    return BoxManager::instance()->getBoxIdForItem(filePath);
}

LinBox* ListView::findMappedFolderBox(const QString& folderPath, const LinBox* excludeBox) const
{
    return ListViewMappedBoxHelper::findMappedFolderBox(*this, folderPath, excludeBox);
}

QString ListView::dumpAllItemsJson() const
{
    return DesktopItemLoader::dumpListItemsJson(m_allItems);
}

QString ListView::dumpVisibleItemsJson() const
{
    return DesktopItemLoader::dumpListItemsJson(m_items);
}

QString ListView::dumpHiddenItemsJson() const
{
    return DesktopItemLoader::dumpListItemsJson(m_hiddenIcons);
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
    const bool useConfigLayout = m_forceConfigLayout || m_hasLoadedConfigLayout;
    QVector<ListItem*> allItemsSnapshot = m_items;
    QSet<LinBox*> boxesToRefresh;

    //for (auto box : BoxManager::instance()->allBoxes()) {
    //    if (!box) continue;
    //    QVector<ListItem*> boxItems = box->items();
    //    for (auto* item : boxItems) {
    //        if (item) {
    //            box->removeListItem(item);
    //        }
    //    }
    //}

    for (auto* item : allItemsSnapshot) {
        if (!item) continue;

        const QString savedBoxId = item->boxId();
        if (!savedBoxId.isEmpty()) {
            LinBox* targetBox = BoxManager::instance()->findBoxById(savedBoxId);
            if (targetBox) {
                item->setBoxId(savedBoxId);
                item->hide();
                setupItemConnections(item, targetBox);
                targetBox->addListItem(item, false);
                boxesToRefresh.insert(targetBox);
                continue;
            }
        }

        item->setItemSize(m_iconWidth, m_iconHeight);
        item->initData();
        item->setLayoutMode(ListItem::LayoutMode::IconMode);
        // Note: Do NOT set parent here. It will be set later based on boxId.
        setupItemConnections(item);

        // 妫€鏌?item 鏄惁宸叉湁鏈夋晥浣嶇疆
        QPoint currentPos = item->getLocationInfo();
        bool hasValidPosition = (currentPos != QPoint(0, 0));
        
        if (!hasValidPosition) {
            QPoint pos = useConfigLayout ? generateItemPosition() : currentPos;
            if (pos == QPoint(0, 0)) {
                pos = generateItemPosition();
            }
            item->updateLocationInfo(pos);
            continue;
        }

        const QPoint savedPos = item->getLocationInfo();
        if (savedPos.x() <= 0 || savedPos.y() <= 0) {
            QPoint newPos = generateItemPosition();
            item->updateLocationInfo(newPos);
        } else {
            item->updateLocationInfo(savedPos);
        }
        // Set parent to ListView for desktop items
        item->setParent(this);
    }

    for (LinBox* box : boxesToRefresh) {
        if (!box) continue;
        box->refreshLayout();
        box->raise();
    }
}
void ListView::setPersistenceEnabled(bool enabled) {
    if (m_enablePersistence == enabled) return;
    m_enablePersistence = enabled;
    
    if (m_enablePersistence) {
        loadBoxes(false); // Don't let the file turn us back off!
        loadIconLayer();
    } else {
        m_hasLoadedConfigLayout = false;
        // Destroy all boxes and restore items to desktop
        for (auto box : BoxManager::instance()->allBoxes()) {
            if (!box) continue;
            const auto& boxItems = box->items();
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
        BoxManager::instance()->clearBoxes();
        loadIconLayer();
    }
    
    // Always commit state change to file
    saveBoxes();

    if (!enabled) {
        FluentDialog* dlg = new FluentDialog(nullptr);
        dlg->setDialogTitle("妗岄潰鏁寸悊");
        dlg->setMessage("鏄惁绔嬪嵆涓€閿暣鐞嗗綋鍓嶆闈㈢殑鍥炬爣甯冨眬锛?");
        dlg->addActionButton("绔嬪嵆鏁寸悊", FluentDialog::ActionType::Primary, [this]() {
            this->setPersistenceEnabled(true); // Undo the disable action if they want an immediate organize/save
            this->onMenuRefresh();
        });
        dlg->addActionButton("鍙栨秷", FluentDialog::ActionType::Close);
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
