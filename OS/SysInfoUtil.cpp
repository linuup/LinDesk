// #pragma once
// #include "SysInfoUtil.h"
// #include <QtWinExtras/qwinfunctions.h>
// #include <iostream>
// QMap<QString, SysFileIcon>* SysInfoUtil::SysIconRegedit_State()
// {
// 	QMap<QString, SysFileIcon>* regeditValue = new QMap<QString, SysFileIcon>;
// 	QSettings settings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\NewStartPanel", QSettings::NativeFormat);
// 	for (const QString& key : settings.allKeys()) {
// 		SysFileIcon icon;
// 		int val = settings.value(key).toInt();
// 		if (key == MY_COMPUTER_CLS) {
// 			icon.name = "This PC";
// 			icon.CLSID = QString(MY_COMPUTER_CLS);
// 			icon.hideIcon = val;
// 			icon.iconIndex = 16;
// 			icon.DllName = "shell32.dll";
// 			icon.sfName = SpecialFolders::ThisPc;
// 		}
// 		else if (key == CONTROL_PANEL_CLS) {
// 			icon.name = "Control Panel";
// 			icon.CLSID = QString(CONTROL_PANEL_CLS);
// 			icon.hideIcon = val;
// 			icon.iconIndex = 58;
// 			icon.DllName = "shell32.dll";
// 			icon.sfName = SpecialFolders::ControlPanel;
// 		}
// 		else if (key == NETWORK_CLS) {

// 			icon.name = "Network";
// 			icon.CLSID = QString(NETWORK_CLS);
// 			icon.hideIcon = val;
// 			icon.iconIndex = 18;
// 			icon.DllName = "shell32.dll";
// 			icon.sfName = SpecialFolders::Network;
// 		}
// 		else if (key == RECYCLE_BIN_CLS) {
// 			icon.name = "Recycle Bin";
// 			icon.CLSID = QString(RECYCLE_BIN_CLS);
// 			icon.hideIcon = val;
// 			icon.iconIndex = 32;
// 			icon.DllName = "shell32.dll";
// 			icon.sfName = SpecialFolders::RecycleBin;
// 		}
// 		else if (key == USER_FOLDER_CLS) {
// 			icon.name = "User";
// 			icon.CLSID = QString(USER_FOLDER_CLS);
// 			icon.hideIcon = val;
// 			icon.iconIndex = 118;
// 			icon.DllName = "imageres.dll";
// 			icon.sfName = SpecialFolders::User;
// 		}
// 		//有状态再存储
// 		if (!icon.name.isEmpty()) {
// 			regeditValue->insert(key, icon);
// 		}
// 	}
// 	return regeditValue;
// }



// QIcon SysInfoUtil::ExtractIconFromExe(const QString& exePath, int iconIndex)
// {
// 	// 获取HICON句柄
// 	HICON hIcon = ExtractIcon(GetModuleHandle(nullptr), exePath.toStdWString().c_str(), iconIndex);

// 	if (hIcon != nullptr) {
// 		// 将HICON转换为QIcon
// 		QIcon icon = QtWin::fromHICON(hIcon);
// 		DestroyIcon(hIcon); // 释放HICON资源
// 		return icon;
// 	}
// 	else {
// 		// 如果无法获取图标，则返回一个空的QIcon
// 		return QIcon();
// 	}
// }

// QIcon SysInfoUtil::SysFileIconByDLL(int index, int size, QString dllName)
// {
// 	HICON hIcon;
// 	UINT uIconId;
// 	QString tmp = "C:\\Windows\\System32\\" + dllName;
// 	index = index - 1;
// #ifdef UNICODE
// 	LPCWSTR dllPath = (LPCWSTR)tmp.utf16();
// #else
// 	LPCWSTR dllPath = (LPCSTR)tmp.toLocal8Bit().constData();
// #endif
// 	// 动态加载 user32.dll
// 	HMODULE hUser32 = LoadLibrary(L"user32.dll");
// 	if (!hUser32) {
// 		std::cerr << "Failed to load user32.dll" << std::endl;
// 		return QIcon();
// 	}
// 	// 获取函数地址
// 	PrivateExtractIcons_t PrivateExtractIcons =
// 		(PrivateExtractIcons_t)GetProcAddress(hUser32, "PrivateExtractIconsW");
// 	if (!PrivateExtractIcons) {
// 		std::cerr << "Failed to get PrivateExtractIcons address" << std::endl;
// 		FreeLibrary(hUser32);
// 		return QIcon();
// 	}

// 	// 提取图标
// 	UINT result = PrivateExtractIcons(dllPath, index, size, size, &hIcon, &uIconId, 1, 0);
// 	//失败返回文件夹图标
// 	if (result == 0) {
// 		PrivateExtractIcons(dllPath, 0, size, size, &hIcon, &uIconId, 1, 0);
// 	}
// 	QIcon icon;
// 	icon.addPixmap(QtWin::fromHICON(hIcon));
// 	// 销毁图标
// 	DestroyIcon(hIcon);
// 	return icon;
// }

