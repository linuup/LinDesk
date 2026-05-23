#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace LinWidget {

namespace IpcType {
    constexpr const char* Launch = "launch";
    constexpr const char* Heartbeat = "heartbeat";
    constexpr const char* Ready = "ready";
    constexpr const char* Error = "error";
    constexpr const char* HostWindowReady = "host_window_ready";
    constexpr const char* WebViewWindowReady = "webview_window_ready";
    constexpr const char* WebViewWindowLost = "webview_window_lost";
    constexpr const char* AddWidget = "add_widget";
    constexpr const char* RemoveWidget = "remove_widget";
    constexpr const char* UpdateWidgetGeometry = "update_widget_geometry";
    constexpr const char* UpdateHostGeometry = "update_host_geometry";
    constexpr const char* UpdateHostOptions = "update_host_options";
    constexpr const char* ReloadWidget = "reload_widget";
    constexpr const char* OpenWidgetSettings = "open_widget_settings";
    constexpr const char* SystemInfoRequest = "system_info_request";
    constexpr const char* SystemInfoResponse = "system_info_response";
    constexpr const char* SystemInfoUpdate = "system_info_update";
    constexpr const char* WidgetGeometryChanged = "widget_geometry_changed";
    constexpr const char* WidgetError = "widget_error";
    constexpr const char* MoveWidget = "move_widget";
    constexpr const char* ResizeWidget = "resize_widget";
    constexpr const char* CloseWidget = "close_widget";
    constexpr const char* ThemeChanged = "theme_changed";
}

QByteArray encodeIpcMessage(const QJsonObject& object);
bool decodeIpcMessage(const QByteArray& line, QJsonObject* object, QString* errorMessage = nullptr);
QJsonObject makeIpcMessage(const QString& type, const QString& widgetId);

}
