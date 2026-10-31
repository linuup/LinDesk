#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>

#include "box/BoxManager.h"
#include "box/LinBox.h"
#include "monitor/LinDirectoryMonitor.h"

LinDirectoryMonitor::LinDirectoryMonitor(const QString& path, bool, QObject* parent) : IDirectoryMonitorListener(path, parent) {}
LinDirectoryMonitor::~LinDirectoryMonitor() {}
bool LinDirectoryMonitor::startListening() { return true; }
void LinDirectoryMonitor::stopListening() {}
FileSystemEvent LinDirectoryMonitor::fromWinEvent(DWORD) { return FileSystemEvent::FileUpdated; }
QString LinDirectoryMonitor::eventToString(FileSystemEvent) { return QString(); }

// Stubs for LinBox to allow standalone testing of BoxManager without Qt GUI widgets
LinBox::LinBox(QWidget* parent) : QWidget(parent) {}
LinBox::~LinBox() {}
void LinBox::setShowType(BoxShowType) {}
void LinBox::addItemState(const DesktopRootItemState&, bool) {}
void LinBox::insertItemState(const DesktopRootItemState&, int, bool) {}
bool LinBox::removeItemState(const QString&, bool) { return true; }
bool LinBox::takeItemState(const QString&, DesktopRootItemState&, bool) { return true; }
void LinBox::setBoxTitle(const QString& t) { m_title = t; }
void LinBox::setResizeBorderWidth(int) {}
void LinBox::toggleCollapse() {}
void LinBox::refreshLayout() {}
QRect LinBox::globalRenameEditorRectForPath(const QString&) const { return QRect(); }
void LinBox::previewGhostDropAt(const QPoint&, const QStringList&, Qt::KeyboardModifiers) {}
void LinBox::clearDropPreview() {}
void LinBox::setMappedFolderMonitor(LinDirectoryMonitor*) {}
void LinBox::setBackgroundColor(const QColor& c) { m_bgColor = c; }
void LinBox::setBackgroundOpacity(float op) { m_bgOpacity = op; }
void LinBox::setGlassEffect(bool g) { m_glassEffect = g; }
void LinBox::setLayeredZDepth(bool z) { m_layeredZDepth = z; }
void LinBox::setAdaptiveWallpaperTint(bool a) { m_adaptiveWallpaperTint = a; }
void LinBox::setActiveDragPulse(bool p) { m_activeDragPulse = p; }
void LinBox::showEvent(QShowEvent*) {}
bool LinBox::eventFilter(QObject*, QEvent*) { return false; }
void LinBox::mousePressEvent(QMouseEvent*) {}
void LinBox::mouseMoveEvent(QMouseEvent*) {}
void LinBox::mouseReleaseEvent(QMouseEvent*) {}
void LinBox::paintEvent(QPaintEvent*) {}
void LinBox::moveEvent(QMoveEvent*) {}
void LinBox::resizeEvent(QResizeEvent*) {}
void LinBox::enterEvent(QEvent*) {}
void LinBox::leaveEvent(QEvent*) {}
bool LinBox::nativeEvent(const QByteArray&, void*, long*) { return false; }
void LinBox::dragEnterEvent(QDragEnterEvent*) {}
void LinBox::dragMoveEvent(QDragMoveEvent*) {}
void LinBox::dragLeaveEvent(QDragLeaveEvent*) {}
void LinBox::dropEvent(QDropEvent*) {}
void LinBox::keyPressEvent(QKeyEvent*) {}
void LinBox::focusInEvent(QFocusEvent*) {}
QVector<QString> LinBox::itemPaths() const { return {}; }
void LinBox::refreshStyles() {}

#include "desktop/ListView.h"
void ListView::onMenuDestroyBox(LinBox*) {}
void ListView::onMenuCustomBox(LinBox*) {}

class PersistenceTests : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tempDir.isValid());
    }

    QTemporaryDir m_tempDir;
};

