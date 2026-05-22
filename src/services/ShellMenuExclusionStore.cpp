#include "services/ShellMenuExclusionStore.h"

#include "services/AppPaths.h"

#include <QByteArray>
#include <QDataStream>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QDir>
#include <QSettings>
#include <QStringList>
#include <algorithm>

namespace {
constexpr int kStoreVersion = 1;
constexpr quint32 kBinaryMagic = 0x4C444D45;
constexpr quint32 kBinaryVersion = 1;

struct RootCache
{
    QString path;
    QDateTime lastModified;
    qint64 size = -1;
    bool loaded = false;
    QJsonObject root;
};

RootCache& rootCache()
{
    static RootCache cache;
    return cache;
}

QString stringFromWide(const std::wstring& value)
{
    return QString::fromStdWString(value);
}

QString keyFromWide(const std::wstring& key)
{
    return stringFromWide(key);
}

QString encodeText(const QString& text)
{
    return QString::fromLatin1(text.toUtf8().toBase64());
}

QJsonArray encodeStringList(const std::vector<std::wstring>& values)
{
    QJsonArray array;
    for (const std::wstring& value : values) {
        array.append(encodeText(stringFromWide(value)));
    }
    return array;
}

bool decodeText(const QJsonValue& value, QString* out)
{
    if (!value.isString() || !out) {
        return false;
    }

    const QByteArray encoded = value.toString().toLatin1();
    const QByteArray decoded = QByteArray::fromBase64(encoded);
    if (encoded.isEmpty() && !value.toString().isEmpty()) {
        return false;
    }

    *out = QString::fromUtf8(decoded);
    return true;
}

std::vector<std::wstring> decodeStringList(const QJsonValue& value)
{
    std::vector<std::wstring> values;
    if (!value.isArray()) {
        return values;
    }

    const QJsonArray array = value.toArray();
    values.reserve(static_cast<size_t>(array.size()));
    for (const QJsonValue& item : array) {
        QString decoded;
        if (decodeText(item, &decoded)) {
            values.push_back(decoded.toStdWString());
        }
    }
    return values;
}

QJsonObject readRootObjectFromDisk(const QString& path)
{
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QByteArray jsonBytes;
#ifdef QT_DEBUG
    jsonBytes = file.readAll();
#else
    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_5_15);
    quint32 magic = 0;
    quint32 version = 0;
    in >> magic >> version >> jsonBytes;
    if (in.status() != QDataStream::Ok || magic != kBinaryMagic || version != kBinaryVersion) {
        return {};
    }
#endif

    const QJsonDocument document = QJsonDocument::fromJson(jsonBytes);
    return document.isObject() ? document.object() : QJsonObject();
}

QJsonObject readRootObject()
{
    const QString path = AppPaths::shellMenuExclusionsFilePath();
    const QFileInfo info(path);
    RootCache& cache = rootCache();
    if (!info.exists()) {
        cache.path = path;
        cache.lastModified = QDateTime();
        cache.size = -1;
        cache.loaded = true;
        cache.root = QJsonObject();
        return {};
    }

    if (cache.loaded &&
        cache.path == path &&
        cache.lastModified == info.lastModified() &&
        cache.size == info.size()) {
        return cache.root;
    }

    cache.path = path;
    cache.lastModified = info.lastModified();
    cache.size = info.size();
    cache.loaded = true;
    cache.root = readRootObjectFromDisk(path);
    return cache.root;
}

bool writeRootObject(const QJsonObject& root)
{
    const QString path = AppPaths::shellMenuExclusionsFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    const QByteArray jsonBytes = QJsonDocument(root).toJson(
#ifdef QT_DEBUG
        QJsonDocument::Indented
#else
        QJsonDocument::Compact
#endif
    );

#ifdef QT_DEBUG
    file.write(jsonBytes);
#else
    QDataStream out(&file);
    out.setVersion(QDataStream::Qt_5_15);
    out << kBinaryMagic << kBinaryVersion << jsonBytes;
#endif

    const bool committed = file.commit();
    if (committed) {
        const QFileInfo info(path);
        RootCache& cache = rootCache();
        cache.path = path;
        cache.lastModified = info.exists() ? info.lastModified() : QDateTime();
        cache.size = info.exists() ? info.size() : -1;
        cache.loaded = true;
        cache.root = root;
    }
    return committed;
}

QJsonArray entriesToJson(const std::vector<ShellMenuExclusionEntry>& entries)
{
    QJsonArray array;
    for (const ShellMenuExclusionEntry& entry : entries) {
        QJsonObject item;
        item.insert(QStringLiteral("raw"), encodeText(stringFromWide(entry.rawText)));
        item.insert(QStringLiteral("normalized"), encodeText(stringFromWide(entry.normalizedText)));
        if (!entry.matchText.empty()) {
            item.insert(QStringLiteral("match"), encodeText(stringFromWide(entry.matchText)));
        }
        if (!entry.menuPath.empty()) {
            item.insert(QStringLiteral("path"), encodeStringList(entry.menuPath));
        }
        if (!entry.matchPath.empty()) {
            item.insert(QStringLiteral("matchPath"), encodeStringList(entry.matchPath));
        }
        item.insert(QStringLiteral("enabled"), entry.enabled);
        array.append(item);
    }
    return array;
}

