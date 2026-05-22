#pragma once

#include "desktop/widgets/linwidget/LinWidgetTypes.h"

#include <QString>
#include <QVector>

namespace LinWidget {

class ConfigManager {
public:
    static QString defaultConfigPath();
    static bool saveLayout(const QString& path,
                           const QVector<WidgetLayoutState>& states,
                           QString* errorMessage = nullptr);
    static bool loadLayout(const QString& path,
                           QVector<WidgetLayoutState>* states,
                           QString* errorMessage = nullptr);
};

}
