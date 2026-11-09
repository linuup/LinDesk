#include "desktop/widgets/linwidget/WidgetRegistryManager.h"

#include "desktop/widgets/linwidget/PermissionManager.h"
#include "desktop/widgets/linwidget/WidgetPackage.h"
#include "services/AppPaths.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

namespace LinWidget {

WidgetRegistryManager& WidgetRegistryManager::instance()
{
    static WidgetRegistryManager manager;
    return manager;
}

bool WidgetRegistryManager::load(QString* errorMessage)
{
    if (m_loaded) {
        if (errorMessage) {
            errorMessage->clear();
        }
        return true;
    }

    m_entries.clear();

    QFile file(AppPaths::linWidgetRegistryFilePath());
    if (!file.exists()) {
        m_loaded = true;
        if (errorMessage) {
            errorMessage->clear();
        }
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot open LinWidget registry: %1").arg(file.errorString());
        }
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("LinWidget registry must be a JSON object.");
        }
        return false;
    }

    const QJsonArray widgets = document.object().value(QStringLiteral("widgets")).toArray();
    for (const QJsonValue& value : widgets) {
        WidgetRegistryEntry entry;
        if (!registryEntryFromJson(value.toObject(), &entry)) {
            continue;
        }
        if (entry.packagePath.isEmpty() || !QDir(entry.packagePath).exists()) {
            continue;
        }
        if (entry.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0) {
            continue;
        }
        m_entries.insert(registryKey(entry.id, entry.isDevelopment), entry);
    }

    m_loaded = true;
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool WidgetRegistryManager::save(QString* errorMessage) const
{
    QFileInfo info(AppPaths::linWidgetRegistryFilePath());
    if (!QDir().mkpath(info.absolutePath())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot create LinWidget registry directory.");
        }
        return false;
    }

    QJsonObject root;
    QFile existingFile(info.absoluteFilePath());
    if (existingFile.exists() && existingFile.open(QIODevice::ReadOnly)) {
        const QJsonDocument existingDocument = QJsonDocument::fromJson(existingFile.readAll());
        if (existingDocument.isObject()) {
            root = existingDocument.object();
        }
    }

    QJsonArray widgets;
    const QVector<WidgetRegistryEntry> sortedEntries = entries();
    for (const WidgetRegistryEntry& entry : sortedEntries) {
        widgets.append(registryEntryToJson(entry));
    }

    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot save LinWidget registry: %1").arg(file.errorString());
        }
        return false;
    }

    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("widgets"), widgets);
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

QVector<WidgetRegistryEntry> WidgetRegistryManager::entries() const
{
    QVector<WidgetRegistryEntry> result = m_entries.values().toVector();
    std::sort(result.begin(), result.end(), [](const WidgetRegistryEntry& left, const WidgetRegistryEntry& right) {
        return left.name.compare(right.name, Qt::CaseInsensitive) < 0;
    });
    return result;
}

WidgetRegistryEntry WidgetRegistryManager::entryById(const QString& id) const
{
    const QString installedKey = registryKey(id, false);
    if (m_entries.contains(installedKey)) {
        return m_entries.value(installedKey);
    }
    const QString developmentKey = registryKey(id, true);
    if (m_entries.contains(developmentKey)) {
        return m_entries.value(developmentKey);
    }
    return m_entries.value(normalizedId(id));
}

bool WidgetRegistryManager::syncFromFolder(const QString& widgetsDir, QString* errorMessage)
{
    const QDir dir(widgetsDir);
    if (!dir.exists()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("LinWidgets directory does not exist.");
        }
        return false;
    }

    bool ok = true;
    QStringList errors;
    const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& entry : entries) {
        QString packageError;
        if (!upsertFromPackage(entry.absoluteFilePath(), &packageError)) {
            ok = false;
            if (!packageError.isEmpty()) {
                errors.append(packageError);
            }
        }
    }

    if (errorMessage) {
        *errorMessage = errors.join(QStringLiteral("\n"));
    }
    return ok;
}

bool WidgetRegistryManager::syncOfficialWidgetsIfOutdated(const QString& bundledWidgetsDir, const QString& userWidgetsDir, QString* errorMessage)
{
    const QDir bundled(bundledWidgetsDir);
    const QDir user(userWidgetsDir);
    if (!bundled.exists() || !user.exists()) {
        return true;
    }

    auto getNewestMtime = [](const QString& dirPath) -> QDateTime {
        QDateTime newest;
        QDirIterator it(dirPath, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QDateTime mtime = it.fileInfo().lastModified();
            if (!newest.isValid() || mtime > newest) {
                newest = mtime;
            }
        }
        return newest;
    };

    bool allOk = true;
    const QFileInfoList bundledEntries = bundled.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& bEntry : bundledEntries) {
        const QString widgetName = bEntry.fileName();
        const QString targetPath = user.filePath(widgetName);
        if (QDir(targetPath).exists()) {
            const QDateTime bundledMtime = getNewestMtime(bEntry.absoluteFilePath());
            const QDateTime userMtime = getNewestMtime(targetPath);
            if (bundledMtime.isValid() && (!userMtime.isValid() || bundledMtime > userMtime)) {
                qDebug() << "[LinWidget] Updating outdated widget in AppData:" << widgetName
                         << "bundled:" << bundledMtime << "user:" << userMtime;
                QString copyErr;
                if (!copyDirectoryForInstall(bEntry.absoluteFilePath(), targetPath, &copyErr)) {
                    allOk = false;
                    if (errorMessage) {
                        *errorMessage = copyErr;
                    }
                }
            }
        }
    }
    return allOk;
}

