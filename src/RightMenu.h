#pragma once
#include <qlist.h>
#include <qicon.h>
#include <vector>
#include <qmenu.h>
#include <qsettings.h>
#include <ShellContextMenu.h>
#include "CommonEnum.h"
#include <ListItem.h>
#define HKEY_ROOT "HKEY_CLASSES_ROOT\\"
#define HKEY_USER "HKEY_CURRENT_USER\\"


class RightMenuCreateFile;
/// <summary>
/// From Regedit get desktop right menu
/// 文件和背景右键类 
/// 默认打开背景菜单
/// 
/// 根据类型和位置 打开对应文件菜单：
/// 文件路径
/// 文件夹
/// 系统特殊文件夹
/// 盘符
/// </summary>
class RightMenu : public QObject
{
	Q_OBJECT
public:

	//单例类 返回具体菜单
	static RightMenu* getInstance()
	{
		static RightMenu instance;
		return &instance;
	}
	
	/// <summary>
	/// 初始化自定义右键菜单项
	/// </summary>
	void initializeCustomMenu();
	
	/// <summary>
	/// Opem Item System RightClick Menu or SpecialFolder Menu
	/// </summary>
	/// <param name="pos"></param>
	/// <param name="listItem"></param>
	void showMenu(QPoint pos, ListItem* listItem) const;

	/// <summary>
	/// Open Items System RightClick Menu
	/// </summary>
	/// <param name="pos"></param>
	/// <param name="items"></param>
	void RightMenu::showMultObjectMenu(QPoint pos, vector<ListItem*> items) const;

	/// <summary>
	/// 设置显示的窗口句柄
	/// </summary>
	/// <param name="h"></param>
	void setHWND(const HWND h) {
		this->hwnd = h;
	}

	/// <summary>
	/// 设置右键菜单显示类型
	/// </summary>
	/// <param name="fileType"></param>
	void setShowType(ObjectType fileType) {
		this->fileType = fileType;
	}

	const wchar_t* objPath() const;

	const wchar_t* objPath(QString str) const;

	ShellContextMenu* sContextMenu;
	
	// 获取自定义菜单
	QMenu* getCustomMenu() const { return m_customMenu; }


	//set icons view action check state
	void setLargeIconsChecked(bool checked);
	void setMediumIconsChecked(bool checked);
	void setSmallIconsChecked(bool checked);
	//set showDesktopIcons action check state
	void setShowDesktopIconsChecked(bool checked);
	void setAutoArrangeIconsChecked(bool checked);
	void setAlignIconsToGridChecked(bool checked);
	void setDoubleClickedHideIconsChecked(bool checked);
	// set sort-by action check state
	void setSortByNameChecked(bool checked);
	void setSortBySizeChecked(bool checked);
	void setSortByTypeChecked(bool checked);
	void setSortByDateChecked(bool checked);

	bool isAutoArrangeIconsChecked() const;
	bool isAlignIconsToGridChecked() const;
signals:
	// 自定义菜单项的信号
	// 1-View
	void LargeIconsTriggered();
	void MediumIconsTriggered();
	void SmallIconsTriggered();
	void AutoArrangeIconsTriggered();
	void AlignIconsToGridTriggered();
	void ShowDesktopIconsTriggered();
	void DoubleClickedHideIconsTriggered();
	// 2-Sort by
	void sortByNameTriggered();
	void sortBySizeTriggered();
	void sortByTypeTriggered();
	void sortByDateTriggered();
	// 3-Refresh
	void refreshTriggered();
	// 4-Paste
	void pasteTriggered();
	// 5-Paste shortcut
	void pasteShortcutTriggered();

	// 7-Create Box
	void createBoxTriggered();

	// 7.5-Organize Desktop
	void organizeDesktopTriggered();
	
	// 8-Exit
	void quitAppTriggered();

	// 9-LinDesk Settings
	void linDeskTriggered();

	void onEmbedToDesktop();

public slots:
	// 菜单项槽函数
	// View
	void onLargeIcons();
	void onMediumIcons();
	void onSmallIcons();
	void onAutoArrangeIcons();
	void onAlignIconsToGrid();
	void onShowDesktopIcons();
	void onDoubleClickedHideIcons();
	// Sort by
	void onSortByName();
	void onSortBySize();
	void onSortByType();
	void onSortByDate();
	// Refresh
	void onRefresh();

	void onOrganizeDesktop();
	void onPaste();
	void onPasteShortcut();
	void onCreateBox();
	void onQuitApp();
	void onLinDesk();

	

private:
	RightMenu() {
		initializeCustomMenu(); // 在构造函数中初始化自定义菜单
	}; // 私有构造函数
	ShellContextMenu* shellContextMenu;

	// 删除拷贝构造函数和赋值操作符
	RightMenu(const RightMenu&) = delete;
	RightMenu& operator=(const RightMenu&) = delete;

	HWND hwnd;	//要显示的窗口句柄

	QString currentObjPath;		//右键要显示对象的路径
	
	ObjectType fileType;		//类型
	
	QMenu* m_customMenu;			// 自定义右键菜单
	

	// 菜单项 Action 指针，用于管理选中状态
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
	QAction* m_linDeskAction = nullptr;
	QAction* pasteShortcutAction = nullptr;
	QAction* m_embedToDesktopAction = nullptr;

	/// <summary>
	/// 创建排序方式子菜单
	/// </summary>
	QMenu* createSortBySubMenu();

	/// <summary>
	/// 创建View子菜单
	/// </summary>
	QMenu* createViewSubMenu();

	QMenu* createUndoDeleteMenu() const;
	void updatePasteActionsState();
	bool clipboardHasFileItems() const;
	bool clipboardContainsCutItems() const;
	
	/// <summary>
	/// 
	/// </summary>
	/// <param name="hwnd"></param>
	/// <param name="pidlParent"></param>
	/// <param name=""></param>
		/*unique_pidl_absolute pidlDesktop;
		HRESULT hr = SHGetKnownFolderIDList(FOLDERID_Desktop, 0, NULL, wil::out_param(pidlDesktop));
		fileContextMenu = pidlDesktop;*/

};

