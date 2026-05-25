#pragma once

#include <QGuiApplication>
#include <QPoint>
#include <QRect>
#include <QScreen>
#include <QWidget>
#include <QWindow>
#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace DpiCoordinateHelper {

#ifdef Q_OS_WIN
namespace Detail {

inline QRect rectFromNative(const RECT& rect)
{
    return QRect(rect.left,
                 rect.top,
                 qMax(0L, rect.right - rect.left),
                 qMax(0L, rect.bottom - rect.top));
}

inline RECT nativeFromRect(const QRect& rect)
{
    RECT nativeRect = {};
    nativeRect.left = rect.left();
    nativeRect.top = rect.top();
    nativeRect.right = rect.right() + 1;
    nativeRect.bottom = rect.bottom() + 1;
    return nativeRect;
}

inline QRect monitorRectForScreen(const QScreen* screen)
{
    if (!screen) {
        return {};
    }

    struct MonitorSearch {
        QString name;
        QRect rect;
    } search { screen->name(), QRect() };

    EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
            auto* search = reinterpret_cast<MonitorSearch*>(data);
            MONITORINFOEXW info = {};
            info.cbSize = sizeof(info);
            if (GetMonitorInfoW(monitor, &info)) {
                const QString deviceName = QString::fromWCharArray(info.szDevice);
                if (deviceName.compare(search->name, Qt::CaseInsensitive) == 0) {
                    search->rect = rectFromNative(info.rcMonitor);
                    return FALSE;
                }
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));

    if (search.rect.isValid()) {
        return search.rect;
    }

    const QRect logicalRect = screen->geometry();
    const qreal dpr = qMax<qreal>(0.1, screen->devicePixelRatio());
    return QRect(qRound(logicalRect.x() * dpr),
                 qRound(logicalRect.y() * dpr),
                 qRound(logicalRect.width() * dpr),
                 qRound(logicalRect.height() * dpr));
}

inline QScreen* screenForNativePoint(const QPoint& nativePoint)
{
    QScreen* bestScreen = nullptr;
    for (QScreen* screen : QGuiApplication::screens()) {
        if (monitorRectForScreen(screen).contains(nativePoint)) {
            return screen;
        }
        if (!bestScreen) {
            bestScreen = screen;
        }
    }
    return bestScreen ? bestScreen : QGuiApplication::primaryScreen();
}

inline QScreen* screenForNativeRect(const QRect& nativeRect)
{
    const QPoint center = nativeRect.isValid() ? nativeRect.center() : QPoint();
    return screenForNativePoint(center);
}

inline QPoint mapQtPointToNative(QScreen* screen, const QPoint& qtPoint)
{
    if (!screen) {
        return qtPoint;
    }

    const QRect logicalRect = screen->geometry();
    const QRect nativeRect = monitorRectForScreen(screen);
    if (!logicalRect.isValid() || !nativeRect.isValid()) {
        const qreal dpr = qMax<qreal>(0.1, screen->devicePixelRatio());
        return QPoint(qRound(qtPoint.x() * dpr), qRound(qtPoint.y() * dpr));
    }

    const qreal scaleX = static_cast<qreal>(nativeRect.width()) / qMax(1, logicalRect.width());
    const qreal scaleY = static_cast<qreal>(nativeRect.height()) / qMax(1, logicalRect.height());
    return QPoint(nativeRect.left() + qRound((qtPoint.x() - logicalRect.left()) * scaleX),
                  nativeRect.top() + qRound((qtPoint.y() - logicalRect.top()) * scaleY));
}

inline QPoint mapNativePointToQt(QScreen* screen, const QPoint& nativePoint)
{
    if (!screen) {
        return nativePoint;
    }

    const QRect logicalRect = screen->geometry();
    const QRect nativeRect = monitorRectForScreen(screen);
    if (!logicalRect.isValid() || !nativeRect.isValid()) {
        const qreal dpr = qMax<qreal>(0.1, screen->devicePixelRatio());
        return QPoint(qRound(nativePoint.x() / dpr), qRound(nativePoint.y() / dpr));
    }

    const qreal scaleX = static_cast<qreal>(logicalRect.width()) / qMax(1, nativeRect.width());
    const qreal scaleY = static_cast<qreal>(logicalRect.height()) / qMax(1, nativeRect.height());
    return QPoint(logicalRect.left() + qRound((nativePoint.x() - nativeRect.left()) * scaleX),
                  logicalRect.top() + qRound((nativePoint.y() - nativeRect.top()) * scaleY));
}

inline QScreen* screenForWidgetOrPoint(const QWidget* widget, const QPoint& qtPoint)
{
    QScreen* screen = QGuiApplication::screenAt(qtPoint);
    if (!screen && widget) {
        screen = widget->screen();
        if (!screen && widget->windowHandle()) {
            screen = widget->windowHandle()->screen();
        }
    }
    return screen ? screen : QGuiApplication::primaryScreen();
}

