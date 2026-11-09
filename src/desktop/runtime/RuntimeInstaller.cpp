#include "desktop/runtime/RuntimeInstaller.h"
#include "services/AppPaths.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QProcess>
#include <QSettings>
#include <QDebug>
#include <QUuid>

namespace {

// 查找 CEF 核心运行库 (libcef.dll + icudtl.dat) 所在目录
QString findCefRuntimeDirectory(const QString& userRuntimeDir)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        userRuntimeDir,
        QDir(appDir).filePath(QStringLiteral("runtime/linwidget")),
        appDir
    };

    for (const QString& dirPath : candidates) {
        if (dirPath.isEmpty()) continue;
        const QDir d(dirPath);
        if (QFileInfo::exists(d.filePath(QStringLiteral("libcef.dll"))) &&
            (QFileInfo::exists(d.filePath(QStringLiteral("icudtl.dat"))) ||
             QFileInfo::exists(d.filePath(QStringLiteral("Resources/icudtl.dat"))))) {
            return d.absolutePath();
        }
    }
    return QString();
}

// 将主程序随包附带的 LinWidgetRuntime.dll 与 LinDeskSubprocess.exe 同步到目标 CEF 运行时目录
void syncBundledRuntimeBinariesToCefDir(const QString& targetCefDir)
{
    if (targetCefDir.isEmpty() || !QDir(targetCefDir).exists()) {
        return;
    }
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList binaryNames = {
        QStringLiteral("LinWidgetRuntime.dll"),
        QStringLiteral("LinDeskSubprocess.exe")
    };

    for (const QString& binName : binaryNames) {
        QString srcPath = QDir(appDir).filePath(binName);
        if (!QFileInfo::exists(srcPath)) {
            srcPath = QDir(appDir).filePath(QStringLiteral("runtime/linwidget/") + binName);
        }
        if (!QFileInfo::exists(srcPath)) {
            continue;
        }

        const QString dstPath = QDir(targetCefDir).filePath(binName);
        if (QFileInfo(srcPath).canonicalFilePath() == QFileInfo(dstPath).canonicalFilePath()) {
            continue;
        }
        if (QFileInfo::exists(dstPath)) {
            QFile::remove(dstPath);
        }
        QFile::copy(srcPath, dstPath);
    }
}

} // namespace

RuntimeInstaller::RuntimeInstaller(QObject* parent)
    : QObject(parent)
{
    if (isRuntimeInstalled()) {
        m_status = Status::Installed;
        const QString manifestPath = QDir(runtimeDirectory()).filePath("manifest.json");
        QFile file(manifestPath);
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (doc.isObject()) {
                m_installedVersion = doc.object().value("runtimeVersion").toString();
            }
        }
        if (m_installedVersion.isEmpty()) {
            m_installedVersion = QStringLiteral("1.0.0");
        }
    } else {
        m_status = Status::NotInstalled;
    }
}

RuntimeInstaller& RuntimeInstaller::instance()
{
    static RuntimeInstaller s_instance;
    return s_instance;
}

RuntimeInstaller::Status RuntimeInstaller::status() const
{
    return m_status;
}

QString RuntimeInstaller::installedVersion() const
{
    return m_installedVersion;
}

QString RuntimeInstaller::runtimeDirectory() const
{
    return QDir(AppPaths::appDataDir()).filePath("runtime/linwidget");
}

