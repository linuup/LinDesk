#include <QJsonDocument>
#include <QJsonObject>
#include "desktop/widgets/runtime_export.h"

#ifdef LINWIDGET_RUNTIME_EXPORTS

#include "desktop/widgets/linwidget/WidgetManager.h"
#include "desktop/widgets/linwidget/WidgetSettingsDialog.h"
#include "desktop/widgets/linwidget/WidgetPackage.h"
#include "desktop/widgets/linwidget/WidgetValidator.h"
#include "desktop/widgets/cef/CefManager.h"
#include "desktop/widgets/cef/LinWidgetCanvasWidget.h"
#include <QCoreApplication>
#include <QDebug>

static LinWidget::WidgetManager* s_runtimeWidgetManager = nullptr;
static LinWidgetDesktopCtx s_desktopCtx = {};

LINWIDGET_EXPORT bool linwidget_is_loaded()
{
    return true;
}

LINWIDGET_EXPORT bool linwidget_init(const LinWidgetDesktopCtx* ctx)
{
    if (!ctx) {
        return false;
    }
    s_desktopCtx = *ctx;

    if (!s_runtimeWidgetManager) {
        s_runtimeWidgetManager = new LinWidget::WidgetManager(nullptr);
    }

    s_runtimeWidgetManager->setDesktopLayerWindow((WId)ctx->desktopWindowHwnd);

    if (ctx->onWidgetSettingsRequested) {
        QObject::connect(s_runtimeWidgetManager, &LinWidget::WidgetManager::widgetSettingsRequested,
                         [ctx](const QString& instanceId) {
                             if (ctx->onWidgetSettingsRequested) {
                                 const QByteArray utf8 = instanceId.toUtf8();
                                 ctx->onWidgetSettingsRequested(utf8.constData(), ctx->userData);
                             }
                         });
    }

    if (ctx->onImeCursorRectChanged) {
        QObject::connect(s_runtimeWidgetManager, &LinWidget::WidgetManager::imeCursorRectChanged,
                         [ctx](const QRect& r) {
                             if (ctx->onImeCursorRectChanged) {
                                 ctx->onImeCursorRectChanged(r.x(), r.y(), r.width(), r.height(), ctx->userData);
                             }
                         });
    }

    if (ctx->desktopWidgetPtr) {
        LinCef::LinWidgetCanvasWidget::setHostWidget(static_cast<QWidget*>(ctx->desktopWidgetPtr));
    }

    // 启动组件管理器
    s_runtimeWidgetManager->start();
    return true;
}

LINWIDGET_EXPORT void linwidget_shutdown()
{
    LinCef::LinWidgetCanvasWidget::setHostWidget(nullptr);
    if (s_runtimeWidgetManager) {
        s_runtimeWidgetManager->stopAll(true);
        delete s_runtimeWidgetManager;
        s_runtimeWidgetManager = nullptr;
    }
    LinCef::CefManager::instance()->shutdown();
}

LINWIDGET_EXPORT void* linwidget_widget_manager()
{
    return s_runtimeWidgetManager;
}

LINWIDGET_EXPORT void linwidget_render_canvas(QPainter* painter, const QRegion* dirtyRegion)
{
    if (s_runtimeWidgetManager && s_runtimeWidgetManager->canvasWidget() && painter && dirtyRegion) {
        s_runtimeWidgetManager->canvasWidget()->renderCanvas(*painter, *dirtyRegion);
    }
}

LINWIDGET_EXPORT bool linwidget_forward_mouse_event(QMouseEvent* event)
{
    return s_runtimeWidgetManager ? s_runtimeWidgetManager->forwardMouseEvent(event) : false;
}

LINWIDGET_EXPORT bool linwidget_forward_wheel_event(QWheelEvent* event)
{
    return s_runtimeWidgetManager ? s_runtimeWidgetManager->forwardWheelEvent(event) : false;
}

LINWIDGET_EXPORT bool linwidget_forward_key_event(QKeyEvent* event)
{
    return s_runtimeWidgetManager ? s_runtimeWidgetManager->forwardKeyEvent(event) : false;
}

LINWIDGET_EXPORT bool linwidget_forward_input_method_event(QInputMethodEvent* event)
{
    return s_runtimeWidgetManager ? s_runtimeWidgetManager->forwardInputMethodEvent(event) : false;
}

LINWIDGET_EXPORT bool linwidget_is_widget_at(int clientX, int clientY)
{
    return s_runtimeWidgetManager ? s_runtimeWidgetManager->isWidgetAt(QPoint(clientX, clientY)) : false;
}

LINWIDGET_EXPORT int linwidget_widget_at(int clientX, int clientY, char* outBuffer, int bufferSize)
{
    if (!s_runtimeWidgetManager || !outBuffer || bufferSize <= 0) {
        return 0;
    }
    const QString id = s_runtimeWidgetManager->widgetAt(QPoint(clientX, clientY));
    if (id.isEmpty()) {
        outBuffer[0] = '\0';
        return 0;
    }
    const QByteArray utf8 = id.toUtf8();
    const int copyLen = qMin(utf8.size(), bufferSize - 1);
    memcpy(outBuffer, utf8.constData(), copyLen);
    outBuffer[copyLen] = '\0';
    return copyLen;
}

LINWIDGET_EXPORT void linwidget_clear_widget_focus()
{
    if (s_runtimeWidgetManager) {
        s_runtimeWidgetManager->clearWidgetFocus();
    }
}

LINWIDGET_EXPORT bool linwidget_is_widget_layer_embedded()
{
    return s_runtimeWidgetManager ? s_runtimeWidgetManager->isWidgetLayerEmbedded() : true;
}

LINWIDGET_EXPORT void linwidget_set_widget_layer_embedded(bool embedded)
{
    if (s_runtimeWidgetManager) {
        s_runtimeWidgetManager->setWidgetLayerEmbedded(embedded);
    }
}

