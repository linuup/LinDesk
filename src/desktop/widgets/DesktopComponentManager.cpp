#include "desktop/widgets/DesktopComponentManager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <QtMath>

#include <algorithm>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>

namespace {

quint64 cellKey(int x, int y)
{
    return ((quint64)(uint)x << 32) | (quint64)(uint)y;
}

DesktopComponentMode clampedMode(DesktopComponentMode mode)
{
    switch (mode) {
    case DesktopComponentMode::Mini:
    case DesktopComponentMode::Expanded:
    case DesktopComponentMode::Card:
        return mode;
    }
    return DesktopComponentMode::Card;
}

QString timerEndSoundFilePath()
{
    static QString cachedPath;
    if (!cachedPath.isEmpty() && QFileInfo::exists(cachedPath)) {
        return cachedPath;
    }

    QFile resource(QStringLiteral(":/TimeEnd.wav"));
    if (resource.exists() && resource.open(QIODevice::ReadOnly)) {
        const QString outputPath = QDir::temp().absoluteFilePath(QStringLiteral("LinDesk-TimeEnd.wav"));
        QFile output(outputPath);
        if (output.open(QIODevice::WriteOnly)) {
            output.write(resource.readAll());
            output.close();
            cachedPath = QDir::toNativeSeparators(outputPath);
            return cachedPath;
        }
    }

    const QString sourcePath = QStringLiteral("D:/Desktop/LinDesk/src/resources/TimeEnd.wav");
    if (QFileInfo::exists(sourcePath)) {
        cachedPath = QDir::toNativeSeparators(sourcePath);
    }
    return cachedPath;
}

void playTimerEndSound()
{
    const QString soundPath = timerEndSoundFilePath();
    if (soundPath.isEmpty()) {
        return;
    }
    PlaySoundW(reinterpret_cast<LPCWSTR>(soundPath.utf16()), nullptr, SND_ASYNC | SND_FILENAME | SND_NODEFAULT);
}

bool timerNeedsVisualTick(const DesktopTimerState& timer)
{
    return timer.running || timer.finished || timer.remainingMs < timer.durationMs;
}

}

DesktopComponentManager::DesktopComponentManager(QObject* parent)
    : QObject(parent)
{
    m_tickTimer.setInterval(33);
    connect(&m_tickTimer, &QTimer::timeout, this, [this]() {
        bool changedAny = false;
        bool hasVisualTimer = false;
        for (DesktopComponentItemModel& item : m_items) {
            if (item.type != DesktopComponentType::Timer) {
                continue;
            }

            if (timerNeedsVisualTick(item.timer)) {
                hasVisualTimer = true;
            }

            if (item.timer.running) {
                item.timer.remainingMs = qMax<qint64>(0, item.timer.remainingMs - m_tickTimer.interval());
                if (item.timer.remainingMs == 0) {
                    item.timer.running = false;
                    item.timer.finished = true;
                    ++item.timer.loopCount;
                    if (item.timer.soundEnabled) {
                        playTimerEndSound();
                    }
                }
                changedAny = true;
            }
        }

        if (changedAny || hasVisualTimer) {
            emit changed();
        }
        if (!hasVisualTimer) {
            stopTickTimerIfIdle();
        }
    });
}

void DesktopComponentManager::setLayoutMetrics(const DesktopComponentLayoutMetrics& metrics)
{
    m_metrics = metrics;
    for (DesktopComponentItemModel& item : m_items) {
        item.gridSpan = gridSpanForMode(item.mode);
        updateItemRect(item);
    }
}

DesktopComponentItemModel* DesktopComponentManager::createTimer(const QPoint& requestedPos, const QSet<quint64>& occupiedCells)
{
    DesktopComponentItemModel item;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.type = DesktopComponentType::Timer;
    item.mode = DesktopComponentMode::Card;
    item.gridSpan = gridSpanForMode(item.mode);
    item.position = resolvePosition(requestedPos, item.gridSpan, occupiedCells);
    updateItemRect(item);
    m_items.append(item);
    emit changed();
    return m_items.isEmpty() ? nullptr : &m_items.last();
}

void DesktopComponentManager::setItemsFromPersistence(const QVector<QMap<QString, QVariant>>& items, const QSet<quint64>& occupiedCells)
{
    m_items = desktopComponentsFromMaps(items);
    QSet<quint64> usedCells;
    for (DesktopComponentItemModel& item : m_items) {
        item.mode = clampedMode(item.mode);
        item.gridSpan = gridSpanForMode(item.mode);
        QSet<quint64> blockedCells = occupiedCells;
        blockedCells.unite(usedCells);
        item.position = resolvePosition(item.position, item.gridSpan, blockedCells, item.id);
        updateItemRect(item);
        usedCells.unite(occupiedCellsForItem(item));
    }
    ensureTickTimer();
    emit changed();
}

