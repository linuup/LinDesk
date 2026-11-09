#include "desktop/widgets/linwidget/LinWidgetTypes.h"

#include <QJsonArray>
#include <QRegularExpression>

namespace LinWidget {
namespace {
bool requireString(const QJsonObject& object, const QString& key, bool required, QString* value, QString* errorMessage)
{
    const QJsonValue jsonValue = object.value(key);
    if (jsonValue.isUndefined() || jsonValue.isNull()) {
        if (required) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Widget manifest is missing %1.").arg(key);
            }
            return false;
        }
        value->clear();
        return true;
    }
    if (!jsonValue.isString()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget manifest field %1 must be a string.").arg(key);
        }
        return false;
    }
    *value = jsonValue.toString().trimmed();
    if (required && value->isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget manifest field %1 must not be empty.").arg(key);
        }
        return false;
    }
    return true;
}

bool readInt(const QJsonObject& object, const QString& key, int defaultValue, int* value, QString* errorMessage)
{
    const QJsonValue jsonValue = object.value(key);
    if (jsonValue.isUndefined() || jsonValue.isNull()) {
        *value = defaultValue;
        return true;
    }
    if (!jsonValue.isDouble()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget manifest field %1 must be a number.").arg(key);
        }
        return false;
    }
    const double number = jsonValue.toDouble();
    if (number != static_cast<int>(number)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget manifest field %1 must be an integer.").arg(key);
        }
        return false;
    }
    *value = static_cast<int>(number);
    return true;
}

bool readBool(const QJsonObject& object, const QString& key, bool defaultValue, bool* value, QString* errorMessage)
{
    const QJsonValue jsonValue = object.value(key);
    if (jsonValue.isUndefined() || jsonValue.isNull()) {
        *value = defaultValue;
        return true;
    }
    if (!jsonValue.isBool()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget manifest field %1 must be a boolean.").arg(key);
        }
        return false;
    }
    *value = jsonValue.toBool();
    return true;
}

bool isSafeRelativePath(const QString& path)
{
    if (path.isEmpty() || path.startsWith(QLatin1Char('/')) || path.startsWith(QLatin1Char('\\'))) {
        return false;
    }
    if (path.contains(QLatin1Char(':')) || path.contains(QLatin1Char('\0'))) {
        return false;
    }
    const QStringList parts = path.split(QRegularExpression(QStringLiteral("[/\\\\]")), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        if (part == QStringLiteral(".") || part == QStringLiteral("..")) {
            return false;
        }
    }
    return true;
}

QString normalizeSettingFieldType(const QString& rawType, bool hasEnumOptions)
{
    const QString lowered = rawType.trimmed().toLower();
    if (lowered == QStringLiteral("bool") || lowered == QStringLiteral("boolean") || lowered == QStringLiteral("switch")) {
        return QStringLiteral("boolean");
    }
    if (lowered == QStringLiteral("int") || lowered == QStringLiteral("integer")) {
        return QStringLiteral("integer");
    }
    if (lowered == QStringLiteral("number") || lowered == QStringLiteral("float") || lowered == QStringLiteral("double")) {
        return QStringLiteral("number");
    }
    if (lowered == QStringLiteral("select") || lowered == QStringLiteral("enum") || (hasEnumOptions && lowered == QStringLiteral("string"))) {
        return QStringLiteral("select");
    }
    if (lowered == QStringLiteral("color") || lowered == QStringLiteral("colour")) {
        return QStringLiteral("color");
    }
    if (lowered == QStringLiteral("string") || lowered == QStringLiteral("text") || lowered.isEmpty()) {
        return hasEnumOptions ? QStringLiteral("select") : QStringLiteral("string");
    }
    return QString();
}