TEST_F(PersistenceTests, BoxDataValidationGood) {
    QSet<QString> seenIds;
    QMap<QString, QVariant> outData;
    int droppedCount = 0;

    QJsonObject obj;
    obj["id"] = QStringLiteral("box-1");
    obj["title"] = QStringLiteral("My Box");
    obj["x"] = 100;
    obj["y"] = 200;
    obj["w"] = 300;
    obj["h"] = 400;
    obj["collapsed"] = false;
    obj["showType"] = 1;
    obj["color"] = QStringLiteral("#334455");
    obj["opacity"] = 0.8;
    obj["glass"] = true;
    QJsonArray files;
    files.append(QStringLiteral("C:/test/file1.txt"));
    files.append(QStringLiteral("C:/test/file2.txt"));
    obj["files"] = files;

    bool ok = BoxManager::validateBoxData(obj, seenIds, outData, droppedCount);
    EXPECT_TRUE(ok);
    EXPECT_EQ(droppedCount, 0);
    EXPECT_EQ(outData.value("id").toString(), QStringLiteral("box-1"));
    EXPECT_EQ(outData.value("w").toInt(), 300);
    EXPECT_EQ(outData.value("h").toInt(), 400);
    EXPECT_EQ(outData.value("showType").toInt(), 1);
    EXPECT_EQ(outData.value("files").toStringList().size(), 2);
}

TEST_F(PersistenceTests, BoxDataValidationBadEntries) {
    QSet<QString> seenIds;
    int droppedCount = 0;

    // 1. Empty ID
    {
        QJsonObject obj;
        obj["id"] = QStringLiteral("");
        obj["w"] = 100;
        obj["h"] = 100;
        QMap<QString, QVariant> outData;
        EXPECT_FALSE(BoxManager::validateBoxData(obj, seenIds, outData, droppedCount));
        EXPECT_EQ(droppedCount, 1);
    }

    // 2. Non-positive dimensions
    {
        QJsonObject obj;
        obj["id"] = QStringLiteral("box-zero-dim");
        obj["w"] = 0;
        obj["h"] = 100;
        QMap<QString, QVariant> outData;
        EXPECT_FALSE(BoxManager::validateBoxData(obj, seenIds, outData, droppedCount));
        EXPECT_EQ(droppedCount, 2);
    }

    // 3. Duplicate ID
    {
        QJsonObject obj1;
        obj1["id"] = QStringLiteral("box-dup");
        obj1["w"] = 100;
        obj1["h"] = 100;
        QMap<QString, QVariant> outData1;
        EXPECT_TRUE(BoxManager::validateBoxData(obj1, seenIds, outData1, droppedCount));

        QJsonObject obj2;
        obj2["id"] = QStringLiteral("box-dup");
        obj2["w"] = 100;
        obj2["h"] = 100;
        QMap<QString, QVariant> outData2;
        EXPECT_FALSE(BoxManager::validateBoxData(obj2, seenIds, outData2, droppedCount));
        EXPECT_EQ(droppedCount, 3);
    }

    // 4. Invalid color string sanitized
    {
        QJsonObject obj;
        obj["id"] = QStringLiteral("box-bad-color");
        obj["w"] = 100;
        obj["h"] = 100;
        obj["color"] = QStringLiteral("not_a_color_!@#$");
        QMap<QString, QVariant> outData;
        EXPECT_TRUE(BoxManager::validateBoxData(obj, seenIds, outData, droppedCount));
        EXPECT_EQ(outData.value("color").toString(), QStringLiteral("#2b2b2b"));
        EXPECT_EQ(droppedCount, 4);
    }

    // 5. Out-of-range showType sanitized
    {
        QJsonObject obj;
        obj["id"] = QStringLiteral("box-bad-showtype");
        obj["w"] = 100;
        obj["h"] = 100;
        obj["showType"] = 999;
        QMap<QString, QVariant> outData;
        EXPECT_TRUE(BoxManager::validateBoxData(obj, seenIds, outData, droppedCount));
        EXPECT_EQ(outData.value("showType").toInt(), 2);
        EXPECT_EQ(droppedCount, 5);
    }

    // 6. Duplicate files in array deduplicated
    {
        QJsonObject obj;
        obj["id"] = QStringLiteral("box-dup-files");
        obj["w"] = 100;
        obj["h"] = 100;
        QJsonArray files;
        files.append(QStringLiteral("C:/a.txt"));
        files.append(QStringLiteral("C:/a.txt"));
        obj["files"] = files;
        QMap<QString, QVariant> outData;
        EXPECT_TRUE(BoxManager::validateBoxData(obj, seenIds, outData, droppedCount));
        EXPECT_EQ(outData.value("files").toStringList().size(), 1);
        EXPECT_EQ(droppedCount, 6);
    }
}

