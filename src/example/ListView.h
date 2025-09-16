#include <QObject>
#include <QList>
#include <QPainter>
#include <QPixmap>
#include <QString>
#include <QWidget>

class ListView : public QWidget {
    Q_OBJECT

public:
    enum class ViewMode {
        IconView,
        DetailedView
    };

    explicit ListView(ViewMode viewMode = ViewMode::IconView, QWidget* parent = nullptr);

    // 添加一个 ListItem
    void addItem(ListItem* item);

    // 设置选中的项
    void setSelectedItem(ListItem* item);

    // 获取当前选中的项
    ListItem* selectedItem() const;

    // 清除所有选中的项
    void clearSelection();

    // 检查一个项是否被选中
    bool isItemSelected(ListItem* item) const;

    // 获取所有选中的项（支持多选）
    QList<ListItem*> selectedItems() const;

    // 设置多选模式
    void setMultiSelection(bool enabled);

    // 获取是否启用多选
    bool isMultiSelection() const;

    // 设置背景壁纸图片
    void setBackgroundImage(const QString& imagePath);

    // 获取背景壁纸图片路径
    QString backgroundImage() const;

protected:
    void paintEvent(QPaintEvent* event) override;  // 重写绘制事件来绘制背景

signals:
    void itemSelected(ListItem* item);
    void selectionCleared();
    void selectionChanged();

private:
    QList<ListItem*> m_items;             // 存储所有 ListItem 对象
    QList<ListItem*> m_selectedItems;      // 存储所有选中的项
    bool m_isMultiSelection;               // 是否启用多选
    ViewMode m_viewMode;
    QPixmap m_backgroundImage;             // 存储背景壁纸图片
};
