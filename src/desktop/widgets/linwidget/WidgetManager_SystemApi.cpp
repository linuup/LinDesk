#include "desktop/widgets/linwidget/WidgetManager_Internal.h"

namespace LinWidget {

// ============================================================================
// WidgetManager SystemInfo & Storage Request Implementation
// ============================================================================

void WidgetManager::handleSystemInfoRequest(const QJsonObject& message)
{
    const QString instanceId = message.value(QStringLiteral("instanceId")).toString(message.value(QStringLiteral("widgetId")).toString()).trimmed();
    const QString requestId = message.value(QStringLiteral("requestId")).toString();
    const QString action = message.value(QStringLiteral("action")).toString(QStringLiteral("snapshot"));
    const QString watchId = message.value(QStringLiteral("watchId")).toString(requestId);

    const WidgetInstance instance = m_instances.value(instanceId);
    if (instance.instanceId.isEmpty()) {
        qWarning() << "[LinWidget][WidgetManager] system_info_request rejected: instance not running:" << instanceId
                   << "action:" << action;
        sendSystemInfoResponse(instanceId, requestId, action, false, QJsonObject(), QStringLiteral("Widget instance is not running."));
        return;
    }
    if (!m_permissions.canUseDesktopApi(instance.grantedPermissions)) {
        qWarning() << "[LinWidget][WidgetManager] system_info_request rejected: 'desktop' permission not granted for"
                   << instanceId << "granted:" << instance.grantedPermissions << "action:" << action;
        sendSystemInfoResponse(instanceId, requestId, action, false, QJsonObject(), QStringLiteral("Desktop permission is required."));
        return;
    }

    if (action == QStringLiteral("snapshot")) {
        sendSystemInfoResponse(instanceId, requestId, action, true, m_systemInfoService.snapshot());
        return;
    }
    const QStringList categories {
        QStringLiteral("cpu"),
        QStringLiteral("memory"),
        QStringLiteral("gpu"),
        QStringLiteral("disk"),
        QStringLiteral("network"),
        QStringLiteral("battery"),
        QStringLiteral("os"),
        QStringLiteral("uptime")
    };
    if (categories.contains(action)) {
        sendSystemInfoResponse(instanceId, requestId, action, true, m_systemInfoService.categorySnapshot(action));
        return;
    }

    if (action == QStringLiteral("watch")) {
        if (watchId.isEmpty()) {
            sendSystemInfoResponse(instanceId, requestId, action, false, QJsonObject(), QStringLiteral("Missing watch id."));
            return;
        }
        const QString timerKey = instanceId + QLatin1Char('\n') + watchId;
        if (QTimer* oldTimer = m_systemInfoWatchTimers.take(timerKey)) {
            oldTimer->deleteLater();
        }
        const int intervalMs = qMax(500, message.value(QStringLiteral("intervalMs")).toInt(1000));
        auto* timer = new QTimer(this);
        timer->setInterval(intervalMs);
        connect(timer, &QTimer::timeout, this, [this, instanceId, watchId]() {
            if (!m_instances.contains(instanceId)) {
                stopSystemInfoWatchesForInstance(instanceId);
                return;
            }
            sendSystemInfoUpdate(instanceId, watchId, m_systemInfoService.snapshot());
        });
        m_systemInfoWatchTimers.insert(timerKey, timer);
        timer->start();
        sendSystemInfoResponse(instanceId, requestId, action, true, m_systemInfoService.snapshot());
        return;
    }

    if (action == QStringLiteral("unwatch")) {
        const QString timerKey = instanceId + QLatin1Char('\n') + watchId;
        if (QTimer* timer = m_systemInfoWatchTimers.take(timerKey)) {
            timer->deleteLater();
        }
        sendSystemInfoResponse(instanceId, requestId, action, true);
        return;
    }

    sendSystemInfoResponse(instanceId, requestId, action, false, QJsonObject(), QStringLiteral("Unknown system info action."));
}

void WidgetManager::sendSystemInfoResponse(const QString& instanceId,
                                           const QString& requestId,
                                           const QString& action,
                                           bool ok,
                                           const QJsonObject& data,
                                           const QString& errorMessage)
{
    QJsonObject message = makeWidgetMessage(QString::fromLatin1(WidgetMessageType::SystemInfoResponse), instanceId);
    message.insert(QStringLiteral("instanceId"), instanceId);
    message.insert(QStringLiteral("widgetId"), instanceId);
    message.insert(QStringLiteral("requestId"), requestId);
    message.insert(QStringLiteral("action"), action);
    message.insert(QStringLiteral("ok"), ok);
    if (!data.isEmpty()) {
        message.insert(QStringLiteral("data"), data);
    }
    if (!errorMessage.isEmpty()) {
        message.insert(QStringLiteral("error"), errorMessage);
    }
    sendMessageToWidget(instanceId, message);
}

void WidgetManager::sendSystemInfoUpdate(const QString& instanceId,
                                         const QString& watchId,
                                         const QJsonObject& data)
{
    QJsonObject message = makeWidgetMessage(QString::fromLatin1(WidgetMessageType::SystemInfoUpdate), instanceId);
    message.insert(QStringLiteral("instanceId"), instanceId);
    message.insert(QStringLiteral("widgetId"), instanceId);
    message.insert(QStringLiteral("watchId"), watchId);
    message.insert(QStringLiteral("data"), data);
    sendMessageToWidget(instanceId, message);
}

void WidgetManager::handleStorageRequest(const QJsonObject& message)
{
    const QString instanceId = message.value(QStringLiteral("instanceId")).toString(message.value(QStringLiteral("widgetId")).toString()).trimmed();
    const QString requestId = message.value(QStringLiteral("requestId")).toString();
    const QString scope = message.value(QStringLiteral("scope")).toString(QStringLiteral("instance")).trimmed().toLower();
    const QString action = message.value(QStringLiteral("action")).toString(QStringLiteral("get")).trimmed().toLower();
    const QString key = message.value(QStringLiteral("key")).toString().trimmed();

    const WidgetInstance instance = m_instances.value(instanceId);
    if (instance.instanceId.isEmpty()) {
        sendStorageResponse(instanceId, requestId, false, QJsonValue(), QStringLiteral("Widget instance is not running."));
        return;
    }
    if (!m_permissions.canUseStorage(instance.grantedPermissions)) {
        appendSharedLog(QStringLiteral("permission"),
                        QStringLiteral("%1 permission denied: storage").arg(instanceId));
        sendStorageResponse(instanceId, requestId, false, QJsonValue(), QStringLiteral("Storage permission is required."));
        return;
    }
    if (scope != QStringLiteral("instance") && scope != QStringLiteral("widget")) {
        sendStorageResponse(instanceId, requestId, false, QJsonValue(), QStringLiteral("Storage scope must be instance or widget."));
        return;
    }
    if ((action == QStringLiteral("get") || action == QStringLiteral("set") || action == QStringLiteral("remove")) && key.isEmpty()) {
        sendStorageResponse(instanceId, requestId, false, QJsonValue(), QStringLiteral("Storage key is required."));
        return;
    }

    const QString path = storageFilePathForInstance(instance, scope);
    QJsonObject object;
    QString errorMessage;
    if (!loadStorageObject(path, &object, &errorMessage)) {
        sendStorageResponse(instanceId, requestId, false, QJsonValue(), errorMessage);
        return;
    }

    if (action == QStringLiteral("get")) {
        sendStorageResponse(instanceId, requestId, true, object.value(key));
        return;
    }
    if (action == QStringLiteral("all")) {
        sendStorageResponse(instanceId, requestId, true, object);
        return;
    }
    if (action == QStringLiteral("set")) {
        object.insert(key, message.value(QStringLiteral("value")));
        const QByteArray encoded = QJsonDocument(object).toJson(QJsonDocument::Compact);
        const QString widgetDataRoot = QFileInfo(storageFilePathForInstance(instance, QStringLiteral("widget"))).absolutePath();
        const qint64 currentDirBytes = calculateDirectorySize(widgetDataRoot);
        const qint64 oldFileBytes = QFileInfo(path).exists() ? QFileInfo(path).size() : 0;
        if ((currentDirBytes - oldFileBytes + encoded.size()) > kWidgetStorageQuotaBytes) {
            sendStorageResponse(instanceId, requestId, false, QJsonValue(), QStringLiteral("Storage quota (16MB) exceeded."));
            return;
        }
    } else if (action == QStringLiteral("remove")) {
        object.remove(key);
    } else if (action == QStringLiteral("clear")) {
        object = QJsonObject();
    } else {
        sendStorageResponse(instanceId, requestId, false, QJsonValue(), QStringLiteral("Unknown storage action."));
        return;
    }

    if (!saveStorageObject(path, object, &errorMessage)) {
        sendStorageResponse(instanceId, requestId, false, QJsonValue(), errorMessage);
        return;
    }
    sendStorageResponse(instanceId, requestId, true, action == QStringLiteral("set") ? message.value(QStringLiteral("value")) : QJsonValue(true));
}

void WidgetManager::sendStorageResponse(const QString& instanceId,
                                        const QString& requestId,
                                        bool ok,
                                        const QJsonValue& value,
                                        const QString& errorMessage)
{
    QJsonObject message = makeWidgetMessage(QString::fromLatin1(WidgetMessageType::StorageResponse), instanceId);
    message.insert(QStringLiteral("instanceId"), instanceId);
    message.insert(QStringLiteral("widgetId"), instanceId);
    message.insert(QStringLiteral("requestId"), requestId);
    message.insert(QStringLiteral("ok"), ok);
    if (!value.isUndefined()) {
        message.insert(QStringLiteral("value"), value);
    }
    if (!errorMessage.isEmpty()) {
        message.insert(QStringLiteral("error"), errorMessage);
    }
    sendMessageToWidget(instanceId, message);
}

void WidgetManager::stopSystemInfoWatchesForInstance(const QString& instanceId)
{
    const QString prefix = instanceId + QLatin1Char('\n');
    const QStringList keys = m_systemInfoWatchTimers.keys();
    for (const QString& key : keys) {
        if (!key.startsWith(prefix)) {
            continue;
        }
        if (QTimer* timer = m_systemInfoWatchTimers.take(key)) {
            timer->deleteLater();
        }
    }
}


} // namespace LinWidget
