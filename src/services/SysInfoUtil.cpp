#pragma once
#include "services/SysInfoUtil.h"
#include <QtWinExtras/qwinfunctions.h>
#include <iostream>
#include <shobjidl.h>
#include <shlobj.h>
#include <comdef.h>
#include <wrl/client.h>
#include <propsys.h>
#include <propkey.h>
#include <propvarutil.h>
#include <QImageReader>
#include <QStandardPaths>
#include <QFileInfoList>
#include <QQueue>
#include <QPainter>
#include <QXmlStreamReader>

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

QPixmap SysInfoUtil::sanitizeFolderPreviewMatte(const QPixmap& source)
{
    if (source.isNull()) return source;
    QImage image = source.toImage().convertToFormat(QImage::Format_ARGB32);
    if (image.isNull() || image.width() < 8 || image.height() < 8) return source;

    const int width = image.width();
    const int height = image.height();

    const QRgb corners[4] = {
        image.pixel(0, 0),
        image.pixel(width - 1, 0),
        image.pixel(0, height - 1),
        image.pixel(width - 1, height - 1)
    };

    // 如果四角已全部接近完全透明，说明已经过抠图或本身就是透明底，直接返回
    int transparentCorners = 0;
    for (int i = 0; i < 4; ++i) {
        if (qAlpha(corners[i]) <= 15) {
            ++transparentCorners;
        }
    }
    if (transparentCorners >= 3) {
        return source;
    }

    // 检查四角不透明度：底色背景应接近完全不透明 (alpha >= 220)
    for (int i = 0; i < 4; ++i) {
        if (qAlpha(corners[i]) < 220) {
            // 四角包含透明/半透明，无需按纯色底抠图，安全保留原图
            return source;
        }
    }

    // S2.a: 采样四角颜色，求众数/一致性色，支持白/黑/灰等任意纯色底
    const int cornerTolerance = 24;
    auto colorDist = [](QRgb a, QRgb b) -> int {
        const int dr = qAbs(qRed(a) - qRed(b));
        const int dg = qAbs(qGreen(a) - qGreen(b));
        const int db = qAbs(qBlue(a) - qBlue(b));
        return qMax(dr, qMax(dg, db));
    };

    int bestCorner = 0;
    int maxMatches = 0;
    for (int i = 0; i < 4; ++i) {
        int matches = 0;
        for (int j = 0; j < 4; ++j) {
            if (colorDist(corners[i], corners[j]) <= cornerTolerance) {
                ++matches;
            }
        }
        if (matches > maxMatches) {
            maxMatches = matches;
            bestCorner = i;
        }
    }

    // 若四角颜色不一致（至少 3 角吻合），无法可靠判定 matte，安全保留原图
    if (maxMatches < 3) {
        return source;
    }

    // 计算吻合角的平均色作为 matteColor
    int sumR = 0, sumG = 0, sumB = 0, count = 0;
    for (int i = 0; i < 4; ++i) {
        if (colorDist(corners[bestCorner], corners[i]) <= cornerTolerance) {
            sumR += qRed(corners[i]);
            sumG += qGreen(corners[i]);
            sumB += qBlue(corners[i]);
            ++count;
        }
    }
    const QColor matteColor(sumR / count, sumG / count, sumB / count);

    const int floodTolerance = 20;
    QImage result = image;
    QVector<uchar> visited(width * height, 0);
    QQueue<QPoint> queue;

    auto isMattePixel = [&](QRgb pix) {
        if (qAlpha(pix) < 220) return false;
        return qAbs(qRed(pix) - matteColor.red()) <= floodTolerance &&
               qAbs(qGreen(pix) - matteColor.green()) <= floodTolerance &&
               qAbs(qBlue(pix) - matteColor.blue()) <= floodTolerance;
    };

    auto enqueue = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        const int index = y * width + x;
        if (visited[index]) return;
        if (!isMattePixel(result.pixel(x, y))) return;
        visited[index] = 1;
        queue.enqueue(QPoint(x, y));
    };

    for (int x = 0; x < width; ++x) { enqueue(x, 0); enqueue(x, height - 1); }
    for (int y = 0; y < height; ++y) { enqueue(0, y); enqueue(width - 1, y); }

    int clearedPixels = 0;
    while (!queue.isEmpty()) {
        const QPoint point = queue.dequeue();
        result.setPixel(point.x(), point.y(), qRgba(0, 0, 0, 0));
        ++clearedPixels;
        enqueue(point.x() - 1, point.y());
        enqueue(point.x() + 1, point.y());
        enqueue(point.x(), point.y() - 1);
        enqueue(point.x(), point.y() + 1);
    }
    if (clearedPixels == 0) return source;

    // S2.b: 全图对 alpha∈(0,255) 像素做反算去污染（取消 3x3 邻接限制，全图应用）
    auto decontaminateChannel = [](int channel, int matteChannel, int alpha) -> int {
        if (alpha <= 0) return 0;
        const double a = alpha / 255.0;
        const double corrected = (channel - (1.0 - a) * matteChannel) / a;
        return qBound(0, qRound(corrected), 255);
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int idx = y * width + x;
            if (visited[idx]) continue;
            const QRgb pixel = result.pixel(x, y);
            const int alpha = qAlpha(pixel);
            if (alpha <= 0 || alpha >= 255) continue;

            const int dr = qAbs(qRed(pixel) - matteColor.red());
            const int dg = qAbs(qGreen(pixel) - matteColor.green());
            const int db = qAbs(qBlue(pixel) - matteColor.blue());

            // 低 alpha 且接近 matte 的残留边缘杂色置零
            if (alpha <= 24 && dr <= floodTolerance && dg <= floodTolerance && db <= floodTolerance) {
                result.setPixel(x, y, qRgba(0, 0, 0, 0));
                continue;
            }

            result.setPixel(x, y, qRgba(
                decontaminateChannel(qRed(pixel), matteColor.red(), alpha),
                decontaminateChannel(qGreen(pixel), matteColor.green(), alpha),
                decontaminateChannel(qBlue(pixel), matteColor.blue(), alpha),
                alpha));
        }
    }

    // S2.c: 抠除后 autoCrop 再跑一次
    bool wasCropped = false;
    const QImage cropped = autoCropImage(result, &wasCropped);
    if (wasCropped && !cropped.isNull()) {
        result = cropped;
    }

    return QPixmap::fromImage(result);
}

