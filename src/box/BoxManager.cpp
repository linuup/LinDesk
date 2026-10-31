#include "box/BoxManager.h"
#include "box/LinBox.h"
#include "desktop/ListView.h"
#include "monitor/LinDirectoryMonitor.h"
#include "services/DpiScaleHelper.h"
#include "services/AppPaths.h"
#include <QSettings>
#include <QWidget>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QDir>
#include <QUuid>
#include <QDebug>
#include <QSet>
#include <QDateTime>
#include <QtConcurrent/qtconcurrentrun>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {

constexpr int kMaxPersistedBoxes = 4096;
constexpr int kMaxPersistedIconPositions = 100000;

static const char kLinDeskMagic[4] = {'L', 'D', 'B', 'X'};
static const quint8 kLinDeskVersion = 1;
static const quint8 kLinDeskFlagEncrypted = 0x01;
static const quint8 kXorMask[] = {
    0x5A, 0xA5, 0x6C, 0xC6, 0x73, 0x37, 0x8F, 0xF8,
    0x24, 0x42, 0x1E, 0xE1, 0x9B, 0xB9, 0x3D, 0xD3
};

QString normalizeMappedFolderPathValue(const QString& path)
{
    QString normalized = QDir::toNativeSeparators(path.trimmed());
    while (normalized.endsWith('\\') || normalized.endsWith('/')) {
        normalized.chop(1);
    }
    return normalized;
}

struct PersistedBoxSnapshot {
    QString id;
    QString title;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    bool collapsed = false;
    int showType = 0;
    QString color;
    int opacity = 0;
    bool glass = false;
    QString mappedFolderPath;
    QStringList files;
};

}

BoxManager* BoxManager::m_instance = nullptr;

BoxManager::BoxManager(QObject* parent)
    : QObject(parent)
    , m_saveTimer(new QTimer(this))
{
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(150);
    connect(m_saveTimer, &QTimer::timeout, this, [this]() { emit saveRequested(); });

    QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
    m_unifiedLayeredZDepth = settings.value(QStringLiteral("Box/LayeredZDepth"), true).toBool();
    m_unifiedActiveDragPulse = settings.value(QStringLiteral("Box/ActiveDragPulse"), true).toBool();
}

BoxManager::~BoxManager() {
    if (m_activeSaveFuture.isRunning()) {
        m_activeSaveFuture.waitForFinished();
    }
}

void BoxManager::setPersistencePath(const QString& path) {
    m_persistencePath = path;
}

void BoxManager::applyUnifiedStyleToBox(LinBox* box) {
    if (!box) return;

    QColor targetColor = m_unifiedBackgroundColor;
    float targetOpacity = m_unifiedBackgroundOpacity;
    bool targetGlass = m_unifiedGlassEffect;
    int targetShowType = 0; // Icon_ShowType

    // 优先从现有存量 box 查找有效外观配置
    for (const auto& ptr : m_boxes) {
        if (LinBox* existing = ptr.data()) {
            if (existing != box) {
                if (existing->backgroundColor().isValid() && (!targetColor.isValid() || targetColor == QColor(40, 40, 40))) {
                    targetColor = existing->backgroundColor();
                }
                if (existing->backgroundOpacity() > 0.01f && targetOpacity <= 0.01f) {
                    targetOpacity = existing->backgroundOpacity();
                }
                targetShowType = static_cast<int>(existing->showType());
                targetGlass = existing->glassEffect();
                break;
            }
        }
    }

    if (!targetColor.isValid()) {
        targetColor = QColor("#63A8F5");
    }
    if (targetOpacity < 0.01f) {
        targetOpacity = 0.28f;
    }

    m_unifiedBackgroundColor = targetColor;
    m_unifiedBackgroundOpacity = targetOpacity;
    m_unifiedGlassEffect = targetGlass;

    box->setShowType(targetShowType == 1 ? LinBox::BoxShowType::List_ShowType : LinBox::BoxShowType::Icon_ShowType);
    box->setBackgroundColor(targetColor);
    box->setBackgroundOpacity(targetOpacity);
    box->setGlassEffect(targetGlass);
    box->setLayeredZDepth(m_unifiedLayeredZDepth);
    box->setActiveDragPulse(m_unifiedActiveDragPulse);
    box->refreshStyles();
}

