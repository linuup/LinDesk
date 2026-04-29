#pragma once
#include "services/SysInfoUtil.h"
#include <QtWinExtras/qwinfunctions.h>
#include <iostream>
#include <shobjidl.h>
#include <shlobj.h>
#include <comdef.h>
#include <QImageReader>
#include <QStandardPaths>
#include <QFileInfoList>
#include <QQueue>
#include <QPainter>

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

bool isValidWallpaper(const QString& path) {
    if (path.isEmpty()) return false;
    QFileInfo info(path);
    if (!info.exists() || info.size() <= 0) return false;
    
    // Quick check if it's actually an image format we can read
    QImageReader reader(path);
    if (reader.format().isEmpty()) return false;
    
    return true;
}

QString SysInfoUtil::GetRobustWallpaperPath()
{
    QString path;

    // 1. Try IDesktopWallpaper (Recommended for Win10/11)
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) {
        IDesktopWallpaper* pDesktopWallpaper = nullptr;
        hr = CoCreateInstance(CLSID_DesktopWallpaper, NULL, CLSCTX_ALL, IID_IDesktopWallpaper, (void**)&pDesktopWallpaper);
        if (SUCCEEDED(hr)) {
            LPWSTR wallpaperPath = nullptr;
            // Get wallpaper for the primary monitor (NULL uses the default if not multi-monitor specific)
            hr = pDesktopWallpaper->GetWallpaper(NULL, &wallpaperPath);
            if (SUCCEEDED(hr) && wallpaperPath) {
                path = QString::fromWCharArray(wallpaperPath);
                CoTaskMemFree(wallpaperPath);
            }
            pDesktopWallpaper->Release();
        }
        if (hr != RPC_E_CHANGED_MODE) CoUninitialize();
    }

    if (isValidWallpaper(path)) return path;

    // 2. Fall back to SystemParametersInfo
    path = GetWallpaperUsingAPI();
    if (isValidWallpaper(path)) return path;

    // 3. Fall back to Registry
    path = GetDesktopWallpaperPath();
    if (isValidWallpaper(path)) return path;

    // 4. Final attempt: Scan the Themes directory for Transcoded_000, etc.
    // Sometimes SPI_GETDESKWALLPAPER returns a zero-byte "TranscodedWallpaper" 
    // while the actual content is in Transcoded_000
    QString themesDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) 
                        + "/../Roaming/Microsoft/Windows/Themes";
    QDir dir(themesDir);
    if (dir.exists()) {
        QStringList filters;
        filters << "Transcoded_*";
        QFileInfoList list = dir.entryInfoList(filters, QDir::Files, QDir::Time);
        for (const QFileInfo& fileInfo : list) {
            if (isValidWallpaper(fileInfo.absoluteFilePath())) {
                return fileInfo.absoluteFilePath();
            }
        }
    }

    return path; // Return whatever we found, even if invalid, as a last resort
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
QImage SysInfoUtil::autoCropImage(const QImage& source, bool* wasCropped)
{
	if (wasCropped) *wasCropped = false;
	if (source.isNull()) return source;

	int width = source.width();
	int height = source.height();
	int left = width, right = 0, top = height, bottom = 0;
	bool hasVisiblePixel = false;

	// 遍历所有像素，找到非透明像素的边界
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			if (qAlpha(source.pixel(x, y)) > 10) { // 使用较小的阈值过滤极微弱的透明像素
				if (x < left) left = x;
				if (x > right) right = x;
				if (y < top) top = y;
				if (y > bottom) bottom = y;
				hasVisiblePixel = true;
			}
		}
	}

	if (!hasVisiblePixel) return source;

	// 计算有效区域并增加 2 像素的安全外边距 (Safety Margin)
	int cropWidth = qMin(width, right - left + 1 + 4);
	int cropHeight = qMin(height, bottom - top + 1 + 4);
	int cropLeft = qMax(0, left - 2);
	int cropTop = qMax(0, top - 2);

	// 如果裁剪后的区域已经接近原尺寸（留白很少），则不作处理
	if ((cropWidth >= width * 0.95 && cropHeight >= height * 0.95) || 
		(cropWidth < 8 || cropHeight < 8)) {
		return source;
	}

	// 裁剪并重新缩放回到原尺寸
	QRect rect(cropLeft, cropTop, cropWidth, cropHeight);
	QImage cropped = source.copy(rect);
	
	if (wasCropped) *wasCropped = true;

	return cropped.scaled(source.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

namespace {
bool isNearWhiteOpaque(QRgb pixel) {
    return qAlpha(pixel) >= 245 && qRed(pixel) >= 235 && qGreen(pixel) >= 235 && qBlue(pixel) >= 235;
}
bool isCloseToMatteColor(QRgb pixel, const QColor& matteColor, int tolerance) {
    if (qAlpha(pixel) < 245) return false;
    return qAbs(qRed(pixel) - matteColor.red()) <= tolerance &&
           qAbs(qGreen(pixel) - matteColor.green()) <= tolerance &&
           qAbs(qBlue(pixel) - matteColor.blue()) <= tolerance;
}
}

QPixmap SysInfoUtil::sanitizeFolderPreviewMatte(const QPixmap& source)
{
    if (source.isNull()) return source;
    QImage image = source.toImage().convertToFormat(QImage::Format_ARGB32);
    if (image.isNull() || image.width() < 8 || image.height() < 8) return source;

    const QRgb topLeft = image.pixel(0, 0);
    const QRgb topRight = image.pixel(image.width() - 1, 0);
    const QRgb bottomLeft = image.pixel(0, image.height() - 1);
    const QRgb bottomRight = image.pixel(image.width() - 1, image.height() - 1);
    if (!isNearWhiteOpaque(topLeft) || !isNearWhiteOpaque(topRight) ||
        !isNearWhiteOpaque(bottomLeft) || !isNearWhiteOpaque(bottomRight)) return source;

    const QColor matteColor = QColor::fromRgb(topLeft);
    const int tolerance = 16;
    QImage result = image;
    QVector<uchar> visited(image.width() * image.height(), 0);
    QQueue<QPoint> queue;

    auto enqueue = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= image.width() || y >= image.height()) return;
        const int index = y * image.width() + x;
        if (visited[index]) return;
        if (!isCloseToMatteColor(result.pixel(x, y), matteColor, tolerance)) return;
        visited[index] = 1;
        queue.enqueue(QPoint(x, y));
    };

    for (int x = 0; x < image.width(); ++x) { enqueue(x, 0); enqueue(x, image.height() - 1); }
    for (int y = 0; y < image.height(); ++y) { enqueue(0, y); enqueue(image.width() - 1, y); }

    int clearedPixels = 0;
    while (!queue.isEmpty()) {
        const QPoint point = queue.dequeue();
        result.setPixel(point.x(), point.y(), qRgba(qRed(result.pixel(point.x(), point.y())),
            qGreen(result.pixel(point.x(), point.y())), qBlue(result.pixel(point.x(), point.y())), 0));
        ++clearedPixels;
        enqueue(point.x() - 1, point.y()); enqueue(point.x() + 1, point.y());
        enqueue(point.x(), point.y() - 1); enqueue(point.x(), point.y() + 1);
    }
    if (clearedPixels == 0) return source;

    auto decontaminateChannel = [](int channel, int matteChannel, int alpha) -> int {
        if (alpha <= 0) return 0;
        const double a = alpha / 255.0;
        const double corrected = (channel - (1.0 - a) * matteChannel) / a;
        return qBound(0, qRound(corrected), 255);
    };

    for (int y = 0; y < result.height(); ++y) {
        for (int x = 0; x < result.width(); ++x) {
            const int idx = y * result.width() + x;
            if (visited[idx]) continue;
            const QRgb pixel = result.pixel(x, y);
            const int alpha = qAlpha(pixel);
            if (alpha <= 0 || alpha >= 255) continue;

            bool touchesClearedMatte = false;
            for (int dy = -1; dy <= 1 && !touchesClearedMatte; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && ny >= 0 && nx < result.width() && ny < result.height() && visited[ny * result.width() + nx]) {
                        touchesClearedMatte = true; break;
                    }
                }
            }
            if (!touchesClearedMatte) continue;

            result.setPixel(x, y, qRgba(
                decontaminateChannel(qRed(pixel), matteColor.red(), alpha),
                decontaminateChannel(qGreen(pixel), matteColor.green(), alpha),
                decontaminateChannel(qBlue(pixel), matteColor.blue(), alpha), alpha));
        }
    }
    return QPixmap::fromImage(result);
}