namespace {
struct ScopedComInit {
    HRESULT hr = E_FAIL;
    ScopedComInit() {
        hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    }
    ~ScopedComInit() {
        if (hr == S_OK || hr == S_FALSE) {
            CoUninitialize();
        }
    }
};
}

QIcon SysInfoUtil::LoadShellItemIcon(const QString& path, int side)
{
    if (path.trimmed().isEmpty()) {
        return QIcon();
    }
    ScopedComInit comInit;

    QString parsePath = path.trimmed();
    if (parsePath.startsWith(QStringLiteral("shell:Appsfolder\\"), Qt::CaseInsensitive)) {
        // direct
    } else if (parsePath.contains(QLatin1Char('!')) && !parsePath.contains(QLatin1Char('\\')) && !parsePath.contains(QLatin1Char('/')) && !parsePath.contains(QLatin1Char(':'))) {
        parsePath = QStringLiteral("shell:Appsfolder\\") + parsePath;
    }

    const std::wstring nativePath = QDir::toNativeSeparators(parsePath).toStdWString();
    Microsoft::WRL::ComPtr<IShellItem> shellItem;
    HRESULT hr = SHCreateItemFromParsingName(nativePath.c_str(), nullptr, IID_PPV_ARGS(&shellItem));
    if (FAILED(hr) || !shellItem) {
        return QIcon();
    }

    Microsoft::WRL::ComPtr<IShellItemImageFactory> imageFactory;
    hr = shellItem.As(&imageFactory);
    if (FAILED(hr) || !imageFactory) {
        return QIcon();
    }

    const int targetSide = (side > 0) ? side : 48;
    SIZE sz = { targetSide, targetSide };
    HBITMAP hBmp = nullptr;
    // 优先尝试纯图标，允许更大尺寸
    hr = imageFactory->GetImage(sz, SIIGBF_BIGGERSIZEOK | SIIGBF_ICONONLY, &hBmp);
    if (FAILED(hr) || !hBmp) {
        // 如果 ICONONLY 失败，尝试标准 SIIGBF_BIGGERSIZEOK
        hr = imageFactory->GetImage(sz, SIIGBF_BIGGERSIZEOK, &hBmp);
    }
    if (FAILED(hr) || !hBmp) {
        // 再次回退普通无标志提取
        hr = imageFactory->GetImage(sz, 0, &hBmp);
    }

    if (SUCCEEDED(hr) && hBmp) {
        QImage img = QtWin::imageFromHBITMAP(hBmp, QtWin::HBitmapAlpha);
        DeleteObject(hBmp);
        if (!img.isNull()) {
            img = img.convertToFormat(QImage::Format_ARGB32);
            for (int y = 0; y < img.height(); ++y) {
                QRgb* scan = reinterpret_cast<QRgb*>(img.scanLine(y));
                for (int x = 0; x < img.width(); ++x) {
                    const int a = qAlpha(scan[x]);
                    if (a <= 8) {
                        scan[x] = 0;
                    }
                }
            }
            return QIcon(QPixmap::fromImage(img));
        }
    }
    return QIcon();
}

