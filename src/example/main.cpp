#include <QApplication>
#include "ListItem.h"
#include "ListItemData.h"
#include <QDebug>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 创建 ListItemData 实例
    // 获取某个文件的 PIDL（例如桌面上的一个文件）
    LPITEMIDLIST pidl = nullptr;
    HRESULT hr = SHParseDisplayName(L"D:\\Desktop\\YiYiDesk\\src", nullptr, &pidl, 0, nullptr);
    if (SUCCEEDED(hr)) {
        ListItemData item;
        if (item.init(pidl)) {
            qDebug() << "Text:" << item.text();
            qDebug() << "File Path:" << item.filePath();
            qDebug() << "File Size:" << item.fileSize();
            qDebug() << "File Type:" << item.fileType();
            qDebug() << "Last Modified:" << item.lastModified();
        }
        ILFree(pidl); // 释放 PIDL
    }
    // 创建 ListItem 实例
    ListItem item;
    item.resize(400, 60);
    item.setWindowTitle("File List Item Example");
    item.show();

    return app.exec();
}
