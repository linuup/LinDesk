#include <Windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <QDebug>
#include <iostream>
#include <QApplication>
#include <QVector>
#include "LinDesk.h"
#include "SystemApi/SystemApi.h"
#include "SystemApi/Tool.h"
#include "ListView.h"
int main(int argc, char *argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    
    
    
    // 获取桌面上所有文件的列表
    
    //vector<IconItem> items = c.GetDesktopIconsItems(CSystemApi::FindSysListView32());
    //qSetMessagePattern("%{time yyyy-MM-dd hh:mm:ss}--[%{type}]--%{file}--%{function}--%{line}:%{message}");
    qSetMessagePattern("[%{type}]--%{file}--%{function}--%{line}:%{message}");
    QApplication app(argc, argv);
	qDebug() << "Application Dir:" << app.applicationDirPath();
    LinDesk *lindesk = LinDesk::instance();
    lindesk->show();
    ListView* listView = ListView::instance();
    listView->show();
    return app.exec();
}
