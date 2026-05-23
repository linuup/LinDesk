#pragma once

#include <QString>

namespace LinWidget {

struct WidgetProjectMetadata {
    QString id;
    QString name;
    QString author;
    QString description;
    int width = 320;
    int height = 160;
    bool transparent = true;
    bool resizable = true;
};

class LinWidgetProjectTools {
public:
    static bool createProject(const QString& targetDir,
                              const WidgetProjectMetadata& metadata,
                              QString* errorMessage = nullptr);
    static bool validatePackage(const QString& packagePath, QString* errorMessage = nullptr);
    static bool exportProjectZip(const QString& packagePath,
                                 const QString& outputZipPath,
                                 QString* errorMessage = nullptr);
};

}
