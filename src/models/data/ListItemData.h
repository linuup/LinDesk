#pragma once
#include <QString>
#include <QIcon>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QPixmap>
#include <QImage>
#include <QImageReader>
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
#include "services/SysInfoUtil.h"
#include "services/IconCacheManager.h"
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

enum class FileForm {
	Unknown,
	RegularFile,
	Directory,
	ShortcutFile,
	DriveRoot,
	VirtualShellItem
};

enum class TargetKind {
	Unknown,
	RegularFile,
	Folder,
	DriveRoot,
	SpecialFolder
};

class ListItemData {
public:
	static int s_targetIconSize;
	static void setTargetIconSize(int size) { s_targetIconSize = size; }

	// 档位常量与计算规则 (显示像素判断，含 DPI 缩放)
	static constexpr int kIconTierSmallThreshold = 62;   // 物理像素 <= 62 时使用 48px 原生小图标
	static constexpr int kIconTierMediumThreshold = 100; // 物理像素 62~100 时使用 96px 源图标；> 100 时使用 256px Jumbo 图标

	static constexpr int kIconTierSmallSourceSize = 48;  // SHIL_EXTRALARGE (2) 原生 48px
	static constexpr int kIconTierMediumSourceSize = 96; // JUMBO 256 提取后降采样 96px
	static constexpr int kIconTierLargeSourceSize = 256; // JUMBO 256 原样

	static int calculateIconSourceTier(int targetPx) {
		if (targetPx <= kIconTierSmallThreshold) {
			return kIconTierSmallSourceSize; // 48
		} else if (targetPx <= kIconTierMediumThreshold) {
			return kIconTierMediumSourceSize; // 96
		} else {
			return kIconTierLargeSourceSize; // 256
		}
	}
	/*using unique_pidl = wil::unique_<LPITEMIDLIST, decltype(&ILFree), ILFree>;*/

	//移动构造函数，禁止复制
/*ListItemData(ListItemData&&) noexcept = default;
ListItemData& operator=(ListItemData&&) noexcept = default;
ListItemData(const ListItemData&) = delete;
ListItemData& operator=(const ListItemData&) = delete;*/
	ListItemData() = default;

