#pragma once
#include "desktop/RightMenu.h"
#include "desktop/ListViewFileOpsHelper.h"
#include "desktop/Win11MenuBridge.h"
#include "app/AppFonts.h"
#include "services/AppPaths.h"
#include "services/DpiCoordinateHelper.h"
#include "services/ShellMenuExclusionStore.h"
#include "ui/Fluent/FluentDialog.h"
#include <QCoreApplication>
#include <QMenu>
#include <QDebug>
#include "../SystemApi/SystemApi.h"
#include <QProcess>
#include "../SystemApi/Tool.h"
#include <QtWinExtras/QtWin>
#include <QGuiApplication>
#include <QScreen>
#include <QClipboard>
#include <QCursor>
#include <QMimeData>
#include <QUrl>
#include <QApplication>
#include <QStyle>
#include <QPainter>
#include <QDir>
#include <QJsonObject>
#include <QJsonValue>
#include <QOperatingSystemVersion>
#include <QTimer>
#include <Windows.h>
#include <functional>
#include <shellapi.h>
#include <shobjidl.h>

namespace {
constexpr DWORD kClipboardPreferredDropEffectMove = 0x00000002;
constexpr int kWindows11BuildNumber = 22000;
const char kWindows11ContextMenuConfigKey[] = "Desktop/UseWindows11SystemContextMenu";
const char kShowNewBoxMenuConfigKey[] = "Desktop/ShowNewBoxMenu";
const char kShowLinDeskMenuConfigKey[] = "Desktop/ShowLinDeskMenu";
const char kShowThirdPartyShellMenuConfigKey[] = "Desktop/ShowThirdPartyShellMenu";

QString rightMenuConfigPath()
{
    return AppPaths::configFilePath();
}

bool isWindows11OrNewer()
{
    const QOperatingSystemVersion currentVersion = QOperatingSystemVersion::current();
    return currentVersion.majorVersion() > 10 ||
        (currentVersion.majorVersion() == 10 && currentVersion.microVersion() >= kWindows11BuildNumber);
}

bool useChineseShellMenuText()
{
    const LANGID uiLanguage = GetUserDefaultUILanguage();
    return PRIMARYLANGID(uiLanguage) == LANG_CHINESE;
}

QString shellMenuText(const char* englishSource, const char* chineseUtf8)
{
    return useChineseShellMenuText()
        ? QString::fromUtf8(chineseUtf8)
        : QString::fromUtf8(englishSource);
}

bool boolMenuSetting(const char* key, bool defaultValue)
{
    QSettings settings(rightMenuConfigPath(), QSettings::IniFormat);
    return settings.value(QString::fromLatin1(key), defaultValue).toBool();
}

void setBoolMenuSetting(const char* key, bool enabled)
{
    QSettings settings(rightMenuConfigPath(), QSettings::IniFormat);
    settings.setValue(QString::fromLatin1(key), enabled);
    settings.sync();
}

QPoint nativeCursorPosition()
{
    POINT cursor = {};
    GetCursorPos(&cursor);
    return QPoint(cursor.x, cursor.y);
}

QRect nativeWindowRect(HWND hwnd)
{
    RECT rect = {};
    if (!hwnd || !GetWindowRect(hwnd, &rect)) {
        return {};
    }

    return QRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
}

UINT nativeWindowDpi(HWND hwnd)
{
    return hwnd ? GetDpiForWindow(hwnd) : 0;
}

void logRightMenuDpiTrace(const char* source, HWND hwnd, const QPoint& pos)
{
    QScreen* screen = QGuiApplication::screenAt(pos);
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    qDebug() << "[DpiTrace][RightMenu]"
             << "source=" << source
             << "qtPos=" << pos
             << "nativePos=" << DpiCoordinateHelper::qtGlobalPointToNativePoint(hwnd, pos)
             << "qtCursor=" << QCursor::pos()
             << "nativeCursor=" << nativeCursorPosition()
             << "ownerHwnd=" << QString::number(reinterpret_cast<quintptr>(hwnd), 16)
             << "ownerRect=" << nativeWindowRect(hwnd)
             << "ownerDpi=" << nativeWindowDpi(hwnd)
             << "screenDpr=" << (screen ? screen->devicePixelRatio() : 0.0)
             << "screenGeometry=" << (screen ? screen->geometry() : QRect())
             << "screenAvailable=" << (screen ? screen->availableGeometry() : QRect());
}

bool isSupportedSpecialFolderMenu(SpecialFolders folder)
{
    return folder == SpecialFolders::ThisPc ||
        folder == SpecialFolders::RecycleBin ||
        folder == SpecialFolders::ControlPanel ||
        folder == SpecialFolders::Network ||
        folder == SpecialFolders::UserFolder;
}

void openUriWithShell(const wchar_t* uri)
{
    if (!uri || !*uri) {
        return;
    }

    const HINSTANCE result = ShellExecuteW(nullptr, L"open", uri, nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        qWarning() << "[RightMenu] ShellExecute failed for uri";
    }
}

void advanceDesktopWallpaper()
{
    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninit = SUCCEEDED(initHr);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE) {
        qWarning() << "[RightMenu] CoInitializeEx failed for wallpaper advance:" << Qt::hex << initHr;
        return;
    }

    IDesktopWallpaper* wallpaper = nullptr;
    const HRESULT createHr = CoCreateInstance(CLSID_DesktopWallpaper,
        nullptr,
        CLSCTX_ALL,
        IID_IDesktopWallpaper,
        reinterpret_cast<void**>(&wallpaper));
    if (SUCCEEDED(createHr) && wallpaper) {
        const HRESULT advanceHr = wallpaper->AdvanceSlideshow(nullptr, DSD_FORWARD);
        if (FAILED(advanceHr)) {
            qWarning() << "[RightMenu] AdvanceSlideshow failed:" << Qt::hex << advanceHr;
        }
        wallpaper->Release();
    } else {
        qWarning() << "[RightMenu] Failed to create IDesktopWallpaper:" << Qt::hex << createHr;
    }

    if (shouldUninit) {
        CoUninitialize();
    }
}

QString sanitizeIconFileName(QString value)
{
    for (QChar& ch : value) {
        if (!ch.isLetterOrNumber()) {
            ch = QLatin1Char('_');
        }
    }
    return value;
}

bool isUsableMenuBitmap(HBITMAP bitmap)
{
    const quintptr rawValue = reinterpret_cast<quintptr>(bitmap);
    return bitmap && rawValue > 16 && rawValue != static_cast<quintptr>(-1);
}

QString saveShellMenuBitmapIcon(HBITMAP bitmap, const QString& commandId, const QString& prefix)
{
    if (!isUsableMenuBitmap(bitmap)) {
        return QString();
    }

    const QString fileName = QStringLiteral("%1_%2.png").arg(prefix, sanitizeIconFileName(commandId));
    const QString basePath = QCoreApplication::applicationDirPath() + QStringLiteral("/Win11ContextMenu");
    const QString lightFile = basePath + QStringLiteral("/light/") + fileName;
    if (QFile::exists(lightFile)) {
        return fileName;
    }

    QPixmap pixmap = QtWin::fromHBITMAP(bitmap, QtWin::HBitmapAlpha);
    if (pixmap.isNull()) {
        return QString();
    }

    const QStringList themeFolders = {
        basePath + QStringLiteral("/light"),
        basePath + QStringLiteral("/dark")
    };

    bool saved = false;
    const QPixmap normalizedPixmap = pixmap.size().width() > 32 || pixmap.size().height() > 32
        ? pixmap.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation)
        : pixmap;

    for (const QString& folderPath : themeFolders) {
        QDir dir(folderPath);
        if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
            continue;
        }
        saved = normalizedPixmap.save(dir.filePath(fileName), "PNG") || saved;
    }

    return saved ? fileName : QString();
}

QString shellMenuDisplayText(const std::wstring& text)
{
    QString displayText = QString::fromStdWString(text);
    displayText.remove(QLatin1Char('&'));
    return displayText;
}
}

