#pragma once

#include <QtGlobal>
#include <QJsonObject>
#include <QPoint>
#include <QRect>
#include <QRegion>
#include <QVector>
#include <QString>

class QPainter;
class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class QInputMethodEvent;

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// 导出宏定义
#if defined(LINWIDGET_RUNTIME_EXPORTS)
#  define LINWIDGET_EXPORT extern "C" __declspec(dllexport)
#else
#  define LINWIDGET_EXPORT extern "C" __declspec(dllimport)
#endif

// 主程序向运行时传递的桌面宿主上下文
struct LinWidgetDesktopCtx {
    quintptr desktopWindowHwnd = 0;       // ListView 的 HWND
    void* desktopWidgetPtr = nullptr;      // ListView QWidget*
    void (*onWidgetSettingsRequested)(const char* instanceId, void* userData) = nullptr;
    void (*onImeCursorRectChanged)(int x, int y, int w, int h, void* userData) = nullptr;
    void (*onCanvasNeedsRepaint)(void* userData) = nullptr;
    void (*onRuntimeEnabledChanged)(bool enabled, void* userData) = nullptr;
    void* userData = nullptr;
};

// 纯 C 接口定义（由 LinWidgetRuntime.dll 导出）
#if defined(LINWIDGET_RUNTIME_EXPORTS) || defined(LINWIDGET_RUNTIME_IMPLEMENTATION)
LINWIDGET_EXPORT bool linwidget_is_loaded();
LINWIDGET_EXPORT bool linwidget_init(const LinWidgetDesktopCtx* ctx);
LINWIDGET_EXPORT void linwidget_shutdown();
LINWIDGET_EXPORT void* linwidget_widget_manager();
LINWIDGET_EXPORT void linwidget_render_canvas(QPainter* painter, const QRegion* dirtyRegion);
LINWIDGET_EXPORT bool linwidget_forward_mouse_event(QMouseEvent* event);
LINWIDGET_EXPORT bool linwidget_forward_wheel_event(QWheelEvent* event);
LINWIDGET_EXPORT bool linwidget_forward_key_event(QKeyEvent* event);
LINWIDGET_EXPORT bool linwidget_forward_input_method_event(QInputMethodEvent* event);
LINWIDGET_EXPORT bool linwidget_is_widget_at(int clientX, int clientY);
LINWIDGET_EXPORT int linwidget_widget_at(int clientX, int clientY, char* outBuffer, int bufferSize);
LINWIDGET_EXPORT void linwidget_clear_widget_focus();
LINWIDGET_EXPORT bool linwidget_is_widget_layer_embedded();
LINWIDGET_EXPORT void linwidget_set_widget_layer_embedded(bool embedded);
LINWIDGET_EXPORT int linwidget_get_occupied_rects(int* outRects, int maxRects);
LINWIDGET_EXPORT bool linwidget_get_widget_geometry(const char* instanceId, int* outX, int* outY, int* outW, int* outH);
LINWIDGET_EXPORT bool linwidget_set_widget_geometry(const char* instanceId, int x, int y, int w, int h);
LINWIDGET_EXPORT bool linwidget_is_cef_osr_enabled();
LINWIDGET_EXPORT void linwidget_set_cef_osr_enabled(bool enabled);
LINWIDGET_EXPORT bool linwidget_is_enabled();
LINWIDGET_EXPORT void linwidget_set_enabled(bool enabled);
LINWIDGET_EXPORT bool linwidget_restore_from_config(const char* configPath);
LINWIDGET_EXPORT bool linwidget_save_to_config(const char* configPath);
LINWIDGET_EXPORT bool linwidget_start_widget_package(const char* packagePath, const char* instanceId);
LINWIDGET_EXPORT bool linwidget_stop_widget(const char* instanceId);
LINWIDGET_EXPORT bool linwidget_restart_widget(const char* instanceId);
LINWIDGET_EXPORT bool linwidget_create_widget_instance(const char* sourceInstanceId, int posX, int posY);
LINWIDGET_EXPORT bool linwidget_remove_widget_state(const char* instanceId);
LINWIDGET_EXPORT bool linwidget_show_widget_bounds();
LINWIDGET_EXPORT void linwidget_set_show_widget_bounds(bool show);
LINWIDGET_EXPORT void linwidget_sync_all_widget_window_layers();
LINWIDGET_EXPORT bool linwidget_start_wallpaper(const char* packagePath);
LINWIDGET_EXPORT void linwidget_stop_wallpaper(bool restoreSystemWallpaper);
LINWIDGET_EXPORT bool linwidget_has_active_wallpaper();
LINWIDGET_EXPORT bool linwidget_is_wallpaper_interactive();
LINWIDGET_EXPORT void linwidget_set_wallpaper_interactive(bool interactive);
LINWIDGET_EXPORT bool linwidget_is_wallpaper_suspended();
LINWIDGET_EXPORT void linwidget_set_wallpaper_suspended(bool suspended);
LINWIDGET_EXPORT void linwidget_set_wallpaper_framerate(int fps);
LINWIDGET_EXPORT bool linwidget_forward_mouse_move_to_wallpaper(int clientX, int clientY);
LINWIDGET_EXPORT bool linwidget_forward_mouse_click_to_wallpaper(int clientX, int clientY, int button, bool isRelease, int clickCount);
LINWIDGET_EXPORT bool linwidget_forward_wheel_to_wallpaper(int clientX, int clientY, int deltaX, int deltaY);
LINWIDGET_EXPORT int linwidget_active_wallpaper_package_path(char* outBuffer, int bufferSize);
LINWIDGET_EXPORT int linwidget_active_wallpaper_id(char* outBuffer, int bufferSize);
LINWIDGET_EXPORT bool linwidget_active_wallpaper_is_development();
LINWIDGET_EXPORT bool linwidget_open_widget_settings(const char* packagePath, const char* instanceId);
LINWIDGET_EXPORT bool linwidget_show_devtools(const char* instanceId);
LINWIDGET_EXPORT bool linwidget_validate_package(const char* packagePath, char* outReportBuffer, int bufferSize);
LINWIDGET_EXPORT int linwidget_query_manager(const char* requestJson, char* outResponseBuffer, int bufferSize);
LINWIDGET_EXPORT bool linwidget_create_hub_browser(quintptr parentHwnd, const char* hubUrl, int w, int h);
LINWIDGET_EXPORT void linwidget_resize_hub_browser(int w, int h);
LINWIDGET_EXPORT void linwidget_close_hub_browser();
LINWIDGET_EXPORT bool linwidget_has_hub_browser();
LINWIDGET_EXPORT bool linwidget_open_hub_window(const char* hubUrl);
LINWIDGET_EXPORT QWidget* linwidget_create_hub_web_widget(const char* url, QWidget* parent);
#endif

