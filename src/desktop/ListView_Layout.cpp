#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDataStream>
#include <QFile>
#include <QSet>
#include <QCoreApplication>
#include <QSettings>
#include "desktop/ListView.h"
#include "desktop/ListViewSelectionHelper.h"
#include "desktop/ListViewSelectionController.h"
#include "desktop/ListViewDragDropHelper.h"
#include "desktop/LinDesktop.h"
#include "desktop/widgets/linwidget/WidgetPackage.h"
#include "desktop/widgets/linwidget/WidgetSettingsDialog.h"
#include "app/AppFonts.h"
#include "desktop/ListViewMappedBoxHelper.h"
#include "models/DesktopQuadTree.h"
#include "ui/SettingUI.h"
#include <QPushButton>
#include <QtMath>
#include <QProcess>
#include <QStandardPaths>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QScreen>
#include <QClipboard>
#include <QCursor>
#include "SystemApi/SystemApi.h"
#include <QDrag>
#include <QMimeData>
#include <QUrl>
#include <QKeyEvent>
#include <QRegularExpression>
#include <QFileInfo>
#include <QDir>
#include <QFileDialog>
#include <QEventLoop>
#include <QFont>
#include <QFontMetrics>
#include <QInputDialog>
#include <QMenu>
#include <QMoveEvent>
#include <QRegion>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWheelEvent>
#include <climits>
#include <shobjidl.h>
#include "services/SystemApi/resources.h"
#include "ui/Fluent/FluentDialog.h"
#include "ui/Fluent/FluentInfoTip.h"
#include "desktop/ListViewFileOpsHelper.h"
#include "desktop/ListViewSelectionSnapshot.h"
#include "box/LinBox.h"
#include "plugins/PluginManager.h"
#include "services/HotkeyManager.h"
#include "services/AppPaths.h"
#include "services/IconCacheManager.h"
#include "services/SpaceLookIpcClient.h"
#include "services/DpiScaleHelper.h"
#include "box/BoxManager.h"
#include "desktop/ListViewDebugPanel.h"
#include "desktop/DesktopSnakeGame.h"
#include "desktop/widgets/linwidget/WidgetManager.h"
#include "desktop/widgets/linwidget/WidgetRegistryManager.h"

#include "desktop/ListView_Helpers.h"

QSet<quint64> ListView::collectDesktopIconOccupiedCells(const QStringList& ignoredPaths) const
{
    QSet<QString> ignoredSet;
    for (const QString& path : ignoredPaths) {
        ignoredSet.insert(normalizeDesktopItemPath(path));
    }

    QSet<quint64> occupiedCells;
    const int itemHeight = desktopItemFootprintHeight();
    const int strideX = qMax(1, m_iconWidth + m_colSpacing);
    const int strideY = qMax(1, itemHeight + m_rowSpacing);
    const int layoutLeft = desktopLayoutLeft();
    const int layoutTop = desktopLayoutTop();
    const int maxRows = maxGridRowsForBounds(layoutTop, desktopUsableBottom(), itemHeight, strideY);
    const int maxCols = maxGridColsForBounds(layoutLeft, desktopUsableRight(), m_iconWidth, strideX);

    for (const auto& state : m_itemStates) {
        if (!shouldPaintDesktopModelState(state)) {
            continue;
        }
        const QString normalizedPath = normalizeDesktopItemPath(state.model.filePath);
        if (ignoredSet.contains(normalizedPath)) {
            continue;
        }
        const QPoint gridCell = pointToGridCell(state.model.position, layoutLeft, layoutTop, strideX, strideY);
        occupiedCells.insert(packGridCell(qBound(0, gridCell.x(), maxCols - 1), qBound(0, gridCell.y(), maxRows - 1)));
    }

    for (auto it = m_pendingCreatedItemPositions.constBegin(); it != m_pendingCreatedItemPositions.constEnd(); ++it) {
        const QPoint gridCell = pointToGridCell(it.value(), layoutLeft, layoutTop, strideX, strideY);
        occupiedCells.insert(packGridCell(qBound(0, gridCell.x(), maxCols - 1), qBound(0, gridCell.y(), maxRows - 1)));
    }

    return occupiedCells;
}

QVector<DesktopRootItemState*> ListView::collectAutoArrangeStates() const
{
    QVector<DesktopRootItemState*> states;
    states.reserve(m_itemStates.size());
    for (const auto& state : m_itemStates) {
        if (!state.model.boxId.isEmpty() || !state.model.visible) {
            continue;
        }
        states.push_back(const_cast<DesktopRootItemState*>(&state));
    }
    return states;
}

QVector<DesktopRootItemState*> ListView::collectGridOrderedDesktopStates() const
{
    QVector<DesktopRootItemState*> states;
    states.reserve(m_itemStates.size());
    for (const auto& state : m_itemStates) {
        if (!shouldPaintDesktopModelState(state)) {
            continue;
        }
        states.push_back(const_cast<DesktopRootItemState*>(&state));
    }

    const int itemHeight = desktopItemFootprintHeight();
    const int strideX = qMax(1, m_iconWidth + m_colSpacing);
    const int strideY = qMax(1, itemHeight + m_rowSpacing);
    const int layoutLeft = desktopLayoutLeft();
    const int layoutTop = desktopLayoutTop();
    const int maxRows = qMax(1, maxGridRowsForBounds(layoutTop, desktopUsableBottom(), itemHeight, strideY));

    std::stable_sort(states.begin(), states.end(), [&](DesktopRootItemState* a, DesktopRootItemState* b) {
        if (!a || !b) {
            return a != nullptr;
        }

        const int indexA = pointToGridLinearIndex(a->model.position, layoutLeft, layoutTop, strideX, strideY, maxRows);
        const int indexB = pointToGridLinearIndex(b->model.position, layoutLeft, layoutTop, strideX, strideY, maxRows);
        if (indexA != indexB) {
            return indexA < indexB;
        }
        if (a->model.position.x() != b->model.position.x()) {
            return a->model.position.x() < b->model.position.x();
        }
        if (a->model.position.y() != b->model.position.y()) {
            return a->model.position.y() < b->model.position.y();
        }
        return normalizeDesktopItemPath(a->model.filePath) < normalizeDesktopItemPath(b->model.filePath);
    });

    return states;
}