	ListItemData(IconItem item) {
		setIconItem(item);
		m_fileInfo = QFileInfo(QString(item.path).replace("LNK", "lnk"));
		setPIDL(item.pidl_p);
		const SpecialFolders resolvedType =
			(item.shortcutTargetSpecialFolder != SpecialFolders::Empty &&
			 item.shortcutTargetSpecialFolder != SpecialFolders::UnknownFolder)
				? item.shortcutTargetSpecialFolder
				: item.specialFolder;
		setItemDateType(resolvedType);
		m_fullName = item.name;
		if (item.shortcutTargetSpecialFolder != SpecialFolders::Empty && item.shortcutTargetSpecialFolder != SpecialFolders::UnknownFolder && !item.shortcutTargetPath.isEmpty()) {
			m_clsid = item.shortcutTargetPath;
		} else {
			m_clsid = m_iconItem.path;
		}

		// initIcon() deferred to lazy access
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
	bool isVirtualShellPath(const QString& rawPath) const {
		return rawPath.startsWith("::");
	}
	bool isShortcutPath(const QString& rawPath) const {
		const QString suffix = QFileInfo(rawPath).suffix().toLower();
		return (suffix == "lnk" || suffix == "url" || suffix == "appref-ms");
	}
	FileForm resolveFileForm(const QString& rawPath) const {
		if (rawPath.isEmpty()) {
			return FileForm::Unknown;
		}

		if (isVirtualShellPath(rawPath)) {
			return FileForm::VirtualShellItem;
		}

		if (isDriveRootPath(rawPath)) {
			return FileForm::DriveRoot;
		}

		if (isShortcutPath(rawPath)) {
			return FileForm::ShortcutFile;
		}

		if (m_fileInfo.isDir()) {
			return FileForm::Directory;
		}

		if (m_fileInfo.isFile()) {
			return FileForm::RegularFile;
		}

		const QString suffix = QFileInfo(rawPath).suffix().toLower();
		if (!suffix.isEmpty() && (suffix == "ico")) {
			return FileForm::RegularFile;
		}

		return FileForm::Unknown;
	}
	TargetKind resolveTargetKind(const QString& rawPath, SpecialFolders type) const {
		if (type != SpecialFolders::UnknownFolder && type != SpecialFolders::Empty) {
			return TargetKind::SpecialFolder;
		}

		if (isDriveRootPath(rawPath)) {
			return TargetKind::DriveRoot;
		}

		if (m_fileInfo.isDir()) {
			return TargetKind::Folder;
		}

		if (m_fileInfo.isFile()) {
			return TargetKind::RegularFile;
		}

		if (isShortcutPath(rawPath)) {
			return TargetKind::RegularFile;
		}

		return TargetKind::Unknown;
	}
	void applyLegacyObjectType() {
		switch (m_targetKind) {
		case TargetKind::SpecialFolder:
			m_type = ObjectType::SpecialFolder;
			break;
		case TargetKind::DriveRoot:
			m_type = ObjectType::DiskLetter;
			break;
		case TargetKind::Folder:
			m_type = ObjectType::Folder_ObjectType;
			break;
		case TargetKind::RegularFile:
			m_type = ObjectType::File;
			break;
		default:
			m_type = ObjectType::Empty_ObjectType;
			break;
		}
	}
	void setItemDateType(const SpecialFolders type) {
		const QString rawPath = m_iconItem.path;
		m_specialFolderType = SpecialFolders::Empty;
		m_fileForm = resolveFileForm(rawPath);
		m_targetKind = resolveTargetKind(rawPath, type);

		if (type != SpecialFolders::UnknownFolder && type != SpecialFolders::Empty) {
			m_specialFolderType = type;
		}

		applyLegacyObjectType();
	}
	//移动构造函数，禁止复制
/*ListItemData(ListItemData&&) noexcept = default;
ListItemData& operator=(ListItemData&&) noexcept = default;
ListItemData(const ListItemData&) = delete;
ListItemData& operator=(const ListItemData&) = delete;*/

// const IconItem& iconItem() const { return m_iconItem; }
	const QString& fullName() const { return m_fullName; }
	const QString filePath() const { return m_iconItem.path; }
	const QFileInfo& fileInfo() const { return m_fileInfo; }
	const IconItem& itemData() const { return m_iconItem; }
	const QIcon& icon() const {
		if (m_icon.isNull()) {
			const_cast<ListItemData*>(this)->initIcon();
		}
		return m_icon;
	}

	const QString getCLSID() const { return m_clsid; }
	// QDateTime lastModified() const { return m_lastModified; }
	const ObjectType type() const { return m_type; }
	const SpecialFolders specialFolderType() const { return m_specialFolderType; }
	const FileForm fileForm() const { return m_fileForm; }
	const TargetKind targetKind() const { return m_targetKind; }
	LPITEMIDLIST pidl() const { return m_pidl; }

	bool isShortcutType() const {
		return m_fileForm == FileForm::ShortcutFile;
	}
	bool pointsToSpecialFolder() const {
		return m_targetKind == TargetKind::SpecialFolder;
	}
	bool isRealSpecialFolderIcon() const {
		return pointsToSpecialFolder() && m_fileForm != FileForm::ShortcutFile;
	}
	bool isShortcutToSpecialFolder() const {
		return pointsToSpecialFolder() && m_fileForm == FileForm::ShortcutFile;
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
		const int tierSourceSize = calculateIconSourceTier(s_targetIconSize);
		// 仅在档位源尺寸改变或尚无图标时重新初始化
		if (m_iconSizeCached == tierSourceSize && !m_icon.isNull()) {
			return;
		}

		m_iconSizeCached = tierSourceSize;
		QIcon newIcon;

		// 辅助 lambda：按档位获取系统 ImageList 图标
		auto extractIconFromSysIndex = [tierSourceSize](int iIcon) -> QIcon {
			static const GUID IID_IImageList_Local = { 0x46EB5926, 0x582E, 0x4017, { 0x9F, 0xDF, 0xE8, 0x99, 0x8D, 0xAA, 0x09, 0x50 } };
			IImageList* imageList = nullptr;
			// <=62 走 SHGetImageList(2) (SHIL_EXTRALARGE 48px 原生)
			// >62 走 SHGetImageList(4) (SHIL_JUMBO 256px 临时中转)
			const int imageListType = (tierSourceSize <= kIconTierSmallSourceSize) ? 2 : 4;
			if (SUCCEEDED(SHGetImageList(imageListType, IID_IImageList_Local, (void**)&imageList))) {
				HICON hIcon = nullptr;
				if (SUCCEEDED(imageList->GetIcon(iIcon, ILD_TRANSPARENT, &hIcon))) {
					QPixmap pix = QtWin::fromHICON(hIcon);
					DestroyIcon(hIcon);
					if (tierSourceSize == kIconTierMediumSourceSize && !pix.isNull()) {
						if (pix.width() > 96 || pix.height() > 96) {
							pix = pix.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation);
						}
					}
					imageList->Release();
					return QIcon(pix);
				}
				imageList->Release();
			}
			return QIcon();
		};

		if (m_type == ObjectType::SpecialFolder) {
			// 1. 终极方案：按档位使用 Windows Shell Image List (48px 原生 / 256px 降采样 96px / 256px 原样)
			if (m_pidl) {
				SHFILEINFO sfi = { 0 };
				if (SHGetFileInfoW((LPCWSTR)m_pidl, 0, &sfi, sizeof(sfi), SHGFI_PIDL | SHGFI_SYSICONINDEX)) {
					newIcon = extractIconFromSysIndex(sfi.iIcon);
				}
			}

			// 2. 兜底方案：如果高度方案失败，尝试常规路径提取
			if (newIcon.isNull()) {
				SHFILEINFO sfi = { 0 };
				std::wstring wPath = QDir::toNativeSeparators(m_iconItem.path).toStdWString();
				if (SHGetFileInfoW(wPath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON)) {
					if (sfi.hIcon) {
						newIcon = QtWin::fromHICON(sfi.hIcon);
						DestroyIcon(sfi.hIcon);
					}
				}
			}

			// 3. 硬编码补丁仅作为最后的防线
			if (newIcon.isNull()) {
				switch (m_specialFolderType) {
				case SpecialFolders::ThisPc:
					newIcon = SysInfoUtil::SysFileIconByDLL(16, tierSourceSize, "shell32.dll");
					break;
				case SpecialFolders::Network:
					newIcon = SysInfoUtil::SysFileIconByDLL(18, tierSourceSize, "shell32.dll");
					break;
				case SpecialFolders::RecycleBin:
					newIcon = SysInfoUtil::SysFileIconByDLL(32, tierSourceSize, "shell32.dll");
					break;
				case SpecialFolders::UserFolder:
					newIcon = SysInfoUtil::SysFileIconByDLL(118, tierSourceSize, "imageres.dll");
					break;
				default:
					break;
				}
			}
			if (!newIcon.isNull()) {
				QSize availableSize = newIcon.actualSize(QSize(tierSourceSize, tierSourceSize));
				QPixmap pixmap = newIcon.pixmap(availableSize);
				if (!pixmap.isNull()) {
					if (tierSourceSize == kIconTierMediumSourceSize && (pixmap.width() > 96 || pixmap.height() > 96)) {
						pixmap = pixmap.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation);
					}
					bool wasCropped = false;
					QImage img = pixmap.toImage();
					QImage cropped = SysInfoUtil::autoCropImage(img, &wasCropped);
					m_icon = wasCropped ? QIcon(QPixmap::fromImage(cropped)) : QIcon(pixmap);
				} else {
					m_icon = newIcon;
				}
			}
			return;
		}