LINWIDGET_EXPORT int linwidget_get_occupied_rects(int* outRects, int maxRects)
{
    if (!s_runtimeWidgetManager || !s_runtimeWidgetManager->isEnabled() ||
        !s_runtimeWidgetManager->isCefOsrEnabled() || !outRects || maxRects <= 0) {
        return 0;
    }
    int count = 0;
    for (const auto& state : s_runtimeWidgetManager->layoutStates()) {
        if (!state.visible || state.geometry.isEmpty() || !state.geometry.isValid()) {
            continue;
        }
        if (!s_runtimeWidgetManager->hasRunningWidget(state.instanceId)) {
            continue;
        }
        if (count < maxRects) {
            outRects[count * 4 + 0] = state.geometry.x();
            outRects[count * 4 + 1] = state.geometry.y();
            outRects[count * 4 + 2] = state.geometry.width();
            outRects[count * 4 + 3] = state.geometry.height();
            ++count;
        }
    }
    return count;
}

LINWIDGET_EXPORT bool linwidget_is_cef_osr_enabled()
{
    return s_runtimeWidgetManager ? s_runtimeWidgetManager->isCefOsrEnabled() : false;
}

LINWIDGET_EXPORT void linwidget_set_cef_osr_enabled(bool enabled)
{
    if (s_runtimeWidgetManager) {
        s_runtimeWidgetManager->setCefOsrEnabled(enabled);
    }
}

LINWIDGET_EXPORT bool linwidget_is_enabled()
{
    return s_runtimeWidgetManager ? s_runtimeWidgetManager->isEnabled() : false;
}

LINWIDGET_EXPORT void linwidget_set_enabled(bool enabled)
{
    if (s_runtimeWidgetManager) {
        s_runtimeWidgetManager->setEnabled(enabled);
    }
}

LINWIDGET_EXPORT bool linwidget_restore_from_config(const char* configPath)
{
    if (!s_runtimeWidgetManager) return false;
    return s_runtimeWidgetManager->restoreFromConfig(configPath ? QString::fromUtf8(configPath) : QString());
}

LINWIDGET_EXPORT bool linwidget_save_to_config(const char* configPath)
{
    if (!s_runtimeWidgetManager) return false;
    return s_runtimeWidgetManager->saveToConfig(configPath ? QString::fromUtf8(configPath) : QString());
}

LINWIDGET_EXPORT bool linwidget_start_widget_package(const char* packagePath, const char* instanceId)
{
    if (!s_runtimeWidgetManager || !packagePath) return false;
    QString outId;
    return s_runtimeWidgetManager->launchWidgetPackage(
        QString::fromUtf8(packagePath),
        QRect(),
        &outId);
}

LINWIDGET_EXPORT bool linwidget_create_widget_instance(const char* sourceInstanceId, int posX, int posY)
{
    if (!s_runtimeWidgetManager || !sourceInstanceId) return false;
    return s_runtimeWidgetManager->createWidgetInstance(
        QString::fromUtf8(sourceInstanceId),
        QRect(posX, posY, 0, 0));
}

LINWIDGET_EXPORT bool linwidget_remove_widget_state(const char* instanceId)
{
    if (!s_runtimeWidgetManager || !instanceId) return false;
    return s_runtimeWidgetManager->removeWidgetState(QString::fromUtf8(instanceId));
}

LINWIDGET_EXPORT bool linwidget_show_widget_bounds()
{
    return s_runtimeWidgetManager ? s_runtimeWidgetManager->showWidgetBounds() : false;
}

LINWIDGET_EXPORT void linwidget_set_show_widget_bounds(bool show)
{
    if (s_runtimeWidgetManager) {
        s_runtimeWidgetManager->setShowWidgetBounds(show);
    }
}

LINWIDGET_EXPORT void linwidget_sync_all_widget_window_layers()
{
    if (s_runtimeWidgetManager) {
        s_runtimeWidgetManager->syncAllWidgetWindowLayers();
    }
}

LINWIDGET_EXPORT bool linwidget_start_wallpaper(const char* packagePath)
{
    if (!s_runtimeWidgetManager || !packagePath) return false;
    return s_runtimeWidgetManager->startWallpaper(QString::fromUtf8(packagePath), false, false);
}

LINWIDGET_EXPORT bool linwidget_stop_widget(const char* instanceId)
{
    if (!s_runtimeWidgetManager || !instanceId) return false;
    s_runtimeWidgetManager->stopWidget(QString::fromUtf8(instanceId));
    return true;
}

LINWIDGET_EXPORT bool linwidget_restart_widget(const char* instanceId)
{
    if (!s_runtimeWidgetManager || !instanceId) return false;
    return s_runtimeWidgetManager->restartWidget(QString::fromUtf8(instanceId));
}

LINWIDGET_EXPORT void linwidget_stop_wallpaper(bool restoreSystemWallpaper)
{
    if (s_runtimeWidgetManager) {
        s_runtimeWidgetManager->stopWallpaper(restoreSystemWallpaper);
    }
}

LINWIDGET_EXPORT bool linwidget_has_active_wallpaper()
{
    return s_runtimeWidgetManager ? s_runtimeWidgetManager->hasActiveWallpaper() : false;
}

LINWIDGET_EXPORT bool linwidget_is_wallpaper_interactive()
{
    return s_runtimeWidgetManager ? s_runtimeWidgetManager->isWallpaperInteractive() : false;
}

LINWIDGET_EXPORT void linwidget_set_wallpaper_interactive(bool interactive)
{
    if (s_runtimeWidgetManager) {
        s_runtimeWidgetManager->setWallpaperInteractive(interactive);
    }
}

LINWIDGET_EXPORT bool linwidget_forward_mouse_move_to_wallpaper(int clientX, int clientY)
{
    return s_runtimeWidgetManager ? s_runtimeWidgetManager->forwardMouseMoveToWallpaper(QPoint(clientX, clientY)) : false;
}

LINWIDGET_EXPORT bool linwidget_forward_mouse_click_to_wallpaper(int clientX, int clientY, int button, bool isRelease, int clickCount)
{
    return s_runtimeWidgetManager ? s_runtimeWidgetManager->forwardMouseClickToWallpaper(QPoint(clientX, clientY), static_cast<Qt::MouseButton>(button), isRelease, clickCount) : false;
}

LINWIDGET_EXPORT bool linwidget_forward_wheel_to_wallpaper(int clientX, int clientY, int deltaX, int deltaY)
{
    return s_runtimeWidgetManager ? s_runtimeWidgetManager->forwardWheelToWallpaper(QPoint(clientX, clientY), QPoint(deltaX, deltaY)) : false;
}

