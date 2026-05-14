#include "services/update/UpdateChecker.h"

#include "Version.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QNetworkRequest>
#include <QUrl>

namespace {
constexpr const char* kUpdateJsonUrl = "https://github.com/linuup/LinDesk_Update/releases/latest/download/update.json";

QString requiredString(const QJsonObject& object, const char* key)
{
    const QJsonValue value = object.value(QString::fromLatin1(key));
    return value.isString() ? value.toString().trimmed() : QString();
}
}

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<UpdateInfo>("UpdateInfo");
}

bool UpdateChecker::isChecking() const
{
    return m_reply != nullptr;
}

void UpdateChecker::checkForUpdates()
{
    if (m_reply) {
        return;
    }

    QNetworkRequest request(QUrl(QString::fromLatin1(kUpdateJsonUrl)));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("LinDesk/%1").arg(QStringLiteral(LINDESK_VERSION_STRING)));
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    m_reply = m_network.get(request);
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply* reply = m_reply;
        m_reply = nullptr;
        if (!reply) {
            emit checkFailed(tr("Update request was cancelled."));
            return;
        }

        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit checkFailed(reply->errorString());
            return;
        }

        UpdateInfo info;
        QString errorMessage;
        if (!parseUpdateInfo(reply->readAll(), info, errorMessage)) {
            emit checkFailed(errorMessage);
            return;
        }

        const bool updateAvailable =
            info.channel.compare(QStringLiteral("stable"), Qt::CaseInsensitive) == 0 &&
            compareVersions(info.version, QStringLiteral(LINDESK_VERSION_STRING)) > 0;
        emit checkSucceeded(info, updateAvailable);
    });
}

int UpdateChecker::compareVersions(const QString& left, const QString& right)
{
    const QStringList leftParts = left.split('.');
    const QStringList rightParts = right.split('.');
    const int partCount = qMax(leftParts.size(), rightParts.size());
    for (int i = 0; i < partCount; ++i) {
        bool leftOk = false;
        bool rightOk = false;
        const int leftValue = i < leftParts.size() ? leftParts.at(i).toInt(&leftOk) : 0;
        const int rightValue = i < rightParts.size() ? rightParts.at(i).toInt(&rightOk) : 0;
        if (!leftOk && i < leftParts.size()) {
            return QString::compare(left, right, Qt::CaseInsensitive);
        }
        if (!rightOk && i < rightParts.size()) {
            return QString::compare(left, right, Qt::CaseInsensitive);
        }
        if (leftValue < rightValue) {
            return -1;
        }
        if (leftValue > rightValue) {
            return 1;
        }
    }
    return 0;
}

bool UpdateChecker::parseUpdateInfo(const QByteArray& data, UpdateInfo& info, QString& errorMessage) const
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        errorMessage = tr("Invalid update metadata.");
        return false;
    }

    const QJsonObject object = document.object();
    info.version = requiredString(object, "version");
    info.channel = requiredString(object, "channel");
    info.url = requiredString(object, "url");
    info.sha256 = requiredString(object, "sha256").toLower();
    info.notes = requiredString(object, "notes");
    info.publishedAt = requiredString(object, "publishedAt");
    info.minSupportedVersion = requiredString(object, "minSupportedVersion");

    if (info.version.isEmpty() || info.channel.isEmpty() || info.url.isEmpty() ||
        info.sha256.isEmpty() || info.notes.isEmpty() ||
        info.publishedAt.isEmpty() || info.minSupportedVersion.isEmpty()) {
        errorMessage = tr("Update metadata is missing required fields.");
        return false;
    }

    if (!QUrl(info.url).isValid()) {
        errorMessage = tr("Update installer URL is invalid.");
        return false;
    }

    return true;
}
