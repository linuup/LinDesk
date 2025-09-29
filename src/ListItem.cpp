#include "ListItem.h"
#include "ListView.h"
class ListView;

ListItem::ListItem(QWidget* parent)
{
    // 设置无边框窗口
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowSystemMenuHint);

    // 设置透明背景
    setAttribute(Qt::WA_TranslucentBackground);

    // 设置透明并允许透过窗口看到后面的内容
    setAttribute(Qt::WA_TransparentForMouseEvents); // 允许点击透过窗口
    setAttribute(Qt::WA_OpaquePaintEvent);  // 启用背景透明化
    //setStyleSheet("background-color:rgb(255,255,255);");
    
}

ListItem::~ListItem()
{
}

void ListItem::initData()
{
	qDebug() << "ListItem initData" << this;
	m_type = m_itemData.type();
    if(m_type != ObjectType::Empty_ObjectType)
    {
        m_filePath = m_itemData.filePath();
        m_fileInfo = QFileInfo(m_filePath);
        m_baseName = m_fileInfo.baseName();
        QFileIconProvider icon_provider;
        if (m_fileInfo.suffix().isEmpty() || !m_fileInfo.exists()) {

            // 如果文件存在并且不是一个符号链接，则检查它是否是一个 .lnk 文件
            QString lnkPath = m_filePath + ".lnk";
            QFileInfo tmp(lnkPath);
            //bool isLink = CSystemApi::IsLinkFile(reinterpret_cast<const wchar_t*>(lnkPath.utf16()));
            if (tmp.exists() && tmp.isSymLink()) {
                qDebug() << "is linkFile";
                //Lnk File
                //qDebug() << tmp.filePath() << "is a lnkFile" << tmp.isSymLink();
                m_icon = icon_provider.icon(tmp); // 获取特定文件的图标
            }
            m_fileInfo = tmp;
        }
        if (m_icon.isNull()) {
            if (m_fileInfo.isFile()) {
                m_icon = icon_provider.icon(m_fileInfo); // 获取特定文件的图标
            }
            else {
                m_icon = icon_provider.icon(QFileIconProvider::Folder); // 获取文件夹的图标
            }
        }

		//根据图标大小设置区域
		m_itemRect = QRect(m_itemPos, m_itemSize);
        isReady = true;
        return;
    }
    
}

void ListItem::setItemData(ListItemData data)
{
    m_itemData = data;
    //update();  // 数据改变时请求重绘
}

void ListItem::setItemSize(int width, int heigh)
{
	m_itemSize = QSize(width, heigh);
}

void ListItem::setItemPos(int startX, int startY)
{
    m_itemPos = QPoint(startX, startY);
}

ListItemData ListItem::getItemData()
{
    return m_itemData;
}

void ListItem::contextMenuEvent(QContextMenuEvent* event) {
	qDebug() << this->objectName();

    ListView* lin = qobject_cast<ListView*>(this->parent());
    if (lin) {
        //传全局坐标
        //lin->requestSystemRightMenu(m_itemData.type(), mapToGlobal(event->pos()), this->m_itemData.filePath());
    }
	
}

void ListItem::paintEvent(QPaintEvent* event) {
    if (this->isReady == false) {
        return;
    }
    QPainter q(this);
    qDebug() << this->pos() << size() << this->baseName();
    q.fillRect(0, 0, this->width(), this->height(), QColor(228, 171, 171, 255));

    QPainter hoverMaskPainter(this);
    //qDebug() << "MouseMoveEvent:Drawing";
    //q.fillRect(0, 0, 1920, 1080, QColor(228, 171, 171, 255));
    QPainter qPainter_item(this);
    
    qPainter_item.setRenderHint(QPainter::Antialiasing);  // 启用抗锯齿
        QPoint pos = this->pos();
        QSize size = this->size();
        //qPainter_item.fillRect(item->rect(), Qt::yellow);

        // 绘制图标
        QPixmap pixmap = this->icon().pixmap(48, 48); // 获取48x48的图标
        int iconX = (size.width() - pixmap.width()) / 2; // 图标水平居中
        int iconY = 10; // 图标距离顶部10像素
        qPainter_item.drawPixmap(0,0, pixmap);


        // 计算文字宽度并居中
        int textWidth = qPainter_item.fontMetrics().horizontalAdvance(this->baseName());
        int textX = (size.width() - textWidth) / 2; // 文本水平居中
        int textY = size.height() - 10; // 距离底部10像素
        // 绘制文字
        qPainter_item.setPen(Qt::white);
        qPainter_item.drawText(0,0, this->baseName());
    
    /*if (choosingFlag) {
        q.fillRect(drawingMaskRect, Qt::blue);
        update(drawingMaskRect);
    }*/
    



}