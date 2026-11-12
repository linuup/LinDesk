#include "desktop/runtime/RuntimeInstaller.h"
#include "services/AppPaths.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCoreApplication>
#include <QProcess>
#include <QSettings>
#include <QDebug>
#include <QUuid>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QCryptographicHash>
#include <QSysInfo>
#include <QDirIterator>
#include <functional>
#include <iostream>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

// 查找 CEF 核心运行库 (libcef.dll + icudtl.dat) 所在目录
// 规则：严格以独立组件安装目录 (userRuntimeDir) 为主准；仅当未显式卸载且用户数据目录为空时，才允许检测应用自带目录作为开发者免安装便利
QString findCefRuntimeDirectory(const QString& userRuntimeDir)
{
    QSettings s(AppPaths::configFilePath(), QSettings::IniFormat);
    if (s.value(QStringLiteral("LinWidgets/RuntimeUninstalled"), false).toBool()) {
        return QString();
    }

    // 1. 唯一首选标准目录：独立运行时目录 (%APPDATA%/LinDesk/runtime/linwidget)
    if (!userRuntimeDir.isEmpty()) {
        const QDir d(userRuntimeDir);
        if (QFileInfo::exists(d.filePath(QStringLiteral("libcef.dll"))) &&
            (QFileInfo::exists(d.filePath(QStringLiteral("icudtl.dat"))) ||
             QFileInfo::exists(d.filePath(QStringLiteral("Resources/icudtl.dat"))))) {
            return d.absolutePath();
        }
    }

    return QString();
}

// 智能安全文件复制/覆盖：
// 当目标文件被当前进程（如已加载的 libcef.dll / LinWidgetRuntime.dll 等）锁定时：
// 1. 若源路径与目标路径规范路径相同，直接返回 true
// 2. 若目标文件不存在，直接创建上级目录并复制
// 3. 若目标文件已存在：
//    a. 先尝试正常覆盖：删除旧文件再重新复制
//    b. 若 remove 失败（说明文件正被运行中的进程映射锁定）：
//       - 检查文件大小：若 srcSize == dstSize && srcSize > 0，说明二进制内容完全一致且完备，直接保留复用！
//       - 若大小不一致，尝试通过 NTFS 重命名移出原位 (.old.<uuid>) 并标记重启清理，然后再 copy
//       - 若更名仍受阻但目标文件有效 (dstSize > 0)，保留该现有运行中文件，避免打断整个安装/重装流程
bool safeCopyOrReplaceFile(const QString& srcPath, const QString& dstPath)
{
    if (!QFileInfo::exists(srcPath)) {
        return false;
    }

    const QFileInfo srcInfo(srcPath);
    const QFileInfo dstInfo(dstPath);

    if (dstInfo.exists() && srcInfo.canonicalFilePath() == dstInfo.canonicalFilePath()) {
        return true;
    }

    if (!dstInfo.exists()) {
        QDir().mkpath(dstInfo.absolutePath());
        return QFile::copy(srcPath, dstPath);
    }

    // 尝试正常覆盖：删除旧文件再重新复制
    if (QFile::remove(dstPath)) {
        return QFile::copy(srcPath, dstPath);
    }

    // 删除失败说明文件被锁定（例如 libcef.dll、chrome_elf.dll 等正被当前进程映射）
    const qint64 srcSize = srcInfo.size();
    const qint64 dstSize = dstInfo.size();

    // 若大小完全一致且非空，说明二进制内容完备，可直接保留复用
    if (srcSize == dstSize && srcSize > 0) {
        qInfo() << "[RuntimeInstaller] Target locked file has identical size (" << dstSize
                << "bytes), safely reusing:" << dstPath;
        return true;
    }

    // 大小不同时，尝试利用 NTFS 特性重命名原文件（已打开句柄允许重命名），将旧文件让出
    const QString tempOldPath = dstPath + QStringLiteral(".old.%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (QFile::rename(dstPath, tempOldPath)) {
#ifdef Q_OS_WIN
        MoveFileExW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(tempOldPath).utf16()),
                    NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
#endif
        if (QFile::copy(srcPath, dstPath)) {
            return true;
        }
    }

    // 若更名仍失败，但目标文件存在且非空，保留现有文件以确保安装流程顺利走通
    if (dstSize > 0) {
        qWarning() << "[RuntimeInstaller] File is locked by running process and cannot be removed or renamed, keeping existing file:" << dstPath;
        return true;
    }

    return false;
}

