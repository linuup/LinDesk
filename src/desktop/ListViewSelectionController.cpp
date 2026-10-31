#include "desktop/ListViewSelectionController.h"
#include "desktop/ListView.h"
#include "desktop/ListViewSelectionHelper.h"
#include "box/BoxManager.h"
#include "box/LinBox.h"
#include "models/DesktopQuadTree.h"
#include <QSet>
#include <QTimer>
#include <QElapsedTimer>
#include <QtMath>
#include <QRegion>
#include <QDebug>

constexpr int kBackgroundSelectionThrottleFastMs = 8;
constexpr int kBackgroundSelectionThrottleBalancedMs = 16;
constexpr int kBackgroundSelectionThrottleBusyMs = 32;
constexpr int kBackgroundSelectionThrottleMaxMs = 48;
constexpr int kBackgroundSelectionThrottleRelaxFrameThreshold = 3;

void ListViewSelectionController::clearSelectionState(ListView& view)
{
    view.clearCheckedItem();
    view.clearFocusedItem();
    view.clearCutMarks();
    view.resetRenameClickState();
}

void ListViewSelectionController::setSelectedItemPaths(ListView& view, const QStringList& filePaths, const QString& focusedPath, bool expandSingle)
{
    const QString normalizedFocusedPath = view.normalizeDesktopItemPath(focusedPath);
    QStringList desktopPaths;
    QStringList boxPaths;

    for (const QString& path : filePaths) {
        const QString normalizedPath = view.normalizeDesktopItemPath(path);
        if (normalizedPath.isEmpty()) {
            continue;
        }

        if (const DesktopRootItemState* state = view.findDesktopStateByPath(normalizedPath)) {
            if (state->model.boxId.isEmpty()) {
                if (!desktopPaths.contains(normalizedPath)) {
                    desktopPaths.append(normalizedPath);
                }
                continue;
            }
        }

        if (view.findAnyItemStateByPath(normalizedPath) && !boxPaths.contains(normalizedPath)) {
            boxPaths.append(normalizedPath);
        }
    }

    if (!desktopPaths.isEmpty()) {
        BoxManager* bm = BoxManager::instance();
        if (bm) {
            QSet<LinBox*> dirtyBoxes;
            for (const QString& path : view.m_selectedBoxPaths) {
                view.clearItemExpandedState(path);
            }
            for (const auto& box : bm->allBoxes()) {
                if (!box) {
                    continue;
                }
                for (DesktopRootItemState& state : const_cast<QVector<DesktopRootItemState>&>(box->itemStates())) {
                    if (state.model.isSelected || state.model.focused) {
                        state.model.isSelected = false;
                        state.model.renderData.selected = false;
                        state.model.renderData.hovered = state.model.isHovered;
                        state.model.focused = false;
                        dirtyBoxes.insert(box);
                    }
                }
            }
            for (LinBox* dirtyBox : dirtyBoxes) {
                if (!dirtyBox) {
                    continue;
                }
                dirtyBox->refreshLayout();
                dirtyBox->update();
            }
        }
        view.m_selectedBoxPaths.clear();
        view.m_focusedBoxPath.clear();
        view.m_anchorBoxPath.clear();
        view.clearFocusedItem();
        view.applyDesktopSelection(desktopPaths,
                                   normalizedFocusedPath.isEmpty() ? desktopPaths.last() : normalizedFocusedPath,
                                   expandSingle && desktopPaths.size() == 1);
        if (view.m_anchorDesktopPath.isEmpty()) {
            view.m_anchorDesktopPath = desktopPaths.first();
        }
        return;
    }

    if (!boxPaths.isEmpty()) {
        view.clearDesktopSelection();
        view.clearFocusedItem();
        view.m_focusedDesktopPath.clear();
        view.m_anchorDesktopPath.clear();
        for (const QString& path : view.m_selectedBoxPaths) {
            view.clearItemExpandedState(path);
        }

        BoxManager* bm = BoxManager::instance();
        QSet<LinBox*> dirtyBoxes;
        const QString resolvedFocusedPath = normalizedFocusedPath.isEmpty() ? boxPaths.last() : normalizedFocusedPath;
        if (bm) {
            for (const auto& box : bm->allBoxes()) {
                if (!box) {
                    continue;
                }
                bool boxDirty = false;
                for (DesktopRootItemState& state : const_cast<QVector<DesktopRootItemState>&>(box->itemStates())) {
                    const QString statePath = view.normalizeDesktopItemPath(state.model.filePath);
                    const bool selected = boxPaths.contains(statePath);
                    const bool focused = statePath == resolvedFocusedPath;
                    if (state.model.isSelected != selected) {
                        state.model.isSelected = selected;
                        state.model.renderData.selected = selected;
                        state.model.renderData.hovered = state.model.isHovered && !selected;
                        boxDirty = true;
                    }
                    if (state.model.focused != focused) {
                        state.model.focused = focused;
                        boxDirty = true;
                    }
                }
                if (boxDirty) {
                    dirtyBoxes.insert(box);
                }
            }
        }

        for (const QString& path : boxPaths) {
            view.setItemFullNameExpanded(path, false);
        }
        if (expandSingle && boxPaths.size() == 1) {
            view.setItemFullNameExpanded(boxPaths.first(), true);
        }

        for (LinBox* dirtyBox : dirtyBoxes) {
            if (!dirtyBox) {
                continue;
            }
            dirtyBox->refreshLayout();
            dirtyBox->update();
        }

        view.m_selectedBoxPaths = boxPaths.toVector();
        view.m_focusedBoxPath = resolvedFocusedPath;
        if (view.m_anchorBoxPath.isEmpty()) {
            view.m_anchorBoxPath = boxPaths.first();
        }
        return;
    }

    clearSelectionState(view);
}

