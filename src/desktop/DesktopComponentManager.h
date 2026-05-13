#pragma once

#include "desktop/DesktopComponentItem.h"

#include <QObject>
#include <QSet>
#include <QTimer>

class DesktopComponentManager : public QObject {
    Q_OBJECT

public:
    explicit DesktopComponentManager(QObject* parent = nullptr);

    const QVector<DesktopComponentItemModel>& items() const { return m_items; }
    QVector<DesktopComponentItemModel>& items() { return m_items; }

    void setLayoutMetrics(const DesktopComponentLayoutMetrics& metrics);
    const DesktopComponentLayoutMetrics& layoutMetrics() const { return m_metrics; }

    DesktopComponentItemModel* createTimer(const QPoint& requestedPos, const QSet<quint64>& occupiedCells);
    void setItemsFromPersistence(const QVector<QMap<QString, QVariant>>& items, const QSet<quint64>& occupiedCells);
    QVector<QMap<QString, QVariant>> toPersistence() const;

    DesktopComponentItemModel* itemAt(const QPoint& pos);
    const DesktopComponentItemModel* itemAt(const QPoint& pos) const;
    DesktopComponentItemModel* itemById(const QString& id);
    const DesktopComponentItemModel* itemById(const QString& id) const;

    void clearSelection();
    bool selectItem(const QString& id);
    bool setHoveredItemAt(const QPoint& pos);
    bool setItemMode(const QString& id, DesktopComponentMode mode, const QSet<quint64>& occupiedCells);
    bool moveItemTo(const QString& id, const QPoint& requestedPos, const QSet<quint64>& occupiedCells);
    bool toggleTimer(const QString& id);
    bool resetTimer(const QString& id);
    bool setTimerDuration(const QString& id, qint64 durationMs, const QString& presetName);
    bool adjustTimerMinutes(const QString& id, int deltaMinutes);
    bool toggleTimerSound(const QString& id);
    bool setTimerPinned(const QString& id, bool pinned);
    bool setTimerPinnedPosition(const QString& id, const QPoint& globalTopLeft);
    bool removeItem(const QString& id);

    QRect componentRect(const DesktopComponentItemModel& item) const;
    QSet<quint64> occupiedCells(const QString& ignoredId = QString()) const;
    QPoint resolvePosition(const QPoint& requestedPos,
                           const QSize& gridSpan,
                           const QSet<quint64>& externalOccupiedCells,
                           const QString& ignoredId = QString()) const;

signals:
    void changed();

private:
    QSize gridSpanForMode(DesktopComponentMode mode) const;
    QPoint pointToCell(const QPoint& pos) const;
    QPoint cellToPoint(const QPoint& cell) const;
    QRect rectForCellSpan(const QPoint& cell, const QSize& gridSpan) const;
    bool canUseCellArea(const QPoint& cell,
                        const QSize& gridSpan,
                        const QSet<quint64>& externalOccupiedCells,
                        const QSet<quint64>& componentCells) const;
    QSet<quint64> occupiedCellsForItem(const DesktopComponentItemModel& item) const;
    void updateItemRect(DesktopComponentItemModel& item) const;
    void ensureTickTimer();
    void stopTickTimerIfIdle();

    QVector<DesktopComponentItemModel> m_items;
    DesktopComponentLayoutMetrics m_metrics;
    QTimer m_tickTimer;
};
