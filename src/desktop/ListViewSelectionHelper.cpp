#include "desktop/ListViewSelectionHelper.h"
#include "desktop/ListView.h"
#include <QPoint>
#include <QRect>
#include <climits>
#include <cmath>

namespace {

QRect itemLayoutRect(const DesktopRootItemState& state)
{
    int w = state.model.baseSize.width();
    int h = state.model.baseSize.height();
    if (w <= 0 || h <= 0) {
        w = state.model.effectiveSize.width();
        h = state.model.effectiveSize.height();
    }
    if (w <= 0) w = 76;
    if (h <= 0) h = 78;
    return QRect(state.model.position, QSize(w, h));
}

} // namespace

QString ListViewSelectionHelper::findDirectionalFocusCandidateInList(
    const ListView* view,
    const QVector<DesktopRootItemState>& states,
    const QString& focusedPath,
    ListView::FocusMoveDirection direction)
{
    if (!view || states.isEmpty()) {
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

    // If nothing is currently focused, select the top-leftmost visible item
    if (!focusedState) {
        const DesktopRootItemState* bestFirst = nullptr;
        qint64 bestScore = LLONG_MAX;
        for (const auto& state : states) {
            const QString normalized = view->normalizeDesktopItemPath(state.model.filePath);
            if (normalized.isEmpty()) {
                continue;
            }
            const QRect r = itemLayoutRect(state);
            const qint64 score = static_cast<qint64>(r.left()) * 10 + r.top();
            if (score < bestScore) {
                bestScore = score;
                bestFirst = &state;
            }
        }
        if (bestFirst) {
            return view->normalizeDesktopItemPath(bestFirst->model.filePath);
        }
        return QString();
    }

    const QRect currentRect = itemLayoutRect(*focusedState);
    const QPoint currentCenter = currentRect.center();

    QString bestTier1Path;
    double bestTier1Score = 1e18;

    QString bestTier2Path;
    double bestTier2Score = 1e18;

    constexpr int kHysteresis = 4;

    for (const auto& state : states) {
        const QString normalizedPath = view->normalizeDesktopItemPath(state.model.filePath);
        if (normalizedPath.isEmpty() || normalizedPath == normalizedFocused) {
            continue;
        }

        const QRect candRect = itemLayoutRect(state);
        const QPoint candCenter = candRect.center();

        double primaryDist = 0.0;
        double secondaryDist = 0.0;
        bool inBeam = false;

        switch (direction) {
        case ListView::FocusMoveDirection::Right: {
            if (candRect.left() <= currentRect.left() + kHysteresis) {
                continue;
            }
            primaryDist = candRect.left() - currentRect.left();
            const int overlapY = qMax(0, qMin(currentRect.bottom(), candRect.bottom()) - qMax(currentRect.top(), candRect.top()));
            if (overlapY > 0) {
                inBeam = true;
                secondaryDist = qAbs(candCenter.y() - currentCenter.y());
            } else {
                const int gapY = (candRect.top() > currentRect.bottom())
                    ? (candRect.top() - currentRect.bottom())
                    : (currentRect.top() - candRect.bottom());
                secondaryDist = gapY + qAbs(candCenter.y() - currentCenter.y()) * 0.5;
            }

            if (!inBeam && secondaryDist > primaryDist * 1.5) {
                continue;
            }
            break;
        }

        case ListView::FocusMoveDirection::Left: {
            if (candRect.left() >= currentRect.left() - kHysteresis) {
                continue;
            }
            primaryDist = currentRect.left() - candRect.left();
            const int overlapY = qMax(0, qMin(currentRect.bottom(), candRect.bottom()) - qMax(currentRect.top(), candRect.top()));
            if (overlapY > 0) {
                inBeam = true;
                secondaryDist = qAbs(candCenter.y() - currentCenter.y());
            } else {
                const int gapY = (candRect.top() > currentRect.bottom())
                    ? (candRect.top() - currentRect.bottom())
                    : (currentRect.top() - candRect.bottom());
                secondaryDist = gapY + qAbs(candCenter.y() - currentCenter.y()) * 0.5;
            }

            if (!inBeam && secondaryDist > primaryDist * 1.5) {
                continue;
            }
            break;
        }

        case ListView::FocusMoveDirection::Down: {
            if (candRect.top() <= currentRect.top() + kHysteresis) {
                continue;
            }
            primaryDist = candRect.top() - currentRect.top();
            const int overlapX = qMax(0, qMin(currentRect.right(), candRect.right()) - qMax(currentRect.left(), candRect.left()));
            if (overlapX > 0) {
                inBeam = true;
                secondaryDist = qAbs(candCenter.x() - currentCenter.x());
            } else {
                const int gapX = (candRect.left() > currentRect.right())
                    ? (candRect.left() - currentRect.right())
                    : (currentRect.left() - candRect.right());
                secondaryDist = gapX + qAbs(candCenter.x() - currentCenter.x()) * 0.5;
            }

            if (!inBeam && secondaryDist > primaryDist * 1.5) {
                continue;
            }
            break;
        }

        case ListView::FocusMoveDirection::Up: {
            if (candRect.top() >= currentRect.top() - kHysteresis) {
                continue;
            }
            primaryDist = currentRect.top() - candRect.top();
            const int overlapX = qMax(0, qMin(currentRect.right(), candRect.right()) - qMax(currentRect.left(), candRect.left()));
            if (overlapX > 0) {
                inBeam = true;
                secondaryDist = qAbs(candCenter.x() - currentCenter.x());
            } else {
                const int gapX = (candRect.left() > currentRect.right())
                    ? (candRect.left() - currentRect.right())
                    : (currentRect.left() - candRect.right());
                secondaryDist = gapX + qAbs(candCenter.x() - currentCenter.x()) * 0.5;
            }

            if (!inBeam && secondaryDist > primaryDist * 1.5) {
                continue;
            }
            break;
        }
        }

        if (inBeam) {
            const double score = primaryDist * 1.0 + secondaryDist * 0.25;
            if (score < bestTier1Score) {
                bestTier1Score = score;
                bestTier1Path = normalizedPath;
            }
        } else {
            const double score = primaryDist * 1.0 + secondaryDist * 3.5;
            if (score < bestTier2Score) {
                bestTier2Score = score;
                bestTier2Path = normalizedPath;
            }
        }
    }

    if (!bestTier1Path.isEmpty()) {
        return bestTier1Path;
    }
    return bestTier2Path;
}
