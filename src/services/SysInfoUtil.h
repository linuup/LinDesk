#pragma once
#include <qstring.h>
#include <qsettings.h>
#include <qicon.h>
#include <qpixmap.h>
#include <QtWinExtras/qwinfunctions.h>
#include <qdir.h>
#include <QDebug>
#include <QRegularExpression>


//定义函数原型
//__declspec(dllimport) UINT PrivateExtractIcons(
//LPCTSTR lpszFile,
//    int nIconIndex,
//    int cxIcon,
//    int cyIcon,
//    HICON* phicon,
//    UINT* piconid,
//    UINT nIcons,
//    UINT flags
//);

typedef UINT(WINAPI* PrivateExtractIcons_t)(
    LPCTSTR lpszFile,
    int nIconIndex,
    int cxIcon,
    int cyIcon,
    HICON* phicon,
    UINT* piconid,
    UINT nIcons,
    UINT flags
);

//=================================System Icons=================================
enum SystemIcon
{
    PERSONALIZE,    // 个性化
    DISPLAY_SETTINGS     // 显示设置
};
namespace ShortcutArrowManager {
    // 检查系统是否显示快捷方式箭头
    static bool isShortcutArrowEnabled()
    {
#ifdef Q_OS_WIN
        auto checkPath = [](const QString& root) -> bool {
            QSettings reg(root + "\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Icons", QSettings::NativeFormat);
            QString val = reg.value("29").toString();
            if (val.isEmpty()) return true;
            // 常见的隐藏箭头的字符串，包含空图标引用或 shell32,50 等
            if (val.contains("shell32.dll", Qt::CaseInsensitive) && 
               (val.contains("50") || val.contains("-50"))) return false;
            if (val.startsWith(",") || val.contains("null", Qt::CaseInsensitive)) return false;
            return true;
        };

        // 只要任一位置设置了隐藏，就视为关闭（通常 HKLM 优先级更高，但 HKCU 覆盖更广）
        if (!checkPath("HKEY_LOCAL_MACHINE")) return false;
        if (!checkPath("HKEY_CURRENT_USER")) return false;

        // 次要检查：lnkfile 核心设置 (高级用户可能直接删除了 IsShortcut)
        QSettings lnkReg("HKEY_CLASSES_ROOT\\lnkfile", QSettings::NativeFormat);
        if (!lnkReg.contains("IsShortcut")) return false;

        return true;
#else
        return false;
#endif
    }

    // 检查是否使用UAC盾牌图标
    static bool isUACShieldEnabled()
    {
#ifdef Q_OS_WIN
        QSettings registry("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer",
            QSettings::NativeFormat);
        return registry.value("ShowShortcutArrowOverlay", 1).toInt() == 1;
#else
        return false;
#endif
    }
}
namespace SysInfoUtil{
    bool isUsersRootDirectoryRegex(const QString& path);
    /// <summary>
    /// 从注册表获取桌面系统图标状态
    /// </summary>
    /// <returns></returns>
    //QMap<QString, SysFileIcon>* SysIconRegedit_State();
    
    //把系统图标信息添加到集合
    //void AddSysFileIcon(QList<FileIcon> *lists);
    /// <summary>
    /// 提取系统图标
    /// </summary>
    /// <param name="iconName"></param>
    /// <returns></returns>
    QIcon ExtractSystemIcon(SystemIcon iconName);

    /// <summary>
    /// 从指定的可执行文件路径提取图标，并返回一个QIcon对象
    /// </summary>
    /// <param name="exePath"></param>
    /// <param name="iconIndex"></param>
    /// <returns></returns>
    QIcon ExtractIconFromExe(const QString& exePath, int iconIndex = 0);
    
    // 从DLL提取系统图标
    QIcon SysFileIconByDLL(int index = 0, int maxSize = 256, QString dllName = "shell32.dll");
    QIcon SysFileIconByDLL(int index = 0, QString dllName = "shell32.dll");

    /**
    * @brief MkMutiDir  创建多级目录
    * @param path	     未创建的目录
    * @return 	     返回已创建的目录
    */
    QString MkMutiDir(const QString path);

    //保存图标到数据文件夹
    QString SaveIconInDataDir(const QString iconName, const QIcon* icon);

    //从注册表获取桌面背景路径
    QString GetDesktopWallpaperPath();
    QString GetWallpaperUsingAPI();
    QString GetRobustWallpaperPath();

    // 视觉自动裁剪 (Visual Auto-Crop)
    QImage autoCropImage(const QImage& image, bool* wasCropped = nullptr);

    // 文件夹图标背景清理
    QPixmap sanitizeFolderPreviewMatte(const QPixmap& source);

    // 微软商店应用 (UWP/MSIX) 与 Shell 项图标兼容加载及空白文件兜底
    QIcon LoadShellItemIcon(const QString& path, int side = 48);
    QString GetAumidFromShortcut(const QString& lnkPath);
    QString ResolveShortcutTargetPath(const QString& lnkPath);
    QIcon LoadUwpAppIconFromAumid(const QString& aumid, int side = 48);
    QIcon GetBlankFileFallbackIcon(int side = 48);

    // 现代文档白板衬底 (Document Plate) 渲染合成，消除低分辨率安装包/文档图标被粗暴放大导致的模糊
    QIcon CreateDocumentPlateIcon(const QIcon& innerIcon, int targetSide = 256);
}

/* General helper functions. */
bool IsKeyDown(int nVirtKey);