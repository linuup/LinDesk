#pragma once

#include "desktop/widgets/linwidget/LinWidgetTypes.h"

#include <QHash>
#include <QString>
#include <QVector>

namespace LinWidget {

class WidgetRegistryManager {
public:
    bool load(QString* errorMessage = nullptr);
    bool save(QString* errorMessage = nullptr) const;

    QVector<WidgetRegistryEntry> entries() const;
    WidgetRegistryEntry entryById(const QString& id) const;

    bool syncFromFolder(const QString& widgetsDir, QString* errorMessage = nullptr);
    bool upsertFromPackage(const QString& packagePath, QString* errorMessage = nullptr);
    bool addDevelopmentProject(const QString& sourcePath, QString* errorMessage = nullptr);
    bool installDevelopmentProject(const QString& id, QString* installedPath = nullptr, QString* errorMessage = nullptr);

    bool setEnabled(const QString& id, bool enabled);
    bool setEnabled(const QString& id, bool isDevelopment, bool enabled);
    bool setLastRunState(const QString& id, const QString& state);
    bool setLastRunState(const QString& id, bool isDevelopment, const QString& state);
    bool removeById(const QString& id);
    bool removeDevelopmentProject(const QString& id);

private:
    QString normalizedId(const QString& id) const;
    QString registryKey(const QString& id, bool isDevelopment) const;
    QString safePackageDirectoryName(const QString& id) const;
    bool copyDirectoryForInstall(const QString& sourcePath, const QString& targetPath, QString* errorMessage) const;

    QHash<QString, WidgetRegistryEntry> m_entries;
};

}
