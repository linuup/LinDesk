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
int main(int argc, char *argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    CSystemApi c;
    // 获取桌面文件夹的 PIDL
    unique_pidl_absolute pidl = Tool::GetRootPidl();
    
    // 获取桌面文件夹的 ShellFolder
    wil::com_ptr_nothrow<IShellFolder> deskISF;
    HRESULT hr = SHGetDesktopFolder(&deskISF);
    if (FAILED(hr)) {
        qDebug() << "Failed to get desktop folder interface.";
        return 0;
    }
    // 创建枚举器以枚举桌面上的所有项
    IEnumIDList* pEnumIDList;
    /*SHCONTF_CHECKING_FOR_CHILDREN |
        SHCONTF_FOLDERS |
        SHCONTF_NONFOLDERS |
        SHCONTF_INCLUDEHIDDEN |
        SHCONTF_INIT_ON_FIRST_NEXT |
        SHCONTF_NETPRINTERSRCH |
        SHCONTF_SHAREABLE |
        SHCONTF_STORAGE |
        SHCONTF_NAVIGATION_ENUM |
        SHCONTF_FASTITEMS |
        SHCONTF_FLATLIST |
        SHCONTF_ENABLE_ASYNC |
        SHCONTF_INCLUDESUPERHIDDEN*/
    hr = deskISF->EnumObjects(NULL,
        SHCONTF_FOLDERS |
        SHCONTF_NONFOLDERS |
        SHCONTF_INCLUDEHIDDEN |
        SHCONTF_INCLUDESUPERHIDDEN |
        SHCONTF_ENABLE_ASYNC
    , &pEnumIDList);
    
    if (FAILED(hr)) {
        qDebug() << "Failed to get enumeration of desktop items.";
        return 0;
    }
    QVector<IconItem>* itemssss = new QVector<IconItem>();

    // 枚举每个文件项
    LPITEMIDLIST pidlItem = nullptr;  // 临时存储PIDL

    while (pEnumIDList->Next(1, &pidlItem, NULL) == S_OK) {
        // 通过智能指针使用 pidlItem
        STRRET strret;
        IconItem i;
        hr = deskISF->GetDisplayNameOf(pidlItem, SHGDN_FORPARSING, &strret);
        if (SUCCEEDED(hr)) {
            wchar_t szName[MAX_PATH];
            StrRetToBufW(&strret, pidlItem, szName, MAX_PATH);
#ifndef NDEBUG
            qDebug() << "Item: " << QString::fromWCharArray(szName);
#endif
            i.name = QString::fromWCharArray(szName).toStdString();
        }
        i.pidl_p = std::move(pidlItem);
        itemssss->push_back(i);
    }
    qDebug() << itemssss->size();
    qDebug() << itemssss->at(5).name.c_str();


    // 释放枚举器
    pEnumIDList->Release();

    // 获取桌面上所有文件的列表
    
    vector<IconItem> items = c.GetDesktopIconsItems(CSystemApi::FindSysListView32());

    QApplication app(argc, argv);
	qDebug() << "Application Dir:" << app.applicationDirPath();
    LinDesk *lindesk = LinDesk::instance();
    lindesk->show();
    return app.exec();
}
