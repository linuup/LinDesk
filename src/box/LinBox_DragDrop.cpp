#include "box/LinBox_Internal.h"

// ============================================================================
// LinBox Drag & Drop and Reorder Implementation
// ============================================================================

int LinBox::insertionIndexForPos(const QPoint& boxPos, const QStringList& draggedPaths, bool* alignAfterPrev) const
{
    if (m_itemStates.isEmpty()) {
        if (alignAfterPrev) *alignAfterPrev = false;
        return 0;
    }

    const QPoint contentPos = m_contentArea->mapFromGlobal(mapToGlobal(boxPos));
    QSet<QString> draggedPathSet;
    for (const QString& path : draggedPaths) {
        draggedPathSet.insert(normalizeBoxItemPath(path));
    }

    QVector<const DesktopRootItemState*> orderedItems;
    orderedItems.reserve(m_itemStates.size());
    for (const DesktopRootItemState& state : m_itemStates) {
        if (!draggedPathSet.contains(normalizeBoxItemPath(state.model.filePath))) {
            orderedItems.append(&state);
        }
    }

    if (orderedItems.isEmpty()) {
        if (alignAfterPrev) *alignAfterPrev = false;
        return 0;
    }

    if (m_showType == BoxShowType::List_ShowType) {
        if (alignAfterPrev) *alignAfterPrev = false;
        for (int i = 0; i < orderedItems.size(); ++i) {
            const QRect rect = orderedItems[i]->model.renderData.itemRect;
            if (contentPos.y() < rect.center().y()) {
                return i;
            }
        }
        return orderedItems.size();
    }

    QVector<QVector<int>> rows;
    for (int i = 0; i < orderedItems.size(); ++i) {
        const QRect rect = orderedItems[i]->model.renderData.itemRect;
        bool placed = false;
        for (QVector<int>& row : rows) {
            const QRect firstRect = orderedItems[row.first()]->model.renderData.itemRect;
            if (qAbs(firstRect.top() - rect.top()) <= qMax(6, rect.height() / 3)) {
                row.append(i);
                placed = true;
                break;
            }
        }
        if (!placed) {
            rows.append({i});
        }
    }

    QVector<QRect> rowRects;
    QVector<int> rowCenters;
    rowRects.reserve(rows.size());
    rowCenters.reserve(rows.size());
    for (const QVector<int>& row : rows) {
        QRect rowRect = orderedItems[row.first()]->model.renderData.itemRect;
        for (int idx : row) {
            rowRect = rowRect.united(orderedItems[idx]->model.renderData.itemRect);
        }
        rowRects.append(rowRect);
        rowCenters.append(rowRect.center().y());
    }

    int targetRow = 0;
    for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        const int upperBound = (rowIndex == 0)
            ? INT_MIN
            : (rowCenters[rowIndex - 1] + rowCenters[rowIndex]) / 2;
        const int lowerBound = (rowIndex == rows.size() - 1)
            ? INT_MAX
            : (rowCenters[rowIndex] + rowCenters[rowIndex + 1]) / 2;
        if (contentPos.y() >= upperBound && contentPos.y() < lowerBound) {
            targetRow = rowIndex;
            break;
        }
    }

    const QVector<int>& row = rows[targetRow];
    const QRect firstRect = orderedItems[row.first()]->model.renderData.itemRect;
    const QRect lastRect = orderedItems[row.last()]->model.renderData.itemRect;

    struct InsertionLane {
        int insertIndex = 0;
        bool afterPrev = false;
        int x = 0;
    };

    QVector<InsertionLane> lanes;
    lanes.reserve(row.size() + 1);
    lanes.append({row.first(), false, firstRect.left()});
    for (int i = 0; i < row.size() - 1; ++i) {
        const QRect currentRect = orderedItems[row[i]]->model.renderData.itemRect;
        const QRect nextRect = orderedItems[row[i + 1]]->model.renderData.itemRect;
        lanes.append({row[i] + 1, true, (currentRect.right() + nextRect.left()) / 2});
    }
    const int trailingIndex = (targetRow + 1 < rows.size()) ? rows[targetRow + 1].first() : orderedItems.size();
    lanes.append({trailingIndex, true, lastRect.right() + 1});

    int bestLane = 0;
    int bestDistance = INT_MAX;
    for (int i = 0; i < lanes.size(); ++i) {
        const int distance = qAbs(contentPos.x() - lanes[i].x);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestLane = i;
        }
    }

    if (alignAfterPrev) {
        *alignAfterPrev = lanes[bestLane].afterPrev;
    }
    return lanes[bestLane].insertIndex;
}