LINWIDGET_EXPORT int linwidget_active_wallpaper_package_path(char* outBuffer, int bufferSize)
{
    if (!s_runtimeWidgetManager || !outBuffer || bufferSize <= 0) return 0;
    const QString p = s_runtimeWidgetManager->activeWallpaperPackagePath();
    const QByteArray utf8 = p.toUtf8();
    const int copyLen = qMin(utf8.size(), bufferSize - 1);
    memcpy(outBuffer, utf8.constData(), copyLen);
    outBuffer[copyLen] = '\0';
    return copyLen;
}

LINWIDGET_EXPORT int linwidget_active_wallpaper_id(char* outBuffer, int bufferSize)
{
    if (!s_runtimeWidgetManager || !outBuffer || bufferSize <= 0) return 0;
    const QString id = s_runtimeWidgetManager->activeWallpaperId();
    const QByteArray utf8 = id.toUtf8();
    const int copyLen = qMin(utf8.size(), bufferSize - 1);
    memcpy(outBuffer, utf8.constData(), copyLen);
    outBuffer[copyLen] = '\0';
    return copyLen;
}

LINWIDGET_EXPORT bool linwidget_active_wallpaper_is_development()
{
    return s_runtimeWidgetManager ? s_runtimeWidgetManager->activeWallpaperIsDevelopment() : false;
}

LINWIDGET_EXPORT bool linwidget_open_widget_settings(const char* packagePath, const char* instanceId)
{
    if (!packagePath) return false;
    QString pkg = QString::fromUtf8(packagePath);
    QString inst = instanceId ? QString::fromUtf8(instanceId) : QString();
    LinWidget::WidgetManifest manifest;
    QString err;
    if (!LinWidget::WidgetPackage::loadManifest(pkg, &manifest, &err)) {
        return false;
    }
    bool isDev = inst.startsWith(QStringLiteral("dev:"));
    QStringList granted = { QStringLiteral("network"), QStringLiteral("storage"), QStringLiteral("desktop") };
    auto* dialog = LinWidget::WidgetSettingsDialog::openOrCreate(
        pkg, manifest, granted, isDev, inst, nullptr, &err);
    return dialog != nullptr;
}


LINWIDGET_EXPORT bool linwidget_get_widget_geometry(const char* instanceId, int* outX, int* outY, int* outW, int* outH)
{
    if (!s_runtimeWidgetManager || !instanceId) {
        return false;
    }
    const QRect geom = s_runtimeWidgetManager->widgetGeometry(QString::fromUtf8(instanceId));
    if (!geom.isValid()) {
        return false;
    }
    if (outX) *outX = geom.x();
    if (outY) *outY = geom.y();
    if (outW) *outW = geom.width();
    if (outH) *outH = geom.height();
    return true;
}

LINWIDGET_EXPORT bool linwidget_set_widget_geometry(const char* instanceId, int x, int y, int w, int h)
{
    if (!s_runtimeWidgetManager || !instanceId) {
        return false;
    }
    return s_runtimeWidgetManager->setWidgetGeometry(QString::fromUtf8(instanceId), QRect(x, y, w, h));
}

LINWIDGET_EXPORT int linwidget_query_manager(const char* requestJson, char* outResponseBuffer, int bufferSize)
{
    if (!s_runtimeWidgetManager || !requestJson) {
        return 0;
    }
    const QJsonDocument reqDoc = QJsonDocument::fromJson(QByteArray(requestJson));
    if (!reqDoc.isObject()) {
        return 0;
    }
    const QJsonObject resObj = s_runtimeWidgetManager->handleWidgetManagerRequest(reqDoc.object());
    const QByteArray resBytes = QJsonDocument(resObj).toJson(QJsonDocument::Compact);
    if (outResponseBuffer && bufferSize > 0) {
        const int len = qMin(resBytes.size(), bufferSize - 1);
        memcpy(outResponseBuffer, resBytes.constData(), len);
        outResponseBuffer[len] = '\0';
    }
    return resBytes.size();
}

LINWIDGET_EXPORT bool linwidget_show_devtools(const char* instanceId)
{
    if (!s_runtimeWidgetManager || !instanceId) {
        return false;
    }
    return s_runtimeWidgetManager->showDevTools(QString::fromUtf8(instanceId));
}

LINWIDGET_EXPORT bool linwidget_validate_package(const char* packagePath, char* outReportBuffer, int bufferSize)
{
    if (!packagePath) return false;
    const QString pkg = QString::fromUtf8(packagePath);
    LinWidget::ValidationResult res = LinWidget::WidgetValidator::validatePackage(pkg);
    if (outReportBuffer && bufferSize > 0) {
        const QString report = res.toFormattedReport();
        const QByteArray utf8 = report.toUtf8();
        const int copyLen = qMin(utf8.size(), bufferSize - 1);
        memcpy(outReportBuffer, utf8.constData(), copyLen);
        outReportBuffer[copyLen] = '\0';
    }
    return res.ok && !res.hasErrors();
}

#if defined(LINDESK_HAS_CEF)
#include "include/cef_client.h"
#include "include/cef_browser.h"
#include "include/cef_life_span_handler.h"

static CefRefPtr<CefBrowser> s_hubBrowser = nullptr;
static HWND s_hubBrowserHwnd = nullptr;

#include "include/cef_request_handler.h"
#include "include/cef_resource_request_handler.h"
#include "desktop/widgets/linwidget/WidgetManager.h"
#include "services/AppPaths.h"
#include <QDesktopServices>
#include <QUrl>

