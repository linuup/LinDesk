#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDataStream>
#include <QFile>
#include <QSet>
#include <QCoreApplication>
#include <QSettings>
#include "ListView.h"
#include "DesktopQuadTree.h"
#include "SettingUI.h"
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
#include <shobjidl.h>
#include "../OS/SystemApi/resources.h"
#include "Fluent/FluentDialog.h"
#include "LinBox.h"
#include "Plugins/PluginManager.h"
#include "HotkeyManager.h"
#include "BoxManager.h"
#include "ListViewDebugPanel.h"

namespace {
constexpr DWORD kClipboardPreferredDropEffectMove = 0x00000002;

QString normalizeDesktopPath(const QString& path)
{
    QString normalized = QDir::toNativeSeparators(path);
    while (normalized.endsWith('\\') || normalized.endsWith('/')) {
        normalized.chop(1);
    }
    return normalized;
}

QStringList extractLocalPathsFromWindowsMimePayload(const QByteArray& payload, bool utf16)
{
    QStringList paths;
    if (payload.isEmpty()) {
        return paths;
    }

    if (utf16) {
        if (payload.size() < 2) {
            return paths;
        }

        const ushort* raw = reinterpret_cast<const ushort*>(payload.constData());
        const int charCount = payload.size() / 2;
        QString current;
        for (int i = 0; i < charCount; ++i) {
            const ushort ch = raw[i];
            if (ch == 0) {
                if (!current.isEmpty()) {
                    paths.append(QDir::toNativeSeparators(current));
                    current.clear();
                }
            } else {
                current.append(QChar(ch));
            }
        }
        if (!current.isEmpty()) {
            paths.append(QDir::toNativeSeparators(current));
        }
        return paths;
    }

    const QList<QByteArray> parts = payload.split('\0');
    for (const QByteArray& part : parts) {
        if (!part.isEmpty()) {
            paths.append(QDir::toNativeSeparators(QString::fromLocal8Bit(part)));
        }
    }
    return paths;
}

QStringList draggedLocalFilePaths(const QMimeData* mimeData)
{
    QStringList paths;
    if (!mimeData) {
        return paths;
    }

    for (const QUrl& url : mimeData->urls()) {
        if (url.isLocalFile()) {
            const QString localPath = QDir::toNativeSeparators(url.toLocalFile());
            if (!localPath.isEmpty()) {
                paths.append(localPath);
            }
        }
    }

    if (paths.isEmpty()) {
        const QByteArray uriListData = mimeData->data("text/uri-list");
        const QList<QByteArray> lines = uriListData.split('\n');
        for (QByteArray line : lines) {
            line = line.trimmed();
            if (line.isEmpty() || line.startsWith('#')) {
                continue;
            }
            const QUrl url = QUrl(QString::fromUtf8(line));
            if (url.isLocalFile()) {
                const QString localPath = QDir::toNativeSeparators(url.toLocalFile());
                if (!localPath.isEmpty()) {
                    paths.append(localPath);
                }
            }
        }
    }

    if (paths.isEmpty()) {
        const QStringList formats = mimeData->formats();
        for (const QString& format : formats) {
            if (format.contains("FileNameW", Qt::CaseInsensitive)) {
                paths.append(extractLocalPathsFromWindowsMimePayload(mimeData->data(format), true));
            } else if (format.contains("FileName", Qt::CaseInsensitive)) {
                paths.append(extractLocalPathsFromWindowsMimePayload(mimeData->data(format), false));
            }
        }
    }

    paths.removeAll(QString());
    paths.removeDuplicates();
    return paths;
}

QStringList clipboardLocalFilePaths()
{
    QStringList paths;

    if (const QClipboard* clipboard = QGuiApplication::clipboard()) {
        if (const QMimeData* mimeData = clipboard->mimeData()) {
            for (const QUrl& url : mimeData->urls()) {
                if (url.isLocalFile()) {
                    const QString localPath = QDir::toNativeSeparators(url.toLocalFile());
                    if (!localPath.isEmpty()) {
                        paths.append(localPath);
                    }
                }
            }
        }
    }

    if (!paths.isEmpty()) {
        paths.removeDuplicates();
        return paths;
    }

    if (!IsClipboardFormatAvailable(CF_HDROP) || !OpenClipboard(nullptr)) {
        return paths;
    }

    if (const HANDLE clipboardData = GetClipboardData(CF_HDROP)) {
        const HDROP dropHandle = static_cast<HDROP>(clipboardData);
        const UINT count = DragQueryFileW(dropHandle, 0xFFFFFFFF, nullptr, 0);
        wchar_t buffer[MAX_PATH];
        for (UINT i = 0; i < count; ++i) {
            const UINT len = DragQueryFileW(dropHandle, i, buffer, MAX_PATH);
            if (len > 0) {
                paths.append(QDir::toNativeSeparators(QString::fromWCharArray(buffer, len)));
            }
        }
    }

    CloseClipboard();
    paths.removeDuplicates();
    return paths;
}

bool clipboardContainsCutItems()
{
    const UINT format = RegisterClipboardFormatW(L"Preferred DropEffect");
    if (!format || !IsClipboardFormatAvailable(format) || !OpenClipboard(nullptr)) {
        return false;
    }

    bool isCutOperation = false;
    if (const HANDLE clipboardData = GetClipboardData(format)) {
        if (const void* locked = GlobalLock(clipboardData)) {
            const DWORD effect = *static_cast<const DWORD*>(locked);
            isCutOperation = (effect & kClipboardPreferredDropEffectMove) != 0;
            GlobalUnlock(clipboardData);
        }
    }

    CloseClipboard();
    return isCutOperation;
}

QString uniquePasteDestinationPath(const QString& sourcePath, const QString& destDir)
{
    const QFileInfo sourceInfo(sourcePath);
    const QDir dir(destDir);
    const QString originalFileName = sourceInfo.fileName();
    QString originalCandidate = dir.filePath(originalFileName);

    if (!QFileInfo::exists(originalCandidate)) {
        return QDir::toNativeSeparators(originalCandidate);
    }

    QString copyName;
    QString extension;
    if (sourceInfo.isDir()) {
        copyName = originalFileName + " - Copy";
    } else {
        const QString completeSuffix = sourceInfo.completeSuffix();
        extension = completeSuffix.isEmpty() ? QString() : "." + completeSuffix;
        const QString baseName = sourceInfo.completeBaseName().isEmpty() ? originalFileName : sourceInfo.completeBaseName();
        copyName = baseName + " - Copy" + extension;
    }

    QString candidate = dir.filePath(copyName);
    int suffix = 2;
    while (QFileInfo::exists(candidate)) {
        if (sourceInfo.isDir()) {
            candidate = dir.filePath(QString("%1 - Copy (%2)").arg(originalFileName).arg(suffix));
        } else {
            const QString baseName = sourceInfo.completeBaseName().isEmpty() ? originalFileName : sourceInfo.completeBaseName();
            candidate = dir.filePath(QString("%1 - Copy (%2)%3").arg(baseName).arg(suffix).arg(extension));
        }
        ++suffix;
    }

    return QDir::toNativeSeparators(candidate);
}

QString uniqueShortcutPathForTarget(const QString& sourcePath, const QString& desktopDir)
{
    const QFileInfo sourceInfo(sourcePath);
    QString baseName = sourceInfo.fileName();
    if (baseName.isEmpty()) {
        baseName = sourceInfo.completeBaseName();
    }
    if (baseName.isEmpty()) {
        baseName = "New Shortcut";
    }

    QDir dir(desktopDir);
    QString candidate = dir.filePath(baseName + " - Shortcut.lnk");
    int suffix = 2;
    while (QFileInfo::exists(candidate)) {
        candidate = dir.filePath(QString("%1 - Shortcut (%2).lnk").arg(baseName).arg(suffix));
        ++suffix;
    }
    return QDir::toNativeSeparators(candidate);
}

bool copyPathToExactDestination(const QString& sourcePath, const QString& destinationPath)
{
    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists()) {
        return false;
    }