QRect LinBox::insertionIndicatorRect() const
{
    if (m_reorderInsertIndex < 0 || !m_contentArea) {
        return QRect();
    }

    QSet<QString> draggedPathSet;
    for (const QString& path : m_reorderDragPaths) {
        draggedPathSet.insert(normalizeBoxItemPath(path));
    }
    QVector<const DesktopRootItemState*> orderedItems;
    orderedItems.reserve(m_itemStates.size());
    for (const DesktopRootItemState& state : m_itemStates) {
        if (!draggedPathSet.contains(normalizeBoxItemPath(state.model.filePath))) {
            orderedItems.append(&state);
        }
    }

    auto itemBorderRectInBox = [this](const DesktopRootItemState* state) -> QRect {
        if (!state) {
            return QRect();
        }
        const QRect contentRect = state->model.renderData.itemRect;
        const QPoint boxTopLeft = m_contentArea->mapTo(this, contentRect.topLeft());
        return QRect(boxTopLeft, contentRect.size());
    };

    if (m_showType == BoxShowType::List_ShowType) {
        if (orderedItems.isEmpty()) {
            return QRect(scaledPx(this, kBaseItemMarginLeft), m_titleHeight + scaledPx(this, kBaseItemMarginTop), qMax(scaledPx(this, 20), width() - scaledPx(this, 10)), scaledPx(this, kBaseIndicatorLine));
        }

        if (m_reorderInsertIndex >= orderedItems.size()) {
            const QRect rect = itemBorderRectInBox(orderedItems.last());
            return QRect(rect.left(), rect.bottom() + scaledPx(this, 2), rect.width(), scaledPx(this, kBaseIndicatorLine));
        }

        const QRect rect = itemBorderRectInBox(orderedItems[m_reorderInsertIndex]);
        return QRect(rect.left(), rect.top() - scaledPx(this, 2), rect.width(), scaledPx(this, kBaseIndicatorLine));
    }

    if (orderedItems.isEmpty()) {
        return QRect(m_contentArea->x() + scaledPx(this, kBaseItemMarginLeft), m_contentArea->y() + scaledPx(this, kBaseItemMarginTop), scaledPx(this, 3), scaledPx(this, kBaseItemHeight));
    }

    if (m_reorderInsertIndex <= 0) {
        const QRect firstRect = itemBorderRectInBox(orderedItems.first());
        return QRect(firstRect.left() - scaledPx(this, 3), firstRect.top(), scaledPx(this, 3), firstRect.height());
    }

    if (m_reorderInsertIndex >= orderedItems.size()) {
        const QRect lastRect = itemBorderRectInBox(orderedItems.last());
        return QRect(lastRect.right() + scaledPx(this, 2), lastRect.top(), scaledPx(this, 3), lastRect.height());
    }

    const QRect prevRect = itemBorderRectInBox(orderedItems[m_reorderInsertIndex - 1]);
    const QRect nextRect = itemBorderRectInBox(orderedItems[m_reorderInsertIndex]);
    if (m_reorderIndicatorAfterPrev) {
        return QRect(prevRect.right() + scaledPx(this, 2), prevRect.top(), scaledPx(this, 3), prevRect.height());
    }
    return QRect(nextRect.left() - scaledPx(this, 3), nextRect.top(), scaledPx(this, 3), nextRect.height());
}

void LinBox::clearReorderPreview()
{
    if (!m_reorderDragPaths.isEmpty() || m_reorderInsertIndex >= 0) {
        m_reorderDragPaths.clear();
        m_reorderInsertIndex = -1;
        m_reorderIndicatorAfterPrev = false;
        update();
    }
}

void LinBox::showDropTipForItem(const DesktopRootItemState* state, const QPoint& boxPos, bool isCtrl, bool createShortcut)
{
    if (!state) {
        if (m_dragTip) m_dragTip->hide();
        return;
    }

    if (!m_dragTip) {
        m_dragTip = new FluentDragTip();
    }

    const DesktopRootItemModel& model = state->model;
    const QString clsid = model.itemData.getCLSID();
    const bool isRecycleBin = (clsid == APP_RECYCLE_BIN_CLS);
    const bool isPC = (clsid == APP_MY_COMPUTER_CLS);
    const bool isNetwork = (clsid == APP_NETWORK_CLS);
    const bool isControlPanel = (clsid == APP_CONTROL_PANEL_CLS);
    const bool isFolder = (model.objectType == ObjectType::Folder_ObjectType ||
                           model.objectType == ObjectType::SpecialFolder ||
                           model.objectType == ObjectType::DiskLetter);

    if (isPC || isNetwork || isControlPanel) {
        m_dragTip->setText("<b style='color:#ff4d4d'>Operation Forbidden</b>");
    } else {
        QString actionKeyword;
        QString highlightColor = "#70a1ff";
        QString htmlText;
        QString draggedPath;
        DesktopRootItemModel draggedModel;
        bool isDraggedDiskLetter = false;
        if (ListView* lv = ListView::instance()) {
            draggedPath = lv->draggedItemPath();
            isDraggedDiskLetter = !draggedPath.isEmpty() &&
                lv->resolveItemModelByPath(draggedPath, draggedModel) &&
                draggedModel.objectType == ObjectType::DiskLetter;
        }

        if (isRecycleBin) {
            actionKeyword = tr("Move to");
            highlightColor = "#ff4d4d";
            htmlText = QString("<b style='color:%1'>%2</b> <span>%3</span>")
                .arg(highlightColor, actionKeyword, model.displayName);
        } else if (isFolder && createShortcut) {
            actionKeyword = tr("Create shortcut in");
            htmlText = QString("<b style='color:%1'>%2</b> <span>%3</span>")
                .arg(highlightColor, actionKeyword, model.displayName);
        } else if (isFolder && isDraggedDiskLetter) {
            htmlText = QString("<b style='color:%1'>Open</b> <span>%2</span> <b style='color:%1'>here</b>")
                .arg(highlightColor, model.displayName);
        } else if (isFolder) {
            actionKeyword = isCtrl ? tr("Copy to") : tr("Move to");
            htmlText = QString("<b style='color:%1'>%2</b> <span>%3</span>")
                .arg(highlightColor, actionKeyword, model.displayName);
        } else {
            actionKeyword = tr("Open with");
            htmlText = QString("<b style='color:%1'>%2</b> <span>%3</span>")
                .arg(highlightColor, actionKeyword, model.displayName);
        }
        m_dragTip->setText(htmlText);
    }

    QPoint targetPos = boxPos + QPoint(scaledPx(this, kBaseDragTipOffset), scaledPx(this, kBaseDragTipOffset));
    if (targetPos.x() + m_dragTip->width() > width()) targetPos.setX(boxPos.x() - m_dragTip->width() - scaledPx(this, kBaseDragTipMargin));
    if (targetPos.y() + m_dragTip->height() > height()) targetPos.setY(boxPos.y() - m_dragTip->height() - scaledPx(this, kBaseDragTipMargin));
    m_dragTip->showAt(mapToGlobal(targetPos));
}