bool parseSingleSettingField(const QJsonObject& itemObj, const QString& fallbackKey, WidgetSettingField* outField, QString* errorMessage)
{
    if (!outField) {
        return false;
    }
    WidgetSettingField field;
    field.key = itemObj.value(QStringLiteral("key")).toString().trimmed();
    if (field.key.isEmpty()) {
        field.key = itemObj.value(QStringLiteral("id")).toString().trimmed();
    }
    if (field.key.isEmpty()) {
        field.key = fallbackKey.trimmed();
    }
    if (field.key.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget manifest settingsSchema field is missing key.");
        }
        return false;
    }

    const QJsonValue optionsVal = itemObj.contains(QStringLiteral("options"))
        ? itemObj.value(QStringLiteral("options"))
        : itemObj.value(QStringLiteral("enum"));
    if (optionsVal.isArray()) {
        const QJsonArray optArray = optionsVal.toArray();
        const QJsonArray enumNames = itemObj.value(QStringLiteral("enumNames")).toArray();
        for (int i = 0; i < optArray.size(); ++i) {
            const QJsonValue& opt = optArray.at(i);
            WidgetSettingOption option;
            if (opt.isObject()) {
                const QJsonObject optObj = opt.toObject();
                option.value = optObj.value(QStringLiteral("value")).toVariant().toString();
                option.label = optObj.value(QStringLiteral("label")).toString(option.value);
            } else {
                option.value = opt.toVariant().toString();
                if (i < enumNames.size() && enumNames.at(i).isString()) {
                    option.label = enumNames.at(i).toString();
                } else {
                    option.label = option.value;
                }
            }
            if (!option.value.isEmpty()) {
                field.options.append(option);
            }
        }
    }

    const QString rawType = itemObj.value(QStringLiteral("type")).toString();
    field.type = normalizeSettingFieldType(rawType, !field.options.isEmpty());
    if (field.type.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget manifest settingsSchema field '%1' has unsupported type '%2'.").arg(field.key, rawType);
        }
        return false;
    }

    field.title = itemObj.value(QStringLiteral("title")).toString().trimmed();
    if (field.title.isEmpty()) {
        field.title = itemObj.value(QStringLiteral("label")).toString().trimmed();
    }
    if (field.title.isEmpty()) {
        field.title = field.key;
    }
    field.description = itemObj.value(QStringLiteral("description")).toString().trimmed();
    field.defaultValue = itemObj.contains(QStringLiteral("default"))
        ? itemObj.value(QStringLiteral("default"))
        : itemObj.value(QStringLiteral("defaultValue"));

    const QJsonValue minVal = itemObj.contains(QStringLiteral("minimum"))
        ? itemObj.value(QStringLiteral("minimum"))
        : itemObj.value(QStringLiteral("min"));
    if (minVal.isDouble()) {
        field.minimum = minVal.toDouble();
        field.hasMin = true;
    }

    const QJsonValue maxVal = itemObj.contains(QStringLiteral("maximum"))
        ? itemObj.value(QStringLiteral("maximum"))
        : itemObj.value(QStringLiteral("max"));
    if (maxVal.isDouble()) {
        field.maximum = maxVal.toDouble();
        field.hasMax = true;
    }

    if (field.hasMin && field.hasMax && field.maximum < field.minimum) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget manifest settingsSchema field '%1' has maximum less than minimum.").arg(field.key);
        }
        return false;
    }

    *outField = field;
    return true;
}

