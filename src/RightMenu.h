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
	QMenu* getCustomMenu() const { return customMenu; }

signals:
	// 自定义菜单项的信号
	void sortByNameTriggered();
	void sortBySizeTriggered();
	void sortByTypeTriggered();
	void sortByDateTriggered();
	void refreshTriggered();
	void pasteTriggered();
	void pasteShortcutTriggered();
	void viewTriggered();

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
	
	QMenu* customMenu;			// 自定义右键菜单
	
	/// <summary>
	/// 创建排序方式子菜单
	/// </summary>
	QMenu* createSortBySubMenu() const;
	
	/// <summary>
	/// 
	/// </summary>
	/// <param name="hwnd"></param>
	/// <param name="pidlParent"></param>
	/// <param name=""></param>
	RightMenu(HWND hwnd, PCIDLIST_ABSOLUTE pidlParent)
	{
		/*unique_pidl_absolute pidlDesktop;
		HRESULT hr = SHGetKnownFolderIDList(FOLDERID_Desktop, 0, NULL, wil::out_param(pidlDesktop));
		fileContextMenu = pidlDesktop;*/
	}

private slots:
	// 菜单项槽函数
	void onSortByName();
	void onSortBySize();
	void onSortByType();
	void onSortByDate();
	void onRefresh();
	void onPaste();
	void onPasteShortcut();
	void onView();
};

	//protected:
	//    QMenu* menu;    
	//    QString menuName;   //选项名
	//    QString command;    //选项命令
	//    QString iconPath; //图标
	//    int iconIndex = 0; //图标索引
	//    //QString regPath;    //注册表位置
	//private:
	//    int rightMenuWidth = 1920 / 8;  //菜单最小宽度
	//
	//    ShellContextMenu *fileContextMenu;      //文件右键菜单对象
	//
	//    /// <summary>
	//    /// 获取右键菜单项
	//    /// </summary>
	//    std::vector<std::unique_ptr<RightMenu>> rightMenu_Program();
	//        
	//    /// <summary>
	//    /// 把所有子项填充到items里
	//    /// </summary>
	//    /// <param name="qs"></param>注册表路径
	//    /// <param name="items"></param>空数组
	//    void getChildsItemPath(QSettings& qs, std::vector<QString>& items);



