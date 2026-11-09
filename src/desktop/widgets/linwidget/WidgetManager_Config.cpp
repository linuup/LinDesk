#include "desktop/widgets/linwidget/WidgetManager_Internal.h"

namespace LinWidget {

// ============================================================================
// WidgetManager Layout & Configuration Persistence Implementation
// ============================================================================

bool WidgetManager::restoreFromConfig(const QString& configPath, QString* errorMessage)
{
    if (!m_enabled) {
        if (errorMessage) {
            errorMessage->clear();
        }
        return true;
    }

    if (!start()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to start LinWidget runtime.");
        }
        return false;
    }

    const bool wasStoppingAll = m_stoppingAll;
    m_stoppingAll = true;
    stopAll(false);
    m_stoppingAll = wasStoppingAll;

    QVector<WidgetLayoutState> states;
    const QString path = configPath.isEmpty() ? ConfigManager::defaultConfigPath() : configPath;
    if (!ConfigManager::loadLayout(path, &states, errorMessage)) {
        updateDefaultCefPage();
        if (configPath.isEmpty() && m_activeWallpaperId.isEmpty()) {
            loadWallpaperConfig();
        }
        return false;
    }

    if (configPath.isEmpty()) {
        auto& registry = WidgetRegistryManager::instance();
        QString registryError;
        if (registry.load(&registryError)) {
            QSet<QString> knownInstanceIds;
            for (const WidgetLayoutState& state : states) {
                knownInstanceIds.insert(state.instanceId);
            }
            for (const WidgetRegistryEntry& entry : registry.entries()) {
                if (!entry.enabled || !entry.autoLoad || !QFileInfo::exists(entry.packagePath)) {
                    continue;
                }
                WidgetManifest manifest;
                if (!WidgetPackage::loadManifest(entry.packagePath, &manifest, nullptr)) {
                    continue;
                }
                if (manifest.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0) {
                    continue;
                }
                const QString instanceId = instanceLayoutId(manifest, entry.isDevelopment);
                if (knownInstanceIds.contains(instanceId)) {
                    continue;
                }
                WidgetLayoutState state;
                state.instanceId = instanceId;
                state.packagePath = entry.packagePath;
                state.registryId = registryIdForManifest(manifest);
                state.geometry = QRect(QPoint(260, 220), QSize(manifest.width, manifest.height));
                state.zOrder = m_layout.nextZOrder();
                state.visible = true;
                state.isDevelopment = entry.isDevelopment;
                state.createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
                states.append(state);
                knownInstanceIds.insert(instanceId);
            }
        } else if (!registryError.isEmpty()) {
            emit widgetError(QString(), registryError);
        }
    }

    bool ok = true;
    const QVector<WidgetLayoutState> uniqueStates = deduplicatedLayoutStates(states);
    m_layout.setStates(QVector<WidgetLayoutState>());
    for (const WidgetLayoutState& state : uniqueStates) {
        if (!state.visible) {
            m_layout.addOrUpdate(state);
            continue;
        }

        WidgetManifest manifest;
        QString launchError;
        if (!WidgetPackage::loadManifest(state.packagePath, &manifest, &launchError)) {
            ok = false;
            emit widgetError(state.instanceId, launchError);
            continue;
        }

        if (manifest.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0) {
            continue;
        }

        WidgetInstance instance;
        instance.instanceId = state.instanceId;
        instance.packagePath = state.packagePath;
        instance.manifest = manifest;
        instance.isDevelopment = stateIsDevelopment(state);
        instance.autoReload = instance.isDevelopment;
        instance.grantedPermissions = grantedPermissionsForManifest(manifest, state.packagePath, instance.isDevelopment);
        instance.geometry = resolvedWidgetGeometry(manifest, state.geometry, instance.isDevelopment);
        if (instance.instanceId.isEmpty()) {
            instance.instanceId = makeInstanceId(manifest);
        }

        WidgetLayoutState launchState = state;
        launchState.registryId = state.registryId.isEmpty() ? registryIdForManifest(manifest) : state.registryId;
        launchState.isDevelopment = instance.isDevelopment;
        if (launchState.createdAt.isEmpty()) {
            launchState.createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        }
        launchState.geometry = instance.geometry;
        if (!launchWidgetInstance(instance, launchState, &launchError)) {
            ok = false;
            emit widgetError(instance.instanceId, launchError);
            continue;
        }
        emit widgetLaunched(instance.instanceId);
    }

    updateDefaultCefPage();

    if (errorMessage && ok) {
        errorMessage->clear();
    }
    if (configPath.isEmpty()) {
        if (m_activeWallpaperId.isEmpty()) {
            loadWallpaperConfig();
        }
        scheduleSaveToConfig();
    }
    return ok;
}

QVector<WidgetLayoutState> WidgetManager::filterWallpapers(const QVector<WidgetLayoutState>& states) const
{
    QVector<WidgetLayoutState> filtered;
    for (const WidgetLayoutState& state : states) {
        WidgetInstance inst = m_instances.value(state.instanceId);
        if (inst.instanceId.isEmpty()) {
            WidgetManifest manifest;
            if (WidgetPackage::loadManifest(state.packagePath, &manifest)) {
                if (manifest.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0) {
                    continue;
                }
            }
        } else {
            if (inst.manifest.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0) {
                continue;
            }
        }
        filtered.append(state);
    }
    return filtered;
}

bool WidgetManager::saveToConfig(const QString& configPath, QString* errorMessage) const
{
    const QString path = configPath.isEmpty() ? ConfigManager::defaultConfigPath() : configPath;
    return ConfigManager::saveLayout(path, deduplicatedLayoutStates(filterWallpapers(m_layout.states())), errorMessage);
}

QVector<WidgetLayoutState> WidgetManager::layoutStates() const
{
    QVector<WidgetLayoutState> states = deduplicatedLayoutStates(filterWallpapers(m_layout.states()));
    if (!m_enabled) {
        for (WidgetLayoutState& state : states) {
            state.visible = false;
        }
    }
    return states;
}


void WidgetManager::scheduleSaveToConfig()
{
    if (m_stoppingAll) {
        return;
    }
    m_saveTimer.start();
}

void WidgetManager::saveToDefaultConfig()
{
    if (m_stoppingAll) {
        return;
    }

    QString errorMessage;
    if (!saveToConfig(QString(), &errorMessage) && !errorMessage.isEmpty()) {
        emit widgetError(QString(), errorMessage);
    }
}


} // namespace LinWidget
