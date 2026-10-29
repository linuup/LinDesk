#include "desktop/ListViewDragDropHelper.h"
#include "desktop/ListView.h"
#include "box/BoxManager.h"
#include "box/LinBox.h"
#include "desktop/DesktopItemRenderCacheHelper.h"
#include <QPainter>
#include <QLabel>
#include <QCursor>
#include <QDebug>
#include <QGuiApplication>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// Forward declarations of free helper functions defined in ListView.cpp
quint64 packGridCell(int gridX, int gridY);
QPoint pointToGridCell(const QPoint& pos, int marginLeft, int marginTop, int strideX, int strideY);
QPoint gridCellToPoint(int gridX, int gridY, int marginLeft, int marginTop, int strideX, int strideY);
int maxGridRowsForBounds(int layoutTop, int usableBottom, int itemHeight, int strideY);
int maxGridColsForBounds(int layoutLeft, int usableRight, int iconWidth, int strideX);
QPoint gridIndexToCell(int index, int maxRows);

QMap<QString, QPoint> ListViewDragDropHelper::resolveDesktopDragFinalPositions(
    const ListView& listView,
    const QStringList& draggedPaths,
    const QPoint& releasePos,
    const QHash<QString, QPoint>& previewPositions
) {
    QMap<QString, QPoint> result;
    QSet<QString> draggedPathSet;
    QVector<QPair<QString, const DesktopRootItemState*>> draggedStates;

    for (const QString& path : draggedPaths) {
        const QString normalizedPath = listView.normalizeDesktopItemPath(path);
        if (normalizedPath.isEmpty() || draggedPathSet.contains(normalizedPath)) {
            continue;
        }
        const DesktopRootItemState* state = listView.findDesktopStateByPath(normalizedPath);
        if (!state || !listView.shouldPaintDesktopModelState(*state)) {
            continue;
        }
        draggedPathSet.insert(normalizedPath);
        draggedStates.append(qMakePair(normalizedPath, state));
    }

    if (draggedStates.isEmpty()) {
        return result;
    }

    const int itemHeight = listView.desktopItemFootprintHeight();
    const QSize itemSize(listView.m_iconWidth, itemHeight);
    const int layoutLeft = listView.desktopLayoutLeft();
    const int layoutTop = listView.desktopLayoutTop();
    const int usableRight = listView.desktopUsableRight();
    const int usableBottom = listView.desktopUsableBottom();
    const int minX = layoutLeft;
    const int minY = layoutTop;
    const int maxX = qMax(minX, usableRight - listView.m_iconWidth + 1);
    const int maxY = qMax(minY, usableBottom - itemHeight + 1);

    QHash<QString, QPoint> ghostLocalPositions = previewPositions;
    if (ghostLocalPositions.isEmpty()) {
        for (const ListView::GhostInfo& ghost : listView.m_ghosts) {
            if (ghost.filePath.isEmpty()) {
                continue;
            }
            const QString normalizedPath = listView.normalizeDesktopItemPath(ghost.filePath);
            if (!normalizedPath.isEmpty()) {
                ghostLocalPositions.insert(normalizedPath, listView.localPosForGhost(ghost));
            }
        }
    }

    if (listView.m_iconShowRule.gridAlignment) {
        const int strideX = qMax(1, listView.m_iconWidth + listView.m_colSpacing);
        const int strideY = qMax(1, itemHeight + listView.m_rowSpacing);
        const int maxRows = qMax(1, maxGridRowsForBounds(layoutTop, usableBottom, itemHeight, strideY));
        const int maxCols = qMax(1, maxGridColsForBounds(layoutLeft, usableRight, listView.m_iconWidth, strideX));

        QSet<quint64> occupiedSlots = listView.collectDesktopIconOccupiedCells(draggedPaths);
        QVector<QPoint> relativeCells;
        relativeCells.reserve(draggedStates.size());
        QPoint primaryGridCell(-1, -1);

        const QString primaryPath = draggedStates.first().first;
        if (ghostLocalPositions.contains(primaryPath)) {
            primaryGridCell = pointToGridCell(ghostLocalPositions.value(primaryPath), layoutLeft, layoutTop, strideX, strideY);
        }

        if (primaryGridCell.x() < 0 || primaryGridCell.y() < 0) {
            primaryGridCell = pointToGridCell(releasePos, layoutLeft, layoutTop, strideX, strideY);
        }

        for (const auto& pair : draggedStates) {
            if (ghostLocalPositions.contains(pair.first)) {
                const QPoint cell = pointToGridCell(ghostLocalPositions.value(pair.first), layoutLeft, layoutTop, strideX, strideY);
                relativeCells.append(cell - primaryGridCell);
            } else {
                relativeCells.append(QPoint(0, 0));
            }
        }

        const QPoint targetTopLeftCell = resolveNearestGridAreaForDrag(listView, relativeCells, primaryGridCell, occupiedSlots);
        if (targetTopLeftCell.x() >= 0 && targetTopLeftCell.y() >= 0) {
            for (int i = 0; i < draggedStates.size(); ++i) {
                const QPoint cell = targetTopLeftCell + relativeCells[i];
                result.insert(draggedStates[i].first, gridCellToPoint(cell.x(), cell.y(), layoutLeft, layoutTop, strideX, strideY));
            }
        } else {
            const int visibleCapacity = qMax(1, maxRows * maxCols);
            for (const auto& pair : draggedStates) {
                const QPoint overflowCell = gridIndexToCell(occupiedSlots.size() % visibleCapacity, maxRows);
                const QPoint pos = gridCellToPoint(overflowCell.x(), overflowCell.y(), layoutLeft, layoutTop, strideX, strideY);
                result.insert(pair.first, pos);
                occupiedSlots.insert(packGridCell(overflowCell.x(), overflowCell.y()));
            }
        }
    } else {
        QPoint primaryPos = releasePos;
        const QString primaryPath = draggedStates.first().first;
        if (ghostLocalPositions.contains(primaryPath)) {
            primaryPos = ghostLocalPositions.value(primaryPath);
        }

        const QPoint clampedPrimary(qBound(minX, primaryPos.x(), maxX), qBound(minY, primaryPos.y(), maxY));
        const QPoint delta = clampedPrimary - primaryPos;

        for (const auto& pair : draggedStates) {
            QPoint rawPos = releasePos;
            if (ghostLocalPositions.contains(pair.first)) {
                rawPos = ghostLocalPositions.value(pair.first);
            }
            const QPoint targetPos = rawPos + delta;
            const QPoint clampedPos(qBound(minX, targetPos.x(), maxX), qBound(minY, targetPos.y(), maxY));
            result.insert(pair.first, clampedPos);
        }
    }

    return result;
}

