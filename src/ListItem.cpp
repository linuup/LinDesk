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
    //setAttribute(Qt::WA_TransparentForMouseEvents); // 允许点击透过窗口
    //setAttribute(Qt::WA_OpaquePaintEvent);  // 启用背景透明化
    //setStyleSheet("background-color:rgb(25,255,255);");
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

ListItem::~ListItem()
{
}

void ListItem::initData()
{
	m_type = m_itemData.type();
    if(m_type != ObjectType::Empty_ObjectType)
    {
        m_filePath = m_itemData.filePath();
        m_fileInfo = QFileInfo(m_filePath.replace("LNK","lnk"));
        m_baseName = m_fileInfo.baseName();
        QFileIconProvider icon_provider;
        /*qDebug() << m_filePath;
        qDebug() << this->baseName() << m_filePath;*/
        QString targetPath = m_fileInfo.symLinkTarget();
        if (!targetPath.isEmpty()) {
            QFileInfo targetData(targetPath);
            m_icon = icon_provider.icon(targetData); // 获取特定文件的图标
        }
        if (m_icon.isNull()) {
            if (m_fileInfo.isFile()) {
                m_icon = icon_provider.icon(m_fileInfo); // 获取特定文件的图标
            }
            else {
                m_icon = icon_provider.icon(QFileIconProvider::Folder); // 获取文件夹的图标
            }
        }
        
        /*qDebug() << this->baseName() << m_filePath;
        for (auto a : m_icon.availableSizes()) {
			qDebug() << "ListItem initData icon size:" << a;
        }*/
        int iconIndex=m_icon.availableSizes().size();
		//根据图标大小设置区域
		m_itemRect = QRect(m_itemPos, m_itemSize);
        m_iconWidget = new QLabel(this);
        // 设置透明背景
        m_iconWidget->setAttribute(Qt::WA_TranslucentBackground);
        m_iconWidget->resize(m_itemSize);
        // 设置透明并允许透过窗口看到后面的内容
        //m_iconWidget->setAttribute(Qt::WA_TransparentForMouseEvents); // 允许点击透过窗口
        m_iconWidget->setGeometry(0, 0, m_itemSize.width(), m_itemSize.height());
		m_iconWidget->setPixmap(m_icon.pixmap(m_icon.availableSizes().at(1))); // 设置QIcon中的第N个尺寸的图标
  //      qDebug() << "ListItem Icon Widget:" << m_iconWidget->geometry();
		//qDebug() << "ListItem initData icon:" << m_iconWidget->geometry();
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

    //ListView* lin = qobject_cast<ListView*>(this->parent());
    //if (lin) {
        //传全局坐标
        //lin->requestSystemRightMenu(m_itemData.type(), mapToGlobal(event->pos()), this->m_itemData.filePath());
    //}
	
}

void ListItem::paintEvent(QPaintEvent* event) {
    //if (!event->region().intersects(rect())) {
    //    return; // 跳过无关绘制
    //}
    qDebug() << "ListItem painted";
    /*ListView* l = qobject_cast<ListView*>(this->parentWidget());
    if (this->isReady == false|| l->IsSelecting() == true) {
        return;
    }*/
    /*QPainter q(this);
    qDebug() << this->pos() << size() << this->baseName();
    q.fillRect(0, 0, this->width(), this->height(), QColor(228, 171, 171, 255));*/
    QPainter qPainter_item(this);
    
    qPainter_item.setRenderHint(QPainter::Antialiasing);  // 启用抗锯齿
    QPoint pos = this->pos();
    QSize size = this->size();

    // 计算文字宽度并居中
    int textWidth = qPainter_item.fontMetrics().horizontalAdvance(this->baseName());
    int textX = (size.width() - textWidth) / 2; // 文本水平居中
    int textY = size.height() - 10; // 距离底部10像素
    // 绘制文字
    qPainter_item.setPen(Qt::white);
    qPainter_item.drawText(12,12, this->baseName());
    

}