    if (sourceInfo.isDir()) {
        QDir destParent = QFileInfo(destinationPath).dir();
        if (!destParent.exists() && !destParent.mkpath(".")) {
            return false;
        }

        if (!QDir().mkpath(destinationPath)) {
            return false;
        }

        QDir sourceDir(sourcePath);
        const QFileInfoList entries = sourceDir.entryInfoList(
            QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System,
            QDir::DirsFirst | QDir::Name
        );

        for (const QFileInfo& entry : entries) {
            const QString childDestination = QDir(destinationPath).filePath(entry.fileName());
            if (!copyPathToExactDestination(entry.absoluteFilePath(), childDestination)) {
                return false;
            }
        }
        return true;
    }

    QDir destParent = QFileInfo(destinationPath).dir();
    if (!destParent.exists() && !destParent.mkpath(".")) {
        return false;
    }

    if (QFile::exists(destinationPath) && !QFile::remove(destinationPath)) {
        return false;
    }

    return QFile::copy(sourcePath, destinationPath);
}

bool movePathToExactDestination(const QString& sourcePath, const QString& destinationPath)
{
    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists()) {
        return false;
    }

    QDir destParent = QFileInfo(destinationPath).dir();
    if (!destParent.exists() && !destParent.mkpath(".")) {
        return false;
    }

    if (QFileInfo::exists(destinationPath)) {
        if (QFileInfo(destinationPath).isDir()) {
            if (!QDir(destinationPath).removeRecursively()) {
                return false;
            }
        } else if (!QFile::remove(destinationPath)) {
            return false;
        }
    }

    if (sourceInfo.isDir()) {
        if (QDir().rename(sourcePath, destinationPath)) {
            return true;
        }

        if (!copyPathToExactDestination(sourcePath, destinationPath)) {
            return false;
        }
        return QDir(sourcePath).removeRecursively();
    }

    if (QFile::rename(sourcePath, destinationPath)) {
        return true;
    }

    if (!copyPathToExactDestination(sourcePath, destinationPath)) {
        return false;
    }
    return QFile::remove(sourcePath);
}

