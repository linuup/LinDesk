#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include "desktop/widgets/runtime_export.h"

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

    // 尝试加载已安装的运行时
    bool loadInstalledRuntime();
    bool loadInstalledRuntime(const LinWidgetDesktopCtx& ctx);

    // 卸载运行时（彻底停止组件、释放 DLL、清除 runtime 目录，保留用户组件与数据）
    bool uninstallRuntime(QString* errorMessage = nullptr);

    // 本地 zip 包解压安装
    bool installFromLocalZip(const QString& zipFilePath, QString* errorMessage = nullptr);

    // 在线拉取与更新（支持进度与取消）
    void downloadAndInstallLatestRuntime(const QString& downloadUrl = QString());

signals:
    void statusChanged(Status newStatus);
    void installProgress(qint64 bytesReceived, qint64 bytesTotal, const QString& stage);
    void installFinished(bool success, const QString& message);

private:
    explicit RuntimeInstaller(QObject* parent = nullptr);
    ~RuntimeInstaller() override = default;

    void updateStatus(Status newStatus);
    bool extractZipToDir(const QString& zipFilePath, const QString& targetDir, QString* errorMessage);

    Status m_status = Status::NotInstalled;
    QString m_installedVersion;
};
