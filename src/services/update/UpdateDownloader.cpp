#include "services/update/UpdateDownloader.h"

#include "Version.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>

UpdateDownloader::UpdateDownloader(QObject* parent)
    : QObject(parent)
{
}

bool UpdateDownloader::isDownloading() const
{
    return m_reply != nullptr;
}

QString UpdateDownloader::downloadedFilePath() const
{
    return m_currentFilePath;
}

void UpdateDownloader::download(const UpdateInfo& info)
{
    if (m_reply) {
        return;
    }

    m_cancelled = false;
    m_currentInfo = info;

    const QString dirPath = updatesDirectory();
    QDir dir;
    if (!dir.mkpath(dirPath)) {
        emit downloadFailed(QStringLiteral("Cannot create update download directory."));
        return;
    }

    m_currentFilePath = QDir(dirPath).filePath(fileNameForUpdate(info));
    QFile::remove(m_currentFilePath);

    m_outputFile = new QFile(m_currentFilePath, this);
    if (!m_outputFile->open(QIODevice::WriteOnly)) {
        const QString error = m_outputFile->errorString();
        m_outputFile->deleteLater();
        m_outputFile = nullptr;
        emit downloadFailed(QStringLiteral("Cannot write installer: %1").arg(error));
        return;
    }

    QNetworkRequest request(QUrl(info.url));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("LinDesk/%1").arg(QStringLiteral(LINDESK_VERSION_STRING)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    m_reply = m_network.get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        if (m_outputFile && m_reply) {
            m_outputFile->write(m_reply->readAll());
        }
    });
    connect(m_reply, &QNetworkReply::downloadProgress,
            this, &UpdateDownloader::progressChanged);
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        if (!m_reply) {
            return;
        }

        if (m_outputFile) {
            m_outputFile->write(m_reply->readAll());
            m_outputFile->close();
        }

        if (m_cancelled) {
            finishWithError(QStringLiteral("Download cancelled."), true);
            return;
        }

        if (m_reply->error() != QNetworkReply::NoError) {
            finishWithError(m_reply->errorString(), true);
            return;
        }

        if (!verifySha256(m_currentFilePath, m_currentInfo.sha256)) {
            finishWithError(QStringLiteral("SHA256 verification failed."), true);
            return;
        }

        cleanupReply();
        emit downloadSucceeded(m_currentFilePath);
    });
}

void UpdateDownloader::cancel()
{
    if (!m_reply) {
        return;
    }
    m_cancelled = true;
    m_reply->abort();
}

QString UpdateDownloader::updatesDirectory() const
{
    QString localAppData = QString::fromLocal8Bit(qgetenv("LOCALAPPDATA"));
    if (localAppData.isEmpty()) {
        localAppData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    }
    return QDir(localAppData).filePath(QStringLiteral("LinDesk/Updates"));
}

QString UpdateDownloader::fileNameForUpdate(const UpdateInfo& info) const
{
    const QString fromUrl = QFileInfo(QUrl(info.url).path()).fileName();
    if (!fromUrl.isEmpty()) {
        return fromUrl;
    }
    return QStringLiteral("LinDesk_Setup_%1.exe").arg(info.version);
}

bool UpdateDownloader::verifySha256(const QString& filePath, const QString& expectedHash) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        hash.addData(file.read(1024 * 1024));
    }

    const QString actual = QString::fromLatin1(hash.result().toHex()).toLower();
    return actual == expectedHash.trimmed().toLower();
}

void UpdateDownloader::finishWithError(const QString& errorMessage, bool removeFile)
{
    if (m_outputFile) {
        m_outputFile->close();
    }
    if (removeFile && !m_currentFilePath.isEmpty()) {
        QFile::remove(m_currentFilePath);
    }
    cleanupReply();
    emit downloadFailed(errorMessage);
}

void UpdateDownloader::cleanupReply()
{
    if (m_reply) {
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_outputFile) {
        m_outputFile->deleteLater();
        m_outputFile = nullptr;
    }
    m_cancelled = false;
}
