#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QPointer>
#include <memory>
#include "desktop/widgets/runtime_export.h"

class QNetworkAccessManager;
class QNetworkReply;
class QFile;
class QUrl;

class RuntimeInstaller : public QObject {
    Q_OBJECT

public:
    enum class Status {
        NotInstalled,
        Installing,
        Installed,
        Failed
    };
    Q_ENUM(Status)

    static RuntimeInstaller& instance();

    Status status() const;
    QString installedVersion() const;
    QString runtimeDirectory() const;
    QString runtimeDllPath() const;

    bool isRuntimeInstalled() const;
    bool isRuntimeLoaded() const;
    bool isDownloading() const;

    // 尝试加载已安装的运行时
    bool loadInstalledRuntime();
    bool loadInstalledRuntime(const LinWidgetDesktopCtx& ctx);

    // 卸载运行时（彻底停止组件、释放 DLL、清除 runtime 目录，支持选择是否保留用户组件与数据）
    bool uninstallRuntime(bool deleteUserData, QString* errorMessage = nullptr);
    bool uninstallRuntime(QString* errorMessage = nullptr);

    // 本地 zip 包解压安装
    bool installFromLocalZip(const QString& zipFilePath, QString* errorMessage = nullptr);

    // 检测本地构建产物 (如 build/Release/runtime/linwidget) 是否包含可用 CEF 运行库
    bool isLocalBuildRuntimeAvailable() const;
    QString localBuildRuntimeDir() const;

    // 从指定本地文件夹复制安装运行环境
    bool installRuntimeFromFolder(const QString& sourceDir, QString* errorMessage = nullptr);

    // 从本地构建目录一键快速安装运行时到 AppData (免除远程下载)
    bool installRuntimeFromLocalBuild(QString* errorMessage = nullptr);

    // 在线拉取与更新（支持自动查询 GitHub Releases、实时进度与断点重试）
    void downloadAndInstallLatestRuntime(const QString& downloadUrl = QString(), const QString& expectedSha256 = QString());

    // 取消当前进行中的下载任务
    void cancelDownload();

signals:
    void statusChanged(Status newStatus);
    void runtimeInstalledAndReady();
    void installProgress(qint64 bytesReceived, qint64 bytesTotal, const QString& stage);
    void installFinished(bool success, const QString& message);

private:
    explicit RuntimeInstaller(QObject* parent = nullptr);
    ~RuntimeInstaller() override;

    void updateStatus(Status newStatus);
    bool extractZipToDir(const QString& zipFilePath, const QString& targetDir, QString* errorMessage);

    void fetchReleaseMetadata();
    void startDirectDownload(const QUrl& url);
    void handleDownloadFinished();
    void abortAndCleanup(const QString& errorMessage);

    Status m_status = Status::NotInstalled;
    QString m_installedVersion;

    // 在线下载与安装基础设施
    QNetworkAccessManager* m_networkManager = nullptr;
    QPointer<QNetworkReply> m_currentReply;
    QFile* m_downloadFile = nullptr;
    QString m_tempZipPath;
    QString m_targetSha256;
    QString m_resolvedDownloadUrl;
};