// 主程序侧持有的动态桥接单例（负责 LoadLibrary、符号解析与判空转发）
class LinWidgetRuntimeBridge {
public:
    static LinWidgetRuntimeBridge& instance();

    bool isLoaded() const;
    bool loadRuntime(const QString& dllPath = QString());
    bool unloadRuntime();

    bool init(const LinWidgetDesktopCtx& ctx);
    void shutdown();
    void* widgetManager();

    void renderCanvas(QPainter* painter, const QRegion& dirtyRegion);
    bool forwardMouseEvent(QMouseEvent* event);
    bool forwardWheelEvent(QWheelEvent* event);
    bool forwardKeyEvent(QKeyEvent* event);
    bool forwardInputMethodEvent(QInputMethodEvent* event);

    bool isWidgetAt(const QPoint& clientPos);
    QString widgetAt(const QPoint& clientPos);
    void clearWidgetFocus();

    bool isWidgetLayerEmbedded() const;
    void setWidgetLayerEmbedded(bool embedded);

    QVector<QRect> occupiedRects();
    QRect widgetGeometry(const QString& instanceId) const;
    bool setWidgetGeometry(const QString& instanceId, const QRect& geometry);

    bool isCefOsrEnabled() const;
    void setCefOsrEnabled(bool enabled);