bool LinBox::handleMappedBoxItemTargetDrop(const QStringList& sourcePaths,
                                           const DesktopRootItemState* targetState,
                                           bool copyMode,
                                           bool preferShortcut,
                                           QDropEvent* event)
{
    ListView* lv = ListView::instance();
    if (!lv || !event || !isFolderMapped() || !targetState) {
        return false;
    }

    const DesktopRootItemModel& targetModel = targetState->model;
    const QString targetPath = QDir::toNativeSeparators(targetModel.filePath);
    const QStringList operationPaths = sourcePaths;
    const QString operationName = preferShortcut
        ? QStringLiteral("shortcut")
        : (copyMode ? QStringLiteral("copy") : QStringLiteral("move"));
    const auto finishIgnored = [&]() {
        qDebug() << "[MappedBoxDrop] result=ignored";
        event->ignore();
        return true;
    };

    qDebug() << "[MappedBoxDrop] target item"
             << "sources=" << operationPaths
             << "target=" << targetPath
             << "type=" << static_cast<int>(targetModel.objectType)
             << "operation=" << operationName;

    if (operationPaths.isEmpty() || targetPath.isEmpty()) {
        qDebug() << "[MappedBoxDrop] ignored empty source or target";
        return finishIgnored();
    }

    if (isForbiddenDropTarget(targetModel)) {
        qDebug() << "[MappedBoxDrop] forbidden target"
                 << "operation=" << operationName;
        event->setDropAction(Qt::IgnoreAction);
        return finishIgnored();
    }

    for (const QString& sourcePath : operationPaths) {
        if (normalizeBoxItemPath(sourcePath) == normalizeBoxItemPath(targetPath)) {
            qDebug() << "[MappedBoxDrop] ignored self target" << targetPath;
            return finishIgnored();
        }
    }

    const Qt::DropAction dropAction = preferShortcut
        ? Qt::LinkAction
        : (copyMode ? Qt::CopyAction : Qt::MoveAction);

    if (isRecycleBinDropTarget(targetModel)) {
        ListView::DropInteractionOptions options;
        options.copyMode = copyMode;
        options.preferShortcut = preferShortcut;
        options.finalizeMovedSourcesAfterSuccess = true;
        const bool ok = lv->handleDroppedPathsOnItemTarget(operationPaths, targetModel, options);
        qDebug() << "[MappedBoxDrop] recycle operation result"
                 << "target=" << targetPath
                 << "operation=" << operationName
                 << "result=" << ok;

        if (!ok) {
            return finishIgnored();
        }

        lv->reloadMappedFolderBox(this);
        refreshLayout();
        update();
        raise();
        lv->requestSaveBoxes();
        event->setDropAction(dropAction);
        event->accept();
        return true;
    }

    if (isFolderDropTarget(targetModel)) {
        const QString targetFolder = normalizeMappedFolderPath(targetPath);
        const QStringList srcPaths = toNativeQStringListPaths(operationPaths);
        if (targetFolder.isEmpty() || srcPaths.isEmpty()) {
            qDebug() << "[MappedBoxDrop] ignored invalid folder operation"
                     << "targetFolder=" << targetFolder;
            return finishIgnored();
        }

        bool ok = false;
        if (preferShortcut) {
            ok = lv->createShortcutsInFolder(operationPaths, targetFolder);
        } else if (copyMode) {
            ok = CSystemApi::CopyFiles(srcPaths, targetFolder);
        } else {
            ok = CSystemApi::MoveFiles(srcPaths, targetFolder);
        }

        qDebug() << "[MappedBoxDrop] folder operation result"
                 << "targetFolder=" << targetFolder
                 << "operation=" << operationName
                 << "result=" << ok;

        if (!ok) {
            return finishIgnored();
        }

        if (!copyMode && !preferShortcut) {
            lv->finalizeMovedModelSourcesAfterFileOp(operationPaths);
        }
        lv->reloadMappedFolderBox(this);
        refreshLayout();
        update();
        raise();
        lv->requestSaveBoxes();
        event->setDropAction(dropAction);
        event->accept();
        return true;
    }

    ListView::DropInteractionOptions options;
    options.copyMode = copyMode;
    options.preferShortcut = preferShortcut;
    options.finalizeMovedSourcesAfterSuccess = true;
    const bool ok = lv->handleDroppedPathsOnItemTarget(operationPaths, targetModel, options);
    qDebug() << "[MappedBoxDrop] generic item operation result"
             << "target=" << targetPath
             << "operation=" << operationName
             << "result=" << ok;

    if (!ok) {
        return finishIgnored();
    }

    event->setDropAction(dropAction);
    event->accept();
    return true;
}

