#pragma once

#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
#include <QWindow>
#include <QtGlobal>

namespace DpiScaleHelper {

inline qreal uiScaleForScreen(const QScreen* screen)
{
    if (QCoreApplication::testAttribute(Qt::AA_EnableHighDpiScaling)) {
        return 1.0;
    }

    const qreal dpi = screen ? screen->logicalDotsPerInch() : 96.0;
    return qBound<qreal>(1.0, dpi / 96.0, 4.0);
}

inline qreal uiScaleForWidget(const QWidget* widget)
{
    QScreen* screen = nullptr;
    if (widget) {
        screen = widget->screen();
        if (!screen && widget->windowHandle()) {
            screen = widget->windowHandle()->screen();
        }
    }

    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    return uiScaleForScreen(screen);
}

inline int scalePx(const QWidget* widget, int value)
{
    return qMax(1, qRound(value * uiScaleForWidget(widget)));
}

inline int scalePxForScreen(const QScreen* screen, int value)
{
    return qMax(1, qRound(value * uiScaleForScreen(screen)));
}

}