    bool isEnabled() const;
    void setEnabled(bool enabled);

    using EnabledListener = std::function<void(bool)>;
    void addEnabledListener(void* receiver, EnabledListener listener);
    void removeEnabledListener(void* receiver);
    void notifyEnabledChanged(bool enabled);

    bool restoreFromConfig(const QString& configPath);
    bool saveToConfig(const QString& configPath);
    bool startWidgetPackage(const QString& packagePath, const QString& instanceId = QString());
    bool stopWidget(const QString& instanceId);
    bool restartWidget(const QString& instanceId);
    bool createWidgetInstance(const QString& sourceInstanceId, const QPoint& pos);
    bool removeWidgetState(const QString& instanceId);

    bool showWidgetBounds() const;
    void setShowWidgetBounds(bool show);
    void syncAllWidgetWindowLayers();

    bool startWallpaper(const QString& packagePath);
    void stopWallpaper(bool restoreSystemWallpaper = true);
    bool hasActiveWallpaper() const;
    bool isWallpaperInteractive() const;
    void setWallpaperInteractive(bool interactive);
    bool isWallpaperSuspended() const;
    void setWallpaperSuspended(bool suspended);
    void setWallpaperFramerate(int fps);
    bool forwardMouseMoveToWallpaper(const QPoint& clientPos);
    bool forwardMouseClickToWallpaper(const QPoint& clientPos, Qt::MouseButton button, bool isRelease, int clickCount);
    bool forwardWheelToWallpaper(const QPoint& clientPos, const QPoint& angleDelta);
    QString activeWallpaperPackagePath() const;
    QString activeWallpaperId() const;
    bool activeWallpaperIsDevelopment() const;
    bool openWidgetSettings(const QString& packagePath, const QString& instanceId = QString());
    bool showDevTools(const QString& instanceId);
    bool validatePackage(const QString& packagePath, QString* outReport = nullptr);
    QJsonObject queryWidgetManager(const QJsonObject& request);
    bool createHubBrowser(quintptr parentHwnd, const QString& hubUrl, int w, int h);
    void resizeHubBrowser(int w, int h);
    void closeHubBrowser();
    bool hasHubBrowser();
    bool openHubWindow(const QString& hubUrl = QString());
    QWidget* createHubWebWidget(const QString& url, QWidget* parent = nullptr);

private:
    LinWidgetRuntimeBridge();
    ~LinWidgetRuntimeBridge();
    LinWidgetRuntimeBridge(const LinWidgetRuntimeBridge&) = delete;
    LinWidgetRuntimeBridge& operator=(const LinWidgetRuntimeBridge&) = delete;

    void resetFunctionPointers();

#ifdef Q_OS_WIN
    HMODULE m_hModule = nullptr;
#else
    void* m_hModule = nullptr;
#endif