bool copyFilesToDesktopWithRule(const QStringList& sourcePaths, const QString& desktopDir)
{
    bool ok = true;
    for (const QString& filePath : sourcePaths) {
        const QString destinationPath = uniquePasteDestinationPath(filePath, desktopDir);
        ok = copyPathToExactDestination(filePath, destinationPath) && ok;
    }
    return ok;
}

bool createShellShortcut(const QString& sourcePath, const QString& shortcutPath)
{
    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninit = SUCCEEDED(initHr);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE) {
        return false;
    }

    IShellLinkW* shellLink = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&shellLink));
    if (FAILED(hr) || !shellLink) {
        if (shouldUninit) CoUninitialize();
        return false;
    }

    const std::wstring source = QDir::toNativeSeparators(sourcePath).toStdWString();
    shellLink->SetPath(source.c_str());
    shellLink->SetWorkingDirectory(QFileInfo(sourcePath).absolutePath().toStdWString().c_str());

    IPersistFile* persistFile = nullptr;
    hr = shellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persistFile));
    bool ok = false;
    if (SUCCEEDED(hr) && persistFile) {
        const std::wstring shortcut = QDir::toNativeSeparators(shortcutPath).toStdWString();
        ok = SUCCEEDED(persistFile->Save(shortcut.c_str(), TRUE));
        persistFile->Release();
    }

    shellLink->Release();
    if (shouldUninit) CoUninitialize();
    return ok;
}
}

// 静态成员初始化
ListView* ListView::m_instance = nullptr;
int ListItemData::s_targetIconSize = ListView::kMediumIconW;

