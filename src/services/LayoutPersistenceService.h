#pragma once

#include <QMap>
#include <QPointer>
#include <QString>
#include <QVariant>
#include <QVector>

struct DesktopRootItemState;
class LinBox;
class ListItem;

struct PersistentIconInfo {
    int x = 0;
    int y = 0;
    QString boxId;
};

struct LayoutPersistenceState {
    bool enablePersistence = true;
    QMap<QString, QString> iconShowRules;
    int viewMode = 0;
    int sortType = 0;
    int preAutoSort = 0;
    int iconPosMode = 0;
    bool autoArrange = false;
    bool gridAlignment = false;
    bool showDesktopIcons = true;
    bool doubleClickHideIcons = true;
    bool showShortCutArrow = true;
    QVector<QMap<QString, QVariant>> boxData;
    QMap<QString, QMap<QString, QVariant>> iconPositions;
};

class LayoutPersistenceService {
public:
    QMap<QString, QMap<QString, QVariant>> collectIconPositions(
        const QVector<ListItem*>& allItems,
        const QVector<QPointer<LinBox>>& boxes) const;

    QMap<QString, QMap<QString, QVariant>> collectIconPositions(
        const QVector<DesktopRootItemState>& desktopItems,
        const QVector<QPointer<LinBox>>& boxes) const;

    QMap<QString, PersistentIconInfo> toSavedIconPositions(
        const QMap<QString, QMap<QString, QVariant>>& iconPositions) const;

    void save(const QString& persistencePath,
              const LayoutPersistenceState& state,
              const QVector<ListItem*>& allItems) const;

    void save(const QString& persistencePath,
              const LayoutPersistenceState& state) const;

    bool load(const QString& persistencePath,
              bool autoUpdateState,
              LayoutPersistenceState& state) const;
};
