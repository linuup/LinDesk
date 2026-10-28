#include <gtest/gtest.h>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QImage>
#include "desktop/widgets/runtime_export.h"

class RuntimeBridgeTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        if (!qApp) {
            static int argc = 1;
            static char appName[] = "RuntimeBridgeTest";
            static char* argv[] = { appName, nullptr };
            new QApplication(argc, argv);
        }
    }
};

TEST_F(RuntimeBridgeTest, UnloadedStateIsSafeAndDormant)
{
    auto& bridge = LinWidgetRuntimeBridge::instance();
    // 确保若之前有加载则先卸载
    bridge.unloadRuntime();

    EXPECT_FALSE(bridge.isLoaded());
    EXPECT_FALSE(bridge.isEnabled());
    EXPECT_FALSE(bridge.isCefOsrEnabled());
    EXPECT_FALSE(bridge.hasActiveWallpaper());
    EXPECT_FALSE(bridge.isWallpaperInteractive());
    EXPECT_TRUE(bridge.activeWallpaperPackagePath().isEmpty());
    EXPECT_TRUE(bridge.activeWallpaperId().isEmpty());
    EXPECT_FALSE(bridge.activeWallpaperIsDevelopment());

    // 空间测试：未加载时不占用任何格子与区域
    EXPECT_FALSE(bridge.isWidgetAt(QPoint(100, 100)));
    EXPECT_TRUE(bridge.widgetAt(QPoint(100, 100)).isEmpty());
    EXPECT_TRUE(bridge.occupiedRects().isEmpty());

    // 生命周期调用零开销返回 false
    EXPECT_FALSE(bridge.startWidgetPackage("nonexistent"));
    EXPECT_FALSE(bridge.stopWidget("dummy"));
    EXPECT_FALSE(bridge.restartWidget("dummy"));
    EXPECT_FALSE(bridge.createWidgetInstance("dummy", QPoint(0, 0)));
    EXPECT_FALSE(bridge.removeWidgetState("dummy"));
    EXPECT_FALSE(bridge.startWallpaper("dummy"));

    // 渲染判空防御：不应崩溃
    QImage dummyImg(100, 100, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&dummyImg);
    EXPECT_NO_THROW({
        bridge.renderCanvas(&painter, QRegion(0, 0, 100, 100));
    });

    // 事件转发安全返回 false
    EXPECT_FALSE(bridge.forwardMouseEvent(nullptr));
    EXPECT_FALSE(bridge.forwardWheelEvent(nullptr));
    EXPECT_FALSE(bridge.forwardKeyEvent(nullptr));
    EXPECT_FALSE(bridge.forwardInputMethodEvent(nullptr));
}

TEST_F(RuntimeBridgeTest, LoadedWallpaperBridgeContract)
{
    auto& bridge = LinWidgetRuntimeBridge::instance();
    const QString runtimeDll = QDir(QCoreApplication::applicationDirPath()).filePath("runtime/linwidget/LinWidgetRuntime.dll");
    if (!QFile::exists(runtimeDll)) {
        return;
    }

    ASSERT_TRUE(bridge.loadRuntime(runtimeDll));
    EXPECT_TRUE(bridge.isLoaded());

    LinWidgetDesktopCtx ctx;
    EXPECT_TRUE(bridge.init(ctx));

    EXPECT_FALSE(bridge.hasActiveWallpaper());
    EXPECT_FALSE(bridge.isWallpaperInteractive());
    EXPECT_TRUE(bridge.activeWallpaperId().isEmpty());
    EXPECT_TRUE(bridge.activeWallpaperPackagePath().isEmpty());

    // 尝试启动不存在的壁纸包应安全失败，不引发崩溃
    EXPECT_FALSE(bridge.startWallpaper("invalid/path/to/wallpaper"));
    EXPECT_FALSE(bridge.hasActiveWallpaper());

    // 停止壁纸应安全执行
    bridge.stopWallpaper(true);
    EXPECT_FALSE(bridge.hasActiveWallpaper());

    bridge.shutdown();
    bridge.unloadRuntime();
    EXPECT_FALSE(bridge.isLoaded());
}

TEST_F(RuntimeBridgeTest, PackageValidatorLinting)
{
    auto& bridge = LinWidgetRuntimeBridge::instance();
    const QString runtimeDll = QDir(QCoreApplication::applicationDirPath()).filePath("runtime/linwidget/LinWidgetRuntime.dll");
    if (!QFile::exists(runtimeDll)) {
        return;
    }

    ASSERT_TRUE(bridge.loadRuntime(runtimeDll));

    // 1. 校验不存在的路径应返回 false 并输出错误信息
    QString report;
    EXPECT_FALSE(bridge.validatePackage("invalid/nonexistent/widget", &report));
    EXPECT_TRUE(report.contains("ERR_PACKAGE_NOT_FOUND"));

    // 2. 校验内置壁纸组件包，验证静态 lint 格式与结构正常
    const QString wallpaperPkg = QDir(QCoreApplication::applicationDirPath()).filePath("../../src/resources/LinWidgets/Wallpaper");
    if (QDir(wallpaperPkg).exists()) {
        QString validReport;
        bool ok = bridge.validatePackage(wallpaperPkg, &validReport);
        EXPECT_TRUE(ok);
        EXPECT_TRUE(validReport.contains("PASS"));
    }

    bridge.unloadRuntime();
}