QString RuntimeInstaller::runtimeDllPath() const
{
    // 1. 优先使用随主程序打包安装的 LinWidgetRuntime.dll（确保与主程序版本/ABI 100% 一致）
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString localDll = QDir(appDir).filePath(QStringLiteral("LinWidgetRuntime.dll"));
    if (QFileInfo::exists(localDll)) {
        return localDll;
    }
    const QString localRuntimeDll = QDir(appDir).filePath(QStringLiteral("runtime/linwidget/LinWidgetRuntime.dll"));
    if (QFileInfo::exists(localRuntimeDll)) {
        return localRuntimeDll;
    }

    // 2. 兜底查找用户数据目录中的 LinWidgetRuntime.dll
    const QString userDll = QDir(runtimeDirectory()).filePath(QStringLiteral("LinWidgetRuntime.dll"));
    if (QFileInfo::exists(userDll)) {
#ifdef QT_DEBUG
        qWarning() << "[RuntimeInstaller] Found installed runtime in AppData, but running in QT_DEBUG mode without local debug runtime. Skipping release DLL to prevent Qt ABI crash.";
        return QString();
#else
        return userDll;
#endif
    }
    return QString();
}

bool RuntimeInstaller::isRuntimeInstalled() const
{
    QSettings s(AppPaths::configFilePath(), QSettings::IniFormat);
    if (s.value(QStringLiteral("LinWidgets/RuntimeUninstalled"), false).toBool()) {
        return false;
    }
    // 必须同时具备 LinWidgetRuntime.dll（随主程序打包）以及 CEF 核心运行文件 (libcef.dll + icudtl.dat)
    const QString dll = runtimeDllPath();
    if (dll.isEmpty() || !QFileInfo::exists(dll)) {
        return false;
    }
    const QString cefDir = findCefRuntimeDirectory(runtimeDirectory());
    return !cefDir.isEmpty();
}

bool RuntimeInstaller::isRuntimeLoaded() const
{
    return LinWidgetRuntimeBridge::instance().isLoaded();
}

bool RuntimeInstaller::loadInstalledRuntime()
{
    if (!isRuntimeInstalled()) {
        return false;
    }
    const QString cefDir = findCefRuntimeDirectory(runtimeDirectory());
    syncBundledRuntimeBinariesToCefDir(cefDir);

    const QString dll = runtimeDllPath();
    if (!LinWidgetRuntimeBridge::instance().loadRuntime(dll)) {
        updateStatus(Status::Failed);
        return false;
    }
    updateStatus(Status::Installed);
    return true;
}

bool RuntimeInstaller::loadInstalledRuntime(const LinWidgetDesktopCtx& ctx)
{
    if (!isRuntimeInstalled()) {
        return false;
    }
    const QString cefDir = findCefRuntimeDirectory(runtimeDirectory());
    syncBundledRuntimeBinariesToCefDir(cefDir);

    const QString dll = runtimeDllPath();
    if (!LinWidgetRuntimeBridge::instance().loadRuntime(dll)) {
        updateStatus(Status::Failed);
        return false;
    }
    const bool inited = LinWidgetRuntimeBridge::instance().init(ctx);
    if (inited) {
        updateStatus(Status::Installed);
    } else {
        updateStatus(Status::Failed);
    }
    return inited;
}

bool RuntimeInstaller::uninstallRuntime(QString* errorMessage)
{
    Q_UNUSED(errorMessage);

    // 1. 记录已卸载状态并关闭主开关
    QSettings s(AppPaths::configFilePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("LinWidgets/RuntimeUninstalled"), true);
    s.setValue(QStringLiteral("LinWidgets/Enabled"), false);
    s.sync();

    // 2. 停止所有正在运行的小组件与动态壁纸（保持进程内 CEF/DLL 内存映射安全，避免异步回调或野指针崩溃）
    LinWidgetRuntimeBridge::instance().unloadRuntime();

    // 3. 尽力清理用户数据目录下的 CEF 运行时目录（保留用户组件与数据）
    const QString targetDir = runtimeDirectory();
    if (QDir(targetDir).exists()) {
        QDir dir(targetDir);
        dir.removeRecursively();
    }

    m_installedVersion.clear();
    updateStatus(Status::NotInstalled);
    return true;
}

