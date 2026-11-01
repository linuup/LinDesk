#pragma once

#include "desktop/DesktopItemRenderCacheHelper.h"
#include "desktop/DesktopRootItemModel.h"
#include "monitor/DesktopInfoMonitor.h"

#include <QSet>
#include <functional>

/// Runtime state for a desktop item in the model driven pipeline.
struct DesktopRootItemState
{
    DesktopRootItemModel model;
    bool renderDataDirty = true;
};

DesktopRootItemRenderData buildDesktopRootItemRenderData(
    const DesktopRootItemState& state,
    bool forceSelected = false);

QRect desktopRootItemHighlightDirtyRect(const DesktopRootItemState& state);

class ListItemData;

/// External context required when initializing item models.
struct DesktopItemInitContext {
    QList<DesktopInfoMonitor::DesktopIcon> sysIconRegisterInfo;
    QSet<QString> systemFolderCLSIDs;
    bool showHiddenFiles = false;
    std::function<QString(const QString&)> getBoxId;
};

void initDesktopRootItemModel(
    DesktopRootItemModel& model,
    const ListItemData& data,
    const DesktopItemInitContext& context);

/// View level inputs used when building render data.
struct DesktopRenderParams {
    int iconWidth = 76;
    int iconHeight = 78;
    int largeIconW = 100;
    int smallIconW = 62;
    int iconTextGap = 8;
    qreal dpiScale = 1.0;
    qreal devicePixelRatio = 1.0;
    QFont labelFont;
    bool showShortcutArrow = true;
};

DesktopRootItemRenderData buildRenderDataFromModel(
    const DesktopRootItemModel& model,
    const DesktopRenderParams& params);

bool openDesktopRootItem(const DesktopRootItemModel& model);
