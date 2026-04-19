#pragma once

#include <climits>
#include <QVector>

#include "desktop/ListView.h"
#include "desktop/ListItem.h"
#include "box/LinBox.h"

class LinBox;
class ListItem;

class ListViewSelectionHelper
{
public:
    static inline QVector<ListItem*> selectableItemsForHost(const ListView& view, LinBox* box)
    {
        QVector<ListItem*> items;
        if (box) {
            for (ListItem* item : box->items()) {
                if (item && item->isVisible()) {
                    items.append(item);
                }
            }
            return items;
        }

        for (ListItem* item : view.items()) {
            if (item && item->isVisible() && item->boxId().isEmpty()) {
                items.append(item);
            }
        }
        return items;
    }

    static inline QVector<ListItem*> buildRangeSelection(const QVector<ListItem*>& orderedItems, ListItem* anchor, ListItem* target)
    {
        QVector<ListItem*> rangeItems;
        if (!anchor || !target || orderedItems.isEmpty()) {
            return rangeItems;
        }

        const int anchorIndex = orderedItems.indexOf(anchor);
        const int targetIndex = orderedItems.indexOf(target);
        if (anchorIndex < 0 || targetIndex < 0) {
            rangeItems.append(target);
            return rangeItems;
        }

        const int from = qMin(anchorIndex, targetIndex);
        const int to = qMax(anchorIndex, targetIndex);
        rangeItems.reserve(to - from + 1);
        for (int i = from; i <= to; ++i) {
            rangeItems.append(orderedItems[i]);
        }
        return rangeItems;
    }

    static inline ListItem* findDirectionalFocusCandidate(
        const QVector<ListItem*>& orderedItems,
        ListItem* focusedItem,
        ListView::FocusMoveDirection direction)
    {
        if (orderedItems.isEmpty()) {
            return nullptr;
        }
        if (!focusedItem) {
            return orderedItems.first();
        }

        const QPoint currentCenter = focusedItem->geometry().center();
        qint64 bestDistanceSquared = LLONG_MAX;
        int bestPrimaryDistance = INT_MAX;
        ListItem* targetItem = nullptr;

        for (ListItem* candidate : orderedItems) {
            if (!candidate || candidate == focusedItem) {
                continue;
            }

            const QPoint candidateCenter = candidate->geometry().center();
            const int dx = candidateCenter.x() - currentCenter.x();
            const int dy = candidateCenter.y() - currentCenter.y();

            int primaryDistance = INT_MAX;
            switch (direction) {
            case ListView::FocusMoveDirection::Left:
                if (dx >= 0) continue;
                primaryDistance = -dx;
                break;
            case ListView::FocusMoveDirection::Right:
                if (dx <= 0) continue;
                primaryDistance = dx;
                break;
            case ListView::FocusMoveDirection::Up:
                if (dy >= 0) continue;
                primaryDistance = -dy;
                break;
            case ListView::FocusMoveDirection::Down:
                if (dy <= 0) continue;
                primaryDistance = dy;
                break;
            }

            const qint64 distanceSquared = static_cast<qint64>(dx) * dx + static_cast<qint64>(dy) * dy;
            if (distanceSquared < bestDistanceSquared ||
                (distanceSquared == bestDistanceSquared && primaryDistance < bestPrimaryDistance)) {
                bestDistanceSquared = distanceSquared;
                bestPrimaryDistance = primaryDistance;
                targetItem = candidate;
            }
        }

        return targetItem;
    }
};