bool WidgetRegistryManager::upsertFromPackage(const QString& packagePath, QString* errorMessage)
{
    WidgetManifest manifest;
    if (!WidgetPackage::loadManifest(packagePath, &manifest, errorMessage)) {
        return false;
    }

    if (manifest.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0) {
        return true;
    }

    if (normalizedId(manifest.id).isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget manifest is missing id.");
        }
        return false;
    }

    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    WidgetRegistryEntry entry = m_entries.value(registryKey(manifest.id, false));
    const WidgetRegistryEntry legacyEntry = m_entries.value(normalizedId(manifest.id));
    if (entry.id.isEmpty()) {
        if (!legacyEntry.id.isEmpty() && !legacyEntry.isDevelopment) {
            entry = legacyEntry;
        } else {
            entry.id = manifest.id;
            entry.enabled = true;
            entry.trusted = false;
            entry.installedAt = now;
            entry.lastRunState = QStringLiteral("stopped");
        }
    }

    entry.id = manifest.id;
    entry.name = manifest.name;
    entry.version = manifest.version;
    entry.author = manifest.author;
    entry.description = manifest.description;
    entry.icon = manifest.icon;
    entry.packagePath = QFileInfo(packagePath).absoluteFilePath();
    entry.runtimeType = runtimeTypeToString(manifest.type);
    entry.defaultSize = QSize(manifest.width, manifest.height);
    entry.isDevelopment = false;
    entry.sourcePath.clear();
    entry.autoReload = false;
    entry.autoLoad = manifest.autoLoad;
    entry.role = manifest.role;
    entry.permissionsGranted = PermissionManager().normalizePermissions(entry.permissionsGranted);
    entry.updatedAt = now;

    m_entries.insert(registryKey(manifest.id, false), entry);
    m_entries.remove(normalizedId(manifest.id));
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool WidgetRegistryManager::addDevelopmentProject(const QString& sourcePath, QString* errorMessage)
{
    WidgetManifest manifest;
    if (!WidgetPackage::loadManifest(sourcePath, &manifest, errorMessage)) {
        return false;
    }

    const QString entryPath = WidgetPackage::entryPath(sourcePath, manifest);
    if (!QFileInfo::exists(entryPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget entry file does not exist: %1").arg(entryPath);
        }
        return false;
    }

    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    WidgetRegistryEntry entry = m_entries.value(registryKey(manifest.id, true));
    if (entry.id.isEmpty()) {
        entry.id = manifest.id;
        entry.enabled = true;
        entry.trusted = true;
        entry.installedAt = now;
        entry.lastRunState = QStringLiteral("stopped");
    }

    entry.id = manifest.id;
    entry.name = manifest.name;
    entry.version = manifest.version;
    entry.author = manifest.author;
    entry.description = manifest.description;
    entry.icon = manifest.icon;
    entry.packagePath = QFileInfo(sourcePath).absoluteFilePath();
    entry.runtimeType = runtimeTypeToString(manifest.type);
    entry.defaultSize = QSize(manifest.width, manifest.height);
    entry.isDevelopment = true;
    entry.sourcePath = entry.packagePath;
    entry.autoReload = true;
    entry.autoLoad = manifest.autoLoad;
    entry.role = manifest.role;
    entry.permissionsGranted = PermissionManager().normalizePermissions(
        entry.permissionsGranted + QStringList{ QStringLiteral("network"), QStringLiteral("storage"), QStringLiteral("desktop") });
    entry.updatedAt = now;

    m_entries.insert(registryKey(manifest.id, true), entry);
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool WidgetRegistryManager::installDevelopmentProject(const QString& id, QString* installedPath, QString* errorMessage)
{
    const WidgetRegistryEntry entry = m_entries.value(registryKey(id, true));
    if (entry.id.isEmpty() || !entry.isDevelopment) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Development widget is not registered.");
        }
        return false;
    }

    WidgetManifest manifest;
    if (!WidgetPackage::loadManifest(entry.packagePath, &manifest, errorMessage)) {
        return false;
    }
    const QString entryPath = WidgetPackage::entryPath(entry.packagePath, manifest);
    if (!QFileInfo::exists(entryPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget entry file does not exist: %1").arg(entryPath);
        }
        return false;
    }

    const QString targetPath = QDir(AppPaths::userLinWidgetsDir()).filePath(safePackageDirectoryName(manifest.id));
    if (!copyDirectoryForInstall(entry.packagePath, targetPath, errorMessage)) {
        return false;
    }
    if (!upsertFromPackage(targetPath, errorMessage)) {
        return false;
    }
    if (installedPath) {
        *installedPath = targetPath;
    }
    return true;
}

