#include "desktop/ListItem.h"
#include "app/AppFonts.h"
#include "services/DesktopItemLoader.h"
#include "services/IconCacheManager.h"
#include "desktop/ListView.h"
#include "desktop/FilenameEditor.h"
#include "desktop/RenameManager.h"
#include "box/LinBox.h"
#include "ui/SoftDesktopTextRenderer.h"
#include <QCoreApplication>
#include <QFontMetrics>
#include <QImageReader>
#include <QQueue>
#include <QRegularExpression>
#include <QTimer>
#include "services/DpiScaleHelper.h"
#include "services/SysInfoUtil.h"
#include <shellapi.h>
class ListView;

namespace {

int scaledPx(const QWidget* widget, int value)
{
    return DpiScaleHelper::scalePx(widget, value);
}

ListView* owningListView(const QWidget* widget)
{
    const QWidget* current = widget;
    while (current) {
        if (auto* listView = qobject_cast<ListView*>(const_cast<QWidget*>(current))) {
            return listView;
        }
        current = current->parentWidget();
    }
    return nullptr;
}

QString buildIconIdentityKey(const ListItemData& data,
                             ObjectType type,
                             const QFileInfo& fileInfo,
                             const QString& normalizedPath)
{
    if (type == ObjectType::SpecialFolder) {
        return QString("special:%1").arg(data.getCLSID());
    }

    const QString suffix = fileInfo.suffix().toLower();
    if (fileInfo.isDir()) {
        return QString("dir:v3:%1").arg(normalizedPath);
    }

    if (suffix == "exe" || suffix == "msi" || suffix == "msp" || suffix == "msu" ||
        suffix == "dll" || suffix == "cpl" || suffix == "scr" ||
        suffix == "lnk" || suffix == "url" || suffix == "ico" || suffix == "appref-ms") {
        return QString("path:%1").arg(normalizedPath);
    }

    if (!suffix.isEmpty()) {
        return QString("ext:%1").arg(suffix);
    }

    return QString("path:%1").arg(normalizedPath);
}

QString buildBaseIconCacheKey(const QString& identityKey)
{
    return QString("%1|base|%2").arg(identityKey).arg(ListItemData::s_targetIconSize);
}

QString buildRenderPixmapCacheKey(const QString& identityKey, int iconSide)
{
    return QString("%1|render|%2").arg(identityKey).arg(iconSide);
}

QString buildImagePreviewPixmapCacheKey(const QFileInfo& fileInfo, int iconSide)
{
    return QString("image_preview:%1|%2|%3")
        .arg(QDir::toNativeSeparators(fileInfo.absoluteFilePath()))
        .arg(fileInfo.lastModified().toMSecsSinceEpoch())
        .arg(iconSide);
}

QString buildShortcutArrowCacheKey(int overlaySize)
{
    return QString("shortcut_arrow|%1").arg(overlaySize);
}

QIcon requestSystemShellFallbackIcon(const QFileInfo& fileInfo, ObjectType type)
{
    if (type == ObjectType::SpecialFolder) {
        return SysInfoUtil::SysFileIconByDLL(4, ListItemData::s_targetIconSize, "shell32.dll");
    }

    if (fileInfo.isDir()) {
        return SysInfoUtil::SysFileIconByDLL(4, ListItemData::s_targetIconSize, "shell32.dll");
    }

    const QString nativePath = QDir::toNativeSeparators(fileInfo.absoluteFilePath());
    if (!nativePath.isEmpty()) {
        SHFILEINFOW sfi = { 0 };
        const std::wstring wPath = nativePath.toStdWString();
        if (SHGetFileInfoW(wPath.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
            SHGFI_ICON | SHGFI_LARGEICON))
        {
            if (sfi.hIcon) {
                QIcon icon = QtWin::fromHICON(sfi.hIcon);
                DestroyIcon(sfi.hIcon);
                if (!icon.isNull()) {
                    return icon;
                }
            }
        }
    }

    const QString suffix = fileInfo.suffix().toLower();
    if (!suffix.isEmpty()) {
        SHFILEINFOW sfi = { 0 };
        const std::wstring pseudoPath = (L"dummy." + suffix.toStdWString());
        if (SHGetFileInfoW(pseudoPath.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
            SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES))
        {
            if (sfi.hIcon) {
                QIcon icon = QtWin::fromHICON(sfi.hIcon);
                DestroyIcon(sfi.hIcon);
                if (!icon.isNull()) {
                    return icon;
                }
            }
        }
    }

    return SysInfoUtil::SysFileIconByDLL(0, ListItemData::s_targetIconSize, "shell32.dll");
}

QPixmap buildCroppedRenderPixmap(const QIcon& icon, int iconSide)
{
    if (icon.isNull() || iconSide <= 0) {
        return QPixmap();
    }

    const int requestSide = qMax(iconSide, 256);
    QPixmap pix = icon.pixmap(requestSide, requestSide);
    if (pix.isNull()) {
        pix = icon.pixmap(iconSide, iconSide);
    }
    if (pix.isNull()) {
        return QPixmap();
    }

    bool wasCropped = false;
    const QImage croppedImage = SysInfoUtil::autoCropImage(
        pix.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied),
        &wasCropped);
    if (wasCropped && !croppedImage.isNull()) {
        pix = QPixmap::fromImage(croppedImage);
    }