QVector<QMap<QString, QVariant>> DesktopComponentManager::toPersistence() const
{
    return desktopComponentsToMaps(m_items);
}

DesktopComponentItemModel* DesktopComponentManager::itemAt(const QPoint& pos)
{
    for (int i = m_items.size() - 1; i >= 0; --i) {
        if (m_items[i].pinned) {
            continue;
        }
        if (m_items[i].rect.contains(pos)) {
            return &m_items[i];
        }
    }
    return nullptr;
}

const DesktopComponentItemModel* DesktopComponentManager::itemAt(const QPoint& pos) const
{
    for (int i = m_items.size() - 1; i >= 0; --i) {
        if (m_items[i].pinned) {
            continue;
        }
        if (m_items[i].rect.contains(pos)) {
            return &m_items[i];
        }
    }
    return nullptr;
}

DesktopComponentItemModel* DesktopComponentManager::itemById(const QString& id)
{
    for (DesktopComponentItemModel& item : m_items) {
        if (item.id == id) {
            return &item;
        }
    }
    return nullptr;
}

const DesktopComponentItemModel* DesktopComponentManager::itemById(const QString& id) const
{
    for (const DesktopComponentItemModel& item : m_items) {
        if (item.id == id) {
            return &item;
        }
    }
    return nullptr;
}

void DesktopComponentManager::clearSelection()
{
    bool changedAny = false;
    for (DesktopComponentItemModel& item : m_items) {
        if (item.selected) {
            item.selected = false;
            changedAny = true;
        }
    }
    if (changedAny) {
        emit changed();
    }
}

bool DesktopComponentManager::selectItem(const QString& id)
{
    bool changedAny = false;
    for (DesktopComponentItemModel& item : m_items) {
        const bool selected = item.id == id;
        if (item.selected != selected) {
            item.selected = selected;
            changedAny = true;
        }
    }
    if (changedAny) {
        emit changed();
    }
    return itemById(id) != nullptr;
}

bool DesktopComponentManager::setHoveredItemAt(const QPoint& pos)
{
    const DesktopComponentItemModel* hit = itemAt(pos);
    const QString hoveredId = hit ? hit->id : QString();
    bool changedAny = false;
    for (DesktopComponentItemModel& item : m_items) {
        const bool hovered = item.id == hoveredId;
        if (item.hovered != hovered) {
            item.hovered = hovered;
            changedAny = true;
        }
    }
    if (changedAny) {
        emit changed();
    }
    return changedAny;
}

bool DesktopComponentManager::setItemMode(const QString& id, DesktopComponentMode mode, const QSet<quint64>& occupiedCells)
{
    DesktopComponentItemModel* item = itemById(id);
    if (!item) {
        return false;
    }

    item->mode = clampedMode(mode);
    item->gridSpan = gridSpanForMode(item->mode);
    item->position = resolvePosition(item->position, item->gridSpan, occupiedCells, id);
    updateItemRect(*item);
    emit changed();
    return true;
}

bool DesktopComponentManager::moveItemTo(const QString& id, const QPoint& requestedPos, const QSet<quint64>& occupiedCells)
{
    DesktopComponentItemModel* item = itemById(id);
    if (!item) {
        return false;
    }

    const QPoint resolved = resolvePosition(requestedPos, item->gridSpan, occupiedCells, id);
    if (item->position == resolved) {
        return true;
    }
    item->position = resolved;
    updateItemRect(*item);
    emit changed();
    return true;
}

bool DesktopComponentManager::toggleTimer(const QString& id)
{
    DesktopComponentItemModel* item = itemById(id);
    if (!item) {
        return false;
    }

    if (item->timer.finished || item->timer.remainingMs <= 0) {
        item->timer.remainingMs = item->timer.durationMs;
        item->timer.finished = false;
    }
    item->timer.running = !item->timer.running;
    ensureTickTimer();
    emit changed();
    return true;
}

bool DesktopComponentManager::resetTimer(const QString& id)
{
    DesktopComponentItemModel* item = itemById(id);
    if (!item) {
        return false;
    }

    item->timer.running = false;
    item->timer.finished = false;
    item->timer.remainingMs = item->timer.durationMs;
    stopTickTimerIfIdle();
    emit changed();
    return true;
}