bool safeMoveOrReplaceFile(const QString& srcPath, const QString& dstPath)
{
    if (srcPath == dstPath) {
        return true;
    }
    if (!QFile::exists(dstPath) && QFile::rename(srcPath, dstPath)) {
        return true;
    }
    if (safeCopyOrReplaceFile(srcPath, dstPath)) {
        QFile::remove(srcPath);
        return true;
    }
    return false;
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
        safeCopyOrReplaceFile(srcPath, dstPath);
    }
}

} // namespace

RuntimeInstaller::RuntimeInstaller(QObject* parent)
    : QObject(parent)
{
    m_networkManager = new QNetworkAccessManager(this);

    // 冷启动清理机制：若曾被用户显式卸载，此时进程尚未 LoadLibrary 任何 CEF 动态库，执行彻底物理清理
    QSettings s(AppPaths::configFilePath(), QSettings::IniFormat);
    const bool wasUninstalled = s.value(QStringLiteral("LinWidgets/RuntimeUninstalled"), false).toBool();
    if (wasUninstalled) {
        const QString rDir = runtimeDirectory();
        if (QDir(rDir).exists()) {
            QDir(rDir).removeRecursively();
        }
    }

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

RuntimeInstaller::~RuntimeInstaller()
{
    cancelDownload();
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

bool RuntimeInstaller::isDownloading() const
{
    return m_status == Status::Installing && m_currentReply != nullptr;
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
    return uninstallRuntime(false, errorMessage);
}

bool RuntimeInstaller::uninstallRuntime(bool deleteUserData, QString* errorMessage)
{
    Q_UNUSED(errorMessage);

    // 1. 记录已卸载状态并关闭主开关
    QSettings s(AppPaths::configFilePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("LinWidgets/RuntimeUninstalled"), true);
    s.setValue(QStringLiteral("LinWidgets/Enabled"), false);
    s.sync();

    // 2. 停止所有正在运行的小组件与动态壁纸（保持进程内 CEF/DLL 内存映射安全，避免异步回调或野指针崩溃）
    LinWidgetRuntimeBridge::instance().unloadRuntime();

    // 3. 彻底物理清理用户数据目录下的 CEF 运行时目录
    const QString targetDir = runtimeDirectory();
    if (QDir(targetDir).exists()) {
        QDir dir(targetDir);
        const bool removed = dir.removeRecursively();
        if (!removed && QDir(targetDir).exists()) {
            // 对 Windows 进程锁定的 DLL，遍历逐一解除或标记下一次冷启动清除
            QDirIterator it(targetDir, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString filePath = it.next();
#ifdef Q_OS_WIN
                MoveFileExW(reinterpret_cast<LPCWSTR>(filePath.utf16()), NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
#endif
                QFile::remove(filePath);
            }
        }
    }

    // 4. 若用户明确选择同时清理个人壁纸与组件数据
    if (deleteUserData) {
        if (AppPaths::isDebugOrDevMode() &&
            (AppPaths::userLinWidgetsDir().contains(QStringLiteral("resources/LinWidgets")) ||
             AppPaths::userLinWidgetsDir().contains(QStringLiteral("resources\\LinWidgets")))) {
            QFile::remove(AppPaths::linWidgetRegistryFilePath());
            QFile::remove(AppPaths::wallpaperRegistryFilePath());
        } else {
            const QString widgetsDir = AppPaths::userLinWidgetsDir();
            if (QDir(widgetsDir).exists()) {
                QDir(widgetsDir).removeRecursively();
            }
            const QString wallpapersDir = AppPaths::userWallpapersDir();
            if (QDir(wallpapersDir).exists()) {
                QDir(wallpapersDir).removeRecursively();
            }
        }
        const QString dataDir = AppPaths::linWidgetDataDir();
        if (QDir(dataDir).exists()) {
            QDir(dataDir).removeRecursively();
        }
        s.remove(QStringLiteral("LinWallpapers"));
        s.remove(QStringLiteral("LinWidgets/instances"));
        s.sync();
    }

    m_installedVersion.clear();
    updateStatus(Status::NotInstalled);
    return true;
}

bool RuntimeInstaller::extractZipToDir(const QString& zipFilePath, const QString& targetDir, QString* errorMessage)
{
#ifdef Q_OS_WIN
    // 1. 优先使用 Windows 原生 tar 命令解压 zip（Windows 10/11 自带 bsdtar，速度极快）
    QProcess proc;
    QStringList args;
    args << "-xf" << QDir::toNativeSeparators(zipFilePath) << "-C" << QDir::toNativeSeparators(targetDir);
    proc.start("tar", args);
    if (proc.waitForFinished(60000) && proc.exitCode() == 0) {
        return true;
    }

    // 2. 备用兜底：若系统未内置 tar，使用 PowerShell Expand-Archive 回退解压
    qInfo() << "[RuntimeInstaller] tar extraction failed or unavailable, fallback to PowerShell Expand-Archive...";
    QProcess psProc;
    const QString psCmd = QStringLiteral("Expand-Archive -LiteralPath '%1' -DestinationPath '%2' -Force")
                              .arg(QDir::toNativeSeparators(zipFilePath))
                              .arg(QDir::toNativeSeparators(targetDir));
    QStringList psArgs;
    psArgs << "-NoProfile" << "-ExecutionPolicy" << "Bypass" << "-Command" << psCmd;
    psProc.start("powershell.exe", psArgs);
    if (!psProc.waitForFinished(120000) || psProc.exitCode() != 0) {
        const QString err = QString::fromUtf8(psProc.readAllStandardError());
        if (errorMessage) *errorMessage = err.isEmpty() ? tr("解压支持包超时或失败") : err;
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
    emit installProgress(0, 100, tr("正在解压组件支持包..."));

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

    // 校验核心 CEF 动态库 libcef.dll 与 icudtl.dat 是否存在
    const QString libcefInTemp = QDir(effectiveSourceRoot).filePath(QStringLiteral("libcef.dll"));
    const QString icuInTemp = QDir(effectiveSourceRoot).filePath(QStringLiteral("icudtl.dat"));
    const QString icuInSub = QDir(effectiveSourceRoot).filePath(QStringLiteral("Resources/icudtl.dat"));
    if (!QFileInfo::exists(libcefInTemp) || (!QFileInfo::exists(icuInTemp) && !QFileInfo::exists(icuInSub))) {
        QDir(tempExtractDir).removeRecursively();
        const QString err = tr("无效的支持包：缺少核心运行文件 (libcef.dll 或 icudtl.dat)。");
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

    bool moveOk = QDir().rename(effectiveSourceRoot, finalDir);
    if (!moveOk) {
        // 若整目录 rename 失败（如跨盘符或目标目录包含非空残留/锁定文件），逐个文件安全覆盖移动
        QDir().mkpath(finalDir);
        QDirIterator it(effectiveSourceRoot, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        moveOk = true;
        while (it.hasNext()) {
            it.next();
            const QString rel = QDir(effectiveSourceRoot).relativeFilePath(it.filePath());
            const QString dest = QDir(finalDir).filePath(rel);
            if (it.fileInfo().isDir()) {
                QDir().mkpath(dest);
            } else {
                QDir().mkpath(QFileInfo(dest).absolutePath());
                if (!safeMoveOrReplaceFile(it.filePath(), dest)) {
                    moveOk = false;
                }
            }
        }
    }

    if (!moveOk) {
        QDir(tempExtractDir).removeRecursively();
        const QString err = tr("Failed to move extracted runtime to destination: %1").arg(finalDir);
        if (errorMessage) *errorMessage = err;
        updateStatus(Status::Failed);
        emit installFinished(false, err);
        return false;
    }
    if (QDir(tempExtractDir).exists()) {
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

    if (!QFile::exists(manifestPath)) {
        QFile mf(manifestPath);
        if (mf.open(QIODevice::WriteOnly)) {
            QJsonObject mObj;
            mObj.insert(QStringLiteral("name"), QStringLiteral("LinWidget Runtime"));
            mObj.insert(QStringLiteral("runtimeVersion"), m_installedVersion);
            mObj.insert(QStringLiteral("protocolVersion"), QStringLiteral("1.0.0"));
            mf.write(QJsonDocument(mObj).toJson(QJsonDocument::Indented));
        }
    }

    QSettings s(AppPaths::configFilePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("LinWidgets/RuntimeUninstalled"), false);
    s.setValue(QStringLiteral("LinWidgets/Enabled"), true);
    s.sync();

    updateStatus(Status::Installed);
    emit runtimeInstalledAndReady();
    emit installFinished(true, tr("LinWidget runtime successfully installed."));
    return true;
}

QString RuntimeInstaller::localBuildRuntimeDir() const
{
    QStringList candidates;
    candidates << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("runtime/linwidget"));
#ifdef LINDESK_SOURCE_DIR
    candidates << QDir(QStringLiteral(LINDESK_SOURCE_DIR)).filePath(QStringLiteral("build/Release/runtime/linwidget"));
    candidates << QDir(QStringLiteral(LINDESK_SOURCE_DIR)).filePath(QStringLiteral("build/runtime/linwidget"));
    candidates << QDir(QStringLiteral(LINDESK_SOURCE_DIR)).filePath(QStringLiteral("3rdparty/cef/Release"));
#endif

    for (const QString& cand : candidates) {
        if (!cand.isEmpty() && QDir(cand).exists()) {
            const QDir d(cand);
            if (QFileInfo::exists(d.filePath(QStringLiteral("libcef.dll"))) &&
                (QFileInfo::exists(d.filePath(QStringLiteral("icudtl.dat"))) ||
                 QFileInfo::exists(d.filePath(QStringLiteral("Resources/icudtl.dat"))))) {
                return QDir::cleanPath(cand);
            }
        }
    }
    return QString();
}

bool RuntimeInstaller::isLocalBuildRuntimeAvailable() const
{
    return !localBuildRuntimeDir().isEmpty();
}

bool RuntimeInstaller::installRuntimeFromFolder(const QString& sourceDir, QString* errorMessage)
{
    if (sourceDir.isEmpty() || !QDir(sourceDir).exists()) {
        const QString err = tr("Source runtime folder does not exist: %1").arg(sourceDir);
        if (errorMessage) *errorMessage = err;
        return false;
    }

    updateStatus(Status::Installing);

    const QString finalDir = runtimeDirectory();
    QDir().mkpath(finalDir);

    std::function<bool(const QString&, const QString&)> copyRecursive = [&](const QString& src, const QString& dst) -> bool {
        QDir srcDir(src);
        if (!srcDir.exists()) return false;
        QDir().mkpath(dst);
        const auto entries = srcDir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
        for (const QFileInfo& entry : entries) {
            const QString srcPath = entry.absoluteFilePath();
            const QString dstPath = QDir(dst).filePath(entry.fileName());
            if (entry.isDir()) {
                if (!copyRecursive(srcPath, dstPath)) return false;
            } else {
                if (!safeCopyOrReplaceFile(srcPath, dstPath)) return false;
            }
        }
        return true;
    };

    if (!copyRecursive(sourceDir, finalDir)) {
        const QString err = tr("Failed to copy runtime files from %1 to %2").arg(sourceDir, finalDir);
        if (errorMessage) *errorMessage = err;
        updateStatus(Status::Failed);
        emit installFinished(false, err);
        return false;
    }

    syncBundledRuntimeBinariesToCefDir(finalDir);

    const QString manifestPath = QDir(finalDir).filePath(QStringLiteral("manifest.json"));
    if (!QFile::exists(manifestPath)) {
        QFile mf(manifestPath);
        if (mf.open(QIODevice::WriteOnly)) {
            QJsonObject mObj;
            mObj.insert(QStringLiteral("name"), QStringLiteral("LinWidget Runtime (Local Dev)"));
            mObj.insert(QStringLiteral("runtimeVersion"), QStringLiteral("1.0.0-dev"));
            mObj.insert(QStringLiteral("protocolVersion"), QStringLiteral("1.0.0"));
            mf.write(QJsonDocument(mObj).toJson(QJsonDocument::Indented));
        }
    }

    m_installedVersion = QStringLiteral("1.0.0-dev");

    QSettings s(AppPaths::configFilePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("LinWidgets/RuntimeUninstalled"), false);
    s.setValue(QStringLiteral("LinWidgets/Enabled"), true);
    s.sync();

    updateStatus(Status::Installed);
    emit runtimeInstalledAndReady();
    emit installFinished(true, tr("LinWidget runtime successfully installed from local build."));
    return true;
}

bool RuntimeInstaller::installRuntimeFromLocalBuild(QString* errorMessage)
{
    const QString localDir = localBuildRuntimeDir();
    if (localDir.isEmpty()) {
        const QString err = tr("Local build runtime not found. Please compile the LinWidgetRuntime target first.");
        if (errorMessage) *errorMessage = err;
        return false;
    }
    return installRuntimeFromFolder(localDir, errorMessage);
}

void RuntimeInstaller::downloadAndInstallLatestRuntime(const QString& downloadUrl, const QString& expectedSha256)
{
    if (m_status == Status::Installing) {
        qWarning() << "[RuntimeInstaller] Download/installation already in progress.";
        return;
    }

    cancelDownload();

    updateStatus(Status::Installing);
    m_targetSha256 = expectedSha256.trimmed().toLower();
    m_resolvedDownloadUrl = downloadUrl.trimmed();

    // 严格走真实在线下载与解压部署流程，不进行本地就地跳过
    if (!m_resolvedDownloadUrl.isEmpty()) {
        emit installProgress(0, 100, tr("准备下载组件支持包..."));
        startDirectDownload(QUrl(m_resolvedDownloadUrl));
    } else {
        emit installProgress(0, 100, tr("正在获取最新组件支持包信息..."));
        fetchReleaseMetadata();
    }
}

void RuntimeInstaller::fetchReleaseMetadata()
{
    const QUrl apiUrl(QStringLiteral("https://api.github.com/repos/linuup/LinDesk/releases/latest"));
    QNetworkRequest req(apiUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LinDesk-DesktopApp"));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    auto* reply = m_networkManager->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (m_status != Status::Installing) {
            return;
        }

#if defined(Q_PROCESSOR_X86_64) || defined(__x86_64__) || defined(_M_X64)
        const QString archTarget = QStringLiteral("x64");
#else
        const QString archTarget = QStringLiteral("x86");
#endif

        QString chosenUrl;
        QString chosenSha256;

        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray data = reply->readAll();
            const QJsonDocument doc = QJsonDocument::fromJson(data);
            if (doc.isObject()) {
                const QJsonObject root = doc.object();
                const QJsonArray assets = root.value(QStringLiteral("assets")).toArray();

                for (const auto& val : assets) {
                    const QJsonObject a = val.toObject();
                    const QString name = a.value(QStringLiteral("name")).toString();
                    const QString dUrl = a.value(QStringLiteral("browser_download_url")).toString();

                    if (name.startsWith(QStringLiteral("linwidget-runtime-"), Qt::CaseInsensitive) &&
                        name.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
                        if (name.contains(archTarget, Qt::CaseInsensitive) || chosenUrl.isEmpty()) {
                            chosenUrl = dUrl;
                        }
                    }
                }
            }
        }

        // 若 GitHub API 受限 (403 限频) 或网络波动未能返回 asset 列表，自动回退到 v1.0.0 静态永久底座路径
        if (chosenUrl.isEmpty()) {
            qInfo() << "[RuntimeInstaller] GitHub API query returned empty or failed. Fallback to v1.0.0 permanent release URL.";
            chosenUrl = QStringLiteral("https://github.com/linuup/LinDesk/releases/download/v1.0.0/linwidget-runtime-v1.0.0-%1.zip")
                            .arg(archTarget);
        }

        m_resolvedDownloadUrl = chosenUrl;
        if (!chosenSha256.isEmpty()) {
            m_targetSha256 = chosenSha256.trimmed().toLower();
        }

        startDirectDownload(QUrl(m_resolvedDownloadUrl));
    });
}

void RuntimeInstaller::startDirectDownload(const QUrl& url)
{
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }

    if (m_downloadFile) {
        if (m_downloadFile->isOpen()) m_downloadFile->close();
        delete m_downloadFile;
        m_downloadFile = nullptr;
    }

    const QString tempDir = QDir::temp().filePath(QStringLiteral("LinDesk"));
    QDir().mkpath(tempDir);
    m_tempZipPath = QDir(tempDir).filePath(QStringLiteral("linwidget_runtime_download.zip"));

    if (QFile::exists(m_tempZipPath)) {
        QFile::remove(m_tempZipPath);
    }

    m_downloadFile = new QFile(m_tempZipPath, this);
    if (!m_downloadFile->open(QIODevice::WriteOnly)) {
        abortAndCleanup(tr("无法创建临时下载文件：%1").arg(m_tempZipPath));
        return;
    }

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LinDesk-Runtime-Downloader/1.0"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    m_currentReply = m_networkManager->get(req);

    connect(m_currentReply, &QNetworkReply::readyRead, this, [this]() {
        if (m_currentReply && m_downloadFile && m_downloadFile->isOpen()) {
            m_downloadFile->write(m_currentReply->readAll());
        }
    });

    connect(m_currentReply, &QNetworkReply::downloadProgress, this, [this](qint64 bytesReceived, qint64 bytesTotal) {
        if (bytesReceived <= 0) return;
        const double mbReceived = static_cast<double>(bytesReceived) / (1024.0 * 1024.0);
        const double mbTotal = static_cast<double>(bytesTotal) / (1024.0 * 1024.0);
        QString stage;
        if (bytesTotal > 0) {
            const int pct = static_cast<int>((bytesReceived * 100) / bytesTotal);
            stage = tr("正在下载组件支持包：%1 MB / %2 MB (%3%)")
                        .arg(QString::number(mbReceived, 'f', 1))
                        .arg(QString::number(mbTotal, 'f', 1))
                        .arg(pct);
        } else {
            stage = tr("正在下载组件支持包：%1 MB...")
                        .arg(QString::number(mbReceived, 'f', 1));
        }
        emit installProgress(bytesReceived, bytesTotal, stage);
    });

    connect(m_currentReply, &QNetworkReply::finished, this, &RuntimeInstaller::handleDownloadFinished);
}

void RuntimeInstaller::handleDownloadFinished()
{
    if (!m_currentReply) {
        return;
    }

    m_currentReply->deleteLater();
    const auto error = m_currentReply->error();
    const int httpStatus = m_currentReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (m_downloadFile) {
        m_downloadFile->flush();
        m_downloadFile->close();
    }

    if (error == QNetworkReply::OperationCanceledError) {
        abortAndCleanup(tr("已取消下载"));
        return;
    }

    if (error != QNetworkReply::NoError || (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 400))) {
        abortAndCleanup(tr("下载组件运行库失败：%1 (HTTP %2)").arg(m_currentReply->errorString()).arg(httpStatus));
        return;
    }

    // 基础有效性检测（组件支持包体积一般不低于 5MB，若过小可能下载到了错误网页）
    const qint64 fileSize = QFileInfo(m_tempZipPath).size();
    if (fileSize < 5 * 1024 * 1024) {
        abortAndCleanup(tr("下载的组件包体积异常（%1 KB），请检查网络连接后重试。").arg(fileSize / 1024));
        return;
    }

    // 校验 SHA-256 哈希完整性
    if (!m_targetSha256.isEmpty()) {
        emit installProgress(90, 100, tr("正在校验文件完整性..."));
        QFile file(m_tempZipPath);
        if (file.open(QIODevice::ReadOnly)) {
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if (hash.addData(&file)) {
                const QString computedSha256 = QString::fromLatin1(hash.result().toHex()).toLower();
                if (computedSha256 != m_targetSha256) {
                    abortAndCleanup(tr("文件完整性校验未通过，请重新下载。"));
                    return;
                }
            }
        }
    }

    emit installProgress(95, 100, tr("下载完成，正在解压与配置组件运行环境..."));

    QString installError;
    const bool ok = installFromLocalZip(m_tempZipPath, &installError);

    // 安装结束后清理临时下载包
    QFile::remove(m_tempZipPath);

    if (!ok) {
        abortAndCleanup(tr("解压配置组件运行库失败：%1").arg(installError));
    }
}

void RuntimeInstaller::abortAndCleanup(const QString& errorMessage)
{
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }

    if (m_downloadFile) {
        if (m_downloadFile->isOpen()) m_downloadFile->close();
        delete m_downloadFile;
        m_downloadFile = nullptr;
    }

    if (!m_tempZipPath.isEmpty() && QFile::exists(m_tempZipPath)) {
        QFile::remove(m_tempZipPath);
    }

    updateStatus(Status::NotInstalled);
    emit installFinished(false, errorMessage);
}

void RuntimeInstaller::cancelDownload()
{
    if (m_status == Status::Installing || m_currentReply != nullptr) {
        abortAndCleanup(tr("下载任务已取消。"));
    }
}

void RuntimeInstaller::updateStatus(Status newStatus)
{
    if (m_status != newStatus) {
        m_status = newStatus;
        emit statusChanged(m_status);
    }
}
