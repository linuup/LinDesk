#include "services/update/UpdateDownloader.h"

#include "Version.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>
#include <QStorageInfo>

namespace {
constexpr int kDownloadStallTimeoutMs = 15 * 1000;
constexpr qint64 kDiskSpaceSafetyMarginBytes = 16LL * 1024LL * 1024LL;
}

UpdateDownloader::UpdateDownloader(QObject* parent)
    : QObject(parent)
{
    m_stallTimeoutTimer.setSingleShot(true);

    connect(&m_stallTimeoutTimer, &QTimer::timeout, this, [this]() {
        if (!m_reply) {
            return;
        }
        finishWithError(QStringLiteral("Network connection appears interrupted. Please check your connection and try again."), true);
    });
}

bool UpdateDownloader::isDownloading() const
{
    return m_reply != nullptr;
}

QString UpdateDownloader::downloadedFilePath() const
{
    return m_currentFilePath;
}

QString UpdateDownloader::cachedInstallerPath(const UpdateInfo& info) const
{
    return QDir(updatesDirectory()).filePath(fileNameForUpdate(info));
}

bool UpdateDownloader::hasValidCachedInstaller(const UpdateInfo& info, QString* installerPath) const
{
    const QString cachedPath = cachedInstallerPath(info);
    if (!QFileInfo::exists(cachedPath)) {
        return false;
    }

    if (!verifySha256(cachedPath, info.sha256)) {
        QFile::remove(cachedPath);
        return false;
    }

    if (installerPath) {
        *installerPath = cachedPath;
    }
    return true;
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

    m_currentFilePath = cachedInstallerPath(info);
    if (hasValidCachedInstaller(info)) {
        emit downloadSucceeded(m_currentFilePath);
        return;
    }

    QFile::remove(m_currentFilePath);

    m_outputFile = new QFile(m_currentFilePath, this);

    QNetworkRequest request(QUrl(info.url));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("LinDesk/%1").arg(QStringLiteral(LINDESK_VERSION_STRING)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    m_reply = m_network.get(request);
    connect(m_reply, &QNetworkReply::metaDataChanged, this, [this]() {
        if (!m_reply || m_outputFile->isOpen()) {
            return;
        }
        const QVariant lengthHeader = m_reply->header(QNetworkRequest::ContentLengthHeader);
        bool ok = false;
        const qint64 contentLength = lengthHeader.toLongLong(&ok);
        if (ok && contentLength > 0 && !hasEnoughDiskSpace(contentLength + kDiskSpaceSafetyMarginBytes)) {
            finishWithError(diskSpaceErrorMessage(), true);
            return;
        }
        ensureOutputFileOpen();
    });
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(m_reply, &QNetworkReply::errorOccurred, this, [this](QNetworkReply::NetworkError) {
        if (!m_reply || m_cancelled) {
            return;
        }
        finishWithError(QStringLiteral("Network error: %1").arg(m_reply->errorString()), true);
    });
#else
    connect(m_reply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::error), this, [this](QNetworkReply::NetworkError) {
        if (!m_reply || m_cancelled) {
            return;
        }
        finishWithError(QStringLiteral("Network error: %1").arg(m_reply->errorString()), true);
    });
#endif
    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        markProgressActivity();
        if (!ensureOutputFileOpen()) {
            return;
        }
        writeReplyData();
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 bytesReceived, qint64 bytesTotal) {
        markProgressActivity();
        if (bytesTotal > 0 && m_reply) {
            const qint64 remainingBytes = qMax<qint64>(0, bytesTotal - bytesReceived);
            if (!hasEnoughDiskSpace(remainingBytes + kDiskSpaceSafetyMarginBytes)) {
                finishWithError(diskSpaceErrorMessage(), true);
                return;
            }
        }
        emit progressChanged(bytesReceived, bytesTotal);
    });
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        if (!m_reply) {
            return;
        }

        if (m_outputFile) {
            if (!writeReplyData()) {
                return;
            }
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
    startTimeoutTimers();
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
#ifdef _WIN64
    const QString archSuffix = QStringLiteral("x64");
#else
    const QString archSuffix = QStringLiteral("x86");
#endif
    return QStringLiteral("LinDesk_Setup_%1_%2.exe").arg(info.version, archSuffix);
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

bool UpdateDownloader::ensureOutputFileOpen()
{
    if (!m_outputFile) {
        return false;
    }
    if (m_outputFile->isOpen()) {
        return true;
    }
    if (m_outputFile->open(QIODevice::WriteOnly)) {
        return true;
    }

    finishWithError(diskSpaceErrorMessage(), true);
    return false;
}

bool UpdateDownloader::hasEnoughDiskSpace(qint64 requiredBytes) const
{
    if (requiredBytes <= 0) {
        return true;
    }

    QStorageInfo storage(QFileInfo(m_currentFilePath).absolutePath());
    if (!storage.isValid() || !storage.isReady()) {
        return true;
    }
    return storage.bytesAvailable() >= requiredBytes;
}

QString UpdateDownloader::diskSpaceErrorMessage() const
{
    return QStringLiteral("Not enough disk space to save the update installer. Please free space on the drive that contains: %1")
        .arg(QFileInfo(m_currentFilePath).absolutePath());
}

bool UpdateDownloader::writeReplyData()
{
    if (!m_reply || !m_outputFile) {
        return false;
    }

    const QByteArray data = m_reply->readAll();
    if (data.isEmpty()) {
        return true;
    }

    const qint64 written = m_outputFile->write(data);
    if (written != data.size()) {
        finishWithError(diskSpaceErrorMessage(), true);
        return false;
    }
    return true;
}

void UpdateDownloader::startTimeoutTimers()
{
    markProgressActivity();
}

void UpdateDownloader::stopTimeoutTimers()
{
    m_stallTimeoutTimer.stop();
}

void UpdateDownloader::markProgressActivity()
{
    if (m_reply) {
        m_stallTimeoutTimer.start(kDownloadStallTimeoutMs);
    }
}

void UpdateDownloader::finishWithError(const QString& errorMessage, bool removeFile)
{
    stopTimeoutTimers();
    if (m_reply) {
        m_reply->abort();
    }
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
    stopTimeoutTimers();
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
