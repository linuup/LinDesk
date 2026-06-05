#include "desktop/ListViewSelectionHelper.h"
#include "desktop/ListView.h"
#include <QPoint>

QString ListViewSelectionHelper::findDirectionalFocusCandidateInList(
    const ListView* view,
    const QVector<DesktopRootItemState>& states,
    const QString& focusedPath,
    ListView::FocusMoveDirection direction)
{
    if (!view) {
        return QString();
    }

    const DesktopRootItemState* focusedState = nullptr;
    const QString normalizedFocused = view->normalizeDesktopItemPath(focusedPath);
    for (const auto& state : states) {
        if (view->normalizeDesktopItemPath(state.model.filePath) == normalizedFocused) {
            focusedState = &state;
            break;
        }
    }
    if (!focusedState) {
        for (const auto& state : states) {
            const QString normalized = view->normalizeDesktopItemPath(state.model.filePath);
            if (!normalized.isEmpty()) return normalized;
        }
        return QString();
    }

    const QPoint currentCenter = focusedState->model.renderData.selectionContentRect.center();
    qint64 bestDistanceSquared = LLONG_MAX;
    int bestPrimaryDistance = INT_MAX;
    QString candidatePath;

    for (const auto& state : states) {
        const QString normalizedPath = view->normalizeDesktopItemPath(state.model.filePath);
        if (normalizedPath.isEmpty() || normalizedPath == normalizedFocused) {
            continue;
        }

        const QPoint candidateCenter = state.model.renderData.selectionContentRect.center();
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
            candidatePath = normalizedPath;
        }
    }

    return candidatePath;
}