void LinBox::dragEnterEvent(QDragEnterEvent* event) {
    if (m_activeDragPulse && !m_isDragHovered) {
        m_isDragHovered = true;
        update();
    }
    if (!event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }

    ListView* lv = ListView::instance();
    const Qt::KeyboardModifiers effectiveModifiers = effectiveDragModifiers(event->keyboardModifiers());
    const bool isCtrl = (effectiveModifiers & Qt::ControlModifier);
    const bool isInternalDrag = isDesktopOrBoxDragSource(event->source());
    const QStringList paths = lv ? lv->resolveDraggedLocalPaths(event->mimeData()) : QStringList();
    const DesktopRootItemState* hitState = dropTargetItemStateAt(event->pos());
    const QStringList orderedPaths = (isInternalDrag && lv)
        ? lv->orderedDraggedPaths(paths)
        : QStringList();
    const bool preferShortcutForDrop = (isFolderMapped() && lv)
        ? shouldCreateShortcutForMappedFolderDrop(lv, isInternalDrag, orderedPaths, paths, effectiveModifiers)
        : (lv && lv->shouldPreferShortcutForDrop(paths, effectiveModifiers, !isInternalDrag));
    const QString dropTargetText = isFolderMapped() ? mappedFolderDisplayText() : boxTitle();
    auto pathSetContains = [this](const QStringList& valuePaths, const QString& targetPath) {
        const QString normalizedTarget = normalizeBoxItemPath(targetPath);
        for (const QString& path : valuePaths) {
            if (normalizeBoxItemPath(path) == normalizedTarget) {
                return true;
            }
        }
        return false;
    };

    if (hitState && !pathSetContains(paths, hitState->model.filePath)) {
        clearReorderPreview();
        const QString clsid = hitState->model.itemData.getCLSID();
        const bool isForbiddenTarget = (clsid == APP_MY_COMPUTER_CLS || clsid == APP_NETWORK_CLS || clsid == APP_CONTROL_PANEL_CLS);
        const bool isFolderTarget = (hitState->model.objectType == ObjectType::Folder_ObjectType ||
                                     hitState->model.objectType == ObjectType::SpecialFolder ||
                                     hitState->model.objectType == ObjectType::DiskLetter);
        const bool isRecycleBinTarget = (clsid == APP_RECYCLE_BIN_CLS);
        event->setDropAction(isForbiddenTarget ? Qt::IgnoreAction : (preferShortcutForDrop ? Qt::LinkAction : (isCtrl ? Qt::CopyAction : Qt::MoveAction)));
        if (isForbiddenTarget) {
            ListViewFileOpsHelper::updateInternalDragDescription(event->mimeData(), event->dropAction(), hitState->model.displayName, true);
        } else if (isRecycleBinTarget) {
            ListViewFileOpsHelper::updateInternalDragDescriptionCustom(event->mimeData(), DROPIMAGE_MOVE, QCoreApplication::translate("LinBox", "Move to %1"), hitState->model.displayName);
        } else if (isFolderTarget && preferShortcutForDrop) {
            ListViewFileOpsHelper::updateInternalDragDescriptionCustom(event->mimeData(), DROPIMAGE_LINK, QCoreApplication::translate("LinBox", "Create shortcut in %1"), hitState->model.displayName);
        } else if (!isFolderTarget) {
            ListViewFileOpsHelper::updateInternalDragDescriptionCustom(event->mimeData(), DROPIMAGE_LABEL, QCoreApplication::translate("LinBox", "Open with %1"), hitState->model.displayName);
        } else {
            ListViewFileOpsHelper::updateInternalDragDescription(event->mimeData(), event->dropAction(), hitState->model.displayName);
        }
        event->accept();
        showDropTipForItem(hitState, event->pos(), isCtrl, preferShortcutForDrop);
        return;
    }

    if (isFolderMapped()) {
        clearReorderPreview();
        if (!paths.isEmpty() && lv) {
            lv->resetRenameClickState();
        }
        if (mappedFolderState() != MappedFolderState::Normal) {
            if (m_dragTip) m_dragTip->hide();
            event->ignore();
            return;
        }
        const QString targetFolder = normalizeMappedFolderPath(mappedFolderPath());
        const bool isInternalSameFolderMove = isInternalDrag && !isCtrl && !preferShortcutForDrop && areAllPathsDirectChildrenOfFolder(paths, targetFolder);
        if (isInternalSameFolderMove) {
            lv->resetRenameClickState();
            m_reorderDragPaths = paths;
            m_reorderInsertIndex = insertionIndexForPos(event->pos(), paths, &m_reorderIndicatorAfterPrev);
            if (m_dragTip) m_dragTip->hide();
            event->setDropAction(Qt::MoveAction);
            ListViewFileOpsHelper::updateInternalDragDescription(event->mimeData(), event->dropAction(), dropTargetText);
            event->accept();
            update();
            return;
        }
        event->setDropAction(preferShortcutForDrop ? Qt::LinkAction : (isCtrl ? Qt::CopyAction : Qt::MoveAction));
        ListViewFileOpsHelper::updateInternalDragDescription(event->mimeData(), event->dropAction(), dropTargetText);
        event->accept();
        updateDragTip(event->pos(), isCtrl, preferShortcutForDrop);
        return;
    }

    if (!paths.isEmpty() && isInternalDrag && !preferShortcutForDrop && !isCtrl) {
        lv->resetRenameClickState();
        m_reorderDragPaths = paths;
        m_reorderInsertIndex = insertionIndexForPos(event->pos(), paths, &m_reorderIndicatorAfterPrev);
        if (m_dragTip) m_dragTip->hide();
        event->setDropAction(Qt::MoveAction);
        ListViewFileOpsHelper::updateInternalDragDescription(event->mimeData(), event->dropAction(), dropTargetText);
        event->accept();
        update();
        return;
    }

    const QString clsid = hitState ? hitState->model.itemData.getCLSID() : QString();
    const bool isForbiddenTarget = (clsid == APP_MY_COMPUTER_CLS || clsid == APP_NETWORK_CLS || clsid == APP_CONTROL_PANEL_CLS);
    event->setDropAction(isForbiddenTarget ? Qt::IgnoreAction : (preferShortcutForDrop ? Qt::LinkAction : (isCtrl ? Qt::CopyAction : Qt::MoveAction)));
    ListViewFileOpsHelper::updateInternalDragDescription(event->mimeData(), event->dropAction(), dropTargetText, isForbiddenTarget);
    event->accept();

    if (hitState) {
        showDropTipForItem(hitState, event->pos(), isCtrl, preferShortcutForDrop);
    } else if (isInternalDrag && !preferShortcutForDrop) {
        if (m_dragTip) m_dragTip->hide();
    } else {
        updateDragTip(event->pos(), isCtrl, preferShortcutForDrop);
    }
}

