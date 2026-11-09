#include "desktop/ListView_Internal.h"

// ============================================================================
// ListView Item CRUD, Sorting & Attribute Filtering Implementation
// ============================================================================

void ListView::updateSystemIconVisibility(const QString& clsid, bool hidden)
{
    qDebug() << "updateSystemIconVisibility:" << clsid << "->" << (hidden ? "hiding" : "showing");
    
    // 规范化 CLSID 并匹配对应系统图标
    QString normalizedClsid = clsid;
    normalizedClsid.replace("::", "");

    if (monitor) {
        sysIconRegisterInfo = monitor->getCurrentIcons();
    }

    DesktopItemInitContext context = currentDesktopItemInitContext();
    for (auto& state : m_itemStates) {
        QString itemClsid = state.model.itemData.getCLSID();
        itemClsid.replace("::", "");
        if (itemClsid != normalizedClsid) {
            continue;
        }

        DesktopRootItemModel updatedModel;
        initDesktopRootItemModel(updatedModel, state.model.itemData, context);
        updatedModel.layoutMode = state.model.layoutMode;
        updatedModel.position = state.model.position;
        updatedModel.baseSize = state.model.baseSize;
        updatedModel.effectiveSize = state.model.effectiveSize;
        updatedModel.visibleTextHeight = state.model.visibleTextHeight;
        updatedModel.isSelected = state.model.isSelected;
        updatedModel.isHovered = state.model.isHovered;
        updatedModel.focused = state.model.focused;
        updatedModel.isCutMarked = state.model.isCutMarked;
        updatedModel.isFullNameExpanded = state.model.isFullNameExpanded;
        updatedModel.customSortOrder = state.model.customSortOrder;
        updatedModel.expandedPreviewText = state.model.expandedPreviewText;
        updatedModel.isRenaming = state.model.isRenaming;
        updatedModel.isReady = true;
        if (hidden) {
            updatedModel.visible = false;
        }
        state.model = updatedModel;
        markDesktopRenderDataDirty(&state);
    }

    updateAllItemRenderData();
    rebuildSpatialIndex();
    loadIconLayer();
    update();
    return;
}





void ListView::onWindowsShowHiddenFilesChanged(bool showHiddenFiles)
{
    if (m_showHiddenFiles == showHiddenFiles) {
        return;
    }

    m_showHiddenFiles = showHiddenFiles;
    refreshVisibilityForWindowsConfig();
}

void ListView::startListeningSpecialIcon()
{
    // 启动特殊图标监控并获取当前系统图标
    if (monitor->startListening()) {
        sysIconRegisterInfo = monitor->getCurrentIcons();
    }
    // 关联图标可见性变化信号
    connect(monitor, &DesktopInfoMonitor::iconVisibilityChanged,
        this, &ListView::updateSystemIconVisibility);
}


bool ListView::fileHasHiddenAttribute(const QString& filePath) const
{
    if (filePath.isEmpty() || filePath.startsWith("::")) {
        return false;
    }

    const std::wstring nativePath = QDir::toNativeSeparators(filePath).toStdWString();
    const DWORD attributes = GetFileAttributesW(nativePath.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_HIDDEN);
}

void ListView::applyWindowsHiddenVisualState(DesktopRootItemModel& model)
{
    const bool isSpecial = !model.isShortcut && (model.itemData.type() == ObjectType::SpecialFolder ||
        SystemFolderCLSIDs.contains(model.itemData.getCLSID()) ||
        model.filePath.startsWith("::"));
    const bool shouldFade = !isSpecial && m_showHiddenFiles && fileHasHiddenAttribute(model.filePath);
    model.isHiddenFileVisual = shouldFade;
}

bool ListView::shouldShowFileSystemItem(const DesktopRootItemModel& model) const
{
    if (model.objectType == ObjectType::DiskLetter) {
        return false;
    }

    const bool isSpecial = !model.isShortcut && (model.itemData.type() == ObjectType::SpecialFolder ||
        SystemFolderCLSIDs.contains(model.itemData.getCLSID()) ||
        model.filePath.startsWith("::"));
    if (isSpecial) {
        return true;
    }

    return m_showHiddenFiles || !fileHasHiddenAttribute(model.filePath);
}

