#pragma once
#include "SysInfoUtil.h"
#include <QtWinExtras/qwinfunctions.h>
#include <iostream>
//QMap<QString, SysFileIcon>* SysInfoUtil::SysIconRegedit_State()
//{
//	QMap<QString, SysFileIcon>* regeditValue = new QMap<QString, SysFileIcon>;
//	QSettings settings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\NewStartPanel", QSettings::NativeFormat);
//	for (const QString& key : settings.allKeys()) {
//		SysFileIcon icon;
//		int val = settings.value(key).toInt();
//		if (key == MY_COMPUTER_CLS) {
//			icon.name = "This PC";
//			icon.CLSID = QString(MY_COMPUTER_CLS);
//			icon.hideIcon = val;
//			icon.iconIndex = 16;
//			icon.DllName = "shell32.dll";
//			icon.sfName = SpecialFolders::ThisPc;
//		}
//		else if (key == CONTROL_PANEL_CLS) {
//			icon.name = "Control Panel";
//			icon.CLSID = QString(CONTROL_PANEL_CLS);
//			icon.hideIcon = val;
//			icon.iconIndex = 58;
//			icon.DllName = "shell32.dll";
//			icon.sfName = SpecialFolders::ControlPanel;
//		}
//		else if (key == NETWORK_CLS) {
//
//			icon.name = "Network";
//			icon.CLSID = QString(NETWORK_CLS);
//			icon.hideIcon = val;
//			icon.iconIndex = 18;
//			icon.DllName = "shell32.dll";
//			icon.sfName = SpecialFolders::Network;
//		}
//		else if (key == RECYCLE_BIN_CLS) {
//			icon.name = "Recycle Bin";
//			icon.CLSID = QString(RECYCLE_BIN_CLS);
//			icon.hideIcon = val;
//			icon.iconIndex = 32;
//			icon.DllName = "shell32.dll";
//			icon.sfName = SpecialFolders::RecycleBin;
//		}
//		else if (key == USER_FOLDER_CLS) {
//			icon.name = "User";
//			icon.CLSID = QString(USER_FOLDER_CLS);
//			icon.hideIcon = val;
//			icon.iconIndex = 118;
//			icon.DllName = "imageres.dll";
//			icon.sfName = SpecialFolders::User;
//		}
//		//有状态再存储
//		if (!icon.name.isEmpty()) {
//			regeditValue->insert(key, icon);
//		}
//	}
//	return regeditValue;
//}



QIcon SysInfoUtil::ExtractIconFromExe(const QString& exePath, int iconIndex)
{
	// 获取HICON句柄
	HICON hIcon = ExtractIcon(GetModuleHandle(nullptr), exePath.toStdWString().c_str(), iconIndex);

	if (hIcon != nullptr) {
		// 将HICON转换为QIcon
		QIcon icon = QtWin::fromHICON(hIcon);
		DestroyIcon(hIcon); // 释放HICON资源
		return icon;
	}
	else {
		// 如果无法获取图标，则返回一个空的QIcon
		return QIcon();
	}
}

QIcon SysInfoUtil::SysFileIconByDLL(int index, int maxSize, QString dllName)
{
	QString tmp = "C:\\Windows\\System32\\" + dllName;
	index = index - 1;

	LPCWSTR dllPath = (LPCWSTR)tmp.utf16();

	HMODULE hUser32 = LoadLibrary(L"user32.dll");
	if (!hUser32) {
		qWarning() << "Failed to load user32.dll";
		return QIcon();
	}

	PrivateExtractIcons_t PrivateExtractIcons =
		(PrivateExtractIcons_t)GetProcAddress(hUser32, "PrivateExtractIconsW");
	if (!PrivateExtractIcons) {
		qWarning() << "Failed to get PrivateExtractIcons address";
		FreeLibrary(hUser32);
		return QIcon();
	}

	QIcon icon;
	HICON hIcon;
	UINT uIconId;

	// 尝试提取大图标来检测可用尺寸
	QList<int> availableSizes;
	QList<int> testSizes = { 16, 32, 48, 64, 128, 256 };

	// 首先检测哪些尺寸是可用的
	for (int size : testSizes) {
		UINT result = PrivateExtractIcons(dllPath, index, size, size, &hIcon, &uIconId, 1, 0);
		if (result > 0 && hIcon != nullptr) {
			availableSizes.append(size);
			DestroyIcon(hIcon);
		}
	}

	// 如果没有检测到任何尺寸，尝试索引0
	if (availableSizes.isEmpty()) {
		for (int size : testSizes) {
			UINT result = PrivateExtractIcons(dllPath, 0, size, size, &hIcon, &uIconId, 1, 0);
			if (result > 0 && hIcon != nullptr) {
				availableSizes.append(size);
				DestroyIcon(hIcon);
			}
		}
	}

	// 提取所有小于等于maxSize的可用尺寸
	for (int size : availableSizes) {
		if (size <= maxSize) {
			UINT result = PrivateExtractIcons(dllPath, index, size, size, &hIcon, &uIconId, 1, 0);
			if (result == 0) {
				result = PrivateExtractIcons(dllPath, 0, size, size, &hIcon, &uIconId, 1, 0);
			}

			if (result > 0 && hIcon != nullptr) {
				QPixmap pixmap = QtWin::fromHICON(hIcon);
				if (!pixmap.isNull()) {
					icon.addPixmap(pixmap);
				}
				DestroyIcon(hIcon);
			}
		}
	}

	FreeLibrary(hUser32);

	// 如果仍然没有图标，使用回退方案
	if (icon.isNull()) {
		return QIcon();
	}

	return icon;
}

