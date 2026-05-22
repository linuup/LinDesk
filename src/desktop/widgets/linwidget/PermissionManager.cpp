#include "desktop/widgets/linwidget/PermissionManager.h"

namespace LinWidget {

QStringList PermissionManager::normalizePermissions(const QStringList& permissions) const
{
    QStringList result;
    for (const QString& permission : permissions) {
        const QString normalized = permission.trimmed().toLower();
        if (!normalized.isEmpty() && !result.contains(normalized)) {
            result.append(normalized);
        }
    }
    return result;
}

bool PermissionManager::hasPermission(const QStringList& grantedPermissions, const QString& permission) const
{
    return normalizePermissions(grantedPermissions).contains(permission.trimmed().toLower());
}

bool PermissionManager::canUseNetwork(const QStringList& grantedPermissions) const
{
    return hasPermission(grantedPermissions, QStringLiteral("network"));
}

bool PermissionManager::canUseStorage(const QStringList& grantedPermissions) const
{
    return hasPermission(grantedPermissions, QStringLiteral("storage"));
}

bool PermissionManager::canUseDesktopApi(const QStringList& grantedPermissions) const
{
    return hasPermission(grantedPermissions, QStringLiteral("desktop"));
}

bool PermissionManager::hasPermission(const WidgetManifest& manifest, const QString& permission) const
{
    return hasPermission(manifest.permissions, permission);
}

bool PermissionManager::canUseNetwork(const WidgetManifest& manifest) const
{
    return canUseNetwork(manifest.permissions);
}

bool PermissionManager::canUseStorage(const WidgetManifest& manifest) const
{
    return canUseStorage(manifest.permissions);
}

bool PermissionManager::canUseDesktopApi(const WidgetManifest& manifest) const
{
    return canUseDesktopApi(manifest.permissions);
}

}