ListView::ListView(QWidget* parent) : QWidget(parent),
    m_lastRightClickPos(-1, -1)  // 初始化右键点击位置为无效值
{
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

    // Set persistence path first so we can load viewMode before creating items
    setFocusPolicy(Qt::StrongFocus);
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
    
    // Clean up drag tip
    if (m_dragTip) {
        delete m_dragTip;
        m_dragTip = nullptr;
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
            if (event->keyboardModifiers() & Qt::ControlModifier) {
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
    if (event->source() != this) {
        if (m_dragTip) m_dragTip->hide();
        if (event->keyboardModifiers() & Qt::ControlModifier) {
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

    const bool isInternalDrag = (event->source() == this);
    if (!isInternalDrag) {
        if (m_dragTip) {
            m_dragTip->hide();
        }
        return;
    }

    // 1. Find if we are hovering over an item
    ListItem* hitItem = nullptr;
    if (m_iconShowRule.gridAlignment) {
        for (auto* item : m_items) {
            if (item && item->isVisible() && item->geometry().contains(event->pos()) && !m_selectedItems.contains(item)) {
                hitItem = item;
                break;
            }
        }
    }

    bool isCtrl = (event->keyboardModifiers() & Qt::ControlModifier);

    if (hitItem) {
        if (m_draggedItem) {
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
        } else if (isFolder && isDraggedDiskLetter) {
            htmlText = QString("<b style='color:%1'>在</b> <span>%2</span> <b style='color:%1'>里创建链接</b>")
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
        if (m_dragTip) {
            m_dragTip->hide();
        }
    }
}

void ListView::dropEvent(QDropEvent* event)
{
    RenameManager::instance()->cancelRename();
    //if (m_dragTip) m_dragTip->hide();

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
    const QStringList draggedPaths = draggedLocalFilePaths(mime);

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
        
        // 根据键盘修饰符判断是复制还是移动（Ctrl=复制, 无修饰符=移动）
        bool isCopyAction = (event->keyboardModifiers() & Qt::ControlModifier);

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
            // 根据键盘修饰符选择复制或移动文件到目标文件夹
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
        // 根据键盘修饰符判断是复制还是移动（Ctrl=复制, 无修饰符=移动）
        bool isCopyAction = (event->keyboardModifiers() & Qt::ControlModifier);
        
        // 外部拖放或强制复制
        if (event->source() != this || isCopyAction) {
            std::vector<std::wstring> srcPaths;
            for (const QString& localPath : draggedPaths) {
                if (!localPath.isEmpty()) srcPaths.push_back(localPath.toStdWString());
            }

            if (!srcPaths.empty()) {
                // 根据键盘修饰符选择复制或移动
                bool result;
                if (isCopyAction) {
                    result = CSystemApi::CopyFiles(srcPaths, QDir::toNativeSeparators(m_desktopDir).toStdWString());
                } else {
                    result = CSystemApi::MoveFiles(srcPaths, QDir::toNativeSeparators(m_desktopDir).toStdWString());
                }
                event->accept();
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
            // 保存图标位置到配置文件
            requestSaveBoxes();
        }
        clearCheckedItem();
    } else {
        // --- 外部拖入逻辑 ---
        QString desktopDir = m_desktopDir;
        std::vector<std::wstring> srcPaths;
        for (const QString& local : draggedPaths) {
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
    } else if (m_enablePersistence || useConfigState) { // Prefer config-loaded positions when a config layout exists
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
        const bool aIsDirLike = (a->type() == ObjectType::DiskLetter) || a->fileInfo().isDir();
        const bool bIsDirLike = (b->type() == ObjectType::DiskLetter) || b->fileInfo().isDir();
        QString extA = aIsDirLike ? QString() : a->fileInfo().suffix().toLower();
        QString extB = bIsDirLike ? QString() : b->fileInfo().suffix().toLower();
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

    QPoint pos;
    if (!takeReservedCreatedItemPosition(item->filePath(), pos)) {
        pos = generateItemPosition();
    }
    item->setItemSize(m_iconWidth, m_iconHeight);
    item->updateLocationInfo(pos);
    
    setupItemConnections(item);
    
    // 所有文件都加入 m_items 和 m_allItems
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
        // 从所有相关数组中移除item
        m_items.removeOne(item);
        m_allItems.removeOne(item);
        m_selectedItems.removeOne(item);
        
        // 确保从所属的 Box 中也移除该 item (防止悬空指针)
        if (!item->boxId().isEmpty()) {
            LinBox* box = BoxManager::instance()->findBoxById(item->boxId());
            if (box) {
                box->removeListItem(item);
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
    rebuildSpatialIndex();
    update();
    repaint();
    
}

void ListView::removeItemNoDelete(ListItem* item)
{
    if (item && item->isReady) {
        qDebug() << "[removeItemNoDelete]" << item->filePath();
        m_selectedItems.removeOne(item);
        
        // Keep the item in global render collections. Box only stores a reference.
        item->setVisible(false);
        item->hide();
    }
    
    // 强制更新界面
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

        int gridX = qMax(0, qRound((double)(pos.x() - m_marginLeft) / strideX));
        int gridY = qMax(0, qRound((double)(pos.y() - m_marginTop) / strideY));

        while (true) {
            QPoint candidate(
                m_marginLeft + gridX * strideX,
                m_marginTop + gridY * strideY
            );
            candidate.setX(qMax(m_marginLeft, qMin(candidate.x(), width() - m_iconWidth)));
            candidate.setY(qMax(m_marginTop, qMin(candidate.y(), height() - m_bottomPadding - m_iconHeight)));

            bool occupied = false;
            for (auto* item : m_items) {
                if (!item || !item->boxId().isEmpty()) continue;
                if (item->pos() == candidate) {
                    occupied = true;
                    break;
                }
            }

            if (!occupied) {
                for (auto it = m_pendingCreatedItemPositions.constBegin(); it != m_pendingCreatedItemPositions.constEnd(); ++it) {
                    if (it.value() == candidate) {
                        occupied = true;
                        break;
                    }
                }
            }

            if (!occupied) {
                return candidate;
            }

            ++gridY;
            if (m_marginTop + gridY * strideY > height() - m_bottomPadding - m_iconHeight) {
                gridY = 0;
                ++gridX;
            }
        }
    }

    pos.setX(qMax(m_marginLeft, qMin(pos.x(), width() - m_iconWidth)));
    pos.setY(qMax(m_marginTop, qMin(pos.y(), height() - m_bottomPadding - m_iconHeight)));

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

        pos += QPoint(16, 16);
        pos.setX(qMax(m_marginLeft, qMin(pos.x(), width() - m_iconWidth)));
        pos.setY(qMax(m_marginTop, qMin(pos.y(), height() - m_bottomPadding - m_iconHeight)));
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
    qDebug() << "[generateItemPosition] gridAlignment=" << m_iconShowRule.gridAlignment 
             << "m_lastRightClickPos=" << m_lastRightClickPos;
    
    // 非Grid布局且由UI操作触发的（右键菜单新建），直接使用鼠标右键位置
    if (!m_iconShowRule.gridAlignment && m_lastRightClickPos != QPoint(-1, -1)) {
        QPoint pos = m_lastRightClickPos;
        qDebug() << "[generateItemPosition] Using right-click position:" << pos;
        // 重置 m_lastRightClickPos，防止非UI创建的文件也使用该坐标
        m_lastRightClickPos = QPoint(-1, -1);
        return pos;
    }
    
    qDebug() << "[generateItemPosition] Using grid layout position";

    // Grid模式或非UI触发的UnGrid模式：寻找第一个可用的网格位置
    int x = m_marginLeft;
    int y = m_marginTop;

    while (true) {
        bool occupied = false;
        QRect currentRect(x, y, m_iconWidth, m_iconHeight);

        for (auto* item : m_items) {
            if (item && item->boxId().isEmpty() && (item->pos().x() > 0 || item->pos().y() > 0)) {
                // If the item has already been assigned a position (not 0,0), respect its occupation
                if (currentRect.intersects(item->geometry())) {
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
    const QStringList filePaths = clipboardLocalFilePaths();
    if (filePaths.isEmpty()) {
        qDebug() << "ListView: Paste skipped, clipboard has no local files";
        return;
    }

    const bool isCutOperation = clipboardContainsCutItems();
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
        ok = copyFilesToDesktopWithRule(filePaths, m_desktopDir);
    }

    qDebug() << "ListView: Paste triggered" << (isCutOperation ? "[move]" : "[copy]") << "result:" << ok;
    if (ok) {
        if (isCutOperation) {
            QGuiApplication::clipboard()->clear();
        }
    }
}

void ListView::onMenuPasteShortcut() {
    const QStringList filePaths = clipboardLocalFilePaths();
    if (filePaths.isEmpty()) {
        qDebug() << "ListView: Paste Shortcut skipped, clipboard has no local files";
        return;
    }

    int createdCount = 0;
    for (const QString& filePath : filePaths) {
        const QString shortcutPath = uniqueShortcutPathForTarget(filePath, m_desktopDir);
        if (createShellShortcut(filePath, shortcutPath)) {
            ++createdCount;
        }
    }

    qDebug() << "ListView: Paste Shortcut triggered, created:" << createdCount << "of" << filePaths.size();
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

        auto boxIt = m_pendingCreatedItemBoxes.find(newPath);
        if (boxIt != m_pendingCreatedItemBoxes.end()) {
            QPointer<LinBox> targetBox = boxIt.value();
            m_pendingCreatedItemBoxes.erase(boxIt);
            if (targetBox) {
                moveItemToBox(newPath, targetBox, false);
                targetBox->refreshLayout();
                targetBox->raise();
                if (m_iconShowRule.autoArrange) {
                    loadIconLayer();
                } else {
                    update();
                }
            }
        }
        
        // 保存持久化文件，记录新文件位置
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
            
            // 保存持久化文件，移除已删除文件记录
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
            if (targetBox) {
                moveItemToBox(newPath, targetBox, false);
                targetBox->refreshLayout();
                targetBox->raise();
                if (m_iconShowRule.autoArrange) {
                    loadIconLayer();
                } else {
                    update();
                }
            }
        }
        
        // 保存持久化文件，更新重命名文件记录
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
	
	// 先断开该 item 的所有信号连接，防止重复连接
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
			// Ctrl: 切换单个 item 选中状态
			if (m_selectedItems.contains(item)) {
				m_selectedItems.removeOne(item);
				item->unCheckSelf();
			} else {
				m_selectedItems.append(item);
				item->setFullNameExpanded(false);
				item->checkSelf();
			}
            m_wasSelectedBeforeClick = false; // Multi-select cancels rename timing
		} else if (modifiers & Qt::ShiftModifier) {
			// Shift: 追加选中
			if (!m_selectedItems.contains(item)) {
				m_selectedItems.append(item);
				item->setFullNameExpanded(false);
				item->checkSelf();
			}
            m_wasSelectedBeforeClick = false;
		} else {
			// 无修饰键：
			// 如果点击的 item 已经在多选集合里，保留整个选中组（准备多选拖动）
			// 否则清空选中，只选这一个
			if (!m_selectedItems.contains(item)) {
                RenameManager::instance()->cancelRename(); // New selection cancels rename
				clearCheckedItem();
				m_selectedItems.append(item);
				item->setFullNameExpanded(true);
				item->checkSelf();
                m_wasSelectedBeforeClick = false;
			} else {
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
        
        // 检查启动 QDrag (外部拖出) 的阈值
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
            
            // 保存图标位置到配置文件（拖拽结束后需要保存）
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
		
		// 保存图标位置到配置文件
		requestSaveBoxes();
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

		QLabel* ghostWidget = new QLabel(nullptr);
        ghostWidget->setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowTransparentForInput);
		ghostWidget->setAttribute(Qt::WA_TranslucentBackground);
		ghostWidget->setAttribute(Qt::WA_NoSystemBackground);
        ghostWidget->setAttribute(Qt::WA_TransparentForMouseEvents);
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
		clearCheckedItem();
		m_selectedItems.append(item);
		item->checkSelf();
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
    
    for (const auto& data : boxData) {
        const QString boxId = data.value("id").toString();
        LinBox* targetBox = BoxManager::instance()->findBoxById(boxId);
        if (!targetBox) {
            qWarning() << "[applyBoxAssignmentsFromConfig] Box not found:" << boxId;
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
            
            // 设置 boxId 并将 item 实际添加到 box 中
            item->setBoxId(boxId);
            item->hide();
            
            // 使用 addListItem 来添加，它会正确设置父窗口为 m_contentArea
            setupItemConnections(item, targetBox);
            targetBox->addListItem(item, false);
            
            qDebug() << "[applyBoxAssignmentsFromConfig] Item moved, new parent:" << item->parentWidget();
        }
    }
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
    m_isLoadingPersistence = true;
    m_forceConfigLayout = false;
    m_hasLoadedConfigLayout = false;

    QVector<LinBox*> existingBoxes;
    for (const QPointer<LinBox>& box : BoxManager::instance()->allBoxes()) {
        if (box) existingBoxes.append(box.data());
    }
    for (auto* box : existingBoxes) {
        if (box) {
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

    qDebug() << "Loaded. Persistence:" << m_enablePersistence;
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
    
    // Connect to BoxManager and ListView
    BoxManager::instance()->setupConnections(box, this);
    BoxManager::instance()->addBox(box);
    
    box->setShowType((LinBox::BoxShowType)showType);
    box->setGeometry(geom);
    if (collapsed) box->toggleCollapse();

    box->show();
    box->setGlassEffect(glass); // Apply blur AFTER window is fully visible to prevent black screen
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
    
    QVector<ListItem*> boxItems = box->items();
    for (auto* item : boxItems) {
        if (!item) continue;

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

void ListView::moveItemToBox(const QString& filePath, LinBox* targetBox, bool refresh) {
    BoxManager::instance()->moveItemToBox(filePath, targetBox, this);
    
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

bool ListView::importPathsToBox(const QStringList& sourcePaths, LinBox* targetBox, bool copyMode)
{
    if (sourcePaths.isEmpty() || !targetBox) {
        return false;
    }

    bool ok = true;
    QStringList createdPaths;

    for (const QString& sourcePath : sourcePaths) {
        if (sourcePath.isEmpty()) {
            continue;
        }

        const QString destinationPath = uniquePasteDestinationPath(sourcePath, m_desktopDir);
        const QString normalizedDestination = normalizeDesktopPath(destinationPath);

        bool singleOk = false;
        if (copyMode) {
            singleOk = copyPathToExactDestination(sourcePath, destinationPath);
        } else {
            singleOk = movePathToExactDestination(sourcePath, destinationPath);
        }

        if (singleOk) {
            createdPaths.append(normalizedDestination);
            m_pendingCreatedItemBoxes[normalizedDestination] = targetBox;
        } else {
            ok = false;
        }
    }

    return ok && !createdPaths.isEmpty();
}

QStringList ListView::resolveDraggedLocalPaths(const QMimeData* mimeData) const
{
    return draggedLocalFilePaths(mimeData);
}

bool ListView::handleDroppedPathsOnItem(const QStringList& sourcePaths, ListItem* targetItem, bool copyMode)
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

bool ListView::isItemInAnyBox(const QString& filePath) const {
    return BoxManager::instance()->isItemInAnyBox(filePath);
}

QString ListView::getBoxIdForItem(const QString& filePath) const {
    return BoxManager::instance()->getBoxIdForItem(filePath);
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

        item->setItemSize(m_iconWidth, m_iconHeight);
        item->initData();
        item->setLayoutMode(ListItem::LayoutMode::IconMode);
        // Note: Do NOT set parent here. It will be set later based on boxId.
        setupItemConnections(item);

        // 检查 item 是否已有有效位置
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

        const QString savedBoxId = item->boxId();
        if (!savedBoxId.isEmpty()) {
            LinBox* targetBox = BoxManager::instance()->findBoxById(savedBoxId);
            if (targetBox) {
                item->setBoxId(savedBoxId);
                item->hide();
                setupItemConnections(item, targetBox);
                targetBox->addListItem(item, false);
                continue;
            }
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