    if (pix.width() != iconSide || pix.height() != iconSide) {
        pix = pix.scaled(iconSide, iconSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return pix;
}

bool isNearWhiteOpaque(QRgb pixel)
{
    return qAlpha(pixel) >= 245 &&
        qRed(pixel) >= 235 &&
        qGreen(pixel) >= 235 &&
        qBlue(pixel) >= 235;
}

bool isCloseToMatteColor(QRgb pixel, const QColor& matteColor, int tolerance)
{
    if (qAlpha(pixel) < 245) {
        return false;
    }

    return qAbs(qRed(pixel) - matteColor.red()) <= tolerance &&
        qAbs(qGreen(pixel) - matteColor.green()) <= tolerance &&
        qAbs(qBlue(pixel) - matteColor.blue()) <= tolerance;
}

QPixmap sanitizeFolderPreviewMatte(const QPixmap& source)
{
    if (source.isNull()) {
        return source;
    }

    QImage image = source.toImage().convertToFormat(QImage::Format_ARGB32);
    if (image.isNull() || image.width() < 8 || image.height() < 8) {
        return source;
    }

    const QRgb topLeft = image.pixel(0, 0);
    const QRgb topRight = image.pixel(image.width() - 1, 0);
    const QRgb bottomLeft = image.pixel(0, image.height() - 1);
    const QRgb bottomRight = image.pixel(image.width() - 1, image.height() - 1);
    if (!isNearWhiteOpaque(topLeft) ||
        !isNearWhiteOpaque(topRight) ||
        !isNearWhiteOpaque(bottomLeft) ||
        !isNearWhiteOpaque(bottomRight)) {
        return source;
    }

    const QColor matteColor = QColor::fromRgb(topLeft);
    const int tolerance = 16;
    QImage result = image;
    QVector<uchar> visited(image.width() * image.height(), 0);
    QQueue<QPoint> queue;

    auto enqueue = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= image.width() || y >= image.height()) {
            return;
        }
        const int index = y * image.width() + x;
        if (visited[index]) {
            return;
        }
        if (!isCloseToMatteColor(result.pixel(x, y), matteColor, tolerance)) {
            return;
        }
        visited[index] = 1;
        queue.enqueue(QPoint(x, y));
    };

    for (int x = 0; x < image.width(); ++x) {
        enqueue(x, 0);
        enqueue(x, image.height() - 1);
    }
    for (int y = 0; y < image.height(); ++y) {
        enqueue(0, y);
        enqueue(image.width() - 1, y);
    }

    int clearedPixels = 0;
    while (!queue.isEmpty()) {
        const QPoint point = queue.dequeue();
        result.setPixel(point.x(), point.y(), qRgba(qRed(result.pixel(point.x(), point.y())),
                                                    qGreen(result.pixel(point.x(), point.y())),
                                                    qBlue(result.pixel(point.x(), point.y())),
                                                    0));
        ++clearedPixels;

        enqueue(point.x() - 1, point.y());
        enqueue(point.x() + 1, point.y());
        enqueue(point.x(), point.y() - 1);
        enqueue(point.x(), point.y() + 1);
    }

    if (clearedPixels == 0) {
        return source;
    }

    auto isClearedMattePixel = [&](int x, int y) -> bool {
        if (x < 0 || y < 0 || x >= image.width() || y >= image.height()) {
            return false;
        }
        return visited[y * image.width() + x] != 0;
    };

    auto decontaminateChannel = [](int channel, int matteChannel, int alpha) -> int {
        if (alpha <= 0) {
            return 0;
        }

        const double a = alpha / 255.0;
        const double corrected = (channel - (1.0 - a) * matteChannel) / a;
        return qBound(0, qRound(corrected), 255);
    };

    for (int y = 0; y < result.height(); ++y) {
        for (int x = 0; x < result.width(); ++x) {
            if (isClearedMattePixel(x, y)) {
                continue;
            }

            const QRgb pixel = result.pixel(x, y);
            const int alpha = qAlpha(pixel);
            if (alpha <= 0 || alpha >= 255) {
                continue;
            }

            bool touchesClearedMatte = false;
            for (int dy = -1; dy <= 1 && !touchesClearedMatte; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    if (isClearedMattePixel(x + dx, y + dy)) {
                        touchesClearedMatte = true;
                        break;
                    }
                }
            }

            if (!touchesClearedMatte) {
                continue;
            }

            const int correctedR = decontaminateChannel(qRed(pixel), matteColor.red(), alpha);
            const int correctedG = decontaminateChannel(qGreen(pixel), matteColor.green(), alpha);
            const int correctedB = decontaminateChannel(qBlue(pixel), matteColor.blue(), alpha);
            result.setPixel(x, y, qRgba(correctedR, correctedG, correctedB, alpha));
        }
    }

    return QPixmap::fromImage(result);
}

bool isShortcutSuffix(const QFileInfo& fileInfo)
{
    const QString suffix = fileInfo.suffix().toLower();
    return suffix == "lnk" || suffix == "url" || suffix == "appref-ms";
}

bool canUseImagePreview(const QFileInfo& fileInfo, ObjectType type)
{
    return type != ObjectType::SpecialFolder &&
        fileInfo.isFile() &&
        !isShortcutSuffix(fileInfo);
}

QPixmap buildImagePreviewPixmap(const QFileInfo& fileInfo, ObjectType type, int iconSide)
{
    if (!canUseImagePreview(fileInfo, type) || iconSide <= 0) {
        return QPixmap();
    }

    QImageReader reader(fileInfo.absoluteFilePath());
    reader.setAutoTransform(true);
    if (!reader.canRead()) {
        return QPixmap();
    }

    QImage image = reader.read();
    if (image.isNull()) {
        return QPixmap();
    }

    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QSize targetSize(iconSide, iconSide);
    const QImage scaled = image.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (scaled.isNull()) {
        return QPixmap();
    }

    QPixmap preview(targetSize);
    preview.fill(Qt::transparent);

    QPainter painter(&preview);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QPoint drawPos((targetSize.width() - scaled.width()) / 2,
                         (targetSize.height() - scaled.height()) / 2);
    painter.drawImage(drawPos, scaled);
    return preview;
}