LinBox* BoxManager::createBox(const QPoint& pos, QWidget* parent) {
    LinBox* box = new LinBox(parent);
    box->setBoxId(QUuid::createUuid().toString());

    const int defaultW = DpiScaleHelper::scalePx(parent ? parent : box, 300);
    const int defaultH = DpiScaleHelper::scalePx(parent ? parent : box, 400);
    box->setGeometry(pos.x(), pos.y(), defaultW, defaultH);

    applyUnifiedStyleToBox(box);
    
    setupBoxConnections(box);
    box->show();
    box->refreshLayout();
    box->raise();
    box->update();
    if (parent) {
        parent->update(box->geometry().adjusted(-16, -16, 16, 16));
    }
    
    m_boxes.append(QPointer<LinBox>(box));
    m_boxIdIndex.insert(box->boxId(), box);
    requestImmediateSave();
    
    emit boxCreated(box);
    return box;
}

void BoxManager::destroyBox(LinBox* box, ListView* listView) {
    if (!box) return;
    Q_UNUSED(listView);

    // 通知外部处理（恢复图标到桌面等）
    emit boxDestroyed(box);
    if (LinDirectoryMonitor* monitor = box->mappedFolderMonitor()) {
        monitor->stopListening();
        box->setMappedFolderMonitor(nullptr);
    }
    
    m_boxIdIndex.remove(box->boxId());
    m_boxes.removeAll(QPointer<LinBox>(box));
    box->hide();
    box->setParent(nullptr);
    box->deleteLater();
    
    requestImmediateSave();
}

void BoxManager::setupConnections(LinBox* box, ListView* listView) {
    if (!box) return;
    
    setupBoxConnections(box);
    
    if (listView) {
        // 拖放与菜单操作连接
        connect(box, &LinBox::destroyRequested, listView, &ListView::onMenuDestroyBox, Qt::UniqueConnection);
        connect(box, &LinBox::customRequested, listView, &ListView::onMenuCustomBox, Qt::UniqueConnection);
    }
}

void BoxManager::setupBoxConnections(LinBox* box) {
    if (!box) return;
    
    // 布局几何变化走 150ms 节流保存
    connect(box, &LinBox::geometryChanged, this, [this]() {
        if (m_saveTimer) {
            m_saveTimer->start();
        }
    }, Qt::UniqueConnection);
}

void BoxManager::addBox(LinBox* box) {
    if (!box) return;
    for (auto& ptr : m_boxes) {
        if (ptr == box) return;  // Already exists
    }
    m_boxes.append(QPointer<LinBox>(box));
    m_boxIdIndex.insert(box->boxId(), box);
    setupBoxConnections(box);
}

void BoxManager::removeBox(LinBox* box) {
    if (box) {
        m_boxIdIndex.remove(box->boxId());
        m_boxes.removeAll(QPointer<LinBox>(box));
    }
}