void ListViewSelectionController::setSelectedItems(ListView& view, const QStringList& filePaths, bool expandSingle)
{
    const QString focusedPath = filePaths.isEmpty() ? QString() : view.normalizeDesktopItemPath(filePaths.last());
    setSelectedItemPaths(view, filePaths, focusedPath, expandSingle);
}

void ListViewSelectionController::applySelectionState(ListView& view, const QStringList& filePaths, const QString& focusedPath, bool expandSingle)
{
    setSelectedItemPaths(view, filePaths, focusedPath, expandSingle);
}

void ListViewSelectionController::toggleItemSelection(ListView& view, const QString& filePath)
{
    const QString normalizedPath = view.normalizeDesktopItemPath(filePath);
    if (normalizedPath.isEmpty()) {
        return;
    }

    if (const DesktopRootItemState* state = view.findDesktopStateByPath(normalizedPath)) {
        if (state->model.boxId.isEmpty()) {
            QStringList updatedPaths = selectedDesktopPaths(view);
            if (updatedPaths.contains(normalizedPath)) {
                updatedPaths.removeAll(normalizedPath);
                view.clearItemExpandedState(normalizedPath);
            } else {
                updatedPaths.append(normalizedPath);
                view.setItemFullNameExpanded(normalizedPath, false);
            }
            view.applyDesktopSelection(updatedPaths, normalizedPath, false);
            if (view.m_anchorDesktopPath.isEmpty() && !updatedPaths.isEmpty()) {
                view.m_anchorDesktopPath = normalizedPath;
            }
            return;
        }
    }

    if (view.findAnyItemStateByPath(normalizedPath)) {
        QStringList updatedPaths = selectedBoxPaths(view);
        if (view.m_selectedBoxPaths.contains(normalizedPath)) {
            updatedPaths.removeAll(normalizedPath);
            view.clearItemExpandedState(normalizedPath);
        } else {
            updatedPaths.append(normalizedPath);
            view.setItemFullNameExpanded(normalizedPath, false);
        }
        setSelectedItemPaths(view, updatedPaths, normalizedPath, false);
        if (view.m_anchorBoxPath.isEmpty() && !updatedPaths.isEmpty()) {
            view.m_anchorBoxPath = normalizedPath;
        }
    }
}

void ListViewSelectionController::selectRangeToPath(ListView& view, const QString& filePath, LinBox* box)
{
    const QString normalizedPath = view.normalizeDesktopItemPath(filePath);
    if (normalizedPath.isEmpty()) {
        return;
    }

    if (!box) {
        const QStringList orderedPaths = selectableDesktopPaths(view);
        if (orderedPaths.isEmpty()) {
            return;
        }

        const QString anchorPath = view.m_anchorDesktopPath.isEmpty() ? normalizedPath : view.m_anchorDesktopPath;
        const int anchorIndex = orderedPaths.indexOf(view.normalizeDesktopItemPath(anchorPath));
        const int itemIndex = orderedPaths.indexOf(normalizedPath);
        if (anchorIndex < 0 || itemIndex < 0) {
            view.applyDesktopSelection({ normalizedPath }, normalizedPath, true);
            view.m_anchorDesktopPath = normalizedPath;
            return;
        }

        QStringList rangePaths;
        const int start = qMin(anchorIndex, itemIndex);
        const int end = qMax(anchorIndex, itemIndex);
        for (int i = start; i <= end; ++i) {
            rangePaths.append(orderedPaths.at(i));
        }
        view.applyDesktopSelection(rangePaths, normalizedPath, false);
        return;
    }

    const QStringList orderedPaths = selectableBoxPaths(view, box);
    if (orderedPaths.isEmpty()) {
        return;
    }

    const QString anchorPath = view.m_anchorBoxPath.isEmpty() ? normalizedPath : view.m_anchorBoxPath;
    const int anchorIndex = orderedPaths.indexOf(view.normalizeDesktopItemPath(anchorPath));
    const int itemIndex = orderedPaths.indexOf(normalizedPath);
    if (anchorIndex < 0 || itemIndex < 0) {
        setSelectedItemPaths(view, { normalizedPath }, normalizedPath, true);
        view.m_anchorBoxPath = normalizedPath;
        return;
    }

    QStringList rangePaths;
    const int start = qMin(anchorIndex, itemIndex);
    const int end = qMax(anchorIndex, itemIndex);
    for (int i = start; i <= end; ++i) {
        rangePaths.append(orderedPaths.at(i));
    }
    setSelectedItemPaths(view, rangePaths, normalizedPath, false);
}