QString buildVisibleDisplayName(const ListItemData& data,
                                const QFileInfo& fileInfo,
                                ObjectType type,
                                const QString& filePath)
{
    if (type == ObjectType::SpecialFolder) {
        return data.fullName();
    }

    if (type == ObjectType::DiskLetter) {
        const QString fullName = data.fullName().trimmed();
        return fullName.isEmpty() ? QDir::toNativeSeparators(filePath) : fullName;
    }

    if (isShortcutSuffix(fileInfo)) {
        const QString shortcutName = fileInfo.completeBaseName();
        return shortcutName.isEmpty() ? data.fullName() : shortcutName;
    }

    if (fileInfo.isFile()) {
        const QString fileName = fileInfo.fileName();
        return fileName.isEmpty() ? data.fullName() : fileName;
    }

    if (fileInfo.isDir()) {
        const QString dirName = fileInfo.fileName();
        return dirName.isEmpty() ? data.fullName() : dirName;
    }

    return data.fullName();
}

int fixedIconSideForMode(const QWidget* widget, ListItem::LayoutMode layoutMode, int itemWidth)
{
    if (layoutMode == ListItem::LayoutMode::ListMode) {
        return DpiScaleHelper::scalePx(widget, 20);
    }

    const int largeThreshold = DpiScaleHelper::scalePx(widget, ListView::kLargeIconW);
    const int smallThreshold = DpiScaleHelper::scalePx(widget, ListView::kSmallIconW);
    if (itemWidth >= largeThreshold) {
        return DpiScaleHelper::scalePx(widget, 96);
    }
    if (itemWidth <= smallThreshold) {
        return DpiScaleHelper::scalePx(widget, 32);
    }
    return DpiScaleHelper::scalePx(widget, 48);
}

QSize baseItemSizeForMode(const QWidget* widget, ListItem::LayoutMode layoutMode)
{
    if (layoutMode == ListItem::LayoutMode::ListMode) {
        return QSize(0, DpiScaleHelper::scalePx(widget, 30));
    }

    ListView* listView = owningListView(widget);
    if (!listView) {
        return QSize(DpiScaleHelper::scalePx(widget, 76), DpiScaleHelper::scalePx(widget, 78));
    }

    switch (listView->viewMode()) {
    case ListView::ViewMode::LargeIcon:
        return QSize(DpiScaleHelper::scalePx(widget, ListView::kLargeIconW), DpiScaleHelper::scalePx(widget, ListView::kLargeIconH));
    case ListView::ViewMode::SmallIcon:
        return QSize(DpiScaleHelper::scalePx(widget, ListView::kSmallIconW), DpiScaleHelper::scalePx(widget, ListView::kSmallIconH));
    case ListView::ViewMode::MediumIcon:
    default:
        return QSize(DpiScaleHelper::scalePx(widget, ListView::kMediumIconW), DpiScaleHelper::scalePx(widget, ListView::kMediumIconH));
    }
}

bool isWrapSeparator(QChar ch)
{
    return ch.isSpace() || ch == '-' || ch == '_' || ch == '.' || ch == '(' || ch == ')' || ch == '[' || ch == ']';
}

QStringList buildDesktopLines(const QString& text, const QFontMetrics& metrics, int width, int maxLines)
{
    QString remaining = text;
    remaining.replace('\n', ' ');

    QStringList lines;
    if (remaining.isEmpty() || width <= 0 || maxLines <= 0) {
        lines << remaining;
        return lines;
    }

    while (!remaining.isEmpty() && lines.size() < maxLines) {
        if (metrics.horizontalAdvance(remaining) <= width) {
            lines << remaining;
            break;
        }

        if (lines.size() == maxLines - 1) {
            lines << metrics.elidedText(remaining, Qt::ElideRight, width);
            break;
        }

        int bestFit = 0;
        int preferredBreak = -1;
        for (int i = 1; i <= remaining.size(); ++i) {
            const QString candidate = remaining.left(i);
            if (metrics.horizontalAdvance(candidate) > width) {
                break;
            }

            bestFit = i;
            if (isWrapSeparator(remaining.at(i - 1))) {
                preferredBreak = i;
            }
        }

        int splitIndex = preferredBreak;
        // Prefer a natural separator only when it is already close to the
        // maximum fit width; otherwise keep filling the current line.
        if (splitIndex <= 0 || bestFit - splitIndex > 3) {
            splitIndex = bestFit;
        }
        if (splitIndex <= 0) {
            splitIndex = 1;
        }

        QString currentLine = remaining.left(splitIndex).trimmed();
        if (currentLine.isEmpty()) {
            currentLine = remaining.left(splitIndex);
        }
        lines << currentLine;
        remaining = remaining.mid(splitIndex).trimmed();
    }

    if (lines.isEmpty()) {
        lines << metrics.elidedText(text, Qt::ElideRight, width);
    }

    return lines;
}

int computeDesktopTextHeight(const QString& text, const QFontMetrics& metrics, int width, int maxLines)
{
    const QStringList lines = buildDesktopLines(text, metrics, width, maxLines);
    return qMax(1, lines.size()) * metrics.lineSpacing();
}

}