class HubClientHandler : public CefClient,
                         public CefLifeSpanHandler,
                         public CefRequestHandler,
                         public CefResourceRequestHandler {
public:
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
    CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        CefRefPtr<CefRequest> request,
        bool is_navigation,
        bool is_download,
        const CefString& request_initiator,
        bool& disable_default_handling) override { return this; }

    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
        s_hubBrowser = browser;
        if (browser && browser->GetHost()) {
            s_hubBrowserHwnd = browser->GetHost()->GetWindowHandle();
        }
    }

    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
        s_hubBrowser = nullptr;
        s_hubBrowserHwnd = nullptr;
    }

    ReturnValue OnBeforeResourceLoad(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        CefRefPtr<CefRequest> request,
        CefRefPtr<CefCallback> callback) override {
        if (!request) return RV_CONTINUE;
        std::wstring urlW = request->GetURL().ToWString();
        if (urlW.find(L"bridge/postMessage") != std::wstring::npos) {
            CefRefPtr<CefPostData> postData = request->GetPostData();
            if (postData) {
                CefPostData::ElementVector elements;
                postData->GetElements(elements);
                std::string body;
                for (const auto& elem : elements) {
                    if (elem && elem->GetType() == PDE_TYPE_BYTES) {
                        size_t size = elem->GetBytesCount();
                        if (size > 0) {
                            std::vector<char> buffer(size);
                            elem->GetBytes(size, buffer.data());
                            body.append(buffer.data(), size);
                        }
                    }
                }
                if (!body.empty()) {
                    const QByteArray jsonBytes(body.data(), static_cast<int>(body.size()));
                    const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes);
                    if (doc.isObject()) {
                        const QJsonObject obj = doc.object();
                        const QString action = obj.value(QStringLiteral("action")).toString();
                        const QString widgetId = obj.value(QStringLiteral("widgetId")).toString();
                        const QString path = obj.value(QStringLiteral("packagePath")).toString();

                        QMetaObject::invokeMethod(QCoreApplication::instance(), [action, widgetId, path, obj]() {
                            if (!s_runtimeWidgetManager) return;
                            if (action == QStringLiteral("stop_wallpaper")) {
                                s_runtimeWidgetManager->stopWallpaper(true);
                            } else if (action == QStringLiteral("set_master_enabled")) {
                                s_runtimeWidgetManager->setEnabled(obj.value(QStringLiteral("enabled")).toBool(true));
                            } else if (action == QStringLiteral("set_show_bounds")) {
                                s_runtimeWidgetManager->setShowWidgetBounds(obj.value(QStringLiteral("showBounds")).toBool(false));
                            } else if (action == QStringLiteral("open_folder")) {
                                if (!path.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
                            } else if (action == QStringLiteral("open_widgets_dir")) {
                                QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::userLinWidgetsDir()));
                            } else {
                                s_runtimeWidgetManager->handleWidgetManagerRequest(obj);
                            }
                        }, Qt::QueuedConnection);
                    }
                }
            }
            return RV_CANCEL;
        }
        return RV_CONTINUE;
    }

    IMPLEMENT_REFCOUNTING(HubClientHandler);
};

LINWIDGET_EXPORT bool linwidget_create_hub_browser(quintptr parentHwnd, const char* hubUrl, int w, int h)
{
    if (!LinCef::CefManager::instance()->initialize()) {
        return false;
    }

    if (s_hubBrowser && s_hubBrowserHwnd && IsWindow(s_hubBrowserHwnd)) {
        MoveWindow(s_hubBrowserHwnd, 0, 0, w, h, TRUE);
        return true;
    }

    CefWindowInfo windowInfo;
    CefRect rect(0, 0, w, h);
    windowInfo.SetAsChild(reinterpret_cast<HWND>(parentHwnd), rect);

    CefBrowserSettings settings;
    settings.background_color = 0xff090d16;

    CefRefPtr<HubClientHandler> handler = new HubClientHandler();
    const std::wstring urlW = hubUrl ? QString::fromUtf8(hubUrl).toStdWString() : L"about:blank";
    return CefBrowserHost::CreateBrowser(windowInfo, handler, urlW, settings, nullptr, nullptr);
}

