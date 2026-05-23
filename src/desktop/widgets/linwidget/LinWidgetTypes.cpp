#include "desktop/widgets/linwidget/LinWidgetTypes.h"

#include <QJsonArray>

namespace LinWidget {

QString runtimeTypeToString(RuntimeType type)
{
    return type == RuntimeType::Native ? QStringLiteral("native") : QStringLiteral("html");
}

RuntimeType runtimeTypeFromString(const QString& value)
{
    return value.compare(QStringLiteral("native"), Qt::CaseInsensitive) == 0
        ? RuntimeType::Native
        : RuntimeType::Html;
}

QJsonObject manifestToJson(const WidgetManifest& manifest)
{
    QJsonArray permissions;
    for (const QString& permission : manifest.permissions) {
        permissions.append(permission);
    }

    QJsonObject object;
    object.insert(QStringLiteral("id"), manifest.id);
    object.insert(QStringLiteral("name"), manifest.name);
    object.insert(QStringLiteral("version"), manifest.version);
    object.insert(QStringLiteral("author"), manifest.author);
    object.insert(QStringLiteral("description"), manifest.description);
    object.insert(QStringLiteral("width"), manifest.width);
    object.insert(QStringLiteral("height"), manifest.height);
    object.insert(QStringLiteral("minWidth"), manifest.minWidth);
    object.insert(QStringLiteral("minHeight"), manifest.minHeight);
    object.insert(QStringLiteral("maxWidth"), manifest.maxWidth);
    object.insert(QStringLiteral("maxHeight"), manifest.maxHeight);
    object.insert(QStringLiteral("type"), runtimeTypeToString(manifest.type));
    object.insert(QStringLiteral("transparent"), manifest.transparent);
    object.insert(QStringLiteral("resizable"), manifest.resizable);
    object.insert(QStringLiteral("autoLoad"), manifest.autoLoad);
    object.insert(QStringLiteral("entry"), manifest.entry);
    object.insert(QStringLiteral("settings"), manifest.settings);
    object.insert(QStringLiteral("permissions"), permissions);
    return object;
}

bool manifestFromJson(const QJsonObject& object, WidgetManifest* manifest, QString* errorMessage)
{
    if (!manifest) {
        return false;
    }

    WidgetManifest parsed;
    parsed.id = object.value(QStringLiteral("id")).toString().trimmed();
    parsed.name = object.value(QStringLiteral("name")).toString().trimmed();
    parsed.version = object.value(QStringLiteral("version")).toString().trimmed();
    parsed.author = object.value(QStringLiteral("author")).toString().trimmed();
    parsed.description = object.value(QStringLiteral("description")).toString().trimmed();
    parsed.type = runtimeTypeFromString(object.value(QStringLiteral("type")).toString(QStringLiteral("html")).trimmed());
    parsed.width = object.value(QStringLiteral("width")).toInt(300);
    parsed.height = object.value(QStringLiteral("height")).toInt(120);
    parsed.minWidth = object.value(QStringLiteral("minWidth")).toInt(120);
    parsed.minHeight = object.value(QStringLiteral("minHeight")).toInt(80);
    parsed.maxWidth = object.value(QStringLiteral("maxWidth")).toInt(0);
    parsed.maxHeight = object.value(QStringLiteral("maxHeight")).toInt(0);
    parsed.transparent = object.value(QStringLiteral("transparent")).toBool(true);
    parsed.resizable = object.value(QStringLiteral("resizable")).toBool(true);
    parsed.autoLoad = object.value(QStringLiteral("autoLoad")).toBool(false);
    parsed.entry = object.value(QStringLiteral("entry")).toString(QStringLiteral("index.html")).trimmed();
    parsed.settings = object.value(QStringLiteral("settings")).toString().trimmed();

    const QJsonArray permissions = object.value(QStringLiteral("permissions")).toArray();
    for (const QJsonValue& value : permissions) {
        const QString permission = value.toString().trimmed().toLower();
        if (!permission.isEmpty() && !parsed.permissions.contains(permission)) {
            parsed.permissions.append(permission);
        }
    }

    if (parsed.id.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget manifest is missing id.");
        }
        return false;
    }
    if (parsed.name.isEmpty()) {
        parsed.name = parsed.id;
    }
    if (parsed.version.isEmpty()) {
        parsed.version = QStringLiteral("1.0.0");
    }
    if (parsed.width <= 0 || parsed.height <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget manifest size must be greater than zero.");
        }
        return false;
    }
    parsed.minWidth = qMax(1, parsed.minWidth);
    parsed.minHeight = qMax(1, parsed.minHeight);
    if (parsed.maxWidth > 0 && parsed.maxWidth < parsed.minWidth) {
        parsed.maxWidth = parsed.minWidth;
    }
    if (parsed.maxHeight > 0 && parsed.maxHeight < parsed.minHeight) {
        parsed.maxHeight = parsed.minHeight;
    }
    parsed.width = qMax(parsed.width, parsed.minWidth);
    parsed.height = qMax(parsed.height, parsed.minHeight);
    if (parsed.maxWidth > 0) {
        parsed.width = qMin(parsed.width, parsed.maxWidth);
    }
    if (parsed.maxHeight > 0) {
        parsed.height = qMin(parsed.height, parsed.maxHeight);
    }
    if (parsed.entry.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget manifest is missing entry.");
        }
        return false;
    }

    *manifest = parsed;
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