bool RightMenu::isWindows11SystemContextMenuSupported()
{
#if defined(_DEBUG)
    return true;
#else
    return isWindows11OrNewer();
#endif
}

RightMenu::~RightMenu()
{
    Win11MenuBridge::shutdownHost();
    invalidateCustomMenuCaches();
}

bool RightMenu::isWindows11SystemContextMenuEnabled()
{
    QSettings settings(rightMenuConfigPath(), QSettings::IniFormat);
    const QString configKey = QString::fromLatin1(kWindows11ContextMenuConfigKey);

    if (!isWindows11SystemContextMenuSupported()) {
        settings.setValue(configKey, false);
        settings.sync();
        return false;
    }

    if (!settings.contains(configKey)) {
        settings.setValue(configKey, true);
        settings.sync();
        return true;
    }

    return settings.value(configKey, true).toBool();
}

void RightMenu::setWindows11SystemContextMenuEnabled(bool enabled)
{
    QSettings settings(rightMenuConfigPath(), QSettings::IniFormat);
    const bool finalEnabled = isWindows11SystemContextMenuSupported() && enabled;
    settings.setValue(QString::fromLatin1(kWindows11ContextMenuConfigKey), finalEnabled);
    settings.sync();
    if (!finalEnabled) {
        Win11MenuBridge::shutdownHost();
    }
}

bool RightMenu::isNewBoxMenuItemEnabled()
{
    return boolMenuSetting(kShowNewBoxMenuConfigKey, true);
}

void RightMenu::setNewBoxMenuItemEnabled(bool enabled)
{
    setBoolMenuSetting(kShowNewBoxMenuConfigKey, enabled);
    getInstance()->invalidateCustomMenuCaches();
}

bool RightMenu::isLinDeskMenuEnabled()
{
    return boolMenuSetting(kShowLinDeskMenuConfigKey, true);
}

void RightMenu::setLinDeskMenuEnabled(bool enabled)
{
    setBoolMenuSetting(kShowLinDeskMenuConfigKey, enabled);
    getInstance()->invalidateCustomMenuCaches();
}

bool RightMenu::isThirdPartyShellMenuEnabled()
{
    return boolMenuSetting(kShowThirdPartyShellMenuConfigKey, true);
}

void RightMenu::setThirdPartyShellMenuEnabled(bool enabled)
{
    setBoolMenuSetting(kShowThirdPartyShellMenuConfigKey, enabled);
}
  
QMenu* RightMenu::createSortBySubMenu()
{
    QMenu* sortByMenu = new QMenu(shellMenuText("Sort by", "排序方式"), m_customMenu);
    m_sortByActionGroup = new QActionGroup(sortByMenu);
    QActionGroup* sortGroup = m_sortByActionGroup;
    
    // 2.1-Name
    this->m_sortByNameAction = new QAction(shellMenuText("Name", "名称"), sortByMenu);
    m_sortByNameAction->setCheckable(true);
    m_sortByNameAction->setActionGroup(sortGroup);
    connect(m_sortByNameAction, &QAction::triggered, this, &RightMenu::onSortByName);
    sortByMenu->addAction(m_sortByNameAction);
    
    // 2.2-Size
    this->m_sortBySizeAction = new QAction(shellMenuText("Size", "大小"), sortByMenu);
    m_sortBySizeAction->setCheckable(true);
    m_sortBySizeAction->setActionGroup(sortGroup);
    connect(m_sortBySizeAction, &QAction::triggered, this, &RightMenu::onSortBySize);
    sortByMenu->addAction(m_sortBySizeAction);
    
    // 2.3-Item type
    this->m_sortByTypeAction = new QAction(shellMenuText("Item type", "项目类型"), sortByMenu);
    m_sortByTypeAction->setCheckable(true);
    m_sortByTypeAction->setActionGroup(sortGroup);
    connect(m_sortByTypeAction, &QAction::triggered, this, &RightMenu::onSortByType);
    sortByMenu->addAction(m_sortByTypeAction);
    
    // 2.4-Date modified
    this->m_sortByDateAction = new QAction(shellMenuText("Date modified", "修改日期"), sortByMenu);
    m_sortByDateAction->setCheckable(true);
    m_sortByDateAction->setActionGroup(sortGroup);
    connect(m_sortByDateAction, &QAction::triggered, this, &RightMenu::onSortByDate);
    sortByMenu->addAction(m_sortByDateAction);

    // 不默认选中任何选项（Default 状态下均不勾选）
    
    return sortByMenu;
}

QMenu* RightMenu::createViewSubMenu()
{
    QMenu* viewMenu = new QMenu(shellMenuText("View", "查看"), m_customMenu);
    QActionGroup* viewGroup = new QActionGroup(viewMenu);

    // 2.1-Large icons
    this->m_largeIconsAction = new QAction(shellMenuText("Large icons", "大图标"), viewMenu);
    m_largeIconsAction->setCheckable(true);
    m_largeIconsAction->setActionGroup(viewGroup);
    connect(m_largeIconsAction, &QAction::triggered, this, &RightMenu::onLargeIcons);
    viewMenu->addAction(m_largeIconsAction);

    //1.2 Medium icons
    this->m_mediumIconsAction = new QAction(shellMenuText("Medium icons", "中等图标"), viewMenu);
    m_mediumIconsAction->setCheckable(true);
    m_mediumIconsAction->setActionGroup(viewGroup);
    connect(m_mediumIconsAction, &QAction::triggered, this, &RightMenu::onMediumIcons);
    viewMenu->addAction(m_mediumIconsAction);

    //1.3 Small icons
    this->m_smallIconsAction = new QAction(shellMenuText("Small icons", "小图标"), viewMenu);
    m_smallIconsAction->setCheckable(true);
    m_smallIconsAction->setActionGroup(viewGroup);
    connect(m_smallIconsAction, &QAction::triggered, this, &RightMenu::onSmallIcons);
    viewMenu->addAction(m_smallIconsAction);

    //1.4 Separator
    viewMenu->addSeparator();

    //1.5 Auto arrange icons
    this->m_autoArrangeIconsAction = new QAction(shellMenuText("Auto arrange icons", "自动排列图标"), viewMenu);
    m_autoArrangeIconsAction->setCheckable(true);
    connect(m_autoArrangeIconsAction, &QAction::triggered, this, &RightMenu::onAutoArrangeIcons);
    viewMenu->addAction(m_autoArrangeIconsAction);

    //1.6 Align icons to grid
    this->m_alignIconsToGridAction = new QAction(shellMenuText("Align icons to grid", "将图标与网格对齐"), viewMenu);
    m_alignIconsToGridAction->setCheckable(true);
    //m_alignIconsToGridAction->setChecked(true); // 默认开启网格对齐
    connect(m_alignIconsToGridAction, &QAction::triggered, this, &RightMenu::onAlignIconsToGrid);
    viewMenu->addAction(m_alignIconsToGridAction);

    //1.4 Separator
    viewMenu->addSeparator();

    //1.7 Show desktop icons
    this->m_showDesktopIconsAction = new QAction(shellMenuText("Show desktop icons", "显示桌面图标"), viewMenu);
    m_showDesktopIconsAction->setCheckable(true);
    connect(m_showDesktopIconsAction, &QAction::triggered, this, &RightMenu::onShowDesktopIcons);
    viewMenu->addAction(m_showDesktopIconsAction);

    //1.8 Double-click to hide desktop icons
    this->m_doubleClickedHideIconsAction = new QAction(shellMenuText("Double-click to hide icons", "双击隐藏图标"), viewMenu);
    m_doubleClickedHideIconsAction->setCheckable(true);
    connect(m_doubleClickedHideIconsAction, &QAction::triggered, this, &RightMenu::onDoubleClickedHideIcons);
    viewMenu->addAction(m_doubleClickedHideIconsAction);

    return viewMenu;
}

QMenu* RightMenu::createUndoDeleteMenu() const
{
    return nullptr;
}

