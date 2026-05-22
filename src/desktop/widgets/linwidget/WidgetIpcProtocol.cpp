#include "desktop/widgets/linwidget/WidgetIpcProtocol.h"

#include <QJsonDocument>

namespace LinWidget {

QByteArray encodeIpcMessage(const QJsonObject& object)
{
    QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    payload.append('\n');
    return payload;
}

bool decodeIpcMessage(const QByteArray& line, QJsonObject* object, QString* errorMessage)
{
    const QJsonDocument document = QJsonDocument::fromJson(line.trimmed());
    if (!document.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("IPC message must be a JSON object.");
        }
        return false;
    }
    if (object) {
        *object = document.object();
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

QJsonObject makeIpcMessage(const QString& type, const QString& widgetId)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), type);
    object.insert(QStringLiteral("widgetId"), widgetId);
    return object;
}

}