bool RuntimeInstaller::extractZipToDir(const QString& zipFilePath, const QString& targetDir, QString* errorMessage)
{
#ifdef Q_OS_WIN
    // 使用 Windows 原生 tar 命令解压 zip（Windows 10/11 自带 bsdtar，支持解压 zip）
    QProcess proc;
    QStringList args;
    args << "-xf" << QDir::toNativeSeparators(zipFilePath) << "-C" << QDir::toNativeSeparators(targetDir);
    proc.start("tar", args);
    if (!proc.waitForFinished(60000)) {
        if (errorMessage) *errorMessage = tr("Extraction timed out.");
        return false;
    }
    if (proc.exitCode() != 0) {
        if (errorMessage) *errorMessage = QString::fromUtf8(proc.readAllStandardError());
        return false;
    }
    return true;
#else
    Q_UNUSED(zipFilePath);
    Q_UNUSED(targetDir);
    if (errorMessage) *errorMessage = tr("Unsupported platform");
    return false;
#endif
}

bool RuntimeInstaller::installFromLocalZip(const QString& zipFilePath, QString* errorMessage)
{
    if (!QFileInfo::exists(zipFilePath)) {
        if (errorMessage) *errorMessage = tr("Zip file does not exist: %1").arg(zipFilePath);
        return false;
    }

    updateStatus(Status::Installing);
    emit installProgress(0, 100, tr("正在解压 CEF 运行支持包..."));

    const QString finalDir = runtimeDirectory();
    const QString tempExtractDir = finalDir + QStringLiteral("_install_tmp_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QDir().mkpath(tempExtractDir);

    if (!extractZipToDir(zipFilePath, tempExtractDir, errorMessage)) {
        QDir(tempExtractDir).removeRecursively();
        updateStatus(Status::Failed);
        emit installFinished(false, errorMessage ? *errorMessage : tr("Extraction failed."));
        return false;
    }

    // 兼容存在单层顶层目录的压缩包（例如解压出 cef_binary_xxx/ 或 runtime/）
    QDir tempRoot(tempExtractDir);
    const QFileInfoList topEntries = tempRoot.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
    QString effectiveSourceRoot = tempExtractDir;
    if (topEntries.size() == 1 && topEntries.first().isDir() &&
        !QFileInfo::exists(tempRoot.filePath(QStringLiteral("libcef.dll")))) {
        effectiveSourceRoot = topEntries.first().absoluteFilePath();
    }

    // 兼容 Release/ 与 Resources/ 子目录结构：将 CEF 二进制与 icudtl.dat、.pak、locales 平铺到根目录
    const QStringList flattenSubDirs = { QStringLiteral("Release"), QStringLiteral("Resources") };
    for (const QString& subName : flattenSubDirs) {
        const QDir subDir(QDir(effectiveSourceRoot).filePath(subName));
        if (!subDir.exists()) continue;

        const QFileInfoList subFiles = subDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
        for (const QFileInfo& entry : subFiles) {
            const QString destFile = QDir(effectiveSourceRoot).filePath(entry.fileName());
            if (!QFileInfo::exists(destFile)) {
                QFile::rename(entry.absoluteFilePath(), destFile);
            }
        }
        const QDir nestedLocalesDir(subDir.filePath(QStringLiteral("locales")));
        if (nestedLocalesDir.exists()) {
            const QString destLocalesPath = QDir(effectiveSourceRoot).filePath(QStringLiteral("locales"));
            QDir().mkpath(destLocalesPath);
            const QFileInfoList localeEntries = nestedLocalesDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
            for (const QFileInfo& entry : localeEntries) {
                const QString destFile = QDir(destLocalesPath).filePath(entry.fileName());
                if (!QFileInfo::exists(destFile)) {
                    QFile::rename(entry.absoluteFilePath(), destFile);
                }
            }
        }
    }

    // 校验核心 CEF 动态库 libcef.dll 是否存在（LinWidgetRuntime.dll 已随主程序打包，无需强制压缩包内包含）
    const QString libcefInTemp = QDir(effectiveSourceRoot).filePath(QStringLiteral("libcef.dll"));
    if (!QFileInfo::exists(libcefInTemp)) {
        QDir(tempExtractDir).removeRecursively();
        const QString err = tr("无效的 CEF 运行支持包：未找到核心文件 libcef.dll。");
        if (errorMessage) *errorMessage = err;
        updateStatus(Status::Failed);
        emit installFinished(false, err);
        return false;
    }

    // 将主程序自带的 LinWidgetRuntime.dll 与 LinDeskSubprocess.exe 同步到解压目录
    syncBundledRuntimeBinariesToCefDir(effectiveSourceRoot);

    // 停止当前正在运行的组件
    LinWidgetRuntimeBridge::instance().unloadRuntime();

    if (QDir(finalDir).exists()) {
        QDir(finalDir).removeRecursively();
    }
    QDir().mkpath(QFileInfo(finalDir).absolutePath());

    if (!QDir().rename(effectiveSourceRoot, finalDir)) {
        QDir(tempExtractDir).removeRecursively();
        const QString err = tr("Failed to move extracted runtime to destination: %1").arg(finalDir);
        if (errorMessage) *errorMessage = err;
        updateStatus(Status::Failed);
        emit installFinished(false, err);
        return false;
    }
    if (effectiveSourceRoot != tempExtractDir && QDir(tempExtractDir).exists()) {
        QDir(tempExtractDir).removeRecursively();
    }

    // 读取新版本号
    const QString manifestPath = QDir(finalDir).filePath("manifest.json");
    QFile file(manifestPath);
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isObject()) {
            m_installedVersion = doc.object().value("runtimeVersion").toString();
        }
    }
    if (m_installedVersion.isEmpty()) {
        m_installedVersion = QStringLiteral("1.0.0");
    }

    QSettings s(AppPaths::configFilePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("LinWidgets/RuntimeUninstalled"), false);
    s.setValue(QStringLiteral("LinWidgets/Enabled"), true);
    s.sync();

    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (bridge.loadRuntime()) {
        bridge.setEnabled(true);
        bridge.restoreFromConfig(QString());
    }

    updateStatus(Status::Installed);
    emit installFinished(true, tr("LinWidget runtime successfully installed."));
    return true;
}