LINWIDGET_EXPORT void linwidget_resize_hub_browser(int w, int h)
{
    if (s_hubBrowserHwnd && IsWindow(s_hubBrowserHwnd)) {
        SetWindowPos(s_hubBrowserHwnd, nullptr, 0, 0, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

LINWIDGET_EXPORT void linwidget_close_hub_browser()
{
    if (s_hubBrowser && s_hubBrowser->GetHost()) {
        s_hubBrowser->GetHost()->CloseBrowser(true);
        s_hubBrowser = nullptr;
        s_hubBrowserHwnd = nullptr;
    }
}

LINWIDGET_EXPORT bool linwidget_has_hub_browser()
{
    return s_hubBrowser != nullptr && s_hubBrowserHwnd != nullptr && IsWindow(s_hubBrowserHwnd);
}

LINWIDGET_EXPORT bool linwidget_open_hub_window(const char* hubUrl)
{
    if (!LinCef::CefManager::instance()->initialize()) {
        return false;
    }

    if (s_hubBrowser && s_hubBrowserHwnd && IsWindow(s_hubBrowserHwnd)) {
        ShowWindow(s_hubBrowserHwnd, SW_RESTORE);
        SetForegroundWindow(s_hubBrowserHwnd);
        return true;
    }

    CefWindowInfo windowInfo;
#ifdef Q_OS_WIN
    const std::wstring title = L"LinDesk Hub · 桌面组件与动态壁纸管理中心";
    windowInfo.SetAsPopup(nullptr, title);
    windowInfo.style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VISIBLE;
    windowInfo.bounds.x = CW_USEDEFAULT;
    windowInfo.bounds.y = CW_USEDEFAULT;
    windowInfo.bounds.width = 1200;
    windowInfo.bounds.height = 780;
#endif

    CefBrowserSettings settings;
    settings.background_color = 0xff070a12;

    CefRefPtr<HubClientHandler> handler = new HubClientHandler();
    const std::wstring urlW = hubUrl ? QString::fromUtf8(hubUrl).toStdWString() : L"about:blank";
    return CefBrowserHost::CreateBrowser(windowInfo, handler, urlW, settings, nullptr, nullptr);
}
#else
LINWIDGET_EXPORT bool linwidget_create_hub_browser(quintptr, const char*, int, int) { return false; }
LINWIDGET_EXPORT void linwidget_resize_hub_browser(int, int) {}
LINWIDGET_EXPORT void linwidget_close_hub_browser() {}
LINWIDGET_EXPORT bool linwidget_has_hub_browser() { return false; }
#endif

#endif // LINWIDGET_RUNTIME_EXPORTS

// ==============================================================================
// 主程序侧 LinWidgetRuntimeBridge 实现
// ==============================================================================

#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDebug>
#include "services/AppPaths.h"

LinWidgetRuntimeBridge::LinWidgetRuntimeBridge()
{
    resetFunctionPointers();
}

LinWidgetRuntimeBridge::~LinWidgetRuntimeBridge()
{
    unloadRuntime();
}

LinWidgetRuntimeBridge& LinWidgetRuntimeBridge::instance()
{
    static LinWidgetRuntimeBridge s_instance;
    return s_instance;
}

void LinWidgetRuntimeBridge::resetFunctionPointers()
{
    m_pfnIsLoaded = nullptr;
    m_pfnInit = nullptr;
    m_pfnShutdown = nullptr;
    m_pfnWidgetManager = nullptr;
    m_pfnRenderCanvas = nullptr;
    m_pfnForwardMouseEvent = nullptr;
    m_pfnForwardWheelEvent = nullptr;
    m_pfnForwardKeyEvent = nullptr;
    m_pfnForwardInputMethodEvent = nullptr;
    m_pfnIsWidgetAt = nullptr;
    m_pfnWidgetAt = nullptr;
    m_pfnClearWidgetFocus = nullptr;
    m_pfnIsWidgetLayerEmbedded = nullptr;
    m_pfnSetWidgetLayerEmbedded = nullptr;
    m_pfnGetOccupiedRects = nullptr;
    m_pfnIsCefOsrEnabled = nullptr;
    m_pfnSetCefOsrEnabled = nullptr;
    m_pfnIsEnabled = nullptr;
    m_pfnSetEnabled = nullptr;
    m_pfnRestoreFromConfig = nullptr;
    m_pfnSaveToConfig = nullptr;
    m_pfnStartWidgetPackage = nullptr;
    m_pfnStopWidget = nullptr;
    m_pfnRestartWidget = nullptr;
    m_pfnCreateWidgetInstance = nullptr;
    m_pfnRemoveWidgetState = nullptr;
    m_pfnShowWidgetBounds = nullptr;
    m_pfnSetShowWidgetBounds = nullptr;
    m_pfnSyncAllWidgetWindowLayers = nullptr;
    m_pfnStartWallpaper = nullptr;
    m_pfnStopWallpaper = nullptr;
    m_pfnHasActiveWallpaper = nullptr;
    m_pfnIsWallpaperInteractive = nullptr;
    m_pfnSetWallpaperInteractive = nullptr;
    m_pfnForwardMouseMoveToWallpaper = nullptr;
    m_pfnForwardMouseClickToWallpaper = nullptr;
    m_pfnForwardWheelToWallpaper = nullptr;
    m_pfnActiveWallpaperPackagePath = nullptr;
    m_pfnActiveWallpaperId = nullptr;
    m_pfnActiveWallpaperIsDevelopment = nullptr;
    m_pfnOpenWidgetSettings = nullptr;
    m_pfnValidatePackage = nullptr;
    m_pfnCreateHubBrowser = nullptr;
    m_pfnResizeHubBrowser = nullptr;
    m_pfnCloseHubBrowser = nullptr;
    m_pfnHasHubBrowser = nullptr;
    m_pfnOpenHubWindow = nullptr;
}

bool LinWidgetRuntimeBridge::isLoaded() const
{
    return m_hModule != nullptr && m_pfnIsLoaded != nullptr && m_pfnIsLoaded();
}

bool LinWidgetRuntimeBridge::loadRuntime(const QString& dllPath)
{
    if (isLoaded()) {
        return true;
    }

    QString targetPath = dllPath;
    if (targetPath.isEmpty()) {
        const QString localRuntimePath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("runtime/linwidget/LinWidgetRuntime.dll"));
        if (QFileInfo::exists(localRuntimePath)) {
            targetPath = localRuntimePath;
        } else {
            const QString localAppPath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("LinWidgetRuntime.dll"));
            if (QFileInfo::exists(localAppPath)) {
                targetPath = localAppPath;
            } else {
                const QString runtimeUserPath = QDir(AppPaths::appDataDir()).filePath(QStringLiteral("runtime/linwidget/LinWidgetRuntime.dll"));
                if (QFileInfo::exists(runtimeUserPath)) {
#ifdef QT_DEBUG
                    qWarning() << "[LinWidgetRuntimeBridge] Running in Debug mode; skipping AppData release runtime DLL to avoid Qt ABI crash.";
#else
                    targetPath = runtimeUserPath;
#endif
                }
            }
        }
    }

    if (targetPath.isEmpty() || !QFileInfo::exists(targetPath)) {
        return false;
    }

#ifdef Q_OS_WIN
    // 添加运行时目录到 DLL 搜索路径，确保 CEF 依赖的 libcef.dll 能被正确定位
    const QString runtimeDir = QFileInfo(targetPath).absolutePath();
    SetDllDirectoryW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(runtimeDir).utf16()));

    HMODULE hMod = LoadLibraryW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(targetPath).utf16()));
    if (!hMod) {
        qWarning() << "[LinWidgetRuntimeBridge] Failed to load DLL:" << targetPath << "GetLastError:" << GetLastError();
        return false;
    }

    m_hModule = hMod;

    // 解析符号
    #define RESOLVE_FN(name, field) \
        field = reinterpret_cast<decltype(field)>(GetProcAddress(m_hModule, name)); \
        if (!field) { qWarning() << "[LinWidgetRuntimeBridge] Missing symbol:" << name; }

    RESOLVE_FN("linwidget_is_loaded", m_pfnIsLoaded);
    RESOLVE_FN("linwidget_init", m_pfnInit);
    RESOLVE_FN("linwidget_shutdown", m_pfnShutdown);
    RESOLVE_FN("linwidget_widget_manager", m_pfnWidgetManager);
    RESOLVE_FN("linwidget_render_canvas", m_pfnRenderCanvas);
    RESOLVE_FN("linwidget_forward_mouse_event", m_pfnForwardMouseEvent);
    RESOLVE_FN("linwidget_forward_wheel_event", m_pfnForwardWheelEvent);
    RESOLVE_FN("linwidget_forward_key_event", m_pfnForwardKeyEvent);
    RESOLVE_FN("linwidget_forward_input_method_event", m_pfnForwardInputMethodEvent);
    RESOLVE_FN("linwidget_is_widget_at", m_pfnIsWidgetAt);
    RESOLVE_FN("linwidget_widget_at", m_pfnWidgetAt);
    RESOLVE_FN("linwidget_clear_widget_focus", m_pfnClearWidgetFocus);
    RESOLVE_FN("linwidget_is_widget_layer_embedded", m_pfnIsWidgetLayerEmbedded);
    RESOLVE_FN("linwidget_set_widget_layer_embedded", m_pfnSetWidgetLayerEmbedded);
    RESOLVE_FN("linwidget_get_occupied_rects", m_pfnGetOccupiedRects);
    RESOLVE_FN("linwidget_is_cef_osr_enabled", m_pfnIsCefOsrEnabled);
    RESOLVE_FN("linwidget_set_cef_osr_enabled", m_pfnSetCefOsrEnabled);
    RESOLVE_FN("linwidget_is_enabled", m_pfnIsEnabled);
    RESOLVE_FN("linwidget_set_enabled", m_pfnSetEnabled);
    RESOLVE_FN("linwidget_restore_from_config", m_pfnRestoreFromConfig);
    RESOLVE_FN("linwidget_save_to_config", m_pfnSaveToConfig);
    RESOLVE_FN("linwidget_start_widget_package", m_pfnStartWidgetPackage);
    RESOLVE_FN("linwidget_stop_widget", m_pfnStopWidget);
    RESOLVE_FN("linwidget_restart_widget", m_pfnRestartWidget);
    RESOLVE_FN("linwidget_create_widget_instance", m_pfnCreateWidgetInstance);
    RESOLVE_FN("linwidget_remove_widget_state", m_pfnRemoveWidgetState);
    RESOLVE_FN("linwidget_show_widget_bounds", m_pfnShowWidgetBounds);
    RESOLVE_FN("linwidget_set_show_widget_bounds", m_pfnSetShowWidgetBounds);
    RESOLVE_FN("linwidget_sync_all_widget_window_layers", m_pfnSyncAllWidgetWindowLayers);
    RESOLVE_FN("linwidget_start_wallpaper", m_pfnStartWallpaper);
    RESOLVE_FN("linwidget_stop_wallpaper", m_pfnStopWallpaper);
    RESOLVE_FN("linwidget_has_active_wallpaper", m_pfnHasActiveWallpaper);
    RESOLVE_FN("linwidget_is_wallpaper_interactive", m_pfnIsWallpaperInteractive);
    RESOLVE_FN("linwidget_set_wallpaper_interactive", m_pfnSetWallpaperInteractive);
    RESOLVE_FN("linwidget_forward_mouse_move_to_wallpaper", m_pfnForwardMouseMoveToWallpaper);
    RESOLVE_FN("linwidget_forward_mouse_click_to_wallpaper", m_pfnForwardMouseClickToWallpaper);
    RESOLVE_FN("linwidget_forward_wheel_to_wallpaper", m_pfnForwardWheelToWallpaper);
    RESOLVE_FN("linwidget_active_wallpaper_package_path", m_pfnActiveWallpaperPackagePath);
    RESOLVE_FN("linwidget_active_wallpaper_id", m_pfnActiveWallpaperId);
    RESOLVE_FN("linwidget_active_wallpaper_is_development", m_pfnActiveWallpaperIsDevelopment);
    RESOLVE_FN("linwidget_open_widget_settings", m_pfnOpenWidgetSettings);
    RESOLVE_FN("linwidget_get_widget_geometry", m_pfnGetWidgetGeometry);
    RESOLVE_FN("linwidget_set_widget_geometry", m_pfnSetWidgetGeometry);
    RESOLVE_FN("linwidget_query_manager", m_pfnQueryManager);
    RESOLVE_FN("linwidget_show_devtools", m_pfnShowDevTools);
    RESOLVE_FN("linwidget_validate_package", m_pfnValidatePackage);
    RESOLVE_FN("linwidget_create_hub_browser", m_pfnCreateHubBrowser);
    RESOLVE_FN("linwidget_resize_hub_browser", m_pfnResizeHubBrowser);
    RESOLVE_FN("linwidget_close_hub_browser", m_pfnCloseHubBrowser);
    RESOLVE_FN("linwidget_has_hub_browser", m_pfnHasHubBrowser);
    RESOLVE_FN("linwidget_open_hub_window", m_pfnOpenHubWindow);

    #undef RESOLVE_FN
