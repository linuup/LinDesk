#pragma once

#include <QActionGroup>
#include <QMenu>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QIcon>
#include <QList>
#include <vector>

#include <ShellContextMenu.h>
#include "CommonEnum.h"
#include "app/LanguageManager.h"
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
    void showMenu(QPoint pos, const DesktopRootItemModel& model) const;
    void showMultObjectMenu(QPoint pos, const QVector<DesktopRootItemModel>& models) const;
    static bool isWindows11SystemContextMenuSupported();
    static bool isWindows11SystemContextMenuEnabled();
    static void setWindows11SystemContextMenuEnabled(bool enabled);

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
    void createBoxTriggered();
    void mapFolderTriggered();
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
    void onMapFolder();
    void onQuitApp();
    void onLinDesk();

private:
    struct CachedCustomMenuData {
        std::vector<ShellContextMenu::CustomMenuItem> items;
        std::vector<HBITMAP> bitmaps;
        bool valid = false;
    };

    RightMenu()
    {
        connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this](const QString&) {
            initializeCustomMenu();
        });
        initializeCustomMenu();
    }

    RightMenu(const RightMenu&) = delete;
    RightMenu& operator=(const RightMenu&) = delete;

    QMenu* createSortBySubMenu();
    QMenu* createViewSubMenu();
    QMenu* createUndoDeleteMenu() const;
    void connectMenuActionInvalidation(QMenu* menu);
    void invalidateCustomMenuCaches();
    QList<QAction*> filteredCustomMenuActions(bool includeCreateBox, bool includeLinDeskMenu) const;
    void clearCachedCustomMenuItems(CachedCustomMenuData& cache);
    void ensureCachedCustomMenuData(CachedCustomMenuData& cache, bool includeCreateBox, bool includeLinDeskMenu) const;

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
    mutable CachedCustomMenuData m_cachedDesktopMenuData;
    mutable CachedCustomMenuData m_cachedSharedBoxMenuData;
};
