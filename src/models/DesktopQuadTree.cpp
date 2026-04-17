#include "models/DesktopQuadTree.h"
#include "desktop/ListItem.h"

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

void DesktopQuadTree::rebuild(const QVector<ListItem*>& items)
{
    clear();
    if (!m_root) {
        return;
    }

    for (ListItem* item : items) {
        if (!item || !item->isVisible() || !item->boxId().isEmpty()) {
            continue;
        }

        const QRect rect = item->getChooseItemRect();
        if (rect.isEmpty() || !m_bounds.intersects(rect)) {
            continue;
        }

        insert(m_root.get(), Entry{ item, rect }, 0);
    }
}

QVector<ListItem*> DesktopQuadTree::query(const QRect& area) const
{
    QVector<ListItem*> results;
    if (!m_root || area.isEmpty()) {
        return results;
    }

    QHash<ListItem*, bool> seen;
    query(m_root.get(), area, results, seen);
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

void DesktopQuadTree::query(const Node* node, const QRect& area, QVector<ListItem*>& results, QHash<ListItem*, bool>& seen) const
{
    if (!node || !node->bounds.intersects(area)) {
        return;
    }

    for (const Entry& entry : node->entries) {
        if (entry.item && entry.rect.intersects(area) && !seen.contains(entry.item)) {
            seen.insert(entry.item, true);
            results.append(entry.item);
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