void BoxManager::clearBoxes() {
    m_boxIdIndex.clear();
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
    if (boxId.isEmpty()) return nullptr;
    auto it = m_boxIdIndex.find(boxId);
    if (it != m_boxIdIndex.end()) {
        if (LinBox* box = it.value().data()) {
            return box;
        }
    }
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

void BoxManager::requestSave() {
    // 节流请求保存
    if (m_saveTimer) {
        m_saveTimer->start();
    } else {
        emit saveRequested();
    }
}

void BoxManager::requestImmediateSave() {
    // 显式立即保存（低频操作如新建、销毁、净化）
    if (m_saveTimer && m_saveTimer->isActive()) {
        m_saveTimer->stop();
    }
    emit saveRequested();
}

void BoxManager::saveAll() {
    saveAll(m_persistencePath, false, QMap<QString, QMap<QString, QVariant>>(),
            1, 0, 0, 0, false, false, true, false, true, true);
}

bool BoxManager::validateBoxData(const QJsonObject& b, QSet<QString>& seenBoxIds, QMap<QString, QVariant>& outData, int& droppedCount)
{
    QString id = b.value(QStringLiteral("id")).toString().trimmed();
    if (id.isEmpty()) {
        qWarning() << "[BoxManager] Validation failed: box has empty id";
        ++droppedCount;
        return false;
    }

    if (seenBoxIds.contains(id)) {
        qWarning() << "[BoxManager] Validation failed: duplicate box id:" << id;
        ++droppedCount;
        return false;
    }

    int w = b.value(QStringLiteral("w")).toInt();
    int h = b.value(QStringLiteral("h")).toInt();
    if (w <= 0 || h <= 0) {
        qWarning() << "[BoxManager] Validation failed: non-positive box dimensions (w:" << w << "h:" << h << ") id:" << id;
        ++droppedCount;
        return false;
    }

    QString colorStr = b.value(QStringLiteral("color")).toString().trimmed();
    if (!colorStr.isEmpty() && !QColor(colorStr).isValid()) {
        qWarning() << "[BoxManager] Sanitizing invalid color" << colorStr << "for box:" << id;
        colorStr = QStringLiteral("#2b2b2b");
        ++droppedCount;
    }

    int showType = b.value(QStringLiteral("showType")).toInt();
    if (showType < 0 || showType > 2) {
        qWarning() << "[BoxManager] Sanitizing out-of-range showType" << showType << "for box:" << id;
        showType = qBound(0, showType, 2);
        ++droppedCount;
    }

    seenBoxIds.insert(id);

    outData["id"] = id;
    outData["title"] = b.value(QStringLiteral("title")).toString();
    outData["x"] = b.value(QStringLiteral("x")).toInt();
    outData["y"] = b.value(QStringLiteral("y")).toInt();
    outData["w"] = w;
    outData["h"] = h;
    outData["collapsed"] = b.value(QStringLiteral("collapsed")).toBool();
    outData["showType"] = showType;
    outData["color"] = colorStr.isEmpty() ? QStringLiteral("#2b2b2b") : colorStr;
    outData["opacity"] = b.value(QStringLiteral("opacity")).toDouble();
    outData["glass"] = b.value(QStringLiteral("glass")).toBool(false);
    outData["mappedFolderPath"] = b.value(QStringLiteral("mappedFolderPath")).toString();

    QStringList files;
    QSet<QString> seenFiles;
    QJsonArray fArr = b.value(QStringLiteral("files")).toArray();
    for (int j = 0; j < fArr.size(); ++j) {
        const QString fPath = fArr[j].toString().trimmed();
        if (fPath.isEmpty()) {
            continue;
        }
        if (seenFiles.contains(fPath)) {
            ++droppedCount;
            continue;
        }
        seenFiles.insert(fPath);
        files << fPath;
    }
    outData["files"] = files;
    return true;
}

bool BoxManager::validateIconPosition(const QJsonObject& posObj, const QSet<QString>& validBoxIds, QString& outPath, QMap<QString, QVariant>& outPos, int& droppedCount)
{
    outPath = posObj.value(QStringLiteral("path")).toString().trimmed();
    if (outPath.isEmpty()) {
        qWarning() << "[BoxManager] Validation failed: icon position has empty path";
        ++droppedCount;
        return false;
    }

    QString boxId = posObj.value(QStringLiteral("boxId")).toString().trimmed();
    if (!boxId.isEmpty() && !validBoxIds.contains(boxId)) {
        qWarning() << "[BoxManager] Sanitizing dangling boxId" << boxId << "for icon:" << outPath;
        boxId.clear();
        ++droppedCount;
    }

    outPos["x"] = posObj.value(QStringLiteral("x")).toInt();
    outPos["y"] = posObj.value(QStringLiteral("y")).toInt();
    outPos["boxId"] = boxId;
    outPos["ord"] = posObj.value(QStringLiteral("ord")).toInt();
    return true;
}

QByteArray BoxManager::encodePersistenceData(const QByteArray& jsonData) {
    if (jsonData.isEmpty()) {
        return QByteArray();
    }

    // 1. zlib 压缩
    QByteArray compressed = qCompress(jsonData, 9);
    // 2. 循环异或混淆
    for (int i = 0; i < compressed.size(); ++i) {
        compressed[i] = static_cast<char>(static_cast<quint8>(compressed[i]) ^ kXorMask[i % sizeof(kXorMask)]);
    }
    // 3. 添加包头 (Magic 4B + Version 1B + Flags 1B)
    QByteArray result;
    result.reserve(6 + compressed.size());
    result.append(kLinDeskMagic, 4);
    result.append(static_cast<char>(kLinDeskVersion));
    result.append(static_cast<char>(kLinDeskFlagEncrypted));
    result.append(compressed);
    return result;
}

QByteArray BoxManager::decodePersistenceData(const QByteArray& rawData) {
    if (rawData.isEmpty()) {
        return QByteArray();
    }

    // Tier 1: 检查 LinDesk 二进制加密包头 (LDBX)
    if (rawData.size() >= 6 &&
        memcmp(rawData.constData(), kLinDeskMagic, 4) == 0 &&
        static_cast<quint8>(rawData[4]) == kLinDeskVersion) {

        const quint8 flags = static_cast<quint8>(rawData[5]);
        QByteArray payload = rawData.mid(6);
        if (flags & kLinDeskFlagEncrypted) {
            for (int i = 0; i < payload.size(); ++i) {
                payload[i] = static_cast<char>(static_cast<quint8>(payload[i]) ^ kXorMask[i % sizeof(kXorMask)]);
            }
        }
        QByteArray uncompressed = qUncompress(payload);
        if (!uncompressed.isEmpty()) {
            return uncompressed;
        }
    }

    // Tier 2: 尝试直接纯 qCompress 压缩流
    QByteArray uncompressed = qUncompress(rawData);
    if (!uncompressed.isEmpty()) {
        return uncompressed;
    }

    // Tier 3: 原始明文（回落到后续的 JSON / QDataStream 解析）
    return rawData;
}

QFuture<bool> BoxManager::saveAll(const QString& persistencePath, bool enablePersistence,
                                  const QMap<QString, QMap<QString, QVariant>>& iconPositions,
                                  int viewMode, int sortType, int preAutoSort, int iconPosMode,
                                  bool autoArrange, bool gridAlignment,
                                  bool showDesktopIcons, bool doubleClickHideIcons,
                                  bool showShortCutArrow,
                                  bool async) {
    if (persistencePath.isEmpty()) {
        qWarning() << "[BoxManager] Cannot save to empty persistence path";
        emit saveFinished(false);
        return QtConcurrent::run([]() { return false; });
    }

    // 主线程收集 QWidget / LinBox 状态快照
    QVector<PersistedBoxSnapshot> snapshots;
    snapshots.reserve(m_boxes.size());
    for (auto& ptr : m_boxes) {
        LinBox* box = ptr.data();
        if (!box) continue;
        PersistedBoxSnapshot s;
        s.id = box->boxId();
        s.title = box->boxTitle();
        s.x = box->x();
        s.y = box->y();
        s.w = box->width();
        s.h = box->isCollapsed() ? box->originalHeight() : box->height();
        s.collapsed = box->isCollapsed();
        s.showType = (int)box->showType();
        s.color = box->backgroundColor().name();
        s.opacity = qRound(box->backgroundOpacity() * 100);
        s.glass = box->glassEffect();
        s.mappedFolderPath = box->mappedFolderPath();
        
        QSet<QString> seenPaths;
        for (const QString& filePath : box->itemPaths()) {
            if (filePath.isEmpty() || seenPaths.contains(filePath)) continue;
            seenPaths.insert(filePath);
            s.files.append(filePath);
        }
        snapshots.append(s);
    }

    auto performSave = [persistencePath, enablePersistence, iconPositions,
                        viewMode, sortType, preAutoSort, iconPosMode,
                        autoArrange, gridAlignment, showDesktopIcons,
                        doubleClickHideIcons, showShortCutArrow,
                        snapshots]() -> bool {
        const QString tmpPath = persistencePath + QStringLiteral(".tmp");
        const QString bakPath = persistencePath + QStringLiteral(".bak");

        QFileInfo fi(persistencePath);
        QDir().mkpath(fi.absolutePath());

        QJsonObject rootObj;
        QJsonObject settingsObj;
        settingsObj["viewMode"] = viewMode;
        settingsObj["sortType"] = sortType;
        settingsObj["preAutoSort"] = preAutoSort;
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
            if (it.key().trimmed().isEmpty()) continue;
            QJsonObject posObj;
            posObj["path"] = it.key();
            for (auto pit = it.value().begin(); pit != it.value().end(); ++pit) {
                posObj[pit.key()] = QJsonValue::fromVariant(pit.value());
            }
            iconPosArray.append(posObj);
        }
        rootObj["iconPositions"] = iconPosArray;

        QJsonArray boxArray;
        for (const auto& s : snapshots) {
            QJsonObject b;
            b["id"] = s.id;
            b["title"] = s.title;
            b["x"] = s.x;
            b["y"] = s.y;
            b["w"] = s.w;
            b["h"] = s.h;
            b["collapsed"] = s.collapsed;
            b["showType"] = s.showType;
            b["color"] = s.color;
            b["opacity"] = s.opacity;
            b["glass"] = s.glass;
            b["mappedFolderPath"] = s.mappedFolderPath;
            
            QJsonArray fArr;
            for (const QString& f : s.files) {
                fArr.append(f);
            }
            b["files"] = fArr;
            boxArray.append(b);
        }
        rootObj["box-datas"] = boxArray;

        QFile tmpFile(tmpPath);
        if (!tmpFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qWarning() << "[BoxManager] Failed to open temp file for atomic writing:" << tmpPath;
            return false;
        }

        const QByteArray jsonData = QJsonDocument(rootObj).toJson(QJsonDocument::Compact);
        const QByteArray binaryPayload = encodePersistenceData(jsonData);
        if (tmpFile.write(binaryPayload) != binaryPayload.size()) {
            qWarning() << "[BoxManager] Failed to write complete binary data to:" << tmpPath;
            tmpFile.close();
            QFile::remove(tmpPath);
            return false;
        }

        tmpFile.flush();
        tmpFile.close();

#ifdef QT_DEBUG
        // Debug 模式下同步写出一份格式化明文 JSON 伴随文件，方便开发人员直接查看与调试
        const QString debugJsonPath = persistencePath + QStringLiteral(".debug.json");
        QFile debugFile(debugJsonPath);
        if (debugFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            debugFile.write(QJsonDocument(rootObj).toJson(QJsonDocument::Indented));
            debugFile.close();
            qDebug() << "[BoxManager] [DEBUG] Plaintext JSON snapshot updated at:" << debugJsonPath;
        }
#endif

        bool replaced = false;
#ifdef Q_OS_WIN
        const std::wstring wTmp = QDir::toNativeSeparators(tmpPath).toStdWString();
        const std::wstring wTarget = QDir::toNativeSeparators(persistencePath).toStdWString();
        if (MoveFileExW(wTmp.c_str(), wTarget.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            replaced = true;
        }
#endif
        if (!replaced) {
            if (QFile::exists(persistencePath)) {
                QFile::remove(persistencePath);
            }
            replaced = QFile::rename(tmpPath, persistencePath);
            if (!replaced) {
                if (QFile::copy(tmpPath, persistencePath)) {
                    QFile::remove(tmpPath);
                    replaced = true;
                }
            }
        }

        if (QFile::exists(tmpPath)) {
            QFile::remove(tmpPath);
        }

        if (!replaced) {
            qWarning() << "[BoxManager] Atomic commit failed to replace" << tmpPath << "with" << persistencePath;
            return false;
        }

        return true;
    };

    if (async) {
        if (m_activeSaveFuture.isRunning()) {
            m_activeSaveFuture.waitForFinished();
        }
        QPointer<BoxManager> weakThis(this);
        m_activeSaveFuture = QtConcurrent::run([weakThis, performSave]() -> bool {
            const bool ok = performSave();
            if (weakThis && !QCoreApplication::closingDown()) {
                QMetaObject::invokeMethod(weakThis.data(), [weakThis, ok]() {
                    if (weakThis && !QCoreApplication::closingDown()) {
                        emit weakThis->saveFinished(ok);
                    }
                }, Qt::QueuedConnection);
            }
            return ok;
        });
        return m_activeSaveFuture;
    }

    if (m_activeSaveFuture.isRunning()) {
        m_activeSaveFuture.waitForFinished();
    }
    const bool ok = performSave();
    if (!QCoreApplication::closingDown()) {
        emit saveFinished(ok);
    }
    m_activeSaveFuture = QFuture<bool>();
    return m_activeSaveFuture;
}

void BoxManager::loadAll(const QString& persistencePath, bool autoUpdateState,
                         bool& enablePersistence, QMap<QString, QString>& iconShowRules,
                         int& viewMode, int& sortType, int& preAutoSort, int& iconPosMode,
                         bool& autoArrange, bool& gridAlignment,
                         bool& showDesktopIcons, bool& doubleClickHideIcons,
                         bool& showShortCutArrow,
                         QVector<QMap<QString, QVariant>>& boxData,
                         QMap<QString, QMap<QString, QVariant>>& iconPositions,
                         int* droppedCount) {
    if (droppedCount) {
        *droppedCount = 0;
    }

    const QString tmpPath = persistencePath + QStringLiteral(".tmp");
    if (QFile::exists(tmpPath)) {
        QFile::remove(tmpPath);
    }

    auto tryLoadFile = [&](const QString& filePath, int& localDropped) -> bool {
        QFile file(filePath);
        if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
            return false;
        }

        const QByteArray content = file.readAll();
        file.close();

        if (content.trimmed().isEmpty()) {
            qWarning() << "[BoxManager] File is empty:" << filePath;
            return false;
        }

        const QByteArray decodedContent = decodePersistenceData(content);

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(decodedContent, &parseError);
        if (parseError.error == QJsonParseError::NoError && !doc.isNull() && doc.isObject()) {
            QJsonObject rootObj = doc.object();
            if (rootObj.contains("settings") && rootObj.contains("box-datas") && rootObj.contains("iconPositions")) {
                // 加载设置
                QJsonObject settingsObj = rootObj["settings"].toObject();
                enablePersistence = settingsObj.value("enablePersistence").toBool(true);
                viewMode = settingsObj.value("viewMode").toInt(1);
                sortType = settingsObj.value("sortType").toInt(0);
                preAutoSort = settingsObj.value("preAutoSort").toInt(0);
                iconPosMode = settingsObj.value("iconPosMode").toInt(0);
                autoArrange = settingsObj.value("autoArrange").toBool(false);
                gridAlignment = settingsObj.value("gridAlignment").toBool(false);
                showDesktopIcons = settingsObj.value("showDesktopIcons").toBool(true);
                doubleClickHideIcons = settingsObj.value("doubleClickHideIcons").toBool(false);
                showShortCutArrow = settingsObj.value("showShortCutArrow").toBool(true);

                // 加载 Box 并做条目级校验与上限控制
                QJsonArray boxArray = rootObj["box-datas"].toArray();
                if (boxArray.size() > kMaxPersistedBoxes) {
                    qWarning() << "[BoxManager] Persisted box count exceeds max limit:" << boxArray.size() << ">" << kMaxPersistedBoxes;
                    localDropped += (boxArray.size() - kMaxPersistedBoxes);
                }
                const int boxLimit = qMin(boxArray.size(), kMaxPersistedBoxes);

                QSet<QString> seenBoxIds;
                boxData.clear();
                for (int i = 0; i < boxLimit; ++i) {
                    const QJsonValue val = boxArray[i];
                    if (!val.isObject()) {
                        qWarning() << "[BoxManager] Dropping non-object box at index:" << i;
                        ++localDropped;
                        continue;
                    }
                    QMap<QString, QVariant> bData;
                    if (validateBoxData(val.toObject(), seenBoxIds, bData, localDropped)) {
                        boxData.append(bData);
                    }
                }

                // 加载图标位置并做条目级校验与上限控制
                QJsonArray iconPosArray = rootObj["iconPositions"].toArray();
                if (iconPosArray.size() > kMaxPersistedIconPositions) {
                    qWarning() << "[BoxManager] Persisted icon count exceeds limit:" << iconPosArray.size() << ">" << kMaxPersistedIconPositions;
                    localDropped += (iconPosArray.size() - kMaxPersistedIconPositions);
                }
                const int iconLimit = qMin(iconPosArray.size(), kMaxPersistedIconPositions);

                iconPositions.clear();
                for (int i = 0; i < iconLimit; ++i) {
                    const QJsonValue val = iconPosArray[i];
                    if (!val.isObject()) {
                        qWarning() << "[BoxManager] Dropping non-object icon position at index:" << i;
                        ++localDropped;
                        continue;
                    }
                    QString path;
                    QMap<QString, QVariant> posMap;
                    if (validateIconPosition(val.toObject(), seenBoxIds, path, posMap, localDropped)) {
                        iconPositions[path] = posMap;
                    }
                }
                return true;
            } else {
                qWarning() << "[BoxManager] Missing required keys in JSON" << filePath << "(needs settings, box-datas, iconPositions)";
                return false;
            }
        } else {
            qWarning() << "[BoxManager] Persistence parse error in" << filePath << ":" << parseError.errorString();
            return false;
        }
    };

    int totalDropped = 0;
    bool loaded = false;

    if (QFile::exists(persistencePath)) {
        loaded = tryLoadFile(persistencePath, totalDropped);
    }

    if (!loaded) {
        // 绝不抹除用户坏文件：如果主文件存在且非空，但解析失败，立即自动重命名隔离为 .corrupted_yyyyMMdd_hhmmss 保留案发现场！
        if (QFile::exists(persistencePath) && QFile(persistencePath).size() > 0) {
            const QString timeStamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss"));
            const QString quarantinePath = persistencePath + QStringLiteral(".corrupted_") + timeStamp;

            bool quarantined = QFile::rename(persistencePath, quarantinePath);
            if (!quarantined) {
                if (QFile::copy(persistencePath, quarantinePath)) {
                    QFile::remove(persistencePath);
                    quarantined = true;
                }
            }
            qCritical() << "[BoxManager] CRITICAL: Persistence file corrupted! Quarantined to:" << quarantinePath
                        << "(Original user file preserved. Launching with clean layout without overwriting corrupted data)";
        } else {
            qDebug() << "[BoxManager] No persistence file found at:" << persistencePath;
        }

        qWarning() << "[BoxManager] Starting with clean default layout.";
        boxData.clear();
        iconPositions.clear();
        totalDropped = 0; // 严禁设置 dirty，防止启动立即写空文件破坏现场！
    }

    if (droppedCount) {
        *droppedCount = totalDropped;
    }

    for (const auto& bMap : boxData) {
        if (bMap.contains("color")) {
            const QColor c = QColor(bMap.value("color").toString());
            if (c.isValid()) {
                m_unifiedBackgroundColor = c;
            }
        }
        if (bMap.contains("opacity")) {
            const double op = bMap.value("opacity").toDouble();
            const float parsedOpacity = static_cast<float>(op > 1.0 ? qBound(0.0, op / 100.0, 1.0) : qBound(0.0, op, 1.0));
            if (parsedOpacity > 0.01f) {
                m_unifiedBackgroundOpacity = parsedOpacity;
            }
        }
        if (bMap.contains("glass")) {
            m_unifiedGlassEffect = bMap.value("glass").toBool();
        }
        if (m_unifiedBackgroundColor.isValid() && m_unifiedBackgroundOpacity > 0.01f) {
            break;
        }
    }

    qDebug() << "[BoxManager] Loaded" << boxData.size() << "boxes and" << iconPositions.size()
             << "icon positions (sanitized/dropped:" << totalDropped << ") from:" << persistencePath;
}

