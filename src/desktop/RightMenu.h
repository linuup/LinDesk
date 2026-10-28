#pragma once

#include <QActionGroup>
#include <QMenu>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QIcon>
#include <QJsonArray>
#include <QList>
#include <vector>
#include <memory>

#include <ShellContextMenu.h>
#include "CommonEnum.h"
#include "desktop/DesktopRootItemModel.h"

#define HKEY_ROOT "HKEY_CLASSES_ROOT\\"
#define HKEY_USER "HKEY_CURRENT_USER\\"

class RightMenuCreateFile;

class RightMenu : public QObject
{
    Q_OBJECT
public:
    static RightMenu* getInstance()
    {
        static RightMenu instance;
        return &instance;
    }

    ~RightMenu() override;

    void initializeCustomMenu();
    void prewarmWin11DesktopSession() const;
    void showMenu(QPoint pos, const DesktopRootItemModel& model, bool extendedVerbs = false) const;
    void showMultObjectMenu(QPoint pos, const QVector<DesktopRootItemModel>& models, bool extendedVerbs = false) const;
    static bool isWindows11SystemContextMenuSupported();
    static bool isWindows11SystemContextMenuEnabled();
    static void setWindows11SystemContextMenuEnabled(bool enabled);
    static bool isNewBoxMenuItemEnabled();
    static void setNewBoxMenuItemEnabled(bool enabled);
    static bool isLinDeskMenuEnabled();
    static void setLinDeskMenuEnabled(bool enabled);
    static bool isThirdPartyShellMenuEnabled();
    static void setThirdPartyShellMenuEnabled(bool enabled);
    static bool isShellMenuExclusionEnabled();
    static void setShellMenuExclusionEnabled(bool enabled);

    static std::vector<ShellContextMenu::CustomMenuItem> convertQActionsToCustomItems(const QList<QAction*>& actions, std::vector<HBITMAP>& tempBitmaps);
    static std::vector<ShellContextMenu::CustomMenuItem> exportCustomMenuItems(
        const QMenu* menu,
        std::vector<HBITMAP>& tempBitmaps,
        bool includeCreateBox = true,
        bool includeLinDeskMenu = true);

    void setHWND(const HWND h) { hwnd = h; }
    void setShowType(ObjectType type) { fileType = type; }

    const wchar_t* objPath() const;
    const wchar_t* objPath(QString str) const;

    ShellContextMenu* sContextMenu = nullptr;
    QMenu* getCustomMenu() const { return m_customMenu; }
    const std::vector<ShellContextMenu::CustomMenuItem>& desktopCustomMenuItems() const;
    const std::vector<ShellContextMenu::CustomMenuItem>& sharedBoxCustomMenuItems() const;
    std::vector<ShellMenuSnapshotItem> shellMenuPreviewItems(const QString& key) const;
    std::vector<ShellContextMenu::CustomMenuItem> filteredDesktopCustomMenuItems() const;
    std::vector<ShellContextMenu::CustomMenuItem> filteredSharedBoxCustomMenuItems() const;
    void invalidateCustomMenuCaches();
    static std::vector<ShellContextMenu::CustomMenuItem> filterCustomItemsForExclusionKey(
        const std::vector<ShellContextMenu::CustomMenuItem>& items,
        const std::wstring& key);

    void setLargeIconsChecked(bool checked);
    void setMediumIconsChecked(bool checked);
    void setSmallIconsChecked(bool checked);
    void setShowDesktopIconsChecked(bool checked);
    void setAutoArrangeIconsChecked(bool checked);
    void setAlignIconsToGridChecked(bool checked);
    void setDoubleClickedHideIconsChecked(bool checked);
    void setSortByNameChecked(bool checked);
    void setSortBySizeChecked(bool checked);
    void setSortByTypeChecked(bool checked);
    void setSortByDateChecked(bool checked);

    bool isAutoArrangeIconsChecked() const;
    bool isAlignIconsToGridChecked() const;
    void preparePasteActionsForMenu() const;
    void updatePasteActionsState();
    bool clipboardHasFileItems() const;
    bool clipboardContainsCutItems() const;

signals:
    void LargeIconsTriggered();
    void MediumIconsTriggered();
    void SmallIconsTriggered();
    void AutoArrangeIconsTriggered();
    void AlignIconsToGridTriggered();
    void ShowDesktopIconsTriggered();
    void DoubleClickedHideIconsTriggered();
    void sortByNameTriggered();
    void sortBySizeTriggered();
    void sortByTypeTriggered();
    void sortByDateTriggered();
    void refreshTriggered();
    void pasteTriggered();
    void pasteShortcutTriggered();
    void createBoxTriggered(const QPoint& pos = QPoint());
    void mapFolderTriggered(const QPoint& pos = QPoint());
    void organizeDesktopTriggered();
    void quitAppTriggered();
    void linDeskTriggered();
    void onEmbedToDesktop();

public slots:
    void onLargeIcons();
    void onMediumIcons();
    void onSmallIcons();
    void onAutoArrangeIcons();
    void onAlignIconsToGrid();
    void onShowDesktopIcons();
    void onDoubleClickedHideIcons();
    void onSortByName();
    void onSortBySize();
    void onSortByType();
    void onSortByDate();
    void onRefresh();
    void onOrganizeDesktop();
    void onPaste();
    void onPasteShortcut();
    void onCreateBox();
    void onCreateBox(const QPoint& pos);
    void onMapFolder();
    void onMapFolder(const QPoint& pos);
    void onQuitApp();
    void onLinDesk();

private:
    struct CachedCustomMenuData {
        std::vector<ShellContextMenu::CustomMenuItem> items;
        std::vector<HBITMAP> bitmaps;
        bool valid = false;
    };

