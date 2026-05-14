#pragma once

#include "services/update/UpdateTypes.h"

#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>

class UpdateDownloader : public QObject
{
    Q_OBJECT

public:
    explicit UpdateDownloader(QObject* parent = nullptr);

    bool isDownloading() const;
    QString downloadedFilePath() const;

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
    void finishWithError(const QString& errorMessage, bool removeFile);
    void cleanupReply();

    QNetworkAccessManager m_network;
    QNetworkReply* m_reply = nullptr;
    QFile* m_outputFile = nullptr;
    QString m_currentFilePath;
    UpdateInfo m_currentInfo;
    bool m_cancelled = false;
};