// QString SysInfoUtil::MkMutiDir(const QString path)
// {
// 	QDir dir(path);
// 	if (dir.exists(path)) {
// 		return path;
// 	}
// 	QString parentDir = MkMutiDir(path.mid(0, path.lastIndexOf('/')));
// 	QString dirname = path.mid(path.lastIndexOf('/') + 1);
// 	QDir parentPath(parentDir);
// 	if (!dirname.isEmpty())
// 		parentPath.mkpath(dirname);
// 	return parentDir + "/" + dirname;
// }

// QString SysInfoUtil::SaveIconInDataDir(const QString iconName, const QIcon* icon)
// {
// 	QString appTempDataPath = QDir::tempPath();
// 	QDir dirPath(appTempDataPath);
// 	dirPath.cdUp();
// 	QString iconPath = dirPath.path() + "/ZhuoMian/Icon/";
// 	QDir iconDir(iconPath);
// 	if (!iconDir.exists()) {
// 		MkMutiDir(iconPath);
// 	}
// 	QString iconFullPath = iconPath + iconName + ".png";
// 	icon->pixmap(icon->actualSize(QSize(256, 256))).save(iconFullPath);
// 	return iconFullPath;
// }

// QString SysInfoUtil::GetDesktopWallpaperPath()
// {
// 	// 使用QSettings来访问注册表中的配置信息，指定组织名、应用程序名以及注册表路径模式
// 	QSettings settings("HKEY_CURRENT_USER\\Control Panel\\Desktop", QSettings::NativeFormat);
// 	// 尝试获取名为"Wallpaper"的键对应的值，这里获取到的是QVariant类型
// 	QVariant wallpaperVariant = settings.value("Wallpaper");
// 	if (wallpaperVariant.isValid())
// 	{
// 		QString wallpaperPath = wallpaperVariant.toString();
// 		// 返回绝对路径形式的壁纸路径，通过QFileInfo来处理可能的相对路径等情况
// 		return QFileInfo(wallpaperPath).absoluteFilePath();
// 	}
// 	else
// 	{
// 		qWarning() << "Failed to retrieve desktop wallpaper path from the registry";
// 		return QString();
// 	}
// 	/*HKEY hKey;
// 	LONG result = RegOpenKeyEx(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, KEY_READ, &hKey);
// 	if (result == ERROR_SUCCESS)
// 	{
// 		wchar_t wallpaperPath[MAX_PATH] = {};
// 		DWORD dwType, dwDataSize = sizeof(wallpaperPath);
// 		result = RegQueryValueEx(hKey, L"Wallpaper", NULL, &dwType, (LPBYTE)wallpaperPath, &dwDataSize);

// 		if (result == ERROR_SUCCESS && dwType == REG_SZ)
// 		{
// 			QString path(QString::fromWCharArray(wallpaperPath));
// 			RegCloseKey(hKey);
// 			return QFileInfo(path).absoluteFilePath();
// 		}
// 	}

// 	RegCloseKey(hKey);
// 	qWarning() << "Failed to retrieve desktop wallpaper path from the registry";
// 	return QString();*/
// }

// void SysInfoUtil::AddSysFileIcon(QList<FileIcon>* lists)
// {
// 	auto regeditValues = SysInfoUtil::SysIconRegedit_State();
// 	// 使用迭代器遍历
// 	for (QMap<QString, SysFileIcon>::iterator it = regeditValues->begin(); it != regeditValues->end(); ++it) {
// 		SysFileIcon value = it.value();
// 		if (value.hideIcon == 1) {
// 			continue;
// 		}
// 		//qDebug() << value.name << value.iconIndex;
// 		QIcon sysIcon = SysInfoUtil::SysFileIconByDLL(value.iconIndex, 256, value.DllName);
// 		FileIcon icon;
// 		icon.baseName = value.name;
// 		icon.fullName = value.name;
// 		icon.clsid = value.CLSID;
// 		icon.fileIcon = sysIcon;
// 		icon.fileType = FileType::SpecialFolder;
// 		icon.sf = value.sfName;
// 		lists->append(icon);
// 	}
// }
// QIcon SysInfoUtil::ExtractSystemIcon(SystemIcon iconName)
// {
// 	switch (iconName)
// 	{
// 	case PERSONALIZE:
// 		return SysInfoUtil::SysFileIconByDLL(1, 16, "display.dll");
// 	case DISPLAY_SETTINGS:
// 		return SysInfoUtil::SysFileIconByDLL(1, 16, "themecpl.dll");
// 	}
// 	return QIcon();
// }

// bool IsKeyDown(int nVirtKey)
// {
// 	SHORT status = (GetKeyState(nVirtKey) & 0x8000);

// 	return (status != 0);
// }