		const QString fileSuffix = m_fileInfo.suffix().toLower();
		const bool isShortcut = (fileSuffix == "lnk" || fileSuffix == "appref-ms" || m_fileInfo.isSymLink());

		// 1. 特殊处理 .appref-ms 文件传统提取
		if (fileSuffix == "appref-ms") {
			SHFILEINFO sfi = { 0 };
			std::wstring wPath = QDir::toNativeSeparators(m_iconItem.path).toStdWString();
			if (SHGetFileInfoW(wPath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON)) {
				if (sfi.hIcon) {
					newIcon = QtWin::fromHICON(sfi.hIcon);
					DestroyIcon(sfi.hIcon);
				}
			}
		}

		// 2. 常规提取（针对快捷方式目标实体文件、普通文件、普通文件夹）
		if (newIcon.isNull()) {
			QFileIconProvider icon_provider;

			// 检查是否为文件夹，或快捷方式指向有效文件夹
			QString resolvedDirPath;
			if (!isShortcut && !m_fileInfo.isFile()) {
				resolvedDirPath = m_iconItem.path;
			} else if (isShortcut) {
				QString symTarget = m_fileInfo.symLinkTarget();
				if (!symTarget.isEmpty() && QFileInfo(symTarget).isDir()) {
					resolvedDirPath = symTarget;
				} else if (!m_iconItem.shortcutTargetPath.isEmpty() && QFileInfo(m_iconItem.shortcutTargetPath).isDir()) {
					resolvedDirPath = m_iconItem.shortcutTargetPath;
				}
			}

			if (!resolvedDirPath.isEmpty()) {
				// --- 文件夹分支：有内容显示 Live Preview 内容缩略图，空文件夹显示纯净空文件夹图标 ---
				const QString nativeDirPath = QDir::toNativeSeparators(resolvedDirPath);
				const bool isDirEmpty = QDir(resolvedDirPath).isEmpty();
				if (!isDirEmpty) {
					// 核心关键：必须向 Windows Shell 请求 256x256 高清尺寸，才会触发合成内部文档缩略图 (Live Preview)
					HBITMAP hBmp = CSystemApi::GetShellThumbnail(nativeDirPath, 256, 256);
					if (hBmp) {
						QPixmap rawPreview = QtWin::fromHBITMAP(hBmp, QtWin::HBitmapAlpha);
						DeleteObject(hBmp);
						// 抠除纯色背景并去杂色
						QPixmap sanitized = SysInfoUtil::sanitizeFolderPreviewMatte(rawPreview);
						QPixmap finalPreview = !sanitized.isNull() ? sanitized : rawPreview;
						if (tierSourceSize <= kIconTierSmallSourceSize) {
							finalPreview = finalPreview.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation);
						} else if (tierSourceSize == kIconTierMediumSourceSize && (finalPreview.width() > 96 || finalPreview.height() > 96)) {
							finalPreview = finalPreview.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation);
						}
						newIcon = QIcon(finalPreview);
					}
				}

