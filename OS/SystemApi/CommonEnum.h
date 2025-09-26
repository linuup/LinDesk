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
    Libraries,
    Linux
};
//对象类型
enum ObjectType
{
    Empty_ObjectType,
    File,//文件
    Folder_ObjectType,//文件夹
    Link,//快捷方式
    SpecialFolder,//特殊文件夹
    DiskLetter,//盘符
    BackGround,//背景
};

