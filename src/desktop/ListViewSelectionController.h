#pragma once
#include <QPoint>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <QRect>
#include <QRegion>
#include "desktop/DesktopRootItemState.h"
#include "desktop/DesktopRootItemModel.h"

#include "desktop/ListView.h"

class LinBox;

class ListViewSelectionController {
public:
    // Selection state management
    static void clearSelectionState(ListView& view);
    static void setSelectedItemPaths(ListView& view, const QStringList& filePaths, const QString& focusedPath = QString(), bool expandSingle = false);
    static void setSelectedItems(ListView& view, const QStringList& filePaths, bool expandSingle = false);
    static void applySelectionState(ListView& view, const QStringList& filePaths, const QString& focusedPath = QString(), bool expandSingle = false);
    static void toggleItemSelection(ListView& view, const QString& filePath);
    static void selectRangeToPath(ListView& view, const QString& filePath, LinBox* box);

    // Queries
    static bool isDesktopPathSelected(const ListView& view, const QString& filePath);
    static QVector<DesktopRootItemModel> selectedDesktopModels(const ListView& view);
    static QStringList selectedDesktopPaths(const ListView& view);
    static QStringList selectedBoxPaths(const ListView& view);
    static QStringList selectableDesktopPaths(const ListView& view);
    static QStringList selectableBoxPaths(const ListView& view, LinBox* box);

    // Helper selection setters
    static void setDesktopItemSelected(ListView& view, DesktopRootItemState* state, const QString& normalizedPath, bool selected, QRegion* dirtyRegion = nullptr);
    static void setDesktopItemSelected(ListView& view, const QString& filePath, bool selected, QRegion* dirtyRegion = nullptr);
    static void clearDesktopSelection(ListView& view, QRegion* dirtyRegion = nullptr);
    static void applyDesktopSelection(ListView& view, const QStringList& filePaths, const QString& focusedPath, bool expandSingle);
    static QRect desktopSelectionRectForItem(const ListView& view, const DesktopRootItemState& state);

    // Background selection (marquee select)
    static void beginBackgroundSelection(ListView& view, const QPoint& startPos);
    static bool updateBackgroundSelection(ListView& view, const QPoint& currentPos, QRect* selectionRect = nullptr);
    static void endBackgroundSelection(ListView& view);
    static void flushBackgroundSelectionUpdate(ListView& view);
    static void resetBackgroundSelectionCandidateCache(ListView& view);
    static void updateBackgroundSelectionCandidateCache(ListView& view, const QRegion& uncoveredSelectionRegion);
    static void rebuildBackgroundSelectionOcclusionRegion(ListView& view);
    static int currentBackgroundSelectionThrottleMs(const ListView& view);
    static void updateBackgroundSelectionThrottle(ListView& view, qint64 refreshElapsedMs, const QRect& selectionRect);

    // Focus management
    static void setFocusedItemPath(ListView& view, const QString& filePath);
    static void clearFocusedItem(ListView& view);
    static void moveFocusInHost(ListView& view, LinBox* box, ListView::FocusMoveDirection direction, bool extendSelection, bool focusOnly);
    static QString focusedItemPath(const ListView& view);
    static QString anchorItemPath(const ListView& view);
    static QString findDirectionalDesktopFocusCandidate(const ListView& view, const QString& focusedPath, ListView::FocusMoveDirection direction);
    static QString findDirectionalBoxFocusCandidate(const ListView& view, LinBox* box, const QString& focusedPath, ListView::FocusMoveDirection direction);

    // Snapshot restoration helpers
    static QStringList selectedItemPaths(const ListView& view);
    static void restoreSelectionStateByPaths(ListView& view, const QStringList& selectedPaths, const QString& focusedPath, const QString& anchorPath, bool expandSingle = false);
};