    struct CachedFilteredCustomMenuData {
        std::vector<ShellContextMenu::CustomMenuItem> items;
        bool valid = false;
    };

    struct CachedWin11DesktopMenu {
        QJsonArray items;
        qint64 timestamp = 0;
        bool valid = false;
        bool largeChecked = false;
        bool mediumChecked = false;
        bool smallChecked = false;
        bool autoArrangeChecked = false;
        bool alignGridChecked = false;
        bool showDesktopChecked = false;
        bool doubleClickHideChecked = false;
        bool pasteEnabled = false;
        bool pasteShortcutEnabled = false;
    };

    RightMenu()
    {
        initializeCustomMenu();
        connectClipboardStateRefresh();
    }

    RightMenu(const RightMenu&) = delete;
    RightMenu& operator=(const RightMenu&) = delete;

    QMenu* createSortBySubMenu();
    QMenu* createViewSubMenu();
    QMenu* createUndoDeleteMenu() const;
    void connectMenuActionInvalidation(QMenu* menu);
    QList<QAction*> filteredCustomMenuActions(bool includeCreateBox, bool includeLinDeskMenu) const;
    void clearCachedCustomMenuItems(CachedCustomMenuData& cache);
    void ensureCachedCustomMenuData(CachedCustomMenuData& cache, bool includeCreateBox, bool includeLinDeskMenu) const;
    void applyCachedPasteActionsState() const;
    void refreshPasteActionsStateAsync() const;
    void connectClipboardStateRefresh();
    bool showWindows11DesktopMenu(const QPoint& pos) const;
    void showDefaultDesktopShellMenu(const QPoint& pos) const;
    QJsonArray buildWindows11DesktopMenuItems(const ShellMenuCommandSession* shellSession = nullptr) const;
    void handleWindows11MenuCommand(const QString& commandId, const QPoint& pos, ShellMenuCommandSession* shellSession = nullptr);
    std::vector<std::wstring> exclusionKeysForModel(const DesktopRootItemModel& model) const;
    static std::vector<ShellMenuSnapshotItem> customItemsToPreviewItems(const std::vector<ShellContextMenu::CustomMenuItem>& items);

    ShellContextMenu* shellContextMenu = nullptr;
    HWND hwnd = nullptr;
    QString currentObjPath;
    ObjectType fileType = ObjectType::BackGround;
    QMenu* m_customMenu = nullptr;

    QAction* m_largeIconsAction = nullptr;
    QAction* m_mediumIconsAction = nullptr;
    QAction* m_smallIconsAction = nullptr;
    QAction* m_autoArrangeIconsAction = nullptr;
    QAction* m_alignIconsToGridAction = nullptr;
    QAction* m_showDesktopIconsAction = nullptr;
    QAction* m_doubleClickedHideIconsAction = nullptr;
    QAction* m_sortByDateAction = nullptr;
    QAction* m_sortByNameAction = nullptr;
    QAction* m_sortBySizeAction = nullptr;
    QAction* m_sortByTypeAction = nullptr;
    QAction* m_pasteAction = nullptr;
    QAction* m_createBoxAction = nullptr;
    QAction* m_mapFolderAction = nullptr;
    QAction* m_linDeskAction = nullptr;
    QAction* pasteShortcutAction = nullptr;
    QAction* m_embedToDesktopAction = nullptr;
    QActionGroup* m_sortByActionGroup = nullptr;
    void updateCachedPasteActionItem(CachedCustomMenuData& cache) const;
    void updateCachedPasteActionItem(CachedFilteredCustomMenuData& cache) const;

    mutable CachedCustomMenuData m_cachedDesktopMenuData;
    mutable CachedCustomMenuData m_cachedSharedBoxMenuData;
    mutable CachedFilteredCustomMenuData m_cachedFilteredDesktopMenuData;
    mutable CachedFilteredCustomMenuData m_cachedFilteredSharedBoxMenuData;
    mutable CachedWin11DesktopMenu m_cachedWin11Menu;
    mutable std::unique_ptr<ShellMenuCommandSession> m_prewarmedWin11Session;
    mutable bool m_cachedPasteEnabled = false;
    mutable bool m_cachedPasteShortcutEnabled = false;
    mutable bool m_pasteStateRefreshPending = false;
    bool m_clipboardRefreshConnected = false;
    mutable QPoint m_lastMenuGlobalPos;
};