QPoint ListViewDragDropHelper::resolveNearestGridAreaForDrag(
    const ListView& listView,
    const QVector<QPoint>& relativeCells,
    const QPoint& preferredTopLeftCell,
    const QSet<quint64>& occupiedSlots
) {
    if (relativeCells.isEmpty()) {
        return QPoint(-1, -1);
    }

    const int itemHeight = listView.desktopItemFootprintHeight();
    const int strideX = qMax(1, listView.m_iconWidth + listView.m_colSpacing);
    const int strideY = qMax(1, itemHeight + listView.m_rowSpacing);
    const int layoutLeft = listView.desktopLayoutLeft();
    const int layoutTop = listView.desktopLayoutTop();
    const int maxRows = qMax(1, maxGridRowsForBounds(layoutTop, listView.desktopUsableBottom(), itemHeight, strideY));
    const int maxCols = qMax(1, maxGridColsForBounds(layoutLeft, listView.desktopUsableRight(), listView.m_iconWidth, strideX));

    int groupWidth = 1;
    int groupHeight = 1;
    QSet<quint64> uniqueRelativeSlots;
    QVector<QPoint> uniqueRelativeCells;
    uniqueRelativeCells.reserve(relativeCells.size());

    for (const QPoint& cell : relativeCells) {
        if (cell.x() < 0 || cell.y() < 0) {
            continue;
        }
        const quint64 key = packGridCell(cell.x(), cell.y());
        if (uniqueRelativeSlots.contains(key)) {
            continue;
        }
        uniqueRelativeSlots.insert(key);
        uniqueRelativeCells.append(cell);
        groupWidth = qMax(groupWidth, cell.x() + 1);
        groupHeight = qMax(groupHeight, cell.y() + 1);
    }

    if (uniqueRelativeCells.isEmpty() || groupWidth > maxCols || groupHeight > maxRows) {
        return QPoint(-1, -1);
    }

    const QPoint clampedPreferred(qBound(0, preferredTopLeftCell.x(), maxCols - groupWidth),
                                  qBound(0, preferredTopLeftCell.y(), maxRows - groupHeight));

    auto candidateIsFree = [&](const QPoint& topLeftCell) {
        if (topLeftCell.x() < 0 || topLeftCell.y() < 0 ||
            topLeftCell.x() + groupWidth > maxCols ||
            topLeftCell.y() + groupHeight > maxRows) {
            return false;
        }

        for (const QPoint& relativeCell : uniqueRelativeCells) {
            const QPoint cell = topLeftCell + relativeCell;
            if (occupiedSlots.contains(packGridCell(cell.x(), cell.y()))) {
                return false;
            }
        }
        return true;
    };

    if (candidateIsFree(clampedPreferred)) {
        return clampedPreferred;
    }

    const int maxRadius = qMax(maxCols, maxRows);
    for (int radius = 1; radius <= maxRadius; ++radius) {
        QVector<QPair<int, QPoint>> candidates;
        const int minX = qMax(0, clampedPreferred.x() - radius);
        const int maxX = qMin(maxCols - groupWidth, clampedPreferred.x() + radius);
        const int minY = qMax(0, clampedPreferred.y() - radius);
        const int maxY = qMin(maxRows - groupHeight, clampedPreferred.y() + radius);

        for (int x = minX; x <= maxX; ++x) {
            for (int y = minY; y <= maxY; ++y) {
                if (qMax(qAbs(x - clampedPreferred.x()), qAbs(y - clampedPreferred.y())) != radius) {
                    continue;
                }
                const QPoint candidate(x, y);
                if (!candidateIsFree(candidate)) {
                    continue;
                }
                const int dx = x - clampedPreferred.x();
                const int dy = y - clampedPreferred.y();
                candidates.append(qMakePair(dx * dx + dy * dy, candidate));
            }
        }

        if (!candidates.isEmpty()) {
            std::sort(candidates.begin(), candidates.end(), [maxRows](const QPair<int, QPoint>& a, const QPair<int, QPoint>& b) {
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
}

QPoint ListViewDragDropHelper::resolveNearestGridCreatePosition(
    const ListView& listView,
    const QPoint& requestedPos,
    const QSet<quint64>& occupiedSlots
) {
    const int itemHeight = listView.desktopItemFootprintHeight();
    const int strideX = qMax(1, listView.m_iconWidth + listView.m_colSpacing);
    const int strideY = qMax(1, itemHeight + listView.m_rowSpacing);
    const int layoutLeft = listView.desktopLayoutLeft();
    const int layoutTop = listView.desktopLayoutTop();
    const int maxRows = qMax(1, maxGridRowsForBounds(layoutTop, listView.desktopUsableBottom(), itemHeight, strideY));
    const int maxCols = qMax(1, maxGridColsForBounds(layoutLeft, listView.desktopUsableRight(), listView.m_iconWidth, strideX));
    const int visibleCapacity = qMax(1, maxRows * maxCols);
    const QPoint preferredCell = pointToGridCell(requestedPos, layoutLeft, layoutTop, strideX, strideY);
    const QPoint clampedPreferred(qBound(0, preferredCell.x(), maxCols - 1),
                                  qBound(0, preferredCell.y(), maxRows - 1));

    if (occupiedSlots.size() < visibleCapacity) {
        QVector<QPair<int, QPoint>> candidates;
        const int maxRadius = qMax(maxCols, maxRows);
        for (int radius = 0; radius <= maxRadius; ++radius) {
            candidates.clear();
            const int minX = qMax(0, clampedPreferred.x() - radius);
            const int maxX = qMin(maxCols - 1, clampedPreferred.x() + radius);
            const int minY = qMax(0, clampedPreferred.y() - radius);
            const int maxY = qMin(maxRows - 1, clampedPreferred.y() + radius);

            for (int x = minX; x <= maxX; ++x) {
                for (int y = minY; y <= maxY; ++y) {
                    if (qMax(qAbs(x - clampedPreferred.x()), qAbs(y - clampedPreferred.y())) != radius) {
                        continue;
                    }
                    if (occupiedSlots.contains(packGridCell(x, y))) {
                        continue;
                    }
                    const int dx = x - clampedPreferred.x();
                    const int dy = y - clampedPreferred.y();
                    candidates.append(qMakePair(dx * dx + dy * dy, QPoint(x, y)));
                }
            }

            if (!candidates.isEmpty()) {
                std::sort(candidates.begin(), candidates.end(), [maxRows](const QPair<int, QPoint>& a, const QPair<int, QPoint>& b) {
                    if (a.first != b.first) {
                        return a.first < b.first;
                    }
                    const int indexA = a.second.x() * maxRows + a.second.y();
                    const int indexB = b.second.x() * maxRows + b.second.y();
                    return indexA < indexB;
                });
                const QPoint cell = candidates.first().second;
                return gridCellToPoint(cell.x(), cell.y(), layoutLeft, layoutTop, strideX, strideY);
            }
        }
    }

    const QPoint overflowCell = gridIndexToCell(occupiedSlots.size() % visibleCapacity, maxRows);
    return gridCellToPoint(overflowCell.x(), overflowCell.y(), layoutLeft, layoutTop, strideX, strideY);
}

QPoint ListViewDragDropHelper::resolveNearestFreeCreatePosition(
    const ListView& listView,
    const QPoint& requestedPos
) {
    if (listView.m_iconShowRule.gridAlignment) {
        QSet<quint64> occupiedSlots = listView.collectDesktopIconOccupiedCells();
        for (const auto& rect : listView.desktopBlockedOccupiedRects()) {
            const int strideX = qMax(1, listView.m_iconWidth + listView.m_colSpacing);
            const int strideY = qMax(1, listView.desktopItemFootprintHeight() + listView.m_rowSpacing);
            const int layoutLeft = listView.desktopLayoutLeft();
            const int layoutTop = listView.desktopLayoutTop();
            const int startX = qMax(0, (rect.left() - layoutLeft) / strideX);
            const int endX = (rect.right() - layoutLeft) / strideX;
            const int startY = qMax(0, (rect.top() - layoutTop) / strideY);
            const int endY = (rect.bottom() - layoutTop) / strideY;
            for (int x = startX; x <= endX; ++x) {
                for (int y = startY; y <= endY; ++y) {
                    occupiedSlots.insert(packGridCell(x, y));
                }
            }
        }
        return resolveNearestGridCreatePosition(listView, requestedPos, occupiedSlots);
    }

    const int itemHeight = listView.desktopItemFootprintHeight();
    const QSize itemSize(listView.m_iconWidth, itemHeight);
    const int layoutLeft = listView.desktopLayoutLeft();
    const int layoutTop = listView.desktopLayoutTop();
    const int usableRight = listView.desktopUsableRight();
    const int usableBottom = listView.desktopUsableBottom();
    const int minX = layoutLeft;
    const int minY = layoutTop;
    const int maxX = qMax(minX, usableRight - listView.m_iconWidth + 1);
    const int maxY = qMax(minY, usableBottom - itemHeight + 1);

    const QPoint clampedPreferred(qBound(minX, requestedPos.x(), maxX), qBound(minY, requestedPos.y(), maxY));

    auto candidateIsFree = [&](const QPoint& pos) {
        const QRect rect(pos, itemSize);
        if (listView.rectIntersectsDesktopBlockedArea(rect)) {
            return false;
        }
        for (const DesktopRootItemState& state : listView.m_itemStates) {
            if (listView.shouldPaintDesktopModelState(state)) {
                if (QRect(state.model.position, itemSize).intersects(rect)) {
                    return false;
                }
            }
        }
        return true;
    };

    if (candidateIsFree(clampedPreferred)) {
        return clampedPreferred;
    }

    const int step = 15;
    const int maxRadius = qMax(maxX - minX, maxY - minY);
    for (int radius = step; radius <= maxRadius; radius += step) {
        QVector<QPair<int, QPoint>> candidates;
        const int minXBound = qMax(minX, clampedPreferred.x() - radius);
        const int maxXBound = qMin(maxX, clampedPreferred.x() + radius);
        const int minYBound = qMax(minY, clampedPreferred.y() - radius);
        const int maxYBound = qMin(maxY, clampedPreferred.y() + radius);

        for (int x = minXBound; x <= maxXBound; x += step) {
            for (int y = minYBound; y <= maxYBound; y += step) {
                if (qMax(qAbs(x - clampedPreferred.x()), qAbs(y - clampedPreferred.y())) != radius) {
                    continue;
                }
                const QPoint candidate(x, y);
                if (!candidateIsFree(candidate)) {
                    continue;
                }
                const int dx = x - clampedPreferred.x();
                const int dy = y - clampedPreferred.y();
                candidates.append(qMakePair(dx * dx + dy * dy, candidate));
            }
        }

        if (!candidates.isEmpty()) {
            std::sort(candidates.begin(), candidates.end(), [](const QPair<int, QPoint>& a, const QPair<int, QPoint>& b) {
                return a.first < b.first;
            });
            return candidates.first().second;
        }
    }

    return clampedPreferred;
}

void ListViewDragDropHelper::createDragGhosts(ListView& listView, const QPoint& clickPos) {
    listView.clearDragGhosts();

    QVector<QPair<QString, DesktopRootItemState*>> draggedItems;
    QRect combinedRect;

    for (const QString& desktopPath : listView.selectedDesktopPaths()) {
        bool alreadyTracked = false;
        for (const auto& item : draggedItems) {
            if (item.first == desktopPath) {
                alreadyTracked = true;
                break;
            }
        }
        if (alreadyTracked) {
            continue;
        }

        DesktopRootItemState* state = listView.findDesktopStateByPath(desktopPath);
        if (!state) {
            continue;
        }

        const DesktopRootItemRenderData& renderData = state->model.renderData;
        if (!renderData.itemRect.isValid() || renderData.itemRect.isEmpty()) {
            continue;
        }

        draggedItems.append({ desktopPath, state });
        combinedRect = combinedRect.isNull() ? renderData.itemRect : combinedRect.united(renderData.itemRect);
    }

    if (draggedItems.isEmpty() || !combinedRect.isValid() || combinedRect.isEmpty()) {
        return;
    }

    QPixmap pixmap(combinedRect.size());
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    for (const auto& item : draggedItems) {
        const DesktopRootItemRenderData& renderData = item.second->model.renderData;
        painter.save();
        painter.translate(renderData.itemRect.topLeft() - combinedRect.topLeft());
        paintDesktopRootItemGhost(painter, renderData);
        painter.restore();
    }
    painter.end();

    QLabel* ghostWidget = new QLabel(nullptr);
    ghostWidget->setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowTransparentForInput);
    ghostWidget->setAttribute(Qt::WA_TranslucentBackground);
    ghostWidget->setAttribute(Qt::WA_NoSystemBackground);
    ghostWidget->setAttribute(Qt::WA_TransparentForMouseEvents);
    ghostWidget->setStyleSheet("background: transparent;");

    ghostWidget->setPixmap(pixmap);
    ghostWidget->setFixedSize(combinedRect.size());

    const QPoint globalCombinedTopLeft = listView.mapToGlobal(combinedRect.topLeft());
    const QPoint globalClickPos = listView.mapToGlobal(clickPos);
    const QPoint combinedOffset = globalCombinedTopLeft - globalClickPos;

    ghostWidget->move(QCursor::pos() + combinedOffset);
    ghostWidget->show();
#ifdef Q_OS_WIN
    if (HWND hwnd = reinterpret_cast<HWND>(ghostWidget->winId())) {
        SetWindowPos(
            hwnd,
            HWND_NOTOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW
        );
    }
#endif

    listView.m_combinedGhostWidget = ghostWidget;
    listView.m_combinedGhostOffset = combinedOffset;
    listView.m_lastGhostMousePos = QCursor::pos();

    for (const auto& item : draggedItems) {
        const QPoint itemGlobalPos = listView.mapToGlobal(item.second->model.renderData.itemRect.topLeft());
        const QPoint itemOffset = itemGlobalPos - globalClickPos;
        listView.m_ghosts.append({ ghostWidget, item.first, itemOffset });
    }
}

void ListViewDragDropHelper::moveDesktopGhostsForPreview(ListView& listView, const QPoint& globalPos) {
    if (listView.m_ghosts.isEmpty()) {
        return;
    }

    listView.m_lastGhostMousePos = globalPos;
    if (listView.m_combinedGhostWidget) {
        listView.m_combinedGhostWidget->move(globalPos + listView.m_combinedGhostOffset);
    }
}