QString SysInfoUtil::GetAumidFromShortcut(const QString& lnkPath)
{
    if (lnkPath.trimmed().isEmpty()) {
        return QString();
    }
    ScopedComInit comInit;
    const std::wstring nativePath = QDir::toNativeSeparators(lnkPath.trimmed()).toStdWString();

    // 1. 从快捷方式 PropertyStore 中读取 PKEY_AppUserModel_ID
    Microsoft::WRL::ComPtr<IPropertyStore> propStore;
    HRESULT hr = SHGetPropertyStoreFromParsingName(nativePath.c_str(), nullptr, GPS_DEFAULT, IID_PPV_ARGS(&propStore));
    if (SUCCEEDED(hr) && propStore) {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        hr = propStore->GetValue(PKEY_AppUserModel_ID, &pv);
        if (SUCCEEDED(hr)) {
            if (pv.vt == VT_LPWSTR && pv.pwszVal) {
                QString aumid = QString::fromWCharArray(pv.pwszVal).trimmed();
                PropVariantClear(&pv);
                if (!aumid.isEmpty()) {
                    return aumid;
                }
            }
            PropVariantClear(&pv);
        }
    }

    // 2. 从 IShellLink 读取目标或参数（处理形如 shell:Appsfolder\<AUMID>）
    Microsoft::WRL::ComPtr<IShellLinkW> shellLink;
    hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink));
    if (SUCCEEDED(hr) && shellLink) {
        Microsoft::WRL::ComPtr<IPersistFile> persistFile;
        if (SUCCEEDED(shellLink.As(&persistFile))) {
            if (SUCCEEDED(persistFile->Load(nativePath.c_str(), STGM_READ))) {
                WCHAR target[MAX_PATH] = { 0 };
                shellLink->GetPath(target, MAX_PATH, nullptr, SLGP_RAWPATH);
                QString targetStr = QString::fromWCharArray(target).trimmed();
                const QString appsPrefix = QStringLiteral("shell:Appsfolder\\");
                int idx = targetStr.indexOf(appsPrefix, 0, Qt::CaseInsensitive);
                if (idx >= 0) {
                    return targetStr.mid(idx + appsPrefix.length()).trimmed();
                }

                WCHAR args[1024] = { 0 };
                shellLink->GetArguments(args, 1024);
                QString argsStr = QString::fromWCharArray(args).trimmed();
                idx = argsStr.indexOf(appsPrefix, 0, Qt::CaseInsensitive);
                if (idx >= 0) {
                    return argsStr.mid(idx + appsPrefix.length()).trimmed();
                }
            }
        }
    }

    return QString();
}