QIcon SysInfoUtil::SysFileIconByDLL(int index, QString dllName)
{
	return SysFileIconByDLL(index, 256, dllName);
}

QString SysInfoUtil::MkMutiDir(const QString path)
{
	QDir dir(path);
	if (dir.exists(path)) {
		return path;
	}
	QString parentDir = MkMutiDir(path.mid(0, path.lastIndexOf('/')));
	QString dirname = path.mid(path.lastIndexOf('/') + 1);
	QDir parentPath(parentDir);
	if (!dirname.isEmpty())
		parentPath.mkpath(dirname);
	return parentDir + "/" + dirname;
}

QString SysInfoUtil::SaveIconInDataDir(const QString iconName, const QIcon* icon)
{
	QString appTempDataPath = QDir::tempPath();
	QDir dirPath(appTempDataPath);
	dirPath.cdUp();
	QString iconPath = dirPath.path() + "/ZhuoMian/Icon/";
	QDir iconDir(iconPath);
	if (!iconDir.exists()) {
		MkMutiDir(iconPath);
	}
	QString iconFullPath = iconPath + iconName + ".png";
	icon->pixmap(icon->actualSize(QSize(256, 256))).save(iconFullPath);
	return iconFullPath;
}

QString SysInfoUtil::GetDesktopWallpaperPath()
{
	// 使用QSettings来访问注册表中的配置信息，指定组织名、应用程序名以及注册表路径模式
	QSettings settings("HKEY_CURRENT_USER\\Control Panel\\Desktop", QSettings::NativeFormat);
	// 尝试获取名为"Wallpaper"的键对应的值，这里获取到的是QVariant类型
	QVariant wallpaperVariant = settings.value("Wallpaper");
	if (wallpaperVariant.isValid())
	{
		QString wallpaperPath = wallpaperVariant.toString();
		// 返回绝对路径形式的壁纸路径，通过QFileInfo来处理可能的相对路径等情况
		return QFileInfo(wallpaperPath).absoluteFilePath();
	}
	else
	{
		qDebug() << "Failed to retrieve desktop wallpaper path from the registry";
		return QString();
	}
	/*HKEY hKey;
	LONG result = RegOpenKeyEx(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, KEY_READ, &hKey);
	if (result == ERROR_SUCCESS)
	{
		wchar_t wallpaperPath[MAX_PATH] = {};
		DWORD dwType, dwDataSize = sizeof(wallpaperPath);
		result = RegQueryValueEx(hKey, L"Wallpaper", NULL, &dwType, (LPBYTE)wallpaperPath, &dwDataSize);

		if (result == ERROR_SUCCESS && dwType == REG_SZ)
		{
			QString path(QString::fromWCharArray(wallpaperPath));
			RegCloseKey(hKey);
			return QFileInfo(path).absoluteFilePath();
		}
	}

	RegCloseKey(hKey);
	qWarning() << "Failed to retrieve desktop wallpaper path from the registry";
	return QString();*/
}

QString SysInfoUtil::GetWallpaperUsingAPI()
{
	wchar_t wallpaperPath[MAX_PATH];

	// 使用 SystemParametersInfo 获取壁纸路径
	if (SystemParametersInfo(SPI_GETDESKWALLPAPER, MAX_PATH, wallpaperPath, 0)) {
		return QString::fromWCharArray(wallpaperPath);
	}

	return QString();
}



bool SysInfoUtil::isUsersRootDirectoryRegex(const QString& path)
{
	QRegularExpression regex(
		"^[Cc]:[/\\\\](Users)$",
		QRegularExpression::CaseInsensitiveOption
	);
	return regex.match(QDir::fromNativeSeparators(path)).hasMatch();
}

//void SysInfoUtil::AddSysFileIcon(QList<FileIcon>* lists)
//{
//	auto regeditValues = SysInfoUtil::SysIconRegedit_State();
//	// 使用迭代器遍历
//	for (QMap<QString, SysFileIcon>::iterator it = regeditValues->begin(); it != regeditValues->end(); ++it) {
//		SysFileIcon value = it.value();
//		if (value.hideIcon == 1) {
//			continue;
//		}
//		//qDebug() << value.name << value.iconIndex;
//		QIcon sysIcon = SysInfoUtil::SysFileIconByDLL(value.iconIndex, 256, value.DllName);
//		FileIcon icon;
//		icon.baseName = value.name;
//		icon.fullName = value.name;
//		icon.clsid = value.CLSID;
//		icon.fileIcon = sysIcon;
//		icon.fileType = FileType::SpecialFolder;
//		icon.sf = value.sfName;
//		lists->append(icon);
//	}
//}
QIcon SysInfoUtil::ExtractSystemIcon(SystemIcon iconName)
{
	switch (iconName)
	{
	case PERSONALIZE:
		return SysInfoUtil::SysFileIconByDLL(1, 16, "display.dll");
	case DISPLAY_SETTINGS:
		return SysInfoUtil::SysFileIconByDLL(1, 16, "themecpl.dll");
	}
	return QIcon();
}

bool IsKeyDown(int nVirtKey)
{
	SHORT status = (GetKeyState(nVirtKey) & 0x8000);

	return (status != 0);
}