ListItem::ListItem(QWidget* parent)
	: QWidget(parent)
{
    m_baseItemSize = baseItemSizeForMode(this, LayoutMode::IconMode);
	// Child widgets should not have separate window flags
	// setWindowFlags(Qt::FramelessWindowHint | Qt::WindowSystemMenuHint);
	setAttribute(Qt::WA_TranslucentBackground);
	setStyleSheet("background: transparent;");

	// Keep the item transparent so only icon and text are painted.
	//setAttribute(Qt::WA_TransparentForMouseEvents);
	//setAttribute(Qt::WA_OpaquePaintEvent);
	//setStyleSheet("background-color:rgb(25,255,255);");
	//setAttribute(Qt::WA_TransparentForMouseEvents);


	// Add a subtle shadow so white desktop text stays readable.
	QGraphicsDropShadowEffect* shadowEffect = new QGraphicsDropShadowEffect(this);
	
	shadowEffect->setBlurRadius(DpiScaleHelper::scalePx(this, 5));
	shadowEffect->setColor(QColor(0, 0, 0, 200));
	shadowEffect->setOffset(-DpiScaleHelper::scalePx(this, 1), DpiScaleHelper::scalePx(this, 1));



	textLabel = new QLabel(this);
	textLabel->setGraphicsEffect(shadowEffect);


	// Match the desktop label font.
	QFont yaheiFont = AppFonts::uiFont(9);
	textLabel->setFont(yaheiFont);
	textLabel->setTextFormat(Qt::PlainText);
	textLabel->setWordWrap(false);

	
	textLabel->setStyleSheet("color: white; background: transparent;");
}

ListItem::ListItem(const QString& path, QWidget* parent)
	: QWidget(parent)
	, m_baseItemSize(baseItemSizeForMode(parent, LayoutMode::IconMode))
	, m_itemSize(baseItemSizeForMode(parent, LayoutMode::IconMode))
	, m_itemPos(0, 0)
	, m_iconWidget(nullptr)
	, textLabel(nullptr)
{
	this->setItemSize(m_baseItemSize.width(), m_baseItemSize.height());
	
	setParent(parent);
	this->setObjectName(QUuid::createUuid().toString(QUuid::WithoutBraces));
	

	// Child widgets should not have separate window flags
	// setWindowFlags(Qt::FramelessWindowHint | Qt::WindowSystemMenuHint);
	setAttribute(Qt::WA_TranslucentBackground);
	setStyleSheet("background: transparent;");

	// Keep the item transparent so only icon and text are painted.
	//setAttribute(Qt::WA_TransparentForMouseEvents);
	//setAttribute(Qt::WA_OpaquePaintEvent);
	//setStyleSheet("background-color:rgb(25,255,255);");
	//setAttribute(Qt::WA_TransparentForMouseEvents);

	// Add a subtle shadow so white desktop text stays readable.
	QGraphicsDropShadowEffect* shadowEffect = new QGraphicsDropShadowEffect(this);

	shadowEffect->setBlurRadius(DpiScaleHelper::scalePx(this, 5));
	shadowEffect->setColor(QColor(0, 0, 0, 200));
	shadowEffect->setOffset(-DpiScaleHelper::scalePx(this, 1), DpiScaleHelper::scalePx(this, 1));



	textLabel = new QLabel(this);
	textLabel->setGraphicsEffect(shadowEffect);


	// Match the desktop label font.
	QFont yaheiFont = AppFonts::uiFont(9);
	textLabel->setFont(yaheiFont);
	textLabel->setTextFormat(Qt::PlainText);
	textLabel->setWordWrap(false);

	textLabel->setStyleSheet("color: white; background: transparent;");
	QString tmp = path;
	tmp = tmp.replace('/', '\\');
	const ListItemData data = DesktopItemLoader().loadItem(tmp);
	setItemData(data);
	initData();
	updateLocationInfo(QPoint(0, 0));
	show();

}

ListItem::~ListItem()
{
	this->isReady = false;
    // Qt destroys child widgets automatically.
}


void ListItem::setLayoutMode(LayoutMode mode) {
	if (m_layoutMode == mode) return;
	m_layoutMode = mode;
	initData();
	update();
}

