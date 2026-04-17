#include "box/BoxManager.h"
#include "box/LinBox.h"
#include "desktop/ListView.h"
#include "desktop/ListItem.h"
#include <QWidget>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QDataStream>
#include <QDir>
#include <QUuid>
#include <QDebug>
#include <QSet>

namespace {

QString normalizeMappedFolderPathValue(const QString& path)
{
    QString normalized = QDir::toNativeSeparators(path.trimmed());
    while (normalized.endsWith('\\') || normalized.endsWith('/')) {
        normalized.chop(1);
    }
    return normalized;
}

}

BoxManager* BoxManager::m_instance = nullptr;

BoxManager::BoxManager(QObject* parent)
    : QObject(parent)
    , m_saveTimer(new QTimer(this))
{
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(150);
    connect(m_saveTimer, &QTimer::timeout, this, [this]() { emit saveRequested(); });
}

BoxManager::~BoxManager() {
    saveAll();
}

void BoxManager::setPersistencePath(const QString& path) {
    m_persistencePath = path;
}

LinBox* BoxManager::createBox(const QPoint& pos, QWidget* parent) {
    LinBox* box = new LinBox(parent);
    box->setBoxId(QUuid::createUuid().toString());
    box->move(pos);

    LinBox* templateBox = nullptr;
    for (const auto& ptr : m_boxes) {
        if (LinBox* existing = ptr.data()) {
            templateBox = existing;
            break;
        }
    }
    if (templateBox) {
        box->setShowType(templateBox->showType());
        box->setBackgroundColor(templateBox->backgroundColor());
        box->setBackgroundOpacity(templateBox->backgroundOpacity());
        box->setGlassEffect(templateBox->glassEffect());
    }
    
    setupBoxConnections(box);
    box->show();
    box->raise();
    
    m_boxes.append(QPointer<LinBox>(box));
    requestSave();
    
    emit boxCreated(box);
    return box;
}

void BoxManager::destroyBox(LinBox* box, ListView* listView) {
    if (!box) return;
    Q_UNUSED(listView);

    // 閫氱煡澶栭儴澶勭悊锛堟仮澶嶅浘鏍囧埌妗岄潰绛夛級
    emit boxDestroyed(box);
    
    m_boxes.removeAll(QPointer<LinBox>(box));
    box->hide();
    box->setParent(nullptr);
    box->deleteLater();
    
    requestSave();
}

void BoxManager::setupConnections(LinBox* box, ListView* listView) {
    if (!box) return;
    
    // 甯冨眬鍙樺寲鏃惰姹備繚瀛?
    connect(box, &LinBox::geometryChanged, this, &BoxManager::requestSave, Qt::UniqueConnection);
    
    // Item 淇″彿杞彂鍒?ListView
    if (listView) {
        connect(box, &LinBox::itemRelayClicked, listView, &ListView::onItemRelayClicked, Qt::UniqueConnection);
        connect(box, &LinBox::itemRelayMoved, listView, &ListView::onItemRelayMoved, Qt::UniqueConnection);
        connect(box, &LinBox::itemRelayReleased, listView, &ListView::onItemRelayReleased, Qt::UniqueConnection);
        connect(box, &LinBox::itemRelayDoubleClicked, listView, &ListView::onItemRelayDoubleClicked, Qt::UniqueConnection);
        connect(box, &LinBox::itemRelayContextMenuRequested, listView, &ListView::onItemRelayContextMenuRequested, Qt::UniqueConnection);
        
        // 鎷栨斁鏂囦欢鍒?Box
        connect(box, &LinBox::destroyRequested, listView, &ListView::onMenuDestroyBox, Qt::UniqueConnection);
        connect(box, &LinBox::customRequested, listView, &ListView::onMenuCustomBox, Qt::UniqueConnection);
    }
}

void BoxManager::addBox(LinBox* box) {
    if (!box) return;
    for (auto& ptr : m_boxes) {
        if (ptr == box) return;  // Already exists
    }
    m_boxes.append(QPointer<LinBox>(box));
    setupBoxConnections(box);
}

void BoxManager::removeBox(LinBox* box) {
    if (box) {
        m_boxes.removeAll(QPointer<LinBox>(box));
    }
}

void BoxManager::clearBoxes() {
    m_boxes.clear();
}

LinBox* BoxManager::findBoxAt(const QPoint& pos) const {
    for (auto& ptr : m_boxes) {
        LinBox* box = ptr.data();
        if (box && box->geometry().contains(pos)) {
            return box;
        }
    }
    return nullptr;
}

