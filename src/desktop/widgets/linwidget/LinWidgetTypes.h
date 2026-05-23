#pragma once

#include <QJsonObject>
#include <QMap>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>

namespace LinWidget {

enum class RuntimeType {
    Native,
    Html
};

struct WidgetManifest {
    QString id;
    QString name;
    QString version;
    QString author;
    QString description;
    RuntimeType type = RuntimeType::Html;
    int width = 300;
    int height = 120;
    int minWidth = 120;
    int minHeight = 80;
    int maxWidth = 0;
    int maxHeight = 0;
    bool transparent = true;
    bool resizable = true;
    bool autoLoad = false;
    QString entry;
    QString settings;
    QStringList permissions;
};

struct WidgetInstance {
    QString instanceId;
    QString packagePath;
    WidgetManifest manifest;
    QStringList grantedPermissions;
    bool isDevelopment = false;
    bool autoReload = false;
    QRect geometry;
};

struct WidgetLayoutState {
    QString instanceId;
    QString packagePath;
    QRect geometry;
    int zOrder = 0;
    bool visible = true;
};

struct WidgetRegistryEntry {
    QString id;
    QString name;
    QString version;
    QString author;
    QString description;
    QString packagePath;
    QString runtimeType;
    QSize defaultSize;
    bool isDevelopment = false;
    QString sourcePath;
    bool autoReload = true;
    bool autoLoad = false;
    bool enabled = true;
    bool trusted = false;
    QString installedAt;
    QString updatedAt;
    QString lastRunState = QStringLiteral("stopped");
    QStringList permissionsGranted;
};

QString runtimeTypeToString(RuntimeType type);
RuntimeType runtimeTypeFromString(const QString& value);
QJsonObject manifestToJson(const WidgetManifest& manifest);
bool manifestFromJson(const QJsonObject& object, WidgetManifest* manifest, QString* errorMessage = nullptr);
QJsonObject layoutStateToJson(const WidgetLayoutState& state);
bool layoutStateFromJson(const QJsonObject& object, WidgetLayoutState* state);
QJsonObject registryEntryToJson(const WidgetRegistryEntry& entry);
bool registryEntryFromJson(const QJsonObject& object, WidgetRegistryEntry* entry);

}