bool ListViewSelectionController::isDesktopPathSelected(const ListView& view, const QString& filePath)
{
    const QString normalizedPath = view.normalizeDesktopItemPath(filePath);
    return !normalizedPath.isEmpty() && view.m_selectedDesktopPathSet.contains(normalizedPath);
}

QVector<DesktopRootItemModel> ListViewSelectionController::selectedDesktopModels(const ListView& view)
{
    QVector<DesktopRootItemModel> models;
    models.reserve(view.m_selectedDesktopPaths.size());
    for (const QString& path : view.m_selectedDesktopPaths) {
        const DesktopRootItemState* state = view.findDesktopStateByPath(path);
        if (state) {
            models.push_back(state->model);
        }
    }
    return models;
}

QStringList ListViewSelectionController::selectedDesktopPaths(const ListView& view)
{
    QStringList paths;
    for (const QString& path : view.m_selectedDesktopPaths) {
        if (!path.isEmpty()) {
            paths.append(path);
        }
    }
    return paths;
}

QStringList ListViewSelectionController::selectedBoxPaths(const ListView& view)
{
    QStringList paths;
    for (const QString& path : view.m_selectedBoxPaths) {
        if (!path.isEmpty()) {
            paths.append(path);
        }
    }
    return paths;
}

QStringList ListViewSelectionController::selectableDesktopPaths(const ListView& view)
{
    QStringList paths;
    paths.reserve(view.m_itemStates.size());
    for (const auto& state : view.m_itemStates) {
        if (!view.shouldPaintDesktopModelState(state)) {
            continue;
        }
        const QString normalizedPath = view.normalizeDesktopItemPath(state.model.filePath);
        if (!normalizedPath.isEmpty()) {
            paths.append(normalizedPath);
        }
    }
    return paths;
}

QStringList ListViewSelectionController::selectableBoxPaths(const ListView& view, LinBox* box)
{
    QStringList paths;
    if (!box) {
        return paths;
    }
    const auto& states = box->itemStates();
    paths.reserve(states.size());
    for (const auto& state : states) {
        if (!state.model.visible) {
            continue;
        }
        const QString normalizedPath = view.normalizeDesktopItemPath(state.model.filePath);
        if (!normalizedPath.isEmpty()) {
            paths.append(normalizedPath);
        }
    }
    return paths;
}

void ListViewSelectionController::setDesktopItemSelected(ListView& view, DesktopRootItemState* state, const QString& normalizedPath, bool selected, QRegion* dirtyRegion)
{
    if (!state || normalizedPath.isEmpty()) {
        return;
    }

    if (selected) {
        if (!view.m_selectedDesktopPathSet.contains(normalizedPath)) {
            view.m_selectedDesktopPaths.push_back(normalizedPath);
        }
        view.m_selectedDesktopPathSet.insert(normalizedPath);
    } else {
        view.m_selectedDesktopPathSet.remove(normalizedPath);
        view.m_selectedDesktopPaths.removeOne(normalizedPath);
    }

    if (state->model.isSelected == selected && state->model.renderData.selected == selected) {
        return;
    }

    state->model.isSelected = selected;
    state->model.renderData.selected = selected;
    const QRect dirty = desktopRootItemHighlightDirtyRect(*state);
    if (dirtyRegion) {
        *dirtyRegion += dirty;
        return;
    }
    view.update(dirty);
}

void ListViewSelectionController::setDesktopItemSelected(ListView& view, const QString& filePath, bool selected, QRegion* dirtyRegion)
{
    DesktopRootItemState* state = view.findDesktopStateByPath(filePath);
    if (!state) {
        return;
    }

    const QString normalizedPath = view.normalizeDesktopItemPath(state->model.filePath);
    setDesktopItemSelected(view, state, normalizedPath, selected, dirtyRegion);
}

