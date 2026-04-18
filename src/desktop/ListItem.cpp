#include "desktop/ListItem.h"
#include "services/DesktopItemLoader.h"
#include "services/IconCacheManager.h"
#include "desktop/ListView.h"
#include "desktop/FilenameEditor.h"
#include "desktop/RenameManager.h"
#include "ui/SoftDesktopTextRenderer.h"
#include <QFontMetrics>
#include <QRegularExpression>
#include <QTimer>
#include "services/SysInfoUtil.h"
#include <shellapi.h>
class ListView;

namespace {

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
        return QString("dir:%1").arg(normalizedPath);
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

bool isShortcutSuffix(const QFileInfo& fileInfo)
{
    const QString suffix = fileInfo.suffix().toLower();
    return suffix == "lnk" || suffix == "url" || suffix == "appref-ms";
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

int fixedIconSideForMode(ListItem::LayoutMode layoutMode)
{
    if (layoutMode == ListItem::LayoutMode::ListMode) {
        return 20;
    }

    ListView* listView = ListView::instance();
    if (!listView) {
        return 48;
    }

    switch (listView->viewMode()) {
    case ListView::ViewMode::LargeIcon:
        return 96;
    case ListView::ViewMode::SmallIcon:
        return 32;
    case ListView::ViewMode::MediumIcon:
    default:
        return 48;
    }
}

QSize baseItemSizeForMode(ListItem::LayoutMode layoutMode)
{
    if (layoutMode == ListItem::LayoutMode::ListMode) {
        return QSize(0, 30);
    }

    ListView* listView = ListView::instance();
    if (!listView) {
        return QSize(76, 78);
    }

    switch (listView->viewMode()) {
    case ListView::ViewMode::LargeIcon:
        return QSize(ListView::kLargeIconW, ListView::kLargeIconH);
    case ListView::ViewMode::SmallIcon:
        return QSize(ListView::kSmallIconW, ListView::kSmallIconH);
    case ListView::ViewMode::MediumIcon:
    default:
        return QSize(ListView::kMediumIconW, ListView::kMediumIconH);
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

}

ListItem::ListItem(QWidget* parent)
	: QWidget(parent)
{
    m_baseItemSize = QSize(76, 78);
	// Child widgets should not have separate window flags
	// setWindowFlags(Qt::FramelessWindowHint | Qt::WindowSystemMenuHint);
	setAttribute(Qt::WA_TranslucentBackground);
	setStyleSheet("background: transparent;");

	// 设置透明并允许透过窗口看到后面的内容
	//setAttribute(Qt::WA_TransparentForMouseEvents); // 允许点击透过窗口
	//setAttribute(Qt::WA_OpaquePaintEvent);  // 启用背景透明化
	//setStyleSheet("background-color:rgb(25,255,255);");
	//setAttribute(Qt::WA_TransparentForMouseEvents);


	// 创建阴影滤镜实现描边效果
	QGraphicsDropShadowEffect* shadowEffect = new QGraphicsDropShadowEffect(this);
	
	shadowEffect->setBlurRadius(5);           // 增加模糊半径，让效果更柔和
	shadowEffect->setColor(QColor(0, 0, 0, 200)); // 使用更深的黑色，增加不透明度
	shadowEffect->setOffset(-1, 1);              // 增加偏移量，让描边更明显



	textLabel = new QLabel(this);
	textLabel->setGraphicsEffect(shadowEffect);


	// 设置微软雅黑字体
	QFont yaheiFont;
	yaheiFont.setFamily("Microsoft YaHei UI");
	yaheiFont.setPointSize(9);
	yaheiFont.setWeight(QFont::Normal);
	textLabel->setFont(yaheiFont);
	textLabel->setTextFormat(Qt::PlainText);
	textLabel->setWordWrap(false);

	
	textLabel->setStyleSheet("color: white; background: transparent;");
}

ListItem::ListItem(const QString& path, QWidget* parent)
	: QWidget(parent)
	, m_baseItemSize(76, 78)
	, m_itemSize(76, 78)
	, m_itemPos(0, 0)
	, m_iconWidget(nullptr)
	, textLabel(nullptr)
{
	this->setItemSize(76, 78);
	
	setParent(parent);
	this->setObjectName(QUuid::createUuid().toString(QUuid::WithoutBraces));
	

	// Child widgets should not have separate window flags
	// setWindowFlags(Qt::FramelessWindowHint | Qt::WindowSystemMenuHint);
	setAttribute(Qt::WA_TranslucentBackground);
	setStyleSheet("background: transparent;");

	// 设置透明并允许透过窗口看到后面的内容
	//setAttribute(Qt::WA_TransparentForMouseEvents); // 允许点击透过窗口
	//setAttribute(Qt::WA_OpaquePaintEvent);  // 启用背景透明化
	//setStyleSheet("background-color:rgb(25,255,255);");
	//setAttribute(Qt::WA_TransparentForMouseEvents);

	// 创建阴影滤镜实现描边效果
	QGraphicsDropShadowEffect* shadowEffect = new QGraphicsDropShadowEffect(this);

	shadowEffect->setBlurRadius(5);           // 增加模糊半径，让效果更柔和
	shadowEffect->setColor(QColor(0, 0, 0, 200)); // 使用更深的黑色，增加不透明度
	shadowEffect->setOffset(-1, 1);              // 增加偏移量，让描边更明显



	textLabel = new QLabel(this);
	textLabel->setGraphicsEffect(shadowEffect);


	// 设置微软雅黑字体
	QFont yaheiFont;
	yaheiFont.setFamily("Microsoft YaHei UI");
	yaheiFont.setPointSize(9);
	yaheiFont.setWeight(QFont::Normal);
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
    // 子控件 (m_iconWidget, textLabel) 会被 Qt 自动删除，无需手动 delete
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
				iconSide = qBound(16, fixedIconSideForMode(m_layoutMode), 128);
				iconSide = qMin(iconSide, m_itemSize.width() - 4);
				topMargin = 2;
				leftMargin = (m_itemSize.width() - iconSide) / 2;
			} else {
				// List Mode: standard list icon size (20px in 30px row)
				iconSide = fixedIconSideForMode(m_layoutMode);
				topMargin = (m_itemSize.height() - iconSide) / 2;
				leftMargin = 8;
			}

            const QString renderPixmapCacheKey = buildRenderPixmapCacheKey(iconIdentityKey, iconSide);
			QPixmap rawPix = IconCacheManager::instance().getPixmap(renderPixmapCacheKey, [this, iconSide]() {
                return buildCroppedRenderPixmap(m_icon, iconSide);
            });
            if (rawPix.isNull()) {
                QIcon fallbackIcon = requestSystemShellFallbackIcon(m_fileInfo, m_type);
                rawPix = buildCroppedRenderPixmap(fallbackIcon, iconSide);
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

			m_iconWidget->setPixmap(rawPix);
			m_iconWidget->setAlignment(Qt::AlignCenter); 
			m_iconWidget->resize(iconSide, iconSide);
			m_iconWidget->move(leftMargin, topMargin);
			m_iconWidget->show();

			// --- Shortcut Arrow Overlay ---
			bool isShortcut = m_fileInfo.suffix().toLower() == "lnk" || m_fileInfo.suffix().toLower() == "url";
			bool showArrowSetting = ListView::instance()->shortcutArrowVisible();

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
						ListView::ViewMode currentMode = ListView::instance()->viewMode();
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
            const QSize fallbackBaseSize = baseItemSizeForMode(m_layoutMode);
            const int baseWidth = m_baseItemSize.width() > 0 ? m_baseItemSize.width() : fallbackBaseSize.width();
            const int baseHeight = m_baseItemSize.height() > 0 ? m_baseItemSize.height() : fallbackBaseSize.height();
            const int iconSide = qMin(qBound(16, fixedIconSideForMode(m_layoutMode), 128), baseWidth - 4);
            const QRect iconArea((baseWidth - iconSide) / 2, 2, iconSide, iconSide);
            const int textWidth = qMax(10, baseWidth - 4);
            const QFontMetrics labelMetrics(textLabel->font());
            int textHeight = 0;
            const bool isBoxItem = !m_boxId.isEmpty();
            const int textTopGap = isBoxItem ? 2 : 4;
            if (m_expandFullNameOnSelect) {
                const QString expandedText = m_expandedPreviewText.isEmpty() ? m_baseName : m_expandedPreviewText;
                if (isBoxItem) {
                    const QStringList lines = buildDesktopLines(expandedText, labelMetrics, textWidth, 64);
                    textHeight = qMax(1, lines.size()) * labelMetrics.lineSpacing();
                } else {
                    textHeight = FilenameEditor::heightForText(expandedText, textLabel->font(), textWidth);
                }
            } else {
                const QStringList lines = buildDesktopLines(m_baseName, labelMetrics, textWidth, 2);
                const int lineCount = qMax(1, lines.size());
                textHeight = lineCount * labelMetrics.lineSpacing();
            }
            const int desiredHeight = qMax(baseHeight, iconArea.bottom() + textTopGap + textHeight);

            if (m_itemSize.width() != baseWidth || m_itemSize.height() != desiredHeight) {
                m_itemSize = QSize(baseWidth, desiredHeight);
                resize(m_itemSize);
                updateLocationInfo(m_itemPos);
            }
        }

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
	
	// Shrink less in ListMode to allow easier clicking and dragging on thin rows
	int shrinkSize = (m_layoutMode == LayoutMode::IconMode) ? 15 : 2; 
	m_chooseRect = m_itemRect.adjusted(shrinkSize, shrinkSize, -shrinkSize, -shrinkSize);
}

// resetPersistentRenderState 和 applyPersistentRenderState 已移除
// 现在统一使用 m_itemPos 和 m_boxId，通过 updateLocationInfo 和 setBoxId 管理

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
	if (this->isReady == true) {
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
    update();
}

void ListItem::setExpandedPreviewText(const QString& text)
{
    if (m_expandedPreviewText == text) {
        return;
    }
    m_expandedPreviewText = text;
    initData();
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

QRect ListItem::iconRect() const
{
    if (m_layoutMode == LayoutMode::ListMode) {
        const int iconSide = fixedIconSideForMode(m_layoutMode);
        const int topMargin = (height() - iconSide) / 2;
        return QRect(8, topMargin, iconSide, iconSide);
    }

    const int iconSide = qMin(qBound(16, fixedIconSideForMode(m_layoutMode), 128), width() - 4);
    const int leftMargin = (width() - iconSide) / 2;
    return QRect(leftMargin, 2, iconSide, iconSide);
}

QRect ListItem::textRect() const
{
    if (m_layoutMode == LayoutMode::ListMode) {
        return QRect(36, 0, qMax(10, width() - 40), height());
    }

    const QRect iconArea = iconRect();
    const int top = iconArea.bottom() + 4;
    return QRect(2, top, qMax(10, width() - 4), qMax(18, height() - top));
}

QRect ListItem::renameEditorRect() const
{
    QRect rect = textRect();
    rect.setHeight(qMax(22, rect.height()));
    return rect;
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
    // 如果item不可见或未准备好，直接返回，不进行绘制
    if (!this->isVisible() || !this->isReady) {
        return;
    }
	//if (!event->region().intersects(rect())) {
	//    return; // 跳过无关绘制
	//}
	//qDebug() << "ListItem painted";
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);  // 启用抗锯齿
	QRect rect = this->rect();
	QPoint pos = this->pos();
	QSize size = this->size();
	// 计算文字宽度并居中

	// 准备文字绘制变量
	QFont labelFont = textLabel ? textLabel->font() : font();
	painter.setFont(labelFont);
	
	

	//给itemRect画个高亮
	if (isSelected) {
		// Windows桌面图标选中效果通常包括：
		// 1. 半透明蓝色背景
		// 2. 浅蓝色边框
		// 3. 圆角效果
		// 4. 周围高亮

		// 方法1：现代Windows效果（半透明蓝色背景 + 边框）


		// 设置填充颜色为半透明蓝色
		int width = m_itemSize.width();
		int height = m_itemSize.height();

		// 第一层：外发光（最大范围）
		painter.setPen(Qt::NoPen);
		painter.setBrush(QColor(100, 150, 255, 30));
		painter.drawRoundedRect(-3, -3, width + 6, height + 6, 8, 8);

		// 第二层：中发光
		/*painter.setBrush(QColor(80, 140, 255, 60));
		painter.drawRoundedRect(-1, -1, width + 2, height + 2, 6, 6);*/

		// 第三层：内发光（主背景）
		painter.setBrush(QColor(255, 255, 255, 90));
		painter.drawRoundedRect(1, 1, width - 2, height - 2, 2, 2);

		// 第四层：高亮边框
		painter.setPen(QPen(QColor(255, 255, 255, 150), 0.7));
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(1, 1, width - 2, height - 2, 2, 2);
	}
	else if (isHovered) {
		// 鼠标悬浮效果（参照选中，但更浅，且增加白色细虚线边框）
		int width = m_itemSize.width();
		int height = m_itemSize.height();

		// 背景：一层极浅的背景
		painter.setPen(Qt::NoPen);
		painter.setBrush(QColor(255, 255, 255, 25)); // 保持极淡背景
		painter.drawRoundedRect(1, 1, width - 2, height - 2, 2, 2);

		// 边框：特别细的白色点线
		QPen dashPen(QColor(255, 255, 255, 100)); // 略微增加不透明度以便看清虚线
		dashPen.setWidthF(0.8);                  // 特别细
		dashPen.setStyle(Qt::DotLine);          // 点线风格
		dashPen.setDashPattern(QVector<qreal>({1.0, 1.0})); // 设置点线模式
		dashPen.setCapStyle(Qt::FlatCap);        // 点线端点平整
		
		painter.setPen(dashPen);
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(1, 1, width - 2, height - 2, 2, 2);
	}

    if (RenameManager::instance()->activeItem() != this) {
        const QRect labelRect = textRect();
        if (!labelRect.isEmpty()) {
            const QFontMetrics fm(labelFont);
            QStringList textLines;
            SoftDesktopTextRenderer::Options options;
            options.textColor = QColor(255, 255, 255);
            options.glowColor = QColor(0, 0, 0, 170);
            options.innerShadowColor = QColor(0, 0, 0, 115);
            options.largeGlowRadius = 1;
            options.tightGlowRadius = 1;

            if (m_layoutMode == LayoutMode::IconMode) {
                textLines = buildDesktopLines(displayName(), fm, labelRect.width(), m_expandFullNameOnSelect ? 64 : 2);
                options.alignment = Qt::AlignHCenter | Qt::AlignTop;
            } else {
                textLines << fm.elidedText(displayName(), Qt::ElideRight, labelRect.width());
                options.alignment = Qt::AlignVCenter | Qt::AlignLeft;
            }

            SoftDesktopTextRenderer::drawText(&painter, labelRect, textLines, labelFont, options);
        }
    }


}
