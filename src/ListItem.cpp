#include "ListItem.h"
#include "ListView.h"
#include <SysInfoUtil.h>
#include <shellapi.h>
class ListView;

ListItem::ListItem(QWidget* parent)
	: QWidget(parent)
{
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
	yaheiFont.setFamily("Microsoft YaHei");
	yaheiFont.setPointSize(10);
	yaheiFont.setWeight(QFont::ExtraLight);
	//textLabel->setFont(yaheiFont);

	
	textLabel->setStyleSheet("color: white; background: transparent;");
}

ListItem::ListItem(const QString& path, QWidget* parent)
	: QWidget(parent)
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
	yaheiFont.setFamily("Microsoft YaHei");
	yaheiFont.setPointSize(10);
	yaheiFont.setWeight(QFont::ExtraLight);
	//textLabel->setFont(yaheiFont);

	textLabel->setStyleSheet("color: white; background: transparent;");
	QString tmp = path;
	tmp = tmp.replace('/', '\\');
	ListItemData* data = new ListItemData(CSystemApi::CreateIconItem(tmp.toStdWString().c_str()));
	setItemData(*data);
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
	m_itemData.initIcon(); // Refresh icons if target size changed
	m_type = m_itemData.type();

	if (m_type != ObjectType::Empty_ObjectType)
	{
		if (m_type == ObjectType::SpecialFolder) {
			m_filePath = QString::fromStdString(m_itemData.itemData().path);
		} else {
			m_filePath = m_itemData.filePath();
		}
		m_fileInfo = QFileInfo(m_filePath.replace("LNK", "lnk"));
		m_baseName = (m_type == ObjectType::SpecialFolder) ? m_itemData.fullName() : m_fileInfo.baseName();
		m_icon = m_itemData.icon();
		
		if (m_type == ObjectType::SpecialFolder) {
			m_normalizedPath = QString::fromStdString(m_itemData.itemData().path);
		} else {
			m_normalizedPath = QDir::toNativeSeparators(m_filePath);
		}
		while (m_normalizedPath.endsWith('\\')) m_normalizedPath.chop(1);

		if (!m_icon.isNull() && !m_icon.availableSizes().isEmpty()) {
			if (!m_iconWidget) {
				m_iconWidget = new QLabel(this);
				m_iconWidget->setAttribute(Qt::WA_TranslucentBackground);
				m_iconWidget->setAttribute(Qt::WA_TransparentForMouseEvents);
			}
			
			int iconSide;
			int topMargin;
			int leftMargin;
			
			if (m_layoutMode == LayoutMode::IconMode) {
				iconSide = qMin(m_itemSize.width() - 4, m_itemSize.height() - 25);
				iconSide = qBound(16, iconSide, 128); 
				topMargin = 5;
				leftMargin = (m_itemSize.width() - iconSide) / 2;
			} else {
				// List Mode: standard list icon size (20px in 30px row)
				iconSide = 20; 
				topMargin = (m_itemSize.height() - iconSide) / 2;
				leftMargin = 8;
			}

			QPixmap rawPix = m_icon.pixmap(iconSide, iconSide);
			if (rawPix.width() != iconSide || rawPix.height() != iconSide) {
				rawPix = rawPix.scaled(iconSide, iconSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
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

					QPixmap arrowPix = arrowIcon.pixmap(overlaySize, overlaySize);
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
			textLabel->setAlignment(Qt::AlignCenter);
			textLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
			textLabel->show();
		}

		isReady = true;
	}
}

void ListItem::setItemData(ListItemData data)
{
	m_itemData = data;
	m_type = m_itemData.type();
	if (m_type == ObjectType::SpecialFolder) {
		m_filePath = QString::fromStdString(m_itemData.itemData().path);
		m_normalizedPath = m_filePath;
	}
	else {
		m_filePath = m_itemData.filePath();
		m_normalizedPath = QDir::toNativeSeparators(m_filePath);
	}
	while (m_normalizedPath.endsWith('\\')) m_normalizedPath.chop(1);
	// Reset icon cache so initData will recreate m_iconWidget
	m_itemData.m_iconSizeCached = -1;
	//update();  // ?????????
}

void ListItem::setItemSize(int width, int height)
{
	m_itemSize = QSize(width, height);
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
	if (this->isReady == true) {
		update();
	}
}

void ListItem::openItem()
{
	if (!isReady || m_filePath.isEmpty()) return;

	// 使用 Windows ShellExecuteW 打开文件或文件夹
	// 转换路径为本地分隔符（虽然 ShellExecute 通常处理两者，但本地化更稳妥）
	std::wstring wPath = QDir::toNativeSeparators(m_filePath).toStdWString();
	ShellExecuteW(NULL, L"open", wPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

ListItemData ListItem::getItemData()
{
	return m_itemData;
}

void ListItem::refreshIcon()
{
    // Force icon refresh by resetting cached size and re-initializing
    m_itemData.m_iconSizeCached = -1;
    m_itemData.initIcon();
    m_icon = m_itemData.icon();
    initData(); // Re-create icon widget with new icon
    update();   // Trigger repaint
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
	QString text = this->baseName();
	QFontMetrics fm = painter.fontMetrics();
	int textWidth = fm.horizontalAdvance(text);
	int textHeight = fm.height();

	if (m_layoutMode == LayoutMode::IconMode) {
		int textX;
		if (textWidth > rect.width() - 10) {
			text = fm.elidedText(text, Qt::ElideRight, rect.width() - 10);
			textWidth = fm.horizontalAdvance(text);
			textX = 5;
		} else {
			textX = (rect.width() - textWidth) / 2;
		}
		textLabel->resize(textWidth, textHeight);
		textLabel->setText(text);
		textLabel->move(textX, rect.height() - textHeight - 5);
	} else {
		// List Mode: Text to the right of icon
		int iconRight = 8 + 20 + 10; // Left margin + icon size + spacing
		int availableTextW = qMax(10, rect.width() - iconRight - 10);
		if (textWidth > availableTextW) {
			text = fm.elidedText(text, Qt::ElideRight, availableTextW);
			textWidth = fm.horizontalAdvance(text);
		}
		textLabel->resize(textWidth, textHeight);
		textLabel->setText(text);
		textLabel->move(iconRight, (rect.height() - textHeight) / 2);
	}
	

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


}