void ListViewSelectionController::clearDesktopSelection(ListView& view, QRegion* dirtyRegion)
{
    const QStringList previousSelection = selectedDesktopPaths(view);
    for (const QString& path : previousSelection) {
        view.clearItemExpandedState(path);
        setDesktopItemSelected(view, path, false, dirtyRegion);
    }
    view.m_selectedDesktopPaths.clear();
    view.m_selectedDesktopPathSet.clear();
}

void ListViewSelectionController::applyDesktopSelection(ListView& view, const QStringList& filePaths, const QString& focusedPath, bool expandSingle)
{
    QRegion dirtyRegion;
    clearDesktopSelection(view, &dirtyRegion);

    QStringList filteredPaths;
    for (const QString& path : filePaths) {
        const QString normalizedPath = view.normalizeDesktopItemPath(path);
        if (normalizedPath.isEmpty() || filteredPaths.contains(normalizedPath)) {
            continue;
        }
        if (!view.findDesktopStateByPath(normalizedPath)) {
            continue;
        }
        filteredPaths.append(normalizedPath);
        setDesktopItemSelected(view, normalizedPath, true, &dirtyRegion);
    }

    if (!dirtyRegion.isEmpty()) {
        view.update(dirtyRegion);
    }

    view.m_focusedDesktopPath = filteredPaths.isEmpty()
        ? QString()
        : view.normalizeDesktopItemPath(focusedPath.isEmpty() ? filteredPaths.last() : focusedPath);
    if (view.m_focusedDesktopPath.isEmpty() && !filteredPaths.isEmpty()) {
        view.m_focusedDesktopPath = filteredPaths.last();
    }

    if (expandSingle && filteredPaths.size() == 1) {
        view.setItemFullNameExpanded(filteredPaths.first(), true);
    }
}

QRect ListViewSelectionController::desktopSelectionRectForItem(const ListView& view, const DesktopRootItemState& state)
{
    Q_UNUSED(view);
    return state.model.renderData.selectionContentRect;
}

void ListViewSelectionController::beginBackgroundSelection(ListView& view, const QPoint& startPos)
{
    view.clearWidgetFocus();
    view.clearCheckedItem();
    view.activateWindow();
    view.setFocus(Qt::MouseFocusReason);
    if (view.mouseGrabber() != &view) {
        view.grabMouse();
    }
    view.rebuildSpatialIndex();
    rebuildBackgroundSelectionOcclusionRegion(view);
    view.m_backgroundSelectionThrottleTimer->stop();
    view.m_backgroundSelectionFrameTimer.invalidate();
    view.m_backgroundSelectionThrottleMs = kBackgroundSelectionThrottleFastMs;
    view.m_backgroundSelectionRefreshCostMs = 0.0;
    view.m_backgroundSelectionThrottleRelaxFrames = 0;
    view.m_pendingBackgroundSelectionPos = startPos;
    view.m_hasPendingBackgroundSelectionUpdate = false;
    resetBackgroundSelectionCandidateCache(view);

    view.m_isSelecting = true;
    view.m_startPos = startPos;
    view.m_selectionStart = startPos;
    view.m_selectionRect = QRect(startPos, QSize(0, 0));
}

bool ListViewSelectionController::updateBackgroundSelection(ListView& view, const QPoint& currentPos, QRect* selectionRect)
{
    if (!view.m_isSelecting) {
        return false;
    }

    const QRect rect = QRect(view.m_selectionStart, currentPos).normalized();
    const bool shouldShowSelection = rect.width() > 3 || rect.height() > 3;
    const QRect previousRect = view.m_renderedSelectionRect;
    view.m_selectionRect = shouldShowSelection ? rect : QRect();
    view.m_pendingBackgroundSelectionPos = currentPos;
    view.m_hasPendingBackgroundSelectionUpdate = true;

    // Zero-ghosting dirty rect update: invalidate union of old and new selection rects
    QRect dirtyRect;
    if (!previousRect.isNull() && !view.m_selectionRect.isNull()) {
        dirtyRect = previousRect.united(view.m_selectionRect);
    } else if (!view.m_selectionRect.isNull()) {
        dirtyRect = view.m_selectionRect;
    } else {
        dirtyRect = previousRect;
    }
    if (!dirtyRect.isNull()) {
        view.update(dirtyRect.adjusted(-4, -4, 4, 4));
    }
    view.m_renderedSelectionRect = view.m_selectionRect;

    if (shouldShowSelection) {
        const int throttleMs = currentBackgroundSelectionThrottleMs(view);
        const bool shouldFlushNow =
            !view.m_backgroundSelectionFrameTimer.isValid() ||
            view.m_backgroundSelectionFrameTimer.elapsed() >= throttleMs;

        if (shouldFlushNow) {
            flushBackgroundSelectionUpdate(view);
        } else if (!view.m_backgroundSelectionThrottleTimer->isActive()) {
            const int remainingMs = qMax(1, throttleMs - static_cast<int>(view.m_backgroundSelectionFrameTimer.elapsed()));
            view.m_backgroundSelectionThrottleTimer->start(remainingMs);
        }
    } else {
        view.m_backgroundSelectionThrottleTimer->stop();
        view.m_hasPendingBackgroundSelectionUpdate = false;
        resetBackgroundSelectionCandidateCache(view);
        view.clearCheckedItem();
    }

    if (selectionRect) {
        *selectionRect = shouldShowSelection ? rect : QRect();
    }

    return shouldShowSelection;
}