TEST_F(PersistenceTests, IconPositionValidation) {
    QSet<QString> validBoxIds = { QStringLiteral("box-1") };
    int droppedCount = 0;

    // 1. Valid icon position with valid boxId
    {
        QJsonObject obj;
        obj["path"] = QStringLiteral("C:/Desktop/test.lnk");
        obj["x"] = 50;
        obj["y"] = 60;
        obj["boxId"] = QStringLiteral("box-1");
        obj["ord"] = 1;
        QString path;
        QMap<QString, QVariant> pos;
        EXPECT_TRUE(BoxManager::validateIconPosition(obj, validBoxIds, path, pos, droppedCount));
        EXPECT_EQ(path, QStringLiteral("C:/Desktop/test.lnk"));
        EXPECT_EQ(pos.value("boxId").toString(), QStringLiteral("box-1"));
        EXPECT_EQ(pos.value("x").toInt(), 50);
        EXPECT_EQ(droppedCount, 0);
    }

    // 2. Empty path -> dropped
    {
        QJsonObject obj;
        obj["path"] = QStringLiteral("   ");
        QString path;
        QMap<QString, QVariant> pos;
        EXPECT_FALSE(BoxManager::validateIconPosition(obj, validBoxIds, path, pos, droppedCount));
        EXPECT_EQ(droppedCount, 1);
    }

    // 3. Dangling boxId -> boxId cleared, coordinates preserved
    {
        QJsonObject obj;
        obj["path"] = QStringLiteral("C:/Desktop/orphan.lnk");
        obj["x"] = 150;
        obj["y"] = 250;
        obj["boxId"] = QStringLiteral("non_existent_box");
        obj["ord"] = 2;
        QString path;
        QMap<QString, QVariant> pos;
        EXPECT_TRUE(BoxManager::validateIconPosition(obj, validBoxIds, path, pos, droppedCount));
        EXPECT_EQ(path, QStringLiteral("C:/Desktop/orphan.lnk"));
        EXPECT_TRUE(pos.value("boxId").toString().isEmpty());
        EXPECT_EQ(pos.value("x").toInt(), 150);
        EXPECT_EQ(pos.value("y").toInt(), 250);
        EXPECT_EQ(droppedCount, 2);
    }
}

TEST_F(PersistenceTests, AtomicSaveAndDebugInspection) {
    const QString filePath = m_tempDir.filePath(QStringLiteral("data.dat"));
    const QString tmpPath = filePath + QStringLiteral(".tmp");

    QMap<QString, QMap<QString, QVariant>> iconPositions;
    QMap<QString, QVariant> pos;
    pos["x"] = 120;
    pos["y"] = 240;
    pos["boxId"] = QString();
    pos["ord"] = 0;
    iconPositions[QStringLiteral("C:/Desktop/file1.txt")] = pos;

    // 1. 保存：生成受保护二进制 data.dat，tmp 不残留
    BoxManager::instance()->saveAll(filePath, true, iconPositions, 1, 0, 0, 0, false, false, true, false, true, /*async=*/false);
    EXPECT_TRUE(QFile::exists(filePath));
    EXPECT_FALSE(QFile::exists(tmpPath));

#ifdef QT_DEBUG
    // 2. Debug 模式下验证自动伴随生成了明文可读的 .debug.json 方便开发者观察
    const QString debugPath = filePath + QStringLiteral(".debug.json");
    EXPECT_TRUE(QFile::exists(debugPath));
    QFile debugFile(debugPath);
    ASSERT_TRUE(debugFile.open(QIODevice::ReadOnly));
    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(debugFile.readAll(), &parseErr);
    debugFile.close();
    EXPECT_EQ(parseErr.error, QJsonParseError::NoError);
    EXPECT_TRUE(doc.isObject());
    EXPECT_TRUE(doc.object().contains("iconPositions"));
#endif

    // 3. 重新读取：验证受保护二进制被完整解码恢复
    bool enablePersistence = false;
    QMap<QString, QString> rules;
    int viewMode = 0, sortType = 0, preAuto = 0, iconMode = 0;
    bool autoArrange = false, gridAlign = false, showDesktop = false, doubleClick = false, arrow = false;
    QVector<QMap<QString, QVariant>> loadedBoxes;
    QMap<QString, QMap<QString, QVariant>> loadedPositions;
    int droppedCount = -1;

    BoxManager::instance()->loadAll(filePath, false, enablePersistence, rules, viewMode, sortType, preAuto, iconMode,
                                    autoArrange, gridAlign, showDesktop, doubleClick, arrow,
                                    loadedBoxes, loadedPositions, &droppedCount);

    EXPECT_EQ(droppedCount, 0);
    EXPECT_TRUE(loadedPositions.contains(QStringLiteral("C:/Desktop/file1.txt")));
    EXPECT_EQ(loadedPositions[QStringLiteral("C:/Desktop/file1.txt")].value("x").toInt(), 120);
}

