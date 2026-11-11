#include <gtest/gtest.h>
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <windows.h>
#include <commctrl.h>
#include <commoncontrols.h>
#include <shellapi.h>
#include <shlobj.h>
#include "services/SysInfoUtil.h"
#include "services/SystemApi/SystemApi.h"
#include "services/IconCacheManager.h"

class StoreAppIconCompatTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        if (!qApp) {
            static int argc = 1;
            static char appName[] = "StoreAppIconCompatTest";
            static char* argv[] = { appName, nullptr };
            new QGuiApplication(argc, argv);
        }
    }
};

TEST_F(StoreAppIconCompatTest, LoadShellItemIcon_InvalidPathReturnsNullSafe)
{
    // 测试无效路径安全容错：返回空 QIcon，不抛异常
    QIcon icon = SysInfoUtil::LoadShellItemIcon(QStringLiteral("C:/__non_existent_path_test_12345__.xyz"), 48);
    EXPECT_TRUE(icon.isNull());

    QIcon emptyIcon = SysInfoUtil::LoadShellItemIcon(QString(), 48);
    EXPECT_TRUE(emptyIcon.isNull());
}

TEST_F(StoreAppIconCompatTest, LoadShellItemIcon_ValidSystemFileReturnsNonEmpty)
{
    // 测试有效系统文件通过 IShellItemImageFactory 正常取得高清图标
    const QString explorerPath = QStringLiteral("C:/Windows/explorer.exe");
    if (QFile::exists(explorerPath)) {
        QIcon icon = SysInfoUtil::LoadShellItemIcon(explorerPath, 48);
        EXPECT_FALSE(icon.isNull());
        QPixmap pix = icon.pixmap(48, 48);
        EXPECT_FALSE(pix.isNull());
        EXPECT_GT(pix.width(), 0);
        EXPECT_GT(pix.height(), 0);
    }
}

TEST_F(StoreAppIconCompatTest, LoadFolderIcon_NativeSystemFolderHighRes)
{
    // 测试系统文件夹通过 Shell 原生系统图标索引取得 256px Jumbo 原生高清图标
    WCHAR desktopPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOP, NULL, 0, desktopPath))) {
        SHFILEINFOW sfiDesk = { 0 };
        DWORD_PTR res = SHGetFileInfoW(desktopPath, 0, &sfiDesk, sizeof(sfiDesk), SHGFI_SYSICONINDEX);
        EXPECT_TRUE(res != 0);

        static const GUID IID_IImageList_Local = { 0x46EB5926, 0x582E, 0x4017, { 0x9F, 0xDF, 0xE8, 0x99, 0x8D, 0xAA, 0x09, 0x50 } };
        IImageList* imageList = nullptr;
        if (SUCCEEDED(SHGetImageList(4 /*SHIL_JUMBO*/, IID_IImageList_Local, (void**)&imageList))) {
            HICON hIcon = nullptr;
            if (SUCCEEDED(imageList->GetIcon(sfiDesk.iIcon, ILD_TRANSPARENT, &hIcon))) {
                QPixmap pix = QtWin::fromHICON(hIcon);
                EXPECT_FALSE(pix.isNull());
                EXPECT_EQ(pix.width(), 256);
                EXPECT_EQ(pix.height(), 256);
                DestroyIcon(hIcon);
            }
            imageList->Release();
        }
    }
}

TEST_F(StoreAppIconCompatTest, LoadUwpAppIcon_InstalledCalculator)
{
    // 微软商店内置计算器 AUMID 兼容测试
    const QString calcAumid = QStringLiteral("Microsoft.WindowsCalculator_8wekyb3d8bbwe!App");
    QIcon icon = SysInfoUtil::LoadUwpAppIconFromAumid(calcAumid, 48);
    
    // 如果系统装有计算器则验证其非空；即使未安装也需保证不崩溃
    if (!icon.isNull()) {
        QPixmap pix = icon.pixmap(48, 48);
        EXPECT_FALSE(pix.isNull());
        EXPECT_GT(pix.width(), 0);
        EXPECT_GT(pix.height(), 0);

        QImage img = pix.toImage().convertToFormat(QImage::Format_ARGB32);
        int darkSemi = 0, totalSemi = 0, totalNonZero = 0;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                QRgb p = img.pixel(x, y);
                int a = qAlpha(p);
                if (a > 0) totalNonZero++;
                if (a > 0 && a < 255) {
                    totalSemi++;
                    if (qRed(p) < 30 && qGreen(p) < 30 && qBlue(p) < 30) darkSemi++;
                }
            }
        }
        std::cout << "[Diagnostic Calc] nonZero: " << totalNonZero 
                  << " semi: " << totalSemi 
                  << " darkSemi: " << darkSemi << std::endl;
    }

    // 测试非法 AUMID 容错性
    QIcon invalidIcon = SysInfoUtil::LoadUwpAppIconFromAumid(QStringLiteral("NonExistent.Package_invalid!App"), 48);
    EXPECT_TRUE(invalidIcon.isNull());
}