LinBox* BoxManager::findBoxById(const QString& boxId) const {
    for (auto& ptr : m_boxes) {
        LinBox* box = ptr.data();
        if (box && box->boxId() == boxId) {
            return box;
        }
    }
    return nullptr;
}

void BoxManager::raiseAllBoxes() {
    for (auto& ptr : m_boxes) {
        if (LinBox* box = ptr.data()) box->raise();
    }
}

void BoxManager::bringToFront(LinBox* box) {
    if (!box) {
        return;
    }

    const QPointer<LinBox> ptr(box);
    if (!m_boxes.isEmpty() && m_boxes.back() == ptr) {
        return;
    }
    m_boxes.removeAll(ptr);
    m_boxes.append(ptr);
    raiseAllBoxes();
}

void BoxManager::setupBoxConnections(LinBox* box) {
    if (!box) return;
    
    // 甯冨眬鍙樺寲鏃惰姹備繚瀛?
    connect(box, &LinBox::geometryChanged, this, &BoxManager::requestSave, Qt::UniqueConnection);
}

void BoxManager::requestSave() {
    // 瑙﹀彂 saveRequested 淇″彿锛岃 ListView 鏉ヤ繚瀛橈紙鍖呮嫭鍥炬爣浣嶇疆锛?
    emit saveRequested();
}

void BoxManager::saveAll() {
    QFile file(m_persistencePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "[BoxManager] Failed to open file for writing:" << m_persistencePath;
        return;
    }

#ifdef QT_DEBUG
    QJsonObject rootObj;
    QJsonObject settingsObj;
    // Default settings for new files
    settingsObj["viewMode"] = 1;  // Medium icon
    settingsObj["sortType"] = 0;
    settingsObj["iconPosMode"] = 0;
    settingsObj["autoArrange"] = false;
    settingsObj["gridAlignment"] = false;
    settingsObj["showDesktopIcons"] = true;
    settingsObj["doubleClickHideIcons"] = false;
    settingsObj["showShortCutArrow"] = true;
    settingsObj["enablePersistence"] = false;
    rootObj["settings"] = settingsObj;
    
    QJsonObject iconPosObj;
    rootObj["iconPositions"] = iconPosObj;
    
    QJsonArray boxArray;
    for (auto& ptr : m_boxes) {
        LinBox* box = ptr.data();
        if (!box) continue;
        QJsonObject b;
        b["id"] = box->boxId();
        b["title"] = box->boxTitle();
        b["x"] = box->x();
        b["y"] = box->y();
        b["w"] = box->width();
        b["h"] = box->isCollapsed() ? box->originalHeight() : box->height();
        b["collapsed"] = box->isCollapsed();
        b["showType"] = (int)box->showType();
        b["color"] = box->backgroundColor().name();
        b["opacity"] = qRound(box->backgroundOpacity() * 100);
        b["glass"] = box->glassEffect();
        b["mappedFolderPath"] = box->mappedFolderPath();
        
        QJsonArray files;
        QSet<QString> seenPaths;
        for (auto* item : box->items()) {
            if (!item) continue;
            QString filePath = item->filePath();
            if (seenPaths.contains(filePath)) continue;
            seenPaths.insert(filePath);
            files.append(filePath);
        }
        b["files"] = files;
        boxArray.append(b);
    }
    rootObj["box-datas"] = boxArray;
    
    file.write(QJsonDocument(rootObj).toJson());
#else
    QDataStream out(&file);
    out << (int)m_boxes.size();
    for (auto& ptr : m_boxes) {
        LinBox* box = ptr.data();
        if (!box) continue;
        out << box->boxId() << box->boxTitle() << box->geometry() 
            << box->isCollapsed() << box->originalHeight() << (int)box->showType() 
            << box->backgroundColor() << (double)box->backgroundOpacity() << box->glassEffect()
            << box->mappedFolderPath();
        QStringList files;
        QSet<QString> seenPaths;
        for (auto* item : box->items()) {
            if (!item) continue;
            QString filePath = item->filePath();
            if (seenPaths.contains(filePath)) continue;
            seenPaths.insert(filePath);
            files << filePath;
        }
        out << files;
    }
#endif
    
    file.close();
    qDebug() << "[BoxManager] Saved" << m_boxes.size() << "boxes to:" << m_persistencePath;
}