void RightMenu::initializeCustomMenu()
{
    invalidateCustomMenuCaches();

    if (m_customMenu) {
        delete m_customMenu;
        m_customMenu = nullptr;
    }

    m_sortByActionGroup = nullptr;
    m_largeIconsAction = nullptr;
    m_mediumIconsAction = nullptr;
    m_smallIconsAction = nullptr;
    m_autoArrangeIconsAction = nullptr;
    m_alignIconsToGridAction = nullptr;
    m_showDesktopIconsAction = nullptr;
    m_doubleClickedHideIconsAction = nullptr;
    m_sortByDateAction = nullptr;
    m_sortByNameAction = nullptr;
    m_sortBySizeAction = nullptr;
    m_sortByTypeAction = nullptr;
    m_pasteAction = nullptr;
    m_createBoxAction = nullptr;
    m_mapFolderAction = nullptr;
    m_linDeskAction = nullptr;
    pasteShortcutAction = nullptr;

    m_customMenu = new QMenu();
    
    // 1-View 子菜单
    QMenu* viewMenu = createViewSubMenu();
    m_customMenu->addMenu(viewMenu);
    
    // 2-Sort by 子菜单
    QMenu* sortByMenu = createSortBySubMenu();
    m_customMenu->addMenu(sortByMenu);
    
    // 3-Refresh
    QAction* refreshAction = new QAction(shellMenuText("Refresh", "刷新"), m_customMenu);
    connect(refreshAction, &QAction::triggered, this, &RightMenu::onRefresh);
    m_customMenu->addAction(refreshAction);
    
    // 4-分割线
    m_customMenu->addSeparator();
    
    // 5-Paste
    m_pasteAction = new QAction(shellMenuText("Paste", "粘贴"), m_customMenu);
    m_pasteAction->setEnabled(false);
    connect(m_pasteAction, &QAction::triggered, this, &RightMenu::onPaste);
    m_customMenu->addAction(m_pasteAction);

    pasteShortcutAction = new QAction(shellMenuText("Paste shortcut", "粘贴快捷方式"), m_customMenu);
    pasteShortcutAction->setEnabled(false);
    connect(pasteShortcutAction, &QAction::triggered, this, &RightMenu::onPasteShortcut);
    m_customMenu->addAction(pasteShortcutAction);

    m_customMenu->addSeparator();
    QPixmap gridPix(16, 16);
    gridPix.fill(Qt::transparent);
    {
        QPainter p(&gridPix);
        p.setRenderHint(QPainter::Antialiasing, true);

        // 使用更粗的线条
        p.setPen(QPen(QColor(0, 120, 212), 2.0));
        p.setBrush(Qt::NoBrush);

        // 绘制加号
        p.drawLine(3, 8, 13, 8);  // 水平线
        p.drawLine(8, 3, 8, 13);  // 垂直线
    }
    const QIcon linDeskIcon(":/Assets/icon.png");
    QMenu* linDeskMenu = new QMenu(tr("LinDesk"), m_customMenu);

    //linDeskMenu->setIcon(QIcon::fromTheme("application-x-executable"));
    linDeskMenu->setIcon(QIcon(linDeskIcon));
    // 7-Create Box
    m_createBoxAction = new QAction(tr("New Box"), m_customMenu);
    
    // Create a grid icon for shell menu integration
    
    m_createBoxAction->setIcon(QIcon(gridPix));

    connect(m_createBoxAction, &QAction::triggered, this, QOverload<>::of(&RightMenu::onCreateBox));
    m_customMenu->addAction(m_createBoxAction);

    m_mapFolderAction = new QAction(tr("Map Folder"), linDeskMenu);
    m_mapFolderAction->setIcon(QIcon(":/Assets/folder-share.svg"));
    connect(m_mapFolderAction, &QAction::triggered, this, QOverload<>::of(&RightMenu::onMapFolder));
    linDeskMenu->addAction(m_mapFolderAction);

    QAction* organizeDesktopAction = new QAction(tr("Organize Desktop"), linDeskMenu);
    organizeDesktopAction->setIcon(QIcon(":/Assets/all1.svg"));
    connect(organizeDesktopAction, &QAction::triggered, this, &RightMenu::onOrganizeDesktop);
    linDeskMenu->addAction(organizeDesktopAction);
    m_linDeskAction = new QAction(tr("Setting"), linDeskMenu);
    m_linDeskAction->setIcon(QIcon(":/Assets/settings.svg"));
    connect(m_linDeskAction, &QAction::triggered, this, &RightMenu::onLinDesk);
    linDeskMenu->addAction(m_linDeskAction);
    
    // 8-Exit
    QAction* exitAction = new QAction(tr("Exit LinDesk"), linDeskMenu);
    exitAction->setIcon(QIcon(":/Assets/exit.svg"));
    connect(exitAction, &QAction::triggered, this, &RightMenu::onQuitApp);
    linDeskMenu->addAction(exitAction);

    m_customMenu->addMenu(linDeskMenu);

    m_customMenu->setFixedWidth(150);
    m_customMenu->setStyleSheet(QStringLiteral("QMenu { font-weight: bold; font-family: %1; }").arg(AppFonts::uiCssFamily()));
    connectMenuActionInvalidation(m_customMenu);
    applyCachedPasteActionsState();
}

void RightMenu::onLargeIcons() { m_largeIconsAction->setChecked(true); emit LargeIconsTriggered(); }
void RightMenu::onMediumIcons() { m_mediumIconsAction->setChecked(true); emit MediumIconsTriggered(); }
void RightMenu::onSmallIcons() { m_smallIconsAction->setChecked(true); emit SmallIconsTriggered(); }
void RightMenu::onAutoArrangeIcons() { emit AutoArrangeIconsTriggered(); }
void RightMenu::onAlignIconsToGrid() { emit AlignIconsToGridTriggered(); }
void RightMenu::onShowDesktopIcons() { emit ShowDesktopIconsTriggered(); }
void RightMenu::onDoubleClickedHideIcons() {emit DoubleClickedHideIconsTriggered();}

void RightMenu::onSortByName() { m_sortByNameAction->setChecked(true); emit sortByNameTriggered(); }
void RightMenu::onSortBySize() { m_sortBySizeAction->setChecked(true); emit sortBySizeTriggered(); }
void RightMenu::onSortByType() { m_sortByTypeAction->setChecked(true); emit sortByTypeTriggered(); }
void RightMenu::onSortByDate() {
    m_sortByActionGroup->blockSignals(true);
    if(m_sortByDateAction)
        m_sortByDateAction->setChecked(true);
    emit sortByDateTriggered();
    m_sortByActionGroup->blockSignals(false); 
} // Fixed typo in previous manual edit attempt if any

void RightMenu::onRefresh() { emit refreshTriggered(); }
void RightMenu::onPaste() { emit pasteTriggered(); }
void RightMenu::onPasteShortcut() { emit pasteShortcutTriggered(); }
void RightMenu::onCreateBox() {
    onCreateBox(m_lastMenuGlobalPos);
}
void RightMenu::onCreateBox(const QPoint& pos) {
    const QPoint effectivePos = (!pos.isNull() && pos != QPoint(-1, -1)) ? pos : m_lastMenuGlobalPos;
    emit createBoxTriggered(effectivePos);
}
void RightMenu::onMapFolder() {
    onMapFolder(m_lastMenuGlobalPos);
}
void RightMenu::onMapFolder(const QPoint& pos) {
    const QPoint effectivePos = (!pos.isNull() && pos != QPoint(-1, -1)) ? pos : m_lastMenuGlobalPos;
    emit mapFolderTriggered(effectivePos);
}
void RightMenu::onOrganizeDesktop() { emit organizeDesktopTriggered(); }
void RightMenu::onQuitApp() { emit quitAppTriggered(); }
void RightMenu::onLinDesk() { emit linDeskTriggered(); }

bool RightMenu::clipboardHasFileItems() const
{
    if (!ListViewFileOpsHelper::clipboardLocalFilePaths().isEmpty()) {
        return true;
    }

    return ListViewFileOpsHelper::clipboardHasVirtualFileItems();
}

