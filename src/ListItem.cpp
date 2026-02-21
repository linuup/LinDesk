#include "ListItem.h"
#include "ListView.h"
#include <SysInfoUtil.h>
#include <shellapi.h>
class ListView;

ListItem::ListItem(QWidget* parent)
	: QWidget(parent)
{
	// 设置无边框窗口
	setWindowFlags(Qt::FramelessWindowHint | Qt::WindowSystemMenuHint);

	// 设置透明背景
	setAttribute(Qt::WA_TranslucentBackground);

	// 设置透明并允许透过窗口看到后面的内容
	//setAttribute(Qt::WA_TransparentForMouseEvents); // 允许点击透过窗口
	//setAttribute(Qt::WA_OpaquePaintEvent);  // 启用背景透明化
	//setStyleSheet("background-color:rgb(25,255,255);");
	setAttribute(Qt::WA_TransparentForMouseEvents);


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
	

	// 设置无边框窗口
	setWindowFlags(Qt::FramelessWindowHint | Qt::WindowSystemMenuHint);

	// 设置透明背景
	setAttribute(Qt::WA_TranslucentBackground);

	// 设置透明并允许透过窗口看到后面的内容
	//setAttribute(Qt::WA_TransparentForMouseEvents); // 允许点击透过窗口
	//setAttribute(Qt::WA_OpaquePaintEvent);  // 启用背景透明化
	//setStyleSheet("background-color:rgb(25,255,255);");
	setAttribute(Qt::WA_TransparentForMouseEvents);

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
	// 2. 删除所有子控件
	if (m_iconWidget) {
		delete m_iconWidget;
		m_iconWidget = nullptr;
	}

	if (textLabel) {
		delete textLabel;
		textLabel = nullptr;
	}
	this->isReady = false;
	// 4. 立即删除自己
	QWidget::close();  // 关闭窗口
}


void ListItem::initData()
{
	m_type = m_itemData.type();

	if (m_type != ObjectType::Empty_ObjectType)
	{
		
		m_filePath = m_itemData.filePath();
		m_fileInfo = QFileInfo(m_filePath.replace("LNK", "lnk"));
		if (m_type == ObjectType::SpecialFolder) {
			update();
		}
		if (m_itemData.fullName() == "This PC") {
			update();
		}
		m_baseName = (m_type == ObjectType::SpecialFolder) ? m_itemData.fullName() : m_fileInfo.baseName();
		m_icon = m_itemData.icon();

		// 先删除旧的图标 widget，防止重复调用时堆叠
		if (m_iconWidget) {
			//m_iconWidget->deleteLater();
			m_iconWidget = nullptr;
		}

		// 显示图标
		if (!m_icon.isNull() && !m_icon.availableSizes().isEmpty()) {
			m_iconWidget = new QLabel(this);
			// 优先取第3个尺寸，不够则取最后一个
			int sizeIdx = qMin(2, m_icon.availableSizes().size() - 1);
			m_iconWidget->setPixmap(m_icon.pixmap(m_icon.availableSizes().at(sizeIdx)));
			// 设置透明背景
			m_iconWidget->setAttribute(Qt::WA_TranslucentBackground);
			// 计算居中位置
			int topMargin = 5;
			int centerX = (m_itemSize.width() - m_iconWidget->pixmap()->width()) / 2;
			m_iconWidget->move(centerX, topMargin);
			// 允许点击穿透
			m_iconWidget->setAttribute(Qt::WA_TransparentForMouseEvents);
			m_iconWidget->show();
		}

		isReady = true;
	}

}

void ListItem::setItemData(ListItemData data)
{
	m_itemData = data;
	//update();  // 数据改变时请求重绘
}

void ListItem::setItemSize(int width, int height)
{
	m_itemSize = QSize(width, height);
	this->resize(width, height);
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
	int shrinkSize = 15;
	m_chooseRect = m_itemRect.adjusted(shrinkSize, shrinkSize, -shrinkSize, -shrinkSize);
}

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

void ListItem::contextMenuEvent(QContextMenuEvent* event) {
	
	if (event->reason() == QContextMenuEvent::Mouse) {
		qDebug() << this->objectName();
	}
	//ListView* lin = qobject_cast<ListView*>(this->parent());
	//if (lin) {
		//传全局坐标
		//lin->requestSystemRightMenu(m_itemData.type(), mapToGlobal(event->pos()), this->m_itemData.filePath());
	//}

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
	QPainter hoverMaskPainter(this);
	painter.setRenderHint(QPainter::Antialiasing);  // 启用抗锯齿
	QRect rect = this->rect();
	QPoint pos = this->pos();
	QSize size = this->size();
	// 计算文字宽度并居中

	QString text = this->baseName();
	QFontMetrics fm = painter.fontMetrics();

	// 计算文字需要的空间
	int textWidth = fm.horizontalAdvance(text);
	int textHeight = fm.height();

	// 计算绘制位置
	int textX, textY;

	// 水平位置：居中，但如果文字太长则左对齐并添加省略号
	if (textWidth > rect.width() - 20) {  // 保留20像素边距
		text = fm.elidedText(text, Qt::ElideRight, rect.width() - 20);
		textWidth = fm.horizontalAdvance(text);
		textX = 10;  // 左对齐
	}
	else {
		textX = (rect.width() - textWidth) / 2;  // 居中
	}

	// 垂直位置：底部向上10像素，但要考虑文字基线
	textY = rect.height() - 15;  // 距离底部10像素

	// 确保文字不会超出下边界
	if (textY > rect.height() - 5) {
		textY = rect.height() - 5;
	}

	// 确保文字不会与上边界重叠
	if (textY < textHeight) {
		textY = textHeight;
	}

	
	// 绘制文字
	textLabel->resize(textWidth, textHeight);
	textLabel->setText(text);
	textLabel->move(textX, textY);
	



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
		hoverMaskPainter.setPen(Qt::NoPen);
		hoverMaskPainter.setBrush(QColor(100, 150, 255, 30));
		hoverMaskPainter.drawRoundedRect(-3, -3, width + 6, height + 6, 8, 8);

		// 第二层：中发光
		/*hoverMaskPainter.setBrush(QColor(80, 140, 255, 60));
		hoverMaskPainter.drawRoundedRect(-1, -1, width + 2, height + 2, 6, 6);*/

		// 第三层：内发光（主背景）
		hoverMaskPainter.setBrush(QColor(255, 255, 255, 90));
		hoverMaskPainter.drawRoundedRect(1, 1, width - 2, height - 2, 2, 2);

		// 第四层：高亮边框
		hoverMaskPainter.setPen(QPen(QColor(255, 255, 255, 150), 0.7));
		hoverMaskPainter.setBrush(Qt::NoBrush);
		hoverMaskPainter.drawRoundedRect(1, 1, width - 2, height - 2, 2, 2);
	}


}