void BoxManager::saveAll(const QString& persistencePath, bool enablePersistence,
                         const QVector<ListItem*>& allItems,
                         const QMap<QString, QMap<QString, QVariant>>& iconPositions,
                         int viewMode, int sortType, int iconPosMode,
                         bool autoArrange, bool gridAlignment,
                         bool showDesktopIcons, bool doubleClickHideIcons,
                         bool showShortCutArrow) {
    QFile file(persistencePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "[BoxManager] Failed to open file for writing:" << persistencePath;
        return;
    }

#ifdef QT_DEBUG
    QJsonObject rootObj;
    QJsonObject settingsObj;
    settingsObj["viewMode"] = viewMode;
    settingsObj["sortType"] = sortType;
    settingsObj["iconPosMode"] = iconPosMode;
    settingsObj["autoArrange"] = autoArrange;
    settingsObj["gridAlignment"] = gridAlignment;
    settingsObj["showDesktopIcons"] = showDesktopIcons;
    settingsObj["doubleClickHideIcons"] = doubleClickHideIcons;
    settingsObj["showShortCutArrow"] = showShortCutArrow;
    settingsObj["enablePersistence"] = enablePersistence;
    rootObj["settings"] = settingsObj;
    
    QJsonArray iconPosArray;
    for (auto it = iconPositions.begin(); it != iconPositions.end(); ++it) {
        QJsonObject posObj;
        posObj["path"] = it.key();
        for (auto pit = it.value().begin(); pit != it.value().end(); ++pit) {
            posObj[pit.key()] = QJsonValue::fromVariant(pit.value());
        }
        iconPosArray.append(posObj);
    }
    rootObj["iconPositions"] = iconPosArray;
    
    QJsonArray boxArray;
    for (auto& ptr : m_boxes) {
        LinBox* box = ptr.data();
        if (!box) continue;
        QJsonObject b;
        b["id"] = box->boxId();
        b["title"] = box->boxTitle();
        b["x"] = box->x();
        b["y"] = box->y();
        b["w"] = box->width();
        b["h"] = box->isCollapsed() ? box->originalHeight() : box->height();
        b["collapsed"] = box->isCollapsed();
        b["showType"] = (int)box->showType();
        b["color"] = box->backgroundColor().name();
        b["opacity"] = qRound(box->backgroundOpacity() * 100);
        b["glass"] = box->glassEffect();
        b["mappedFolderPath"] = box->mappedFolderPath();
        
        QJsonArray files;
        QSet<QString> seenPaths;
        for (auto* item : box->items()) {
            if (!item) continue;
            QString filePath = item->filePath();
            if (seenPaths.contains(filePath)) continue;
            seenPaths.insert(filePath);
            files.append(filePath);
        }
        b["files"] = files;
        boxArray.append(b);
    }
    rootObj["box-datas"] = boxArray;
    
    file.write(QJsonDocument(rootObj).toJson());
#else
    QDataStream out(&file);
    out << (int)m_boxes.size();
    for (auto& ptr : m_boxes) {
        LinBox* box = ptr.data();
        if (!box) continue;
        out << box->boxId() << box->boxTitle() << box->geometry() 
            << box->isCollapsed() << box->originalHeight() << (int)box->showType() 
            << box->backgroundColor() << (double)box->backgroundOpacity() << box->glassEffect()
            << box->mappedFolderPath();
        QStringList files;
        QSet<QString> seenPaths;
        for (auto* item : box->items()) {
            if (!item) continue;
            QString filePath = item->filePath();
            if (seenPaths.contains(filePath)) continue;
            seenPaths.insert(filePath);
            files << filePath;
        }
        out << files;
    }
    // Save settings
    out << enablePersistence << viewMode << sortType << iconPosMode 
        << autoArrange << gridAlignment << showDesktopIcons 
        << doubleClickHideIcons << showShortCutArrow;
#endif
    
    file.close();
    qDebug() << "[BoxManager] Saved" << m_boxes.size() << "boxes and settings to:" << persistencePath;
}