bool RightMenu::clipboardContainsCutItems() const
{
    const QMimeData* mimeData = QApplication::clipboard()->mimeData();
    if (mimeData && mimeData->hasFormat("Preferred DropEffect")) {
        QByteArray data = mimeData->data("Preferred DropEffect");
        if (data.size() >= 4) {
            DWORD effect = *reinterpret_cast<const DWORD*>(data.data());
            return (effect & kClipboardPreferredDropEffectMove) != 0;
        }
    }

    const UINT format = RegisterClipboardFormatW(L"Preferred DropEffect");
    if (!format || !IsClipboardFormatAvailable(format) || !OpenClipboard(nullptr)) {
        return false;
    }

    bool isCutOperation = false;
    if (const HANDLE clipboardData = GetClipboardData(format)) {
        const void* locked = GlobalLock(clipboardData);
        if (locked) {
            const DWORD effect = *static_cast<const DWORD*>(locked);
            isCutOperation = (effect & kClipboardPreferredDropEffectMove) != 0;
            GlobalUnlock(clipboardData);
        }
    }

    CloseClipboard();
    return isCutOperation;
}

void RightMenu::updatePasteActionsState()
{
    const bool hasFileItems = clipboardHasFileItems();
    const bool hasVirtualFileItems = ListViewFileOpsHelper::clipboardHasVirtualFileItems();
    const bool isCutOperation = hasFileItems && clipboardContainsCutItems();

    m_cachedPasteEnabled = hasFileItems;
    m_cachedPasteShortcutEnabled = hasFileItems && !isCutOperation && !hasVirtualFileItems;

    if (m_pasteAction) {
        m_pasteAction->setEnabled(m_cachedPasteEnabled);
    }
    if (pasteShortcutAction) {
        pasteShortcutAction->setEnabled(m_cachedPasteShortcutEnabled);
    }

    updateCachedPasteActionItem(m_cachedDesktopMenuData);
    updateCachedPasteActionItem(m_cachedSharedBoxMenuData);
    updateCachedPasteActionItem(m_cachedFilteredDesktopMenuData);
    updateCachedPasteActionItem(m_cachedFilteredSharedBoxMenuData);
}

void RightMenu::preparePasteActionsForMenu() const
{
    const_cast<RightMenu*>(this)->updatePasteActionsState();
}

void RightMenu::applyCachedPasteActionsState() const
{
    if (m_pasteAction) {
        m_pasteAction->setEnabled(m_cachedPasteEnabled);
    }
    if (pasteShortcutAction) {
        pasteShortcutAction->setEnabled(m_cachedPasteShortcutEnabled);
    }
}

void RightMenu::refreshPasteActionsStateAsync() const
{
    if (m_pasteStateRefreshPending) {
        return;
    }

    m_pasteStateRefreshPending = true;
    QTimer::singleShot(0, const_cast<RightMenu*>(this), [this]() {
        auto* self = const_cast<RightMenu*>(this);
        self->updatePasteActionsState();
        m_pasteStateRefreshPending = false;
    });
}

void RightMenu::connectClipboardStateRefresh()
{
    if (m_clipboardRefreshConnected) {
        return;
    }

    m_clipboardRefreshConnected = true;

    if (QClipboard* clipboard = QGuiApplication::clipboard()) {
        connect(clipboard, &QClipboard::dataChanged, this, [this]() {
            refreshPasteActionsStateAsync();
        });
    }

    refreshPasteActionsStateAsync();
}

void RightMenu::connectMenuActionInvalidation(QMenu* menu)
{
    if (!menu) {
        return;
    }

    for (QAction* action : menu->actions()) {
        if (!action) {
            continue;
        }

        connect(action, &QAction::changed, this, [this]() {
            invalidateCustomMenuCaches();
        });

        if (QMenu* subMenu = action->menu()) {
            connectMenuActionInvalidation(subMenu);
        }
    }
}

void RightMenu::updateCachedPasteActionItem(CachedCustomMenuData& cache) const
{
    if (!cache.valid) {
        return;
    }
    for (auto& item : cache.items) {
        if (item.text == L"粘贴" || item.text == L"&Paste" || item.text == L"Paste") {
            item.enabled = m_cachedPasteEnabled;
        } else if (item.text == L"粘贴快捷方式" || item.text == L"Paste shortcut") {
            item.enabled = m_cachedPasteShortcutEnabled;
        }
    }
}

void RightMenu::updateCachedPasteActionItem(CachedFilteredCustomMenuData& cache) const
{
    if (!cache.valid) {
        return;
    }
    for (auto& item : cache.items) {
        if (item.text == L"粘贴" || item.text == L"&Paste" || item.text == L"Paste") {
            item.enabled = m_cachedPasteEnabled;
        } else if (item.text == L"粘贴快捷方式" || item.text == L"Paste shortcut") {
            item.enabled = m_cachedPasteShortcutEnabled;
        }
    }
}

void RightMenu::invalidateCustomMenuCaches()
{
    clearCachedCustomMenuItems(m_cachedDesktopMenuData);
    clearCachedCustomMenuItems(m_cachedSharedBoxMenuData);
    m_cachedFilteredDesktopMenuData.items.clear();
    m_cachedFilteredDesktopMenuData.valid = false;
    m_cachedFilteredSharedBoxMenuData.items.clear();
    m_cachedFilteredSharedBoxMenuData.valid = false;
    m_cachedWin11Menu.items = QJsonArray();
    m_cachedWin11Menu.valid = false;
}

QList<QAction*> RightMenu::filteredCustomMenuActions(bool includeCreateBox, bool includeLinDeskMenu) const
{
    QList<QAction*> filteredActions;
    if (!m_customMenu) {
        return filteredActions;
    }

    for (QAction* action : m_customMenu->actions()) {
        if (!action) {
            continue;
        }

        if (!includeCreateBox && action->text() == tr("New Box")) {
            continue;
        }

        if (!includeLinDeskMenu && action->menu() && action->menu()->title() == tr("LinDesk")) {
            continue;
        }

        filteredActions.append(action);
    }

    while (!filteredActions.isEmpty() && filteredActions.first()->isSeparator()) {
        filteredActions.removeFirst();
    }
    while (!filteredActions.isEmpty() && filteredActions.last()->isSeparator()) {
        filteredActions.removeLast();
    }

    for (int i = filteredActions.size() - 1; i > 0; --i) {
        if (filteredActions[i]->isSeparator() && filteredActions[i - 1]->isSeparator()) {
            filteredActions.removeAt(i);
        }
    }

    return filteredActions;
}

void RightMenu::clearCachedCustomMenuItems(CachedCustomMenuData& cache)
{
    for (HBITMAP bitmap : cache.bitmaps) {
        if (bitmap) {
            DeleteObject(bitmap);
        }
    }

    cache.bitmaps.clear();
    cache.items.clear();
    cache.valid = false;
}

void RightMenu::ensureCachedCustomMenuData(CachedCustomMenuData& cache, bool includeCreateBox, bool includeLinDeskMenu) const
{
    if (cache.valid) {
        return;
    }

    auto* self = const_cast<RightMenu*>(this);
    self->clearCachedCustomMenuItems(cache);
    cache.items = convertQActionsToCustomItems(filteredCustomMenuActions(includeCreateBox, includeLinDeskMenu), cache.bitmaps);
    cache.valid = true;
}

const std::vector<ShellContextMenu::CustomMenuItem>& RightMenu::desktopCustomMenuItems() const
{
    ensureCachedCustomMenuData(m_cachedDesktopMenuData, true, true);
    return m_cachedDesktopMenuData.items;
}

const std::vector<ShellContextMenu::CustomMenuItem>& RightMenu::sharedBoxCustomMenuItems() const
{
    ensureCachedCustomMenuData(m_cachedSharedBoxMenuData, false, false);
    return m_cachedSharedBoxMenuData.items;
}

static bool isShellMenuTextExcluded(const std::wstring& key, const std::wstring& rawText)
{
    return ShellMenuExclusionStore::contains(key, rawText);
}

