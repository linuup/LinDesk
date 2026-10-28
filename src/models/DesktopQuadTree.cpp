#include "models/DesktopQuadTree.h"
#include <algorithm>

DesktopQuadTree::DesktopQuadTree(const QRect& bounds, int maxDepth, int maxItemsPerNode)
    : m_bounds(bounds),
      m_maxDepth(maxDepth),
      m_maxItemsPerNode(maxItemsPerNode)
{
    if (!m_bounds.isEmpty()) {
        m_root = std::make_unique<Node>(m_bounds);
    }
}

DesktopQuadTree::~DesktopQuadTree() = default;

void DesktopQuadTree::setBounds(const QRect& bounds)
{
    m_bounds = bounds;
    clear();
}

void DesktopQuadTree::clear()
{
    if (m_bounds.isEmpty()) {
        m_root.reset();
        return;
    }
    m_root = std::make_unique<Node>(m_bounds);
}

void DesktopQuadTree::rebuild(const QVector<DesktopRootItemState*>& states)
{
    clear();
    if (!m_root) {
        return;
    }

    for (int i = 0; i < states.size(); ++i) {
        DesktopRootItemState* state = states[i];
        if (!state || !state->model.visible) {
            continue;
        }

        QRect rect = state->model.renderData.itemRect;
        if (!state->model.renderData.selectionContentRect.isEmpty()) {
            rect = rect.isEmpty() ? state->model.renderData.selectionContentRect
                                  : rect.united(state->model.renderData.selectionContentRect);
        }
        if (rect.isEmpty() || !m_bounds.intersects(rect)) {
            continue;
        }

        insert(m_root.get(), Entry{ state, rect, i }, 0);
    }
}

QVector<DesktopRootItemState*> DesktopQuadTree::query(const QRect& area) const
{
    QVector<DesktopRootItemState*> results;
    if (!m_root || area.isEmpty()) {
        return results;
    }

    QHash<DesktopRootItemState*, bool> seen;
    query(m_root.get(), area, results, seen);
    return results;
}

QVector<DesktopRootItemState*> DesktopQuadTree::queryPoint(const QPoint& pos) const
{
    if (!m_root || !m_bounds.contains(pos)) {
        return {};
    }

    QVector<Entry> matchingEntries;
    const Node* current = m_root.get();
    while (current) {
        for (const Entry& entry : current->entries) {
            if (entry.state && entry.rect.contains(pos)) {
                matchingEntries.append(entry);
            }
        }
        if (current->isLeaf()) {
            break;
        }
        const int idx = childIndexForPoint(pos, current->bounds);
        if (idx == 0) current = current->topLeft.get();
        else if (idx == 1) current = current->topRight.get();
        else if (idx == 2) current = current->bottomLeft.get();
        else if (idx == 3) current = current->bottomRight.get();
        else break;
    }

    std::sort(matchingEntries.begin(), matchingEntries.end(), [](const Entry& a, const Entry& b) {
        return a.zOrder > b.zOrder;
    });

    QVector<DesktopRootItemState*> results;
    results.reserve(matchingEntries.size());
    for (const Entry& e : matchingEntries) {
        results.append(e.state);
    }
    return results;
}

QRect DesktopQuadTree::childBounds(const QRect& parent, int index) const
{
    const int halfWidth = parent.width() / 2;
    const int halfHeight = parent.height() / 2;

    switch (index) {
    case 0:
        return QRect(parent.left(), parent.top(), halfWidth, halfHeight);
    case 1:
        return QRect(parent.left() + halfWidth, parent.top(), parent.width() - halfWidth, halfHeight);
    case 2:
        return QRect(parent.left(), parent.top() + halfHeight, halfWidth, parent.height() - halfHeight);
    case 3:
        return QRect(parent.left() + halfWidth, parent.top() + halfHeight, parent.width() - halfWidth, parent.height() - halfHeight);
    default:
        return QRect();
    }
}

int DesktopQuadTree::childIndexForRect(const QRect& rect, const QRect& parent) const
{
    for (int i = 0; i < 4; ++i) {
        const QRect child = childBounds(parent, i);
        if (!child.isEmpty() && child.contains(rect)) {
            return i;
        }
    }
    return -1;
}

int DesktopQuadTree::childIndexForPoint(const QPoint& pos, const QRect& parent) const
{
    const int midX = parent.left() + parent.width() / 2;
    const int midY = parent.top() + parent.height() / 2;
    const bool top = (pos.y() < midY);
    const bool left = (pos.x() < midX);

    if (top && left) return 0;
    if (top && !left) return 1;
    if (!top && left) return 2;
    return 3;
}

void DesktopQuadTree::subdivide(Node* node)
{
    if (!node || !node->isLeaf()) {
        return;
    }

    node->topLeft = std::make_unique<Node>(childBounds(node->bounds, 0));
    node->topRight = std::make_unique<Node>(childBounds(node->bounds, 1));
    node->bottomLeft = std::make_unique<Node>(childBounds(node->bounds, 2));
    node->bottomRight = std::make_unique<Node>(childBounds(node->bounds, 3));
}

void DesktopQuadTree::insert(Node* node, const Entry& entry, int depth)
{
    if (!node) {
        return;
    }

    if (depth >= m_maxDepth || node->bounds.width() <= 1 || node->bounds.height() <= 1) {
        node->entries.append(entry);
        return;
    }

    if (node->isLeaf() && node->entries.size() >= m_maxItemsPerNode) {
        subdivide(node);

        const QVector<Entry> existingEntries = node->entries;
        node->entries.clear();
        for (const Entry& existingEntry : existingEntries) {
            insert(node, existingEntry, depth);
        }
    }

    if (!node->isLeaf()) {
        const int childIndex = childIndexForRect(entry.rect, node->bounds);
        if (childIndex == 0) {
            insert(node->topLeft.get(), entry, depth + 1);
            return;
        }
        if (childIndex == 1) {
            insert(node->topRight.get(), entry, depth + 1);
            return;
        }
        if (childIndex == 2) {
            insert(node->bottomLeft.get(), entry, depth + 1);
            return;
        }
        if (childIndex == 3) {
            insert(node->bottomRight.get(), entry, depth + 1);
            return;
        }
    }

    node->entries.append(entry);
}

void DesktopQuadTree::query(const Node* node, const QRect& area, QVector<DesktopRootItemState*>& results, QHash<DesktopRootItemState*, bool>& seen) const
{
    if (!node || !node->bounds.intersects(area)) {
        return;
    }

    for (const Entry& entry : node->entries) {
        if (entry.state && entry.rect.intersects(area) && !seen.contains(entry.state)) {
            seen.insert(entry.state, true);
            results.append(entry.state);
        }
    }

    if (node->isLeaf()) {
        return;
    }

    query(node->topLeft.get(), area, results, seen);
    query(node->topRight.get(), area, results, seen);
    query(node->bottomLeft.get(), area, results, seen);
    query(node->bottomRight.get(), area, results, seen);
}
