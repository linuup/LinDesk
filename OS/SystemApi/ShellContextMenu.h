#pragma once
#include <windows.h>
#include <optional>
#include <functional>
#include <map>
#include "CommonEnum.h"
#include <wil/com.h>
#include <ShlObj.h>
#include "Tool.h"
#include "MenuHelper.h"
#include "WindowSubclassWrapper.h"
#include "ResourceHelper.h"
#include "resources.h"
#include "SystemApi.h"
#include "shtypes.h"

//定义为Unicode



class PidlHelper {

public:

};

using unique_pidl_absolute = wil::unique_cotaskmem_ptr<std::remove_pointer_t<PIDLIST_ABSOLUTE>>;
using unique_pidl_relative = wil::unique_cotaskmem_ptr<std::remove_pointer_t<PIDLIST_RELATIVE>>;
using unique_pidl_child = wil::unique_cotaskmem_ptr<std::remove_pointer_t<PITEMID_CHILD>>;


class SysMenuHelper {
private:

public:
    static HMENU AddSubMenuItem(HMENU hMenu, UINT id, const std::wstring& text, HMENU subMenu, UINT pos, bool enable);
    static void AddStringItem(HMENU hMenu, UINT id, const std::wstring& text, UINT pos, bool enable);
    static void AddSeparator(HMENU hMenu, UINT pos, bool enable);
    static void EnableItem(HMENU hMenu, UINT id, bool enable);

};

static const int MIN_SHELL_MENU_ID = 1;
static const int MAX_SHELL_MENU_ID = 1000;
static const int CUSTOM_MENU_ID_BASE = 10001;




/// <summary>
/// 右键菜单类：
/// 1.初始化 COM
/// 2.获取对象的IShellFolder接口
/// 2.获取对象的PIDL
/// 4.通过PIDL获取IContextMenu接口
/// 5.创建空的弹出式菜单：CreatePopupMenu
/// 6.调用 QueryContextMenu 将上下文菜单项添加到弹出式菜单中
/// 7.显示上下文菜单：TrackPopupMenuEx
/// 8.执行对应菜单命令：InvokeCommand
/// </summary>
//
//class ShellContextMenu : private boost::noncopyable
class ShellContextMenu
{
public:

    enum class Flags
    {
        Standard = 0,
        Rename = 1 << 0,
        ExtendedVerbs = 1 << 1
    };


    //操作类型
    enum class OperationType
    {
        file,//文件
        folder,//文件夹
        link,//快捷方式
        specialFolder,//特殊文件夹
        MultObject,//多个项目，文件和文件号组合
        diskLetter,//盘符
        BackGround//背景
    };
    void setOperationType(OperationType type) {
        operationType = type;
    }
    
    ShellContextMenu() {}

    ~ShellContextMenu() {};


    

private:
    // 防止拷贝构造和赋值操作
    //ShellContextMenu(ShellContextMenu const&) = delete;
    //ShellContextMenu& operator=(ShellContextMenu const&) = delete;
public:

    /// <summary>
    /// 自定义菜单项（追加到背景菜单末尾）
    /// </summary>
    struct CustomMenuItem {
        std::wstring text;       // 显示文本
        std::function<void()> callback; // 选中时的回调
        std::vector<CustomMenuItem> children; // 子菜单项
        bool isSeparator = false; // 是否为分隔线
        bool checkable = false;   // 是否可选
        bool checked = false;     // 是否已选中
        bool enabled = true;      // 是否可用
        HBITMAP hBitmap = nullptr;    // 图标位图句柄
    };

    /// <summary>
    /// 设置要追加到背景右键菜单的自定义菜单项
    /// </summary>
    void setCustomMenuItems(const std::vector<CustomMenuItem>& items) {
        m_customItems = items;
    }

    void ShowMenu(HWND hwnd, const POINT *pt, IUnknown* site, Flags flags = Flags::Standard);


    /// <summary>	
    /// 去掉腾讯桌面整理的菜单
    /// </summary>
    void RemoveTencentDeskGoMenu(HMENU hMenu);


    /*
    获取快捷方式在父文件夹中的 ITEMIDLIST 结构
    获取快捷方式的父文件夹对象对应的 IShellFolder 接口指针
    从IShellFolder获取IContextMenu接口
    IContextMenu得到菜单并调用
    */
    //为桌面生成右键上下文菜单
    void DisplayDesktopContextMenu();

    void DisplayItemContextMenu(LPITEMIDLIST pidl);
    //为快捷方式生成右键上下文菜单
    void DisplayLinkContextMenu(const wchar_t* path);

    // 为文件生成右键上下文菜单
    void DisplayFileContextMenu(const wchar_t* path);

    // 为文件夹生成右键上下文菜单
    void DisplayFolderContextMenu(const wchar_t* folderPath);

    // 为系统特殊文件夹生成右键上下文菜单
    void DisplaySpecialFolderContextMenu(SpecialFolders sf);

    // 为盘符生成右键上下文菜单
    void DisplayDriveContextMenu(const wchar_t* driveLetter, HWND hWnd, int xPos, int yPos);

    //选择多个文件生成右键上下文菜单
    void DisplayMultipleContextMenu(vector<LPITEMIDLIST> pidls);
  
private:
    OperationType operationType = OperationType::BackGround;

    wil::com_ptr_nothrow<IContextMenu3> MaybeGetShellContextMenu(HWND hwnd) const;	//获取文件Shell上下文接口

    std::optional<std::string> MaybeGetFilesystemDirectory() const;		//获取执行菜单Shell时，结构体需要的路径

    LRESULT ParentWindowSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    wil::unique_hmenu BuildViewMenu();
    wil::unique_hmenu BuildSortMenu();


    unique_pidl_absolute m_pidlParent;
    const std::vector<unique_pidl_child> m_pidlItems;
    std::vector<CustomMenuItem> m_customItems; // 自定义追加菜单项
    std::map<UINT, std::function<void()>> m_customCallbackMap; // 映射命令ID到回调
    //ShellContextMenuHandler* const m_handler;
    wil::com_ptr_nothrow<IContextMenu3> m_contextMenu;

    IContextMenu* m_pContextMenu;


    // 获取桌面IShellFolder文件夹接口...
    IShellFolder* m_pDesktopFolder;
    //IShellFolder文件夹接口...
    IShellFolder* m_pShellFolder;
    LPITEMIDLIST m_pPIDL = nullptr;

    // 解析路径获取IShellFolder
    IShellFolder* GetIshellFolderByPath(const wchar_t* path);

    /// <summary>
    /// Get Desktop IshellFolder
    /// </summary>
    /// <returns></returns>
    bool GetDesktop_IshellFolder();

    /// <summary>
    /// 从IShellFolder获取IContextMenu接口
    /// PIDL转长指针,此函数需要PIDL对应的IShellFolder接口绑定转换
    /// </summary>
    /// <param name="pidl"></param>
    /// <param name="pShellFolder"></param>
    /// <returns></returns>
    IContextMenu3* IContextMenuFromIShellFolder(PCIDLIST_ABSOLUTE pidl, IShellFolder* pShellFolder);
    
};
DEFINE_ENUM_FLAG_OPERATORS(ShellContextMenu::Flags);

HRESULT GetRootPidl(PIDLIST_ABSOLUTE* pidl);	//获取根节点的Pidl Desktop_Pidl
HRESULT BindToIdl(PIDLIST_ABSOLUTE pidl, REFIID riid, void** ppv);