void BoxManager::loadAll(bool autoUpdateState) {
    bool enablePersistence = true;
    QMap<QString, QString> rules;
    int viewMode = 1, sortType = 0, preAutoSort = 0, iconPosMode = 0;
    bool autoArrange = false, gridAlignment = false, showDesktopIcons = true, doubleClickHideIcons = false, showShortCutArrow = true;
    QVector<QMap<QString, QVariant>> boxData;
    QMap<QString, QMap<QString, QVariant>> iconPositions;
    loadAll(m_persistencePath, autoUpdateState, enablePersistence, rules,
            viewMode, sortType, preAutoSort, iconPosMode,
            autoArrange, gridAlignment, showDesktopIcons, doubleClickHideIcons, showShortCutArrow,
            boxData, iconPositions);
}

QString BoxManager::getBoxIdForItem(const QString& filePath) const {
    if (filePath.isEmpty()) return QString();
    
    QString target = QDir::toNativeSeparators(filePath);
    while (target.endsWith('\\')) target.chop(1);
    
    int targetColonIndex = target.indexOf(QStringLiteral("::"));
    if (targetColonIndex > 0) {
        bool allSlashes = true;
        for (int i = 0; i < targetColonIndex; ++i) {
            if (target[i] != '\\' && target[i] != '/') {
                allSlashes = false;
                break;
            }
        }
        if (allSlashes) {
            target = target.mid(targetColonIndex);
        }
    }

    for (auto& ptr : m_boxes) {
        LinBox* box = ptr.data();
        if (!box) continue;
        for (const QString& filePathInBox : box->itemPaths()) {
            QString itemPath = QDir::toNativeSeparators(filePathInBox);
            while (itemPath.endsWith('\\')) itemPath.chop(1);
            
            int itemColonIndex = itemPath.indexOf(QStringLiteral("::"));
            if (itemColonIndex > 0) {
                bool allSlashes = true;
                for (int i = 0; i < itemColonIndex; ++i) {
                    if (itemPath[i] != '\\' && itemPath[i] != '/') {
                        allSlashes = false;
                        break;
                    }
                }
                if (allSlashes) {
                    itemPath = itemPath.mid(itemColonIndex);
                }
            }
            
            if (itemPath.compare(target, Qt::CaseInsensitive) == 0) {
                return box->boxId();
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
    m_unifiedGlassEffect = enabled;
    for (auto& ptr : m_boxes) {
        if (LinBox* box = ptr.data()) box->setGlassEffect(enabled);
    }
    requestSave();
}

void BoxManager::setAllBoxesBackgroundColor(const QColor& color) {
    m_unifiedBackgroundColor = color;
    for (auto& ptr : m_boxes) {
        if (LinBox* box = ptr.data()) box->setBackgroundColor(color);
    }
    requestSave();
}

void BoxManager::setAllBoxesBackgroundOpacity(float opacity) {
    m_unifiedBackgroundOpacity = opacity;
    for (auto& ptr : m_boxes) {
        if (LinBox* box = ptr.data()) box->setBackgroundOpacity(opacity);
    }
    requestSave();
}

void BoxManager::setAllBoxesLayeredZDepth(bool enabled) {
    m_unifiedLayeredZDepth = enabled;
    for (auto& ptr : m_boxes) {
        if (LinBox* box = ptr.data()) box->setLayeredZDepth(enabled);
    }
}

void BoxManager::setAllBoxesActiveDragPulse(bool enabled) {
    m_unifiedActiveDragPulse = enabled;
    for (auto& ptr : m_boxes) {
        if (LinBox* box = ptr.data()) box->setActiveDragPulse(enabled);
    }
}
