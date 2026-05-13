#pragma once
#include <QObject>
#include <QVector>
#include <QMap>
#include <QString>
#include <QRect>
#include <QColor>
#include <QTimer>
#include <QPointer>
#include <QVariant>

class LinBox;
class ListView;

class BoxManager : public QObject {
    Q_OBJECT

public:
    static BoxManager* instance(QObject* parent = nullptr);
    
    // Box 生命周期管理
    LinBox* createBox(const QPoint& pos, QWidget* parent = nullptr);
    void destroyBox(LinBox* box, ListView* listView = nullptr);
    LinBox* findBoxAt(const QPoint& pos) const;
    LinBox* findBoxById(const QString& boxId) const;
    
    // Box 列表操作
    QVector<QPointer<LinBox>> allBoxes() const { return m_boxes; }
    void addBox(LinBox* box);
    void removeBox(LinBox* box);
    void clearBoxes();
    int boxCount() const { return m_boxes.size(); }
    void raiseAllBoxes();
    void bringToFront(LinBox* box);
    
    // 连接设置
    void setupConnections(LinBox* box, ListView* listView);
    
    // 数据持久化
    void saveAll();
    void saveAll(const QString& persistencePath, bool enablePersistence,
                 const QMap<QString, QMap<QString, QVariant>>& iconPositions,
                 const QVector<QMap<QString, QVariant>>& desktopComponents,
                 int viewMode, int sortType, int preAutoSort, int iconPosMode,
                 bool autoArrange, bool gridAlignment,
                 bool showDesktopIcons, bool doubleClickHideIcons,
                 bool showShortCutArrow);
    void loadAll(bool autoUpdateState = true);
    void loadAll(const QString& persistencePath, bool autoUpdateState,
                 bool& enablePersistence, QMap<QString, QString>& iconShowRules,
                 int& viewMode, int& sortType, int& preAutoSort, int& iconPosMode,
                 bool& autoArrange, bool& gridAlignment,
                 bool& showDesktopIcons, bool& doubleClickHideIcons,
                 bool& showShortCutArrow,
                 QVector<QMap<QString, QVariant>>& boxData,
                 QMap<QString, QMap<QString, QVariant>>& iconPositions,
                 QVector<QMap<QString, QVariant>>& desktopComponents);
    void requestSave();
    void setPersistencePath(const QString& path);
    QString persistencePath() const { return m_persistencePath; }
    
    // 文件归属查询
    QString getBoxIdForItem(const QString& filePath) const;
    bool isItemInAnyBox(const QString& filePath) const;
    LinBox* findBoxContainingItem(const QString& filePath) const;
    
    // 移动文件到 Box
    // 批量操作
    void refreshAllBoxesLayout();
    void setAllBoxesGlassEffect(bool enabled);
    void setAllBoxesBackgroundColor(const QColor& color);
    void setAllBoxesBackgroundOpacity(float opacity);

signals:
    void boxCreated(LinBox* box);
    void boxDestroyed(LinBox* box);
    void boxDataChanged();
    void saveRequested();

private:
    explicit BoxManager(QObject* parent = nullptr);
    ~BoxManager();
    
    void setupBoxConnections(LinBox* box);
    
    static BoxManager* m_instance;
    QVector<QPointer<LinBox>> m_boxes;
    QTimer* m_saveTimer;
    
    // 持久化路径
    QString m_persistencePath;
    
    // 禁止拷贝
    Q_DISABLE_COPY(BoxManager)
};

// inline 实现单例
inline BoxManager* BoxManager::instance(QObject* parent) {
    if (!m_instance) {
        m_instance = new BoxManager(parent);
    }
    return m_instance;
}