bool WidgetRegistryManager::setEnabled(const QString& id, bool enabled)
{
    auto it = m_entries.find(normalizedId(id));
    if (it == m_entries.end()) {
        it = m_entries.find(registryKey(id, false));
    }
    if (it == m_entries.end()) {
        it = m_entries.find(registryKey(id, true));
    }
    if (it == m_entries.end()) {
        return false;
    }
    it->enabled = enabled;
    it->updatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return true;
}

bool WidgetRegistryManager::setEnabled(const QString& id, bool isDevelopment, bool enabled)
{
    auto it = m_entries.find(registryKey(id, isDevelopment));
    if (it == m_entries.end()) {
        return false;
    }
    it->enabled = enabled;
    it->updatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return true;
}

bool WidgetRegistryManager::setLastRunState(const QString& id, const QString& state)
{
    auto it = m_entries.find(normalizedId(id));
    if (it == m_entries.end()) {
        it = m_entries.find(registryKey(id, false));
    }
    if (it == m_entries.end()) {
        it = m_entries.find(registryKey(id, true));
    }
    if (it == m_entries.end()) {
        return false;
    }
    it->lastRunState = state == QStringLiteral("running")
        ? QStringLiteral("running")
        : QStringLiteral("stopped");
    it->updatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return true;
}

bool WidgetRegistryManager::setLastRunState(const QString& id, bool isDevelopment, const QString& state)
{
    auto it = m_entries.find(registryKey(id, isDevelopment));
    if (it == m_entries.end()) {
        return false;
    }
    it->lastRunState = state == QStringLiteral("running")
        ? QStringLiteral("running")
        : QStringLiteral("stopped");
    it->updatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return true;
}

bool WidgetRegistryManager::setPermissionsGranted(const QString& id, const QStringList& permissions)
{
    WidgetRegistryEntry entry = entryById(id);
    return setPermissionsGranted(id, entry.isDevelopment, permissions);
}

bool WidgetRegistryManager::setPermissionsGranted(const QString& id, bool isDevelopment, const QStringList& permissions)
{
    auto it = m_entries.find(registryKey(id, isDevelopment));
    if (it == m_entries.end()) {
        it = m_entries.find(normalizedId(id));
    }
    if (it == m_entries.end()) {
        return false;
    }
    it->permissionsGranted = PermissionManager().normalizePermissions(permissions);
    it->updatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return true;
}

bool WidgetRegistryManager::removeById(const QString& id)
{
    return m_entries.remove(normalizedId(id)) > 0 ||
        m_entries.remove(registryKey(id, false)) > 0 ||
        m_entries.remove(registryKey(id, true)) > 0;
}

bool WidgetRegistryManager::removeDevelopmentProject(const QString& id)
{
    return m_entries.remove(registryKey(id, true)) > 0;
}

QString WidgetRegistryManager::normalizedId(const QString& id) const
{
    return id.trimmed().toLower();
}

QString WidgetRegistryManager::registryKey(const QString& id, bool isDevelopment) const
{
    return QStringLiteral("%1:%2").arg(isDevelopment ? QStringLiteral("dev") : QStringLiteral("installed"), normalizedId(id));
}

QString WidgetRegistryManager::safePackageDirectoryName(const QString& id) const
{
    QString safe = id.trimmed();
    for (QChar& ch : safe) {
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('.') && ch != QLatin1Char('-') && ch != QLatin1Char('_')) {
            ch = QLatin1Char('_');
        }
    }
    return safe.isEmpty() ? QStringLiteral("widget") : safe;
}

bool WidgetRegistryManager::copyDirectoryForInstall(const QString& sourcePath, const QString& targetPath, QString* errorMessage) const
{
    const QDir sourceDir(sourcePath);
    if (!sourceDir.exists()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Source widget directory does not exist.");
        }
        return false;
    }

    QDir().mkpath(targetPath);
    const QFileInfoList entries = sourceDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& entry : entries) {
        const QString name = entry.fileName();
        if (name == QStringLiteral(".git") || name == QStringLiteral("node_modules")) {
            continue;
        }

        const QString targetEntryPath = QDir(targetPath).filePath(name);
        if (entry.isDir()) {
            if (!copyDirectoryForInstall(entry.absoluteFilePath(), targetEntryPath, errorMessage)) {
                return false;
            }
            continue;
        }

        if (QFileInfo::exists(targetEntryPath)) {
            QFile::remove(targetEntryPath);
        }
        if (!QFile::copy(entry.absoluteFilePath(), targetEntryPath)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Cannot copy widget file: %1").arg(entry.absoluteFilePath());
            }
            return false;
        }
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

}