void ListItem::initData()
{
	m_type = m_itemData.type();

	if (m_type != ObjectType::Empty_ObjectType)
	{
		if (m_type == ObjectType::SpecialFolder) {
			m_filePath = QString::fromStdString(m_itemData.itemData().path);
		} else {
			m_filePath = m_itemData.filePath();
		}
		m_fileInfo = QFileInfo(m_filePath);
		m_baseName = buildVisibleDisplayName(m_itemData, m_fileInfo, m_type, m_filePath);
		m_icon = m_itemData.icon();
		
		if (m_type == ObjectType::SpecialFolder) {
			m_normalizedPath = QString::fromStdString(m_itemData.itemData().path);
		} else {
			m_normalizedPath = QDir::toNativeSeparators(m_filePath);
		}
		while (m_normalizedPath.endsWith('\\')) m_normalizedPath.chop(1);

        const QString iconIdentityKey = buildIconIdentityKey(m_itemData, m_type, m_fileInfo, m_normalizedPath);
        const QString baseIconCacheKey = buildBaseIconCacheKey(iconIdentityKey);
        m_icon = IconCacheManager::instance().getIcon(baseIconCacheKey, [this]() {
            m_itemData.initIcon();
            return m_itemData.icon();
        });
        if (m_icon.isNull()) {
            m_icon = requestSystemShellFallbackIcon(m_fileInfo, m_type);
        }
        // Ultimate fallback: blank file icon so no item is ever invisible
        if (m_icon.isNull()) {
            m_icon = SysInfoUtil::SysFileIconByDLL(0, ListItemData::s_targetIconSize, "shell32.dll");
        }

		if (!m_icon.isNull()) {
			if (!m_iconWidget) {
				m_iconWidget = new QLabel(this);
				m_iconWidget->setAttribute(Qt::WA_TranslucentBackground);
				m_iconWidget->setAttribute(Qt::WA_TransparentForMouseEvents);
			}
			
			int iconSide;
			int topMargin;
			int leftMargin;
			
			if (m_layoutMode == LayoutMode::IconMode) {
				iconSide = qBound(DpiScaleHelper::scalePx(this, 16), fixedIconSideForMode(this, m_layoutMode, m_itemSize.width()), DpiScaleHelper::scalePx(this, 128));
				iconSide = qMin(iconSide, m_itemSize.width() - DpiScaleHelper::scalePx(this, 4));
				topMargin = DpiScaleHelper::scalePx(this, 4);
				leftMargin = (m_itemSize.width() - iconSide) / 2;
			} else {
				// List Mode: standard list icon size (20px in 30px row)
				iconSide = fixedIconSideForMode(this, m_layoutMode, m_itemSize.width());
				topMargin = (m_itemSize.height() - iconSide) / 2;
				leftMargin = DpiScaleHelper::scalePx(this, 8);
			}

            QPixmap rawPix;
            if (canUseImagePreview(m_fileInfo, m_type)) {
                const QString imagePreviewCacheKey = buildImagePreviewPixmapCacheKey(m_fileInfo, iconSide);
                rawPix = IconCacheManager::instance().getPixmap(imagePreviewCacheKey, [this, iconSide]() {
                    return buildImagePreviewPixmap(m_fileInfo, m_type, iconSide);
                });
            }

            if (rawPix.isNull()) {
                const QString renderPixmapCacheKey = buildRenderPixmapCacheKey(iconIdentityKey, iconSide);
                rawPix = IconCacheManager::instance().getPixmap(renderPixmapCacheKey, [this, iconSide]() {
                    return buildCroppedRenderPixmap(m_icon, iconSide);
                });
            }
            if (rawPix.isNull()) {
                QIcon fallbackIcon = requestSystemShellFallbackIcon(m_fileInfo, m_type);
                rawPix = buildCroppedRenderPixmap(fallbackIcon, iconSide);
            }
            if (m_type != ObjectType::SpecialFolder && m_fileInfo.isDir() && !rawPix.isNull()) {
                rawPix = sanitizeFolderPreviewMatte(rawPix);
            }
            if (m_type == ObjectType::DiskLetter && !rawPix.isNull()) {
                QPixmap fadedPix(rawPix.size());
                fadedPix.fill(Qt::transparent);
                QPainter fadePainter(&fadedPix);
                fadePainter.setRenderHint(QPainter::SmoothPixmapTransform);
                fadePainter.setOpacity(0.8);
                fadePainter.drawPixmap(0, 0, rawPix);
                fadePainter.end();
                rawPix = fadedPix;
            }
            if (m_hiddenFileVisual && !rawPix.isNull()) {
                QPixmap fadedPix(rawPix.size());
                fadedPix.fill(Qt::transparent);
                QPainter fadePainter(&fadedPix);
                fadePainter.setRenderHint(QPainter::SmoothPixmapTransform);
                fadePainter.setOpacity(0.45);
                fadePainter.drawPixmap(0, 0, rawPix);
                fadePainter.end();
                rawPix = fadedPix;
            }

			m_iconWidget->setPixmap(rawPix);
			m_iconWidget->setAlignment(Qt::AlignCenter); 
			m_iconWidget->resize(iconSide, iconSide);
			m_iconWidget->move(leftMargin, topMargin);
			m_iconWidget->show();

			// --- Shortcut Arrow Overlay ---
			bool isShortcut = m_fileInfo.suffix().toLower() == "lnk" || m_fileInfo.suffix().toLower() == "url";
			ListView* listView = owningListView(this);
			bool showArrowSetting = listView ? listView->shortcutArrowVisible() : true;

			if (isShortcut && showArrowSetting) {
				if (!m_shortcutArrowOverlay) {
					m_shortcutArrowOverlay = new QLabel(this);
					m_shortcutArrowOverlay->setAttribute(Qt::WA_TranslucentBackground);
					m_shortcutArrowOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
					m_shortcutArrowOverlay->setScaledContents(true);
				}
				
				// Extract the native Windows shortcut arrow
				QIcon arrowIcon = SysInfoUtil::SysFileIconByDLL(155, 32, "imageres.dll");
				if (arrowIcon.isNull()) {
					arrowIcon = SysInfoUtil::SysFileIconByDLL(30, 32, "shell32.dll");
				}

				if (!arrowIcon.isNull()) {
					int overlaySize;
					if (m_layoutMode == LayoutMode::ListMode) {
						overlaySize = ListView::kListArrowSize;
					} else {
						// Map of ViewMode -> Arrow Size
						ListView::ViewMode currentMode = listView ? listView->viewMode() : ListView::ViewMode::MediumIcon;
						switch (currentMode) {
							case ListView::ViewMode::LargeIcon:  overlaySize = ListView::kLargeArrowSize; break;
							case ListView::ViewMode::MediumIcon: overlaySize = ListView::kMediumArrowSize; break;
							case ListView::ViewMode::SmallIcon:  overlaySize = ListView::kSmallArrowSize; break;
							default: overlaySize = ListView::kMediumArrowSize; break;
						}
					}

					QPixmap arrowPix = IconCacheManager::instance().getPixmap(buildShortcutArrowCacheKey(overlaySize), [overlaySize]() {
                        QIcon cachedArrowIcon = SysInfoUtil::SysFileIconByDLL(155, 32, "imageres.dll");
                        if (cachedArrowIcon.isNull()) {
                            cachedArrowIcon = SysInfoUtil::SysFileIconByDLL(30, 32, "shell32.dll");
                        }
                        return cachedArrowIcon.pixmap(overlaySize, overlaySize);
                    });
					m_shortcutArrowOverlay->setPixmap(arrowPix);
					m_shortcutArrowOverlay->setFixedSize(overlaySize, overlaySize);
					
					// Position: bottom-left of the icon
					m_shortcutArrowOverlay->move(leftMargin, topMargin + iconSide - overlaySize);
					m_shortcutArrowOverlay->show();
				} else {
					if (m_shortcutArrowOverlay) m_shortcutArrowOverlay->hide();
				}
			} else {
				if (m_shortcutArrowOverlay) m_shortcutArrowOverlay->hide();
			}
		} else {
			if (m_iconWidget) m_iconWidget->hide();
			if (m_shortcutArrowOverlay) m_shortcutArrowOverlay->hide();
		}
		
		if (textLabel == nullptr) {
			textLabel = new QLabel(this);
			textLabel->setTextFormat(Qt::PlainText);
			textLabel->setWordWrap(false);
			textLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
			textLabel->show();
		}

        if (m_layoutMode == LayoutMode::IconMode) {
            const QSize fallbackBaseSize = baseItemSizeForMode(this, m_layoutMode);
            const int baseWidth = m_baseItemSize.width() > 0 ? m_baseItemSize.width() : fallbackBaseSize.width();
            const int textWidth = qMax(DpiScaleHelper::scalePx(this, 10), baseWidth - DpiScaleHelper::scalePx(this, 4));
            const QFontMetrics labelMetrics(textLabel->font());
            int textHeight = 0;
            if (m_expandFullNameOnSelect) {
                const QString expandedText = m_expandedPreviewText.isEmpty() ? m_baseName : m_expandedPreviewText;
                textHeight = computeDesktopTextHeight(expandedText, labelMetrics, textWidth, 64);
            } else {
                textHeight = computeDesktopTextHeight(m_baseName, labelMetrics, textWidth, 2);
            }
            m_visibleTextHeight = textHeight;
        } else if (textLabel) {
            m_visibleTextHeight = QFontMetrics(textLabel->font()).lineSpacing();
        }

        applyEffectiveGeometry();
        refreshTextLabelPresentation();

        if (isSelected || m_expandFullNameOnSelect) {
            raise();
            if (m_iconWidget) {
                m_iconWidget->raise();
            }
            if (m_shortcutArrowOverlay) {
                m_shortcutArrowOverlay->raise();
            }
            if (textLabel) {
                textLabel->raise();
            }
        }

		isReady = true;
	}
}

