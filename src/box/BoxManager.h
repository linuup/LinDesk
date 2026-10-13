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

#include <QFuture>
#include <QHash>

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
    QFuture<bool> saveAll(const QString& persistencePath, bool enablePersistence,
                          const QMap<QString, QMap<QString, QVariant>>& iconPositions,
                          int viewMode, int sortType, int preAutoSort, int iconPosMode,
                          bool autoArrange, bool gridAlignment,
                          bool showDesktopIcons, bool doubleClickHideIcons,
                          bool showShortCutArrow,
                          bool async = true);
    void loadAll(bool autoUpdateState = true);
    void loadAll(const QString& persistencePath, bool autoUpdateState,
                 bool& enablePersistence, QMap<QString, QString>& iconShowRules,
                 int& viewMode, int& sortType, int& preAutoSort, int& iconPosMode,
                 bool& autoArrange, bool& gridAlignment,
                 bool& showDesktopIcons, bool& doubleClickHideIcons,
                 bool& showShortCutArrow,
                 QVector<QMap<QString, QVariant>>& boxData,
                 QMap<QString, QMap<QString, QVariant>>& iconPositions,
                 int* droppedCount = nullptr);
    void requestSave();
    void requestImmediateSave();
    void setPersistencePath(const QString& path);
    QString persistencePath() const { return m_persistencePath; }

    // 条目级校验与净化辅助
    static bool validateBoxData(const QJsonObject& obj, QSet<QString>& seenBoxIds, QMap<QString, QVariant>& outData, int& droppedCount);
    static bool validateIconPosition(const QJsonObject& obj, const QSet<QString>& validBoxIds, QString& outPath, QMap<QString, QVariant>& outPos, int& droppedCount);
    
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
    void applyUnifiedStyleToBox(LinBox* box);
    QColor unifiedBackgroundColor() const { return m_unifiedBackgroundColor; }
    float unifiedBackgroundOpacity() const { return m_unifiedBackgroundOpacity; }
    bool unifiedGlassEffect() const { return m_unifiedGlassEffect; }
    void setUnifiedBackgroundColor(const QColor& color) { m_unifiedBackgroundColor = color; }
    void setUnifiedBackgroundOpacity(float opacity) { m_unifiedBackgroundOpacity = opacity; }
    void setUnifiedGlassEffect(bool enabled) { m_unifiedGlassEffect = enabled; }

signals:
    void boxCreated(LinBox* box);
    void boxDestroyed(LinBox* box);
    void boxDataChanged();
    void saveRequested();
    void saveFinished(bool success);

private:
    explicit BoxManager(QObject* parent = nullptr);
    ~BoxManager();
    
    void setupBoxConnections(LinBox* box);
    
    static BoxManager* m_instance;
    QVector<QPointer<LinBox>> m_boxes;
    QHash<QString, QPointer<LinBox>> m_boxIdIndex;
    QTimer* m_saveTimer;
    
    // 持久化路径
    QString m_persistencePath;
    
    // 统一盒子外观设置
    QColor m_unifiedBackgroundColor = QColor("#63A8F5");
    float m_unifiedBackgroundOpacity = 0.28f;
    bool m_unifiedGlassEffect = false;
    
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