std::vector<ShellContextMenu::CustomMenuItem> RightMenu::filterCustomItemsForExclusionKey(
    const std::vector<ShellContextMenu::CustomMenuItem>& items,
    const std::wstring& key)
{
    const std::vector<ShellMenuExclusionEntry> exclusions = ShellMenuExclusionStore::load(key);
    if (exclusions.empty()) {
        return items;
    }

    std::vector<ShellContextMenu::CustomMenuItem> filtered;
    filtered.reserve(items.size());
    std::function<void(const std::vector<ShellContextMenu::CustomMenuItem>&,
        std::vector<ShellContextMenu::CustomMenuItem>&,
        const std::vector<std::wstring>&)> appendFiltered;
    appendFiltered = [&](const std::vector<ShellContextMenu::CustomMenuItem>& sourceItems,
        std::vector<ShellContextMenu::CustomMenuItem>& targetItems,
        const std::vector<std::wstring>& parentMatchPath) {
    for (const ShellContextMenu::CustomMenuItem& item : sourceItems) {
        if (item.isSeparator) {
            targetItems.push_back(item);
            continue;
        }

        const std::wstring normalizedText = ShellContextMenu::NormalizeMenuText(item.text);
        const std::wstring matchText = ShellContextMenu::MatchMenuText(item.text);
        std::vector<std::wstring> matchPath = parentMatchPath;
        matchPath.push_back(matchText);
        bool excluded = false;
        for (const ShellMenuExclusionEntry& exclusion : exclusions) {
            if (ShellContextMenu::MenuEntryMatches(exclusion, item.text, normalizedText, matchText, matchPath)) {
                excluded = true;
                break;
            }
        }
        if (excluded) {
            continue;
        }

        ShellContextMenu::CustomMenuItem copy = item;
        if (!copy.children.empty()) {
            copy.children.clear();
            appendFiltered(item.children, copy.children, matchPath);
        }
        if (!copy.children.empty() || item.children.empty()) {
            targetItems.push_back(std::move(copy));
        }
    }
    };
    appendFiltered(items, filtered, {});

    while (!filtered.empty() && filtered.front().isSeparator) {
        filtered.erase(filtered.begin());
    }
    while (!filtered.empty() && filtered.back().isSeparator) {
        filtered.pop_back();
    }
    for (int i = static_cast<int>(filtered.size()) - 1; i > 0; --i) {
        if (filtered[i].isSeparator && filtered[i - 1].isSeparator) {
            filtered.erase(filtered.begin() + i);
        }
    }
    return filtered;
}

std::vector<ShellContextMenu::CustomMenuItem> RightMenu::filteredDesktopCustomMenuItems() const
{
    if (!m_cachedFilteredDesktopMenuData.valid) {
        m_cachedFilteredDesktopMenuData.items = filterCustomItemsForExclusionKey(desktopCustomMenuItems(), L"desktop");
        m_cachedFilteredDesktopMenuData.valid = true;
    }
    return m_cachedFilteredDesktopMenuData.items;
}

std::vector<ShellContextMenu::CustomMenuItem> RightMenu::filteredSharedBoxCustomMenuItems() const
{
    if (!m_cachedFilteredSharedBoxMenuData.valid) {
        m_cachedFilteredSharedBoxMenuData.items = filterCustomItemsForExclusionKey(sharedBoxCustomMenuItems(), L"box_background");
        m_cachedFilteredSharedBoxMenuData.valid = true;
    }
    return m_cachedFilteredSharedBoxMenuData.items;
}

std::vector<ShellMenuSnapshotItem> RightMenu::customItemsToPreviewItems(const std::vector<ShellContextMenu::CustomMenuItem>& items)
{
    std::vector<ShellMenuSnapshotItem> result;
    result.reserve(items.size());
    std::function<void(const std::vector<ShellContextMenu::CustomMenuItem>&,
        std::vector<ShellMenuSnapshotItem>&,
        const std::vector<std::wstring>&,
        const std::vector<std::wstring>&)> appendItems;
    appendItems = [&](const std::vector<ShellContextMenu::CustomMenuItem>& sourceItems,
        std::vector<ShellMenuSnapshotItem>& targetItems,
        const std::vector<std::wstring>& parentMenuPath,
        const std::vector<std::wstring>& parentMatchPath) {
    for (const ShellContextMenu::CustomMenuItem& customItem : sourceItems) {
        ShellMenuSnapshotItem item;
        item.isSeparator = customItem.isSeparator;
        if (item.isSeparator) {
            targetItems.push_back(item);
            continue;
        }
        item.text = customItem.text;
        item.normalizedText = ShellContextMenu::NormalizeMenuText(customItem.text);
        item.matchText = ShellContextMenu::MatchMenuText(customItem.text);
        item.menuPath = parentMenuPath;
        item.menuPath.push_back(item.text);
        item.matchPath = parentMatchPath;
        item.matchPath.push_back(item.matchText);
        item.source = L"lindesk";
        item.enabled = customItem.enabled;
        item.checked = customItem.checked;
        item.hBitmap = customItem.hBitmap;
        appendItems(customItem.children, item.children, item.menuPath, item.matchPath);
        targetItems.push_back(std::move(item));
    }
    };
    appendItems(items, result, {}, {});
    return result;
}

std::vector<ShellMenuSnapshotItem> RightMenu::shellMenuPreviewItems(const QString& key) const
{
    std::vector<ShellMenuSnapshotItem> items = ShellContextMenu::SnapshotMenuForExclusionKey(key.toStdWString());
    if (key == QStringLiteral("desktop")) {
        std::vector<ShellMenuSnapshotItem> customItems = customItemsToPreviewItems(desktopCustomMenuItems());
        items.insert(items.end(), customItems.begin(), customItems.end());
    } else if (key == QStringLiteral("box_background")) {
        std::vector<ShellMenuSnapshotItem> customItems = customItemsToPreviewItems(sharedBoxCustomMenuItems());
        items.insert(items.end(), customItems.begin(), customItems.end());
    }
    return items;
}