void ListView::refreshVisibilityForWindowsConfig()
{
    const QVector<QPointer<LinBox>> boxes = BoxManager::instance()->allBoxes();
    for (const QPointer<LinBox>& box : boxes) {
        if (box && box->isFolderMapped()) {
            reloadMappedFolderBox(box);
        }
    }

    onMenuRefresh();
}

void ListView::syncBoxVisibilityWithDesktopIcons()
{
    const QVector<QPointer<LinBox>> boxes = BoxManager::instance()->allBoxes();
    for (const QPointer<LinBox>& box : boxes) {
        if (!box) {
            continue;
        }

        if (m_iconShowRule.showDesktopIcons) {
            if (!box->isVisible()) {
                box->show();
            }
            box->raise();
        } else {
            box->hide();
        }
    }
}


ListView::SortOrderType ListView::getOppositeDirection() {

    switch (m_iconShowRule.sortType) {
    case SortOrderType::SortByName_ASC:
        return SortOrderType::SortByName_DESC;
    case SortOrderType::SortByName_DESC:
        return SortOrderType::SortByName_ASC;
    case SortOrderType::SortBySize_ASC:
        return SortOrderType::SortBySize_DESC;
    case SortOrderType::SortBySize_DESC:
        return SortOrderType::SortBySize_ASC;
    case SortOrderType::SortByType_ASC:
        return SortOrderType::SortByType_DESC;
    case SortOrderType::SortByType_DESC:
        return SortOrderType::SortByType_ASC;
    case SortOrderType::SortByDateModified_ASC:
        return SortOrderType::SortByDateModified_DESC;
    case SortOrderType::SortByDateModified_DESC:
        return SortOrderType::SortByDateModified_ASC;
    case SortOrderType::SortByCategorized:
        return SortOrderType::SortByCategorized;
    case SortOrderType::SortByCustom:
        return SortOrderType::SortByCustom;
    case SortOrderType::Default:
        return SortOrderType::SortByName_ASC; // Or stay at Default
    default:
        return SortOrderType::SortByName_ASC;
    }
}
void ListView::sortItems(SortOrderType order)
{
    {
        if (m_iconShowRule.sortType == order) {
            order = getOppositeDirection();
        }

        performSort(order);
        return;
    }

}

void ListView::performSort(SortOrderType order)
{
    switch (order) {
    case SortOrderType::Default:
        // Use Categorized sort if auto-arrange is on, otherwise do nothing
        if (m_iconShowRule.autoArrange) {
            sortByCategorized();
        }
        break;
    case SortOrderType::SortByName_ASC:
        sortByName(true);
        break;
    case SortOrderType::SortByName_DESC:
        sortByName(false);
        break;
    case SortOrderType::SortBySize_ASC:
        sortBySize(true);
        break;
    case SortOrderType::SortBySize_DESC:
        sortBySize(false);
        break;
    case SortOrderType::SortByType_ASC:
        sortByType(true);
        break;
    case SortOrderType::SortByType_DESC:
        sortByType(false);
        break;
    case SortOrderType::SortByDateModified_ASC:
        sortByDate(true);
        break;
    case SortOrderType::SortByDateModified_DESC:
        sortByDate(false);
        break;
    case SortOrderType::SortByCategorized:
        sortByCategorized();
        break;
    case SortOrderType::SortByCustom:
        sortByCustom();
        break;
    default:
        // Do nothing for unknown states
        break;
    }
    m_iconShowRule.sortType = order;
    applyAutoArrangeAfterDesktopMutation(false, false);
}

void ListView::sortByCategorized()
{
    {
        QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
        sortAutoArrangeStates(states, SortOrderType::SortByCategorized);
        assignCustomSortOrders(states);
        return;
    }

}