void ListViewSelectionController::endBackgroundSelection(ListView& view)
{
    const QRect previousRect = view.m_renderedSelectionRect;
    if (view.m_hasPendingBackgroundSelectionUpdate) {
        flushBackgroundSelectionUpdate(view);
    }
    view.m_backgroundSelectionThrottleTimer->stop();
    view.m_backgroundSelectionFrameTimer.invalidate();
    view.m_hasPendingBackgroundSelectionUpdate = false;
    view.m_isSelecting = false;
    view.m_startPos = QPoint();
    view.m_selectionStart = QPoint();
    view.m_selectionRect = QRect();
    view.m_renderedSelectionRect = QRect();
    view.m_backgroundSelectionOcclusionRegion = QRegion();
    view.m_backgroundSelectionThrottleMs = kBackgroundSelectionThrottleFastMs;
    view.m_backgroundSelectionRefreshCostMs = 0.0;
    view.m_backgroundSelectionThrottleRelaxFrames = 0;
    resetBackgroundSelectionCandidateCache(view);
    if (view.mouseGrabber() == &view) {
        view.releaseMouse();
    }
    view.activateWindow();
    view.setFocus(Qt::MouseFocusReason);
    if (!previousRect.isNull()) {
        view.update(previousRect.adjusted(-4, -4, 4, 4));
    }
}

void ListViewSelectionController::flushBackgroundSelectionUpdate(ListView& view)
{
    if (!view.m_isSelecting || !view.m_hasPendingBackgroundSelectionUpdate) {
        return;
    }

    view.m_backgroundSelectionThrottleTimer->stop();
    const QRect rect = QRect(view.m_selectionStart, view.m_pendingBackgroundSelectionPos).normalized();
    QElapsedTimer refreshTimer;
    refreshTimer.start();
    if (rect.width() > 3 || rect.height() > 3) {
        view.refreshCheckedItem(rect);
    } else {
        view.clearCheckedItem();
    }

    updateBackgroundSelectionThrottle(view, qMax<qint64>(1, refreshTimer.elapsed()), rect);
    view.m_backgroundSelectionFrameTimer.restart();
    view.m_hasPendingBackgroundSelectionUpdate = false;
}

void ListViewSelectionController::resetBackgroundSelectionCandidateCache(ListView& view)
{
    view.m_backgroundSelectionPreviousUncoveredRegion = QRegion();
    view.m_backgroundSelectionCandidateStates.clear();
    view.m_backgroundSelectionCandidateScratchStates.clear();
    view.m_backgroundSelectionCandidateSet.clear();
}

void ListViewSelectionController::updateBackgroundSelectionCandidateCache(ListView& view, const QRegion& uncoveredSelectionRegion)
{
    if (!view.m_spatialIndex) {
        view.m_backgroundSelectionCandidateStates.clear();
        view.m_backgroundSelectionCandidateStates.reserve(view.m_itemStates.size());
        for (auto& state : view.m_itemStates) {
            if (uncoveredSelectionRegion.intersects(state.model.renderData.selectionContentRect)) {
                view.m_backgroundSelectionCandidateStates.append(&state);
            }
        }
        view.m_backgroundSelectionPreviousUncoveredRegion = uncoveredSelectionRegion;
        return;
    }

    const QRegion addedRegion = view.m_backgroundSelectionPreviousUncoveredRegion.isEmpty()
        ? uncoveredSelectionRegion
        : uncoveredSelectionRegion.subtracted(view.m_backgroundSelectionPreviousUncoveredRegion);

    if (!addedRegion.isEmpty()) {
        const QVector<QRect> addedRects = addedRegion.rects();
        for (const QRect& addedRect : addedRects) {
            const QVector<DesktopRootItemState*> queryStates = view.m_spatialIndex->query(addedRect);
            for (DesktopRootItemState* state : queryStates) {
                if (!state || !state->model.boxId.isEmpty() || view.m_backgroundSelectionCandidateSet.contains(state)) {
                    continue;
                }
                if (!uncoveredSelectionRegion.intersects(state->model.renderData.selectionContentRect)) {
                    continue;
                }
                view.m_backgroundSelectionCandidateSet.insert(state);
                view.m_backgroundSelectionCandidateStates.append(state);
            }
        }
    }

    if (!view.m_backgroundSelectionPreviousUncoveredRegion.isEmpty()) {
        const QRegion removedRegion = view.m_backgroundSelectionPreviousUncoveredRegion.subtracted(uncoveredSelectionRegion);
        if (!removedRegion.isEmpty()) {
            view.m_backgroundSelectionCandidateScratchStates.clear();
            view.m_backgroundSelectionCandidateScratchStates.reserve(view.m_backgroundSelectionCandidateStates.size());
            for (DesktopRootItemState* state : view.m_backgroundSelectionCandidateStates) {
                if (state && uncoveredSelectionRegion.intersects(state->model.renderData.selectionContentRect)) {
                    view.m_backgroundSelectionCandidateScratchStates.append(state);
                    continue;
                }
                if (state) {
                    view.m_backgroundSelectionCandidateSet.remove(state);
                }
            }
            view.m_backgroundSelectionCandidateStates.swap(view.m_backgroundSelectionCandidateScratchStates);
        }
    }

    view.m_backgroundSelectionPreviousUncoveredRegion = uncoveredSelectionRegion;
}