QString SysInfoUtil::ResolveShortcutTargetPath(const QString& lnkPath)
{
    if (lnkPath.trimmed().isEmpty()) {
        return QString();
    }
    ScopedComInit comInit;
    const std::wstring nativePath = QDir::toNativeSeparators(lnkPath.trimmed()).toStdWString();

    Microsoft::WRL::ComPtr<IShellLinkW> shellLink;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink));
    if (FAILED(hr) || !shellLink) {
        return QString();
    }

    Microsoft::WRL::ComPtr<IPersistFile> persistFile;
    if (FAILED(shellLink.As(&persistFile)) || !persistFile) {
        return QString();
    }

    if (FAILED(persistFile->Load(nativePath.c_str(), STGM_READ))) {
        return QString();
    }

    // 1. 尝试从 PIDL 获取目标路径（对各种 Shell 路径最通用）
    PIDLIST_ABSOLUTE targetPidl = nullptr;
    if (SUCCEEDED(shellLink->GetIDList(&targetPidl)) && targetPidl) {
        PWSTR pszPath = nullptr;
        if (SUCCEEDED(SHGetNameFromIDList(targetPidl, SIGDN_FILESYSPATH, &pszPath)) && pszPath) {
            QString res = QString::fromWCharArray(pszPath);
            CoTaskMemFree(pszPath);
            ILFree(targetPidl);
            if (!res.isEmpty()) {
                return res;
            }
        } else if (SUCCEEDED(SHGetNameFromIDList(targetPidl, SIGDN_DESKTOPABSOLUTEPARSING, &pszPath)) && pszPath) {
            QString res = QString::fromWCharArray(pszPath);
            CoTaskMemFree(pszPath);
            ILFree(targetPidl);
            if (!res.isEmpty()) {
                return res;
            }
        }
        ILFree(targetPidl);
    }

    // 2. 尝试从 GetPath 获取直接物理路径
    WCHAR target[MAX_PATH] = { 0 };
    WIN32_FIND_DATAW wfd = { 0 };
    if (SUCCEEDED(shellLink->GetPath(target, MAX_PATH, &wfd, SLGP_RAWPATH)) && target[0] != L'\0') {
        return QString::fromWCharArray(target);
    }

    return QString();
}