void ListView::sortByCustom()
{
    QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
    sortAutoArrangeStates(states, SortOrderType::SortByCustom);
    assignCustomSortOrders(states);
}

void ListView::sortByName(bool ascending)
{
    {
        QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
        sortAutoArrangeStates(states, ascending ? SortOrderType::SortByName_ASC : SortOrderType::SortByName_DESC);
        assignCustomSortOrders(states);
        return;
    }

}

void ListView::sortBySize(bool ascending)
{
    {
        QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
        sortAutoArrangeStates(states, ascending ? SortOrderType::SortBySize_ASC : SortOrderType::SortBySize_DESC);
        assignCustomSortOrders(states);
        return;
    }

}

void ListView::sortByType(bool ascending)
{
    {
        QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
        sortAutoArrangeStates(states, ascending ? SortOrderType::SortByType_ASC : SortOrderType::SortByType_DESC);
        assignCustomSortOrders(states);
        return;
    }

}

void ListView::sortByDate(bool ascending)
{
    {
        QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
        sortAutoArrangeStates(states, ascending ? SortOrderType::SortByDateModified_ASC : SortOrderType::SortByDateModified_DESC);
        assignCustomSortOrders(states);
        return;
    }

}

bool ListView::addItem(const QString& filePath, bool refresh)
{
    const QString normalizedPath = normalizeDesktopItemPath(filePath);
    if (normalizedPath.isEmpty()) {
        return false;
    }

    const bool added = !findAnyItemStateByPath(normalizedPath) && addDesktopModelItemFromPath(filePath);
    
    // 同步持久化存储
    if (refresh) {
        requestSaveBoxes();
    }
    return added;
}

bool ListView::addItemNoDelete(const QString& filePath)
{
    const QString normalizedPath = normalizeDesktopItemPath(filePath);
    if (normalizedPath.isEmpty() || findDesktopStateByPath(normalizedPath)) {
        return false;
    }

    return addDesktopModelItemFromPath(filePath);
}

void ListView::removeItem(const QString& filePath)
{
    removeItemPathsBatch({ filePath });
}

void ListView::removeItemNoDelete(const QString& filePath)
{
    setItemPathsTransientHidden({ filePath }, true);
}

void ListView::removeItemsBatch(const QStringList& paths, bool refresh)
{
    removeItemPathsBatch(paths, refresh);
}

void ListView::setItemsTransientHidden(const QStringList& paths, bool hidden)
{
    setItemPathsTransientHidden(paths, hidden);
}

void ListView::trackPendingExternalRemovals(const QStringList& paths)
{
    trackPendingExternalRemovalPaths(paths);
}

bool ListView::consumePendingExternalRemoval(const QString& path)
{
    const QString normalizedPath = normalizeDesktopPath(path);
    if (normalizedPath.isEmpty()) {
        return false;
    }
    return m_pendingExternalRemovalPaths.remove(normalizedPath);
}

DesktopRootItemState* ListView::findItemByPath(const QString& filePath)
{
    return findAnyItemStateByPath(filePath);
}

const DesktopRootItemState* ListView::findItemByPath(const QString& filePath) const
{
    return findAnyItemStateByPath(filePath);
}


void ListView::removeItemPathsBatch(const QStringList& paths, bool refresh)
{
    if (paths.isEmpty()) {
        return;
    }

    QSet<QString> seenPaths;
    QSet<LinBox*> sourceBoxes;
    bool removedAny = false;

    for (const QString& rawPath : paths) {
        const QString normalizedPath = normalizeDesktopItemPath(rawPath);
        if (normalizedPath.isEmpty() || seenPaths.contains(normalizedPath)) {
            continue;
        }
        seenPaths.insert(normalizedPath);

        if (DesktopRootItemState* state = findDesktopStateByPath(normalizedPath)) {
            IconCacheManager::instance().invalidateItemCache(state->model.iconCacheKey, normalizedPath);
            removeDesktopStateByPath(normalizedPath);
            removedAny = true;
            continue;
        }

        if (LinBox* box = BoxManager::instance()->findBoxContainingItem(normalizedPath)) {
            if (box->removeItemState(normalizedPath, false)) {
                sourceBoxes.insert(box);
                removedAny = true;
            }
        }
    }

    if (!removedAny) {
        return;
    }

    for (LinBox* box : sourceBoxes) {
        if (!box) {
            continue;
        }
        box->refreshLayout();
        box->update();
    }

    if (refresh) {
        if (m_iconShowRule.autoArrange) {
            applyAutoArrangeLayout(false, false);
        } else {
            rebuildSpatialIndex();
            update();
        }
    }
}