TEST_F(PersistenceTests, CorruptedFileQuarantinedWithoutDataLoss) {
    const QString filePath = m_tempDir.filePath(QStringLiteral("user_damaged.dat"));

    // 写入无法解析的损坏数据
    {
        QFile file(filePath);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("GARBAGE_PAYLOAD_NOT_RECOGNIZED_BY_DECODER");
        file.close();
    }

    bool enablePersistence = false;
    QMap<QString, QString> rules;
    int viewMode = 0, sortType = 0, preAuto = 0, iconMode = 0;
    bool autoArrange = false, gridAlign = false, showDesktop = false, doubleClick = false, arrow = false;
    QVector<QMap<QString, QVariant>> loadedBoxes;
    QMap<QString, QMap<QString, QVariant>> loadedPositions;
    int droppedCount = -1;

    // 尝试加载损坏文件，验证绝不崩溃
    EXPECT_NO_THROW({
        BoxManager::instance()->loadAll(filePath, false, enablePersistence, rules, viewMode, sortType, preAuto, iconMode,
                                        autoArrange, gridAlign, showDesktop, doubleClick, arrow,
                                        loadedBoxes, loadedPositions, &droppedCount);
    });

    // 1. 验证内存回落为安全空桌面，不崩溃
    EXPECT_EQ(droppedCount, 0); // 绝不设 dirty 标记，防止启动后保存直接覆写原文件
    EXPECT_TRUE(loadedBoxes.isEmpty());
    EXPECT_TRUE(loadedPositions.isEmpty());

    // 2. 验证坏文件被安全隔离为 .corrupted_xxxx，原文件不存在（被挪走），案发现场保留
    EXPECT_FALSE(QFile::exists(filePath));
    QDir dir(m_tempDir.path());
    QStringList quarantined = dir.entryList(QStringList() << QStringLiteral("user_damaged.dat.corrupted_*"), QDir::Files);
    EXPECT_GE(quarantined.size(), 1);
}

