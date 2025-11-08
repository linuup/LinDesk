#pragma once


enum SpecialFolders {
    Empty,
	UnknownFolder,
    ThisPc,//桌面
    RecycleBin,
    ControlPanel,
    ControlPanel2,
    Network,
    UserFolder,
    OneDrive,
    Libraries,
    Linux
};
//对象类型
enum ObjectType
{
    Empty_ObjectType,//初始化默认类型
    File,//文件
    Folder_ObjectType,//文件夹
    MultObject,//多个项目，文件和文件号组合
    Link,//快捷方式
    SpecialFolder,//特殊文件夹
    DiskLetter,//盘符
    BackGround,//背景
};

