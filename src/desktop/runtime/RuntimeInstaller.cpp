#include "desktop/runtime/RuntimeInstaller.h"
#include "services/AppPaths.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QProcess>
#include <QDebug>
#include <QUuid>

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
    // 1. 优先使用本地构建/开发目录的 DLL（与当前主程序同配置 Debug/Release 且即时构建热更新）
    const QString localRuntimeDll = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("runtime/linwidget/LinWidgetRuntime.dll"));
    if (QFileInfo::exists(localRuntimeDll)) {
        return localRuntimeDll;
    }
    const QString localDll = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("LinWidgetRuntime.dll"));
    if (QFileInfo::exists(localDll)) {
        return localDll;
    }

    // 2. 生产环境使用安装在用户数据目录的运行时
    const QString userDll = QDir(runtimeDirectory()).filePath(QStringLiteral("LinWidgetRuntime.dll"));
    if (QFileInfo::exists(userDll)) {
#ifdef QT_DEBUG
        // Debug 模式下若只有 release 版的 userDll，避免混合加载导致 Qt/CRT 异常
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
    const QString dll = runtimeDllPath();
    return !dll.isEmpty() && QFileInfo::exists(dll);
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
    // 1. 彻底停掉组件并释放 DLL
    LinWidgetRuntimeBridge::instance().unloadRuntime();

    // 2. 清理运行时目录（保留用户组件 LinWidgets/ 与数据 LinWidgetsData/）
    const QString targetDir = runtimeDirectory();
    if (QDir(targetDir).exists()) {
        QDir dir(targetDir);
        if (!dir.removeRecursively()) {
            if (errorMessage) {
                *errorMessage = tr("Failed to completely remove runtime directory: %1").arg(targetDir);
            }
            return false;
        }
    }

    m_installedVersion.clear();
    updateStatus(Status::NotInstalled);
    emit installFinished(true, tr("LinWidget runtime successfully uninstalled."));
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
    emit installProgress(0, 100, tr("Extracting..."));

    // 目标目录与同卷临时目录（保证同卷重命名原子性，并避免系统 C 盘空间受限问题）
    const QString finalDir = runtimeDirectory();
    const QString tempExtractDir = finalDir + QStringLiteral("_install_tmp_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QDir().mkpath(tempExtractDir);

    if (!extractZipToDir(zipFilePath, tempExtractDir, errorMessage)) {
        QDir(tempExtractDir).removeRecursively();
        updateStatus(Status::Failed);
        emit installFinished(false, errorMessage ? *errorMessage : tr("Extraction failed."));
        return false;
    }

    // 校验核心 DLL 是否存在
    const QString dllInTemp = QDir(tempExtractDir).filePath("LinWidgetRuntime.dll");
    if (!QFileInfo::exists(dllInTemp)) {
        QDir(tempExtractDir).removeRecursively();
        const QString err = tr("Invalid runtime package: LinWidgetRuntime.dll not found.");
        if (errorMessage) *errorMessage = err;
        updateStatus(Status::Failed);
        emit installFinished(false, err);
        return false;
    }

    // 兼容旧版运行时压缩包（Resources 子目录结构）：将 icudtl.dat、.pak 与 locales 平铺到 libcef.dll 同级目录
    const QDir nestedResDir(QDir(tempExtractDir).filePath(QStringLiteral("Resources")));
    if (nestedResDir.exists()) {
        const QFileInfoList resEntries = nestedResDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
        for (const QFileInfo& entry : resEntries) {
            const QString destFile = QDir(tempExtractDir).filePath(entry.fileName());
            if (!QFileInfo::exists(destFile)) {
                QFile::copy(entry.absoluteFilePath(), destFile);
            }
        }
        const QDir nestedLocalesDir(nestedResDir.filePath(QStringLiteral("locales")));
        if (nestedLocalesDir.exists()) {
            const QString destLocalesPath = QDir(tempExtractDir).filePath(QStringLiteral("locales"));
            QDir().mkpath(destLocalesPath);
            const QFileInfoList localeEntries = nestedLocalesDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
            for (const QFileInfo& entry : localeEntries) {
                const QString destFile = QDir(destLocalesPath).filePath(entry.fileName());
                if (!QFileInfo::exists(destFile)) {
                    QFile::copy(entry.absoluteFilePath(), destFile);
                }
            }
        }
    }


    // 停止并卸载当前已加载的运行时（若有）
    LinWidgetRuntimeBridge::instance().unloadRuntime();

    if (QDir(finalDir).exists()) {
        QDir(finalDir).removeRecursively();
    }
    QDir().mkpath(QFileInfo(finalDir).absolutePath());

    if (!QDir().rename(tempExtractDir, finalDir)) {
        // 如果重命名失败，尝试逐文件复制
        QDir(tempExtractDir).removeRecursively();
        const QString err = tr("Failed to move extracted runtime to destination: %1").arg(finalDir);
        if (errorMessage) *errorMessage = err;
        updateStatus(Status::Failed);
        emit installFinished(false, err);
        return false;
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

    updateStatus(Status::Installed);
    emit installFinished(true, tr("LinWidget runtime successfully installed."));
    return true;
}

void RuntimeInstaller::downloadAndInstallLatestRuntime(const QString& downloadUrl)
{
    // 在线下载接口（后续连接 GitHub Releases，复用 UpdateDownloader）
    Q_UNUSED(downloadUrl);
}

void RuntimeInstaller::updateStatus(Status newStatus)
{
    if (m_status != newStatus) {
        m_status = newStatus;
        emit statusChanged(m_status);
    }
}
