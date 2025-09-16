#include <QObject>
#include <QWidget>
#include <QPainter>
#include <QIcon>
#include "ListItemData.h"

#include "ListItemData.h"

class ListItem : public QWidget {
    Q_OBJECT

public:
    explicit ListItem(QWidget* parent = nullptr)
        : QWidget(parent), m_itemData(nullptr) {}

    // 设置对应的 ListItemData
    void setItemData(ListItemData* data) {
        m_itemData = data;
        update();  // 数据改变时请求重绘
    }

    // 获取对应的 ListItemData
    ListItemData* itemData() const { return m_itemData; }

protected:
    void paintEvent(QPaintEvent* event) override {
        if (!m_itemData) return;

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        // 绘制图标
        if (!m_itemData->icon().isNull()) {
            QRect iconRect(10, 10, 32, 32);
            painter.drawPixmap(iconRect, m_itemData->icon().pixmap(32, 32));
        }

        // 绘制文本
        QRect textRect(50, 25, width() - 60, 20);
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, m_itemData->text());

        // 这里可以绘制更多的内容，具体取决于 `ListItemData` 的内容
    }

private:
    ListItemData* m_itemData;  // 关联的数据类
};