inline QScreen* screenForWindowOrPoint(HWND window, const QPoint& qtPoint)
{
    QScreen* screen = QGuiApplication::screenAt(qtPoint);
    if (screen) {
        return screen;
    }
    if (window) {
        RECT rect = {};
        if (GetWindowRect(window, &rect)) {
            screen = screenForNativeRect(rectFromNative(rect));
        }
    }
    return screen ? screen : QGuiApplication::primaryScreen();
}

}

inline QPoint qtGlobalPointToNativePoint(const QWidget* widget, const QPoint& qtPoint)
{
    return Detail::mapQtPointToNative(Detail::screenForWidgetOrPoint(widget, qtPoint), qtPoint);
}

inline QPoint qtGlobalPointToNativePoint(HWND window, const QPoint& qtPoint)
{
    return Detail::mapQtPointToNative(Detail::screenForWindowOrPoint(window, qtPoint), qtPoint);
}

inline QRect qtGlobalRectToNativeRect(const QWidget* widget, const QRect& qtRect)
{
    if (!qtRect.isValid()) {
        return {};
    }
    const QPoint topLeft = qtGlobalPointToNativePoint(widget, qtRect.topLeft());
    const QPoint bottomRight = qtGlobalPointToNativePoint(widget, qtRect.bottomRight());
    return QRect(topLeft, bottomRight).normalized();
}

inline QRect qtGlobalRectToNativeRect(HWND window, const QRect& qtRect)
{
    if (!qtRect.isValid()) {
        return {};
    }
    const QPoint topLeft = qtGlobalPointToNativePoint(window, qtRect.topLeft());
    const QPoint bottomRight = qtGlobalPointToNativePoint(window, qtRect.bottomRight());
    return QRect(topLeft, bottomRight).normalized();
}

inline QPoint nativePointToQtGlobalPoint(const QWidget* widget, const POINT& nativePoint)
{
    Q_UNUSED(widget);
    const QPoint point(nativePoint.x, nativePoint.y);
    return Detail::mapNativePointToQt(Detail::screenForNativePoint(point), point);
}

inline QPoint nativePointToQtGlobalPoint(HWND window, const POINT& nativePoint)
{
    Q_UNUSED(window);
    const QPoint point(nativePoint.x, nativePoint.y);
    return Detail::mapNativePointToQt(Detail::screenForNativePoint(point), point);
}

inline QRect nativeRectToQtGlobalRect(const QWidget* widget, const RECT& nativeRect)
{
    Q_UNUSED(widget);
    const QRect rect = Detail::rectFromNative(nativeRect);
    QScreen* screen = Detail::screenForNativeRect(rect);
    const QPoint topLeft = Detail::mapNativePointToQt(screen, rect.topLeft());
    const QPoint bottomRight = Detail::mapNativePointToQt(screen, rect.bottomRight());
    return QRect(topLeft, bottomRight).normalized();
}

inline QRect nativeRectToQtGlobalRect(HWND window, const RECT& nativeRect)
{
    Q_UNUSED(window);
    const QRect rect = Detail::rectFromNative(nativeRect);
    QScreen* screen = Detail::screenForNativeRect(rect);
    const QPoint topLeft = Detail::mapNativePointToQt(screen, rect.topLeft());
    const QPoint bottomRight = Detail::mapNativePointToQt(screen, rect.bottomRight());
    return QRect(topLeft, bottomRight).normalized();
}

inline QRect nativeRectToQtGlobalRectForDpi(const RECT& nativeRect, UINT dpi)
{
    const QRect rect = Detail::rectFromNative(nativeRect);
    const qreal scale = dpi > 0 ? 96.0 / static_cast<qreal>(dpi) : 1.0;
    QScreen* screen = Detail::screenForNativeRect(rect);
    if (!screen) {
        return QRect(qRound(rect.x() * scale),
                     qRound(rect.y() * scale),
                     qRound(rect.width() * scale),
                     qRound(rect.height() * scale));
    }

    const QRect nativeScreenRect = Detail::monitorRectForScreen(screen);
    const QRect logicalScreenRect = screen->geometry();
    if (!nativeScreenRect.isValid() || !logicalScreenRect.isValid()) {
        return QRect(qRound(rect.x() * scale),
                     qRound(rect.y() * scale),
                     qRound(rect.width() * scale),
                     qRound(rect.height() * scale));
    }

    return QRect(logicalScreenRect.left() + qRound((rect.left() - nativeScreenRect.left()) * scale),
                 logicalScreenRect.top() + qRound((rect.top() - nativeScreenRect.top()) * scale),
                 qRound(rect.width() * scale),
                 qRound(rect.height() * scale));
}

inline RECT qtGlobalRectToNativeRECT(HWND window, const QRect& qtRect)
{
    return Detail::nativeFromRect(qtGlobalRectToNativeRect(window, qtRect));
}

#else
inline QPoint qtGlobalPointToNativePoint(const QWidget*, const QPoint& qtPoint) { return qtPoint; }
inline QRect qtGlobalRectToNativeRect(const QWidget*, const QRect& qtRect) { return qtRect; }
#endif

}