std::vector<ShellMenuExclusionEntry> entriesFromJson(const QJsonArray& array)
{
    std::vector<ShellMenuExclusionEntry> entries;
    entries.reserve(static_cast<size_t>(array.size()));
    for (const QJsonValue& value : array) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject object = value.toObject();
        QString rawText;
        QString normalizedText;
        if (!decodeText(object.value(QStringLiteral("raw")), &rawText)) {
            continue;
        }
        if (!decodeText(object.value(QStringLiteral("normalized")), &normalizedText)) {
            normalizedText = QString::fromStdWString(ShellContextMenu::NormalizeMenuText(rawText.toStdWString()));
        }

        ShellMenuExclusionEntry entry;
        entry.rawText = rawText.toStdWString();
        entry.normalizedText = normalizedText.toStdWString();
        QString matchText;
        if (decodeText(object.value(QStringLiteral("match")), &matchText)) {
            entry.matchText = matchText.toStdWString();
        }
        entry.menuPath = decodeStringList(object.value(QStringLiteral("path")));
        entry.matchPath = decodeStringList(object.value(QStringLiteral("matchPath")));
        entry.enabled = object.value(QStringLiteral("enabled")).toBool(true);
        if (!entry.rawText.empty() || !entry.normalizedText.empty()) {
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

QJsonObject normalizedRoot(QJsonObject root)
{
    root.insert(QStringLiteral("version"), kStoreVersion);
    if (!root.value(QStringLiteral("exclusions")).isObject()) {
        root.insert(QStringLiteral("exclusions"), QJsonObject());
    }
    return root;
}

QStringList legacyKeysToMigrate()
{
    QStringList keys = {
        QStringLiteral("desktop"),
        QStringLiteral("box_background"),
        QStringLiteral("folder"),
        QStringLiteral("shortcut"),
        QStringLiteral("disk"),
        QStringLiteral("file"),
        QStringLiteral("file_no_extension")
    };
    QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
    keys.append(settings.value(QStringLiteral("ShellMenuExclusions/CustomExtensionKeys")).toStringList());
    keys.removeDuplicates();
    keys.removeAll(QString());
    return keys;
}
}

void ShellMenuExclusionStore::ensureMigrated()
{
    const QString path = AppPaths::shellMenuExclusionsFilePath();
    if (QFileInfo::exists(path)) {
        return;
    }

    QJsonObject exclusions;
    for (const QString& key : legacyKeysToMigrate()) {
        const std::vector<ShellMenuExclusionEntry> entries = ShellContextMenu::LoadExclusions(key.toStdWString());
        if (!entries.empty()) {
            exclusions.insert(key, entriesToJson(entries));
        }
    }

    if (exclusions.isEmpty()) {
        return;
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), kStoreVersion);
    root.insert(QStringLiteral("exclusions"), exclusions);
    writeRootObject(root);
}

std::vector<ShellMenuExclusionEntry> ShellMenuExclusionStore::load(const QString& key)
{
    ensureMigrated();
    const QJsonObject root = readRootObject();
    const QJsonObject exclusions = root.value(QStringLiteral("exclusions")).toObject();
    return entriesFromJson(exclusions.value(key).toArray());
}

std::vector<ShellMenuExclusionEntry> ShellMenuExclusionStore::load(const std::wstring& key)
{
    return load(keyFromWide(key));
}

std::vector<ShellMenuExclusionEntry> ShellMenuExclusionStore::loadForKeys(const std::vector<std::wstring>& keys)
{
    ensureMigrated();
    const QJsonObject root = readRootObject();
    const QJsonObject exclusions = root.value(QStringLiteral("exclusions")).toObject();
    std::vector<ShellMenuExclusionEntry> merged;
    for (const std::wstring& key : keys) {
        std::vector<ShellMenuExclusionEntry> entries = entriesFromJson(exclusions.value(keyFromWide(key)).toArray());
        merged.insert(merged.end(), entries.begin(), entries.end());
    }
    return merged;
}

void ShellMenuExclusionStore::save(const QString& key, const std::vector<ShellMenuExclusionEntry>& entries)
{
    if (key.isEmpty()) {
        return;
    }

    ensureMigrated();
    QJsonObject root = normalizedRoot(readRootObject());
    QJsonObject exclusions = root.value(QStringLiteral("exclusions")).toObject();
    if (entries.empty()) {
        exclusions.remove(key);
    } else {
        exclusions.insert(key, entriesToJson(entries));
    }
    root.insert(QStringLiteral("exclusions"), exclusions);
    writeRootObject(root);
}