void LinBox::dragLeaveEvent(QDragLeaveEvent* event) {
    if (m_isDragHovered) {
        m_isDragHovered = false;
        update();
    }
    if (m_dragTip) m_dragTip->hide();
    clearReorderPreview();
}

void LinBox::clearDropPreview()
{
    if (m_isDragHovered) {
        m_isDragHovered = false;
        update();
    }
    if (m_dragTip) {
        m_dragTip->hide();
    }
    clearReorderPreview();
}

void LinBox::previewGhostDropAt(const QPoint& boxPos, const QStringList& draggedPaths, Qt::KeyboardModifiers modifiers)
{
    if (m_activeDragPulse && !m_isDragHovered) {
        m_isDragHovered = true;
        update();
    }
    ListView* lv = ListView::instance();
    if (!lv || draggedPaths.isEmpty()) {
        clearDropPreview();
        return;
    }

    const Qt::KeyboardModifiers effectiveModifiers = effectiveDragModifiers(modifiers);
    const bool isCtrl = (effectiveModifiers & Qt::ControlModifier);
    const DesktopRootItemState* hitState = dropTargetItemStateAt(boxPos);
    const QStringList orderedPaths = lv->orderedDraggedPaths(draggedPaths);
    const bool preferShortcutForDrop = isFolderMapped()
        ? shouldCreateShortcutForMappedFolderDrop(lv, true, orderedPaths, draggedPaths, effectiveModifiers)
        : lv->shouldPreferShortcutForDrop(draggedPaths, effectiveModifiers, false);

    auto pathSetContains = [this](const QStringList& valuePaths, const QString& targetPath) {
        const QString normalizedTarget = normalizeBoxItemPath(targetPath);
        for (const QString& path : valuePaths) {
            if (normalizeBoxItemPath(path) == normalizedTarget) {
                return true;
            }
        }
        return false;
    };

    if (hitState && !pathSetContains(draggedPaths, hitState->model.filePath)) {
        clearReorderPreview();
        showDropTipForItem(hitState, boxPos, isCtrl, preferShortcutForDrop);
        return;
    }

    if (isFolderMapped()) {
        clearReorderPreview();
        const QString targetFolder = normalizeMappedFolderPath(mappedFolderPath());
        const bool isInternalSameFolderMove =
            !isCtrl &&
            !preferShortcutForDrop &&
            areAllPathsDirectChildrenOfFolder(draggedPaths, targetFolder);
        if (isInternalSameFolderMove) {
            m_reorderDragPaths = draggedPaths;
            m_reorderInsertIndex = insertionIndexForPos(boxPos, draggedPaths, &m_reorderIndicatorAfterPrev);
            if (m_dragTip) {
                m_dragTip->hide();
            }
            update();
            return;
        }
        updateDragTip(boxPos, isCtrl, preferShortcutForDrop);
        return;
    }

    if (!preferShortcutForDrop && !isCtrl) {
        m_reorderDragPaths = draggedPaths;
        m_reorderInsertIndex = insertionIndexForPos(boxPos, draggedPaths, &m_reorderIndicatorAfterPrev);
        if (m_dragTip) {
            m_dragTip->hide();
        }
        update();
        return;
    }

    clearReorderPreview();
    updateDragTip(boxPos, isCtrl, preferShortcutForDrop);
}