bool ListView::applyGridAlignedDesktopDropLayout(const QStringList& draggedPaths, const QPoint& releasePos)
{
    if (draggedPaths.isEmpty()) {
        return false;
    }

    const int itemHeight = desktopItemFootprintHeight();
    const int strideX = qMax(1, m_iconWidth + m_colSpacing);
    const int strideY = qMax(1, itemHeight + m_rowSpacing);
    const int layoutLeft = desktopLayoutLeft();
    const int layoutTop = desktopLayoutTop();
    const int maxRows = qMax(1, maxGridRowsForBounds(layoutTop, desktopUsableBottom(), itemHeight, strideY));
    const int maxCols = qMax(1, maxGridColsForBounds(layoutLeft, desktopUsableRight(), m_iconWidth, strideX));

    QVector<DesktopRootItemState*> orderedStates = collectGridOrderedDesktopStates();
    if (orderedStates.isEmpty()) {
        return false;
    }

    QSet<QString> draggedPathSet;
    for (const QString& path : draggedPaths) {
        const QString normalizedPath = normalizeDesktopItemPath(path);
        if (!normalizedPath.isEmpty()) {
            draggedPathSet.insert(normalizedPath);
        }
    }
    if (draggedPathSet.isEmpty()) {
        return false;
    }

    QVector<DesktopRootItemState*> orderedDraggedStates;
    QVector<DesktopRootItemState*> remainingStates;
    orderedDraggedStates.reserve(draggedPathSet.size());
    remainingStates.reserve(orderedStates.size());

    for (DesktopRootItemState* state : orderedStates) {
        if (!state) {
            continue;
        }
        const QString normalizedPath = normalizeDesktopItemPath(state->model.filePath);
        if (draggedPathSet.contains(normalizedPath)) {
            orderedDraggedStates.append(state);
        } else {
            remainingStates.append(state);
        }
    }

    if (orderedDraggedStates.isEmpty()) {
        return false;
    }

    QHash<DesktopRootItemState*, QPoint> newPositions;
    newPositions.reserve(orderedStates.size());

    auto clampCellToBounds = [&](const QPoint& cell) {
        return QPoint(qBound(0, cell.x(), maxCols - 1), qBound(0, cell.y(), maxRows - 1));
    };

    auto normalizedCellForPos = [&](const QPoint& pos) {
        return clampCellToBounds(pointToGridCell(pos, layoutLeft, layoutTop, strideX, strideY));
    };

    QHash<DesktopRootItemState*, QPoint> originalDraggedCells;
    originalDraggedCells.reserve(orderedDraggedStates.size());
    int originalMinCellX = INT_MAX;
    int originalMinCellY = INT_MAX;
    int originalMaxCellX = INT_MIN;
    int originalMaxCellY = INT_MIN;

    for (DesktopRootItemState* state : orderedDraggedStates) {
        if (!state) {
            continue;
        }
        const QPoint cell = normalizedCellForPos(state->model.position);
        originalDraggedCells.insert(state, cell);
        originalMinCellX = qMin(originalMinCellX, cell.x());
        originalMinCellY = qMin(originalMinCellY, cell.y());
        originalMaxCellX = qMax(originalMaxCellX, cell.x());
        originalMaxCellY = qMax(originalMaxCellY, cell.y());
    }

    if (originalDraggedCells.isEmpty()) {
        return false;
    }

    int previewMinCellX = INT_MAX;
    int previewMinCellY = INT_MAX;
    bool hasPreviewCells = false;
    for (const auto& ghost : m_ghosts) {
        if (!ghost.widget || ghost.filePath.isEmpty()) {
            continue;
        }
        const QString normalizedPath = normalizeDesktopItemPath(ghost.filePath);
        if (!draggedPathSet.contains(normalizedPath)) {
            continue;
        }
        const QPoint cell = normalizedCellForPos(localPosFromGhostWidget(ghost.widget));
        previewMinCellX = qMin(previewMinCellX, cell.x());
        previewMinCellY = qMin(previewMinCellY, cell.y());
        hasPreviewCells = true;
    }

    QPoint targetTopLeftCell = hasPreviewCells
        ? QPoint(previewMinCellX, previewMinCellY)
        : normalizedCellForPos(releasePos);

    const int selectionWidth = originalMaxCellX - originalMinCellX + 1;
    const int selectionHeight = originalMaxCellY - originalMinCellY + 1;
    if (selectionWidth > maxCols || selectionHeight > maxRows) {
        return false;
    }

    targetTopLeftCell.setX(qBound(0, targetTopLeftCell.x(), maxCols - selectionWidth));
    targetTopLeftCell.setY(qBound(0, targetTopLeftCell.y(), maxRows - selectionHeight));

    QSet<quint64> occupiedSlots = desktopBlockedOccupiedCells();

    for (DesktopRootItemState* state : orderedDraggedStates) {
        if (!state || !originalDraggedCells.contains(state)) {
            continue;
        }

        const QPoint originalCell = originalDraggedCells.value(state);
        const QPoint relativeCell(originalCell.x() - originalMinCellX, originalCell.y() - originalMinCellY);
        const QPoint targetCell = targetTopLeftCell + relativeCell;
        const quint64 slotKey = packGridCell(targetCell.x(), targetCell.y());
        if (occupiedSlots.contains(slotKey)) {
            return false;
        }

        occupiedSlots.insert(slotKey);
        newPositions.insert(state, gridCellToPoint(targetCell.x(), targetCell.y(), layoutLeft, layoutTop, strideX, strideY));
    }

    QVector<DesktopRootItemState*> displacedStates;
    displacedStates.reserve(remainingStates.size());

    for (DesktopRootItemState* state : remainingStates) {
        if (!state) {
            continue;
        }

        const QPoint originalCell = normalizedCellForPos(state->model.position);
        const quint64 slotKey = packGridCell(originalCell.x(), originalCell.y());
        if (!occupiedSlots.contains(slotKey)) {
            occupiedSlots.insert(slotKey);
            newPositions.insert(state, gridCellToPoint(originalCell.x(), originalCell.y(), layoutLeft, layoutTop, strideX, strideY));
        } else {
            displacedStates.append(state);
        }
    }

    auto nearestAvailableCell = [&](const QPoint& preferredCell) {
        QVector<QPair<int, QPoint>> candidates;
        const int maxRadius = qMax(maxCols, maxRows);
        for (int radius = 0; radius <= maxRadius; ++radius) {
            candidates.clear();
            const int minX = qMax(0, preferredCell.x() - radius);
            const int maxX = qMin(maxCols - 1, preferredCell.x() + radius);
            const int minY = qMax(0, preferredCell.y() - radius);
            const int maxY = qMin(maxRows - 1, preferredCell.y() + radius);

            for (int x = minX; x <= maxX; ++x) {
                for (int y = minY; y <= maxY; ++y) {
                    if (qMax(qAbs(x - preferredCell.x()), qAbs(y - preferredCell.y())) != radius) {
                        continue;
                    }

                    const quint64 slotKey = packGridCell(x, y);
                    if (occupiedSlots.contains(slotKey)) {
                        continue;
                    }

                    const int dx = x - preferredCell.x();
                    const int dy = y - preferredCell.y();
                    const int distanceScore = dx * dx + dy * dy;
                    candidates.append(qMakePair(distanceScore, QPoint(x, y)));
                }
            }

            if (!candidates.isEmpty()) {
                std::sort(candidates.begin(), candidates.end(), [&](const QPair<int, QPoint>& a, const QPair<int, QPoint>& b) {
                    if (a.first != b.first) {
                        return a.first < b.first;
                    }

                    const int indexA = a.second.x() * maxRows + a.second.y();
                    const int indexB = b.second.x() * maxRows + b.second.y();
                    return indexA < indexB;
                });
                return candidates.first().second;
            }
        }

        return QPoint(-1, -1);
    };

    for (DesktopRootItemState* state : displacedStates) {
        if (!state) {
            continue;
        }

        const QPoint preferredCell = normalizedCellForPos(state->model.position);
        const QPoint assignedCell = nearestAvailableCell(preferredCell);
        if (assignedCell.x() < 0 || assignedCell.y() < 0) {
            return false;
        }

        occupiedSlots.insert(packGridCell(assignedCell.x(), assignedCell.y()));
        newPositions.insert(state, gridCellToPoint(assignedCell.x(), assignedCell.y(), layoutLeft, layoutTop, strideX, strideY));
    }

    bool changed = false;
    for (DesktopRootItemState* state : orderedStates) {
        if (!state || !newPositions.contains(state)) {
            continue;
        }

        const QPoint newPos = newPositions.value(state);
        if (state->model.position == newPos) {
            continue;
        }

        state->model.position = newPos;
        markDesktopRenderDataDirty(state);
        changed = true;
    }

    if (!changed) {
        return false;
    }

    updateAllItemRenderData();
    rebuildSpatialIndex();
    update();
    requestSaveBoxes();
    return true;
}