#include "services/AppPaths.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif

TEST_F(RuntimeBridgeTest, RestoreFromConfigInitializesCefWithoutCrash)
{
    auto& bridge = LinWidgetRuntimeBridge::instance();
    const QString runtimeDir = QDir(QCoreApplication::applicationDirPath()).filePath("runtime/linwidget");
    const QString runtimeDll = QDir(runtimeDir).filePath("LinWidgetRuntime.dll");
    if (!QFile::exists(runtimeDll)) {
        return;
    }

    // 1. 模拟旧版/未重生成构建目录结构：icudtl.dat 仅存在于 Resources 子目录，不在 runtime/linwidget 根目录
    const QString rootIcu = QDir(runtimeDir).filePath("icudtl.dat");
    const QString nestedResDir = QDir(runtimeDir).filePath("Resources");
    const QString nestedIcu = QDir(nestedResDir).filePath("icudtl.dat");
    bool createdNestedRes = false;
    if (QFile::exists(rootIcu) && !QFile::exists(nestedIcu)) {
        createdNestedRes = !QDir(nestedResDir).exists();
        QDir().mkpath(nestedResDir);
        QFile::copy(rootIcu, nestedIcu);
    }
    if (QFile::exists(nestedIcu)) {
        // 若后台没有其他进程映射 rootIcu，则移除 rootIcu 以验证自动补齐逻辑
        QFile::remove(rootIcu);
    }

    // 2. 模拟主进程已持有 cef-cache/lockfile 排他写锁（FILE_SHARE_READ），验证 CefManager 自动切换进程级缓存目录而不触发 ExitCode 21 / 0xc0000005 崩溃
#ifdef Q_OS_WIN
    const QString defaultCacheDir = QDir(AppPaths::userLocalConfigDir()).filePath("LinWidgets/cef-cache");
    QDir().mkpath(defaultCacheDir);
    const QString lockFilePath = QDir::toNativeSeparators(QDir(defaultCacheDir).filePath("lockfile"));
    const std::wstring lockFileW = lockFilePath.toStdWString();
    HANDLE hLock = CreateFileW(
        lockFileW.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE,
        nullptr);
#endif

    ASSERT_TRUE(bridge.loadRuntime(runtimeDll));
    LinWidgetDesktopCtx ctx = {};
    ASSERT_TRUE(bridge.init(ctx));
    bridge.setEnabled(true);
    bridge.setCefOsrEnabled(true);

    const QString tempConfig = QDir(QCoreApplication::applicationDirPath()).filePath("test_empty_widgets.json");
    QFile::remove(tempConfig);

    // 3. 验证预检拦截：当必要资产（如 LinDeskSubprocess.exe）缺失时，CefManager::initialize() 安全中止而不触发 FATAL CHECK 崩溃
    const QString subInRuntime = QDir(runtimeDir).filePath("LinDeskSubprocess.exe");
    const QString subInApp = QDir(QCoreApplication::applicationDirPath()).filePath("LinDeskSubprocess.exe");
    const QString subInRuntimeBak = subInRuntime + ".bak";
    const QString subInAppBak = subInApp + ".bak";
    const bool renamedRuntimeSub = QFile::exists(subInRuntime) && QFile::rename(subInRuntime, subInRuntimeBak);
    const bool renamedAppSub = QFile::exists(subInApp) && QFile::rename(subInApp, subInAppBak);
    if (renamedRuntimeSub && (!QFile::exists(subInApp) || renamedAppSub)) {
        EXPECT_TRUE(bridge.restoreFromConfig(tempConfig));
    }
    if (renamedRuntimeSub) {
        QFile::rename(subInRuntimeBak, subInRuntime);
    }
    if (renamedAppSub) {
        QFile::rename(subInAppBak, subInApp);
    }

    // 4. 恢复资产后再次执行 restoreFromConfig，验证 CefManager 自动将 icudtl.dat 补齐到 libcef.dll 同级目录并完成初始化
    EXPECT_TRUE(bridge.restoreFromConfig(tempConfig));
    QFile::remove(tempConfig);

    EXPECT_TRUE(QFile::exists(rootIcu));
    if (createdNestedRes && QFile::exists(rootIcu)) {
        QDir(nestedResDir).removeRecursively();
    }

    bridge.shutdown();
    bridge.unloadRuntime();

#ifdef Q_OS_WIN
    if (hLock != INVALID_HANDLE_VALUE) {
        CloseHandle(hLock);
    }
#endif
}