TEST_F(PersistenceTests, PurgeAndCleanRewrite) {
    const QString filePath = m_tempDir.filePath(QStringLiteral("data.json"));

    // Write a handcrafted dirty JSON file with:
    // - 1 valid box
    // - 1 box with non-positive dimensions (should be dropped)
    // - 1 valid icon
    // - 1 icon with empty path (should be dropped)
    // - 1 icon with dangling boxId (should be sanitized to empty boxId)
    QJsonObject rootObj;
    QJsonObject settingsObj;
    settingsObj["viewMode"] = 1;
    rootObj["settings"] = settingsObj;

    QJsonArray boxArray;
    QJsonObject goodBox;
    goodBox["id"] = QStringLiteral("good-box");
    goodBox["w"] = 200;
    goodBox["h"] = 150;
    boxArray.append(goodBox);

    QJsonObject badBox;
    badBox["id"] = QStringLiteral("bad-box");
    badBox["w"] = 0; // Invalid!
    badBox["h"] = 100;
    boxArray.append(badBox);
    rootObj["box-datas"] = boxArray;

    QJsonArray iconArray;
    QJsonObject goodIcon;
    goodIcon["path"] = QStringLiteral("C:/Desktop/valid.txt");
    goodIcon["x"] = 10;
    goodIcon["y"] = 20;
    goodIcon["boxId"] = QStringLiteral("good-box");
    iconArray.append(goodIcon);

    QJsonObject emptyPathIcon;
    emptyPathIcon["path"] = QStringLiteral(""); // Invalid!
    emptyPathIcon["x"] = 30;
    emptyPathIcon["y"] = 40;
    iconArray.append(emptyPathIcon);

    QJsonObject danglingBoxIcon;
    danglingBoxIcon["path"] = QStringLiteral("C:/Desktop/dangling.txt");
    danglingBoxIcon["x"] = 50;
    danglingBoxIcon["y"] = 60;
    danglingBoxIcon["boxId"] = QStringLiteral("non_existent_box"); // Dangling!
    iconArray.append(danglingBoxIcon);
    rootObj["iconPositions"] = iconArray;

    {
        QFile file(filePath);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(rootObj).toJson());
        file.close();
    }

    // First load: droppedCount must be > 0
    bool enablePersistence = false;
    QMap<QString, QString> rules;
    int viewMode = 0, sortType = 0, preAuto = 0, iconMode = 0;
    bool autoArrange = false, gridAlign = false, showDesktop = false, doubleClick = false, arrow = false;
    QVector<QMap<QString, QVariant>> loadedBoxes;
    QMap<QString, QMap<QString, QVariant>> loadedPositions;
    int droppedCount = 0;

    BoxManager::instance()->loadAll(filePath, false, enablePersistence, rules, viewMode, sortType, preAuto, iconMode,
                                    autoArrange, gridAlign, showDesktop, doubleClick, arrow,
                                    loadedBoxes, loadedPositions, &droppedCount);

    EXPECT_GT(droppedCount, 0);
    EXPECT_EQ(loadedBoxes.size(), 1);
    EXPECT_EQ(loadedBoxes[0].value("id").toString(), QStringLiteral("good-box"));
    EXPECT_EQ(loadedPositions.size(), 2);
    EXPECT_TRUE(loadedPositions.contains(QStringLiteral("C:/Desktop/valid.txt")));
    EXPECT_TRUE(loadedPositions.contains(QStringLiteral("C:/Desktop/dangling.txt")));
    // Dangling boxId should have been cleaned to empty
    EXPECT_TRUE(loadedPositions[QStringLiteral("C:/Desktop/dangling.txt")].value("boxId").toString().isEmpty());

    // Clean save (simulating ListView::loadBoxes behavior where sanitized positions with cleared boxId are persisted)
    QMap<QString, QMap<QString, QVariant>> cleanPositions;
    // Keep dangling.txt which had its boxId sanitized to empty (desktop icon)
    cleanPositions[QStringLiteral("C:/Desktop/dangling.txt")] = loadedPositions[QStringLiteral("C:/Desktop/dangling.txt")];

    BoxManager::instance()->saveAll(filePath, enablePersistence, cleanPositions, viewMode, sortType, preAuto, iconMode,
                                    autoArrange, gridAlign, showDesktop, doubleClick, arrow, /*async=*/false);

    // Second load: file is now clean! droppedCount must be 0!
    QVector<QMap<QString, QVariant>> reloadedBoxes;
    QMap<QString, QMap<QString, QVariant>> reloadedPositions;
    int secondDroppedCount = -1;

    BoxManager::instance()->loadAll(filePath, false, enablePersistence, rules, viewMode, sortType, preAuto, iconMode,
                                    autoArrange, gridAlign, showDesktop, doubleClick, arrow,
                                    reloadedBoxes, reloadedPositions, &secondDroppedCount);

    EXPECT_EQ(secondDroppedCount, 0);
    EXPECT_EQ(reloadedPositions.size(), 1);
    EXPECT_TRUE(reloadedPositions.contains(QStringLiteral("C:/Desktop/dangling.txt")));
    EXPECT_TRUE(reloadedPositions[QStringLiteral("C:/Desktop/dangling.txt")].value("boxId").toString().isEmpty());
}

