#include "desktop/ListViewSelectionSnapshot.h"

#include "desktop/ListView.h"

ListViewSelectionSnapshot ListViewSelectionSnapshot::capture(const ListView& view)
{
    ListViewSelectionSnapshot snapshot;
    snapshot.m_selectedPaths = view.selectedItemPaths();
    snapshot.m_focusedPath = view.focusedItemPath();
    snapshot.m_anchorPath = view.anchorItemPath();
    return snapshot;
}

void ListViewSelectionSnapshot::restore(ListView& view, bool expandSingle) const
{
    view.restoreSelectionStateByPaths(m_selectedPaths, m_focusedPath, m_anchorPath, expandSingle);
}