    // 函数指针定义
    bool (*m_pfnIsLoaded)() = nullptr;
    bool (*m_pfnInit)(const LinWidgetDesktopCtx*) = nullptr;
    void (*m_pfnShutdown)() = nullptr;
    void* (*m_pfnWidgetManager)() = nullptr;
    void (*m_pfnRenderCanvas)(QPainter*, const QRegion*) = nullptr;
    bool (*m_pfnForwardMouseEvent)(QMouseEvent*) = nullptr;
    bool (*m_pfnForwardWheelEvent)(QWheelEvent*) = nullptr;
    bool (*m_pfnForwardKeyEvent)(QKeyEvent*) = nullptr;
    bool (*m_pfnForwardInputMethodEvent)(QInputMethodEvent*) = nullptr;
    bool (*m_pfnIsWidgetAt)(int, int) = nullptr;
    int (*m_pfnWidgetAt)(int, int, char*, int) = nullptr;
    void (*m_pfnClearWidgetFocus)() = nullptr;
    bool (*m_pfnIsWidgetLayerEmbedded)() = nullptr;
    void (*m_pfnSetWidgetLayerEmbedded)(bool) = nullptr;
    int (*m_pfnGetOccupiedRects)(int*, int) = nullptr;
    bool (*m_pfnGetWidgetGeometry)(const char*, int*, int*, int*, int*) = nullptr;
    bool (*m_pfnSetWidgetGeometry)(const char*, int, int, int, int) = nullptr;
    bool (*m_pfnIsCefOsrEnabled)() = nullptr;
    void (*m_pfnSetCefOsrEnabled)(bool) = nullptr;
    bool (*m_pfnIsEnabled)() = nullptr;
    void (*m_pfnSetEnabled)(bool) = nullptr;
    bool (*m_pfnRestoreFromConfig)(const char*) = nullptr;
    bool (*m_pfnSaveToConfig)(const char*) = nullptr;
    bool (*m_pfnStartWidgetPackage)(const char*, const char*) = nullptr;
    bool (*m_pfnStopWidget)(const char*) = nullptr;
    bool (*m_pfnRestartWidget)(const char*) = nullptr;
    bool (*m_pfnCreateWidgetInstance)(const char*, int, int) = nullptr;
    bool (*m_pfnRemoveWidgetState)(const char*) = nullptr;
    bool (*m_pfnShowWidgetBounds)() = nullptr;
    void (*m_pfnSetShowWidgetBounds)(bool) = nullptr;
    void (*m_pfnSyncAllWidgetWindowLayers)() = nullptr;
    bool (*m_pfnStartWallpaper)(const char*) = nullptr;
    void (*m_pfnStopWallpaper)(bool) = nullptr;
    bool (*m_pfnHasActiveWallpaper)() = nullptr;
    bool (*m_pfnIsWallpaperInteractive)() = nullptr;
    void (*m_pfnSetWallpaperInteractive)(bool) = nullptr;
    bool (*m_pfnIsWallpaperSuspended)() = nullptr;
    void (*m_pfnSetWallpaperSuspended)(bool) = nullptr;
    void (*m_pfnSetWallpaperFramerate)(int) = nullptr;
    bool (*m_pfnForwardMouseMoveToWallpaper)(int, int) = nullptr;
    bool (*m_pfnForwardMouseClickToWallpaper)(int, int, int, bool, int) = nullptr;
    bool (*m_pfnForwardWheelToWallpaper)(int, int, int, int) = nullptr;
    int (*m_pfnActiveWallpaperPackagePath)(char*, int) = nullptr;
    int (*m_pfnActiveWallpaperId)(char*, int) = nullptr;
    bool (*m_pfnActiveWallpaperIsDevelopment)() = nullptr;
    bool (*m_pfnOpenWidgetSettings)(const char*, const char*) = nullptr;
    bool (*m_pfnShowDevTools)(const char*) = nullptr;
    bool (*m_pfnValidatePackage)(const char*, char*, int) = nullptr;
    int (*m_pfnQueryManager)(const char*, char*, int) = nullptr;
    bool (*m_pfnCreateHubBrowser)(quintptr, const char*, int, int) = nullptr;
    void (*m_pfnResizeHubBrowser)(int, int) = nullptr;
    void (*m_pfnCloseHubBrowser)() = nullptr;
    bool (*m_pfnHasHubBrowser)() = nullptr;
    bool (*m_pfnOpenHubWindow)(const char*) = nullptr;
    QWidget* (*m_pfnCreateHubWebWidget)(const char*, QWidget*) = nullptr;
    QMap<void*, EnabledListener> m_enabledListeners;
};