TEST_F(StoreAppIconCompatTest, GetBlankFileFallbackIcon_GuaranteedValid)
{
    // 验证空白文档兜底防线 100% 返回有效且可绘制的图标
    QIcon icon48 = SysInfoUtil::GetBlankFileFallbackIcon(48);
    EXPECT_FALSE(icon48.isNull());
    QPixmap pix48 = icon48.pixmap(48, 48);
    EXPECT_FALSE(pix48.isNull());
    EXPECT_EQ(pix48.width(), 48);
    EXPECT_EQ(pix48.height(), 48);

    QIcon icon96 = SysInfoUtil::GetBlankFileFallbackIcon(96);
    EXPECT_FALSE(icon96.isNull());
    QPixmap pix96 = icon96.pixmap(96, 96);
    EXPECT_FALSE(pix96.isNull());
    EXPECT_EQ(pix96.width(), 96);
    EXPECT_EQ(pix96.height(), 96);
}

TEST_F(StoreAppIconCompatTest, CreateDocumentPlateIcon_GuaranteedValidAndHighRes)
{
    // 验证文档白板衬底（Plate）合成，确保返回高清有效图层
    QIcon sampleInner = SysInfoUtil::SysFileIconByDLL(1, 48, QStringLiteral("msiexec.exe"));
    QIcon plateIcon = SysInfoUtil::CreateDocumentPlateIcon(sampleInner, 256);
    EXPECT_FALSE(plateIcon.isNull());
    QPixmap pix256 = plateIcon.pixmap(256, 256);
    EXPECT_FALSE(pix256.isNull());
    EXPECT_EQ(pix256.width(), 256);
    EXPECT_EQ(pix256.height(), 256);

    QPixmap pix48 = plateIcon.pixmap(48, 48);
    EXPECT_FALSE(pix48.isNull());
    EXPECT_EQ(pix48.width(), 48);
    EXPECT_EQ(pix48.height(), 48);
}

TEST_F(StoreAppIconCompatTest, FolderThumbnail_EmptyVsNonEmpty)
{
    QString tempRoot = "D:/Temp/lin_diag_test";
    QDir().mkpath(tempRoot + "/empty_dir");
    QDir().mkpath(tempRoot + "/non_empty_dir");
    QFile f(tempRoot + "/non_empty_dir/sample.txt");
    if (f.open(QIODevice::WriteOnly)) {
        f.write("Hello LinDesk thumbnail test");
        f.close();
    }

    QString pathEmpty = QDir::toNativeSeparators(tempRoot + "/empty_dir");
    QString pathNonEmpty = QDir::toNativeSeparators(tempRoot + "/non_empty_dir");

    HBITMAP hEmpty256 = CSystemApi::GetShellThumbnail(pathEmpty, 256, 256);
    HBITMAP hNonEmpty256 = CSystemApi::GetShellThumbnail(pathNonEmpty, 256, 256);

    EXPECT_TRUE(hNonEmpty256 != NULL);
    if (hNonEmpty256) {
        QPixmap pNonEmpty = QtWin::fromHBITMAP(hNonEmpty256, QtWin::HBitmapAlpha);
        EXPECT_FALSE(pNonEmpty.isNull());
        EXPECT_EQ(pNonEmpty.width(), 256);
        EXPECT_EQ(pNonEmpty.height(), 256);

        QPixmap sanitized = SysInfoUtil::sanitizeFolderPreviewMatte(pNonEmpty);
        EXPECT_FALSE(sanitized.isNull());
        DeleteObject(hNonEmpty256);
    }

    if (hEmpty256) {
        QPixmap pEmpty = QtWin::fromHBITMAP(hEmpty256, QtWin::HBitmapAlpha);
        EXPECT_FALSE(pEmpty.isNull());
        DeleteObject(hEmpty256);
    }
}

TEST_F(StoreAppIconCompatTest, GetAumidFromShortcut_SafeOnInvalidPath)
{
    QString aumid = SysInfoUtil::GetAumidFromShortcut(QStringLiteral("C:/invalid_shortcut.lnk"));
    EXPECT_TRUE(aumid.isEmpty());
}