				// 如果为空文件夹或缩略图生成失败，则显示原生高清系统默认空文件夹图标
				if (newIcon.isNull()) {
					std::wstring wPath = nativeDirPath.toStdWString();
					SHFILEINFOW sfi = { 0 };
					if (SHGetFileInfoW(wPath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX)) {
						newIcon = extractIconFromSysIndex(sfi.iIcon);
					}
					if (newIcon.isNull()) {
						newIcon = icon_provider.icon(QFileInfo(resolvedDirPath));
					}
					if (newIcon.isNull()) {
						newIcon = SysInfoUtil::SysFileIconByDLL(4, tierSourceSize, "shell32.dll");
					}
				}
			} else {
				// 普通文件 / 普通文件快捷方式
				QString targetPath = m_fileInfo.symLinkTarget();
				if (targetPath.isEmpty() && isShortcut) {
					if (!m_iconItem.shortcutTargetPath.isEmpty()) {
						targetPath = m_iconItem.shortcutTargetPath;
					} else {
						targetPath = SysInfoUtil::ResolveShortcutTargetPath(m_iconItem.path);
					}
				}
				if (!targetPath.isEmpty()) {
					QFileInfo targetData(targetPath);
					const QString targetSuffix = targetData.suffix().toLower();
					if (targetSuffix == "ico") {
						newIcon = QIcon(targetPath);
					} else if (targetSuffix == "msi" || targetSuffix == "msp") {
						newIcon = SysInfoUtil::SysFileIconByDLL(1, 48, QStringLiteral("msiexec.exe"));
					} else {
						static const QSet<QString> s_imgExts = {
							QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
							QStringLiteral("webp"), QStringLiteral("bmp"), QStringLiteral("gif"),
							QStringLiteral("jfif"), QStringLiteral("tif"), QStringLiteral("tiff")
						};
						if (s_imgExts.contains(targetSuffix) && targetData.exists()) {
							QImageReader r(targetPath);
							r.setAutoTransform(true);
							if (r.canRead()) {
								QImage img = r.read();
								if (!img.isNull()) {
									if (img.width() > tierSourceSize || img.height() > tierSourceSize) {
										img = img.scaled(tierSourceSize, tierSourceSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
									}
									newIcon = QIcon(QPixmap::fromImage(img));
								}
							}
						}
					}
					if (newIcon.isNull()) {
						newIcon = icon_provider.icon(targetData);
					}
				}
				if (newIcon.isNull() && !isShortcut) {
					if (fileSuffix == "ico") {
						newIcon = QIcon(m_iconItem.path);
						if (newIcon.isNull()) {
							newIcon = icon_provider.icon(m_fileInfo);
						}
					} else if (fileSuffix == "msi" || fileSuffix == "msp") {
						const QString extKey = QString("ext:%1|pure|%2").arg(fileSuffix).arg(tierSourceSize);
						newIcon = IconCacheManager::instance().getIcon(extKey, [&]() {
							return SysInfoUtil::SysFileIconByDLL(1, 48, QStringLiteral("msiexec.exe"));
						});
					} else if (!fileSuffix.isEmpty() && fileSuffix != "exe") {
						const QString extKey = QString("ext:%1|base|%2").arg(fileSuffix).arg(tierSourceSize);
						newIcon = IconCacheManager::instance().getIcon(extKey, [&]() {
							return icon_provider.icon(m_fileInfo);
						});
					} else {
						newIcon = icon_provider.icon(m_fileInfo);
					}
				}
			}
		}

		// 3. Win32 系统图标索引回退 (SHGFI_SYSICONINDEX / LARGEICON)
		if (newIcon.isNull()) {
			std::wstring wPath = QDir::toNativeSeparators(m_iconItem.path).toStdWString();
			SHFILEINFOW sfi = { 0 };

			if (SHGetFileInfoW(wPath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX)) {
				newIcon = extractIconFromSysIndex(sfi.iIcon);
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

		// 4. --- 专属补偿：针对无本地物理目标的 Store/UWP 应用快捷方式 ---
		if (newIcon.isNull() && isShortcut) {
			// 优先尝试读取 AUMID 解析 AppxManifest 原始高清透明 Logo（100% 纯净无黑边）
			const QString aumid = SysInfoUtil::GetAumidFromShortcut(m_iconItem.path);
			if (!aumid.isEmpty()) {
				newIcon = SysInfoUtil::LoadUwpAppIconFromAumid(aumid, tierSourceSize);
			}
			// 兜底尝试 Shell Item 提取
			if (newIcon.isNull()) {
				newIcon = SysInfoUtil::LoadShellItemIcon(m_iconItem.path, tierSourceSize);
			}
		}

		// --- L4 终极兜底：当所有提取链路均失败时，提供标准优雅的空白文件图标防线 ---
		if (newIcon.isNull()) {
			newIcon = SysInfoUtil::GetBlankFileFallbackIcon(tierSourceSize);
		}
		
		if (!newIcon.isNull()) {
			// 对提取的图标进行视觉自动裁剪 (Visual Auto-Crop)
			// 获取当前档位尺寸进行裁剪计算，避免无意义的 256px 放大
			QSize availableSize = newIcon.actualSize(QSize(tierSourceSize, tierSourceSize));
			QPixmap pixmap = newIcon.pixmap(availableSize);
			if (!pixmap.isNull()) {
				if (tierSourceSize == kIconTierMediumSourceSize && (pixmap.width() > 96 || pixmap.height() > 96)) {
					pixmap = pixmap.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation);
				}
				bool wasCropped = false;
				QImage img = pixmap.toImage();
				QImage cropped = SysInfoUtil::autoCropImage(img, &wasCropped);
				
				// 如果裁切后发生了显著变化，重新生成 QIcon
				if (wasCropped) {
					m_icon = QIcon(QPixmap::fromImage(cropped));
				} else {
					m_icon = QIcon(pixmap);
				}
			} else {
				m_icon = newIcon;
			}
		}

		// 终极保证：m_icon 绝对不为空
		if (m_icon.isNull()) {
			m_icon = SysInfoUtil::GetBlankFileFallbackIcon(tierSourceSize);
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
	FileForm m_fileForm = FileForm::Unknown;
	TargetKind m_targetKind = TargetKind::Unknown;
	LPITEMIDLIST m_pidl = nullptr;       // PIDL (Safe initialization)
};
