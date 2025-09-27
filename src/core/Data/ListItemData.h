#pragma once
#include <QString>
#include <QIcon>
#include <QDateTime>
#include <QFileInfo>
#include <QPixmap>
#include <QImage>
#include <windows.h>
#include <shlobj.h>
#include <QDebug>
#include <shlwapi.h>
#include <wil/resource.h>
#include <QtWinExtras/qwinfunctions.h>
#include <SystemApi.h>
#include <QFileIconProvider>
#include <QContextMenuEvent>




const QSet<QString> SystemFolderCLSIDs = {
    "::{20D04FE0-3AEA-1069-A2D8-08002B30309D}",  // This PC
    "::{645FF040-5081-101B-9F08-00AA002F954E}",  // Recycle Bin
    "::{26EE0668-A00A-44D7-9371-BEB064C98683}",  // Control Panel (Classic)
    "::{5399E694-6CE5-4D6C-8FCE-1D8870FDCBA0}",   // Control Panel (Category View)
    "::{031E4825-7B94-4DC3-B131-E946B44C8DD5}",  // Libraries
    "::{B2B4A4D1-2754-4140-A2EB-9A76D9D7CDC6}",  // Linux (WSL)
    "::{F02C1A0D-BE21-4350-88B0-7367FC96EF3C}"   // Network
};

class ListItemData {
public:
    /*using unique_pidl = wil::unique_<LPITEMIDLIST, decltype(&ILFree), ILFree>;*/

    //移动构造函数，禁止复制
/*ListItemData(ListItemData&&) noexcept = default;
ListItemData& operator=(ListItemData&&) noexcept = default;
ListItemData(const ListItemData&) = delete;
ListItemData& operator=(const ListItemData&) = delete;*/
	ListItemData() = default;

    ListItemData(IconItem item) {
        setIconItem(item);
        setPIDL(item.pidl_p);
        setItemDateType(item.specialFolder);
		m_fullName = QString::fromStdString(item.name.c_str());

        qDebug() << "\nFullName:" << m_fullName;
        qDebug() << "\nPath:" << QString::fromStdString(item.path.c_str());
    };

    void setIconItem(const IconItem& item) {
        m_iconItem = item;
     }
    // 初始化
    bool setPIDL(LPITEMIDLIST pidl) {
        m_pidl = std::move(pidl);
        //m_pidl.reset(ILClone(pidl));
        if (!m_pidl) return false;
        return updatePropertiesFromPIDL();
    }
    void setItemDateType(const SpecialFolders type) {
        auto pathTmp = m_iconItem.path.c_str();
        QFileInfo tmp(pathTmp);
        tmp = tmp.exists() ? tmp : QFileInfo();
        //设置为特殊文件夹
        if (type != SpecialFolders::UnknownFolder || SpecialFolders::Empty) {
            m_specialFolderType = type;
        }else {
            //判断对象类型
            if (tmp.isFile()) {
                m_type = ObjectType::File;
                //m_type = ObjectType::Link;
            }
            else if (tmp.isDir()) {
                m_type = ObjectType::Folder_ObjectType;
            }
        }
    }
         //移动构造函数，禁止复制
    /*ListItemData(ListItemData&&) noexcept = default;
    ListItemData& operator=(ListItemData&&) noexcept = default;
    ListItemData(const ListItemData&) = delete;
    ListItemData& operator=(const ListItemData&) = delete;*/
    
  // const IconItem& iconItem() const { return m_iconItem; }
   const QString& fullName() const { return m_fullName; }
   const QString filePath() const { return QString(m_iconItem.path.c_str()); }
  // const QString& data() const { return m_data; }
   
  // qint64 fileSize() const { return m_fileSize; }
  // QDateTime lastModified() const { return m_lastModified; }
   const ObjectType type() const { return m_type; }
   const SpecialFolders specialFolderType() const { return m_specialFolderType; }
   //LPITEMIDLIST pidl() const { return m_pidl.get(); }

    /*bool isSystemFolder(const QString& path) {
        return SystemFolderCLSIDs.contains(path);
    }*/
private:
    
    bool updatePropertiesFromPIDL() {
        if (!m_pidl) return false;
       // 获取路径
       //wchar_t path[MAX_PATH];
       //if (SHGetPathFromIDList(m_pidl.get(), path)) {
       //    m_filePath = QString::fromWCharArray(path);
       //}

       //// 获取文件信息，包括显示名称、类型和图标
       //SHFILEINFO sfi = {};
       //if (SHGetFileInfo(reinterpret_cast<LPCTSTR>(m_pidl.get()), 0, &sfi, sizeof(sfi),
       //    SHGFI_PIDL | SHGFI_DISPLAYNAME | SHGFI_TYPENAME | SHGFI_ICON)) {
       //    m_text = QString::fromWCharArray(sfi.szDisplayName);
       //    m_fileType = QString::fromWCharArray(sfi.szTypeName);
       //    if (sfi.hIcon) {
       //        // m_icon = QIcon(QPixmap::fromImage(QtWin::fromHICON(sfi.hIcon)));
       //        // m_icon = nullptr;
       //        DestroyIcon(sfi.hIcon);
       //    }
       //}
       //// 获取文件大小和最后修改时间
       //if (!m_filePath.isEmpty()) {
       //    QFileInfo fileInfo(m_filePath);
       //    m_fileSize = fileInfo.size();
       //    m_lastModified = fileInfo.lastModified();
       //}

        return true;
    }
    //Shell信息
    IconItem m_iconItem;

	
    // 文件信息
    
    QString m_fullName;
    QString m_shellPath;    //Shell需要的路径 ：C:\\Users\\a\\Desktop
    ObjectType m_type = ObjectType::Empty_ObjectType;  //数据对象类型
    SpecialFolders m_specialFolderType = SpecialFolders::Empty;  //特殊文件夹类型
    LPITEMIDLIST m_pidl;       // PIDL
};
