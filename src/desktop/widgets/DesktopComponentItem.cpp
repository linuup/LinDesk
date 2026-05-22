#include "desktop/widgets/DesktopComponentItem.h"

#include <QUuid>
#include <QtGlobal>

namespace {

int modeToInt(DesktopComponentMode mode)
{
    return static_cast<int>(mode);
}

DesktopComponentMode modeFromInt(int value)
{
    switch (value) {
    case 0:
        return DesktopComponentMode::Mini;
    case 2:
        return DesktopComponentMode::Expanded;
    case 1:
    default:
        return DesktopComponentMode::Card;
    }
}

QSize gridSpanForMode(DesktopComponentMode mode)
{
    switch (mode) {
    case DesktopComponentMode::Mini:
        return QSize(2, 1);
    case DesktopComponentMode::Expanded:
        return QSize(4, 3);
    case DesktopComponentMode::Card:
    default:
        return QSize(3, 2);
    }
}

}

QMap<QString, QVariant> desktopComponentToMap(const DesktopComponentItemModel& item)
{
    QMap<QString, QVariant> data;
    data["id"] = item.id;
    data["type"] = static_cast<int>(item.type);
    data["mode"] = modeToInt(item.mode);
    data["x"] = item.position.x();
    data["y"] = item.position.y();
    data["gridW"] = item.gridSpan.width();
    data["gridH"] = item.gridSpan.height();
    data["title"] = item.title;
    data["durationMs"] = item.timer.durationMs;
    data["remainingMs"] = item.timer.remainingMs;
    data["running"] = item.timer.running;
    data["finished"] = item.timer.finished;
    data["soundEnabled"] = item.timer.soundEnabled;
    data["loopCount"] = item.timer.loopCount;
    data["presetName"] = item.timer.presetName;
    data["pinned"] = item.pinned;
    data["hasPinnedPosition"] = item.hasPinnedPosition;
    data["pinnedX"] = item.pinnedPosition.x();
    data["pinnedY"] = item.pinnedPosition.y();
    return data;
}

bool desktopComponentFromMap(const QMap<QString, QVariant>& data, DesktopComponentItemModel& item)
{
    const int type = data.value("type").toInt(0);
    if (type != static_cast<int>(DesktopComponentType::Timer)) {
        return false;
    }

    item = DesktopComponentItemModel();
    item.id = data.value("id").toString();
    if (item.id.trimmed().isEmpty()) {
        item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    item.type = DesktopComponentType::Timer;
    item.mode = modeFromInt(data.value("mode", 1).toInt());
    item.gridSpan = gridSpanForMode(item.mode);
    item.position = QPoint(data.value("x").toInt(), data.value("y").toInt());
    item.title = data.value("title", QStringLiteral("Focus Timer")).toString();
    if (item.title.trimmed().isEmpty()) {
        item.title = QStringLiteral("Focus Timer");
    }

    const qint64 fallbackDuration = 25 * 60 * 1000;
    item.timer.durationMs = qMax<qint64>(60 * 1000, data.value("durationMs", fallbackDuration).toLongLong());
    item.timer.remainingMs = qBound<qint64>(0, data.value("remainingMs", item.timer.durationMs).toLongLong(), item.timer.durationMs);
    item.timer.running = data.value("running", false).toBool() && item.timer.remainingMs > 0;
    item.timer.finished = data.value("finished", false).toBool();
    item.timer.soundEnabled = data.value("soundEnabled", true).toBool();
    item.timer.loopCount = qMax(0, data.value("loopCount", 0).toInt());
    item.timer.presetName = data.value("presetName", QStringLiteral("25 min")).toString();
    if (item.timer.presetName.trimmed().isEmpty()) {
        item.timer.presetName = QStringLiteral("25 min");
    }
    item.pinned = data.value("pinned", false).toBool();
    item.hasPinnedPosition = data.value("hasPinnedPosition", false).toBool();
    item.pinnedPosition = QPoint(data.value("pinnedX", 0).toInt(), data.value("pinnedY", 0).toInt());
    item.dirty = true;
    return true;
}

QVector<QMap<QString, QVariant>> desktopComponentsToMaps(const QVector<DesktopComponentItemModel>& items)
{
    QVector<QMap<QString, QVariant>> result;
    result.reserve(items.size());
    for (const DesktopComponentItemModel& item : items) {
        result.append(desktopComponentToMap(item));
    }
    return result;
}

QVector<DesktopComponentItemModel> desktopComponentsFromMaps(const QVector<QMap<QString, QVariant>>& items)
{
    QVector<DesktopComponentItemModel> result;
    result.reserve(items.size());
    for (const QMap<QString, QVariant>& data : items) {
        DesktopComponentItemModel item;
        if (desktopComponentFromMap(data, item)) {
            result.append(item);
        }
    }
    return result;
}