void ShellMenuExclusionStore::save(const std::wstring& key, const std::vector<ShellMenuExclusionEntry>& entries)
{
    save(keyFromWide(key), entries);
}

int ShellMenuExclusionStore::merge(const QString& key, const std::vector<ShellMenuExclusionEntry>& entries)
{
    if (key.isEmpty() || entries.empty()) {
        return 0;
    }

    std::vector<ShellMenuExclusionEntry> existingEntries = load(key);
    int addedCount = 0;
    for (const ShellMenuExclusionEntry& entry : entries) {
        if (entry.rawText.empty() && entry.normalizedText.empty() && entry.matchText.empty() && entry.matchPath.empty()) {
            continue;
        }

        bool exists = false;
        for (const ShellMenuExclusionEntry& existing : existingEntries) {
            if (ShellContextMenu::MenuEntryMatches(existing,
                    entry.rawText,
                    entry.normalizedText,
                    entry.matchText,
                    entry.matchPath)) {
                exists = true;
                break;
            }
        }
        if (exists) {
            continue;
        }

        existingEntries.push_back(entry);
        ++addedCount;
    }

    if (addedCount > 0) {
        save(key, existingEntries);
    }
    return addedCount;
}

void ShellMenuExclusionStore::add(const QString& key, const ShellMenuExclusionEntry& entry)
{
    if (key.isEmpty() || (entry.rawText.empty() && entry.normalizedText.empty() && entry.matchText.empty() && entry.matchPath.empty())) {
        return;
    }

    std::vector<ShellMenuExclusionEntry> entries = load(key);
    for (const ShellMenuExclusionEntry& existing : entries) {
        if (ShellContextMenu::MenuEntryMatches(existing,
                entry.rawText,
                entry.normalizedText,
                entry.matchText,
                entry.matchPath)) {
            return;
        }
    }

    entries.push_back(entry);
    save(key, entries);
}

void ShellMenuExclusionStore::add(const QString& key, const QString& rawText)
{
    if (key.isEmpty() || rawText.isEmpty()) {
        return;
    }

    std::vector<ShellMenuExclusionEntry> entries = load(key);
    const std::wstring raw = rawText.toStdWString();
    const std::wstring normalized = ShellContextMenu::NormalizeMenuText(raw);
    const std::wstring match = ShellContextMenu::MatchMenuText(raw);
    for (const ShellMenuExclusionEntry& entry : entries) {
        if (ShellContextMenu::MenuEntryMatches(entry, raw, normalized, match)) {
            return;
        }
    }

    ShellMenuExclusionEntry entry;
    entry.rawText = raw;
    entry.normalizedText = normalized;
    entry.matchText = match;
    entry.enabled = true;
    entries.push_back(entry);
    save(key, entries);
}

void ShellMenuExclusionStore::add(const std::wstring& key, const std::wstring& rawText)
{
    add(keyFromWide(key), stringFromWide(rawText));
}

void ShellMenuExclusionStore::remove(const QString& key, const ShellMenuExclusionEntry& entry)
{
    std::vector<ShellMenuExclusionEntry> entries = load(key);
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const ShellMenuExclusionEntry& existing) {
        return ShellContextMenu::MenuEntryMatches(existing,
            entry.rawText,
            entry.normalizedText,
            entry.matchText,
            entry.matchPath);
    }), entries.end());
    save(key, entries);
}

void ShellMenuExclusionStore::removeByText(const QString& key,
    const QString& rawText,
    const QString& normalizedText,
    const QString& matchText,
    const QStringList& matchPath)
{
    std::vector<ShellMenuExclusionEntry> entries = load(key);
    const std::wstring raw = rawText.toStdWString();
    const std::wstring normalized = normalizedText.isEmpty()
        ? ShellContextMenu::NormalizeMenuText(raw)
        : normalizedText.toStdWString();
    const std::wstring match = matchText.isEmpty()
        ? ShellContextMenu::MatchMenuText(raw)
        : matchText.toStdWString();
    std::vector<std::wstring> path;
    path.reserve(static_cast<size_t>(matchPath.size()));
    for (const QString& segment : matchPath) {
        path.push_back(segment.toStdWString());
    }
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const ShellMenuExclusionEntry& entry) {
        return ShellContextMenu::MenuEntryMatches(entry, raw, normalized, match, path);
    }), entries.end());
    save(key, entries);
}

bool ShellMenuExclusionStore::contains(const std::wstring& key, const std::wstring& rawText)
{
    const std::wstring normalizedText = ShellContextMenu::NormalizeMenuText(rawText);
    const std::wstring matchText = ShellContextMenu::MatchMenuText(rawText);
    for (const ShellMenuExclusionEntry& exclusion : load(key)) {
        if (ShellContextMenu::MenuEntryMatches(exclusion, rawText, normalizedText, matchText)) {
            return true;
        }
    }
    return false;
}