QIcon SysInfoUtil::LoadUwpAppIconFromAumid(const QString& aumid, int side)
{
    if (aumid.trimmed().isEmpty()) {
        return QIcon();
    }
    const QString trimmedAumid = aumid.trimmed();

    // 1. 优先尝试直接用 shell:Appsfolder\<AUMID> 走 LoadShellItemIcon
    QIcon shellIcon = LoadShellItemIcon(QStringLiteral("shell:Appsfolder\\") + trimmedAumid, side);
    if (!shellIcon.isNull()) {
        return shellIcon;
    }

    // 2. 解析 AUMID 中的 PackageFamilyName
    QString packageFamilyName = trimmedAumid;
    const int exclIdx = trimmedAumid.indexOf(QLatin1Char('!'));
    if (exclIdx > 0) {
        packageFamilyName = trimmedAumid.left(exclIdx);
    }

    // 3. 查注册表定位 PackageRootFolder
    QString packageRootFolder;
    auto searchRegistryForPackageRoot = [&](const QString& rootPath) -> QString {
        const QString familiesPath = rootPath + QStringLiteral("\\Families\\") + packageFamilyName;
        QSettings famSettings(familiesPath, QSettings::NativeFormat);
        const QStringList subkeys = famSettings.childGroups();
        for (const QString& subkey : subkeys) {
            const QString pkgRegPath = rootPath + QStringLiteral("\\Packages\\") + subkey;
            QSettings pkgSettings(pkgRegPath, QSettings::NativeFormat);
            const QString rootFolder = pkgSettings.value(QStringLiteral("PackageRootFolder")).toString();
            if (!rootFolder.isEmpty() && QDir(rootFolder).exists()) {
                return rootFolder;
            }
        }

        const QString packagesPath = rootPath + QStringLiteral("\\Packages");
        QSettings pkgListSettings(packagesPath, QSettings::NativeFormat);
        for (const QString& pkgName : pkgListSettings.childGroups()) {
            if (pkgName.startsWith(packageFamilyName, Qt::CaseInsensitive)) {
                QSettings subSettings(packagesPath + QStringLiteral("\\") + pkgName, QSettings::NativeFormat);
                const QString rootFolder = subSettings.value(QStringLiteral("PackageRootFolder")).toString();
                if (!rootFolder.isEmpty() && QDir(rootFolder).exists()) {
                    return rootFolder;
                }
            }
        }
        return QString();
    };

    const QString hkcuRepo = QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\Repository");
    packageRootFolder = searchRegistryForPackageRoot(hkcuRepo);
    if (packageRootFolder.isEmpty()) {
        const QString hklmRepo = QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\Repository");
        packageRootFolder = searchRegistryForPackageRoot(hklmRepo);
    }

    if (packageRootFolder.isEmpty() || !QDir(packageRootFolder).exists()) {
        return QIcon();
    }

    // 4. 解析 AppxManifest.xml 提取 Logo 相对路径
    QString logoRelativePath;
    const QString manifestPath = QDir(packageRootFolder).filePath(QStringLiteral("AppxManifest.xml"));
    QFile manifestFile(manifestPath);
    if (manifestFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QXmlStreamReader xml(&manifestFile);
        while (!xml.atEnd() && !xml.hasError()) {
            QXmlStreamReader::TokenType token = xml.readNext();
            if (token == QXmlStreamReader::StartElement) {
                const QStringRef name = xml.name();
                if (name == QStringLiteral("VisualElements") || name.endsWith(QStringLiteral(":VisualElements"))) {
                    const auto attrs = xml.attributes();
                    if (attrs.hasAttribute(QStringLiteral("Square150x150Logo"))) {
                        logoRelativePath = attrs.value(QStringLiteral("Square150x150Logo")).toString();
                    } else if (attrs.hasAttribute(QStringLiteral("Square44x44Logo"))) {
                        logoRelativePath = attrs.value(QStringLiteral("Square44x44Logo")).toString();
                    } else if (attrs.hasAttribute(QStringLiteral("Logo"))) {
                        logoRelativePath = attrs.value(QStringLiteral("Logo")).toString();
                    }
                    if (!logoRelativePath.isEmpty()) {
                        break;
                    }
                }
            }
        }
    }

    // 5. 寻找最优 scale / targetsize 图片文件
    auto findBestImageCandidate = [&](const QString& baseRelative) -> QString {
        if (baseRelative.isEmpty()) return QString();
        const QString absPath = QDir(packageRootFolder).filePath(baseRelative);
        if (QFile::exists(absPath)) {
            return absPath;
        }

        const QFileInfo fi(absPath);
        const QString dir = fi.absolutePath();
        const QString baseName = fi.completeBaseName();
        const QString ext = fi.suffix().isEmpty() ? QStringLiteral("png") : fi.suffix();

        const QStringList preferredScales = {
            QStringLiteral(".scale-200."), QStringLiteral(".scale-150."),
            QStringLiteral(".scale-125."), QStringLiteral(".scale-100."),
            QStringLiteral(".scale-400."), QStringLiteral(".targetsize-48."),
            QStringLiteral(".targetsize-256."), QStringLiteral(".targetsize-32.")
        };
        for (const QString& scaleTag : preferredScales) {
            const QString candidate = dir + QStringLiteral("/") + baseName + scaleTag + ext;
            if (QFile::exists(candidate)) {
                return candidate;
            }
        }

        QDir assetDir(dir);
        const QStringList matches = assetDir.entryList(QStringList() << (baseName + QStringLiteral("*.png")), QDir::Files);
        if (!matches.isEmpty()) {
            return assetDir.filePath(matches.first());
        }
        return QString();
    };

    QString bestImagePath = findBestImageCandidate(logoRelativePath);
    if (bestImagePath.isEmpty()) {
        const QString assetsDir = QDir(packageRootFolder).filePath(QStringLiteral("Assets"));
        if (QDir(assetsDir).exists()) {
            const QStringList fallbackFilters = {
                QStringLiteral("*StoreLogo*.png"), QStringLiteral("*Logo*.png"), QStringLiteral("*Square*.png")
            };
            for (const QString& flt : fallbackFilters) {
                const QStringList entries = QDir(assetsDir).entryList(QStringList() << flt, QDir::Files);
                if (!entries.isEmpty()) {
                    bestImagePath = QDir(assetsDir).filePath(entries.first());
                    break;
                }
            }
        }
    }

    if (!bestImagePath.isEmpty() && QFile::exists(bestImagePath)) {
        QPixmap pix(bestImagePath);
        if (!pix.isNull()) {
            const int targetSide = (side > 0) ? side : 48;
            if (pix.width() > targetSide || pix.height() > targetSide) {
                pix = pix.scaled(targetSide, targetSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            return QIcon(pix);
        }
    }

    return QIcon();
}

QIcon SysInfoUtil::GetBlankFileFallbackIcon(int side)
{
    const int targetSide = (side > 0) ? side : 48;
    // 1. 优先尝试从 shell32.dll 读取 0 号空白文件图标
    QIcon dllIcon = SysFileIconByDLL(0, targetSide, QStringLiteral("shell32.dll"));
    if (!dllIcon.isNull()) {
        QPixmap px = dllIcon.pixmap(targetSide, targetSide);
        if (!px.isNull()) {
            if (px.width() != targetSide || px.height() != targetSide) {
                px = px.scaled(targetSide, targetSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            return QIcon(px);
        }
    }

    // 2. 内存绘制纯白优雅折角文档图标作为终极防线
    QPixmap fallbackPix(targetSide, targetSide);
    fallbackPix.fill(Qt::transparent);
    {
        QPainter p(&fallbackPix);
        p.setRenderHint(QPainter::Antialiasing, true);

        const qreal pad = targetSide * 0.12;
        const qreal fold = targetSide * 0.22;
        const qreal w = targetSide - pad * 2.0;
        const qreal h = targetSide - pad * 1.8;
        const qreal left = pad;
        const qreal top = pad * 0.9;
        const qreal right = left + w;
        const qreal bottom = top + h;

        QPolygonF docPoly;
        docPoly << QPointF(left, top)
                << QPointF(right - fold, top)
                << QPointF(right, top + fold)
                << QPointF(right, bottom)
                << QPointF(left, bottom);

        p.setBrush(QColor(250, 250, 252));
        p.setPen(QPen(QColor(180, 185, 195), qMax(1.0, targetSide * 0.025)));
        p.drawPolygon(docPoly);

        QPolygonF foldPoly;
        foldPoly << QPointF(right - fold, top)
                 << QPointF(right - fold, top + fold)
                 << QPointF(right, top + fold);
        p.setBrush(QColor(230, 233, 240));
        p.setPen(QPen(QColor(180, 185, 195), qMax(1.0, targetSide * 0.025)));
        p.drawPolygon(foldPoly);
    }
    return QIcon(fallbackPix);
}

QIcon SysInfoUtil::CreateDocumentPlateIcon(const QIcon& innerIcon, int targetSide)
{
    const int side = (targetSide > 0) ? targetSide : 256;

    // 1. 优先获取官方 Windows 现代 256x256 折角文档白板衬底
    // 在 imageres.dll 中，索引 3（即资源序号 2）为 Windows 官方高分辨率折角空白文档图标
    QPixmap platePix;
    QIcon plateIcon = SysFileIconByDLL(3, side, QStringLiteral("imageres.dll"));
    if (!plateIcon.isNull()) {
        platePix = plateIcon.pixmap(side, side);
    }

    // 2. 若系统 DLL 提取失败，采用高精度纯矢量抗锯齿折角白板衬底兜底
    if (platePix.isNull() || platePix.width() < side / 2) {
        platePix = QPixmap(side, side);
        platePix.fill(Qt::transparent);
        QPainter p(&platePix);
        p.setRenderHint(QPainter::Antialiasing, true);

        const qreal pad = side * 0.12;
        const qreal fold = side * 0.20;
        const qreal w = side - pad * 2.0;
        const qreal h = side - pad * 1.8;
        const qreal left = pad;
        const qreal top = pad * 0.9;
        const qreal right = left + w;
        const qreal bottom = top + h;

        QPolygonF docPoly;
        docPoly << QPointF(left, top)
                << QPointF(right - fold, top)
                << QPointF(right, top + fold)
                << QPointF(right, bottom)
                << QPointF(left, bottom);

        // 柔和微投影
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 22));
        p.drawPolygon(docPoly.translated(0, side * 0.03));

        // 纯白现代卡片
        p.setBrush(QColor(255, 255, 255));
        p.setPen(QPen(QColor(218, 222, 230), qMax(1.0, side * 0.02)));
        p.drawPolygon(docPoly);

        // 折角阴影与内折
        QPolygonF foldPoly;
        foldPoly << QPointF(right - fold, top)
                 << QPointF(right - fold, top + fold)
                 << QPointF(right, top + fold);
        p.setBrush(QColor(238, 240, 245));
        p.drawPolygon(foldPoly);
    }

    // 3. 将内部图标按黄金比例居中合成在文档纸张上
    QPixmap composite(side, side);
    composite.fill(Qt::transparent);
    {
        QPainter p(&composite);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        // 绘制高清衬底
        p.drawPixmap(0, 0, platePix);

        if (!innerIcon.isNull()) {
            const int innerSide = qMax(16, qRound(side * 0.42));
            QPixmap innerPix = innerIcon.pixmap(innerSide, innerSide);
            if (!innerPix.isNull()) {
                const qreal centerX = side * 0.50;
                const qreal centerY = side * 0.53;
                const int ix = qRound(centerX - innerPix.width() / 2.0);
                const int iy = qRound(centerY - innerPix.height() / 2.0);
                p.drawPixmap(ix, iy, innerPix);
            }
        }
    }

    return QIcon(composite);
}

