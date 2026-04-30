#include "services/LayoutPersistenceService.h"

#include <QDir>

#include "box/BoxManager.h"
#include "box/LinBox.h"
#include "desktop/DesktopRootItemState.h"

namespace {
QString normalizePersistencePath(const QString& path)
{
    QString normalized = QDir::toNativeSeparators(path);
    while (normalized.endsWith('\\')) {
        normalized.chop(1);
    }
    return normalized;
}
}

QMap<QString, QMap<QString, QVariant>> LayoutPersistenceService::collectIconPositions(
    const QVector<DesktopRootItemState>& desktopItems,
    const QVector<QPointer<LinBox>>& boxes) const
{
    QMap<QString, QMap<QString, QVariant>> iconPositions;

    for (const auto& state : desktopItems) {
        const QString filePath = normalizePersistencePath(state.model.filePath);
        if (filePath.isEmpty()) {
            continue;
        }

        QMap<QString, QVariant> posInfo;
        posInfo["x"] = state.model.position.x();
        posInfo["y"] = state.model.position.y();
        posInfo["boxId"] = state.model.boxId;
        posInfo["ord"] = state.model.customSortOrder;
        iconPositions[filePath] = posInfo;
    }

    for (const auto& boxPtr : boxes) {
        LinBox* box = boxPtr.data();
        if (!box) {
            continue;
        }

        for (const DesktopRootItemState& state : box->itemStates()) {
            const QString filePath = normalizePersistencePath(state.model.filePath);
            if (filePath.isEmpty()) {
                continue;
            }

            QMap<QString, QVariant> posInfo;
            posInfo["x"] = state.model.position.x();
            posInfo["y"] = state.model.position.y();
            posInfo["boxId"] = state.model.boxId;
            posInfo["ord"] = state.model.customSortOrder;
            iconPositions[filePath] = posInfo;
        }
    }

    return iconPositions;
}

QMap<QString, PersistentIconInfo> LayoutPersistenceService::toSavedIconPositions(
    const QMap<QString, QMap<QString, QVariant>>& iconPositions) const
{
    QMap<QString, PersistentIconInfo> savedIconPositions;

    for (auto it = iconPositions.begin(); it != iconPositions.end(); ++it) {
        PersistentIconInfo info;
        info.x = it.value().value("x").toInt();
        info.y = it.value().value("y").toInt();
        info.boxId = it.value().value("boxId").toString();
        savedIconPositions[it.key()] = info;
    }

    return savedIconPositions;
}

void LayoutPersistenceService::save(const QString& persistencePath,
                                    const LayoutPersistenceState& state) const
{
    BoxManager::instance()->saveAll(
        persistencePath,
        state.enablePersistence,
        state.iconPositions,
        state.viewMode,
        state.sortType,
        state.preAutoSort,
        state.iconPosMode,
        state.autoArrange,
        state.gridAlignment,
        state.showDesktopIcons,
        state.doubleClickHideIcons,
        state.showShortCutArrow
    );
}

bool LayoutPersistenceService::load(const QString& persistencePath,
                                    bool autoUpdateState,
                                    LayoutPersistenceState& state) const
{
    const bool previousEnablePersistence = state.enablePersistence;

    BoxManager::instance()->loadAll(
        persistencePath,
        autoUpdateState,
        state.enablePersistence,
        state.iconShowRules,
        state.viewMode,
        state.sortType,
        state.preAutoSort,
        state.iconPosMode,
        state.autoArrange,
        state.gridAlignment,
        state.showDesktopIcons,
        state.doubleClickHideIcons,
        state.showShortCutArrow,
        state.boxData,
        state.iconPositions
    );

    return !state.boxData.isEmpty() ||
           !state.iconPositions.isEmpty() ||
           state.enablePersistence != previousEnablePersistence;
}
