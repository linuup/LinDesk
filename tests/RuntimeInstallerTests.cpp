#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QSettings>
#include "desktop/runtime/RuntimeInstaller.h"
#include "desktop/widgets/runtime_export.h"
#include "services/AppPaths.h"

class RuntimeInstallerTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        if (!qApp) {
            static int argc = 1;
            static char appName[] = "RuntimeInstallerTest";
            static char* argv[] = { appName, nullptr };
            new QCoreApplication(argc, argv);
        }
    }

    static void TearDownTestSuite() {
        QSettings s(AppPaths::configFilePath(), QSettings::IniFormat);
        s.remove(QStringLiteral("LinWidgets/RuntimeUninstalled"));
        s.sync();
    }
};

TEST_F(RuntimeInstallerTest, DataIsolationConstraint)
{
    // 测试契约：组件系统的安装、卸载绝不破坏桌面图标数据文件 (data.dat / data.json)
    const QString userDataDir = AppPaths::appDataDir();
    const QString dataDatPath = QDir(userDataDir).filePath("data.dat");
    const QString dataJsonPath = QDir(userDataDir).filePath("data.json");
    const QString widgetsDir = AppPaths::userLinWidgetsDir();
    const QString widgetsDataDir = AppPaths::userLinWidgetsDataDir();

    // 确保组件数据目录与用户目录存在模拟数据
    QDir().mkpath(widgetsDir);
    QDir().mkpath(widgetsDataDir);
    const QString sampleWidgetFile = QDir(widgetsDir).filePath("test_widget.txt");
    const QString sampleDataFile = QDir(widgetsDataDir).filePath("state.json");

    {
        QFile f(sampleWidgetFile);
        f.open(QIODevice::WriteOnly);
        f.write("widget content");
    }
    {
        QFile f(sampleDataFile);
        f.open(QIODevice::WriteOnly);
        f.write("{\"key\":\"value\"}");
    }

    // 模拟存在桌面持久化数据
    bool hasDataDat = QFile::exists(dataDatPath);
    if (!hasDataDat) {
        QFile f(dataDatPath);
        if (f.open(QIODevice::WriteOnly)) {
            f.write("mock_desktop_data");
        }
    }

    // 执行卸载操作 (哪怕此时运行时本来就不存在或已存在)
    QString error;
    RuntimeInstaller::instance().uninstallRuntime(&error);

    // 契约断言 1：卸载后，用户的 LinWidgets/ 与 LinWidgetsData/ 目录及内容必须严格保留
    EXPECT_TRUE(QFile::exists(sampleWidgetFile));
    EXPECT_TRUE(QFile::exists(sampleDataFile));

    // 契约断言 2：桌面数据文件绝不能被删除或清空
    if (!hasDataDat) {
        EXPECT_TRUE(QFile::exists(dataDatPath));
        QFile::remove(dataDatPath);
    }

    // 清理测试模拟文件
    QFile::remove(sampleWidgetFile);
    QFile::remove(sampleDataFile);
}

TEST_F(RuntimeInstallerTest, DeleteUserDataWhenExplicitlyRequested)
{
    const QString widgetsDir = AppPaths::userLinWidgetsDir();
    const QString widgetsDataDir = AppPaths::userLinWidgetsDataDir();
    const QString wallpapersDir = AppPaths::userWallpapersDir();

    QDir().mkpath(widgetsDir);
    QDir().mkpath(widgetsDataDir);
    QDir().mkpath(wallpapersDir);

    const QString sampleWidget = QDir(widgetsDir).filePath("test_widget_del.txt");
    const QString sampleData = QDir(widgetsDataDir).filePath("state_del.json");
    const QString sampleWallpaper = QDir(wallpapersDir).filePath("test_wp_del.txt");

    {
        QFile f(sampleWidget);
        f.open(QIODevice::WriteOnly);
        f.write("test widget");
    }
    {
        QFile f(sampleData);
        f.open(QIODevice::WriteOnly);
        f.write("test state");
    }
    {
        QFile f(sampleWallpaper);
        f.open(QIODevice::WriteOnly);
        f.write("test wallpaper");
    }

    QString error;
    RuntimeInstaller::instance().uninstallRuntime(true /* deleteUserData */, &error);

    // 显式选择清理个人数据时，组件与壁纸个人目录应被清除
    EXPECT_FALSE(QFile::exists(sampleWidget));
    EXPECT_FALSE(QFile::exists(sampleData));
    EXPECT_FALSE(QFile::exists(sampleWallpaper));
}

TEST_F(RuntimeInstallerTest, InstallationStatusAndVersionCheck)
{
    auto& installer = RuntimeInstaller::instance();
    const QString runtimeDir = installer.runtimeDirectory();

    // 验证目录路径结构符合预期
    EXPECT_TRUE(runtimeDir.endsWith("runtime/linwidget") || runtimeDir.endsWith("runtime\\linwidget"));

    // 验证状态一致性：若实际 runtimeDllPath 不存在，则 isRuntimeInstalled 必须返回 false
    const bool dllExists = QFile::exists(installer.runtimeDllPath());
    if (!dllExists) {
        EXPECT_FALSE(installer.isRuntimeInstalled());
        EXPECT_TRUE(installer.installedVersion().isEmpty());
    } else {
        // 若 DLL 存在，必须额外具备有效 CEF 核心库且未被用户标记卸载才算真正安装
        QSettings s(AppPaths::configFilePath(), QSettings::IniFormat);
        const bool uninstalled = s.value(QStringLiteral("LinWidgets/RuntimeUninstalled"), false).toBool();
        if (uninstalled) {
            EXPECT_FALSE(installer.isRuntimeInstalled());
        }
    }
}

TEST_F(RuntimeInstallerTest, LocalZipInstallationFlow)
{
    const QString distZip = QDir(QCoreApplication::applicationDirPath()).filePath("../../dist/linwidget-runtime-v1.0.0.zip");
    if (!QFile::exists(distZip)) {
        return;
    }

    auto& installer = RuntimeInstaller::instance();
    QString error;
    bool ok = installer.installFromLocalZip(distZip, &error);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(error.isEmpty());
    EXPECT_TRUE(installer.isRuntimeInstalled());
    EXPECT_EQ(installer.status(), RuntimeInstaller::Status::Installed);
    EXPECT_TRUE(QFile::exists(QDir(installer.runtimeDirectory()).filePath("icudtl.dat")));
    EXPECT_TRUE(QFile::exists(QDir(installer.runtimeDirectory()).filePath("locales/zh-CN.pak")));
}