TEST_F(PersistenceTests, EncryptedBinaryPersistenceRoundtrip) {
    const QString filePath = m_tempDir.filePath(QStringLiteral("secured_data.dat"));

    // 1. 构造桌面与图标配置
    QMap<QString, QMap<QString, QVariant>> iconPositions;
    QMap<QString, QVariant> pos1;
    pos1["x"] = 50;
    pos1["y"] = 100;
    pos1["boxId"] = QString();
    pos1["ord"] = 0;
    iconPositions[QStringLiteral("C:/Desktop/app1.lnk")] = pos1;

    QMap<QString, QVariant> pos2;
    pos2["x"] = 200;
    pos2["y"] = 300;
    pos2["boxId"] = QString();
    pos2["ord"] = 1;
    iconPositions[QStringLiteral("C:/Desktop/document.pdf")] = pos2;

    // 2. 保存为受保护二进制
    BoxManager::instance()->saveAll(filePath, true, iconPositions,
                                    2, 1, 0, 0, false, true, true, false, true, /*async=*/false);

    // 3. 验证写出的文件是专有二进制格式（带有 LDBX 魔数，记事本等直接解析必报错）
    {
        QFile file(filePath);
        ASSERT_TRUE(file.open(QIODevice::ReadOnly));
        const QByteArray rawBytes = file.readAll();
        file.close();

        // 验证文件头
        ASSERT_GE(rawBytes.size(), 6);
        EXPECT_EQ(rawBytes.left(4), QByteArray("LDBX"));

        // 验证非明文（直接作为 JSON 解析报错，证明记事本直接打开无法查阅明文）
        QJsonParseError directErr;
        QJsonDocument::fromJson(rawBytes, &directErr);
        EXPECT_NE(directErr.error, QJsonParseError::NoError);

        // 验证通过 decodePersistenceData 解码后为合法 JSON
        const QByteArray decoded = BoxManager::decodePersistenceData(rawBytes);
        QJsonParseError parseErr;
        QJsonDocument doc = QJsonDocument::fromJson(decoded, &parseErr);
        EXPECT_EQ(parseErr.error, QJsonParseError::NoError);
        EXPECT_TRUE(doc.isObject());
        EXPECT_TRUE(doc.object().contains("settings"));
        EXPECT_TRUE(doc.object().contains("iconPositions"));
    }

    // 4. 重新加载，验证数据无损还原
    bool loadedEnablePersistence = false;
    QMap<QString, QString> rules;
    int loadedViewMode = 0, loadedSortType = 0, loadedPreAuto = 0, loadedIconMode = 0;
    bool loadedAutoArrange = false, loadedGridAlign = false, loadedShowDesktop = false;
    bool loadedDoubleClick = false, loadedArrow = false;
    QVector<QMap<QString, QVariant>> loadedBoxes;
    QMap<QString, QMap<QString, QVariant>> loadedPositions;
    int reloadedDroppedCount = -1;

    BoxManager::instance()->loadAll(filePath, false, loadedEnablePersistence, rules,
                                    loadedViewMode, loadedSortType, loadedPreAuto, loadedIconMode,
                                    loadedAutoArrange, loadedGridAlign, loadedShowDesktop,
                                    loadedDoubleClick, loadedArrow,
                                    loadedBoxes, loadedPositions, &reloadedDroppedCount);

    EXPECT_EQ(reloadedDroppedCount, 0);
    EXPECT_TRUE(loadedEnablePersistence);
    EXPECT_EQ(loadedViewMode, 2);
    EXPECT_EQ(loadedSortType, 1);
    EXPECT_TRUE(loadedGridAlign);
    EXPECT_EQ(loadedPositions.size(), 2);
    EXPECT_TRUE(loadedPositions.contains(QStringLiteral("C:/Desktop/app1.lnk")));
    EXPECT_EQ(loadedPositions[QStringLiteral("C:/Desktop/app1.lnk")].value("x").toInt(), 50);
    EXPECT_TRUE(loadedPositions.contains(QStringLiteral("C:/Desktop/document.pdf")));
    EXPECT_EQ(loadedPositions[QStringLiteral("C:/Desktop/document.pdf")].value("y").toInt(), 300);
}