void ListItem::setItemData(ListItemData data)
{
	m_itemData = data;
	m_type = m_itemData.type();
	if (m_type == ObjectType::SpecialFolder) {
        m_filePath = data.filePath();
		m_normalizedPath = m_filePath;
	}else {
		m_filePath = data.filePath();
		m_normalizedPath = QDir::toNativeSeparators(m_filePath);
	}
	while (m_normalizedPath.endsWith('\\')) m_normalizedPath.chop(1);
	// Reset icon cache so initData will recreate m_iconWidget
	m_itemData.m_iconSizeCached = -1;
	//update();
}

void ListItem::setItemSize(int width, int height)
{
	m_baseItemSize = QSize(width, height);
	m_itemSize = m_baseItemSize;
	this->resize(width, height);
	initData(); // Recompute icon size and position when item size changes
}

void ListItem::setItemPos(int startX, int startY)
{
	m_itemPos = QPoint(startX, startY);
}

void ListItem::setItemPos(QPoint pos)
{
	m_itemPos = pos;
}

void ListItem::updateLocationInfo(QPoint pos)
{
	this->setItemPos(pos);
	this->move(pos);
	m_itemRect = QRect(pos, m_itemSize);

    const QRect chooseRectLocal = selectionContentRect();
	m_chooseRect = QRect(pos + chooseRectLocal.topLeft(), chooseRectLocal.size());
}

// Persistent render state is tracked with m_itemPos and m_boxId.

void ListItem::checkSelf()
{
	isSelected = true;
	update();
}

void ListItem::unCheckSelf()
{
	isSelected = false;
    m_expandFullNameOnSelect = false;
    m_expandedPreviewText.clear();
    initData();
    requestOwningBoxLayoutRefresh();
	if (this->isReady == true) {
		update();
	}
}

void ListItem::setFocused(bool focused)
{
    if (isFocused == focused) {
        return;
    }
    isFocused = focused;
    update();
}

void ListItem::setCutMarked(bool cutMarked)
{
    if (property("cutMarked").toBool() == cutMarked) {
        return;
    }
    setProperty("cutMarked", cutMarked);
    update();
}

void ListItem::setHiddenFileVisual(bool hiddenFileVisual)
{
    if (m_hiddenFileVisual == hiddenFileVisual) {
        return;
    }

    m_hiddenFileVisual = hiddenFileVisual;
    if (isReady) {
        initData();
        update();
    }
}

void ListItem::openItem()
{
	if (m_filePath.isEmpty()) return;

	// Let Windows Shell handle activation so invalid executables and broken
	// shortcuts surface the same native error UI Explorer would show.
	const QString nativePath = QDir::toNativeSeparators(m_filePath);
	CSystemApi::File_RightMenuOperations(reinterpret_cast<const TCHAR*>(nativePath.utf16()), Z_OPEN);
}

ListItemData ListItem::getItemData() const
{
	return m_itemData;
}