void ListViewSelectionController::rebuildBackgroundSelectionOcclusionRegion(ListView& view)
{
    view.m_backgroundSelectionOcclusionRegion = QRegion();

    BoxManager* manager = BoxManager::instance();
    if (!manager) {
        return;
    }

    const QWidget* sharedParent = view.parentWidget();
    for (const QPointer<LinBox>& boxPtr : manager->allBoxes()) {
        LinBox* box = boxPtr.data();
        if (!box || !box->isVisible()) {
            continue;
        }

        QRect boxRectInView;
        if (box->parentWidget() == sharedParent) {
            boxRectInView = QRect(view.mapFromParent(box->geometry().topLeft()), box->geometry().size());
        } else {
            boxRectInView = QRect(view.mapFromGlobal(box->mapToGlobal(QPoint(0, 0))), box->size());
        }

        if (!boxRectInView.isEmpty()) {
            view.m_backgroundSelectionOcclusionRegion += boxRectInView;
        }
    }
}

int ListViewSelectionController::currentBackgroundSelectionThrottleMs(const ListView& view)
{
    return view.m_backgroundSelectionThrottleMs;
}

void ListViewSelectionController::updateBackgroundSelectionThrottle(ListView& view, qint64 refreshElapsedMs, const QRect& selectionRect)
{
    const qreal viewportArea = qMax(1, view.width() * view.height());
    const qreal selectionArea = qMax(1, selectionRect.width() * selectionRect.height());
    const qreal coverage = qBound<qreal>(0.0, selectionArea / viewportArea, 1.0);
    const qreal weightedCostMs = qMax<qreal>(1.0, static_cast<qreal>(refreshElapsedMs)) * (1.0 + coverage * 0.35);

    if (view.m_backgroundSelectionRefreshCostMs <= 0.0) {
        view.m_backgroundSelectionRefreshCostMs = weightedCostMs;
    } else {
        view.m_backgroundSelectionRefreshCostMs = (view.m_backgroundSelectionRefreshCostMs * 0.7) + (weightedCostMs * 0.3);
    }

    int targetThrottleMs = kBackgroundSelectionThrottleFastMs;
    if (view.m_backgroundSelectionRefreshCostMs >= 9.0 || refreshElapsedMs >= 12) {
        targetThrottleMs = kBackgroundSelectionThrottleMaxMs;
    } else if (view.m_backgroundSelectionRefreshCostMs >= 5.5 || refreshElapsedMs >= 8) {
        targetThrottleMs = kBackgroundSelectionThrottleBusyMs;
    } else if (view.m_backgroundSelectionRefreshCostMs >= 2.5 || refreshElapsedMs >= 4) {
        targetThrottleMs = kBackgroundSelectionThrottleBalancedMs;
    }

    if (targetThrottleMs > view.m_backgroundSelectionThrottleMs) {
        view.m_backgroundSelectionThrottleMs = targetThrottleMs;
        view.m_backgroundSelectionThrottleRelaxFrames = 0;
    } else if (targetThrottleMs < view.m_backgroundSelectionThrottleMs) {
        ++view.m_backgroundSelectionThrottleRelaxFrames;
        if (view.m_backgroundSelectionThrottleRelaxFrames >= kBackgroundSelectionThrottleRelaxFrameThreshold) {
            view.m_backgroundSelectionThrottleMs = targetThrottleMs;
            view.m_backgroundSelectionThrottleRelaxFrames = 0;
        }
    } else {
        view.m_backgroundSelectionThrottleRelaxFrames = 0;
    }
}