#endif

    qInfo() << "[LinWidgetRuntimeBridge] Successfully loaded runtime from:" << targetPath;
    return true;
}

bool LinWidgetRuntimeBridge::unloadRuntime()
{
    if (!m_hModule) {
        return false;
    }
    shutdown();
#ifdef Q_OS_WIN
    FreeLibrary(m_hModule);
#endif
    m_hModule = nullptr;
    resetFunctionPointers();
    return true;
}

bool LinWidgetRuntimeBridge::init(const LinWidgetDesktopCtx& ctx)
{
    return m_pfnInit ? m_pfnInit(&ctx) : false;
}

void LinWidgetRuntimeBridge::shutdown()
{
    if (m_pfnShutdown) {
        m_pfnShutdown();
    }
}

void* LinWidgetRuntimeBridge::widgetManager()
{
    return m_pfnWidgetManager ? m_pfnWidgetManager() : nullptr;
}

void LinWidgetRuntimeBridge::renderCanvas(QPainter* painter, const QRegion& dirtyRegion)
{
    if (m_pfnRenderCanvas) {
        m_pfnRenderCanvas(painter, &dirtyRegion);
    }
}

bool LinWidgetRuntimeBridge::forwardMouseEvent(QMouseEvent* event)
{
    return m_pfnForwardMouseEvent ? m_pfnForwardMouseEvent(event) : false;
}

bool LinWidgetRuntimeBridge::forwardWheelEvent(QWheelEvent* event)
{
    return m_pfnForwardWheelEvent ? m_pfnForwardWheelEvent(event) : false;
}

