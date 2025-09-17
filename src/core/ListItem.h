ďťż#pragma once
#include <qstring.h>
#include <qdebug.h>
#include <qicon.h>
#include "CommonEnum.h"
// //1 计算机 0 逻辑驱动器
#define MY_COMPUTER_CLS   "{20D04FE0-3AEA-1069-A2D8-08002B30309D}"//计算机/我的电脑
#define NETWORK_CLS 	  "{F02C1A0D-BE21-4350-88B0-7367FC96EF3C}"//网络
#define RECYCLE_BIN_CLS   "{645FF040-5081-101B-9F08-00AA002F954E}"//回收站
#define CONTROL_PANEL_CLS "{5399E694-6CE5-4D6C-8FCE-1D8870FDCBA0}"//控制面板
#define USER_FOLDER_CLS   "{59031a47-3f72-44a7-89c5-5595fe6b30ee}"//用户文件夹

//file
#define MAX_FILENAME_PATH 260

//文件类型
enum FileType
{
    File,//文件
    ZFolder,//文件夹
    Link,//快捷方式
    SpecialFolder,//特殊文件夹
    DiskLetter,//磁盘分区
    BackGround,//背景
};
    DiskLetter,//磁盘分区
    BackGround,//背景
};

struct SysFileIcon
{
    QString name;   //文件名
    QString CLSID;  //CLSID
    int hideIcon = 1;   //隐藏图标
    int iconIndex = 0;  //图标索引
    QString DllName;    //隐藏dll路径
    SpecialFolders sfName;   //特殊文件夹名称
};

//Sys Icon regedit Path
#define DESKTOP_SYSFILE_ICON_PATH "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\NewStartPanel"

class FileWidget;




class FileIcon {

public:
    QString baseName;           //基本文件名
    QString suffixName;         //文件后缀名
    QString fullName;
    QString filePath;           //文件路径
    QIcon fileIcon;             //文件图标
    FileType fileType;          //file, folder,link, specialFolders, diskLetter, BackGround
    SpecialFolders sf;          //特殊文件夹名称
    QString clsid;
    QString command;
    QPoint originalPoint;       //原始位置
    FileWidget* fileWidget;     //文件窗口

    FileIcon();

    /*
    设置文件信息
    */
    void setFileInfo(FileIcon* _fileIcon);

};