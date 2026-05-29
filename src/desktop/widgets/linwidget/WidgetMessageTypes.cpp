#include "desktop/widgets/linwidget/WidgetMessageTypes.h"

namespace LinWidget {

QJsonObject makeWidgetMessage(const QString& type, const QString& widgetId)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), type);
    object.insert(QStringLiteral("widgetId"), widgetId);
    return object;
}

}