QJsonArray RightMenu::buildWindows11DesktopMenuItems(const ShellMenuCommandSession* shellSession) const
{
    auto actionItem = [](const QString& text,
                         const QString& commandId,
                         bool enabled = true,
                         bool checkable = false,
                         bool checked = false,
                         const QString& icon = QString(),
                         const QString& shortcut = QString()) {
        QJsonObject item;
        item.insert(QStringLiteral("type"), QStringLiteral("action"));
        item.insert(QStringLiteral("text"), text);
        item.insert(QStringLiteral("commandId"), commandId);
        item.insert(QStringLiteral("enabled"), enabled);
        item.insert(QStringLiteral("checkable"), checkable);
        item.insert(QStringLiteral("checked"), checked);
        if (!icon.isEmpty()) {
            item.insert(QStringLiteral("icon"), icon);
        }
        if (!shortcut.isEmpty()) {
            item.insert(QStringLiteral("shortcut"), shortcut);
        }
        return item;
    };

    auto separatorItem = []() {
        QJsonObject item;
        item.insert(QStringLiteral("type"), QStringLiteral("separator"));
        return item;
    };

    auto submenuItem = [](const QString& text, const QJsonArray& children, const QString& icon = QString()) {
        QJsonObject item;
        item.insert(QStringLiteral("type"), QStringLiteral("submenu"));
        item.insert(QStringLiteral("text"), text);
        item.insert(QStringLiteral("items"), children);
        if (!icon.isEmpty()) {
            item.insert(QStringLiteral("icon"), icon);
        }
        return item;
    };

    auto desktopActionItem = [&](const QString& text,
                                 const QString& commandId,
                                 const QString& icon = QString()) {
        return isShellMenuTextExcluded(L"desktop", text.toStdWString())
            ? QJsonObject()
            : actionItem(text, commandId, true, false, false, icon);
    };

    auto snapshotItemsToJson = [&](const std::vector<ShellMenuSnapshotItem>& snapshotItems, const QString& iconPrefix) {
        std::function<QJsonObject(const ShellMenuSnapshotItem&)> convertItem;
        convertItem = [&](const ShellMenuSnapshotItem& item) {
            if (item.isSeparator) {
                return separatorItem();
            }

            const QString iconKey = item.commandId.empty()
                ? QString::fromStdWString(item.text)
                : QString::fromStdString(item.commandId);
            const QString icon = saveShellMenuBitmapIcon(item.hBitmap, iconKey, iconPrefix);

            if (!item.children.empty()) {
                QJsonArray childItems;
                for (const ShellMenuSnapshotItem& child : item.children) {
                    childItems.append(convertItem(child));
                }
                return submenuItem(shellMenuDisplayText(item.text), childItems, icon);
            }

            return actionItem(shellMenuDisplayText(item.text),
                QString::fromStdString(item.commandId),
                item.enabled,
                false,
                item.checked,
                icon);
        };

        QJsonArray result;
        for (const ShellMenuSnapshotItem& item : snapshotItems) {
            result.append(convertItem(item));
        }
        return result;
    };

    QJsonArray viewItems;
    viewItems.append(actionItem(shellMenuText("Large icons", "大图标"), QStringLiteral("view_large"), true, true, m_largeIconsAction && m_largeIconsAction->isChecked(), QStringLiteral("LargeIcons.png"), QStringLiteral("Ctrl+Shift+2")));
    viewItems.append(actionItem(shellMenuText("Medium icons", "中等图标"), QStringLiteral("view_medium"), true, true, m_mediumIconsAction && m_mediumIconsAction->isChecked(), QStringLiteral("MediumIcons.png"), QStringLiteral("Ctrl+Shift+3")));
    viewItems.append(actionItem(shellMenuText("Small icons", "小图标"), QStringLiteral("view_small"), true, true, m_smallIconsAction && m_smallIconsAction->isChecked(), QStringLiteral("SmallIcons.png"), QStringLiteral("Ctrl+Shift+4")));
    viewItems.append(separatorItem());
    viewItems.append(actionItem(shellMenuText("Auto arrange icons", "自动排列图标"), QStringLiteral("auto_arrange_icons"), true, true, m_autoArrangeIconsAction && m_autoArrangeIconsAction->isChecked(), QStringLiteral("AutoArrangeIcons.png")));
    viewItems.append(actionItem(shellMenuText("Align icons to grid", "将图标与网格对齐"), QStringLiteral("align_icons_to_grid"), true, true, m_alignIconsToGridAction && m_alignIconsToGridAction->isChecked()));
    viewItems.append(separatorItem());
    viewItems.append(actionItem(shellMenuText("Show desktop icons", "显示桌面图标"), QStringLiteral("show_desktop_icons"), true, true, m_showDesktopIconsAction && m_showDesktopIconsAction->isChecked(), QStringLiteral("ShowDesktopIcons.png")));
    viewItems.append(actionItem(shellMenuText("Double click to hide icons", "双击隐藏图标"), QStringLiteral("double_click_hide_icons"), true, true, m_doubleClickedHideIconsAction && m_doubleClickedHideIconsAction->isChecked()));

    QJsonArray sortItems;
    sortItems.append(actionItem(shellMenuText("Name", "名称"), QStringLiteral("sort_name"), true, true, m_sortByNameAction && m_sortByNameAction->isChecked()));
    sortItems.append(actionItem(shellMenuText("Size", "大小"), QStringLiteral("sort_size"), true, true, m_sortBySizeAction && m_sortBySizeAction->isChecked()));
    sortItems.append(actionItem(shellMenuText("Item type", "项目类型"), QStringLiteral("sort_type"), true, true, m_sortByTypeAction && m_sortByTypeAction->isChecked()));
    sortItems.append(actionItem(shellMenuText("Date modified", "修改日期"), QStringLiteral("sort_date"), true, true, m_sortByDateAction && m_sortByDateAction->isChecked()));

    QJsonArray linDeskItems;
    const QJsonObject mapFolderItem = desktopActionItem(tr("Map Folder"), QStringLiteral("map_folder"), QStringLiteral("MapFolder.png"));
    if (!mapFolderItem.isEmpty()) {
        linDeskItems.append(mapFolderItem);
    }
    const QJsonObject organizeItem = desktopActionItem(tr("Organize Desktop"), QStringLiteral("organize_desktop"), QStringLiteral("OrganizeDesktop.png"));
    if (!organizeItem.isEmpty()) {
        linDeskItems.append(organizeItem);
    }
    const QJsonObject settingsItem = desktopActionItem(tr("Setting"), QStringLiteral("settings"), QStringLiteral("Settings.png"));
    if (!settingsItem.isEmpty()) {
        linDeskItems.append(settingsItem);
    }
    const QJsonObject exitItem = desktopActionItem(tr("Exit LinDesk"), QStringLiteral("exit_lindesk"), QStringLiteral("Exit.png"));
    if (!exitItem.isEmpty()) {
        if (!linDeskItems.isEmpty()) {
            linDeskItems.append(separatorItem());
        }
        linDeskItems.append(exitItem);
    }

    const QJsonArray newItems = shellSession
        ? snapshotItemsToJson(shellSession->newMenuItems(), QStringLiteral("ShellNew"))
        : QJsonArray();
    const QJsonArray thirdPartyItems = shellSession && isThirdPartyShellMenuEnabled()
        ? snapshotItemsToJson(shellSession->thirdPartyMenuItems(), QStringLiteral("ShellExternal"))
        : QJsonArray();
    const bool showNewBoxItem = isNewBoxMenuItemEnabled();
    const bool showLinDeskMenu = isLinDeskMenuEnabled();

    QJsonArray items;
    items.append(submenuItem(shellMenuText("View", "查看"), viewItems, QStringLiteral("View.png")));
    items.append(submenuItem(shellMenuText("Sort by", "排序方式"), sortItems, QStringLiteral("SortBy.png")));
    items.append(actionItem(shellMenuText("Refresh", "刷新"), QStringLiteral("refresh"), true, false, false, QStringLiteral("Refresh.png")));
    items.append(separatorItem());
    if (m_pasteAction && m_pasteAction->isEnabled()) {
        items.append(actionItem(shellMenuText("Paste", "粘贴"), QStringLiteral("paste")));
    }
    if (pasteShortcutAction && pasteShortcutAction->isEnabled()) {
        items.append(actionItem(shellMenuText("Paste shortcut", "粘贴快捷方式"), QStringLiteral("paste_shortcut")));
    }
    if (!newItems.isEmpty()) {
        items.append(submenuItem(shellMenuText("New", "新建"), newItems, QStringLiteral("New.png")));
    }
    if (showNewBoxItem && !isShellMenuTextExcluded(L"desktop", tr("New Box").toStdWString())) {
        items.append(actionItem(shellMenuText("New Box", "新建盒子"), QStringLiteral("create_box"), true, false, false, QStringLiteral("NewBox.png")));
    }
    if (showLinDeskMenu && !linDeskItems.isEmpty() && !isShellMenuTextExcluded(L"desktop", QStringLiteral("LinDesk").toStdWString())) {
        items.append(submenuItem(QStringLiteral("LinDesk"), linDeskItems, QStringLiteral("LinDesk.png")));
    }
    items.append(separatorItem());
    items.append(actionItem(shellMenuText("Next desktop background", "下一个背景"), QStringLiteral("next_desktop_background"), true, false, false, QStringLiteral("NextBackground.png")));
    items.append(actionItem(shellMenuText("Display settings", "显示设置"), QStringLiteral("display_settings"), true, false, false, QStringLiteral("DisplaySettings.png")));
    items.append(actionItem(shellMenuText("Personalize", "个性化"), QStringLiteral("personalize"), true, false, false, QStringLiteral("Personalize.png")));
    if (!thirdPartyItems.isEmpty()) {
        items.append(separatorItem());
        for (const QJsonValue& item : thirdPartyItems) {
            items.append(item);
        }
        items.append(separatorItem());
    } else {
        items.append(separatorItem());
    }
    items.append(actionItem(shellMenuText("Show more options", "显示更多选项"), QStringLiteral("show_more_options"), true, false, false, QStringLiteral("ShowMoreOptions.png")));

    return items;
}