void ListView::sortAutoArrangeStates(QVector<DesktopRootItemState*>& states, SortOrderType order) const
{
    const SortOrderType effectiveOrder = (order == SortOrderType::Default)
        ? SortOrderType::SortByCategorized
        : order;

    auto specialPriority = [](const DesktopRootItemState* state) {
        if (!state || state->model.objectType != ObjectType::SpecialFolder) {
            return 100;
        }
        const QString clsid = state->model.clsid;
        if (clsid == APP_USER_FOLDER_CLS) return 1;
        if (clsid == APP_MY_COMPUTER_CLS) return 2;
        if (clsid == APP_NETWORK_CLS) return 3;
        if (clsid == APP_RECYCLE_BIN_CLS) return 4;
        if (clsid == APP_CONTROL_PANEL_CLS) return 5;
        return 6;
    };

    auto categorizedPriorityForState = [](const DesktopRootItemState* state) {
        if (!state) return 999;
        if (state->model.objectType == ObjectType::SpecialFolder) return 0;
        if (state->model.objectType == ObjectType::DiskLetter || state->model.fileInfo.isDir()) return 1;
        const QString ext = state->model.fileInfo.suffix().toLower();
        if (ext == "exe" || ext == "lnk" || ext == "url") return 2;
        static const QStringList docExts = {"txt", "pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "md", "zip", "rar", "7z"};
        if (docExts.contains(ext)) return 3;
        return 4;
    };

    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setIgnorePunctuation(true);
    collator.setLocale(QLocale::system());

    switch (effectiveOrder) {
    case SortOrderType::SortByCustom:
        std::stable_sort(states.begin(), states.end(), [](DesktopRootItemState* a, DesktopRootItemState* b) {
            const int orderA = a ? a->model.customSortOrder : -1;
            const int orderB = b ? b->model.customSortOrder : -1;
            const int safeOrderA = orderA < 0 ? INT_MAX : orderA;
            const int safeOrderB = orderB < 0 ? INT_MAX : orderB;
            if (safeOrderA != safeOrderB) {
                return safeOrderA < safeOrderB;
            }
            return a->model.displayName.toLower() < b->model.displayName.toLower();
        });
        break;
    case SortOrderType::SortByName_ASC:
    case SortOrderType::SortByName_DESC:
    {
        const bool ascending = (effectiveOrder == SortOrderType::SortByName_ASC);
        std::sort(states.begin(), states.end(), [&](DesktopRootItemState* a, DesktopRootItemState* b) {
            const int prioA = specialPriority(a);
            const int prioB = specialPriority(b);
            if (prioA != prioB) {
                return ascending ? (prioA < prioB) : (prioA > prioB);
            }
            const int result = collator.compare(a->model.sortName, b->model.sortName);
            return ascending ? (result < 0) : (result > 0);
        });
        break;
    }
    case SortOrderType::SortBySize_ASC:
    case SortOrderType::SortBySize_DESC:
    {
        const bool ascending = (effectiveOrder == SortOrderType::SortBySize_ASC);
        std::sort(states.begin(), states.end(), [ascending](DesktopRootItemState* a, DesktopRootItemState* b) {
            const bool aIsSpecial = (a->model.objectType == ObjectType::SpecialFolder);
            const bool bIsSpecial = (b->model.objectType == ObjectType::SpecialFolder);
            if (aIsSpecial != bIsSpecial) {
                return aIsSpecial;
            }
            const bool aIsDir = a->model.fileInfo.isDir();
            const bool bIsDir = b->model.fileInfo.isDir();
            if (aIsDir != bIsDir) {
                return ascending ? aIsDir : !aIsDir;
            }
            const qint64 sizeA = a->model.fileInfo.size();
            const qint64 sizeB = b->model.fileInfo.size();
            if (sizeA != sizeB) {
                return ascending ? (sizeA < sizeB) : (sizeA > sizeB);
            }
            return a->model.displayName.toLower() < b->model.displayName.toLower();
        });
        break;
    }
    case SortOrderType::SortByType_ASC:
    case SortOrderType::SortByType_DESC:
    {
        const bool ascending = (effectiveOrder == SortOrderType::SortByType_ASC);
        std::sort(states.begin(), states.end(), [ascending](DesktopRootItemState* a, DesktopRootItemState* b) {
            const bool aIsSpecial = (a->model.objectType == ObjectType::SpecialFolder);
            const bool bIsSpecial = (b->model.objectType == ObjectType::SpecialFolder);
            if (aIsSpecial != bIsSpecial) {
                return aIsSpecial;
            }
            const bool aIsDirLike = (a->model.objectType == ObjectType::DiskLetter) || a->model.fileInfo.isDir();
            const bool bIsDirLike = (b->model.objectType == ObjectType::DiskLetter) || b->model.fileInfo.isDir();
            const QString extA = aIsDirLike ? QString() : a->model.fileInfo.suffix().toLower();
            const QString extB = bIsDirLike ? QString() : b->model.fileInfo.suffix().toLower();
            if (extA != extB) {
                return ascending ? (extA < extB) : (extA > extB);
            }
            return a->model.displayName.toLower() < b->model.displayName.toLower();
        });
        break;
    }
    case SortOrderType::SortByDateModified_ASC:
    case SortOrderType::SortByDateModified_DESC:
    {
        const bool ascending = (effectiveOrder == SortOrderType::SortByDateModified_ASC);
        std::sort(states.begin(), states.end(), [ascending](DesktopRootItemState* a, DesktopRootItemState* b) {
            const bool aIsSpecial = (a->model.objectType == ObjectType::SpecialFolder);
            const bool bIsSpecial = (b->model.objectType == ObjectType::SpecialFolder);
            if (aIsSpecial != bIsSpecial) {
                return aIsSpecial;
            }
            const QDateTime dateA = a->model.fileInfo.lastModified();
            const QDateTime dateB = b->model.fileInfo.lastModified();
            if (dateA != dateB) {
                return ascending ? (dateA < dateB) : (dateA > dateB);
            }
            return a->model.displayName.toLower() < b->model.displayName.toLower();
        });
        break;
    }
    case SortOrderType::SortByCategorized:
    default:
        std::sort(states.begin(), states.end(), [&](DesktopRootItemState* a, DesktopRootItemState* b) {
            const int pA = categorizedPriorityForState(a);
            const int pB = categorizedPriorityForState(b);
            if (pA != pB) {
                return pA < pB;
            }
            return collator.compare(a->model.sortName, b->model.sortName) < 0;
        });
        break;
    }
}

void ListView::assignCustomSortOrders(const QVector<DesktopRootItemState*>& orderedStates)
{
    int order = 0;
    for (DesktopRootItemState* state : orderedStates) {
        if (!state || !state->model.boxId.isEmpty()) {
            continue;
        }
        state->model.customSortOrder = order++;
    }
}

void ListView::clearDesktopCustomSortOrders()
{
    const QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
    for (DesktopRootItemState* state : states) {
        if (!state || !state->model.boxId.isEmpty()) {
            continue;
        }
        state->model.customSortOrder = -1;
    }
}

int ListView::desktopCustomInsertionIndexForPos(const QPoint& pos, const QStringList& draggedPaths) const
{
    QVector<DesktopRootItemState*> orderedStates = collectAutoArrangeStates();
    sortAutoArrangeStates(orderedStates, SortOrderType::SortByCustom);
    QSet<QString> draggedPathSet;
    for (const QString& path : draggedPaths) {
        draggedPathSet.insert(normalizeDesktopItemPath(path));
    }
    orderedStates.erase(std::remove_if(orderedStates.begin(), orderedStates.end(), [&](DesktopRootItemState* state) {
        return state && draggedPathSet.contains(normalizeDesktopItemPath(state->model.filePath));
    }), orderedStates.end());

    for (int i = 0; i < orderedStates.size(); ++i) {
        const QRect itemRect = orderedStates[i]->model.renderData.itemRect;
        if (itemRect.contains(pos)) {
            return pos.y() <= itemRect.center().y() ? i : i + 1;
        }
    }

    int bestIndex = 0;
    qint64 bestScore = LLONG_MAX;
    for (int i = 0; i <= orderedStates.size(); ++i) {
        const QRect rect = desktopAutoArrangeInsertionIndicatorRectForIndex(i, orderedStates);
        if (rect.isNull()) {
            continue;
        }

        const int dx = pos.x() < rect.left()
            ? rect.left() - pos.x()
            : (pos.x() > rect.right() ? pos.x() - rect.right() : 0);
        const int dy = pos.y() < rect.top()
            ? rect.top() - pos.y()
            : (pos.y() > rect.bottom() ? pos.y() - rect.bottom() : 0);
        const qint64 score = static_cast<qint64>(dx) * dx + static_cast<qint64>(dy) * dy;
        if (score < bestScore) {
            bestScore = score;
            bestIndex = i;
        }
    }

    return qBound(0, bestIndex, orderedStates.size());
}

void ListView::updateDesktopAutoArrangeReorderPreview(const QPoint& pos, const QStringList& draggedPaths)
{
    const int nextIndex = desktopCustomInsertionIndexForPos(pos, draggedPaths);
    QVector<DesktopRootItemState*> allOrderedStates = collectAutoArrangeStates();
    sortAutoArrangeStates(allOrderedStates, SortOrderType::SortByCustom);
    QVector<DesktopRootItemState*> orderedStates = collectAutoArrangeStates();
    sortAutoArrangeStates(orderedStates, SortOrderType::SortByCustom);
    QSet<QString> draggedPathSet;
    for (const QString& path : draggedPaths) {
        draggedPathSet.insert(normalizeDesktopItemPath(path));
    }
    orderedStates.erase(std::remove_if(orderedStates.begin(), orderedStates.end(), [&](DesktopRootItemState* state) {
        return state && draggedPathSet.contains(normalizeDesktopItemPath(state->model.filePath));
    }), orderedStates.end());

    QRect nextIndicatorRect;
    const int lineHeight = 2;
    const int horizontalInset = 8;
    const int indicatorWidth = qMax(20, m_iconWidth - horizontalInset * 2);
    for (DesktopRootItemState* draggedState : allOrderedStates) {
        if (!draggedState || !draggedPathSet.contains(normalizeDesktopItemPath(draggedState->model.filePath))) {
            continue;
        }

        const QRect draggedRect = draggedState->model.renderData.itemRect;
        const QRect draggedSlotRect = draggedRect.adjusted(0, -qMax(2, m_rowSpacing / 2), 0, qMax(2, m_rowSpacing / 2));
        if (!draggedSlotRect.contains(pos)) {
            continue;
        }

        const int draggedIndex = allOrderedStates.indexOf(draggedState);
        if (draggedIndex < 0) {
            continue;
        }

        const bool insertBeforeItem = pos.y() <= draggedRect.center().y();
        int y = insertBeforeItem ? draggedRect.top() : draggedRect.bottom();
        int left = draggedRect.left() + horizontalInset;
        int width = indicatorWidth;
        if (insertBeforeItem && draggedIndex > 0 && allOrderedStates[draggedIndex - 1]) {
            const QRect prevRect = allOrderedStates[draggedIndex - 1]->model.renderData.itemRect;
            y = (prevRect.bottom() + draggedRect.top()) / 2;
            y = qMin(y, draggedRect.top() - lineHeight);
            left = qMin(prevRect.left(), draggedRect.left()) + horizontalInset;
            const int right = qMax(prevRect.right(), draggedRect.right()) - horizontalInset;
            width = qMax(20, right - left);
        } else if (!insertBeforeItem && draggedIndex + 1 < allOrderedStates.size() && allOrderedStates[draggedIndex + 1]) {
            const QRect nextRect = allOrderedStates[draggedIndex + 1]->model.renderData.itemRect;
            y = (draggedRect.bottom() + nextRect.top()) / 2;
            y = qMax(y, draggedRect.bottom() + lineHeight);
            left = qMin(draggedRect.left(), nextRect.left()) + horizontalInset;
            const int right = qMax(draggedRect.right(), nextRect.right()) - horizontalInset;
            width = qMax(20, right - left);
        }

        nextIndicatorRect = QRect(left, y - lineHeight / 2, width, lineHeight);
        break;
    }

    for (int i = 0; i < allOrderedStates.size(); ++i) {
        if (!nextIndicatorRect.isNull()) {
            break;
        }
        DesktopRootItemState* hoverState = allOrderedStates[i];
        if (!hoverState || !hoverState->model.renderData.itemRect.contains(pos)) {
            continue;
        }

        const QRect hoverRect = hoverState->model.renderData.itemRect;
        const bool insertBeforeItem = pos.y() <= hoverRect.center().y();
        if (insertBeforeItem) {
            if (i > 0 && allOrderedStates[i - 1]) {
                const QRect prevRect = allOrderedStates[i - 1]->model.renderData.itemRect;
                const int y = (prevRect.bottom() + hoverRect.top()) / 2;
                const int left = qMin(prevRect.left(), hoverRect.left()) + horizontalInset;
                const int right = qMax(prevRect.right(), hoverRect.right()) - horizontalInset;
                nextIndicatorRect = QRect(left,
                    y - lineHeight / 2,
                    qMax(20, right - left),
                    lineHeight);
            } else {
                nextIndicatorRect = QRect(hoverRect.left() + horizontalInset,
                    hoverRect.top() - lineHeight / 2,
                    indicatorWidth,
                    lineHeight);
            }
        } else {
            if (i + 1 < allOrderedStates.size() && allOrderedStates[i + 1]) {
                const QRect nextRect = allOrderedStates[i + 1]->model.renderData.itemRect;
                const int y = (hoverRect.bottom() + nextRect.top()) / 2;
                const int left = qMin(hoverRect.left(), nextRect.left()) + horizontalInset;
                const int right = qMax(hoverRect.right(), nextRect.right()) - horizontalInset;
                nextIndicatorRect = QRect(left,
                    y - lineHeight / 2,
                    qMax(20, right - left),
                    lineHeight);
            } else {
                nextIndicatorRect = QRect(hoverRect.left() + horizontalInset,
                    hoverRect.bottom() - lineHeight / 2,
                    indicatorWidth,
                    lineHeight);
            }
        }
        break;
    }
    if (nextIndicatorRect.isNull()) {
        nextIndicatorRect = desktopAutoArrangeInsertionIndicatorRectForIndex(nextIndex, orderedStates);
    }
    for (DesktopRootItemState* draggedState : allOrderedStates) {
        if (!draggedState || !draggedPathSet.contains(normalizeDesktopItemPath(draggedState->model.filePath)) || nextIndicatorRect.isNull()) {
            continue;
        }

        const QRect draggedRect = draggedState->model.renderData.itemRect;
        if (!draggedRect.intersects(nextIndicatorRect) && !draggedRect.contains(nextIndicatorRect.center())) {
            continue;
        }

        const int y = pos.y() <= draggedRect.center().y()
            ? draggedRect.top() - lineHeight
            : draggedRect.bottom() + lineHeight;
        nextIndicatorRect.moveTop(y - lineHeight / 2);
        break;
    }

    const bool sameDraggedItems = (m_desktopReorderDraggedPaths == draggedPaths);
    if (m_desktopReorderInsertIndex == nextIndex &&
        sameDraggedItems &&
        m_desktopReorderIndicatorRect == nextIndicatorRect) {
        return;
    }

    m_desktopReorderInsertIndex = nextIndex;
    m_desktopReorderDraggedPaths = draggedPaths;
    m_desktopReorderIndicatorRect = nextIndicatorRect;
    update();
}

void ListView::clearDesktopAutoArrangeReorderPreview()
{
    if (m_desktopReorderInsertIndex < 0 &&
        m_desktopReorderDraggedPaths.isEmpty() &&
        m_desktopReorderIndicatorRect.isNull()) {
        return;
    }

    m_desktopReorderInsertIndex = -1;
    m_desktopReorderDraggedPaths.clear();
    m_desktopReorderIndicatorRect = QRect();
    update();
}

QRect ListView::desktopAutoArrangeInsertionIndicatorRect() const
{
    if (!m_iconShowRule.autoArrange ||
        m_iconShowRule.sortType != SortOrderType::SortByCustom ||
        m_desktopReorderInsertIndex < 0) {
        return QRect();
    }

    if (!m_desktopReorderIndicatorRect.isNull()) {
        return m_desktopReorderIndicatorRect;
    }

    QVector<DesktopRootItemState*> orderedStates = collectAutoArrangeStates();
    sortAutoArrangeStates(orderedStates, SortOrderType::SortByCustom);
    QSet<QString> draggedPathSet;
    for (const QString& path : m_desktopReorderDraggedPaths) {
        draggedPathSet.insert(normalizeDesktopItemPath(path));
    }
    orderedStates.erase(std::remove_if(orderedStates.begin(), orderedStates.end(), [&](DesktopRootItemState* state) {
        return state && draggedPathSet.contains(normalizeDesktopItemPath(state->model.filePath));
    }), orderedStates.end());

    return desktopAutoArrangeInsertionIndicatorRectForIndex(m_desktopReorderInsertIndex, orderedStates);
}

QRect ListView::desktopAutoArrangeInsertionIndicatorRectForIndex(int insertIndex, const QVector<DesktopRootItemState*>& orderedStates) const
{
    insertIndex = qBound(0, insertIndex, orderedStates.size());
    const int lineHeight = 2;
    const int horizontalInset = 8;
    const int indicatorWidth = qMax(20, m_iconWidth - horizontalInset * 2);

    if (orderedStates.isEmpty()) {
        return QRect(m_marginLeft + horizontalInset, m_marginTop - lineHeight / 2, indicatorWidth, lineHeight);
    }

    if (insertIndex <= 0) {
        const QRect nextRect = orderedStates.first()->model.renderData.itemRect;
        const int y = qMax(0, nextRect.top());
        return QRect(nextRect.left() + horizontalInset, y - lineHeight / 2, indicatorWidth, lineHeight);
    }

    if (insertIndex >= orderedStates.size()) {
        const QRect prevRect = orderedStates.last()->model.renderData.itemRect;
        const int y = qMin(height() - lineHeight, prevRect.bottom());
        return QRect(prevRect.left() + horizontalInset, y - lineHeight / 2, indicatorWidth, lineHeight);
    }

    const QRect prevRect = orderedStates[insertIndex - 1]->model.renderData.itemRect;
    const QRect nextRect = orderedStates[insertIndex]->model.renderData.itemRect;
    const int strideX = m_iconWidth + m_colSpacing;
    const bool sameColumn = qAbs(prevRect.center().x() - nextRect.center().x()) <= qMax(1, strideX / 2);

    if (sameColumn) {
        const int y = (prevRect.bottom() + nextRect.top()) / 2;
        const int left = qMin(prevRect.left(), nextRect.left()) + horizontalInset;
        const int right = qMax(prevRect.right(), nextRect.right()) - horizontalInset;
        return QRect(left, y - lineHeight / 2, qMax(20, right - left), lineHeight);
    }

    const int y = qMax(0, nextRect.top());
    return QRect(nextRect.left() + horizontalInset, y - lineHeight / 2, indicatorWidth, lineHeight);
}

QVector<QPoint> ListView::layoutAutoArrangeItems(const QVector<DesktopRootItemState*>& states) const
{
    QVector<QPoint> positions;
    positions.reserve(states.size());

    const int itemWidth = m_iconWidth;
    const int itemHeight = desktopItemFootprintHeight();
    const int strideX = itemWidth + m_colSpacing;
    const int strideY = itemHeight + m_rowSpacing;
    int iconX = desktopLayoutLeft();
    int iconY = desktopLayoutTop();
    const int usableBottom = desktopUsableBottom();
    const int maxRows = maxGridRowsForBounds(desktopLayoutTop(), usableBottom, itemHeight, strideY);
    const int maxCols = maxGridColsForBounds(desktopLayoutLeft(), desktopUsableRight(), itemWidth, strideX);
    const int visibleCapacity = qMax(1, maxRows * maxCols);
    QSet<quint64> occupiedSlots = desktopBlockedOccupiedCells();
    int searchIndex = 0;
    logDesktopLayoutMetrics("AutoArrangeModel",
                            this,
                            desktopLayoutLeft(),
                            desktopLayoutTop(),
                            desktopUsableRight(),
                            usableBottom,
                            itemWidth,
                            itemHeight,
                            strideX,
                            strideY);
    for (DesktopRootItemState* state : states) {
        if (!state) {
            positions.push_back(QPoint());
            continue;
        }

        QPoint assignedCell(-1, -1);
        for (int offset = 0; offset < visibleCapacity; ++offset) {
            const QPoint candidateCell = gridIndexToCell((searchIndex + offset) % visibleCapacity, maxRows);
            const quint64 slotKey = packGridCell(candidateCell.x(), candidateCell.y());
            if (!occupiedSlots.contains(slotKey)) {
                assignedCell = candidateCell;
                occupiedSlots.insert(slotKey);
                searchIndex = (searchIndex + offset + 1) % visibleCapacity;
                break;
            }
        }

        if (assignedCell.x() < 0) {
            assignedCell = gridIndexToCell(searchIndex % visibleCapacity, maxRows);
            searchIndex = (searchIndex + 1) % visibleCapacity;
        }
        iconX = desktopLayoutLeft() + assignedCell.x() * strideX;
        iconY = desktopLayoutTop() + assignedCell.y() * strideY;
        positions.push_back(QPoint(iconX, iconY));
    }

    return positions;
}

void ListView::finalizeAutoArrangeLayout(const QVector<DesktopRootItemState*>& states,
                                         const QVector<QPoint>& positions,
                                         bool persist,
                                         bool refreshSelection)
{
    if (states.size() != positions.size()) {
        return;
    }

    QScopedPointer<ListViewSelectionSnapshot> selectionSnapshot;
    if (refreshSelection) {
        selectionSnapshot.reset(new ListViewSelectionSnapshot(ListViewSelectionSnapshot::capture(*this)));
    }

    for (int i = 0; i < states.size(); ++i) {
        DesktopRootItemState* state = states[i];
        if (!state) {
            continue;
        }

        const QPoint& pos = positions[i];
        state->model.position = pos;
        state->model.baseSize = QSize(m_iconWidth, m_iconHeight);
    }

    markAllDesktopRenderDataDirty();
    updateAllItemRenderData();
    rebuildSpatialIndex();
    update();
    if (persist) {
        requestSaveBoxes();
    }
    if (selectionSnapshot) {
        selectionSnapshot->restore(*this, false);
    }
}

void ListView::applyAutoArrangeLayout(bool persist, bool refreshSelection)
{
    QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
    sortAutoArrangeStates(states, m_iconShowRule.sortType);
    if (m_iconShowRule.sortType == SortOrderType::SortByCustom) {
        assignCustomSortOrders(states);
    }
    const QVector<QPoint> positions = layoutAutoArrangeItems(states);
    finalizeAutoArrangeLayout(states, positions, persist, refreshSelection);
}

void ListView::applyAutoArrangeAfterDesktopMutation(bool persist, bool refreshSelection)
{
    if (!m_iconShowRule.autoArrange) {
        return;
    }

    applyAutoArrangeLayout(persist, refreshSelection);
}

void ListView::loadAutoLayout()
{
    applyAutoArrangeLayout(false, false);
}

void ListView::loadGridLayout()
{
    {
        const int strideX = m_iconWidth + m_colSpacing;
        const int itemHeight = desktopItemFootprintHeight();
        const int strideY = itemHeight + m_rowSpacing;
        const int layoutLeft = desktopLayoutLeft();
        const int layoutTop = desktopLayoutTop();
        const int maxRows = maxGridRowsForBounds(layoutTop, desktopUsableBottom(), itemHeight, strideY);
        const int maxCols = maxGridColsForBounds(layoutLeft, desktopUsableRight(), m_iconWidth, strideX);
        const int visibleCapacity = qMax(1, maxCols * maxRows);
        QSet<quint64> occupiedSlots;
        occupiedSlots.unite(desktopBlockedOccupiedCells());
        int overflowIndex = 0;

        for (auto& state : m_itemStates) {
            if (!shouldPaintDesktopModelState(state)) {
                continue;
            }

            state.model.baseSize = QSize(m_iconWidth, m_iconHeight);
            state.model.effectiveSize = state.model.baseSize;

            QPoint p = state.model.position;
            if (p == QPoint(0, 0)) {
                p = generateItemPosition();
                state.model.position = p;
            }

            if (m_iconShowRule.gridAlignment) {
                const int requestedIndex = pointToGridLinearIndex(p, layoutLeft, layoutTop, strideX, strideY, maxRows);
                int gridX = 0;
                int gridY = 0;

                if (occupiedSlots.size() < visibleCapacity) {
                    for (int step = 0; step < visibleCapacity; ++step) {
                        const int linearIndex = (requestedIndex + step) % visibleCapacity;
                        const QPoint candidateCell = gridIndexToCell(linearIndex, maxRows);
                        const quint64 slotKey = packGridCell(candidateCell.x(), candidateCell.y());
                        if (!occupiedSlots.contains(slotKey)) {
                            gridX = candidateCell.x();
                            gridY = candidateCell.y();
                            occupiedSlots.insert(slotKey);
                            break;
                        }
                    }
                } else {
                    const QPoint overflowCell = gridIndexToCell(overflowIndex % visibleCapacity, maxRows);
                    gridX = overflowCell.x();
                    gridY = overflowCell.y();
                    ++overflowIndex;
                }

                state.model.position = gridCellToPoint(gridX, gridY, layoutLeft, layoutTop, strideX, strideY);
            }
        }

        markAllDesktopRenderDataDirty();
        updateAllItemRenderData();
        rebuildSpatialIndex();
        update();
        return;
    }
}

QPoint ListView::generateItemPosition()
{
    qDebug() << "[generateItemPosition] gridAlignment=" << m_iconShowRule.gridAlignment 
             << "m_lastRightClickPos=" << m_lastRightClickPos;
    
    // 闂傚倸鍊搁悧濠囧礄閻ョ牫d闂佹眹鍩勯崹浼村疮椤愶附鍎戞い鎺嶈兌閳绘棃鏌￠崘銊モ偓褰掑汲閻曠枠闂備胶鎳撻悘婵堢矓瀹曞洨绀婇柡鍐ㄥ€归崰鍡涙煥閺囨浜鹃悷婊勬緲閸婂潡骞嗛崘顔肩闁告挷鑳堕ˇ鈺呮⒑閸涘﹦鎳勫┑鐐诧躬瀵偊顢橀姀鈥虫濠电姴锕ら崯顐ょ矆鐎ｎ喗鐓涢柛灞剧⊕缁惰尙绱撻崒娑欏磳闁轰礁绉靛鍕槈濮橀硸妲烽梻浣哄劦閺呮盯鎮洪妸褎鏆滈悗娑櫳戞刊鎾偣閹帒濡介柡鍡楃箰椤顦村┑顔炬暬閹儵鏁愭径濠勭厬闂佽鍎兼慨銈夊汲濞戞◤褰掓晲閸涱垍銈夋煟?
    if (!m_iconShowRule.gridAlignment && m_lastRightClickPos != QPoint(-1, -1)) {
        QPoint pos = m_lastRightClickPos;
        qDebug() << "[generateItemPosition] Using right-click position:" << pos;
        // 闂傚倷鐒﹁ぐ鍐矓閸洘鍋?m_lastRightClickPos闂備焦瀵х粙鎴︽儗閸屾锝囨嫚瀹割喗妗ㄩ梺闈涚箞閸婃牗鎱ㄥǎ鐤橀梻浣告啞鐢绮欓幋鐘电＝闁瑰墽绮崕宥夋煕閺囥劌澧柣搴枛闇夐柣妯挎珪閺嬪嫮绱掗垾鍐叉殲闁瑰嘲鎳愰幑鍕Ω閿旇姤顓归梺鑽ゅТ濞层垽宕曢崡鐑嗙劷婵°倕鎳庨崘鈧?
        m_lastRightClickPos = QPoint(-1, -1);
        return resolveCreatePosition(pos);
    }
    
    qDebug() << "[generateItemPosition] Using grid layout position";

    const int strideX = m_iconWidth + m_colSpacing;
    const int itemHeight = desktopItemFootprintHeight();
    const int strideY = itemHeight + m_rowSpacing;
    const int layoutLeft = desktopLayoutLeft();
    const int layoutTop = desktopLayoutTop();
    const int maxRows = maxGridRowsForBounds(layoutTop, desktopUsableBottom(), itemHeight, strideY);
    const int maxCols = maxGridColsForBounds(layoutLeft, desktopUsableRight(), m_iconWidth, strideX);
    const int visibleCapacity = qMax(1, maxCols * maxRows);
    QSet<quint64> occupiedSlots;
    int desktopItemCount = 0;
    occupiedSlots.unite(desktopBlockedOccupiedCells());

    for (const auto& state : m_itemStates) {
        if (!state.model.boxId.isEmpty() || !state.model.visible) {
            continue;
        }
        if (state.model.position.x() > 0 || state.model.position.y() > 0) {
            ++desktopItemCount;
            const QPoint gridCell = pointToGridCell(state.model.position, layoutLeft, layoutTop, strideX, strideY);
            occupiedSlots.insert(packGridCell(qBound(0, gridCell.x(), maxCols - 1), qBound(0, gridCell.y(), maxRows - 1)));
        }
    }
    for (auto it = m_pendingCreatedItemPositions.constBegin(); it != m_pendingCreatedItemPositions.constEnd(); ++it) {
        ++desktopItemCount;
        const QPoint gridCell = pointToGridCell(it.value(), layoutLeft, layoutTop, strideX, strideY);
        occupiedSlots.insert(packGridCell(qBound(0, gridCell.x(), maxCols - 1), qBound(0, gridCell.y(), maxRows - 1)));
    }

    if (occupiedSlots.size() < visibleCapacity) {
        for (int gridX = 0; gridX < maxCols; ++gridX) {
            for (int gridY = 0; gridY < maxRows; ++gridY) {
                if (occupiedSlots.contains(packGridCell(gridX, gridY))) {
                    continue;
                }

                return gridCellToPoint(gridX, gridY, layoutLeft, layoutTop, strideX, strideY);
            }
        }
    }

    const QPoint overflowCell = gridIndexToCell(desktopItemCount % visibleCapacity, maxRows);
    return gridCellToPoint(overflowCell.x(), overflowCell.y(), layoutLeft, layoutTop, strideX, strideY);

    return QPoint(0, 0); // 闂備胶顭堢换鎴犲垝瀹€鈧懞閬嶆惞椤愶絼姘﹂梺鍝勫€圭€笛呯矆閳?
}

void ListView::updateAllItemRenderData()
{
    const DesktopRenderParams params = currentRenderParams();
    const QString focusedDesktopPath = effectiveFocusedDesktopPath();
    const QString focusedBoxPath = effectiveFocusedBoxPath();
    bool rebuiltAny = false;

    for (auto& state : m_itemStates) {
        const QString normalizedPath = normalizeDesktopItemPath(state.model.filePath);
        state.model.focused =
            (!m_focusedDesktopPath.isEmpty() && normalizedPath == focusedDesktopPath) ||
            (!m_focusedBoxPath.isEmpty() && normalizedPath == focusedBoxPath);

        if (!state.renderDataDirty) {
            continue;
        }

        state.model.renderData = buildRenderDataFromModel(state.model, params);
        state.renderDataDirty = false;
        rebuiltAny = true;
    }

    if (rebuiltAny) {
        markSpatialIndexDirty();
    }
}

QPoint ListView::remapPositionForViewMetrics(const QPoint& pos,
                                             int oldIconWidth,
                                             int oldIconHeight,
                                             int oldRowSpacing,
                                             int oldColSpacing,
                                             int oldMarginTop,
                                             int oldMarginLeft) const
{
    if (pos == QPoint(0, 0)) {
        return pos;
    }

    const int oldStrideX = qMax(1, oldIconWidth + oldColSpacing);
    const int oldStrideY = qMax(1, oldIconHeight + oldRowSpacing);
    const int newStrideX = qMax(1, m_iconWidth + m_colSpacing);
    const int newStrideY = qMax(1, desktopItemFootprintHeight() + m_rowSpacing);

    QPoint mappedPos = pos;
    if (m_iconShowRule.gridAlignment) {
        const QPoint gridCell = pointToGridCell(pos, oldMarginLeft, oldMarginTop, oldStrideX, oldStrideY);
        mappedPos = gridCellToPoint(gridCell.x(), gridCell.y(), desktopLayoutLeft(), desktopLayoutTop(), newStrideX, newStrideY);
    } else {
        const double logicalX = static_cast<double>(pos.x() - oldMarginLeft) / oldStrideX;
        const double logicalY = static_cast<double>(pos.y() - oldMarginTop) / oldStrideY;
        mappedPos.setX(desktopLayoutLeft() + qRound(logicalX * newStrideX));
        mappedPos.setY(desktopLayoutTop() + qRound(logicalY * newStrideY));
    }

    mappedPos.setX(qMax(desktopLayoutLeft(), qMin(mappedPos.x(), desktopUsableRight() - m_iconWidth + 1)));
    mappedPos.setY(qMax(desktopLayoutTop(), qMin(mappedPos.y(), desktopUsableBottom() - desktopItemFootprintHeight() + 1)));
    return mappedPos;
}

void ListView::applyViewModeMetricsChange(int oldIconWidth,
                                          int oldIconHeight,
                                          int oldRowSpacing,
                                          int oldColSpacing,
                                          int oldMarginTop,
                                          int oldMarginLeft)
{
    if (oldIconWidth == m_iconWidth &&
        oldIconHeight == m_iconHeight &&
        oldRowSpacing == m_rowSpacing &&
        oldColSpacing == m_colSpacing &&
        oldMarginTop == m_marginTop &&
        oldMarginLeft == m_marginLeft) {
        return;
    }

    for (auto& state : m_itemStates) {
        if (!state.model.boxId.isEmpty()) {
            continue;
        }

        const QPoint currentPos = state.model.position;
        if (currentPos == QPoint(0, 0)) {
            continue;
        }

        state.model.position = remapPositionForViewMetrics(
            currentPos,
            oldIconWidth,
            oldIconHeight,
            oldRowSpacing,
            oldColSpacing,
            oldMarginTop,
            oldMarginLeft);
    }

    for (auto it = m_savedIconPositions.begin(); it != m_savedIconPositions.end(); ++it) {
        if (!it.value().boxId.isEmpty()) {
            continue;
        }

        const QPoint currentPos(it.value().x, it.value().y);
        const QPoint mappedPos = remapPositionForViewMetrics(
            currentPos,
            oldIconWidth,
            oldIconHeight,
            oldRowSpacing,
            oldColSpacing,
            oldMarginTop,
            oldMarginLeft);
        it.value().x = mappedPos.x();
        it.value().y = mappedPos.y();
    }

    for (auto it = m_pendingCreatedItemPositions.begin(); it != m_pendingCreatedItemPositions.end(); ++it) {
        it.value() = remapPositionForViewMetrics(
            it.value(),
            oldIconWidth,
            oldIconHeight,
            oldRowSpacing,
            oldColSpacing,
            oldMarginTop,
            oldMarginLeft);
    }
    markAllDesktopRenderDataDirty();
    updateAllItemRenderData();
    return;
}

void ListView::saveBoxes() {
    if (m_isLoadingPersistence) {
        return;
    }

    LayoutPersistenceService persistenceService;
    LayoutPersistenceState state;
    state.enablePersistence = m_enablePersistence;
    state.viewMode = (int)m_iconShowRule.viewMode;
    state.sortType = (int)m_iconShowRule.sortType;
    state.preAutoSort = (int)m_iconShowRule.sortTypeBeforeAutoArrange;
    state.iconPosMode = (int)m_iconShowRule.iconPosMode;
    state.autoArrange = m_iconShowRule.autoArrange;
    state.gridAlignment = m_iconShowRule.gridAlignment;
    state.showDesktopIcons = m_iconShowRule.showDesktopIcons;
    state.doubleClickHideIcons = m_iconShowRule.doubleClickHideIcons;
    state.showShortCutArrow = m_iconShowRule.showShortCutArrow;
    state.iconPositions = persistenceService.collectIconPositions(
        m_itemStates,
        BoxManager::instance()->allBoxes()
    );
    m_savedIconPositions = persistenceService.toSavedIconPositions(state.iconPositions);
    persistenceService.save(m_persistencePath, state);
}

void ListView::requestSaveBoxes() {
    if (m_isLoadingPersistence) {
        return;
    }
    BoxManager::instance()->requestSave();
}

void ListView::applyBoxAssignmentsFromConfig(const QVector<QMap<QString, QVariant>>& boxData) {
    qDebug() << "[applyBoxAssignmentsFromConfig] Processing" << boxData.size() << "boxes";
    QSet<LinBox*> boxesToRefresh;
    
    for (const auto& data : boxData) {
        const QString boxId = data.value("id").toString();
        LinBox* targetBox = BoxManager::instance()->findBoxById(boxId);
        if (!targetBox) {
            qWarning() << "[applyBoxAssignmentsFromConfig] Box not found:" << boxId;
            continue;
        }

        if (targetBox->isFolderMapped()) {
            continue;
        }
        
        const QStringList files = data.value("files").toStringList();
        qDebug() << "[applyBoxAssignmentsFromConfig] Box" << boxId << "has" << files.size() << "files";
        
        for (const QString& path : files) {
            DesktopRootItemState* state = findDesktopStateByPath(path);
            if (!state) {
                qWarning() << "[applyBoxAssignmentsFromConfig] Item not found:" << path;
                continue;
            }

            qDebug() << "[applyBoxAssignmentsFromConfig] Moving model item to box:" << state->model.displayName
                     << "path:" << state->model.filePath
                     << "to box:" << targetBox;
            DesktopRootItemState stateCopy = *state;
            stateCopy.model.boxId = boxId;
            stateCopy.model.visible = true;
            targetBox->addItemState(stateCopy, false);
            removeDesktopStateByPath(path);
            boxesToRefresh.insert(targetBox);
            
            // 闂佽崵濮崇粈浣规櫠娴犲鍋?boxId 婵°倗濮烽崑鐘测枖濞戙垺鍎?item 闂佽楠稿﹢閬嶅磻閵堝拋鐎舵い鏍嚙鎼达絾瀚氶柟缁樺俯濞奸亶姊?box 濠?
            // 濠电偠鎻紞鈧繛澶嬫礋瀵?addListItem 闂備礁鎼ˇ顓㈠磿閺屻儱閿ゅ┑鐘叉搐缁€澶愭煟濞嗗繑鍣界紒鈧径鎰€甸柣鐔告緲閺嗘洜绱掓潏銊︾凡妞ゎ偁鍨介弫鎰板炊閿濆倸浜炬俊顖氬悑婵ジ鏌涢幘妤€鎳忛悗顓㈡⒑缂佹澧褍閰ｉ幃浼村Ψ閳轰胶鐓戦梺鎸庣箓鐎氼垳绮?m_contentArea
        }
    }

    for (LinBox* box : boxesToRefresh) {
        if (!box) continue;
        box->refreshLayout();
        box->raise();
    }
}

void ListView::applySavedPositionsToItems(const QMap<QString, QMap<QString, QVariant>>& iconPositions) {
    for (auto it = iconPositions.begin(); it != iconPositions.end(); ++it) {
        const QMap<QString, QVariant>& posInfo = it.value();
        const QString boxIdFromConfig = posInfo.value("boxId").toString();
        const QPoint savedPos(posInfo.value("x").toInt(), posInfo.value("y").toInt());

        if (DesktopRootItemState* state = findDesktopStateByPath(it.key())) {
            state->model.boxId = boxIdFromConfig;
            state->model.customSortOrder = posInfo.value("ord").toInt();
            state->model.position = savedPos;
        } else if (LinBox* box = BoxManager::instance()->findBoxContainingItem(it.key())) {
            if (DesktopRootItemState* boxState = box->findItemStateByPath(it.key())) {
                boxState->model.boxId = boxIdFromConfig;
                boxState->model.customSortOrder = posInfo.value("ord").toInt();
                boxState->model.position = savedPos;
            }
        } else {
            qWarning() << "[ListView] No desktop item matched config path:" << it.key();
        }
    }
}

void ListView::loadBoxes(bool autoUpdateState) {
    const ListViewSelectionSnapshot selectionSnapshot = ListViewSelectionSnapshot::capture(*this);

    m_isLoadingPersistence = true;
    m_forceConfigLayout = false;
    m_hasLoadedConfigLayout = false;

    QVector<LinBox*> existingBoxes;
    for (const QPointer<LinBox>& box : BoxManager::instance()->allBoxes()) {
        if (box) existingBoxes.append(box.data());
    }
    for (auto* box : existingBoxes) {
        if (box) {
            if (box->isFolderMapped()) {
                clearMappedFolderItemsForBox(box);
            }
            for (const DesktopRootItemState& boxedState : box->itemStates()) {
                if (!boxedState.model.filePath.isEmpty()) {
                    DesktopRootItemState restoredState = boxedState;
                    resetStateForDesktopView(restoredState, m_iconWidth, m_iconHeight);
                    m_itemStates.push_back(restoredState);
                    markDesktopStateIndexDirty();
                }
            }
            box->deleteLater();
        }
    }
    BoxManager::instance()->clearBoxes();
    m_mappedBoxFilePaths.clear();
    for (auto& state : m_itemStates) {
        state.model.boxId.clear();
    }

    LayoutPersistenceService persistenceService;
    LayoutPersistenceState state;
    state.enablePersistence = m_enablePersistence;
    state.viewMode = (int)m_iconShowRule.viewMode;
    state.sortType = (int)m_iconShowRule.sortType;
    state.preAutoSort = (int)m_iconShowRule.sortTypeBeforeAutoArrange;
    state.iconPosMode = (int)m_iconShowRule.iconPosMode;
    state.autoArrange = m_iconShowRule.autoArrange;
    state.gridAlignment = m_iconShowRule.gridAlignment;
    state.showDesktopIcons = m_iconShowRule.showDesktopIcons;
    state.doubleClickHideIcons = m_iconShowRule.doubleClickHideIcons;
    state.showShortCutArrow = m_iconShowRule.showShortCutArrow;

    persistenceService.load(m_persistencePath, autoUpdateState, state);

    m_enablePersistence = state.enablePersistence;
    m_iconShowRule.viewMode = (ViewMode)state.viewMode;
    if (state.viewMode < (int)ViewMode::LargeIcon || state.viewMode > (int)ViewMode::HugeIcon) {
        m_iconShowRule.viewMode = ViewMode::MediumIcon;
    }
    m_iconShowRule.sortType = (SortOrderType)state.sortType;
    m_iconShowRule.sortTypeBeforeAutoArrange = (SortOrderType)state.preAutoSort;
    m_iconShowRule.iconPosMode = (IconPosMode)state.iconPosMode;
    m_iconShowRule.autoArrange = state.autoArrange;
    m_iconShowRule.gridAlignment = state.gridAlignment;
    m_iconShowRule.showDesktopIcons = state.showDesktopIcons;
    m_iconShowRule.doubleClickHideIcons = state.doubleClickHideIcons;
    m_iconShowRule.showShortCutArrow = state.showShortCutArrow;
    if (m_iconShowRule.autoArrange) {
        if (m_iconShowRule.sortType != SortOrderType::SortByCustom) {
            m_iconShowRule.sortTypeBeforeAutoArrange = m_iconShowRule.sortType;
            m_iconShowRule.sortType = SortOrderType::SortByCustom;
        }
        m_iconShowRule.gridAlignment = true;
        m_iconShowRule.iconPosMode = IconPosMode::AlignIconToGrid;
    }

    QVector<QMap<QString, QVariant>>& boxData = state.boxData;
    QMap<QString, QMap<QString, QVariant>>& iconPositions = state.iconPositions;

    m_forceConfigLayout = !boxData.isEmpty() || !iconPositions.isEmpty();
    m_hasLoadedConfigLayout = m_forceConfigLayout;

    syncViewModeSizes();
    updateRightMenuState();

    for (const auto& data : boxData) {
        createBoxFromSavedData(
            data.value("title").toString(),
            QRect(data.value("x").toInt(), data.value("y").toInt(), 
                  data.value("w").toInt(), data.value("h").toInt()),
            data.value("collapsed").toBool(),
            data.value("h").toInt(),
            data.value("showType").toInt(),
            data.value("files").toStringList(),
            QColor(data.value("color").toString()),
            normalizedSavedBoxOpacity(data.value("opacity")),
            data.value("glass").toBool(),
            data.value("id").toString(),
            data.value("mappedFolderPath").toString(),
            false
        );
    }
    applyBoxAssignmentsFromConfig(boxData);
    applySavedPositionsToItems(iconPositions);

    m_savedIconPositions = persistenceService.toSavedIconPositions(iconPositions);
    loadIconLayer();
    m_forceConfigLayout = false;
    update();
    m_isLoadingPersistence = false;
    if (m_iconShowRule.autoArrange) {
        applyAutoArrangeLayout(false, false);
    }
    selectionSnapshot.restore(*this, false);

    qDebug() << "Loaded. Persistence:" << m_enablePersistence;
}

void ListView::createBoxFromSavedData(const QString& title, const QRect& geom, bool collapsed, int origH, int showType, const QStringList& files, const QColor& color, float opacity, bool glass, const QString& explicitId, const QString& mappedFolderPath, bool refresh) {
    LinBox* box = new LinBox(this);
    const QRect safeGeometry = clampedRestoredBoxGeometry(geom);
    const float safeOpacity = clampedRestoredBoxOpacity(opacity);
    const LinBox::BoxShowType safeShowType = restoredBoxShowType(showType);
    if (explicitId.isEmpty()) {
        box->setBoxId(QUuid::createUuid().toString());
    } else {
        box->setBoxId(explicitId);
    }
    
    if (!mappedFolderPath.isEmpty()) {
        if (title.isEmpty()) {
            box->setBoxTitle(QString());
        } else {
            box->setBoxTitle(title);
        }
        box->setMappedFolderPath(mappedFolderPath);
    } else if (!title.isEmpty()) {
        box->setBoxTitle(title);
    }
    
    if (color.isValid()) box->setBackgroundColor(color);
    box->setBackgroundOpacity(safeOpacity);
    box->setGlassEffect(glass);
    
    // Connect to BoxManager and ListView
    BoxManager::instance()->setupConnections(box, this);
    BoxManager::instance()->addBox(box);
    
    box->setGeometry(safeGeometry);
    box->setShowType(safeShowType);
    if (collapsed) box->toggleCollapse();

    box->show();
    box->setGlassEffect(glass); // Apply blur AFTER window is fully visible to prevent black screen
    if (box->isFolderMapped()) {
        loadMappedFolderItemsForBox(box);
    }
    box->refreshLayout(); // Force initial tight packing
    box->raise();

    if (refresh) {
        loadIconLayer();
        update();
        requestSaveBoxes();
    }
}

void ListView::onMenuDestroyBox(LinBox* box) {
    if (!box) return;

    clearDestroyedBoxRuntimeState(box);

    if (box->isFolderMapped()) {
        if (LinDirectoryMonitor* monitor = box->mappedFolderMonitor()) {
            monitor->stopListening();
            box->setMappedFolderMonitor(nullptr);
        }
        clearMappedFolderItemsForBox(box);
        BoxManager::instance()->destroyBox(box, this);
        update();
        return;
    }

    RenameManager::instance()->cancelRename();
    resetRenameClickState();
    const QVector<DesktopRootItemState> boxStates = box->itemStates();
    for (const DesktopRootItemState& boxedState : boxStates) {
        DesktopRootItemState restoredState = boxedState;
        resetStateForDesktopView(restoredState, m_iconWidth, m_iconHeight);
        if (!m_iconShowRule.autoArrange) {
            restoredState.model.position = generateItemPosition();
        }
        m_itemStates.push_back(restoredState);
        markDesktopStateIndexDirty();
    }
    
    BoxManager::instance()->destroyBox(box, this);
    if (m_iconShowRule.autoArrange) {
        applyAutoArrangeAfterDesktopMutation(true, false);
    } else {
        updateAllItemRenderData();
        rebuildSpatialIndex();
        update();
        requestSaveBoxes();
    }
}

void ListView::clearDestroyedBoxRuntimeState(LinBox* box)
{
    if (!box) {
        return;
    }

    const QString boxId = box->boxId();
    const QVector<DesktopRootItemState> currentStates = box->itemStates();
    QStringList pathsToClear;
    auto rememberPath = [this, &pathsToClear](const QString& path) {
        const QString normalizedPath = normalizeDesktopItemPath(path);
        if (!normalizedPath.isEmpty() && !pathsToClear.contains(normalizedPath)) {
            pathsToClear.append(normalizedPath);
        }
    };

    if (LinDirectoryMonitor* monitor = box->mappedFolderMonitor()) {
        monitor->stopListening();
        box->setMappedFolderMonitor(nullptr);
    }

    if (m_pendingShellNewTargetBox == box) {
        clearPendingShellNewTargetBox();
    }

    if (!boxId.isEmpty()) {
        const QStringList mappedPaths = m_mappedBoxFilePaths.value(boxId);
        for (const QString& mappedPath : mappedPaths) {
            rememberPath(mappedPath);
        }
        m_mappedBoxLoadRequestIds.remove(boxId);
        m_mappedBoxFilePaths.remove(boxId);
    }

    for (auto it = m_pendingCreatedItemBoxes.begin(); it != m_pendingCreatedItemBoxes.end(); ) {
        if (it.value() == box) {
            m_pendingCreatedItemInsertIndices.remove(it.key());
            it = m_pendingCreatedItemBoxes.erase(it);
            continue;
        }
        ++it;
    }

    for (const DesktopRootItemState& state : currentStates) {
        rememberPath(state.model.filePath);
    }

    for (const QString& normalizedPath : pathsToClear) {
        m_selectedBoxPaths.removeAll(normalizedPath);
        if (m_focusedBoxPath == normalizedPath) {
            m_focusedBoxPath.clear();
        }
        if (m_anchorBoxPath == normalizedPath) {
            m_anchorBoxPath.clear();
        }
        if (m_lastClickedBoxPath == normalizedPath) {
            m_lastClickedBoxPath.clear();
        }
        if (m_draggedBoxPath == normalizedPath) {
            m_draggedBoxPath.clear();
        }
    }
}

void ListView::onMenuCustomBox(LinBox* box) {
    // Placeholder for color customizations
    qDebug() << "Customize box requested:" << box->boxTitle();
}

void ListView::moveItemToBox(const QString& filePath, LinBox* targetBox, bool refresh, int insertIndex) {
    if (!targetBox || filePath.isEmpty()) {
        return;
    }

    LinBox* sourceBox = BoxManager::instance()->findBoxContainingItem(filePath);

    DesktopRootItemState stateToMove;
    bool hasStateToMove = false;

    if (DesktopRootItemState* desktopState = findDesktopStateByPath(filePath)) {
        stateToMove = *desktopState;
        stateToMove.model.boxId = targetBox->boxId();
        stateToMove.model.visible = true;
        hasStateToMove = true;
        removeDesktopStateByPath(filePath);
    } else if (sourceBox) {
        DesktopRootItemState boxedState;
        if (sourceBox->takeItemState(filePath, boxedState, false)) {
            stateToMove = boxedState;
            stateToMove.model.boxId = targetBox->boxId();
            stateToMove.model.visible = true;
            hasStateToMove = true;
        }
    }

    if (!hasStateToMove) {
        return;
    }

    if (insertIndex >= 0) {
        targetBox->insertItemState(stateToMove, insertIndex, false);
    } else {
        targetBox->addItemState(stateToMove, false);
    }

    if (sourceBox && sourceBox != targetBox) {
        sourceBox->refreshLayout();
        sourceBox->update();
    }

    if (targetBox) {
        targetBox->refreshLayout();
        targetBox->update();
        targetBox->raise();
    }
    
    if (refresh) {
        updateAllItemRenderData();
        update();
        requestSaveBoxes();
    }
    
    qDebug() << "Moved item to box (re-parented):" << filePath << "->" << targetBox->boxTitle() << "index=" << insertIndex;
}

void ListView::moveItemsToBox(const QStringList& filePaths, LinBox* targetBox, int insertIndex, bool refresh)
{
    if (!targetBox) {
        return;
    }

    const QStringList orderedPaths = orderedDraggedPaths(filePaths);
    const QStringList pathsToMove = orderedPaths.isEmpty() ? filePaths : orderedPaths;
    for (int i = 0; i < pathsToMove.size(); ++i) {
        const int targetIndex = insertIndex >= 0 ? insertIndex + i : -1;
        moveItemToBox(pathsToMove.at(i), targetBox, false, targetIndex);
    }

    targetBox->refreshLayout();
    targetBox->update();
    targetBox->raise();

    if (refresh) {
        update();
        requestSaveBoxes();
    }
}

bool ListView::importPathsToBox(const QStringList& sourcePaths, LinBox* targetBox, bool copyMode, int insertIndex, bool preferShortcut)
{
    if (sourcePaths.isEmpty() || !targetBox) {
        return false;
    }

    if (targetBox->isFolderMapped()) {
        if (targetBox->mappedFolderState() != LinBox::MappedFolderState::Normal) {
            return false;
        }

        const QString targetFolder = normalizeMappedFolderPathForLookup(targetBox->mappedFolderPath());
        if (targetFolder.isEmpty()) {
            return false;
        }

        QStringList srcPaths;
        for (const QString& sourcePath : sourcePaths) {
            if (!sourcePath.isEmpty()) {
                srcPaths.append(QDir::toNativeSeparators(sourcePath));
            }
        }
        if (srcPaths.isEmpty()) {
            return false;
        }

        if (preferShortcut) {
            return createShortcutsInFolder(sourcePaths, targetFolder);
        }
        if (copyMode) {
            return CSystemApi::CopyFiles(srcPaths, QDir::toNativeSeparators(targetFolder));
        }
        return CSystemApi::MoveFiles(srcPaths, QDir::toNativeSeparators(targetFolder));
    }

    bool hasMappedSource = false;
    for (const QString& sourcePath : sourcePaths) {
        if (isPathFromMappedBox(sourcePath)) {
            hasMappedSource = true;
            break;
        }
    }

    if (hasMappedSource && !copyMode && !preferShortcut) {
        QStringList movedSourcePaths;
        QStringList createdPaths;
        int nextInsertIndex = insertIndex;
        for (const QString& sourcePath : sourcePaths) {
            if (sourcePath.isEmpty()) {
                continue;
            }

            const QString destinationPath =
                ListViewFileOpsHelper::uniquePasteDestinationPath(sourcePath, m_desktopDir);
            const QString normalizedDestination = normalizeDesktopPath(destinationPath);
            if (normalizedDestination.isEmpty()) {
                continue;
            }

            if (!ListViewFileOpsHelper::movePathToExactDestination(sourcePath, destinationPath)) {
                continue;
            }

            if (!addItemNoDelete(normalizedDestination)) {
                continue;
            }

            moveItemToBox(normalizedDestination, targetBox, false, nextInsertIndex);
            movedSourcePaths.append(sourcePath);
            createdPaths.append(normalizedDestination);
            if (nextInsertIndex >= 0) {
                ++nextInsertIndex;
            }
        }

        if (movedSourcePaths.isEmpty()) {
            return false;
        }

        finalizeMovedModelSourcesAfterFileOp(movedSourcePaths);
        targetBox->refreshLayout();
        targetBox->update();
        targetBox->raise();
        requestSaveBoxes();
        return !createdPaths.isEmpty();
    }

    bool ok = true;
    QStringList createdPaths;

    int nextInsertIndex = insertIndex;
    for (const QString& sourcePath : sourcePaths) {
        if (sourcePath.isEmpty()) {
            continue;
        }

        DesktopRootItemModel sourceModel;
        const bool hasSourceModel = resolveItemModelByPath(sourcePath, sourceModel);
        const bool isShortcutOnlySource =
            hasSourceModel &&
            (sourceModel.itemData.isRealSpecialFolderIcon() ||
             sourceModel.objectType == ObjectType::DiskLetter ||
             sourceModel.filePath.startsWith(QStringLiteral("::")));

        if (copyMode && !preferShortcut && isShortcutOnlySource) {
            ok = false;
            continue;
        }

        const QString destinationPath = preferShortcut
            ? ListViewFileOpsHelper::uniqueShortcutPathForTarget(sourcePath, m_desktopDir)
            : ListViewFileOpsHelper::uniquePasteDestinationPath(sourcePath, m_desktopDir);
        const QString normalizedDestination = normalizeDesktopPath(destinationPath);

        bool singleOk = false;
        if (preferShortcut) {
            singleOk = ListViewFileOpsHelper::createShellShortcut(sourcePath, destinationPath);
        } else if (copyMode) {
            singleOk = ListViewFileOpsHelper::copyPathToExactDestination(sourcePath, destinationPath);
        } else {
            singleOk = ListViewFileOpsHelper::movePathToExactDestination(sourcePath, destinationPath);
        }

        if (singleOk) {
            createdPaths.append(normalizedDestination);
            m_pendingCreatedItemBoxes[normalizedDestination] = targetBox;
            if (nextInsertIndex >= 0) {
                m_pendingCreatedItemInsertIndices[normalizedDestination] = nextInsertIndex++;
            }
        } else {
            ok = false;
        }
    }

    return ok && !createdPaths.isEmpty();
}

void ListView::reloadMappedFolderBox(LinBox* box)
{
    const ListViewSelectionSnapshot selectionSnapshot = ListViewSelectionSnapshot::capture(*this);
    loadMappedFolderItemsForBox(box);
    selectionSnapshot.restore(*this, false);
}

void ListView::clearMappedFolderBox(LinBox* box)
{
    clearMappedFolderItemsForBox(box);
}

QString ListView::dumpAllItemsJson() const
{
    return DesktopItemLoader::dumpDesktopRootStatesJson(m_itemStates);
}

QString ListView::dumpVisibleItemsJson() const
{
    QVector<DesktopRootItemState> visibleStates;
    for (const auto& state : m_itemStates) {
        if (shouldPaintDesktopModelState(state)) {
            visibleStates.push_back(state);
        }
    }
    return DesktopItemLoader::dumpDesktopRootStatesJson(visibleStates);
}

QString ListView::dumpHiddenItemsJson() const
{
    QVector<DesktopRootItemState> hiddenStates;
    for (const auto& state : m_itemStates) {
        if (!state.model.visible || !state.model.boxId.isEmpty()) {
            hiddenStates.push_back(state);
        }
    }
    return DesktopItemLoader::dumpDesktopRootStatesJson(hiddenStates);
}

void ListView::loadPersistentLayout() {
    const bool useConfigLayout = m_isLoadingPersistence || m_forceConfigLayout;
    {
        QSet<LinBox*> boxesToRefresh;

        if (useConfigLayout) {
            for (auto& state : m_itemStates) {
                if (!state.model.boxId.isEmpty()) {
                    continue;
                }

                const QString normalizedPath = normalizeDesktopPath(state.model.filePath);
                if (!m_savedIconPositions.contains(normalizedPath)) {
                    state.model.position = QPoint(0, 0);
                }
            }
        }

        for (int i = 0; i < m_itemStates.size();) {
            DesktopRootItemState& state = m_itemStates[i];
            const QString savedBoxId = state.model.boxId;
            if (!savedBoxId.isEmpty()) {
                if (LinBox* targetBox = BoxManager::instance()->findBoxById(savedBoxId)) {
                    DesktopRootItemState boxedState = state;
                    boxedState.model.boxId = savedBoxId;
                    boxedState.model.visible = true;
                    targetBox->addItemState(boxedState, false);
                    m_itemStates.removeAt(i);
                    markDesktopStateIndexDirty();
                    boxesToRefresh.insert(targetBox);
                    continue;
                }
            }

            const QPoint currentPos = state.model.position;
            const QString normalizedPath = normalizeDesktopPath(state.model.filePath);
            const bool hasPersistedPosition = !useConfigLayout || m_savedIconPositions.contains(normalizedPath);
            const bool hasValidPosition = hasPersistedPosition && (currentPos != QPoint(0, 0));

            if (!hasValidPosition) {
                QPoint pos = useConfigLayout ? generateItemPosition() : currentPos;
                if (pos == QPoint(0, 0)) {
                    pos = generateItemPosition();
                }
                state.model.position = pos;
                ++i;
                continue;
            }

            const QPoint savedPos = state.model.position;
            if (savedPos.x() <= 0 || savedPos.y() <= 0) {
                state.model.position = generateItemPosition();
            } else {
                state.model.position = savedPos;
            }
            ++i;
        }

        for (LinBox* box : boxesToRefresh) {
            if (!box) {
                continue;
            }
            box->refreshLayout();
            box->raise();
        }

        markAllDesktopRenderDataDirty();
        updateAllItemRenderData();
        rebuildSpatialIndex();
        update();
        return;
    }
}

void ListView::setPersistenceEnabled(bool enabled) {
    if (m_enablePersistence == enabled) return;
    m_enablePersistence = enabled;
    
    if (m_enablePersistence) {
        loadBoxes(false); // Don't let the file turn us back off!
        loadIconLayer();
    } else {
        m_hasLoadedConfigLayout = false;
        // Destroy all boxes and restore items to desktop
        for (auto box : BoxManager::instance()->allBoxes()) {
            if (!box) continue;
            const QVector<DesktopRootItemState> boxStates = box->itemStates();
            for (const DesktopRootItemState& boxedState : boxStates) {
                DesktopRootItemState restoredState = boxedState;
                resetStateForDesktopView(restoredState, m_iconWidth, m_iconHeight);
                m_itemStates.push_back(restoredState);
                markDesktopStateIndexDirty();
            }
            box->deleteLater();
        }
        BoxManager::instance()->clearBoxes();
        loadIconLayer();
    }
    
    // Always commit state change to file
    saveBoxes();

    if (!enabled) {
        FluentDialog* dlg = new FluentDialog(nullptr);
        dlg->setDialogTitle(tr("Desktop Organization"));
        dlg->setMessage(tr("Do you want to organize the current desktop icon layout now?"));
        dlg->addActionButton(tr("Organize Now"), FluentDialog::ActionType::Primary, [this]() {
            this->setPersistenceEnabled(true);
            this->onMenuOrganizeDesktop();
        });
        dlg->addActionButton(tr("Cancel"), FluentDialog::ActionType::Close);
        dlg->show();
        dlg->raise();
        dlg->activateWindow();
    }
}