TEST_F(StoreAppIconCompatTest, IconCacheManager_LruCapacityEviction)
{
    auto& mgr = IconCacheManager::instance();
    mgr.clear();
    const int originalCap = mgr.maxCapacity();
    
    // 设置较小上限进行严格验证
    const int testCap = 50;
    mgr.setMaxCapacity(testCap);
    EXPECT_EQ(mgr.maxCapacity(), testCap);

    // 注入 120 个 QIcon
    QPixmap dummyPix(16, 16);
    dummyPix.fill(Qt::blue);
    QIcon dummyIcon(dummyPix);

    for (int i = 0; i < 120; ++i) {
        QString key = QString("test_icon_%1").arg(i);
        mgr.getIcon(key, [&]() { return dummyIcon; });
    }

    // 断言条目数严格不超过上限
    EXPECT_LE(mgr.iconCount(), testCap);
    EXPECT_EQ(mgr.iconCount(), testCap);

    // 断言最新插入的条目必定命中（不需要走 loader）
    bool loaderCalled = false;
    QIcon hit = mgr.getIcon("test_icon_119", [&]() {
        loaderCalled = true;
        return QIcon();
    });
    EXPECT_FALSE(loaderCalled);
    EXPECT_FALSE(hit.isNull());

    // 同样验证 QImage / QPixmap 缓存容量限制
    for (int i = 0; i < 120; ++i) {
        QString key = QString("test_image_%1").arg(i);
        mgr.getPixmap(key, [&]() { return dummyPix; });
    }
    EXPECT_LE(mgr.imageCount(), testCap);
    EXPECT_EQ(mgr.imageCount(), testCap);

    // 恢复全局默认容量并清空测试数据
    mgr.clear();
    mgr.setMaxCapacity(originalCap);
}

TEST_F(StoreAppIconCompatTest, IconCacheManager_InvalidateItemCache)
{
    auto& mgr = IconCacheManager::instance();
    mgr.clear();

    QPixmap dummyPix(16, 16);
    dummyPix.fill(Qt::green);
    QIcon dummyIcon(dummyPix);

    const QString iconKey = "path:C:\\Users\\Desktop\\sample.png";
    const QString normPath = "C:\\Users\\Desktop\\sample.png";

    mgr.getIcon(iconKey + "|base|48", [&]() { return dummyIcon; });
    mgr.getPixmap(iconKey + "|render|48", [&]() { return dummyPix; });
    mgr.getPixmap(iconKey + "|matte|48", [&]() { return dummyPix; });
    mgr.getPixmap("image_preview:" + normPath + "|48", [&]() { return dummyPix; });

    EXPECT_EQ(mgr.iconCount(), 1);
    EXPECT_EQ(mgr.imageCount(), 3);

    // 执行统一失效
    mgr.invalidateItemCache(iconKey, normPath);

    // 断言关联的所有缓存条目被全部清空
    EXPECT_EQ(mgr.iconCount(), 0);
    EXPECT_EQ(mgr.imageCount(), 0);
}

TEST_F(StoreAppIconCompatTest, IconCacheManager_DiskPersistence)
{
    auto& mgr = IconCacheManager::instance();
    const QString filePath = QStringLiteral("C:/test/file_cache_verify.png");
    const qint64 mtime = 123456789;
    const int sizeTier = 48;

    const QString diskKey = mgr.computeDiskKey(filePath, mtime, sizeTier);
    EXPECT_FALSE(diskKey.isEmpty());

    QImage testImg(48, 48, QImage::Format_ARGB32);
    testImg.fill(Qt::yellow);

    mgr.saveDiskCache(diskKey, testImg);
    EXPECT_TRUE(mgr.hasDiskCache(diskKey));

    QImage loaded = mgr.loadDiskCache(diskKey);
    EXPECT_FALSE(loaded.isNull());
    EXPECT_EQ(loaded.width(), 48);
    EXPECT_EQ(loaded.height(), 48);
    EXPECT_EQ(loaded.pixelColor(24, 24), QColor(Qt::yellow));
}

TEST_F(StoreAppIconCompatTest, IconCacheManager_Placeholders)
{
    auto& mgr = IconCacheManager::instance();
    QPixmap folderPix = mgr.genericFolderPlaceholder(48);
    EXPECT_FALSE(folderPix.isNull());
    EXPECT_EQ(folderPix.width(), 48);
    EXPECT_EQ(folderPix.height(), 48);

    QPixmap appPix = mgr.genericAppPlaceholder(48);
    EXPECT_FALSE(appPix.isNull());
    EXPECT_EQ(appPix.width(), 48);
    EXPECT_EQ(appPix.height(), 48);

    QPixmap filePix = mgr.genericFilePlaceholder(48);
    EXPECT_FALSE(filePix.isNull());
    EXPECT_EQ(filePix.width(), 48);
    EXPECT_EQ(filePix.height(), 48);
}
