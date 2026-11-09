#pragma once

#include "desktop/widgets/linwidget/LinWidgetTypes.h"

#include <QString>

namespace LinWidget {

class WidgetPackage {
public:
    static bool loadManifest(const QString& packagePath,
                             WidgetManifest* manifest,
                             QString* errorMessage = nullptr);
    static QString manifestPath(const QString& packagePath);
    static QString entryPath(const QString& packagePath, const WidgetManifest& manifest);
    static bool installPackage(const QString& packageOrLwPath,
                               QString* installedTargetDir = nullptr,
                               QString* detectedType = nullptr,
                               QString* errorMessage = nullptr);
};

}
