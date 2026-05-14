#include "desktop/DesktopComponentRenderer.h"

#include "app/AppFonts.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QtMath>

#include <cmath>

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

bool timerHasStarted(const DesktopTimerState& timer)
{
    return timer.running || timer.finished || timer.remainingMs < timer.durationMs;
}

QString statusTextForTimer(const DesktopTimerState& timer)
{
    if (timer.finished) {
        return QStringLiteral("Done");
    }
    if (timer.running) {
        return QStringLiteral("Running");
    }
    if (timerHasStarted(timer)) {
        return QStringLiteral("Paused");
    }
    return QStringLiteral("Ready");
}

qreal breathingPhaseForTimer(const DesktopTimerState& timer)
{
    if (!timerHasStarted(timer)) {
        return 0.36;
    }

    const qreal durationMs = 1000.0;
    const qreal t = std::fmod(static_cast<qreal>(QDateTime::currentMSecsSinceEpoch()), durationMs) / durationMs;
    const qreal wave = 0.5 - 0.5 * qCos(t * 2.0 * M_PI);
    return wave * wave * (3.0 - 2.0 * wave);
}

qreal breathingRingPhase(const DesktopTimerState& timer)
{
    if (!timerHasStarted(timer)) {
        return 0.0;
    }

    const qreal durationMs = 1000.0;
    return std::fmod(static_cast<qreal>(QDateTime::currentMSecsSinceEpoch()), durationMs) / durationMs;
}

void paintSoftShadow(QPainter& painter, const QRect& rect, int radius, qreal scale)
{
    painter.save();
    painter.setPen(Qt::NoPen);
    const int layers = 3;
    for (int i = layers; i >= 1; --i) {
        const int spread = sp(scale, i);
        QRect shadowRect = rect.adjusted(-spread, sp(scale, 1), spread, sp(scale, 2 + i));
        painter.setBrush(QColor(15, 23, 42, 4 + i * 3));
        painter.drawRoundedRect(shadowRect, radius + spread, radius + spread);
    }
    painter.restore();
}

void paintFluentSurface(QPainter& painter, const QRect& rect, int radius, const QColor& accent, bool selected, qreal scale)
{
    paintSoftShadow(painter, rect, radius, scale);

    painter.save();
    painter.setPen(Qt::NoPen);

    QLinearGradient fill(rect.topLeft(), rect.bottomRight());
    fill.setColorAt(0.0, QColor(255, 255, 255, 226));
    fill.setColorAt(0.52, QColor(248, 252, 255, 208));
    fill.setColorAt(1.0, QColor(236, 243, 252, 196));
    painter.setBrush(fill);
    painter.drawRoundedRect(rect, radius, radius);

    QLinearGradient tint(rect.topLeft(), rect.bottomLeft());
    tint.setColorAt(0.0, QColor(accent.red(), accent.green(), accent.blue(), 22));
    tint.setColorAt(1.0, QColor(accent.red(), accent.green(), accent.blue(), 5));
    painter.setBrush(tint);
    painter.drawRoundedRect(rect.adjusted(sp(scale, 5), sp(scale, 5), -sp(scale, 5), -sp(scale, 5)),
                            qMax(1, radius - sp(scale, 5)),
                            qMax(1, radius - sp(scale, 5)));

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(255, 255, 255, 210), sp(scale, 1)));
    painter.drawRoundedRect(rect.adjusted(1, 1, -1, -1), radius, radius);
    painter.setPen(QPen(QColor(24, 37, 56, 38), sp(scale, 1)));
    painter.drawRoundedRect(rect, radius, radius);

    if (selected) {
        painter.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), 190), sp(scale, 2)));
        painter.drawRoundedRect(rect.adjusted(sp(scale, 2), sp(scale, 2), -sp(scale, 2), -sp(scale, 2)),
                                qMax(1, radius - sp(scale, 2)),
                                qMax(1, radius - sp(scale, 2)));
    }

    painter.restore();
}

void paintBreathingDot(QPainter& painter, const QPoint& center, int baseRadius, const DesktopTimerState& timer, const QColor& accent)
{
    const qreal phase = breathingPhaseForTimer(timer);
    const qreal ringPhase = breathingRingPhase(timer);
    const qreal pulseStrength = timer.finished ? 1.0 : (timer.running ? 0.72 : 0.46);
    const qreal glowRadius = baseRadius * (2.1 + pulseStrength * 0.55 * phase);
    const int glowAlpha = timerHasStarted(timer)
        ? qRound(timer.finished ? 62 + 46 * phase : 28 + 28 * phase)
        : 38;
    const QPointF centerF(center);

    painter.save();
    painter.setPen(Qt::NoPen);

    QRadialGradient glow(centerF, glowRadius);
    glow.setColorAt(0.0, QColor(accent.red(), accent.green(), accent.blue(), glowAlpha));
    glow.setColorAt(0.42, QColor(accent.red(), accent.green(), accent.blue(), qRound(glowAlpha * 0.34)));
    glow.setColorAt(0.72, QColor(accent.red(), accent.green(), accent.blue(), qRound(glowAlpha * 0.10)));
    glow.setColorAt(1.0, QColor(accent.red(), accent.green(), accent.blue(), 0));
    painter.setBrush(glow);
    painter.drawEllipse(centerF, glowRadius, glowRadius);

    if (timerHasStarted(timer)) {
        for (int i = 0; i < 2; ++i) {
            const qreal shifted = std::fmod(ringPhase + i * 0.5, 1.0);
            const qreal eased = shifted * shifted * (3.0 - 2.0 * shifted);
            const qreal ringRadius = baseRadius * (1.15 + eased * (timer.finished ? 2.15 : 1.55));
            const int ringAlpha = qRound((timer.finished ? 92 : 58) * (1.0 - eased));
            if (ringAlpha <= 0) {
                continue;
            }
            QPen ringPen(QColor(accent.red(), accent.green(), accent.blue(), ringAlpha), qMax<qreal>(1.0, baseRadius * 0.18));
            ringPen.setCapStyle(Qt::RoundCap);
            painter.setPen(ringPen);
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(centerF, ringRadius, ringRadius);
        }
        painter.setPen(Qt::NoPen);
    }

    const qreal innerRadius = baseRadius * (1.0 + 0.06 * phase);
    painter.setBrush(QColor(accent.red(), accent.green(), accent.blue(), timerHasStarted(timer) ? 225 : 176));
    painter.drawEllipse(centerF, innerRadius, innerRadius);
    painter.setBrush(QColor(255, 255, 255, 185));
    painter.drawEllipse(centerF + QPointF(-innerRadius / 3.0, -innerRadius / 3.0),
                        qMax<qreal>(1.0, innerRadius / 3.0),
                        qMax<qreal>(1.0, innerRadius / 3.0));
    painter.restore();
}

