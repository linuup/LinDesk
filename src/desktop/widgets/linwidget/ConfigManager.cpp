#include "desktop/widgets/linwidget/ConfigManager.h"

#include "services/AppPaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace LinWidget {

QString ConfigManager::defaultConfigPath()
{
    return AppPaths::linWidgetRegistryFilePath();
}

bool ConfigManager::saveLayout(const QString& path,
                               const QVector<WidgetLayoutState>& states,
                               QString* errorMessage)
{
    QJsonArray array;
    for (const WidgetLayoutState& state : states) {
        array.append(layoutStateToJson(state));
    }

    QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot create widget config directory.");
        }
        return false;
    }

    QJsonObject root;
    QFile existingFile(path);
    if (existingFile.exists() && existingFile.open(QIODevice::ReadOnly)) {
        const QJsonDocument existingDocument = QJsonDocument::fromJson(existingFile.readAll());
        if (existingDocument.isObject()) {
            root = existingDocument.object();
        }
    }

    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("layout"), array);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot save widget config: %1").arg(file.errorString());
        }
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool ConfigManager::loadLayout(const QString& path,
                               QVector<WidgetLayoutState>* states,
                               QString* errorMessage)
{
    if (!states) {
        return false;
    }

    QFile file(path);
    if (!file.exists()) {
        states->clear();
        if (errorMessage) {
            errorMessage->clear();
        }
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot open widget config: %1").arg(file.errorString());
        }
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    const QJsonArray array = document.object().value(QStringLiteral("layout")).toArray();
    QVector<WidgetLayoutState> parsed;
    for (const QJsonValue& value : array) {
        WidgetLayoutState state;
        if (layoutStateFromJson(value.toObject(), &state)) {
            parsed.append(state);
        }
    }

    *states = parsed;
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

}