bool DesktopComponentManager::setTimerDuration(const QString& id, qint64 durationMs, const QString& presetName)
{
    DesktopComponentItemModel* item = itemById(id);
    if (!item) {
        return false;
    }

    item->timer.durationMs = qMax<qint64>(60 * 1000, durationMs);
    item->timer.remainingMs = item->timer.durationMs;
    item->timer.running = false;
    item->timer.finished = false;
    item->timer.presetName = presetName;
    stopTickTimerIfIdle();
    emit changed();
    return true;
}

bool DesktopComponentManager::adjustTimerMinutes(const QString& id, int deltaMinutes)
{
    DesktopComponentItemModel* item = itemById(id);
    if (!item || item->timer.running) {
        return false;
    }

    const qint64 nextDuration = qBound<qint64>(60 * 1000,
                                              item->timer.durationMs + static_cast<qint64>(deltaMinutes) * 60 * 1000,
                                              24LL * 60 * 60 * 1000);
    item->timer.durationMs = nextDuration;
    item->timer.remainingMs = nextDuration;
    item->timer.finished = false;
    item->timer.presetName = QStringLiteral("%1 min").arg(nextDuration / 60000);
    emit changed();
    return true;
}

bool DesktopComponentManager::toggleTimerSound(const QString& id)
{
    DesktopComponentItemModel* item = itemById(id);
    if (!item) {
        return false;
    }
    item->timer.soundEnabled = !item->timer.soundEnabled;
    emit changed();
    return true;
}

bool DesktopComponentManager::setTimerPinned(const QString& id, bool pinned)
{
    DesktopComponentItemModel* item = itemById(id);
    if (!item) {
        return false;
    }
    if (item->pinned == pinned) {
        return true;
    }
    item->pinned = pinned;
    emit changed();
    return true;
}

bool DesktopComponentManager::setTimerPinnedPosition(const QString& id, const QPoint& globalTopLeft)
{
    DesktopComponentItemModel* item = itemById(id);
    if (!item) {
        return false;
    }
    if (item->hasPinnedPosition && item->pinnedPosition == globalTopLeft) {
        return true;
    }
    item->hasPinnedPosition = true;
    item->pinnedPosition = globalTopLeft;
    emit changed();
    return true;
}

bool DesktopComponentManager::removeItem(const QString& id)
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].id == id) {
            m_items.removeAt(i);
            stopTickTimerIfIdle();
            emit changed();
            return true;
        }
    }
    return false;
}

QRect DesktopComponentManager::componentRect(const DesktopComponentItemModel& item) const
{
    return rectForCellSpan(pointToCell(item.position), item.gridSpan);
}

QSet<quint64> DesktopComponentManager::occupiedCells(const QString& ignoredId) const
{
    QSet<quint64> result;
    for (const DesktopComponentItemModel& item : m_items) {
        if (!ignoredId.isEmpty() && item.id == ignoredId) {
            continue;
        }
        result.unite(occupiedCellsForItem(item));
    }
    return result;
}

QPoint DesktopComponentManager::resolvePosition(const QPoint& requestedPos,
                                                const QSize& gridSpan,
                                                const QSet<quint64>& externalOccupiedCells,
                                                const QString& ignoredId) const
{
    const QPoint preferredCell = pointToCell(requestedPos);
    const QSet<quint64> componentCells = occupiedCells(ignoredId);
    const int maxCols = qMax(1, m_metrics.maxCols);
    const int maxRows = qMax(1, m_metrics.maxRows);
    const QSize safeSpan(qBound(1, gridSpan.width(), maxCols), qBound(1, gridSpan.height(), maxRows));
    const QPoint clampedPreferred(qBound(0, preferredCell.x(), maxCols - safeSpan.width()),
                                  qBound(0, preferredCell.y(), maxRows - safeSpan.height()));

    const int maxRadius = qMax(maxCols, maxRows);
    for (int radius = 0; radius <= maxRadius; ++radius) {
        QVector<QPair<int, QPoint>> candidates;
        const int minX = qMax(0, clampedPreferred.x() - radius);
        const int maxX = qMin(maxCols - safeSpan.width(), clampedPreferred.x() + radius);
        const int minY = qMax(0, clampedPreferred.y() - radius);
        const int maxY = qMin(maxRows - safeSpan.height(), clampedPreferred.y() + radius);

        for (int x = minX; x <= maxX; ++x) {
            for (int y = minY; y <= maxY; ++y) {
                if (qMax(qAbs(x - clampedPreferred.x()), qAbs(y - clampedPreferred.y())) != radius) {
                    continue;
                }
                const QPoint cell(x, y);
                if (!canUseCellArea(cell, safeSpan, externalOccupiedCells, componentCells)) {
                    continue;
                }
                const int score = (x - clampedPreferred.x()) * (x - clampedPreferred.x()) +
                    (y - clampedPreferred.y()) * (y - clampedPreferred.y());
                candidates.append(qMakePair(score, cell));
            }
        }

        if (!candidates.isEmpty()) {
            std::sort(candidates.begin(), candidates.end(), [](const QPair<int, QPoint>& a, const QPair<int, QPoint>& b) {
                if (a.first != b.first) {
                    return a.first < b.first;
                }
                if (a.second.x() != b.second.x()) {
                    return a.second.x() < b.second.x();
                }
                return a.second.y() < b.second.y();
            });
            return cellToPoint(candidates.first().second);
        }
    }

    return cellToPoint(clampedPreferred);
}

