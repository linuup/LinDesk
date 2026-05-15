#pragma once

#include "services/update/UpdateTypes.h"

#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QTimer>

class UpdateDownloader : public QObject
{
    Q_OBJECT

public:
    explicit UpdateDownloader(QObject* parent = nullptr);

    bool isDownloading() const;
    QString downloadedFilePath() const;
    QString cachedInstallerPath(const UpdateInfo& info) const;
    bool hasValidCachedInstaller(const UpdateInfo& info, QString* installerPath = nullptr) const;

    void download(const UpdateInfo& info);
    void cancel();

signals:
    void progressChanged(qint64 bytesReceived, qint64 bytesTotal);
    void downloadSucceeded(const QString& installerPath);
    void downloadFailed(const QString& errorMessage);

private:
    QString updatesDirectory() const;
    QString fileNameForUpdate(const UpdateInfo& info) const;
    bool verifySha256(const QString& filePath, const QString& expectedHash) const;
    void startTimeoutTimers();
    void stopTimeoutTimers();
    void markProgressActivity();
    void finishWithError(const QString& errorMessage, bool removeFile);
    void cleanupReply();

    QNetworkAccessManager m_network;
    QNetworkReply* m_reply = nullptr;
    QFile* m_outputFile = nullptr;
    QString m_currentFilePath;
    UpdateInfo m_currentInfo;
    QTimer m_stallTimeoutTimer;
    bool m_cancelled = false;
};
