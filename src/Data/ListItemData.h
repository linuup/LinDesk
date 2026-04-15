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
#include <commoncontrols.h>
#include <wil/resource.h>
#include <QtWinExtras/qwinfunctions.h>
#include <SystemApi.h>
#include <QFileIconProvider>
#include <QContextMenuEvent>
#include <QRect>
#include "SysInfoUtil.h"
// //1 隐藏 0 显示
#define APP_MY_COMPUTER_CLS   "::{20D04FE0-3AEA-1069-A2D8-08002B30309D}" // 计算机/此电脑
#define APP_NETWORK_CLS       "::{F02C1A0D-BE21-4350-88B0-7367FC96EF3C}" // 网络
#define APP_RECYCLE_BIN_CLS   "::{645FF040-5081-101B-9F08-00AA002F954E}" // 回收站
#define APP_CONTROL_PANEL_CLS "::{26EE0668-A00A-44D7-9371-BEB064C98683}" // 控制面板
#define APP_CONTROL_PANEL1_CLS "::{5399E694-6CE5-4D6C-8FCE-1D8870FDCBA0}" // 控制面板
#define APP_USER_FOLDER_CLS   "::{59031a47-3f72-44a7-89c5-5595fe6b30ee}" // 用户文件夹
#define APP_LIBRARIES_CLS     "::{031E4825-7B94-4DC3-B131-E946B44C8DD5}" // 库
#define APP_LINUX_CLS         "::{B2B4A4D1-2754-4140-A2EB-9A76D9D7CDC6}" // WSL
#define APP_ONEDRIVE_CLS      "::{018D5C66-4533-4307-9B53-224DE2ED1FE6}" // OneDrive

const QSet<QString> SystemFolderCLSIDs = {
	"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}",  // This PC
	"::{645FF040-5081-101B-9F08-00AA002F954E}",  // Recycle Bin
	"::{26EE0668-A00A-44D7-9371-BEB064C98683}",  // Control Panel (Classic)
	"::{5399E694-6CE5-4D6C-8FCE-1D8870FDCBA0}",  // Control Panel (Category View)
	"::{031E4825-7B94-4DC3-B131-E946B44C8DD5}",  // Libraries
	"::{B2B4A4D1-2754-4140-A2EB-9A76D9D7CDC6}",  // Linux (WSL)
	"::{F02C1A0D-BE21-4350-88B0-7367FC96EF3C}",  // Network
	"::{59031a47-3f72-44a7-89c5-5595fe6b30ee}",  // User Folder
	"::{018D5C66-4533-4307-9B53-224DE2ED1FE6}"   // OneDrive
};

class ListItemData {
public:
	static int s_targetIconSize;
	static void setTargetIconSize(int size) { s_targetIconSize = size; }
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
		const SpecialFolders resolvedType =
			(item.shortcutTargetSpecialFolder != SpecialFolders::Empty &&
			 item.shortcutTargetSpecialFolder != SpecialFolders::UnknownFolder)
				? item.shortcutTargetSpecialFolder
				: item.specialFolder;
		setItemDateType(resolvedType);
		m_fullName = QString::fromStdString(item.name.c_str());
		m_fileInfo = QFileInfo(QString(item.path.c_str()).replace("LNK", "lnk"));
		if (item.shortcutTargetSpecialFolder != SpecialFolders::Empty && item.shortcutTargetSpecialFolder != SpecialFolders::UnknownFolder && !item.shortcutTargetPath.empty()) {
			m_clsid = QString::fromStdString(item.shortcutTargetPath.c_str());
		} else {
			m_clsid = QString::fromStdString(m_iconItem.path.c_str());
		}

