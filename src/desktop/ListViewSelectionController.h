#pragma once

#include <QPointer>
#include <QStringList>
#include <QVector>

#include "desktop/ListView.h"
#include "desktop/ListItem.h"
#include "desktop/ListViewSelectionHelper.h"

class LinBox;
class ListItem;
class ListView;

class ListViewSelectionController
{
public:
    static inline void clearSelection(ListView& view)
    {
        view.clearCheckedItem();
        view.setFocusedItem(nullptr);
        view.m_anchorItem = nullptr;
        view.clearCutMarks();
        view.resetRenameClickState();
    }

    static inline void applySelection(ListView& view, const QVector<ListItem*>& items, ListItem* focusedItem, bool expandSingle)
    {
        view.clearCheckedItem();

        QVector<ListItem*> filteredItems;
        filteredItems.reserve(items.size());
        for (ListItem* item : items) {
            if (!item) {
                continue;
            }
            if (!filteredItems.contains(item)) {
                filteredItems.append(item);
            }
        }

        for (ListItem* item : filteredItems) {
            view.setItemFullNameExpanded(item, false);
            item->checkSelf();
        }

        if (expandSingle && filteredItems.size() == 1 && filteredItems.first()) {
            view.setItemFullNameExpanded(filteredItems.first(), true);
        }

        view.m_selectedItems = filteredItems;
        view.setFocusedItem(focusedItem);
        if (!view.m_anchorItem && focusedItem) {
            view.m_anchorItem = focusedItem;
        }
    }

    static inline void setFocus(ListView& view, ListItem* item)
    {
        view.setFocusedItem(item);
    }

    static inline void toggleSelection(ListView& view, ListItem* item)
    {
        if (!item) {
            return;
        }

        if (view.m_selectedItems.contains(item)) {
            view.m_selectedItems.removeOne(item);
            view.clearItemExpandedState(item);
            item->unCheckSelf();
        } else {
            view.m_selectedItems.append(item);
            view.setItemFullNameExpanded(item, false);
            item->checkSelf();
        }
        view.setFocusedItem(item);
    }

    static inline void selectRange(ListView& view, ListItem* item, LinBox* box)
    {
        if (!item) {
            return;
        }

        QVector<ListItem*> orderedItems = ListViewSelectionHelper::selectableItemsForHost(view, box);
        if (orderedItems.isEmpty()) {
            return;
        }

        ListItem* anchor = view.m_anchorItem ? view.m_anchorItem.data() : item;
        int anchorIndex = orderedItems.indexOf(anchor);
        int itemIndex = orderedItems.indexOf(item);
        if (anchorIndex < 0 || itemIndex < 0) {
            applySelection(view, { item }, item, true);
            view.m_anchorItem = item;
            return;
        }

        QVector<ListItem*> rangeItems = ListViewSelectionHelper::buildRangeSelection(orderedItems, anchor, item);
        applySelection(view, rangeItems, item, false);
    }

    static inline void clearCutMarks(ListView& view)
    {
        for (ListItem* item : view.m_allItems) {
            if (!item) {
                continue;
            }
            item->setProperty("cutMarked", false);
            item->update();
        }
    }

    static inline void applyCutMarks(ListView& view, const QVector<ListItem*>& items)
    {
        clearCutMarks(view);
        for (ListItem* item : items) {
            if (!item) {
                continue;
            }
            item->setProperty("cutMarked", true);
            item->update();
        }
    }
};
