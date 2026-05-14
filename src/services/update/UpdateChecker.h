#pragma once

#include "services/update/UpdateTypes.h"

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class UpdateChecker : public QObject {
    Q_OBJECT

public:
    explicit UpdateChecker(QObject* parent = nullptr);

    void checkForUpdates();
    bool isChecking() const;

    static int compareVersions(const QString& left, const QString& right);

signals:
    void checkSucceeded(const UpdateInfo& info, bool updateAvailable);
    void checkFailed(const QString& errorMessage);

private:
    bool parseUpdateInfo(const QByteArray& data, UpdateInfo& info, QString& errorMessage) const;

    QNetworkAccessManager m_network;
    QNetworkReply* m_reply = nullptr;
};