void ListView::setItemPathsTransientHidden(const QStringList& paths, bool hidden)
{
    if (paths.isEmpty()) {
        return;
    }

    QSet<LinBox*> dirtyBoxes;
    bool desktopDirty = false;
    for (const QString& rawPath : paths) {
        const QString normalizedPath = normalizeDesktopItemPath(rawPath);
        if (normalizedPath.isEmpty()) {
            continue;
        }

        if (DesktopRootItemState* state = findDesktopStateByPath(normalizedPath)) {
            state->model.visible = !hidden;
            desktopDirty = true;
            continue;
        }

        if (LinBox* box = BoxManager::instance()->findBoxContainingItem(normalizedPath)) {
            if (DesktopRootItemState* state = box->findItemStateByPath(normalizedPath)) {
                state->model.visible = !hidden;
                dirtyBoxes.insert(box);
            }
        }
    }

    for (LinBox* box : dirtyBoxes) {
        if (!box) {
            continue;
        }
        box->refreshLayout();
        box->update();
    }

    if (desktopDirty) {
        updateAllItemRenderData();
        rebuildSpatialIndex();
        update();
    }
}

void ListView::trackPendingExternalRemovalPaths(const QStringList& paths)
{
    for (const QString& rawPath : paths) {
        const QString normalizedPath = normalizeDesktopPath(rawPath);
        if (!normalizedPath.isEmpty()) {
            m_pendingExternalRemovalPaths.insert(normalizedPath);
        }
    }
}

void ListView::removeDesktopStateByPath(const QString& filePath)
{
    const QString normalizedPath = normalizeDesktopItemPath(filePath);
    if (normalizedPath.isEmpty()) {
        return;
    }

    for (auto it = m_itemStates.begin(); it != m_itemStates.end(); ) {
        if (normalizeDesktopItemPath(it->model.filePath).compare(normalizedPath, Qt::CaseInsensitive) == 0) {
            IconCacheManager::instance().invalidateItemCache(it->model.iconCacheKey, normalizedPath);
            const QRect dirty = desktopRootItemHighlightDirtyRect(it->model.renderData);
            const QString foundPath = normalizeDesktopItemPath(it->model.filePath);
            it = m_itemStates.erase(it);
            markDesktopStateIndexDirty();
            markSpatialIndexDirty();
            if (m_hoveredDesktopPath.compare(foundPath, Qt::CaseInsensitive) == 0) {
                m_hoveredDesktopPath.clear();
            }
            m_selectedDesktopPathSet.remove(foundPath);
            m_selectedDesktopPaths.removeOne(foundPath);
            m_selectedDesktopPathSet.remove(normalizedPath);
            m_selectedDesktopPaths.removeOne(normalizedPath);
            if (m_focusedDesktopPath.compare(foundPath, Qt::CaseInsensitive) == 0) {
                m_focusedDesktopPath.clear();
            }
            if (m_anchorDesktopPath.compare(foundPath, Qt::CaseInsensitive) == 0) {
                m_anchorDesktopPath.clear();
            }
            if (m_lastClickedDesktopPath.compare(foundPath, Qt::CaseInsensitive) == 0) {
                m_lastClickedDesktopPath.clear();
            }
            if (m_draggedDesktopPath.compare(foundPath, Qt::CaseInsensitive) == 0) {
                m_draggedDesktopPath.clear();
            }
            update(dirty);
            break;
        } else {
            ++it;
        }
    }
}