bool LinWidgetRuntimeBridge::forwardKeyEvent(QKeyEvent* event)
{
    return m_pfnForwardKeyEvent ? m_pfnForwardKeyEvent(event) : false;
}

bool LinWidgetRuntimeBridge::forwardInputMethodEvent(QInputMethodEvent* event)
{
    return m_pfnForwardInputMethodEvent ? m_pfnForwardInputMethodEvent(event) : false;
}

bool LinWidgetRuntimeBridge::isWidgetAt(const QPoint& clientPos)
{
    return m_pfnIsWidgetAt ? m_pfnIsWidgetAt(clientPos.x(), clientPos.y()) : false;
}

QString LinWidgetRuntimeBridge::widgetAt(const QPoint& clientPos)
{
    if (!m_pfnWidgetAt) return QString();
    char buf[256] = {0};
    const int len = m_pfnWidgetAt(clientPos.x(), clientPos.y(), buf, sizeof(buf));
    return len > 0 ? QString::fromUtf8(buf, len) : QString();
}

void LinWidgetRuntimeBridge::clearWidgetFocus()
{
    if (m_pfnClearWidgetFocus) {
        m_pfnClearWidgetFocus();
    }
}

bool LinWidgetRuntimeBridge::isWidgetLayerEmbedded() const
{
    return m_pfnIsWidgetLayerEmbedded ? m_pfnIsWidgetLayerEmbedded() : true;
}

void LinWidgetRuntimeBridge::setWidgetLayerEmbedded(bool embedded)
{
    if (m_pfnSetWidgetLayerEmbedded) {
        m_pfnSetWidgetLayerEmbedded(embedded);
    }
}

QVector<QRect> LinWidgetRuntimeBridge::occupiedRects()
{
    QVector<QRect> result;
    if (!m_pfnGetOccupiedRects) return result;
    int rectData[400] = {0}; // 最多支持 100 个小组件
    const int count = m_pfnGetOccupiedRects(rectData, 100);
    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        result.append(QRect(rectData[i * 4 + 0], rectData[i * 4 + 1], rectData[i * 4 + 2], rectData[i * 4 + 3]));
    }
    return result;
}

bool LinWidgetRuntimeBridge::isCefOsrEnabled() const
{
    return m_pfnIsCefOsrEnabled ? m_pfnIsCefOsrEnabled() : false;
}

void LinWidgetRuntimeBridge::setCefOsrEnabled(bool enabled)
{
    if (m_pfnSetCefOsrEnabled) {
        m_pfnSetCefOsrEnabled(enabled);
    }
}

bool LinWidgetRuntimeBridge::isEnabled() const
{
    return m_pfnIsEnabled ? m_pfnIsEnabled() : false;
}

void LinWidgetRuntimeBridge::setEnabled(bool enabled)
{
    if (m_pfnSetEnabled) {
        m_pfnSetEnabled(enabled);
    }
}

bool LinWidgetRuntimeBridge::restoreFromConfig(const QString& configPath)
{
    if (!m_pfnRestoreFromConfig) return false;
    const QByteArray utf8 = configPath.toUtf8();
    return m_pfnRestoreFromConfig(utf8.isEmpty() ? nullptr : utf8.constData());
}

bool LinWidgetRuntimeBridge::saveToConfig(const QString& configPath)
{
    if (!m_pfnSaveToConfig) return false;
    const QByteArray utf8 = configPath.toUtf8();
    return m_pfnSaveToConfig(utf8.isEmpty() ? nullptr : utf8.constData());
}

bool LinWidgetRuntimeBridge::startWidgetPackage(const QString& packagePath, const QString& instanceId)
{
    if (!m_pfnStartWidgetPackage) return false;
    const QByteArray pkgUtf8 = packagePath.toUtf8();
    const QByteArray idUtf8 = instanceId.toUtf8();
    return m_pfnStartWidgetPackage(pkgUtf8.constData(), idUtf8.isEmpty() ? nullptr : idUtf8.constData());
}

bool LinWidgetRuntimeBridge::createWidgetInstance(const QString& sourceInstanceId, const QPoint& pos)
{
    if (!m_pfnCreateWidgetInstance) return false;
    const QByteArray idUtf8 = sourceInstanceId.toUtf8();
    return m_pfnCreateWidgetInstance(idUtf8.constData(), pos.x(), pos.y());
}

bool LinWidgetRuntimeBridge::removeWidgetState(const QString& instanceId)
{
    if (!m_pfnRemoveWidgetState) return false;
    const QByteArray idUtf8 = instanceId.toUtf8();
    return m_pfnRemoveWidgetState(idUtf8.constData());
}

bool LinWidgetRuntimeBridge::showWidgetBounds() const
{
    return m_pfnShowWidgetBounds ? m_pfnShowWidgetBounds() : false;
}

void LinWidgetRuntimeBridge::setShowWidgetBounds(bool show)
{
    if (m_pfnSetShowWidgetBounds) {
        m_pfnSetShowWidgetBounds(show);
    }
}

void LinWidgetRuntimeBridge::syncAllWidgetWindowLayers()
{
    if (m_pfnSyncAllWidgetWindowLayers) {
        m_pfnSyncAllWidgetWindowLayers();
    }
}

bool LinWidgetRuntimeBridge::startWallpaper(const QString& packagePath)
{
    if (!m_pfnStartWallpaper) return false;
    const QByteArray utf8 = packagePath.toUtf8();
    return m_pfnStartWallpaper(utf8.constData());
}

void LinWidgetRuntimeBridge::stopWallpaper(bool restoreSystemWallpaper)
{
    if (m_pfnStopWallpaper) {
        m_pfnStopWallpaper(restoreSystemWallpaper);
    }
}

bool LinWidgetRuntimeBridge::stopWidget(const QString& instanceId)
{
    if (!m_pfnStopWidget) return false;
    const QByteArray utf8 = instanceId.toUtf8();
    return m_pfnStopWidget(utf8.constData());
}

bool LinWidgetRuntimeBridge::restartWidget(const QString& instanceId)
{
    if (!m_pfnRestartWidget) return false;
    const QByteArray utf8 = instanceId.toUtf8();
    return m_pfnRestartWidget(utf8.constData());
}