		initIcon();
		/*qDebug() << "\nFullName:" << m_fullName;
		qDebug() << "\nPath:" << QString::fromStdString(item.path.c_str());*/
	};

	void setIconItem(const IconItem& item) {
		m_iconItem = item;
	}

	bool setPIDL(LPITEMIDLIST pidl) {
		m_pidl = std::move(pidl);
		//m_pidl.reset(ILClone(pidl));
		if (!m_pidl) return false;
		return updatePropertiesFromPIDL();
	}
	bool isDriveRootPath(const QString& rawPath) const {
		if (rawPath.isEmpty()) {
			return false;
		}

		const QString nativePath = QDir::toNativeSeparators(rawPath);
		std::wstring wPath = nativePath.toStdWString();
		if (wPath.empty()) {
			return false;
		}

		if (!PathIsRootW(wPath.c_str())) {
			return false;
		}

		const UINT driveType = GetDriveTypeW(wPath.c_str());
		return driveType != DRIVE_NO_ROOT_DIR && driveType != DRIVE_UNKNOWN;
	}
	void setItemDateType(const SpecialFolders type) {
		auto pathTmp = m_iconItem.path.c_str();
		QFileInfo tmp(pathTmp);
		tmp = tmp.exists() ? tmp : QFileInfo();
		const QString rawPath = QString::fromStdString(m_iconItem.path);

		// Correctly identify special folders excluding Empty and Unknown types
		if (type != SpecialFolders::UnknownFolder && type != SpecialFolders::Empty) {
			m_specialFolderType = type;
			m_type = ObjectType::SpecialFolder;
		}
		else {
			//?????????
			if (isDriveRootPath(rawPath)) {
				m_type = ObjectType::DiskLetter;
			}
			else if (tmp.isFile()) {
				m_type = ObjectType::File;
				//m_type = ObjectType::Link;
			}
			else if (tmp.isDir()) {
				m_type = ObjectType::Folder_ObjectType;
			}
            else {
                const QString suffix = QFileInfo(rawPath).suffix().toLower();
                if (!rawPath.isEmpty() && (suffix == "lnk" || suffix == "url" || suffix == "appref-ms" || suffix == "ico")) {
                    m_type = ObjectType::File;
                }
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
	const QFileInfo& fileInfo() const { return m_fileInfo; }
	const IconItem& itemData() const { return m_iconItem; }
	const QIcon& icon() const { return m_icon; }

	const QString getCLSID() const { return m_clsid; }
	// QDateTime lastModified() const { return m_lastModified; }
	const ObjectType type() const { return m_type; }
	const SpecialFolders specialFolderType() const { return m_specialFolderType; }
	LPITEMIDLIST pidl() const { return m_pidl; }

	bool isShortcutType() const {
		QString ext = m_fileInfo.suffix().toLower();
		return (ext == "lnk" || ext == "url" || ext == "appref-ms");
	}

	QString displayNameForRename() const {
		if (isShortcutType()) {
			return m_fileInfo.completeBaseName();
		}
		return m_fullName;
	}

	QString extensionForRename() const {
		if (isShortcutType()) {
			QString ext = m_fileInfo.suffix();
			return ext.isEmpty() ? "" : "." + ext;
		}
		return "";
	}
public:
	int m_iconSizeCached = -1;
	void initIcon() {
		// Only re-initialize if the target size significantly changed or we have no icon
		if (m_iconSizeCached == s_targetIconSize && !m_icon.isNull()) {
			return;
		}

		m_iconSizeCached = s_targetIconSize;
		QIcon newIcon;
		
		if (m_type == ObjectType::SpecialFolder) {
			// 1. 终极高清方案：使用 Windows Shell Jumbo Image List (支持 256x256 及以上)
			if (m_pidl) {
				SHFILEINFO sfi = { 0 };
				// 获取系统图标索引
				if (SHGetFileInfoW((LPCWSTR)m_pidl, 0, &sfi, sizeof(sfi), SHGFI_PIDL | SHGFI_SYSICONINDEX)) {
					// 显式定义 IID_IImageList 以解决编译错误
					static const GUID IID_IImageList_Local = { 0x46EB5926, 0x582E, 0x4017, { 0x9F, 0xDF, 0xE8, 0x99, 0x8D, 0xAA, 0x09, 0x50 } };
					
					IImageList* imageList = nullptr;
					// 请求 JUMBO (256x256) 图标列表 (SHIL_JUMBO = 4)
					if (SUCCEEDED(SHGetImageList(4, IID_IImageList_Local, (void**)&imageList))) {
						HICON hIcon = nullptr;
						if (SUCCEEDED(imageList->GetIcon(sfi.iIcon, ILD_TRANSPARENT, &hIcon))) {
							newIcon = QtWin::fromHICON(hIcon);
							DestroyIcon(hIcon);
						}
						imageList->Release();
					}
				}
			}

			// 2. 兜底方案：如果高度方案失败，尝试常规路径提取
			if (m_icon.isNull()) {
				SHFILEINFO sfi = { 0 };
				std::wstring wPath = QDir::toNativeSeparators(m_iconItem.path.c_str()).toStdWString();
				if (SHGetFileInfoW(wPath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON)) {
					if (sfi.hIcon) {
						newIcon = QtWin::fromHICON(sfi.hIcon);
						DestroyIcon(sfi.hIcon);
					}
				}
			}

			// 3. 硬编码补丁仅作为最后的防线
			if (m_icon.isNull()) {
				switch (m_specialFolderType) {
				case SpecialFolders::ThisPc:
					newIcon = SysInfoUtil::SysFileIconByDLL(16, s_targetIconSize, "shell32.dll");
					break;
				case SpecialFolders::Network:
					newIcon = SysInfoUtil::SysFileIconByDLL(18, s_targetIconSize, "shell32.dll");
					break;
				case SpecialFolders::RecycleBin:
					newIcon = SysInfoUtil::SysFileIconByDLL(32, s_targetIconSize, "shell32.dll");
					break;
				case SpecialFolders::UserFolder:
					newIcon = SysInfoUtil::SysFileIconByDLL(118, s_targetIconSize, "imageres.dll");
					break;
				default:
					break;
				}
			}
			if (!newIcon.isNull()) m_icon = newIcon;
			return;
		}

		// 特殊处理 .appref-ms 文件获取图标
		if (m_fileInfo.suffix().toLower() == "appref-ms") {
			SHFILEINFO sfi = { 0 };
			std::wstring wPath = QDir::toNativeSeparators(m_iconItem.path.c_str()).toStdWString();
			if (SHGetFileInfoW(wPath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON)) {
				if (sfi.hIcon) {
					newIcon = QtWin::fromHICON(sfi.hIcon);
					DestroyIcon(sfi.hIcon);
				}
			}
		}

		if (newIcon.isNull()) {
			QFileIconProvider icon_provider;
			QString targetPath = m_fileInfo.symLinkTarget();
			if (!targetPath.isEmpty()) {
				QFileInfo targetData(targetPath);
				newIcon = icon_provider.icon(targetData);
			}
			if (newIcon.isNull()) {
				if (m_fileInfo.isFile()) {
					newIcon = icon_provider.icon(m_fileInfo);
				}
				else {
					// --- 文件夹预览图 (Live Preview) ---
					std::wstring wPath = QDir::toNativeSeparators(m_iconItem.path.c_str()).toStdWString();
					// 使用固定的 256px 高素质预览图，避免缩放时产生撕裂 (Fix for bug: tearing after resize)
					HBITMAP hBmp = CSystemApi::GetShellThumbnail(wPath.c_str(), 256, 256);
					if (hBmp) {
						newIcon = QIcon(QtWin::fromHBITMAP(hBmp, QtWin::HBitmapAlpha));
						DeleteObject(hBmp);
					}
					
					// 如果获取失败（例如空文件夹），则显示默认文件夹图标
					if (newIcon.isNull()) {
						newIcon = SysInfoUtil::SysFileIconByDLL(4, s_targetIconSize, "shell32.dll");
					}
				}
			}
		}

		// --- Win32 SHGetFileInfo fallback (Win11 compatibility) ---
		// QFileIconProvider can fail on Win11 for certain file types.
		// Try the Windows Shell Jumbo Image List first for high-quality icons,
		// then fall back to standard SHGetFileInfo.
		if (newIcon.isNull()) {
			std::wstring wPath = QDir::toNativeSeparators(m_iconItem.path.c_str()).toStdWString();
			SHFILEINFOW sfi = { 0 };

			// Attempt 1: Jumbo (256x256) via IImageList
			if (SHGetFileInfoW(wPath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX)) {
				static const GUID IID_IImageList_Local = {
					0x46EB5926, 0x582E, 0x4017,
					{ 0x9F, 0xDF, 0xE8, 0x99, 0x8D, 0xAA, 0x09, 0x50 }
				};
				IImageList* imageList = nullptr;
				// SHIL_JUMBO = 4
				if (SUCCEEDED(SHGetImageList(4, IID_IImageList_Local, (void**)&imageList))) {
					HICON hIcon = nullptr;
					if (SUCCEEDED(imageList->GetIcon(sfi.iIcon, ILD_TRANSPARENT, &hIcon))) {
						newIcon = QtWin::fromHICON(hIcon);
						DestroyIcon(hIcon);
					}
					imageList->Release();
				}
			}

			// Attempt 2: Standard SHGetFileInfo LARGEICON
			if (newIcon.isNull()) {
				memset(&sfi, 0, sizeof(sfi));
				if (SHGetFileInfoW(wPath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON)) {
					if (sfi.hIcon) {
						newIcon = QtWin::fromHICON(sfi.hIcon);
						DestroyIcon(sfi.hIcon);
					}
				}
			}
		}

		// Ultimate fallback: blank file icon from shell32.dll
		if (newIcon.isNull()) {
			newIcon = SysInfoUtil::SysFileIconByDLL(0, s_targetIconSize, "shell32.dll");
		}
		
		if (!newIcon.isNull()) {
			// 对提取的图标进行视觉自动裁剪 (Visual Auto-Crop)
			// 获取最大尺寸图进行裁剪计算，目前上限 256px
			QSize availableSize = newIcon.actualSize(QSize(256, 256));
			QPixmap pixmap = newIcon.pixmap(availableSize);
			if (!pixmap.isNull()) {
				bool wasCropped = false;
				QImage img = pixmap.toImage();
				QImage cropped = SysInfoUtil::autoCropImage(img, &wasCropped);
				
				// 如果裁切后发生了显著变化，重新生成 QIcon
				if (wasCropped) {
					m_icon = QIcon(QPixmap::fromImage(cropped));
				} else {
					m_icon = newIcon;
				}
			} else {
				m_icon = newIcon;
			}
			
		}
	}
	bool updatePropertiesFromPIDL() {
		if (!m_pidl) return false;
		return true;
	}
	//Shell信息
	IconItem m_iconItem;

	// 文件信息
	QFileInfo m_fileInfo;   //文件信息
	QString m_clsid;
	QIcon m_icon;   //图标
	QString m_fullName;
	QString m_shellPath;    //Shell需要的路径 ：C:\\Users\\a\\Desktop
	ObjectType m_type = ObjectType::Empty_ObjectType;  //数据对象类型
	SpecialFolders m_specialFolderType = SpecialFolders::Empty;  //特殊文件夹类型
	LPITEMIDLIST m_pidl = nullptr;       // PIDL (Safe initialization)
};
