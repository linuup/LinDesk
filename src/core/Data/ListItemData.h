#pragma once
#include <QString>
#include <QIcon>
#include <QDateTime>
#include <QFileInfo>
#include <QPixmap>
#include <QImage>
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wil/resource.h>
#include <QtWinExtras/qwinfunctions.h>

class ListItemData {
public:
    using unique_pidl = wil::unique_any<LPITEMIDLIST, decltype(&ILFree), ILFree>;

    ListItemData() = default;
    explicit ListItemData(LPITEMIDLIST pidl) { setPIDL(pidl); }

    // 移动构造函数，禁止复制
    ListItemData(ListItemData&&) noexcept = default;
    ListItemData& operator=(ListItemData&&) noexcept = default;
    ListItemData(const ListItemData&) = delete;
    ListItemData& operator=(const ListItemData&) = delete;

    // 初始化
    bool setPIDL(LPITEMIDLIST pidl) {
        m_pidl.reset(ILClone(pidl));
        if (!m_pidl) return false;
        return updatePropertiesFromPIDL();
    }

    // 获取文件信息
    const QString& text() const { return m_text; }
    const QString& data() const { return m_data; }
    const QIcon& icon() const { return m_icon; }
    const QString& filePath() const { return m_filePath; }
    qint64 fileSize() const { return m_fileSize; }
    QDateTime lastModified() const { return m_lastModified; }
    const QString& fileType() const { return m_fileType; }
    LPITEMIDLIST pidl() const { return m_pidl.get(); }

private:
    bool updatePropertiesFromPIDL() {
        if (!m_pidl) return false;

        // 获取路径
        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDList(m_pidl.get(), path)) {
            m_filePath = QString::fromWCharArray(path);
        }

        // 获取文件信息，包括显示名称、类型和图标
        SHFILEINFO sfi = {};
        if (SHGetFileInfo(reinterpret_cast<LPCTSTR>(m_pidl.get()), 0, &sfi, sizeof(sfi),
            SHGFI_PIDL | SHGFI_DISPLAYNAME | SHGFI_TYPENAME | SHGFI_ICON)) {
            m_text = QString::fromWCharArray(sfi.szDisplayName);
            m_fileType = QString::fromWCharArray(sfi.szTypeName);
            if (sfi.hIcon) {
                // m_icon = QIcon(QPixmap::fromImage(QtWin::fromHICON(sfi.hIcon)));
                // m_icon = nullptr;
                DestroyIcon(sfi.hIcon);
            }
        }
        // 获取文件大小和最后修改时间
        if (!m_filePath.isEmpty()) {
            QFileInfo fileInfo(m_filePath);
            m_fileSize = fileInfo.size();
            m_lastModified = fileInfo.lastModified();
        }

        return true;
    }

    // 文件信息
    QString m_text;
    QString m_data;
    QIcon m_icon;
    QString m_filePath;
    qint64 m_fileSize = 0;
    QDateTime m_lastModified;
    QString m_fileType;

    unique_pidl m_pidl;       // PIDL
};