bool LinWidgetRuntimeBridge::hasActiveWallpaper() const
{
    return m_pfnHasActiveWallpaper ? m_pfnHasActiveWallpaper() : false;
}

bool LinWidgetRuntimeBridge::isWallpaperInteractive() const
{
    return m_pfnIsWallpaperInteractive ? m_pfnIsWallpaperInteractive() : false;
}

void LinWidgetRuntimeBridge::setWallpaperInteractive(bool interactive)
{
    if (m_pfnSetWallpaperInteractive) {
        m_pfnSetWallpaperInteractive(interactive);
    }
}

bool LinWidgetRuntimeBridge::forwardMouseMoveToWallpaper(const QPoint& clientPos)
{
    return m_pfnForwardMouseMoveToWallpaper ? m_pfnForwardMouseMoveToWallpaper(clientPos.x(), clientPos.y()) : false;
}

bool LinWidgetRuntimeBridge::forwardMouseClickToWallpaper(const QPoint& clientPos, Qt::MouseButton button, bool isRelease, int clickCount)
{
    return m_pfnForwardMouseClickToWallpaper ? m_pfnForwardMouseClickToWallpaper(clientPos.x(), clientPos.y(), static_cast<int>(button), isRelease, clickCount) : false;
}

bool LinWidgetRuntimeBridge::forwardWheelToWallpaper(const QPoint& clientPos, const QPoint& angleDelta)
{
    return m_pfnForwardWheelToWallpaper ? m_pfnForwardWheelToWallpaper(clientPos.x(), clientPos.y(), angleDelta.x(), angleDelta.y()) : false;
}

QString LinWidgetRuntimeBridge::activeWallpaperPackagePath() const
{
    if (!m_pfnActiveWallpaperPackagePath) return QString();
    char buf[1024] = {0};
    m_pfnActiveWallpaperPackagePath(buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

QString LinWidgetRuntimeBridge::activeWallpaperId() const
{
    if (!m_pfnActiveWallpaperId) return QString();
    char buf[512] = {0};
    m_pfnActiveWallpaperId(buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

bool LinWidgetRuntimeBridge::activeWallpaperIsDevelopment() const
{
    return m_pfnActiveWallpaperIsDevelopment ? m_pfnActiveWallpaperIsDevelopment() : false;
}

bool LinWidgetRuntimeBridge::openWidgetSettings(const QString& packagePath, const QString& instanceId)
{
    if (!m_pfnOpenWidgetSettings) return false;
    const QByteArray pkgUtf8 = packagePath.toUtf8();
    const QByteArray instUtf8 = instanceId.toUtf8();
    return m_pfnOpenWidgetSettings(pkgUtf8.constData(), instUtf8.isEmpty() ? nullptr : instUtf8.constData());
}


QRect LinWidgetRuntimeBridge::widgetGeometry(const QString& instanceId) const
{
    if (!m_pfnGetWidgetGeometry) return QRect();
    int x = 0, y = 0, w = 0, h = 0;
    const QByteArray idBytes = instanceId.toUtf8();
    if (m_pfnGetWidgetGeometry(idBytes.constData(), &x, &y, &w, &h)) {
        return QRect(x, y, w, h);
    }
    return QRect();
}

bool LinWidgetRuntimeBridge::setWidgetGeometry(const QString& instanceId, const QRect& geometry)
{
    if (!m_pfnSetWidgetGeometry) return false;
    const QByteArray idBytes = instanceId.toUtf8();
    return m_pfnSetWidgetGeometry(idBytes.constData(), geometry.x(), geometry.y(), geometry.width(), geometry.height());
}

QJsonObject LinWidgetRuntimeBridge::queryWidgetManager(const QJsonObject& request)
{
    if (!m_pfnQueryManager) return QJsonObject();
    const QByteArray reqBytes = QJsonDocument(request).toJson(QJsonDocument::Compact);
    char buf[65536] = {0};
    const int len = m_pfnQueryManager(reqBytes.constData(), buf, sizeof(buf));
    if (len > 0) {
        return QJsonDocument::fromJson(QByteArray(buf, len)).object();
    }
    return QJsonObject();
}

bool LinWidgetRuntimeBridge::showDevTools(const QString& instanceId)
{
    if (!m_pfnShowDevTools) return false;
    const QByteArray instUtf8 = instanceId.toUtf8();
    return m_pfnShowDevTools(instUtf8.constData());
}

bool LinWidgetRuntimeBridge::validatePackage(const QString& packagePath, QString* outReport)
{
    if (!m_pfnValidatePackage) {
        if (outReport) *outReport = QStringLiteral("Runtime is not loaded.");
        return false;
    }
    char buf[8192] = {0};
    const bool ok = m_pfnValidatePackage(packagePath.toUtf8().constData(), buf, sizeof(buf));
    if (outReport) {
        *outReport = QString::fromUtf8(buf);
    }
    return ok;
}


bool LinWidgetRuntimeBridge::createHubBrowser(quintptr parentHwnd, const QString& hubUrl, int w, int h)
{
    if (!m_pfnCreateHubBrowser) return false;
    const QByteArray urlUtf8 = hubUrl.toUtf8();
    return m_pfnCreateHubBrowser(parentHwnd, urlUtf8.constData(), w, h);
}

void LinWidgetRuntimeBridge::resizeHubBrowser(int w, int h)
{
    if (m_pfnResizeHubBrowser) {
        m_pfnResizeHubBrowser(w, h);
    }
}

void LinWidgetRuntimeBridge::closeHubBrowser()
{
    if (m_pfnCloseHubBrowser) {
        m_pfnCloseHubBrowser();
    }
}

bool LinWidgetRuntimeBridge::hasHubBrowser()
{
    return m_pfnHasHubBrowser ? m_pfnHasHubBrowser() : false;
}

bool LinWidgetRuntimeBridge::openHubWindow(const QString& hubUrl)
{
    if (!m_pfnOpenHubWindow) {
        if (!isLoaded()) {
            if (!loadRuntime()) return false;
        }
    }
    if (m_pfnOpenHubWindow) {
        return m_pfnOpenHubWindow(hubUrl.isEmpty() ? nullptr : hubUrl.toUtf8().constData());
    }
    return false;
}