void LinBox::dragMoveEvent(QDragMoveEvent* event) {
    if (!event->mimeData()->hasUrls()) {
        if (m_dragTip) m_dragTip->hide();
        event->ignore();
        return;
    }

    if (m_activeDragPulse && !m_isDragHovered) {
        m_isDragHovered = true;
        update();
    }

    ListView* lv = ListView::instance();
    const Qt::KeyboardModifiers effectiveModifiers = effectiveDragModifiers(event->keyboardModifiers());
    const bool isCtrl = (effectiveModifiers & Qt::ControlModifier);
    const bool isInternalDrag = isDesktopOrBoxDragSource(event->source());
    const QStringList paths = lv ? lv->resolveDraggedLocalPaths(event->mimeData()) : QStringList();
    const DesktopRootItemState* hitState = dropTargetItemStateAt(event->pos());
    const QStringList orderedPaths = (isInternalDrag && lv)
        ? lv->orderedDraggedPaths(paths)
        : QStringList();
    const bool preferShortcutForDrop = (isFolderMapped() && lv)
        ? shouldCreateShortcutForMappedFolderDrop(lv, isInternalDrag, orderedPaths, paths, effectiveModifiers)
        : (lv && lv->shouldPreferShortcutForDrop(paths, effectiveModifiers, !isInternalDrag));
    const QString dropTargetText = isFolderMapped() ? mappedFolderDisplayText() : boxTitle();
    auto pathSetContains = [this](const QStringList& valuePaths, const QString& targetPath) {
        const QString normalizedTarget = normalizeBoxItemPath(targetPath);
        for (const QString& path : valuePaths) {
            if (normalizeBoxItemPath(path) == normalizedTarget) {
                return true;
            }
        }
        return false;
    };

    if (hitState && !pathSetContains(paths, hitState->model.filePath)) {
        clearReorderPreview();
        const QString clsid = hitState->model.itemData.getCLSID();
        const bool isForbiddenTarget = (clsid == APP_MY_COMPUTER_CLS || clsid == APP_NETWORK_CLS || clsid == APP_CONTROL_PANEL_CLS);
        const bool isFolderTarget = (hitState->model.objectType == ObjectType::Folder_ObjectType ||
                                     hitState->model.objectType == ObjectType::SpecialFolder ||
                                     hitState->model.objectType == ObjectType::DiskLetter);
        const bool isRecycleBinTarget = (clsid == APP_RECYCLE_BIN_CLS);
        event->setDropAction(isForbiddenTarget ? Qt::IgnoreAction : (preferShortcutForDrop ? Qt::LinkAction : (isCtrl ? Qt::CopyAction : Qt::MoveAction)));
        if (isForbiddenTarget) {
            ListViewFileOpsHelper::updateInternalDragDescription(event->mimeData(), event->dropAction(), hitState->model.displayName, true);
        } else if (isRecycleBinTarget) {
            ListViewFileOpsHelper::updateInternalDragDescriptionCustom(event->mimeData(), DROPIMAGE_MOVE, QCoreApplication::translate("LinBox", "Move to %1"), hitState->model.displayName);
        } else if (isFolderTarget && preferShortcutForDrop) {
            ListViewFileOpsHelper::updateInternalDragDescriptionCustom(event->mimeData(), DROPIMAGE_LINK, QCoreApplication::translate("LinBox", "Create shortcut in %1"), hitState->model.displayName);
        } else if (!isFolderTarget) {
            ListViewFileOpsHelper::updateInternalDragDescriptionCustom(event->mimeData(), DROPIMAGE_LABEL, QCoreApplication::translate("LinBox", "Open with %1"), hitState->model.displayName);
        } else {
            ListViewFileOpsHelper::updateInternalDragDescription(event->mimeData(), event->dropAction(), hitState->model.displayName);
        }
        event->accept();
        showDropTipForItem(hitState, event->pos(), isCtrl, preferShortcutForDrop);
        return;
    }

    if (isFolderMapped()) {
        clearReorderPreview();
        if (!paths.isEmpty() && lv) {
            lv->resetRenameClickState();
        }
        if (mappedFolderState() != MappedFolderState::Normal) {
            if (m_dragTip) m_dragTip->hide();
            event->ignore();
            return;
        }
        const QString targetFolder = normalizeMappedFolderPath(mappedFolderPath());
        const bool isInternalSameFolderMove = isInternalDrag && !isCtrl && !preferShortcutForDrop && areAllPathsDirectChildrenOfFolder(paths, targetFolder);
        if (isInternalSameFolderMove) {
            if (m_dragTip) m_dragTip->hide();
            event->accept();
            return;
        }
        event->setDropAction(preferShortcutForDrop ? Qt::LinkAction : (isCtrl ? Qt::CopyAction : Qt::MoveAction));
        ListViewFileOpsHelper::updateInternalDragDescription(event->mimeData(), event->dropAction(), dropTargetText);
        event->accept();
        updateDragTip(event->pos(), isCtrl, preferShortcutForDrop);
        return;
    }

    if (!paths.isEmpty() && isInternalDrag && !preferShortcutForDrop && !isCtrl) {
        lv->resetRenameClickState();
        m_reorderDragPaths = paths;
        m_reorderInsertIndex = insertionIndexForPos(event->pos(), paths, &m_reorderIndicatorAfterPrev);
        if (m_dragTip) m_dragTip->hide();
        event->setDropAction(Qt::MoveAction);
        ListViewFileOpsHelper::updateInternalDragDescription(event->mimeData(), event->dropAction(), dropTargetText);
        event->accept();
        update();
        return;
    }

    const QString clsid = hitState ? hitState->model.itemData.getCLSID() : QString();
    const bool isForbiddenTarget = (clsid == APP_MY_COMPUTER_CLS || clsid == APP_NETWORK_CLS || clsid == APP_CONTROL_PANEL_CLS);
    event->setDropAction(isForbiddenTarget ? Qt::IgnoreAction : (preferShortcutForDrop ? Qt::LinkAction : (isCtrl ? Qt::CopyAction : Qt::MoveAction)));
    ListViewFileOpsHelper::updateInternalDragDescription(event->mimeData(), event->dropAction(), dropTargetText, isForbiddenTarget);
    event->accept();

    if (hitState) {
        showDropTipForItem(hitState, event->pos(), isCtrl, preferShortcutForDrop);
    } else if (isInternalDrag && !preferShortcutForDrop) {
        if (m_dragTip) m_dragTip->hide();
    } else {
        updateDragTip(event->pos(), isCtrl, preferShortcutForDrop);
    }
}

