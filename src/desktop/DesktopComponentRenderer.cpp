#include "desktop/DesktopComponentRenderer.h"

#include "app/AppFonts.h"

#include <QFontMetrics>
#include <QtMath>

namespace {

int sp(qreal scale, int value)
{
    return qRound(scale * value);
}

QString timerText(qint64 remainingMs)
{
    const qint64 totalSeconds = qMax<qint64>(0, remainingMs / 1000);
    const qint64 minutes = totalSeconds / 60;
    const qint64 seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

QColor accentForTimer(const DesktopTimerState& timer)
{
    if (timer.finished) {
        return QColor(220, 38, 38);
    }
    if (timer.running) {
        return QColor(0, 120, 212);
    }
    if (timer.remainingMs < timer.durationMs) {
        return QColor(217, 119, 6);
    }
    return QColor(74, 94, 120);
}

qreal timerProgress(const DesktopTimerState& timer)
{
    if (timer.durationMs <= 0) {
        return 0.0;
    }
    return qBound<qreal>(0.0, static_cast<qreal>(timer.remainingMs) / static_cast<qreal>(timer.durationMs), 1.0);
}

void paintProgressStrip(QPainter& painter, const QRect& rect, const QColor& accent, const DesktopTimerState& timer, qreal scale)
{
    const int h = sp(scale, 5);
    QRect track(rect.left() + sp(scale, 18), rect.bottom() - sp(scale, 18), rect.width() - sp(scale, 36), h);
    if (track.width() <= 0 || track.height() <= 0) {
        return;
    }

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(198, 210, 226, 120));
    painter.drawRoundedRect(track, h / 2, h / 2);
    QRect fill = track;
    fill.setWidth(qRound(track.width() * timerProgress(timer)));
    painter.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 210));
    painter.drawRoundedRect(fill, h / 2, h / 2);
    painter.restore();
}

void paintCardTimer(QPainter& painter, const DesktopComponentItemModel& item, qreal scale)
{
    const QRect outer = item.rect.adjusted(sp(scale, 8), sp(scale, 8), -sp(scale, 8), -sp(scale, 8));
    const int radius = sp(scale, 18);
    const QColor accent = accentForTimer(item.timer);

    painter.save();
    painter.setPen(QPen(item.selected ? accent : QColor(255, 255, 255, 130), item.selected ? sp(scale, 2) : sp(scale, 1)));
    painter.setBrush(QColor(248, 251, 255, 205));
    painter.drawRoundedRect(outer, radius, radius);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 34));
    painter.drawRoundedRect(outer.adjusted(sp(scale, 8), sp(scale, 8), -sp(scale, 8), -sp(scale, 8)), sp(scale, 14), sp(scale, 14));

    QFont timeFont = AppFonts::uiFont(42);
    timeFont.setWeight(QFont::Bold);
    painter.setFont(timeFont);
    painter.setPen(accent);
    painter.drawText(outer.adjusted(0, sp(scale, 12), 0, -sp(scale, 24)),
                     Qt::AlignCenter,
                     timerText(item.timer.remainingMs));

    painter.restore();
}

void paintMiniTimer(QPainter& painter, const DesktopComponentItemModel& item, qreal scale)
{
    const QRect pill = item.rect.adjusted(sp(scale, 10), sp(scale, 14), -sp(scale, 10), -sp(scale, 14));
    const int radius = pill.height() / 2;
    const QColor accent = accentForTimer(item.timer);

    painter.save();
    painter.setPen(QPen(item.selected ? accent : QColor(255, 255, 255, 150), item.selected ? sp(scale, 2) : sp(scale, 1)));
    painter.setBrush(QColor(248, 251, 255, 218));
    painter.drawRoundedRect(pill, radius, radius);

    const int dotSide = sp(scale, 10);
    QRect dotRect(pill.left() + sp(scale, 16), pill.center().y() - dotSide / 2, dotSide, dotSide);
    painter.setPen(Qt::NoPen);
    painter.setBrush(accent);
    painter.drawEllipse(dotRect);

    QFont timeFont = AppFonts::uiFont(23);
    timeFont.setWeight(QFont::Bold);
    painter.setFont(timeFont);
    painter.setPen(QColor(24, 37, 56));
    painter.drawText(pill.adjusted(sp(scale, 36), 0, -sp(scale, 14), 0),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     timerText(item.timer.remainingMs));
    painter.restore();
}

void paintExpandedTimer(QPainter& painter, const DesktopComponentItemModel& item, qreal scale)
{
    const QRect outer = item.rect.adjusted(sp(scale, 8), sp(scale, 8), -sp(scale, 8), -sp(scale, 8));
    const int radius = sp(scale, 20);
    const QColor accent = accentForTimer(item.timer);

    painter.save();
    painter.setPen(QPen(item.selected ? accent : QColor(255, 255, 255, 130), item.selected ? sp(scale, 2) : sp(scale, 1)));
    painter.setBrush(QColor(248, 251, 255, 214));
    painter.drawRoundedRect(outer, radius, radius);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 28));
    painter.drawEllipse(QRect(outer.center().x() - sp(scale, 72),
                              outer.center().y() - sp(scale, 72),
                              sp(scale, 144),
                              sp(scale, 144)));

    QFont timeFont = AppFonts::uiFont(62);
    timeFont.setWeight(QFont::Bold);
    painter.setFont(timeFont);
    painter.setPen(accent);
    painter.drawText(outer.adjusted(0, sp(scale, 8), 0, -sp(scale, 32)),
                     Qt::AlignCenter,
                     timerText(item.timer.remainingMs));

    painter.restore();
}

}

void paintDesktopComponent(QPainter& painter,
                           const DesktopComponentItemModel& item,
                           const QRegion& dirtyRegion,
                           qreal dpiScale)
{
    if (item.rect.isEmpty() || !dirtyRegion.intersects(item.rect.adjusted(-12, -12, 12, 12))) {
        return;
    }

    switch (item.mode) {
    case DesktopComponentMode::Mini:
        paintMiniTimer(painter, item, dpiScale);
        break;
    case DesktopComponentMode::Expanded:
        paintExpandedTimer(painter, item, dpiScale);
        break;
    case DesktopComponentMode::Card:
    default:
        paintCardTimer(painter, item, dpiScale);
        break;
    }
}