void BoxManager::loadAll(const QString& persistencePath, bool autoUpdateState,
                         bool& enablePersistence, QMap<QString, QString>& iconShowRules,
                         int& viewMode, int& sortType, int& iconPosMode,
                         bool& autoArrange, bool& gridAlignment,
                         bool& showDesktopIcons, bool& doubleClickHideIcons,
                         bool& showShortCutArrow,
                         QVector<QMap<QString, QVariant>>& boxData,
                         QMap<QString, QMap<QString, QVariant>>& iconPositions) {
    QFile file(persistencePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        qDebug() << "[BoxManager] No persistence file found:" << persistencePath;
        return;
    }

#ifdef QT_DEBUG
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject rootObj = doc.object();
    if (doc.isNull() || doc.isEmpty() || !rootObj.contains("settings") || !rootObj.contains("box-datas") || !rootObj.contains("iconPositions")) {
        qWarning() << "[BoxManager] Invalid persistence format. Expected keys: settings, iconPositions, box-datas.";
        file.close();
        return;
    }
    
    // Load settings
    QJsonObject settingsObj = rootObj["settings"].toObject();
    enablePersistence = settingsObj["enablePersistence"].toBool();
    viewMode = settingsObj["viewMode"].toInt();
    sortType = settingsObj["sortType"].toInt();
    iconPosMode = settingsObj["iconPosMode"].toInt();
    autoArrange = settingsObj["autoArrange"].toBool();
    gridAlignment = settingsObj["gridAlignment"].toBool();
    showDesktopIcons = settingsObj["showDesktopIcons"].toBool();
    doubleClickHideIcons = settingsObj["doubleClickHideIcons"].toBool();
    showShortCutArrow = settingsObj["showShortCutArrow"].toBool();
    
    // Load icon positions (array format)
    QJsonArray iconPosArray = rootObj["iconPositions"].toArray();
    for (const QJsonValue& val : iconPosArray) {
        QJsonObject posObj = val.toObject();
        QString path = posObj["path"].toString();
        QMap<QString, QVariant> posMap;
        posMap["x"] = posObj["x"].toInt();
        posMap["y"] = posObj["y"].toInt();
        posMap["boxId"] = posObj["boxId"].toString();
        iconPositions[path] = posMap;
    }
    
    // Load boxes
    QJsonArray boxArray = rootObj["box-datas"].toArray();
    for (int i = 0; i < boxArray.size(); ++i) {
        QJsonObject b = boxArray[i].toObject();
        QMap<QString, QVariant> data;
        data["id"] = b["id"].toString();
        data["title"] = b["title"].toString();
        data["x"] = b["x"].toInt();
        data["y"] = b["y"].toInt();
        data["w"] = b["w"].toInt();
        data["h"] = b["h"].toInt();
        data["collapsed"] = b["collapsed"].toBool();
        data["showType"] = b["showType"].toInt();
        data["color"] = b["color"].toString();
        data["opacity"] = b["opacity"].toDouble();
        data["glass"] = b["glass"].toBool();
        data["mappedFolderPath"] = b["mappedFolderPath"].toString();
        
        QStringList files;
        QJsonArray fArr = b["files"].toArray();
        for (int j = 0; j < fArr.size(); ++j) {
            files << fArr[j].toString();
        }
        data["files"] = files;
        boxData.append(data);
    }
#else
    QDataStream in(&file);
    int count;
    in >> count;
    for (int i = 0; i < count; ++i) {
        QString boxId, title;
        QRect geom;
        bool collapsed, glass;
        int origH, showType;
        QStringList files;
        QString mappedFolderPath;
        QColor color;
        double opacity;
        in >> boxId >> title >> geom >> collapsed >> origH >> showType >> color >> opacity >> glass >> mappedFolderPath >> files;
        
        QMap<QString, QVariant> data;
        data["id"] = boxId;
        data["title"] = title;
        data["x"] = geom.x();
        data["y"] = geom.y();
        data["w"] = geom.width();
        data["h"] = geom.height();
        data["collapsed"] = collapsed;
        data["showType"] = showType;
        data["color"] = color.name();
        data["opacity"] = opacity;
        data["glass"] = glass;
        data["mappedFolderPath"] = mappedFolderPath;
        data["files"] = files;
        boxData.append(data);
    }
    // Load settings
    in >> enablePersistence >> viewMode >> sortType >> iconPosMode 
       >> autoArrange >> gridAlignment >> showDesktopIcons 
       >> doubleClickHideIcons >> showShortCutArrow;
#endif
    
    file.close();
    qDebug() << "[BoxManager] Loaded" << boxData.size() << "boxes from:" << persistencePath;
}

