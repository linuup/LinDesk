#include "ListView.h"

// 静态成员初始化
ListView* ListView::m_instance = nullptr;

ListView::ListView(QWidget* parent) : QWidget(parent) {
    // 设置桌面背景图片
    //SystemParametersInfo(SPI_SETDESKWALLPAPER, 0, (PVOID)imagePath.toStdString().c_str(), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    setWindowTitle("LinDesk");
    setObjectName("LinDesk");
    // 设置无边框窗口
    //setWindowFlags(Qt::FramelessWindowHint | Qt::WindowSystemMenuHint);

    // 设置透明背景
    setAttribute(Qt::WA_TranslucentBackground);

    // 设置透明并允许透过窗口看到后面的内容
    setAttribute(Qt::WA_TransparentForMouseEvents); // 允许点击透过窗口
    setAttribute(Qt::WA_OpaquePaintEvent);  // 启用背景透明化

    setGeometry(QApplication::primaryScreen()->geometry());

    // 按y展开，到底部开始第二列
    loadIconLayer();

    rightMenu = RightMenu::getInstance();
    rightMenu->setHWND((HWND)this->winId());
}

ListView::~ListView() {
    // 清理 ListItem 对象
    //qDeleteAll(m_listItems);
}

void ListView::addItem(ListItem* item)
{
}

void ListView::setSelectedItem(ListItem* item)
{
}

ListItem* ListView::selectedItem() const
{
    return nullptr;
}

void ListView::clearSelection()
{
}

bool ListView::isItemSelected(ListItem* item) const
{
    return false;
}

QVector<ListItem*> ListView::selectedItems() const
{
    return QVector<ListItem*>();
}

void ListView::setMultiSelection(bool enabled)
{
}

bool ListView::isMultiSelection() const
{
    return false;
}

void ListView::setBackgroundImage(const QString& imagePath)
{
}

QString ListView::backgroundImage() const
{
    return QString();
}

void ListView::paintEvent(QPaintEvent* event)
{
}



void ListView::contextMenuEvent(QContextMenuEvent* event)
{
    if (event->reason() == QContextMenuEvent::Mouse && this->rect().contains(event->pos())) {
        requestSystemRightMenu(ObjectType::BackGround, mapToGlobal(event->pos()));
    }
}

ListView* ListView::instance(QWidget* parent) {
    // 如果实例不存在，则创建新的实例
    if (!m_instance) {
        m_instance = new ListView(parent);
    }
    return m_instance;
}

std::vector<ListItemData> ListView::loadAllItemsData()
{
    std::vector<ListItemData> allItemsData;
    CSystemApi c;
    std::vector<IconItem> items = c.GetDesktopPIDLItems();
    for (const IconItem item : items) {
        ListItemData* data = new ListItemData(item);
        ////data.setIconItem(item);
        allItemsData.push_back(*data);
    }
    return allItemsData;
}

//计算每个控件的纵向位置（y），并检查是否超出了窗口的高度。
//一旦超出窗口高度，切换到下一列，并重置纵向位置（y）为新的列开始位置。
void ListView::loadIconLayer()
{
    QVector<ListItem*> listItems;
    vector<ListItemData> allDatas = loadAllItemsData();
    // 行、列总数量
    int row = 12;
    int col = 24;
    int rowSpacing = 5; // 每行之间的间距
    int colSpacing = 10; // 每列之间的间距
    int iconWidth = 76;  // 图标宽度
    int iconHeight = 88; // 图标高度

    // 初始位置
    int Icon_X = 2;   // 第一列的 X 坐标
    int Icon_Y = 8;   // 第一行的 Y 坐标

    // 定义文件索引
    int index = 0;
    // 遍历列和行来布局图标
    //迭代器遍历
    for (auto it = allDatas.begin(); it != allDatas.end(); it++)
    {
        ListItem* listItem = new ListItem(this);
        listItem->resize(iconWidth, iconHeight);
        listItem->setItemData(std::move(*it));
        listItem->setObjectName(QUuid::createUuid().toString(QUuid::WithoutBraces));
        listItem->move(Icon_X, Icon_Y);  // 设置图标位置
        listItems.push_back(listItem);
        // 更新 Y 坐标，增加图标高度和列间距
        Icon_Y = Icon_Y + iconHeight + rowSpacing;

        // 如果超出窗口高度，切换到下一列，Y 坐标重置为顶部
        if (Icon_Y + iconHeight > height()) {
            // 切换到下一列
            Icon_X = Icon_X + iconWidth + colSpacing;
            Icon_Y = 8;  // 重置 Y 坐标为顶部
        }
    }
    m_listItems = listItems;
}