void ListViewSelectionController::setFocusedItemPath(ListView& view, const QString& filePath)
{
    const QString normalizedPath = view.normalizeDesktopItemPath(filePath);
    BoxManager* bm = BoxManager::instance();
    QSet<LinBox*> dirtyBoxes;
    auto updateBoxFocusState = [&](const QString& targetPath) {
        if (!bm) {
            return;
        }
        for (const auto& box : bm->allBoxes()) {
            if (!box) {
                continue;
            }
            bool boxDirty = false;
            for (DesktopRootItemState& state : const_cast<QVector<DesktopRootItemState>&>(box->itemStates())) {
                const bool focused = !targetPath.isEmpty()
                    && view.normalizeDesktopItemPath(state.model.filePath) == targetPath;
                if (state.model.focused != focused) {
                    state.model.focused = focused;
                    boxDirty = true;
                }
            }
            if (boxDirty) {
                dirtyBoxes.insert(box);
            }
        }
    };

    if (normalizedPath.isEmpty()) {
        clearFocusedItem(view);
        view.m_focusedDesktopPath.clear();
        view.m_focusedBoxPath.clear();
        updateBoxFocusState(QString());
        for (LinBox* dirtyBox : dirtyBoxes) {
            if (!dirtyBox) {
                continue;
            }
            dirtyBox->refreshLayout();
            dirtyBox->update();
        }
        return;
    }

    if (const DesktopRootItemState* state = view.findDesktopStateByPath(normalizedPath)) {
        if (state->model.boxId.isEmpty()) {
            clearFocusedItem(view);
            view.m_focusedDesktopPath = normalizedPath;
            view.m_focusedBoxPath.clear();
            updateBoxFocusState(QString());
            for (LinBox* dirtyBox : dirtyBoxes) {
                if (!dirtyBox) {
                    continue;
                }
                dirtyBox->refreshLayout();
                dirtyBox->update();
            }
            return;
        }
    }

    if (view.findAnyItemStateByPath(normalizedPath)) {
        clearFocusedItem(view);
        view.m_focusedDesktopPath.clear();
        view.m_focusedBoxPath = normalizedPath;
        updateBoxFocusState(normalizedPath);
        for (LinBox* dirtyBox : dirtyBoxes) {
            if (!dirtyBox) {
                continue;
            }
            dirtyBox->refreshLayout();
            dirtyBox->update();
        }
        return;
    }

    clearFocusedItem(view);
    view.m_focusedBoxPath.clear();
    updateBoxFocusState(QString());
    for (LinBox* dirtyBox : dirtyBoxes) {
        if (!dirtyBox) {
            continue;
        }
        dirtyBox->refreshLayout();
        dirtyBox->update();
    }
}

void ListViewSelectionController::clearFocusedItem(ListView& view)
{
    if (!view.m_focusedDesktopPath.isEmpty()) {
        view.clearItemExpandedState(view.m_focusedDesktopPath);
    }
    if (!view.m_focusedBoxPath.isEmpty()) {
        view.clearItemExpandedState(view.m_focusedBoxPath);
    }
    view.m_focusedDesktopPath.clear();
    view.m_focusedBoxPath.clear();
}

