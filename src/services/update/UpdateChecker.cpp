#include "services/update/UpdateChecker.h"

#include "Version.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

namespace {
constexpr const char* kUpdateJsonUrl = "https://api.github.com/repos/linuup/LinDesk/releases/latest";
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
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
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
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray responseData = reply->readAll();

        if (reply->error() != QNetworkReply::NoError || (statusCode != 0 && statusCode != 200)) {
            QString errorMsg;
            if (!responseData.isEmpty()) {
                QJsonDocument errDoc = QJsonDocument::fromJson(responseData);
                if (errDoc.isObject() && errDoc.object().contains(QStringLiteral("message"))) {
                    errorMsg = errDoc.object().value(QStringLiteral("message")).toString();
                }
            }
            if (errorMsg.isEmpty()) {
                errorMsg = reply->errorString();
            }
            emit checkFailed(errorMsg);
            return;
        }

        UpdateInfo info;
        QString errorMessage;
        if (!parseUpdateInfo(responseData, info, errorMessage)) {
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

    const bool isDraft = object.value(QStringLiteral("draft")).toBool(false);
    if (isDraft) {
        errorMessage = tr("Draft release cannot be used for update.");
        return false;
    }

    QString tagName = object.value(QStringLiteral("tag_name")).toString().trimmed();
    if (tagName.startsWith('v', Qt::CaseInsensitive)) {
        tagName.remove(0, 1);
    }
    info.version = tagName;

    const bool isPrerelease = object.value(QStringLiteral("prerelease")).toBool(false);
    info.channel = isPrerelease ? QStringLiteral("prerelease") : QStringLiteral("stable");

    info.notes = object.value(QStringLiteral("body")).toString();
    info.publishedAt = object.value(QStringLiteral("published_at")).toString();

    static const QRegularExpression minVerRegex(
        QStringLiteral(R"(min(?:SupportedVersion)?[:\s=]+v?([0-9]+\.[0-9]+(?:\.[0-9]+)?))"),
        QRegularExpression::CaseInsensitiveOption);
    const auto matchMin = minVerRegex.match(info.notes);
    if (matchMin.hasMatch()) {
        info.minSupportedVersion = matchMin.captured(1);
    } else {
        info.minSupportedVersion.clear();
    }

    const QJsonArray assets = object.value(QStringLiteral("assets")).toArray();
    QJsonObject setupAsset;
#ifdef _WIN64
    const QString archSuffix = QStringLiteral("_x64.exe");
#else
    const QString archSuffix = QStringLiteral("_x86.exe");
#endif

    for (const QJsonValue& val : assets) {
        if (!val.isObject()) continue;
        const QJsonObject assetObj = val.toObject();
        const QString assetName = assetObj.value(QStringLiteral("name")).toString();
        if (assetName.startsWith(QStringLiteral("LinDesk_Setup_"), Qt::CaseInsensitive) &&
            assetName.endsWith(archSuffix, Qt::CaseInsensitive)) {
            setupAsset = assetObj;
            break;
        }
    }

    if (setupAsset.isEmpty()) {
        errorMessage = tr("Release asset for current architecture is missing.");
        return false;
    }

    info.url = setupAsset.value(QStringLiteral("browser_download_url")).toString();

    QString digest = setupAsset.value(QStringLiteral("digest")).toString().trimmed();
    if (digest.startsWith(QStringLiteral("sha256:"), Qt::CaseInsensitive)) {
        digest = digest.mid(7);
    }
    info.sha256 = digest.toLower();

    if (info.sha256.isEmpty()) {
        static const QRegularExpression shaRegex(QStringLiteral(R"(\b([a-fA-F0-9]{64})\b)"));
        const auto matchSha = shaRegex.match(info.notes);
        if (matchSha.hasMatch()) {
            info.sha256 = matchSha.captured(1).toLower();
        }
    }

    if (info.version.isEmpty() || info.channel.isEmpty() || info.url.isEmpty() ||
        info.notes.isEmpty() || info.publishedAt.isEmpty()) {
        errorMessage = tr("Update metadata is missing required fields.");
        return false;
    }

    if (!QUrl(info.url).isValid()) {
        errorMessage = tr("Update installer URL is invalid.");
        return false;
    }

    return true;
}
