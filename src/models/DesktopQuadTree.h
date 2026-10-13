#pragma once

#include <QRect>
#include <QVector>
#include <QHash>
#include <memory>

#include "desktop/DesktopRootItemState.h"

class DesktopQuadTree
{
public:
    DesktopQuadTree(const QRect& bounds = QRect(), int maxDepth = 6, int maxItemsPerNode = 8);
    ~DesktopQuadTree();

    void setBounds(const QRect& bounds);
    void clear();
    void rebuild(const QVector<DesktopRootItemState*>& states);
    QVector<DesktopRootItemState*> query(const QRect& area) const;
    QVector<DesktopRootItemState*> queryPoint(const QPoint& pos) const;

private:
    struct Entry
    {
        DesktopRootItemState* state = nullptr;
        QRect rect;
        int zOrder = 0;
    };

    struct Node
    {
        explicit Node(const QRect& rect) : bounds(rect) {}

        QRect bounds;
        QVector<Entry> entries;
        std::unique_ptr<Node> topLeft;
        std::unique_ptr<Node> topRight;
        std::unique_ptr<Node> bottomLeft;
        std::unique_ptr<Node> bottomRight;

        bool isLeaf() const
        {
            return !topLeft && !topRight && !bottomLeft && !bottomRight;
        }
    };

    QRect childBounds(const QRect& parent, int index) const;
    int childIndexForRect(const QRect& rect, const QRect& parent) const;
    int childIndexForPoint(const QPoint& pos, const QRect& parent) const;
    void subdivide(Node* node);
    void insert(Node* node, const Entry& entry, int depth);
    void query(const Node* node, const QRect& area, QVector<DesktopRootItemState*>& results, QHash<DesktopRootItemState*, bool>& seen) const;

    QRect m_bounds;
    int m_maxDepth = 6;
    int m_maxItemsPerNode = 8;
    std::unique_ptr<Node> m_root;
};
