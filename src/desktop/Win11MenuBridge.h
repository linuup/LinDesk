#pragma once

#include <QJsonArray>
#include <QPoint>
#include <QString>

class Win11MenuBridge
{
public:
    static bool showDesktopMenu(const QPoint& screenPos, const QJsonArray& items, QString* commandId);
    static void shutdownHost();
    static void warmupHost();

private:
    static bool ensureHostRunning();
    static bool sendRequest(const QByteArray& payload, QByteArray* response);
};