void ListItem::refreshIcon()
{
    const QString identityKey = buildIconIdentityKey(m_itemData, m_type, m_fileInfo, m_normalizedPath);
    IconCacheManager::instance().invalidateIcon(buildBaseIconCacheKey(identityKey));
    IconCacheManager::instance().invalidatePixmapsByPrefix(identityKey + "|render|");

    // Force icon refresh by resetting cached size and re-initializing
    m_itemData.m_iconSizeCached = -1;
    m_itemData.initIcon();
    m_icon = m_itemData.icon();

    // Fallback: system shell icon
    if (m_icon.isNull()) {
        m_icon = requestSystemShellFallbackIcon(m_fileInfo, m_type);
    }
    // Ultimate fallback: blank file icon
    if (m_icon.isNull()) {
        m_icon = SysInfoUtil::SysFileIconByDLL(0, ListItemData::s_targetIconSize, "shell32.dll");
    }

    initData(); // Re-create icon widget with new icon
    update();   // Trigger repaint
}

void ListItem::setFullNameExpanded(bool expanded)
{
    if (m_expandFullNameOnSelect == expanded) {
        return;
    }
    m_expandFullNameOnSelect = expanded;
    if (!expanded) {
        m_expandedPreviewText.clear();
    }
    initData();
    requestOwningBoxLayoutRefresh();
    update();
}

void ListItem::setExpandedPreviewText(const QString& text)
{
    if (m_expandedPreviewText == text) {
        return;
    }
    m_expandedPreviewText = text;
    initData();
    requestOwningBoxLayoutRefresh();
    update();
}

void ListItem::refreshTextLabelPresentation()
{
    if (!textLabel) {
        return;
    }

    if (RenameManager::instance()->activeItem() == this) {
        textLabel->hide();
        return;
    }
    const QRect labelRect = textRect();
    textLabel->setGeometry(labelRect);
    textLabel->hide();
}

int ListItem::visibleTextHeight() const
{
    return m_visibleTextHeight;
}

QRect ListItem::selectionContentRect() const
{
    if (m_layoutMode != LayoutMode::IconMode) {
        const int inset = DpiScaleHelper::scalePx(this, 2);
        return rect().adjusted(inset, inset, -inset, -inset);
    }

    const QRect iconArea = iconRect();
    const QRect textArea = textRect();
    const int textHeight = qMax(1, m_visibleTextHeight);
    const QRect visibleTextRect(
        textArea.left(),
        textArea.top(),
        textArea.width(),
        qMin(textHeight, textArea.height()));
    return iconArea.united(visibleTextRect);
}

QRect ListItem::iconRect() const
{
    if (m_layoutMode == LayoutMode::ListMode) {
        const int iconSide = fixedIconSideForMode(this, m_layoutMode, width());
        const int topMargin = (height() - iconSide) / 2;
        return QRect(DpiScaleHelper::scalePx(this, 8), topMargin, iconSide, iconSide);
    }

    const int iconSide = qMin(qBound(DpiScaleHelper::scalePx(this, 16), fixedIconSideForMode(this, m_layoutMode, width()), DpiScaleHelper::scalePx(this, 128)), width() - DpiScaleHelper::scalePx(this, 4));
    const int leftMargin = (width() - iconSide) / 2;
    return QRect(leftMargin, DpiScaleHelper::scalePx(this, 1), iconSide, iconSide);
}

QRect ListItem::textRect() const
{
    if (m_layoutMode == LayoutMode::ListMode) {
        return QRect(DpiScaleHelper::scalePx(this, 36), 0, qMax(DpiScaleHelper::scalePx(this, 10), width() - DpiScaleHelper::scalePx(this, 40)), height());
    }

    const QRect iconArea = iconRect();
    const int top = iconArea.bottom() + DpiScaleHelper::scalePx(this, ListItem::kIconTextGap);
    return QRect(DpiScaleHelper::scalePx(this, 2), top, qMax(DpiScaleHelper::scalePx(this, 10), width() - DpiScaleHelper::scalePx(this, 4)), qMax(DpiScaleHelper::scalePx(this, 18), height() - top));
}

QRect ListItem::renameEditorRect() const
{
    QRect rect = textRect();
    rect.setHeight(qMax(DpiScaleHelper::scalePx(this, 22), rect.height()));
    return rect;
}

int ListItem::desiredIconModeHeight() const
{
    if (m_layoutMode != LayoutMode::IconMode) {
        return m_baseItemSize.height();
    }

    const int iconSide = qMin(qBound(DpiScaleHelper::scalePx(this, 16), fixedIconSideForMode(this, m_layoutMode, m_baseItemSize.width()), DpiScaleHelper::scalePx(this, 128)), m_baseItemSize.width() - DpiScaleHelper::scalePx(this, 4));
    const int textTop = DpiScaleHelper::scalePx(this, 1) + iconSide + DpiScaleHelper::scalePx(this, ListItem::kIconTextGap);
    const int lineHeight = textLabel ? QFontMetrics(textLabel->font()).lineSpacing() : fontMetrics().lineSpacing();
    const int textHeight = qMax(m_visibleTextHeight, lineHeight);
    return textTop + textHeight + DpiScaleHelper::scalePx(this, 2);
}

void ListItem::applyEffectiveGeometry()
{
    QSize effectiveSize = m_baseItemSize;
    if (m_layoutMode == LayoutMode::IconMode) {
        effectiveSize.setHeight(desiredIconModeHeight());
    }

    m_itemSize = effectiveSize;
    if (QWidget::size() != effectiveSize) {
        resize(effectiveSize);
    }

    if (parentWidget()) {
        updateLocationInfo(m_itemPos);
    }
}