void ListViewSelectionController::moveFocusInHost(ListView& view, LinBox* box, ListView::FocusMoveDirection direction, bool extendSelection, bool focusOnly)
{
    if (!box) {
        const QStringList orderedPaths = selectableDesktopPaths(view);
        if (orderedPaths.isEmpty()) {
            return;
        }

        QString currentPath = view.effectiveFocusedDesktopPath();
        if (currentPath.isEmpty() && !view.m_selectedDesktopPaths.isEmpty()) {
            currentPath = view.normalizeDesktopItemPath(view.m_selectedDesktopPaths.first());
        }
        QString targetPath = findDirectionalDesktopFocusCandidate(view, currentPath, direction);

        if (targetPath.isEmpty()) {
            return;
        }

        if (focusOnly) {
            view.m_focusedDesktopPath = targetPath;
            return;
        }

        if (extendSelection) {
            QString anchorPath = view.m_anchorDesktopPath.isEmpty() ? targetPath : view.m_anchorDesktopPath;
            const int anchorIndex = orderedPaths.indexOf(view.normalizeDesktopItemPath(anchorPath));
            const int itemIndex = orderedPaths.indexOf(targetPath);
            if (anchorIndex >= 0 && itemIndex >= 0) {
                QStringList rangePaths;
                const int start = qMin(anchorIndex, itemIndex);
                const int end = qMax(anchorIndex, itemIndex);
                for (int i = start; i <= end; ++i) {
                    rangePaths.append(orderedPaths.at(i));
                }
                view.applyDesktopSelection(rangePaths, targetPath, false);
            } else {
                view.applyDesktopSelection({ targetPath }, targetPath, true);
            }
            if (view.m_anchorDesktopPath.isEmpty()) {
                view.m_anchorDesktopPath = anchorPath;
            }
            return;
        }

        view.applyDesktopSelection({ targetPath }, targetPath, true);
        view.m_anchorDesktopPath = targetPath;
        return;
    }

    const QStringList orderedPaths = selectableBoxPaths(view, box);
    if (orderedPaths.isEmpty()) {
        return;
    }

    QString currentPath = view.effectiveFocusedBoxPath();
    if (currentPath.isEmpty() && !view.m_selectedBoxPaths.isEmpty()) {
        currentPath = view.normalizeDesktopItemPath(view.m_selectedBoxPaths.first());
    }
    QString targetPath = findDirectionalBoxFocusCandidate(view, box, currentPath, direction);

    if (targetPath.isEmpty()) {
        return;
    }

    if (focusOnly) {
        view.setFocusedItemPath(targetPath);
        return;
    }

    if (extendSelection) {
        const QString anchorPath = view.m_anchorBoxPath.isEmpty() ? targetPath : view.m_anchorBoxPath;
        const int anchorIndex = orderedPaths.indexOf(view.normalizeDesktopItemPath(anchorPath));
        const int itemIndex = orderedPaths.indexOf(targetPath);
        if (anchorIndex >= 0 && itemIndex >= 0) {
            QStringList rangePaths;
            const int start = qMin(anchorIndex, itemIndex);
            const int end = qMax(anchorIndex, itemIndex);
            for (int i = start; i <= end; ++i) {
                rangePaths.append(orderedPaths.at(i));
            }
            setSelectedItemPaths(view, rangePaths, targetPath, false);
        } else {
            setSelectedItemPaths(view, { targetPath }, targetPath, true);
        }
        if (view.m_anchorBoxPath.isEmpty()) {
            view.m_anchorBoxPath = anchorPath;
        }
        return;
    }

    setSelectedItemPaths(view, { targetPath }, targetPath, true);
    view.m_anchorBoxPath = targetPath;
}

QString ListViewSelectionController::focusedItemPath(const ListView& view)
{
    if (!view.m_focusedDesktopPath.isEmpty()) {
        return view.effectiveFocusedDesktopPath();
    }
    if (!view.m_focusedBoxPath.isEmpty()) {
        return view.effectiveFocusedBoxPath();
    }
    return QString();
}

QString ListViewSelectionController::anchorItemPath(const ListView& view)
{
    if (!view.m_anchorDesktopPath.isEmpty()) {
        return view.effectiveAnchorDesktopPath();
    }
    if (!view.m_anchorBoxPath.isEmpty()) {
        return view.effectiveAnchorBoxPath();
    }
    return QString();
}

QString ListViewSelectionController::findDirectionalDesktopFocusCandidate(const ListView& view, const QString& focusedPath, ListView::FocusMoveDirection direction)
{
    QVector<DesktopRootItemState> candidates;
    for (const auto& state : view.m_itemStates) {
        if (view.shouldPaintDesktopModelState(state)) {
            candidates.push_back(state);
        }
    }
    return ListViewSelectionHelper::findDirectionalFocusCandidateInList(&view, candidates, focusedPath, direction);
}

QString ListViewSelectionController::findDirectionalBoxFocusCandidate(const ListView& view, LinBox* box, const QString& focusedPath, ListView::FocusMoveDirection direction)
{
    if (!box) {
        return QString();
    }
    QVector<DesktopRootItemState> candidates;
    for (const auto& state : box->itemStates()) {
        if (state.model.visible) {
            candidates.push_back(state);
        }
    }
    return ListViewSelectionHelper::findDirectionalFocusCandidateInList(&view, candidates, focusedPath, direction);
}

QStringList ListViewSelectionController::selectedItemPaths(const ListView& view)
{
    QStringList paths;
    paths.append(selectedDesktopPaths(view));
    for (const QString& path : view.m_selectedBoxPaths) {
        if (!path.isEmpty() && !paths.contains(path)) {
            paths.append(path);
        }
    }
    return paths;
}

void ListViewSelectionController::restoreSelectionStateByPaths(ListView& view, const QStringList& selectedPaths, const QString& focusedPath, const QString& anchorPath, bool expandSingle)
{
    setSelectedItemPaths(view, selectedPaths, focusedPath, expandSingle);
    view.m_anchorDesktopPath = anchorPath.isEmpty() ? QString() : view.normalizeDesktopItemPath(anchorPath);
    view.m_anchorBoxPath = anchorPath.isEmpty() ? QString() : view.normalizeDesktopItemPath(anchorPath);
}