void RightMenu::handleWindows11MenuCommand(const QString& commandId, const QPoint& pos, ShellMenuCommandSession* shellSession)
{
    if (commandId.isEmpty()) {
        return;
    }

    if (commandId.startsWith(QStringLiteral("shell_new:")) || commandId.startsWith(QStringLiteral("shell_external:"))) {
        if (!shellSession || !shellSession->invokeShellCommand(commandId.toStdString(), hwnd)) {
            qWarning() << "[RightMenu] failed to invoke Shell command:" << commandId;
        }
    } else if (commandId == QStringLiteral("view_large")) {
        onLargeIcons();
    } else if (commandId == QStringLiteral("view_medium")) {
        onMediumIcons();
    } else if (commandId == QStringLiteral("view_small")) {
        onSmallIcons();
    } else if (commandId == QStringLiteral("auto_arrange_icons")) {
        onAutoArrangeIcons();
    } else if (commandId == QStringLiteral("align_icons_to_grid")) {
        onAlignIconsToGrid();
    } else if (commandId == QStringLiteral("show_desktop_icons")) {
        onShowDesktopIcons();
    } else if (commandId == QStringLiteral("double_click_hide_icons")) {
        onDoubleClickedHideIcons();
    } else if (commandId == QStringLiteral("sort_name")) {
        onSortByName();
    } else if (commandId == QStringLiteral("sort_size")) {
        onSortBySize();
    } else if (commandId == QStringLiteral("sort_type")) {
        onSortByType();
    } else if (commandId == QStringLiteral("sort_date")) {
        onSortByDate();
    } else if (commandId == QStringLiteral("refresh")) {
        onRefresh();
    } else if (commandId == QStringLiteral("paste")) {
        onPaste();
    } else if (commandId == QStringLiteral("paste_shortcut")) {
        onPasteShortcut();
    } else if (commandId == QStringLiteral("next_desktop_background")) {
        advanceDesktopWallpaper();
    } else if (commandId == QStringLiteral("display_settings")) {
        openUriWithShell(L"ms-settings:display");
    } else if (commandId == QStringLiteral("personalize")) {
        openUriWithShell(L"ms-settings:personalization");
    } else if (commandId == QStringLiteral("create_box")) {
        onCreateBox(pos);
    } else if (commandId == QStringLiteral("map_folder")) {
        onMapFolder(pos);
    } else if (commandId == QStringLiteral("organize_desktop")) {
        onOrganizeDesktop();
    } else if (commandId == QStringLiteral("settings")) {
        onLinDesk();
    } else if (commandId == QStringLiteral("exit_lindesk")) {
        onQuitApp();
    } else if (commandId == QStringLiteral("show_more_options")) {
        QTimer::singleShot(80, this, [this, pos]() {
            showDefaultDesktopShellMenu(pos);
        });
    } else {
        qWarning() << "[RightMenu] unknown Win11 menu command:" << commandId;
    }
}

void RightMenu::showDefaultDesktopShellMenu(const QPoint& pos) const
{
    m_lastMenuGlobalPos = pos;
    ShellContextMenu scm;
    logRightMenuDpiTrace("defaultDesktopShellMenu", this->hwnd, pos);
    const QPoint nativePos = DpiCoordinateHelper::qtGlobalPointToNativePoint(this->hwnd, pos);
    POINT p = { nativePos.x(), nativePos.y() };
    scm.DisplayDesktopContextMenu();
    const auto exclusionKeys = ShellContextMenu::ExclusionKeysForPath(ShellContextMenu::OperationType::BackGround, nullptr);
    scm.setExclusions(ShellMenuExclusionStore::loadForKeys(exclusionKeys));
    if (m_customMenu) {
        scm.setCustomMenuItems(filteredDesktopCustomMenuItems());
    }
    scm.ShowMenu(this->hwnd, &p, nullptr);
}

bool RightMenu::showWindows11DesktopMenu(const QPoint& pos) const
{
    m_lastMenuGlobalPos = pos;
    logRightMenuDpiTrace("windows11DesktopMenu", this->hwnd, pos);
    ShellContextMenu shellMenu;
    ShellMenuCommandSession shellSession;
    const auto exclusionKeys = ShellContextMenu::ExclusionKeysForPath(ShellContextMenu::OperationType::BackGround, nullptr);
    shellMenu.setExclusions(ShellMenuExclusionStore::loadForKeys(exclusionKeys));
    const bool hasShellNewSnapshot = shellMenu.prepareDesktopBackgroundMenuSnapshot(hwnd, shellSession);
    if (!hasShellNewSnapshot) {
        qWarning() << "[RightMenu] Shell New snapshot unavailable";
    }

    QString commandId;
    const QPoint nativePos = DpiCoordinateHelper::qtGlobalPointToNativePoint(this->hwnd, pos);
    if (!Win11MenuBridge::showDesktopMenu(nativePos, buildWindows11DesktopMenuItems(&shellSession), &commandId)) {
        return false;
    }

    if (!commandId.isEmpty()) {
        const_cast<RightMenu*>(this)->handleWindows11MenuCommand(commandId, pos, &shellSession);
    }
    return true;
}

void RightMenu::showMenu(QPoint pos, const DesktopRootItemModel& model, bool extendedVerbs) const
{
    m_lastMenuGlobalPos = pos;
    ShellContextMenu scm;
    logRightMenuDpiTrace("showMenu", this->hwnd, pos);
    const QPoint nativePos = DpiCoordinateHelper::qtGlobalPointToNativePoint(this->hwnd, pos);
    POINT p = { nativePos.x(), nativePos.y() };
    const POINT* position = &p;
	if (this->fileType == ObjectType::BackGround) {
		preparePasteActionsForMenu();
		if (RightMenu::isWindows11SystemContextMenuEnabled()) {
            if (showWindows11DesktopMenu(pos)) {
                return;
            }
		}

		scm.DisplayDesktopContextMenu();
        const auto exclusionKeys = ShellContextMenu::ExclusionKeysForPath(ShellContextMenu::OperationType::BackGround, nullptr);
        scm.setExclusions(ShellMenuExclusionStore::loadForKeys(exclusionKeys));
		// 将自定义菜单项注册到Shell背景菜单中
		if (m_customMenu) {
			scm.setCustomMenuItems(filteredDesktopCustomMenuItems());
			scm.ShowMenu(this->hwnd, position, nullptr,
				extendedVerbs ? ShellContextMenu::Flags::ExtendedVerbs : ShellContextMenu::Flags::Standard);
			return;
		}
	}else if (this->fileType == ObjectType::File) {
		scm.setOperationType(ShellContextMenu::OperationType::file);
	}else if (this->fileType == ObjectType::Folder_ObjectType || this->fileType == ObjectType::DiskLetter) {
		scm.setOperationType(ShellContextMenu::OperationType::folder);
	}else if (this->fileType == ObjectType::SpecialFolder) {
		scm.setOperationType(ShellContextMenu::OperationType::specialFolder);
	}
    if (this->fileType == ObjectType::SpecialFolder && isSupportedSpecialFolderMenu(model.specialFolder)) {
        scm.DisplaySpecialFolderContextMenu(model.specialFolder);
    } else {
        scm.DisplayItemContextMenu(model.itemData.pidl());
    }
    scm.setExclusions(ShellMenuExclusionStore::loadForKeys(exclusionKeysForModel(model)));
    scm.ShowMenu(this->hwnd, position, nullptr,
        extendedVerbs ? ShellContextMenu::Flags::ExtendedVerbs : ShellContextMenu::Flags::Standard);
}
#if 0
void RightMenu::showBoxMenu(QPoint pos) const {
	ShellContextMenu scm;
	POINT p = { pos.x(),pos.y() };
	const POINT* position = &p;

	const_cast<RightMenu*>(this)->updatePasteActionsState();
	scm.DisplayBoxBackgroundContextMenu();

	// 将自定义菜单项注册到Shell背景菜单中
	if (m_customMenu) {
		std::vector<HBITMAP> tempBitmaps;
		scm.setCustomMenuItems(convertQActionsToCustomItems(m_customMenu->actions(), tempBitmaps));
		scm.ShowMenu(this->hwnd, position, nullptr);

		for (HBITMAP h : tempBitmaps) {
			DeleteObject(h);
		}
		return;
	}
	scm.ShowMenu(this->hwnd, position, nullptr);
}

