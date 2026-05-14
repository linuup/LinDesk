#pragma once

#include <QMetaType>
#include <QString>

struct UpdateInfo {
    QString version;
    QString channel;
    QString url;
    QString sha256;
    QString notes;
    QString publishedAt;
    QString minSupportedVersion;
};

Q_DECLARE_METATYPE(UpdateInfo)
