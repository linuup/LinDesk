#pragma once
#include <QPoint>
#include <QMap>
#include <QHash>
#include <QStringList>
#include <QSet>
#include <QVector>

class ListView;

class ListViewDragDropHelper {
public:
    static QMap<QString, QPoint> resolveDesktopDragFinalPositions(
        const ListView& listView,
        const QStringList& draggedPaths,
        const QPoint& releasePos,
        const QHash<QString, QPoint>& previewPositions
    );

    static QPoint resolveNearestGridAreaForDrag(
        const ListView& listView,
        const QVector<QPoint>& relativeCells,
        const QPoint& preferredTopLeftCell,
        const QSet<quint64>& occupiedSlots
    );

    static QPoint resolveNearestGridCreatePosition(
        const ListView& listView,
        const QPoint& requestedPos,
        const QSet<quint64>& occupiedSlots
    );

    static QPoint resolveNearestFreeCreatePosition(
        const ListView& listView,
        const QPoint& requestedPos
    );

    static void createDragGhosts(ListView& listView, const QPoint& clickPos);
    static void moveDesktopGhostsForPreview(ListView& listView, const QPoint& globalPos);
};