void LinBox::dropEvent(QDropEvent* event) {
    if (m_isDragHovered) {
        m_isDragHovered = false;
        update();
    }
    if (m_dragTip) m_dragTip->hide();
    const QMimeData* mime = event->mimeData();
    if (!mime->hasUrls()) {
        clearReorderPreview();
        return;
    }

    ListView* lv = ListView::instance();
    if (!lv) return;

    const QStringList paths = lv->resolveDraggedLocalPaths(mime);
    if (paths.isEmpty()) {
        event->ignore();
        return;
    }

    const bool isInternalDrag = isDesktopOrBoxDragSource(event->source());
    const Qt::KeyboardModifiers effectiveModifiers = effectiveDragModifiers(event->keyboardModifiers());
    const bool copyMode = (effectiveModifiers & Qt::ControlModifier);
    const QStringList orderedPaths = isInternalDrag
        ? lv->orderedDraggedPaths(paths)
        : QStringList();
    const QStringList operationPaths = orderedPaths.isEmpty() ? paths : orderedPaths;
    const bool preferShortcutForDrop = isFolderMapped()
        ? shouldCreateShortcutForMappedFolderDrop(lv, isInternalDrag, orderedPaths, paths, effectiveModifiers)
        : lv->shouldPreferShortcutForDrop(paths, effectiveModifiers, !isInternalDrag);
    const DesktopRootItemState* hitState = dropTargetItemStateAt(event->pos());
    auto pathSetContains = [this](const QStringList& valuePaths, const QString& targetPath) {
        const QString normalizedTarget = normalizeBoxItemPath(targetPath);
        for (const QString& path : valuePaths) {
            if (normalizeBoxItemPath(path) == normalizedTarget) {
                return true;
            }
        }
        return false;
    };

    if (isFolderMapped() && hitState) {
        clearReorderPreview();
        if (pathSetContains(paths, hitState->model.filePath)) {
            qDebug() << "[MappedBoxDrop] ignored selected target"
                     << "sources=" << paths
                     << "target=" << hitState->model.filePath;
            event->ignore();
            return;
        }
        handleMappedBoxItemTargetDrop(operationPaths, hitState, copyMode, preferShortcutForDrop, event);
        return;
    }

    if (hitState && !pathSetContains(paths, hitState->model.filePath)) {
        clearReorderPreview();
        ListView::DropInteractionOptions options;
        options.copyMode = copyMode;
        options.preferShortcut = preferShortcutForDrop;
        options.finalizeMovedSourcesAfterSuccess = isInternalDrag;
        if (lv->handleDroppedPathsOnItemTarget(operationPaths, hitState->model, options)) {
            refreshLayout();
            update();
            raise();
            lv->requestSaveBoxes();
            event->setDropAction(preferShortcutForDrop ? Qt::LinkAction : (copyMode ? Qt::CopyAction : Qt::MoveAction));
            event->accept();
        } else {
            event->ignore();
        }
        return;
    }

    if (isFolderMapped()) {
        clearReorderPreview();
        if (mappedFolderState() != MappedFolderState::Normal) {
            qDebug() << "[MappedBoxDrop] target background"
                     << "sources=" << operationPaths
                     << "target=" << mappedFolderPath()
                     << "type=background"
                     << "operation=ignored"
                     << "result=invalid mapped folder";
            event->ignore();
            return;
        }

        const QString targetFolder = normalizeMappedFolderPath(mappedFolderPath());
        const QString operationName = preferShortcutForDrop
            ? QStringLiteral("shortcut")
            : (copyMode ? QStringLiteral("copy") : QStringLiteral("move"));
        const bool isInternalSameFolderMove = isInternalDrag && !copyMode && !preferShortcutForDrop && areAllPathsDirectChildrenOfFolder(paths, targetFolder);
        if (isInternalSameFolderMove) {
            const int insertIndex = insertionIndexForPos(event->pos(), paths, &m_reorderIndicatorAfterPrev);
            clearReorderPreview();
            QSet<QString> draggedPathSet;
            QVector<DesktopRootItemState> draggedStates;
            for (const QString& path : operationPaths) {
                draggedPathSet.insert(normalizeBoxItemPath(path));
            }
            for (int i = 0; i < m_itemStates.size();) {
                if (draggedPathSet.contains(normalizeBoxItemPath(m_itemStates[i].model.filePath))) {
                    draggedStates.append(m_itemStates.takeAt(i));
                } else {
                    ++i;
                }
            }
            const int clampedIndex = qBound(0, insertIndex, m_itemStates.size());
            for (int i = 0; i < draggedStates.size(); ++i) {
                m_itemStates.insert(clampedIndex + i, draggedStates[i]);
            }
            m_lastLayoutItemCount = -1;
            refreshLayout();
            raise();
            lv->requestSaveBoxes();
            event->setDropAction(Qt::MoveAction);
            event->accept();
            return;
        }
        const QStringList srcPaths = toNativeQStringListPaths(operationPaths);
        if (targetFolder.isEmpty() || srcPaths.isEmpty()) {
            qDebug() << "[MappedBoxDrop] target background"
                     << "sources=" << operationPaths
                     << "target=" << targetFolder
                     << "type=background"
                     << "operation=" << operationName
                     << "result=invalid paths";
            event->ignore();
            return;
        }

        bool ok = false;
        if (preferShortcutForDrop) {
            ok = lv->createShortcutsInFolder(operationPaths, targetFolder);
        } else if (copyMode) {
            ok = CSystemApi::CopyFiles(srcPaths, targetFolder);
        } else {
            ok = CSystemApi::MoveFiles(srcPaths, targetFolder);
        }

        qDebug() << "[MappedBoxDrop] target background"
                 << "sources=" << operationPaths
                 << "target=" << targetFolder
                 << "type=background"
                 << "operation=" << operationName
                 << "result=" << ok;

        if (ok && !copyMode && !preferShortcutForDrop) {
            lv->finalizeMovedModelSourcesAfterFileOp(operationPaths);
        }

        if (ok) {
            lv->reloadMappedFolderBox(this);
            refreshLayout();
            update();
            raise();
            lv->requestSaveBoxes();
            event->setDropAction(preferShortcutForDrop ? Qt::LinkAction : (copyMode ? Qt::CopyAction : Qt::MoveAction));
            event->accept();
        } else {
            event->ignore();
        }
        return;
    }

    if (isInternalDrag) {
        lv->resetRenameClickState();
        const int insertIndex = insertionIndexForPos(event->pos(), paths, &m_reorderIndicatorAfterPrev);
        clearReorderPreview();

        if (preferShortcutForDrop) {
            if (lv->importPathsToBox(operationPaths, this, true, insertIndex, true)) {
                event->setDropAction(Qt::LinkAction);
                event->accept();
            } else {
                event->ignore();
            }
            return;
        }

        bool draggedPathsContainMappedSource = false;
        for (const QString& path : paths) {
            if (LinBox* sourceBox = BoxManager::instance()->findBoxContainingItem(path)) {
                if (sourceBox->isFolderMapped()) {
                    draggedPathsContainMappedSource = true;
                    break;
                }
            }
        }

        if (draggedPathsContainMappedSource) {
            if (lv->importPathsToBox(operationPaths, this, copyMode, insertIndex, preferShortcutForDrop)) {
                refreshLayout();
                update();
                raise();
                lv->requestSaveBoxes();
                event->setDropAction(preferShortcutForDrop ? Qt::LinkAction : (copyMode ? Qt::CopyAction : Qt::MoveAction));
                event->accept();
            } else {
                event->ignore();
            }
            return;
        }

        if (copyMode) {
            if (lv->importPathsToBox(operationPaths, this, true, insertIndex, false)) {
                event->setDropAction(Qt::CopyAction);
                event->accept();
            } else {
                event->ignore();
            }
            return;
        }

        bool allFromThisBox = true;
          for (const QString& path : operationPaths) {
            if (!findItemStateByPath(path)) {
                allFromThisBox = false;
                break;
            }
        }

        if (allFromThisBox) {
            QSet<QString> draggedPathSet;
            QVector<DesktopRootItemState> draggedStates;
              for (const QString& path : operationPaths) {
                draggedPathSet.insert(normalizeBoxItemPath(path));
            }
            for (int i = 0; i < m_itemStates.size();) {
                if (draggedPathSet.contains(normalizeBoxItemPath(m_itemStates[i].model.filePath))) {
                    draggedStates.append(m_itemStates.takeAt(i));
                } else {
                    ++i;
                }
            }
            const int clampedIndex = qBound(0, insertIndex, m_itemStates.size());
            for (int i = 0; i < draggedStates.size(); ++i) {
                m_itemStates.insert(clampedIndex + i, draggedStates[i]);
            }
            m_lastLayoutItemCount = -1;
            refreshLayout();
            raise();
            lv->requestSaveBoxes();
            event->setDropAction(Qt::MoveAction);
            event->accept();
            return;
        }

          for (int i = 0; i < operationPaths.size(); ++i) {
              lv->moveItemToBox(operationPaths[i], this, false, insertIndex >= 0 ? insertIndex + i : -1);
          }
        refreshLayout();
        raise();
        lv->requestSaveBoxes();
        event->setDropAction(Qt::MoveAction);
        event->accept();
        return;
    }

    if (lv->importPathsToBox(operationPaths, this, copyMode, -1, preferShortcutForDrop)) {
        event->setDropAction(preferShortcutForDrop ? Qt::LinkAction : (copyMode ? Qt::CopyAction : Qt::MoveAction));
        event->accept();
    } else {
        event->ignore();
    }
    clearReorderPreview();
}


