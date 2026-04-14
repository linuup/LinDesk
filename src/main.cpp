#include <Windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <QDebug>
#include <iostream>
#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QVector>
#include <QStringList>
#include "LinDesk.h"
#include "SystemApi/SystemApi.h"
#include "SystemApi/Tool.h"
#include "LinDesktop.h"
#include "AppLaunchOptions.h"
using namespace ShortcutArrowManager;
int main(int argc, char *argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    // 启用缩放支持
    //QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    //// 允许图标使用高分像素图
    //QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    // 获取桌面上所有文件的列表
    
    //vector<IconItem> items = c.GetDesktopIconsItems(CSystemApi::FindSysListView32());
    //qSetMessagePattern("%{time yyyy-MM-dd hh:mm:ss}--[%{type}]--%{file}--%{function}--%{line}:%{message}");
    //qSetMessagePattern("[%{type}]--%{file}--%{function}--%{line}:%{message}");
    
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/Assets/app_icon.png"));
    app.setQuitOnLastWindowClosed(false);

    const AppLaunchOptions launchOptions = AppLaunchOptions::fromArguments(QCoreApplication::arguments());
    
    // Set Windows 1:1 Desktop Standard font
    QFont defaultFont("Microsoft YaHei UI", 9);
    defaultFont.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(defaultFont);
	//qDebug() << "Application Dir:" << app.applicationDirPath();
    //LinDesk *lindesk = LinDesk::instance();
    //lindesk->show();
    LinDesktop linDesktop;
    launchOptions.apply(&linDesktop);
    linDesktop.show();
    qDebug() << isShortcutArrowEnabled();
    qDebug() << isUACShieldEnabled();
    return app.exec();
}
