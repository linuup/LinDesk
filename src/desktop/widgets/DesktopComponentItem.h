#pragma once

#include <QMap>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVariant>
#include <QVector>

enum class DesktopComponentType {
    Timer = 0
};

enum class DesktopComponentMode {
    Mini = 0,
    Card = 1,
    Expanded = 2
};

struct DesktopTimerState {
    qint64 durationMs = 25 * 60 * 1000;
    qint64 remainingMs = 25 * 60 * 1000;
    bool running = false;
    bool finished = false;
    bool soundEnabled = true;
    int loopCount = 0;
    QString presetName = QStringLiteral("25 min");
};

struct DesktopComponentItemModel {
    QString id;
    DesktopComponentType type = DesktopComponentType::Timer;
    DesktopComponentMode mode = DesktopComponentMode::Card;
    QPoint position;
    QSize gridSpan = QSize(3, 2);
    QRect rect;
    QString title = QStringLiteral("Focus Timer");
    DesktopTimerState timer;
    bool pinned = false;
    bool hasPinnedPosition = false;
    QPoint pinnedPosition;
    bool selected = false;
    bool hovered = false;
    bool dirty = true;
};

struct DesktopComponentLayoutMetrics {
    int layoutLeft = 0;
    int layoutTop = 0;
    int usableRight = 0;
    int usableBottom = 0;
    int itemWidth = 76;
    int itemHeight = 78;
    int strideX = 80;
    int strideY = 84;
    int maxCols = 1;
    int maxRows = 1;
    qreal dpiScale = 1.0;
};

QMap<QString, QVariant> desktopComponentToMap(const DesktopComponentItemModel& item);
bool desktopComponentFromMap(const QMap<QString, QVariant>& data, DesktopComponentItemModel& item);
QVector<QMap<QString, QVariant>> desktopComponentsToMaps(const QVector<DesktopComponentItemModel>& items);
QVector<DesktopComponentItemModel> desktopComponentsFromMaps(const QVector<QMap<QString, QVariant>>& items);