void LinBox::updateDragTip(const QPoint& pos, bool isCtrl, bool createShortcut) {
    if (!m_dragTip) {
        m_dragTip = new FluentDragTip();
    }

    QString targetText = tr("this Box");
    if (isFolderMapped()) {
        const QFileInfo mappedInfo(m_mappedFolderPath);
        const QString folderName = mappedInfo.fileName().isEmpty() ? m_mappedFolderPath : mappedInfo.fileName();
        targetText = folderName;
    }

    if (createShortcut) {
        m_dragTip->setText(QString("<b style='color:#70a1ff'>%1</b> <span>%2</span>").arg(tr("Create shortcut in"), targetText));
    } else {
        m_dragTip->setText(isCtrl
            ? QString("<b style='color:#70a1ff'>%1</b> <span>%2</span>").arg(tr("Copy to"), targetText)
            : QString("<b style='color:#70a1ff'>%1</b> <span>%2</span>").arg(tr("Move to"), targetText));
    }

    QPoint targetPos = pos + QPoint(scaledPx(this, kBaseDragTipOffset), scaledPx(this, kBaseDragTipOffset));
    if (targetPos.x() + m_dragTip->width() > width()) targetPos.setX(pos.x() - m_dragTip->width() - scaledPx(this, kBaseDragTipMargin));
    if (targetPos.y() + m_dragTip->height() > height()) targetPos.setY(pos.y() - m_dragTip->height() - scaledPx(this, kBaseDragTipMargin));

    m_dragTip->showAt(mapToGlobal(targetPos));
}


