#include "ListItem.h"
#include "ListView.h"
class ListView;

ListItem::ListItem(QWidget* parent) : QWidget(parent)
{
		//this->resize(76, 88); // 设置默认大小

		setStyleSheet("background-color: yellow;");
}

ListItem::~ListItem()
{
}

void ListItem::setItemData(ListItemData data)
{
    m_itemData = data;
    //update();  // 数据改变时请求重绘
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
        lin->requestSystemRightMenu(m_itemData.type(), mapToGlobal(event->pos()), this->m_itemData.filePath());
    }
	
}

void ListItem::paintEvent(QPaintEvent* event) {


    //qDebug() << this->size();
    if (&m_itemData == nullptr) {
        return;
    }
    QPainter painter(this);

    painter.setRenderHint(QPainter::Antialiasing);  // 启用抗锯齿

    // 设置蓝色边框的颜色和宽度
    QPen pen;
    if (m_itemData.specialFolderType() == SpecialFolders::UnknownFolder) {
        pen.setColor(Qt::red);  // 设置边框颜色为蓝色
    }
    else {
        pen.setColor(Qt::blue);  // 设置边框颜色为蓝色
    }
    painter.fillRect(0, 0, 76, 88, Qt::yellow);
    // 绘制图标
    QPixmap pixmap = m_itemData.icon().pixmap(48, 48); // 获取48x48的图标
    //QPixmap pixmap = QIcon("C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\Common7\\IDE\\devenv.exe").pixmap(64, 64); // 获取48x48的图标
    int iconX = (width() - pixmap.width()) / 2; // 图标水平居中
    int iconY = 10; // 图标距离顶部10像素
    painter.drawPixmap(iconX, iconY, pixmap);


    // 计算文字宽度并居中
    int textWidth = painter.fontMetrics().horizontalAdvance(m_itemData.fullName());
    int textX = (width() - textWidth) / 2; // 文本水平居中
    int textY = height() - 10; // 距离底部10像素
    painter.setBrush(QBrush(Qt::white));
    // 绘制文字
    painter.drawText(10, height() - 10, m_itemData.fullName());



}