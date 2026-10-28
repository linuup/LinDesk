#pragma once

#include <QJsonObject>
#include <QString>

namespace LinWidget {

namespace WidgetMessageType {
    constexpr const char* Launch = "launch";
    constexpr const char* Ready = "ready";
    constexpr const char* Error = "error";
    constexpr const char* HostWindowReady = "host_window_ready";
    constexpr const char* AddWidget = "add_widget";
    constexpr const char* RemoveWidget = "remove_widget";
    constexpr const char* UpdateWidgetGeometry = "update_widget_geometry";
    constexpr const char* UpdateHostGeometry = "update_host_geometry";
    constexpr const char* UpdateHostOptions = "update_host_options";
    constexpr const char* UpdateAlignmentGuides = "update_alignment_guides";
    constexpr const char* ReloadWidget = "reload_widget";
    constexpr const char* OpenWidgetSettings = "open_widget_settings";
    constexpr const char* SystemInfoRequest = "system_info_request";
    constexpr const char* SystemInfoResponse = "system_info_response";
    constexpr const char* SystemInfoUpdate = "system_info_update";
    constexpr const char* StorageRequest = "storage_request";
    constexpr const char* StorageResponse = "storage_response";
    constexpr const char* CreateWidgetInstanceRequest = "create_widget_instance_request";
    constexpr const char* CreateWidgetInstanceResponse = "create_widget_instance_response";
    constexpr const char* ListWidgetInstancesRequest = "list_widget_instances_request";
    constexpr const char* ListWidgetInstancesResponse = "list_widget_instances_response";
    constexpr const char* CloseWidgetInstanceRequest = "close_widget_instance_request";
    constexpr const char* CloseWidgetInstanceResponse = "close_widget_instance_response";
    constexpr const char* RemoveWidgetInstanceRequest = "remove_widget_instance_request";
    constexpr const char* RemoveWidgetInstanceResponse = "remove_widget_instance_response";
    constexpr const char* WidgetGeometryChanged = "widget_geometry_changed";
    constexpr const char* WidgetError = "widget_error";
    constexpr const char* MoveWidget = "move_widget";
    constexpr const char* ResizeWidget = "resize_widget";
    constexpr const char* CloseWidget = "close_widget";
    constexpr const char* ThemeChanged = "theme_changed";
    constexpr const char* WidgetManagerRequest = "widget_manager";
    constexpr const char* WidgetManagerResponse = "widget_manager_response";
}

namespace WidgetErrorCode {
    constexpr const char* Ok = "ok";
    constexpr const char* Timeout = "timeout";
    constexpr const char* NotFound = "not_found";
    constexpr const char* PermissionDenied = "permission_denied";
    constexpr const char* Internal = "internal";
    constexpr const char* NotImplemented = "not_implemented";
}

struct WidgetEnvelope {
    QString id;
    QString type;
    QString widgetId;
    QJsonObject payload;
    QJsonObject error;

    QJsonObject toJson() const;
    static WidgetEnvelope fromJson(const QJsonObject& obj);
};

QJsonObject makeWidgetMessage(const QString& type, const QString& widgetId);
QJsonObject makeWidgetEnvelopeMessage(const QString& type,
                                      const QString& widgetId,
                                      const QString& requestId = QString(),
                                      const QJsonObject& payload = QJsonObject(),
                                      const QString& errorCode = QString(),
                                      const QString& errorMessage = QString());

}