#endif

std::vector<ShellContextMenu::CustomMenuItem> RightMenu::convertQActionsToCustomItems(const QList<QAction*>& actions, std::vector<HBITMAP>& tempBitmaps) {
	std::vector<ShellContextMenu::CustomMenuItem> items;
	for (QAction* action : actions) {
		ShellContextMenu::CustomMenuItem item;
		if (action->isSeparator()) {
			item.isSeparator = true;
		}
		else {
			item.text = action->text().toStdWString();
			QIcon icon = action->icon();
			if (icon.isNull() && action->menu()) {
				icon = action->menu()->icon();
			}
			if (!icon.isNull()) {
				QPixmap pix = icon.pixmap(16, 16);
				HBITMAP hBmp = QtWin::toHBITMAP(pix, QtWin::HBitmapPremultipliedAlpha);
				item.hBitmap = hBmp;
				tempBitmaps.push_back(hBmp);
			}

			if (action->menu()) {
				item.children = convertQActionsToCustomItems(action->menu()->actions(), tempBitmaps);
			}
			else {
				item.checkable = action->isCheckable();
				item.checked = action->isChecked();
				item.enabled = action->isEnabled();
				QAction* capturedAction = action;
				item.callback = [capturedAction]() {
					capturedAction->trigger();
				};
			}
		}
		items.push_back(item);
	}
	return items;
}

std::vector<ShellContextMenu::CustomMenuItem> RightMenu::exportCustomMenuItems(
    const QMenu* menu,
    std::vector<HBITMAP>& tempBitmaps,
    bool includeCreateBox,
    bool includeLinDeskMenu)
{
    if (!menu) {
        return {};
    }

    QList<QAction*> filteredActions;
    for (QAction* action : menu->actions()) {
        if (!action) {
            continue;
        }

        if (!includeCreateBox && action->text() == QObject::tr("New Box")) {
            continue;
        }

        if (!includeLinDeskMenu && action->menu() && action->menu()->title() == QObject::tr("LinDesk")) {
            continue;
        }

        filteredActions.append(action);
    }

    while (!filteredActions.isEmpty() && filteredActions.first()->isSeparator()) {
        filteredActions.removeFirst();
    }
    while (!filteredActions.isEmpty() && filteredActions.last()->isSeparator()) {
        filteredActions.removeLast();
    }

    for (int i = filteredActions.size() - 1; i > 0; --i) {
        if (filteredActions[i]->isSeparator() && filteredActions[i - 1]->isSeparator()) {
            filteredActions.removeAt(i);
        }
    }

    return convertQActionsToCustomItems(filteredActions, tempBitmaps);
}

void RightMenu::showMultObjectMenu(QPoint pos, const QVector<DesktopRootItemModel>& models, bool extendedVerbs) const
{
    vector<LPITEMIDLIST> pidls;
    pidls.reserve(models.size());
    for (const auto& model : models) {
        if (model.itemData.pidl()) {
            pidls.push_back(model.itemData.pidl());
        }
    }

    logRightMenuDpiTrace("showMultObjectMenu", this->hwnd, pos);
    const QPoint nativePos = DpiCoordinateHelper::qtGlobalPointToNativePoint(this->hwnd, pos);
    POINT p = { nativePos.x(), nativePos.y() };
    const POINT* position = &p;
    ShellContextMenu scm;
    scm.DisplayMultipleContextMenu(pidls);
    scm.ShowMenu(this->hwnd, position, nullptr,
        extendedVerbs ? ShellContextMenu::Flags::ExtendedVerbs : ShellContextMenu::Flags::Standard);
}

std::vector<std::wstring> RightMenu::exclusionKeysForModel(const DesktopRootItemModel& model) const
{
    if (model.objectType == ObjectType::Folder_ObjectType) {
        return ShellContextMenu::ExclusionKeysForPath(ShellContextMenu::OperationType::folder, reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(model.filePath).utf16()));
    }
    if (model.objectType == ObjectType::DiskLetter) {
        return ShellContextMenu::ExclusionKeysForPath(ShellContextMenu::OperationType::diskLetter, reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(model.filePath).utf16()));
    }
    if (model.objectType == ObjectType::SpecialFolder) {
        return {};
    }
    if (model.objectType == ObjectType::File) {
        const QString nativePath = QDir::toNativeSeparators(model.filePath);
        const auto type = model.isShortcut
            ? ShellContextMenu::OperationType::link
            : ShellContextMenu::OperationType::file;
        return ShellContextMenu::ExclusionKeysForPath(type, reinterpret_cast<const wchar_t*>(nativePath.utf16()));
    }
    return {};
}

const wchar_t* RightMenu::objPath() const
{
	return reinterpret_cast<const wchar_t*>(currentObjPath.utf16());
}

const wchar_t* RightMenu::objPath(QString str) const
{
	return reinterpret_cast<const wchar_t*>(str.utf16());
}

void RightMenu::setLargeIconsChecked(bool checked)
{
    if (checked) {
        m_largeIconsAction->setChecked(true);
        m_mediumIconsAction->setChecked(false);
        m_smallIconsAction->setChecked(false);
    } else {
        m_largeIconsAction->setChecked(false);
    }
}

void RightMenu::setMediumIconsChecked(bool checked)
{
    if (checked) {
        m_largeIconsAction->setChecked(false);
        m_mediumIconsAction->setChecked(true);
        m_smallIconsAction->setChecked(false);
    } else {
        m_mediumIconsAction->setChecked(false);
    }
}

void RightMenu::setSmallIconsChecked(bool checked)
{
    if (checked) {
        m_largeIconsAction->setChecked(false);
        m_mediumIconsAction->setChecked(false);
        m_smallIconsAction->setChecked(true);
    } else {
        m_smallIconsAction->setChecked(false);
    }
}

void RightMenu::setShowDesktopIconsChecked(bool checked)
{
    if (checked) {
        m_showDesktopIconsAction->setChecked(true);
    } else {
        m_showDesktopIconsAction->setChecked(false);
    }
}

void RightMenu::setAutoArrangeIconsChecked(bool checked)
{
    if (checked) {
        m_autoArrangeIconsAction->setChecked(true);
    } else {
        m_autoArrangeIconsAction->setChecked(false);
    }
}

void RightMenu::setAlignIconsToGridChecked(bool checked)
{
    if (checked) {
        m_alignIconsToGridAction->setChecked(true);
    } else {
        m_alignIconsToGridAction->setChecked(false);
    }
}

bool RightMenu::isAutoArrangeIconsChecked() const
{
    return m_autoArrangeIconsAction ? m_autoArrangeIconsAction->isChecked() : false;
}

bool RightMenu::isAlignIconsToGridChecked() const
{
    return m_alignIconsToGridAction ? m_alignIconsToGridAction->isChecked() : false;
}

void RightMenu::setDoubleClickedHideIconsChecked(bool checked)
{
    if(checked) {
        m_doubleClickedHideIconsAction->setChecked(true);
    }else {
        m_doubleClickedHideIconsAction->setChecked(false);
    }
}

// Sort-by setters — QActionGroup 已保证互斥，只需 setChecked(true)
void RightMenu::setSortByNameChecked(bool checked)
{
    if (m_sortByNameAction) m_sortByNameAction->setChecked(checked);
}

void RightMenu::setSortBySizeChecked(bool checked)
{
    if (m_sortBySizeAction) m_sortBySizeAction->setChecked(checked);
}

void RightMenu::setSortByTypeChecked(bool checked)
{
    if (m_sortByTypeAction) m_sortByTypeAction->setChecked(checked);
}

void RightMenu::setSortByDateChecked(bool checked)
{
    if (m_sortByDateAction) m_sortByDateAction->setChecked(checked);
}
