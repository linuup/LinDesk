#pragma once
#include <qlist.h>
#include <qicon.h>
#include <vector>
#include <qmenu.h>
#include <qsettings.h>
#include <ShellContextMenu.h>
#include "CommonEnum.h"
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
	/// 单个文件配置右键菜单
	/// </summary>
	/// <param name="fileWidget"></param>
	/// <returns></returns>
	//bool setupMenuForFileType(FileWidget* fileWidget);

	/// <summary>
	/// 多个文件配置右键菜单
	/// </summary>
	/// <param name="allChooseFiles"></param>
	/// <returns></returns>
	//bool setupMenuForFileType(QMap<QString, FileWidget*>* &allChooseFiles);

	/// <summary>
	/// Show RightMenu
	/// </summary>
	/// <param name="pos"></param>
	/// <param name="path">
	/// ShellPath : C:\\**\\
	/// </param>
	void showMenu(QPoint pos, QString path = nullptr) const;
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
signals:
	

private:
	RightMenu() {}; // 私有构造函数
	ShellContextMenu* shellContextMenu;

	// 删除拷贝构造函数和赋值操作符
	RightMenu(const RightMenu&) = delete;
	RightMenu& operator=(const RightMenu&) = delete;

	HWND hwnd;	//要显示的窗口句柄

	QString currentObjPath;		//右键要显示对象的路径
	//SpecialFolders sf;		//特殊文件夹类型
	ObjectType fileType;		//类型
	
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




	//    //右键不包含目录
	//    std::vector<QString> excludeMenus{ "cmd", "Powershell"};
	//    
	//    // 键数组：模板名称
	//    std::vector<QString> templateKeyNames{ "FileName", "NullFile" };
	//    static std::vector<std::unique_ptr<RightMenu>>  rightMenus;    //第三方菜单对象
	//    static std::vector<std::unique_ptr<RightMenuCreateFile>>  createFileRegs;   //新建文件注册表对象集合
	//
	//    /// <summary>
	//    /// 返回桌面背景菜单
	//    /// </summary>
	//    QMenu* GetDesktopBackMenu();
	//
	//public slots:
	//    void openPersonalize_Slot();    //打开个性化
	//    void openDisplay_Slot();    //打开显示设置
	//    void openThirdPartySoftwareMenu_Slot(QString command);    //第三方软件启动命令
	//
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


};

//class RightMenuCreateFile : public RightMenu {
//
//public:
//    RightMenuCreateFile(const QString& _rearName = "", const QString& _shellPath = "")
//        : rearName(_rearName), shellPath(_shellPath) 
//    {
//
//    }
//
//    //右键新建菜单不包含目录
//    static std::vector<QString> excludeCreatMenus;
//
//    /// <summary>
//    /// 获取所有新建文件的注册表对象
//    /// </summary>
//    static std::vector<std::unique_ptr<RightMenuCreateFile>> allRightNewFile_Regedit();
//
//    /// <summary>
//    /// 从注册表查找是否存在模板文件,存在返回模板文件路径，不存在返回None
//    /// </summary>
//    /// <param name="regPath"></param>注册表路径
//    /// <returns></returns>
//    QString findTemplatePath(QString regPath);
//
//    /// <summary>
//    /// 置顶文件和快捷方式
//    /// </summary>
//    static std::vector<std::unique_ptr<RightMenuCreateFile>> sortRightMenu(std::vector<std::unique_ptr<RightMenuCreateFile>>& menus);
//private:
//    //后缀名
//    QString rearName;
//    //快捷键
//    QString shortKey;
//    //模板文件
//    QString _hasTemplate;
//    //注册表路径
//    QString shellPath;
//
//    
//};
//
