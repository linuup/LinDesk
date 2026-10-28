#pragma once

#include "desktop/widgets/linwidget/LinWidgetTypes.h"

#include <QString>
#include <QStringList>

namespace LinWidget {

class PermissionManager {
public:
    QStringList normalizePermissions(const QStringList& permissions) const;
    bool isPermissionSupported(const QString& permission) const;
    bool hasPermission(const QStringList& grantedPermissions, const QString& permission) const;
    bool canUseNetwork(const QStringList& grantedPermissions) const;
    bool canUseStorage(const QStringList& grantedPermissions) const;
    bool canUseDesktopApi(const QStringList& grantedPermissions) const;

    bool hasPermission(const WidgetManifest& manifest, const QString& permission) const;
    bool canUseNetwork(const WidgetManifest& manifest) const;
    bool canUseStorage(const WidgetManifest& manifest) const;
    bool canUseDesktopApi(const WidgetManifest& manifest) const;
};

}