bool parseSettingsSchema(const QJsonValue& schemaValue, QVector<WidgetSettingField>* outSchema, QString* errorMessage)
{
    if (!outSchema) {
        return false;
    }
    outSchema->clear();
    if (schemaValue.isUndefined() || schemaValue.isNull()) {
        return true;
    }

    if (schemaValue.isArray()) {
        const QJsonArray arr = schemaValue.toArray();
        for (const QJsonValue& item : arr) {
            if (!item.isObject()) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Widget manifest settingsSchema array items must be objects.");
                }
                return false;
            }
            WidgetSettingField field;
            if (!parseSingleSettingField(item.toObject(), QString(), &field, errorMessage)) {
                return false;
            }
            outSchema->append(field);
        }
        return true;
    }

    if (schemaValue.isObject()) {
        const QJsonObject rootObj = schemaValue.toObject();
        const QJsonObject propsObj = rootObj.value(QStringLiteral("properties")).isObject()
            ? rootObj.value(QStringLiteral("properties")).toObject()
            : rootObj;
        for (auto it = propsObj.begin(); it != propsObj.end(); ++it) {
            if (it.key() == QStringLiteral("type") && it.value().isString()) {
                continue;
            }
            if (!it.value().isObject()) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Widget manifest settingsSchema property '%1' must be an object.").arg(it.key());
                }
                return false;
            }
            WidgetSettingField field;
            if (!parseSingleSettingField(it.value().toObject(), it.key(), &field, errorMessage)) {
                return false;
            }
            outSchema->append(field);
        }
        return true;
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("Widget manifest field settingsSchema must be an array or object.");
    }
    return false;
}
}

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
    object.insert(QStringLiteral("role"), manifest.role);
    if (manifest.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0) {
        object.insert(QStringLiteral("wallpaperType"), manifest.wallpaperType);
    }
    object.insert(QStringLiteral("transparent"), manifest.transparent);
    object.insert(QStringLiteral("resizable"), manifest.resizable);
    object.insert(QStringLiteral("interactive"), manifest.interactive);
    object.insert(QStringLiteral("autoLoad"), manifest.autoLoad);
    object.insert(QStringLiteral("multiInstance"), manifest.multiInstance);
    object.insert(QStringLiteral("entry"), manifest.entry);
    object.insert(QStringLiteral("settings"), manifest.settings);
    object.insert(QStringLiteral("icon"), manifest.icon);
    object.insert(QStringLiteral("permissions"), permissions);

    if (!manifest.settingsSchema.isEmpty()) {
        QJsonArray schemaArray;
        for (const WidgetSettingField& field : manifest.settingsSchema) {
            QJsonObject fieldObj;
            fieldObj.insert(QStringLiteral("key"), field.key);
            fieldObj.insert(QStringLiteral("type"), field.type);
            fieldObj.insert(QStringLiteral("title"), field.title);
            if (!field.description.isEmpty()) {
                fieldObj.insert(QStringLiteral("description"), field.description);
            }
            if (!field.defaultValue.isUndefined()) {
                fieldObj.insert(QStringLiteral("default"), field.defaultValue);
            }
            if (field.hasMin) {
                fieldObj.insert(QStringLiteral("minimum"), field.minimum);
            }
            if (field.hasMax) {
                fieldObj.insert(QStringLiteral("maximum"), field.maximum);
            }
            if (!field.options.isEmpty()) {
                QJsonArray opts;
                for (const WidgetSettingOption& opt : field.options) {
                    QJsonObject optObj;
                    optObj.insert(QStringLiteral("label"), opt.label);
                    optObj.insert(QStringLiteral("value"), opt.value);
                    opts.append(optObj);
                }
                fieldObj.insert(QStringLiteral("options"), opts);
            }
            schemaArray.append(fieldObj);
        }
        object.insert(QStringLiteral("settingsSchema"), schemaArray);
    }
    return object;
}