void ListItem::requestOwningBoxLayoutRefresh()
{
    QWidget* ancestor = parentWidget();
    while (ancestor) {
        if (LinBox* box = qobject_cast<LinBox*>(ancestor)) {
            box->refreshLayout();
            break;
        }
        ancestor = ancestor->parentWidget();
    }
}

void ListItem::mousePressEvent(QMouseEvent* event)
{
    event->accept();
    emit itemClicked(this, event);
}

void ListItem::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();
    emit itemMoved(this, event);
}

void ListItem::mouseReleaseEvent(QMouseEvent* event)
{
    event->accept();
    emit itemReleased(this, event);
}

void ListItem::mouseDoubleClickEvent(QMouseEvent* event)
{
    event->accept();
    emit itemDoubleClicked(this, event);
}

void ListItem::contextMenuEvent(QContextMenuEvent* event) {
    event->accept();
    emit itemContextMenuRequested(this, event->globalPos());
}

void ListItem::enterEvent(QEvent* event) {
    isHovered = true;
    update();
    QWidget::enterEvent(event);
}

void ListItem::leaveEvent(QEvent* event) {
    isHovered = false;
    update();
    QWidget::leaveEvent(event);
}

void ListItem::paintEvent(QPaintEvent* event) {
    // Skip painting until the item has valid data.
    if (!this->isVisible() || !this->isReady) {
        return;
    }
	//if (!event->region().intersects(rect())) {
	//    return;
	//}
	//qDebug() << "ListItem painted";
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRect rect = this->rect();
	QPoint pos = this->pos();
	QSize size = this->size();
    const QRect contentBounds = selectionContentRect();
    const int outerInset = DpiScaleHelper::scalePx(this, 3);
    const int innerInset = DpiScaleHelper::scalePx(this, 1);
    const QRect outerHighlightRect = contentBounds.adjusted(-outerInset, -outerInset, outerInset, outerInset);
    const QRect innerHighlightRect = contentBounds.adjusted(innerInset, innerInset, -innerInset, -innerInset);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 80, 80, 32));
    //painter.drawRoundedRect(rect.adjusted(0, 0, -1, -1), 4, 4);
	// Prepare label painting.
	QFont labelFont = textLabel ? textLabel->font() : font();
	painter.setFont(labelFont);
    const bool isCutMarked = property("cutMarked").toBool();
    if (isCutMarked) {
        painter.setOpacity(0.55);
    }
	
	

	// Paint selection and hover around the real icon and text bounds.
	if (isSelected) {
		painter.setPen(Qt::NoPen);
		painter.setBrush(QColor(100, 150, 255, 30));
		painter.drawRoundedRect(outerHighlightRect, DpiScaleHelper::scalePx(this, 8), DpiScaleHelper::scalePx(this, 8));

		/*painter.setBrush(QColor(80, 140, 255, 60));
		painter.drawRoundedRect(-1, -1, width + 2, height + 2, 6, 6);*/

		painter.setBrush(QColor(255, 255, 255, 90));
		painter.drawRoundedRect(innerHighlightRect, DpiScaleHelper::scalePx(this, 2), DpiScaleHelper::scalePx(this, 2));

		painter.setPen(QPen(QColor(255, 255, 255, 150), qMax<qreal>(0.7, DpiScaleHelper::uiScaleForWidget(this) * 0.7)));
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(innerHighlightRect, DpiScaleHelper::scalePx(this, 2), DpiScaleHelper::scalePx(this, 2));
	}
	if (isHovered) {
		painter.setPen(Qt::NoPen);
		painter.setBrush(QColor(255, 255, 255, 25));
		painter.drawRoundedRect(innerHighlightRect, DpiScaleHelper::scalePx(this, 2), DpiScaleHelper::scalePx(this, 2));

		QPen dashPen(QColor(255, 255, 255, 100));
		dashPen.setWidthF(0.8);
		dashPen.setStyle(Qt::DotLine);
		dashPen.setDashPattern(QVector<qreal>({1.0, 1.0}));
		dashPen.setCapStyle(Qt::FlatCap);
		
		painter.setPen(dashPen);
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(innerHighlightRect, DpiScaleHelper::scalePx(this, 2), DpiScaleHelper::scalePx(this, 2));
	}

    if (isCutMarked) {
        painter.setOpacity(1.0);
    }

    if (RenameManager::instance()->activeItem() != this) {
        const QRect labelRect = textRect();
        if (!labelRect.isEmpty()) {
            const QFontMetrics fm(labelFont);
            QStringList textLines;
            SoftDesktopTextRenderer::Options options;
            options.textColor = QColor(255, 255, 255);
            options.outlineColor = QColor(0, 0, 0, 105);
            options.outlineOffset = QPoint(0, 1);
            options.innerShadowColor = QColor(0, 0, 0, 0);
            options.glowColor = QColor(0, 0, 0, 52);
            options.glowOffset = QPoint(0, 1);
            options.largeGlowRadius = 1;
            options.tightGlowRadius = 1;

            if (m_layoutMode == LayoutMode::IconMode) {
                textLines = buildDesktopLines(displayName(), fm, labelRect.width(), m_expandFullNameOnSelect ? 64 : 2);
                options.alignment = Qt::AlignHCenter | Qt::AlignTop;
                options.outlineWidth = 1;
            } else {
                textLines << fm.elidedText(displayName(), Qt::ElideRight, labelRect.width());
                options.alignment = Qt::AlignVCenter | Qt::AlignLeft;
                options.outlineWidth = 0;
            }

            SoftDesktopTextRenderer::drawText(&painter, labelRect, textLines, labelFont, options);
        }
    }


}