void BoxManager::loadAll(bool autoUpdateState) {
    QFile file(m_persistencePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        qDebug() << "[BoxManager] No persistence file found:" << m_persistencePath;
        return;
    }

#ifdef QT_DEBUG
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject rootObj = doc.object();
    QJsonArray boxArray = rootObj["box-datas"].toArray();
    
    for (int i = 0; i < boxArray.size(); ++i) {
        QJsonObject b = boxArray[i].toObject();
        QStringList files;
        QJsonArray fArr = b["files"].toArray();
        for (int j = 0; j < fArr.size(); ++j) {
            files << fArr[j].toString();
        }
        
        QRect geom(b["x"].toInt(), b["y"].toInt(), b["w"].toInt(), b["h"].toInt());
        QColor color = QColor(b["color"].toString());
        float opacity = b["opacity"].toDouble() / 100.0f;
        if (opacity <= 0) opacity = 0.4f;
        bool glass = b.contains("glass") ? b["glass"].toBool() : true;
        QString mappedFolderPath = b["mappedFolderPath"].toString();
        QString boxId = b.contains("id") ? b["id"].toString() : QUuid::createUuid().toString();
        
        // 閫氱煡澶栭儴鍒涘缓 Box 骞跺姞杞芥暟鎹?
        // ListView 搴旇杩炴帴 boxCreated 淇″彿骞跺垱寤哄搴旂殑 Box
        emit boxCreated(nullptr); // Placeholder
        Q_UNUSED(boxId);
        Q_UNUSED(geom);
        Q_UNUSED(color);
        Q_UNUSED(opacity);
        Q_UNUSED(glass);
        Q_UNUSED(mappedFolderPath);
        Q_UNUSED(files);
        Q_UNUSED(b);
    }
#else
    QDataStream in(&file);
    int count;
    in >> count;
    for (int i = 0; i < count; ++i) {
        QString boxId;
        QString title;
        QRect geom;
        bool collapsed;
        int origH;
        int showType;
        QStringList files;
        QString mappedFolderPath;
        QColor color;
        double opacity;
        bool glass;
        in >> boxId >> title >> geom >> collapsed >> origH >> showType >> color >> opacity >> glass >> mappedFolderPath >> files;
        emit boxCreated(nullptr); // Placeholder
        Q_UNUSED(boxId);
        Q_UNUSED(title);
        Q_UNUSED(geom);
        Q_UNUSED(collapsed);
        Q_UNUSED(origH);
        Q_UNUSED(showType);
        Q_UNUSED(files);
        Q_UNUSED(color);
        Q_UNUSED(opacity);
        Q_UNUSED(glass);
        Q_UNUSED(mappedFolderPath);
    }
#endif
    
    file.close();
}

QString BoxManager::getBoxIdForItem(const QString& filePath) const {
    if (filePath.isEmpty()) return QString();
    
    QString target = QDir::toNativeSeparators(filePath);
    while (target.endsWith('\\')) target.chop(1);

    for (auto& ptr : m_boxes) {
        LinBox* box = ptr.data();
        if (!box) continue;
        for (auto* item : box->items()) {
            if (item) {
                QString itemPath = QDir::toNativeSeparators(item->filePath());
                while (itemPath.endsWith('\\')) itemPath.chop(1);
                if (itemPath == target) {
                    return box->boxId();
                }
            }
        }
    }
    return QString();
}

bool BoxManager::isItemInAnyBox(const QString& filePath) const {
    return !getBoxIdForItem(filePath).isEmpty();
}

LinBox* BoxManager::findBoxContainingItem(const QString& filePath) const {
    QString boxId = getBoxIdForItem(filePath);
    if (boxId.isEmpty()) return nullptr;
    return findBoxById(boxId);
}

void BoxManager::refreshAllBoxesLayout() {
    for (auto& ptr : m_boxes) {
        if (LinBox* box = ptr.data()) box->refreshLayout();
    }
}

void BoxManager::setAllBoxesGlassEffect(bool enabled) {
    for (auto& ptr : m_boxes) {
        if (LinBox* box = ptr.data()) box->setGlassEffect(enabled);
    }
    requestSave();
}

void BoxManager::setAllBoxesBackgroundColor(const QColor& color) {
    for (auto& ptr : m_boxes) {
        if (LinBox* box = ptr.data()) box->setBackgroundColor(color);
    }
    requestSave();
}

void BoxManager::setAllBoxesBackgroundOpacity(float opacity) {
    for (auto& ptr : m_boxes) {
        if (LinBox* box = ptr.data()) box->setBackgroundOpacity(opacity);
    }
    requestSave();
}

void BoxManager::moveItemToBox(const QString& filePath, LinBox* targetBox, ListView* listView, int insertIndex, bool refreshBoxes) {
    if (filePath.isEmpty() || !targetBox || !listView) return;
    
    // Find and remove item from its current location
    ListItem* item = listView->findItemByPath(filePath);
    if (!item) return;
    
    // Check if item is in another box, if so remove it
    LinBox* currentBox = findBoxContainingItem(filePath);
    if (currentBox && currentBox != targetBox) {
        currentBox->removeListItem(item, refreshBoxes);
    }
    
    // Update item's box id BEFORE adding to new box
    item->setBoxId(targetBox->boxId());
    
    if (insertIndex >= 0) {
        targetBox->insertListItem(item, insertIndex, refreshBoxes);
    } else {
        targetBox->addListItem(item, refreshBoxes);
    }
    
    requestSave();
}

