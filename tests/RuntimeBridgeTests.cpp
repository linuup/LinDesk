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