void RuntimeInstaller::downloadAndInstallLatestRuntime(const QString& downloadUrl)
{
    Q_UNUSED(downloadUrl);

    updateStatus(Status::Installing);
    emit installProgress(50, 100, tr("正在校验 CEF 核心文件并激活运行支持..."));

    QSettings s(AppPaths::configFilePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("LinWidgets/RuntimeUninstalled"), false);
    s.setValue(QStringLiteral("LinWidgets/Enabled"), true);
    s.sync();

    const QString cefDir = findCefRuntimeDirectory(runtimeDirectory());
    const QString dll = runtimeDllPath();
    if (!cefDir.isEmpty() && !dll.isEmpty() && QFileInfo::exists(dll)) {
        syncBundledRuntimeBinariesToCefDir(cefDir);
        auto& bridge = LinWidgetRuntimeBridge::instance();
        if (bridge.loadRuntime(dll)) {
            bridge.setEnabled(true);
            bridge.restoreFromConfig(QString());
        }
        if (m_installedVersion.isEmpty()) {
            m_installedVersion = QStringLiteral("1.0.0");
        }
        emit installProgress(100, 100, tr("组件校验完成"));
        updateStatus(Status::Installed);
        emit installFinished(true, tr("LinWidget runtime successfully installed."));
    } else {
        s.setValue(QStringLiteral("LinWidgets/RuntimeUninstalled"), true);
        s.sync();
        updateStatus(Status::NotInstalled);
        emit installFinished(false, tr("未检测到本地 CEF 核心库 (libcef.dll)，请通过“离线包安装 (*.zip)”导入 CEF 运行包。"));
    }
}

void RuntimeInstaller::updateStatus(Status newStatus)
{
    if (m_status != newStatus) {
        m_status = newStatus;
        emit statusChanged(m_status);
    }
}