QSize DesktopComponentManager::gridSpanForMode(DesktopComponentMode mode) const
{
    switch (mode) {
    case DesktopComponentMode::Mini:
        return QSize(3, 1);
    case DesktopComponentMode::Expanded:
        return QSize(4, 3);
    case DesktopComponentMode::Card:
    default:
        return QSize(3, 2);
    }
}

QPoint DesktopComponentManager::pointToCell(const QPoint& pos) const
{
    const int gridX = qMax(0, qRound((double)(pos.x() - m_metrics.layoutLeft) / qMax(1, m_metrics.strideX)));
    const int gridY = qMax(0, qRound((double)(pos.y() - m_metrics.layoutTop) / qMax(1, m_metrics.strideY)));
    return QPoint(qBound(0, gridX, qMax(0, m_metrics.maxCols - 1)),
                  qBound(0, gridY, qMax(0, m_metrics.maxRows - 1)));
}

QPoint DesktopComponentManager::cellToPoint(const QPoint& cell) const
{
    return QPoint(m_metrics.layoutLeft + cell.x() * m_metrics.strideX,
                  m_metrics.layoutTop + cell.y() * m_metrics.strideY);
}

QRect DesktopComponentManager::rectForCellSpan(const QPoint& cell, const QSize& gridSpan) const
{
    const int w = qMax(m_metrics.itemWidth, (gridSpan.width() - 1) * m_metrics.strideX + m_metrics.itemWidth);
    const int h = qMax(m_metrics.itemHeight, (gridSpan.height() - 1) * m_metrics.strideY + m_metrics.itemHeight);
    return QRect(cellToPoint(cell), QSize(w, h));
}

bool DesktopComponentManager::canUseCellArea(const QPoint& cell,
                                             const QSize& gridSpan,
                                             const QSet<quint64>& externalOccupiedCells,
                                             const QSet<quint64>& componentCells) const
{
    for (int dx = 0; dx < gridSpan.width(); ++dx) {
        for (int dy = 0; dy < gridSpan.height(); ++dy) {
            const quint64 key = cellKey(cell.x() + dx, cell.y() + dy);
            if (externalOccupiedCells.contains(key) || componentCells.contains(key)) {
                return false;
            }
        }
    }
    return true;
}

QSet<quint64> DesktopComponentManager::occupiedCellsForItem(const DesktopComponentItemModel& item) const
{
    QSet<quint64> result;
    const QPoint cell = pointToCell(item.position);
    const QSize safeSpan(qMax(1, item.gridSpan.width()), qMax(1, item.gridSpan.height()));
    for (int dx = 0; dx < safeSpan.width(); ++dx) {
        for (int dy = 0; dy < safeSpan.height(); ++dy) {
            result.insert(cellKey(cell.x() + dx, cell.y() + dy));
        }
    }
    return result;
}

void DesktopComponentManager::updateItemRect(DesktopComponentItemModel& item) const
{
    item.rect = componentRect(item);
    item.dirty = false;
}

void DesktopComponentManager::ensureTickTimer()
{
    for (const DesktopComponentItemModel& item : m_items) {
        if (item.type == DesktopComponentType::Timer && timerNeedsVisualTick(item.timer)) {
            if (!m_tickTimer.isActive()) {
                m_tickTimer.start();
            }
            return;
        }
    }
}

void DesktopComponentManager::stopTickTimerIfIdle()
{
    for (const DesktopComponentItemModel& item : m_items) {
        if (item.type == DesktopComponentType::Timer && timerNeedsVisualTick(item.timer)) {
            return;
        }
    }
    m_tickTimer.stop();
}
