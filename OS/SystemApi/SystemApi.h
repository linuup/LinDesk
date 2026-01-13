// The following ifdef block is the standard way of creating macros which make exporting
// from a DLL simpler. All files within this DLL are compiled with the SYSTEMAPI_EXPORTS
// symbol defined on the command line. This symbol should not be defined on any project
// that uses this DLL. This way any other project whose source files include this file see
// SYSTEMAPI_API functions as being imported from a DLL, whereas this DLL sees symbols
// defined with this macro as being exported.
#pragma once
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include "tchar.h"
#include <string>
#include <vector>
#include <fstream>
#include <locale>
#include <codecvt>
#include <iostream>
#include <CommCtrl.h>
#include "Tool.h"
#include "CommonEnum.h"
#include <codecvt>
using namespace std;
// //1 隐藏 0 显示
#define MY_COMPUTER_CLS   "{20D04FE0-3AEA-1069-A2D8-08002B30309D}"//计算机/此电脑
#define NETWORK_CLS 	  "{F02C1A0D-BE21-4350-88B0-7367FC96EF3C}"//网络
#define RECYCLE_BIN_CLS   "{645FF040-5081-101B-9F08-00AA002F954E}"//回收站
#define CONTROL_PANEL_CLS "{26EE0668-A00A-44D7-9371-BEB064C98683}"//控制面板(Type)
#define CONTROL_PANEL_CLS2 "{5399E694-6CE5-4D6C-8FCE-1D8870FDCBA0}"//控制面板(分类视图)
#define USER_FOLDER_CLS   "{59031a47-3f72-44a7-89c5-5595fe6b30ee}"//用户文件夹
#define LIBRARIES_CLS     "{031E4825-7B94-4DC3-B131-E946B44C8DD5}"//库
#define LINUX_CLS         "{B2B4A4D1-2754-4140-A2EB-9A76D9D7CDC6}"//WSL



// 预定义的特殊项目的 GUID
const GUID MyComputerGUID = { 0x20D04FE0, 0x3AEA, 0x1069, { 0xA2, 0xD8, 0x08, 0x00, 0x2B, 0x30, 0x30, 0x9D } }; // 我的电脑
const GUID RecycleBinGUID = { 0x645FF040, 0x5081, 0x101B, { 0x9F, 0x08, 0x00, 0xAA, 0x00, 0x2F, 0x95, 0x4E } }; // 回收站
const GUID ControlPanelGUID = { 0x26EE0668, 0xA00A, 0x44D7, { 0x93, 0x71, 0xBE, 0xB0, 0x64, 0xC9, 0x86, 0x83 } }; // 控制面板
const GUID ControlPanel2GUID = { 0x5399E694, 0x6CE5, 0x4D6C, { 0x8F, 0xCE, 0x1D, 0x88, 0x70, 0xFD, 0xCB, 0xA0 } }; // 控制面板(分类视图)
const GUID NetworkGUID = { 0xF02C1A0D, 0xBE21, 0x4350, { 0x88, 0xB0, 0x73, 0x67, 0xFC, 0x96, 0xEF, 0x3C } }; // 网络
const GUID UserFolderGUID = { 0x59031A47, 0x3F72, 0x44A7, { 0x89, 0xC5, 0x55, 0x95, 0xFE, 0x6B, 0x30, 0xEE } };// 用户文件夹
const GUID LibrariesGUID = { 0x031E4825, 0x7B94, 0x4DC3, { 0xB1, 0x31, 0xE9, 0x46, 0xB4, 0x4C, 0x8D, 0xD5 } };// 库
const GUID LinuxGUID = { 0xB2B4A4D1, 0x2754, 0x4140, { 0xA2, 0xEB, 0x9A, 0x76, 0xD9, 0xD7, 0xCD, 0xC6 } };// WSL
//图标结构体
struct IconItem
{
    std::string name;
    std::string path;
    POINT position = { 0, 0 };
    LPITEMIDLIST pidl_p = nullptr;  // 在定义处直接初始化为nullptr
    SpecialFolders specialFolder = SpecialFolders::Empty;

    // 可以添加构造函数确保正确初始化
    IconItem() : pidl_p(nullptr), specialFolder(SpecialFolders::Empty) {
        position.x = 0;
        position.y = 0;
    }
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

    static IconItem CreateIconItem(const wchar_t* filePath);
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
	/// 获取所有桌面图标的PIDL
	/// </summary>
	/// <returns></returns>
	vector<IconItem> GetDesktopPIDLItems();
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
	//获取pidl类型
    static SpecialFolders getFolderTypeFromName(const wchar_t* name);
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
