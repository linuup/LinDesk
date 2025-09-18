// The following ifdef block is the standard way of creating macros which make exporting
// from a DLL simpler. All files within this DLL are compiled with the SYSTEMAPI_EXPORTS
// symbol defined on the command line. This symbol should not be defined on any project
// that uses this DLL. This way any other project whose source files include this file see
// SYSTEMAPI_API functions as being imported from a DLL, whereas this DLL sees symbols
// defined with this macro as being exported.
#pragma once
#include <windows.h>
#include "tchar.h"
#include <string>
#include <vector>
#include <fstream>
#include <locale>
#include <codecvt>
#include <iostream>
#include <CommCtrl.h>
#include "Tool.h"
using namespace std;

//图标结构体
struct IconItem
{
    std::string name;
    POINT position;
    LPITEMIDLIST pidl_p;
};
//  右键基本操作
enum File_RightMenu_Operations {
    Z_OPEN,
    Z_EDIT,
    Z_PRINT,
    Z_COPY,
    Z_CREATE_SHORTCUT,  //瞎写的,用不了
    Z_DELETE,   // can't recover
    Z_PROPERTIES    //依赖主进程,不能单独调用
};

enum PathType {
    InvalidPath = 0,    //无效路径
    FilePath = 1,   //文件路径
    DirectoryPath = 2   //文件夹路径
};

class CSystemApi {
public:
	CSystemApi(void);
    ~CSystemApi();

	// TODO: add your methods here.
    /*
    * 是不是快捷方式
    *
    */
    static bool IsLinkFile(const wchar_t* filePath);
    /**
     * 删除文件的原生API函数
    */
    static bool DeleteFileNative(const wchar_t* filePath);
    /**
     * 删除文件到回收站
     * @param comfirm 是否弹出确认删除框
    */
    static bool DeleteFileToRecycleBin(const wchar_t* filePath, bool comfirm = false);
    /**
     * 判断路径是文件还是目录，无效返回0，文件返回1，文件夹返回2
     * @param 路径
    */
    static PathType GetPathType(const wchar_t* path);
    /**
     * @brief 打开文件属性页面
     *
     * @param filePath 路径
     * @param operation 操作ID，见FileRightMenus枚举
    */
    static void File_RightMenuOperations(const TCHAR* filePath, File_RightMenu_Operations operation);

    //获取ListView32句柄
    static HWND FindSysListView32();
    /// <summary>
    /// 获取桌面的显示窗口
    /// </summary>
    /// <returns></returns>
    static HWND FindRealDesktop();
    /// <summary>
    /// 获取桌面句柄
    /// </summary>
    /// <returns></returns>
    HWND FindDesktop();
    /**
    * @brief 获取桌面所有图标位置及名称
    *
    * @param hwnd 桌面窗口句柄
    * @return 图标信息数组
    */
    vector<IconItem> GetDesktopIconsItems(HWND sysListView32);
    
    /// <summary>
    /// 从dll中获取字符
    /// </summary>
    /// <param name="filePath"></param>
    static const char* GetTextFromDll(const char* filePath,int index);
    /// <summary>
    /// 从dll中获取字符
    /// </summary>
    /// <param name="filePath">@C:\Program Files (x86)\Common Files\Microsoft Shared\MSEnv\1033\VSLauncherUI.dll,-1002</param>
    static const char* GetTextFromDll(const char* input);
private:
    //extern IconItem iconItem;
    static HWND global_SysListView32; //桌面图标句柄
    static HWND global_realDesktop; //桌面句柄
    //获取桌面图标窗口回调
    static BOOL CALLBACK FindSysListView32Internal(HWND hwnd, LPARAM lparam);
    //获取桌面窗口回调
    static BOOL CALLBACK FindRealDesktopInternal(HWND hwnd, LPARAM lparam);
    //读取桌面图标名称和坐标
    void GetSpecifyItemString(HANDLE MMEListViewProcess, HWND hwnd, int row, int column, TCHAR* buffer, int bufferSize);
};

/**
*
* 根据clsid打开对应程序，如程序面板，控制面板
*/
int OpenCLSID(const wchar_t* clsidStr);
//测试代码=========================================================
#ifdef _DEBUG
void WriteLocalFile(const vector<IconItem>* items);
#endif // DEBUG