void paintCardTimer(QPainter& painter, const DesktopComponentItemModel& item, qreal scale)
{
    const QRect outer = item.rect.adjusted(sp(scale, 8), sp(scale, 8), -sp(scale, 8), -sp(scale, 8));
    const int radius = sp(scale, 18);
    const QColor accent = accentForTimer(item.timer);

    painter.save();
    paintFluentSurface(painter, outer, radius, accent, item.selected, scale);

    QFont timeFont = AppFonts::uiFont(50);
    timeFont.setWeight(QFont::Bold);
    painter.setFont(timeFont);
    painter.setPen(QColor(17, 24, 39));
    painter.drawText(outer.adjusted(0, sp(scale, 6), 0, -sp(scale, 20)),
                     Qt::AlignCenter,
                     timerText(item.timer.remainingMs));

    const QPoint dotCenter(outer.center().x() - sp(scale, 30), outer.bottom() - sp(scale, 23));
    paintBreathingDot(painter, dotCenter, sp(scale, 5), item.timer, accent);

    if (outer.width() >= sp(scale, 150)) {
        QFont statusFont = AppFonts::uiFont(10);
        statusFont.setWeight(QFont::Medium);
        painter.setFont(statusFont);
        painter.setPen(QColor(71, 85, 105));
        painter.drawText(QRect(dotCenter.x() + sp(scale, 12), outer.bottom() - sp(scale, 34), sp(scale, 86), sp(scale, 22)),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         statusTextForTimer(item.timer));
    }

    painter.restore();
}

void paintMiniTimer(QPainter& painter, const DesktopComponentItemModel& item, qreal scale)
{
    const QRect pill = item.rect.adjusted(sp(scale, 10), sp(scale, 14), -sp(scale, 10), -sp(scale, 14));
    const int radius = pill.height() / 2;
    const QColor accent = accentForTimer(item.timer);

    painter.save();
    paintFluentSurface(painter, pill, radius, accent, item.selected, scale);

    const QPoint dotCenter(pill.left() + sp(scale, 22), pill.center().y());
    paintBreathingDot(painter, dotCenter, sp(scale, 5), item.timer, accent);

    QFont timeFont = AppFonts::uiFont(26);
    timeFont.setWeight(QFont::Bold);
    painter.setFont(timeFont);
    painter.setPen(QColor(17, 24, 39));
    painter.drawText(pill.adjusted(sp(scale, 44), 0, -sp(scale, 18), 0),
                     Qt::AlignCenter,
                     timerText(item.timer.remainingMs));
    painter.restore();
}

void paintExpandedTimer(QPainter& painter, const DesktopComponentItemModel& item, qreal scale)
{
    const QRect outer = item.rect.adjusted(sp(scale, 8), sp(scale, 8), -sp(scale, 8), -sp(scale, 8));
    const int radius = sp(scale, 20);
    const QColor accent = accentForTimer(item.timer);

    painter.save();
    paintFluentSurface(painter, outer, radius, accent, item.selected, scale);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 18));
    painter.drawEllipse(QRect(outer.center().x() - sp(scale, 82),
                              outer.center().y() - sp(scale, 76),
                              sp(scale, 164),
                              sp(scale, 152)));

    QFont timeFont = AppFonts::uiFont(70);
    timeFont.setWeight(QFont::Bold);
    painter.setFont(timeFont);
    painter.setPen(QColor(17, 24, 39));
    painter.drawText(outer.adjusted(0, sp(scale, 4), 0, -sp(scale, 44)),
                     Qt::AlignCenter,
                     timerText(item.timer.remainingMs));

    const QPoint dotCenter(outer.center().x() - sp(scale, 34), outer.bottom() - sp(scale, 34));
    paintBreathingDot(painter, dotCenter, sp(scale, 7), item.timer, accent);

    QFont statusFont = AppFonts::uiFont(12);
    statusFont.setWeight(QFont::Medium);
    painter.setFont(statusFont);
    painter.setPen(QColor(71, 85, 105));
    painter.drawText(QRect(dotCenter.x() + sp(scale, 16), outer.bottom() - sp(scale, 48), sp(scale, 100), sp(scale, 28)),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     statusTextForTimer(item.timer));

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
