#include "ListItem.h"
#include "ListView.h"
class ListView;

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

    



}