bool manifestFromJson(const QJsonObject& object, WidgetManifest* manifest, QString* errorMessage)
{
    if (!manifest) {
        return false;
    }

    WidgetManifest parsed;
    if (!requireString(object, QStringLiteral("id"), true, &parsed.id, errorMessage) ||
        !requireString(object, QStringLiteral("name"), false, &parsed.name, errorMessage) ||
        !requireString(object, QStringLiteral("version"), false, &parsed.version, errorMessage) ||
        !requireString(object, QStringLiteral("author"), false, &parsed.author, errorMessage) ||
        !requireString(object, QStringLiteral("description"), false, &parsed.description, errorMessage) ||
        !requireString(object, QStringLiteral("entry"), false, &parsed.entry, errorMessage) ||
        !requireString(object, QStringLiteral("settings"), false, &parsed.settings, errorMessage) ||
        !requireString(object, QStringLiteral("role"), false, &parsed.role, errorMessage) ||
        !requireString(object, QStringLiteral("icon"), false, &parsed.icon, errorMessage)) {
        return false;
    }
    QString typeText;
    if (!requireString(object, QStringLiteral("type"), false, &typeText, errorMessage)) {
        return false;
    }
    // New 2.0 standard: type can be "widget" or "wallpaper"
    // Legacy standard: role is "widget" | "wallpaper", type is "html" | "native"
    if (typeText.compare(QStringLiteral("widget"), Qt::CaseInsensitive) == 0 ||
        typeText.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0) {
        parsed.role = typeText.toLower();
        parsed.type = RuntimeType::Html;
    } else if (!typeText.isEmpty()) {
        if (typeText.compare(QStringLiteral("html"), Qt::CaseInsensitive) != 0 &&
            typeText.compare(QStringLiteral("native"), Qt::CaseInsensitive) != 0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Widget manifest field type must be widget, wallpaper, html, or native.");
            }
            return false;
        }
        parsed.type = runtimeTypeFromString(typeText);
    } else {
        parsed.type = RuntimeType::Html;
    }

    if (parsed.role.isEmpty()) {
        parsed.role = QStringLiteral("widget");
    }
    if (parsed.role.compare(QStringLiteral("widget"), Qt::CaseInsensitive) != 0 &&
        parsed.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) != 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget manifest field role must be widget or wallpaper.");
        }
        return false;
    }

    QString wpType;
    if (requireString(object, QStringLiteral("wallpaperType"), false, &wpType, errorMessage)) {
        if (!wpType.isEmpty()) {
            wpType = wpType.toLower();
            if (wpType == QStringLiteral("web") || wpType == QStringLiteral("video") || wpType == QStringLiteral("picture")) {
                parsed.wallpaperType = wpType;
            }
        }
    }

    if (!readInt(object, QStringLiteral("width"), 300, &parsed.width, errorMessage) ||
        !readInt(object, QStringLiteral("height"), 120, &parsed.height, errorMessage) ||
        !readInt(object, QStringLiteral("minWidth"), 120, &parsed.minWidth, errorMessage) ||
        !readInt(object, QStringLiteral("minHeight"), 80, &parsed.minHeight, errorMessage) ||
        !readInt(object, QStringLiteral("maxWidth"), 0, &parsed.maxWidth, errorMessage) ||
        !readInt(object, QStringLiteral("maxHeight"), 0, &parsed.maxHeight, errorMessage) ||
        !readBool(object, QStringLiteral("transparent"), true, &parsed.transparent, errorMessage) ||
        !readBool(object, QStringLiteral("resizable"), true, &parsed.resizable, errorMessage) ||
        !readBool(object, QStringLiteral("interactive"), parsed.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0, &parsed.interactive, errorMessage) ||
        !readBool(object, QStringLiteral("autoLoad"), false, &parsed.autoLoad, errorMessage) ||
        !readBool(object, QStringLiteral("multiInstance"), false, &parsed.multiInstance, errorMessage)) {
        return false;
    }
    if (parsed.entry.isEmpty()) {
        parsed.entry = QStringLiteral("index.html");
    }

    const QJsonValue permissionsValue = object.value(QStringLiteral("permissions"));
    if (!permissionsValue.isUndefined() && !permissionsValue.isNull() && !permissionsValue.isArray()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget manifest field permissions must be an array.");
        }
        return false;
    }
    const QJsonArray permissions = permissionsValue.toArray();
    for (const QJsonValue& value : permissions) {
        if (!value.isString()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Widget manifest permissions must contain strings only.");
            }
            return false;
        }
        const QString permission = value.toString().trimmed().toLower();
        if (permission != QStringLiteral("network") &&
            permission != QStringLiteral("storage") &&
            permission != QStringLiteral("desktop") &&
            permission != QStringLiteral("audio")) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Widget manifest permission is not supported: %1.").arg(permission);
            }
            return false;
        }
        if (!permission.isEmpty() && !parsed.permissions.contains(permission)) {
            parsed.permissions.append(permission);
        }
    }

    if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{1,127}$")).match(parsed.id).hasMatch()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget manifest id must be 2 to 128 characters and use letters, numbers, dots, underscores, or hyphens.");
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
    if (parsed.minWidth <= 0 || parsed.minHeight <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget manifest minWidth and minHeight must be greater than zero.");
        }
        return false;
    }
    if (parsed.maxWidth < 0 || parsed.maxHeight < 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget manifest maxWidth and maxHeight must be zero or greater.");
        }
        return false;
    }
    if (parsed.maxWidth > 0 && parsed.maxWidth < parsed.minWidth) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget manifest maxWidth must be greater than or equal to minWidth.");
        }
        return false;
    }
    if (parsed.maxHeight > 0 && parsed.maxHeight < parsed.minHeight) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget manifest maxHeight must be greater than or equal to minHeight.");
        }
        return false;
    }
    if (parsed.width < parsed.minWidth || parsed.height < parsed.minHeight) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget manifest width and height must be greater than or equal to minWidth and minHeight.");
        }
        return false;
    }
    if ((parsed.maxWidth > 0 && parsed.width > parsed.maxWidth) ||
        (parsed.maxHeight > 0 && parsed.height > parsed.maxHeight)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget manifest width and height must not exceed maxWidth or maxHeight.");
        }
        return false;
    }
    if (!isSafeRelativePath(parsed.entry)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget manifest entry must be a safe relative path.");
        }
        return false;
    }
    if (!parsed.settings.isEmpty() && !isSafeRelativePath(parsed.settings)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget manifest settings must be a safe relative path.");
        }
        return false;
    }
    if (!parsed.icon.isEmpty() && !isSafeRelativePath(parsed.icon)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget manifest icon must be a safe relative path.");
        }
        return false;
    }
    if (!parseSettingsSchema(object.value(QStringLiteral("settingsSchema")), &parsed.settingsSchema, errorMessage)) {
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
    object.insert(QStringLiteral("registryId"), state.registryId);
    object.insert(QStringLiteral("geometry"), geometry);
    object.insert(QStringLiteral("zOrder"), state.zOrder);
    object.insert(QStringLiteral("visible"), state.visible);
    object.insert(QStringLiteral("isDevelopment"), state.isDevelopment);
    object.insert(QStringLiteral("createdAt"), state.createdAt);
    object.insert(QStringLiteral("parentInstanceId"), state.parentInstanceId);
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
    parsed.registryId = object.value(QStringLiteral("registryId")).toString().trimmed().toLower();
    parsed.geometry = QRect(
        geometry.value(QStringLiteral("x")).toInt(),
        geometry.value(QStringLiteral("y")).toInt(),
        geometry.value(QStringLiteral("width")).toInt(),
        geometry.value(QStringLiteral("height")).toInt());
    parsed.zOrder = object.value(QStringLiteral("zOrder")).toInt();
    parsed.visible = object.value(QStringLiteral("visible")).toBool(true);
    parsed.isDevelopment = object.value(QStringLiteral("isDevelopment")).toBool(parsed.instanceId.startsWith(QStringLiteral("dev:")));
    parsed.createdAt = object.value(QStringLiteral("createdAt")).toString().trimmed();
    parsed.parentInstanceId = object.value(QStringLiteral("parentInstanceId")).toString().trimmed();
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
    object.insert(QStringLiteral("icon"), entry.icon);
    object.insert(QStringLiteral("packagePath"), entry.packagePath);
    object.insert(QStringLiteral("runtimeType"), entry.runtimeType);
    object.insert(QStringLiteral("role"), entry.role);
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
    parsed.icon = object.value(QStringLiteral("icon")).toString().trimmed();
    parsed.packagePath = object.value(QStringLiteral("packagePath")).toString().trimmed();
    parsed.runtimeType = object.value(QStringLiteral("runtimeType")).toString(QStringLiteral("html")).trimmed();
    parsed.role = object.value(QStringLiteral("role")).toString(QStringLiteral("widget")).trimmed();
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
