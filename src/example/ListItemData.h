#ifndef LISTITEMDATA_H
#define LISTITEMDATA_H

#include <QString>
#include <QIcon>
#include <QDateTime>
#include <QFileInfo>
#include <windows.h>
#include <shlobj.h>
#include <comdef.h>

class ListItemData {
public:
    ListItemData() : m_pidl(nullptr) {}

    ~ListItemData() {
        if (m_pidl) {
            ILFree(m_pidl); // 释放 PIDL 内存
        }
    }

    // 初始化方法，根据 PIDL 填充所有属性
    bool init(LPITEMIDLIST pidl) {
        if (!pidl) {
            return false; // PIDL 无效
        }

        // 释放之前的 PIDL
        if (m_pidl) {
            ILFree(m_pidl);
            m_pidl = nullptr;
        }

        // 复制 PIDL
        m_pidl = ILClone(pidl);
        if (!m_pidl) {
            return false; // 复制失败
        }

        // 根据 PIDL 查询并填充属性
        if (!updatePropertiesFromPIDL()) {
            return false; // 查询失败
        }

        return true;
    }

    // 获取和设置文本（文件名）
    const QString& text() const { return m_text; }

    // 获取和设置附加数据
    const QString& data() const { return m_data; }

    // 获取图标
    const QIcon& icon() const { return m_icon; }

    // 获取文件路径
    const QString& filePath() const { return m_filePath; }

    // 获取文件大小
    qint64 fileSize() const { return m_fileSize; }

    // 获取文件修改时间
    QDateTime lastModified() const { return m_lastModified; }

    // 获取文件类型
    const QString& fileType() const { return m_fileType; }

    // 获取 PIDL
    LPITEMIDLIST pidl() const { return m_pidl; }

private:
    // 根据 PIDL 查询并填充属性
    bool updatePropertiesFromPIDL() {
        if (!m_pidl) {
            return false; // PIDL 无效
        }

        // 获取文件路径
        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDList(m_pidl, path)) {
            m_filePath = QString::fromWCharArray(path);
        } else {
            m_filePath.clear();
        }

        // // 获取文件信息（显示名称、图标、类型等）
        // SHFILEINFO sfi = {};
        // if (SHGetFileInfo(reinterpret_cast<LPCTSTR>(m_pidl), 0, &sfi, sizeof(sfi), SHGFI_PIDL | SHGFI_DISPLAYNAME | SHGFI_TYPENAME | SHGFI_ICON)) {
        //     m_text = QString::fromWCharArray(sfi.szDisplayName); // 显示名称
        //     m_fileType = QString::fromWCharArray(sfi.szTypeName); // 文件类型
        //     if (sfi.hIcon) {
        //         m_icon = QIcon(QPixmap::fromImage(QImage::fromHICON(sfi.hIcon))); // 图标
        //         DestroyIcon(sfi.hIcon); // 释放图标资源
        //     }
        // }

        // // 获取文件大小和修改时间
        // IShellFolder* pShellFolder = nullptr;
        // if (SHGetDesktopFolder(&pShellFolder) == S_OK) {
        //     STRRET strret = {};
        //     if (pShellFolder->GetDisplayNameOf(m_pidl, SHGDN_FORPARSING, &strret) == S_OK) {
        //         wchar_t* pszPath = nullptr;
        //         StrRetToStr(&strret, m_pidl, &pszPath);
        //         if (pszPath) {
        //             QFileInfo fileInfo(QString::fromWCharArray(pszPath));
        //             m_fileSize = fileInfo.size(); // 文件大小
        //             m_lastModified = fileInfo.lastModified(); // 修改时间
        //             CoTaskMemFree(pszPath); // 释放路径内存
        //         }
        //     }
        //     pShellFolder->Release(); // 释放 IShellFolder 接口
        // }

        return true;
    }

    QString m_text;           // 显示文本（例如文件名）
    QString m_data;           // 其他附加数据
    QIcon m_icon;             // 图标
    QString m_filePath;       // 文件路径
    qint64 m_fileSize = 0;    // 文件大小
    QDateTime m_lastModified; // 文件修改时间
    QString m_fileType;       // 文件类型
    LPITEMIDLIST m_pidl;      // PIDL
};

#endif // LISTITEMDATA_H