QJsonObject layoutStateToJson(const WidgetLayoutState& state)
{
    QJsonObject geometry;
    geometry.insert(QStringLiteral("x"), state.geometry.x());
    geometry.insert(QStringLiteral("y"), state.geometry.y());
    geometry.insert(QStringLiteral("width"), state.geometry.width());
    geometry.insert(QStringLiteral("height"), state.geometry.height());

    QJsonObject object;
    object.insert(QStringLiteral("instanceId"), state.instanceId);
    object.insert(QStringLiteral("packagePath"), state.packagePath);
    object.insert(QStringLiteral("geometry"), geometry);
    object.insert(QStringLiteral("zOrder"), state.zOrder);
    object.insert(QStringLiteral("visible"), state.visible);
    return object;
}

bool layoutStateFromJson(const QJsonObject& object, WidgetLayoutState* state)
{
    if (!state) {
        return false;
    }

    const QJsonObject geometry = object.value(QStringLiteral("geometry")).toObject();
    WidgetLayoutState parsed;
    parsed.instanceId = object.value(QStringLiteral("instanceId")).toString().trimmed();
    parsed.packagePath = object.value(QStringLiteral("packagePath")).toString().trimmed();
    parsed.geometry = QRect(
        geometry.value(QStringLiteral("x")).toInt(),
        geometry.value(QStringLiteral("y")).toInt(),
        geometry.value(QStringLiteral("width")).toInt(),
        geometry.value(QStringLiteral("height")).toInt());
    parsed.zOrder = object.value(QStringLiteral("zOrder")).toInt();
    parsed.visible = object.value(QStringLiteral("visible")).toBool(true);
    if (parsed.instanceId.isEmpty() || parsed.packagePath.isEmpty() || !parsed.geometry.isValid()) {
        return false;
    }

    *state = parsed;
    return true;
}

QJsonObject registryEntryToJson(const WidgetRegistryEntry& entry)
{
    QJsonArray permissions;
    for (const QString& permission : entry.permissionsGranted) {
        permissions.append(permission);
    }

    QJsonObject object;
    object.insert(QStringLiteral("id"), entry.id);
    object.insert(QStringLiteral("name"), entry.name);
    object.insert(QStringLiteral("version"), entry.version);
    object.insert(QStringLiteral("author"), entry.author);
    object.insert(QStringLiteral("description"), entry.description);
    object.insert(QStringLiteral("packagePath"), entry.packagePath);
    object.insert(QStringLiteral("runtimeType"), entry.runtimeType);
    object.insert(QStringLiteral("defaultWidth"), entry.defaultSize.width());
    object.insert(QStringLiteral("defaultHeight"), entry.defaultSize.height());
    object.insert(QStringLiteral("isDevelopment"), entry.isDevelopment);
    object.insert(QStringLiteral("sourcePath"), entry.sourcePath);
    object.insert(QStringLiteral("autoReload"), entry.autoReload);
    object.insert(QStringLiteral("autoLoad"), entry.autoLoad);
    object.insert(QStringLiteral("enabled"), entry.enabled);
    object.insert(QStringLiteral("trusted"), entry.trusted);
    object.insert(QStringLiteral("installedAt"), entry.installedAt);
    object.insert(QStringLiteral("updatedAt"), entry.updatedAt);
    object.insert(QStringLiteral("lastRunState"), entry.lastRunState);
    object.insert(QStringLiteral("permissionsGranted"), permissions);
    return object;
}

bool registryEntryFromJson(const QJsonObject& object, WidgetRegistryEntry* entry)
{
    if (!entry) {
        return false;
    }

    WidgetRegistryEntry parsed;
    parsed.id = object.value(QStringLiteral("id")).toString().trimmed();
    parsed.name = object.value(QStringLiteral("name")).toString().trimmed();
    parsed.version = object.value(QStringLiteral("version")).toString().trimmed();
    parsed.author = object.value(QStringLiteral("author")).toString().trimmed();
    parsed.description = object.value(QStringLiteral("description")).toString().trimmed();
    parsed.packagePath = object.value(QStringLiteral("packagePath")).toString().trimmed();
    parsed.runtimeType = object.value(QStringLiteral("runtimeType")).toString(QStringLiteral("html")).trimmed();
    parsed.defaultSize = QSize(
        object.value(QStringLiteral("defaultWidth")).toInt(),
        object.value(QStringLiteral("defaultHeight")).toInt());
    parsed.isDevelopment = object.value(QStringLiteral("isDevelopment")).toBool(false);
    parsed.sourcePath = object.value(QStringLiteral("sourcePath")).toString().trimmed();
    parsed.autoReload = object.value(QStringLiteral("autoReload")).toBool(true);
    parsed.autoLoad = object.value(QStringLiteral("autoLoad")).toBool(false);
    parsed.enabled = object.value(QStringLiteral("enabled")).toBool(true);
    parsed.trusted = object.value(QStringLiteral("trusted")).toBool(false);
    parsed.installedAt = object.value(QStringLiteral("installedAt")).toString().trimmed();
    parsed.updatedAt = object.value(QStringLiteral("updatedAt")).toString().trimmed();
    parsed.lastRunState = object.value(QStringLiteral("lastRunState")).toString(QStringLiteral("stopped")).trimmed();

    const QJsonArray permissions = object.value(QStringLiteral("permissionsGranted")).toArray();
    for (const QJsonValue& value : permissions) {
        const QString permission = value.toString().trimmed().toLower();
        if (!permission.isEmpty() && !parsed.permissionsGranted.contains(permission)) {
            parsed.permissionsGranted.append(permission);
        }
    }

    if (parsed.id.isEmpty() || parsed.packagePath.isEmpty()) {
        return false;
    }
    if (!parsed.defaultSize.isValid()) {
        parsed.defaultSize = QSize(300, 120);
    }
    if (parsed.lastRunState.isEmpty()) {
        parsed.lastRunState = QStringLiteral("stopped");
    }

    *entry = parsed;
    return true;
}

}
