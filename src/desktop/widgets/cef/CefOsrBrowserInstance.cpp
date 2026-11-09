#include "desktop/widgets/cef/CefOsrBrowserInstance.h"
#include "desktop/widgets/cef/CefOsrRenderHandler.h"
#include "desktop/widgets/cef/CefManager.h"

#include <QDebug>
#include <QApplication>
#ifdef _WIN32
#include <windows.h>
#endif

#ifdef LINDESK_HAS_CEF
#include <include/cef_life_span_handler.h>
#include <include/cef_load_handler.h>
#include <include/cef_context_menu_handler.h>
#include <include/cef_request_handler.h>
#include <include/cef_resource_request_handler.h>
#include <include/cef_display_handler.h>
#include <include/cef_keyboard_handler.h>
#include <include/wrapper/cef_stream_resource_handler.h>
#include <include/cef_stream.h>
#include <QJsonObject>
#include <QJsonDocument>
#include <QUrl>
#include <QUrlQuery>
#include <QFileInfo>
#include <QDir>
#include <QMimeDatabase>

namespace LinCef {

class CefOsrClientHandler : public CefClient,
                            public CefLifeSpanHandler,
                            public CefLoadHandler,
                            public CefContextMenuHandler,
                            public CefRequestHandler,
                            public CefResourceRequestHandler,
                            public CefDisplayHandler,
                            public CefKeyboardHandler
{
public:
    explicit CefOsrClientHandler(CefOsrBrowserInstance* owner, CefRefPtr<CefOsrRenderHandler> renderHandler)
        : m_owner(owner)
        , m_renderHandler(renderHandler)
    {
    }

    void detach()
    {
        m_owner = nullptr;
    }

    // CefClient interfaces
    CefRefPtr<CefRenderHandler> GetRenderHandler() override
    {
        return m_renderHandler;
    }

    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override
    {
        return this;
    }

    CefRefPtr<CefLoadHandler> GetLoadHandler() override
    {
        return this;
    }

    CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override
    {
        return this;
    }

    CefRefPtr<CefRequestHandler> GetRequestHandler() override
    {
        return this;
    }

    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override
    {
        return this;
    }

    CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override
    {
        return this;
    }

    // CefKeyboardHandler interfaces
    bool OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                       const CefKeyEvent& event,
                       CefEventHandle os_event,
                       bool* is_keyboard_shortcut) override
    {
        Q_UNUSED(os_event);
        Q_UNUSED(is_keyboard_shortcut);
        if (event.windows_key_code == 123 /* VK_F12 */ && event.type == KEYEVENT_RAWKEYDOWN) {
            if (m_owner) {
                QMetaObject::invokeMethod(m_owner, [owner = m_owner]() {
                    if (owner) {
                        if (owner->hasDevTools()) {
                            owner->closeDevTools();
                        } else {
                            owner->showDevTools();
                        }
                    }
                }, Qt::QueuedConnection);
            }
            return true;
        }
        if ((event.windows_key_code == 116 /* VK_F5 */ ||
             ((event.windows_key_code == 'R' || event.windows_key_code == 'r') && (event.modifiers & EVENTFLAG_CONTROL_DOWN)))
            && event.type == KEYEVENT_RAWKEYDOWN) {
            if (browser) {
                browser->ReloadIgnoreCache();
            }
            return true;
        }
        return false;
    }

    // CefDisplayHandler interface (console logging)
    bool OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                          cef_log_severity_t level,
                          const CefString& message,
                          const CefString& source,
                          int line) override
    {
        Q_UNUSED(browser);
        Q_UNUSED(level);
        Q_UNUSED(message);
        Q_UNUSED(source);
        Q_UNUSED(line);
        // 完全阻止前端 console.log 跨进程转发至 Qt 线程与磁盘日志，消除高频 CPU 和 I/O 损耗。
        // 如需查看完整调试信息，使用 DevTools。
        return true;
    }

    // CefRequestHandler interfaces
    CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        CefRefPtr<CefRequest> request,
        bool is_navigation,
        bool is_download,
        const CefString& request_initiator,
        bool& disable_default_handling) override
    {
        Q_UNUSED(browser);
        Q_UNUSED(frame);
        Q_UNUSED(request);
        Q_UNUSED(is_navigation);
        Q_UNUSED(is_download);
        Q_UNUSED(request_initiator);
        Q_UNUSED(disable_default_handling);
        return this;
    }

    // CefResourceRequestHandler interfaces (Local package mapping & CORS header injection)
    CefRefPtr<CefResourceHandler> GetResourceHandler(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        CefRefPtr<CefRequest> request) override
    {
        Q_UNUSED(browser);
        Q_UNUSED(frame);
        if (!request || !m_owner) {
            return nullptr;
        }

        const QString urlStr = QString::fromStdWString(request->GetURL().ToWString());
        if (!urlStr.startsWith(QLatin1String("linwidget://"), Qt::CaseInsensitive)) {
            return nullptr;
        }

        const QString pkgPath = m_owner->packagePath();
        if (pkgPath.isEmpty()) {
            return nullptr;
        }

        const QUrl parsedUrl(urlStr);
        QString relativePath = parsedUrl.path();
        if (relativePath.startsWith(QLatin1Char('/'))) {
            relativePath.remove(0, 1);
        }

        // External media route (for wallpaper and media playback)
        if (relativePath.startsWith(QLatin1String("_media")) || parsedUrl.host().compare(QLatin1String("media"), Qt::CaseInsensitive) == 0) {
            QUrlQuery query(parsedUrl);
            QString mediaFilePath = query.queryItemValue(QStringLiteral("path"));
            if (mediaFilePath.isEmpty() && relativePath.length() > 6) {
                mediaFilePath = relativePath.mid(7);
            }
            if (!mediaFilePath.isEmpty()) {
                QFileInfo mediaFi(mediaFilePath);
                if (mediaFi.exists() && mediaFi.isFile()) {
                    CefRefPtr<CefStreamReader> stream = CefStreamReader::CreateForFile(mediaFi.canonicalFilePath().toStdWString());
                    if (stream) {
                        static QMimeDatabase mimeDb;
                        QString mimeType = mimeDb.mimeTypeForFile(mediaFi).name();
                        CefResponse::HeaderMap headers;
                        headers.emplace(L"Access-Control-Allow-Origin", L"*");
                        headers.emplace(L"Access-Control-Allow-Methods", L"GET, POST, OPTIONS");
                        headers.emplace(L"Access-Control-Allow-Headers", L"*");
                        return new CefStreamResourceHandler(200, "OK", mimeType.toStdString(), headers, stream);
                    }
                }
            }
        }

        if (relativePath.isEmpty()) {
            relativePath = QStringLiteral("index.html");
        }

        const QString fullPath = QDir(pkgPath).filePath(relativePath);
        const QFileInfo fi(fullPath);
        const QString canonicalTarget = fi.canonicalFilePath();
        const QString canonicalPkg = QFileInfo(pkgPath).canonicalFilePath();

        // Prevent directory traversal attacks outside the widget package
        if (!canonicalTarget.startsWith(canonicalPkg, Qt::CaseInsensitive)) {
            qWarning() << "[LinWidget][CefOsrBrowserInstance] Path traversal blocked for widget"
                       << m_owner->widgetId() << "target:" << fullPath;
            return nullptr;
        }

        if (!fi.exists() || !fi.isFile()) {
            return nullptr;
        }

        CefRefPtr<CefStreamReader> stream = CefStreamReader::CreateForFile(canonicalTarget.toStdWString());
        if (!stream) {
            return nullptr;
        }

        // Determine MIME type
        static QMimeDatabase mimeDb;
        QString mimeType = mimeDb.mimeTypeForFile(fi).name();
        if (mimeType.isEmpty() || mimeType == QStringLiteral("application/octet-stream")) {
            const QString ext = fi.suffix().toLower();
            if (ext == QLatin1String("html") || ext == QLatin1String("htm")) {
                mimeType = QStringLiteral("text/html");
            } else if (ext == QLatin1String("js") || ext == QLatin1String("mjs")) {
                mimeType = QStringLiteral("application/javascript");
            } else if (ext == QLatin1String("css")) {
                mimeType = QStringLiteral("text/css");
            } else if (ext == QLatin1String("json")) {
                mimeType = QStringLiteral("application/json");
            } else if (ext == QLatin1String("svg")) {
                mimeType = QStringLiteral("image/svg+xml");
            }
        }

        CefResponse::HeaderMap headers;
        headers.emplace(L"Access-Control-Allow-Origin", L"*");
        headers.emplace(L"Access-Control-Allow-Methods", L"GET, POST, OPTIONS");
        headers.emplace(L"Access-Control-Allow-Headers", L"*");

        return new CefStreamResourceHandler(200, "OK", mimeType.toStdString(), headers, stream);
    }

    // CefResourceRequestHandler interfaces (Bridge interception & Network permission sandbox)
    ReturnValue OnBeforeResourceLoad(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        CefRefPtr<CefRequest> request,
        CefRefPtr<CefCallback> callback) override
    {
        Q_UNUSED(browser);
        Q_UNUSED(frame);
        Q_UNUSED(callback);
        if (!request) {
            return RV_CONTINUE;
        }

        std::wstring urlW = request->GetURL().ToWString();
        // 1. Intercept Bridge postMessage (supports both linwidget://bridge/postMessage and http://linwidget.bridge/postMessage)
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
                if (!body.empty() && m_owner) {
                    const QByteArray jsonBytes(body.data(), static_cast<int>(body.size()));
                    const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes);
                    if (doc.isObject()) {
                        const QJsonObject obj = doc.object();
                        QMetaObject::invokeMethod(m_owner, [owner = m_owner, obj]() {
                            if (owner) {
                                owner->onWebMessageReceived(obj);
                            }
                        }, Qt::QueuedConnection);
                    }
                }
            }
            return RV_CANCEL;
        }

        // 2. Network permission sandbox: block remote network access if network permission is not granted
        if (m_owner && !m_owner->allowNetwork()) {
            const QString urlStr = QString::fromStdWString(urlW).trimmed().toLower();
            if (urlStr.startsWith(QLatin1String("http://")) ||
                urlStr.startsWith(QLatin1String("https://")) ||
                urlStr.startsWith(QLatin1String("ws://")) ||
                urlStr.startsWith(QLatin1String("wss://")) ||
                urlStr.startsWith(QLatin1String("ftp://"))) {
                qWarning() << "[LinWidget][CefOsrBrowserInstance] Blocked network request for widget"
                           << m_owner->widgetId() << "url:" << QString::fromStdWString(urlW);
                return RV_CANCEL;
            }
        }

        return RV_CONTINUE;
    }

    // CefContextMenuHandler interfaces
    void OnBeforeContextMenu(CefRefPtr<CefBrowser> browser,
                             CefRefPtr<CefFrame> frame,
                             CefRefPtr<CefContextMenuParams> params,
                             CefRefPtr<CefMenuModel> model) override
    {
        Q_UNUSED(browser);
        Q_UNUSED(frame);
        Q_UNUSED(params);
        if (model) {
            model->Clear();
        }
    }

    bool RunContextMenu(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefContextMenuParams> params,
                        CefRefPtr<CefMenuModel> model,
                        CefRefPtr<CefRunContextMenuCallback> callback) override
    {
        Q_UNUSED(browser);
        Q_UNUSED(frame);
        Q_UNUSED(params);
        Q_UNUSED(model);
        Q_UNUSED(callback);
        return true;
    }

    // CefLifeSpanHandler interfaces
    bool OnBeforePopup(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       int popup_id,
                       const CefString& target_url,
                       const CefString& target_frame_name,
                       CefLifeSpanHandler::WindowOpenDisposition target_disposition,
                       bool user_gesture,
                       const CefPopupFeatures& popupFeatures,
                       CefWindowInfo& windowInfo,
                       CefRefPtr<CefClient>& client,
                       CefBrowserSettings& settings,
                       CefRefPtr<CefDictionaryValue>& extra_info,
                       bool* no_javascript_access) override
    {
        Q_UNUSED(browser);
        Q_UNUSED(frame);
        Q_UNUSED(popup_id);
        Q_UNUSED(target_url);
        Q_UNUSED(target_frame_name);
        Q_UNUSED(target_disposition);
        Q_UNUSED(user_gesture);
        Q_UNUSED(popupFeatures);
        Q_UNUSED(settings);
        Q_UNUSED(extra_info);
        Q_UNUSED(no_javascript_access);
#ifdef Q_OS_WIN
        windowInfo.SetAsPopup(nullptr, "LinWidget Popup");
#endif
        client = this;
        return false;
    }

    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override
    {
        if (m_owner) {
            m_owner->onBrowserAfterCreated(browser);
        }
    }

    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override
    {
        if (m_owner) {
            m_owner->onBrowserBeforeClose(browser);
        }
        m_renderHandler = nullptr;
    }

    // CefLoadHandler interfaces
    void OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, TransitionType transition_type) override
    {
        Q_UNUSED(browser);
        Q_UNUSED(transition_type);
        if (m_owner && frame) {
            m_owner->injectBridgeScript(frame);
        }
    }

    void OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool isLoading, bool canGoBack, bool canGoForward) override
    {
        Q_UNUSED(browser);
        if (m_owner) {
            m_owner->onBrowserLoadingStateChange(isLoading, canGoBack, canGoForward);
        }
    }

    void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode) override
    {
        Q_UNUSED(browser);
        if (m_owner) {
            const QString frameUrl = frame ? QString::fromStdWString(frame->GetURL().ToWString()) : QString();
            m_owner->onBrowserLoadEnd(frameUrl, httpStatusCode);
        }
    }

    void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode errorCode, const CefString& errorText, const CefString& failedUrl) override
    {
        Q_UNUSED(browser);
        Q_UNUSED(frame);
        if (m_owner) {
            m_owner->onBrowserLoadError(static_cast<int>(errorCode),
                                        QString::fromStdWString(errorText.ToWString()),
                                        QString::fromStdWString(failedUrl.ToWString()));
        }
    }

    void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                   TerminationStatus status,
                                   int error_code,
                                   const CefString& error_string) override
    {
        Q_UNUSED(browser);
        if (m_owner) {
            const int s = static_cast<int>(status);
            const QString err = QString::fromStdWString(error_string.ToWString());
            QMetaObject::invokeMethod(m_owner, [owner = m_owner, s, error_code, err]() {
                if (owner) {
                    owner->onRenderProcessTerminated(s, error_code, err);
                }
            }, Qt::QueuedConnection);
        }
    }

private:
    CefOsrBrowserInstance* m_owner = nullptr;
    CefRefPtr<CefOsrRenderHandler> m_renderHandler;
    IMPLEMENT_REFCOUNTING(CefOsrClientHandler);
};

} // namespace LinCef
#endif

namespace LinCef {

CefOsrBrowserInstance::CefOsrBrowserInstance(QString widgetId, QObject* parent)
    : QObject(parent)
    , m_widgetId(std::move(widgetId))
{
}

CefOsrBrowserInstance::~CefOsrBrowserInstance()
{
    close();
}

void CefOsrBrowserInstance::setGeometry(const QRect& geometry)
{
    const bool sizeChanged = (m_geometry.size() != geometry.size());
    m_geometry = geometry;

    if (sizeChanged && m_created) {
        resize(m_geometry.width(), m_geometry.height());
    }
}

void CefOsrBrowserInstance::setVisible(bool visible)
{
    m_visible = visible;
#ifdef LINDESK_HAS_CEF
    if (m_browser && m_browser->GetHost()) {
        m_browser->GetHost()->WasHidden(!visible);
        if (!visible) {
            m_browser->GetHost()->SetWindowlessFrameRate(1);
        } else {
            m_browser->GetHost()->SetWindowlessFrameRate(m_framerate);
        }
    }
#endif
}

void CefOsrBrowserInstance::setFramerate(int fps)
{
    m_framerate = qBound(1, fps, 144);
#ifdef LINDESK_HAS_CEF
    if (m_browser && m_browser->GetHost()) {
        m_browser->GetHost()->SetWindowlessFrameRate(m_framerate);
    }
#endif
}

bool CefOsrBrowserInstance::create(const QString& url, const QRect& geometry)
{
    if (m_created) {
        return true;
    }

    m_geometry = geometry;

    if (!CefManager::instance()->initialize()) {
        qWarning() << "[LinWidget][CefOsrBrowserInstance] Cannot create browser, CEF runtime not available for:" << m_widgetId;
        return false;
    }

#ifdef LINDESK_HAS_CEF
    m_renderHandler = new CefOsrRenderHandler(m_widgetId, m_geometry.width(), m_geometry.height());
    connect(m_renderHandler.get(), &CefOsrRenderHandler::frameReady, this,
            [this](const QString& wid, const QImage& image, const QVector<QRect>& dirtyRects) {
                if (m_visible) {
                    emit frameReady(wid, m_geometry, image, dirtyRects);
                }
            });
    connect(m_renderHandler.get(), &CefOsrRenderHandler::imeCompositionRangeChanged, this,
            [this](const QString& wid, const QRect& cursorRect) {
                emit imeCompositionRangeChanged(wid, cursorRect);
            });

    m_clientHandler = new CefOsrClientHandler(this, m_renderHandler);

    CefWindowInfo windowInfo;
    windowInfo.SetAsWindowless(nullptr);
    windowInfo.shared_texture_enabled = 0;

    CefBrowserSettings browserSettings;
    browserSettings.windowless_frame_rate = m_framerate;
    browserSettings.background_color = 0x00000000; // Transparent background

    const std::wstring urlW = url.toStdWString();
    const bool success = CefBrowserHost::CreateBrowser(windowInfo, m_clientHandler, urlW, browserSettings, nullptr, nullptr);
    if (!success) {
        qCritical() << "[LinWidget][CefOsrBrowserInstance] CefBrowserHost::CreateBrowser failed for:" << m_widgetId;
        m_clientHandler->detach();
        m_clientHandler = nullptr;
        m_renderHandler = nullptr;
        return false;
    }

    m_created = true;
    qInfo() << "[LinWidget][CefOsrBrowserInstance] Browser creation initiated for:" << m_widgetId << "url=" << url;
    return true;
#else
    Q_UNUSED(url);
    return false;
#endif
}

void CefOsrBrowserInstance::close()
{
    if (!m_created) {
        return;
    }
    m_created = false;

#ifdef LINDESK_HAS_CEF
    if (m_clientHandler) {
        m_clientHandler->detach();
    }
    if (m_browser && m_browser->GetHost()) {
        m_browser->GetHost()->CloseBrowser(true);
    }
    m_browser = nullptr;
    m_renderHandler = nullptr;
    m_clientHandler = nullptr;
#endif

    emit browserClosed(m_widgetId);
}

void CefOsrBrowserInstance::loadUrl(const QString& url)
{
#ifdef LINDESK_HAS_CEF
    if (m_browser && m_browser->GetMainFrame()) {
        m_browser->GetMainFrame()->LoadURL(url.toStdWString());
    }
#else
    Q_UNUSED(url);
#endif
}

void CefOsrBrowserInstance::resize(int width, int height)
{
    m_geometry.setSize(QSize(qMax(1, width), qMax(1, height)));

#ifdef LINDESK_HAS_CEF
    if (m_renderHandler) {
        m_renderHandler->resize(m_geometry.width(), m_geometry.height());
    }
    if (m_browser && m_browser->GetHost()) {
        m_browser->GetHost()->WasResized();
    }
#endif
}

void CefOsrBrowserInstance::sendMouseClick(int x, int y, Qt::MouseButton button, bool mouseUp, int clickCount)
{
#ifdef LINDESK_HAS_CEF
    if (!m_browser || !m_browser->GetHost()) {
        return;
    }

    uint32_t modifiers = 0;
    const Qt::KeyboardModifiers qMods = QApplication::keyboardModifiers();
#ifdef _WIN32
    if ((qMods & Qt::ShiftModifier) || ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0)) modifiers |= EVENTFLAG_SHIFT_DOWN;
    if ((qMods & Qt::ControlModifier) || ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0)) modifiers |= EVENTFLAG_CONTROL_DOWN;
    if ((qMods & Qt::AltModifier) || ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0)) modifiers |= EVENTFLAG_ALT_DOWN;
#else
    if (qMods & Qt::ShiftModifier) modifiers |= EVENTFLAG_SHIFT_DOWN;
    if (qMods & Qt::ControlModifier) modifiers |= EVENTFLAG_CONTROL_DOWN;
    if (qMods & Qt::AltModifier) modifiers |= EVENTFLAG_ALT_DOWN;
#endif
    if (button == Qt::LeftButton) modifiers |= EVENTFLAG_LEFT_MOUSE_BUTTON;
    else if (button == Qt::RightButton) modifiers |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
    else if (button == Qt::MiddleButton) modifiers |= EVENTFLAG_MIDDLE_MOUSE_BUTTON;

    CefMouseEvent mouseEvent;
    mouseEvent.x = x;
    mouseEvent.y = y;
    mouseEvent.modifiers = modifiers;

    cef_mouse_button_type_t btnType = MBT_LEFT;
    if (button == Qt::RightButton) {
        btnType = MBT_RIGHT;
    } else if (button == Qt::MiddleButton) {
        btnType = MBT_MIDDLE;
    }

    m_browser->GetHost()->SendMouseClickEvent(mouseEvent, btnType, mouseUp, clickCount);
#else
    Q_UNUSED(x); Q_UNUSED(y); Q_UNUSED(button); Q_UNUSED(mouseUp); Q_UNUSED(clickCount);
#endif
}

void CefOsrBrowserInstance::sendMouseMove(int x, int y, bool mouseLeave)
{
#ifdef LINDESK_HAS_CEF
    if (!m_browser || !m_browser->GetHost()) {
        return;
    }

    uint32_t modifiers = 0;
    const Qt::MouseButtons btns = QApplication::mouseButtons();
    if (btns & Qt::LeftButton) modifiers |= EVENTFLAG_LEFT_MOUSE_BUTTON;
    if (btns & Qt::RightButton) modifiers |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
    if (btns & Qt::MiddleButton) modifiers |= EVENTFLAG_MIDDLE_MOUSE_BUTTON;

    CefMouseEvent mouseEvent;
    mouseEvent.x = x;
    mouseEvent.y = y;
    mouseEvent.modifiers = modifiers;

    m_browser->GetHost()->SendMouseMoveEvent(mouseEvent, mouseLeave);
#else
    Q_UNUSED(x); Q_UNUSED(y); Q_UNUSED(mouseLeave);
#endif
}

void CefOsrBrowserInstance::sendMouseWheel(int x, int y, int deltaX, int deltaY)
{
#ifdef LINDESK_HAS_CEF
    if (!m_browser || !m_browser->GetHost()) {
        return;
    }

    CefMouseEvent mouseEvent;
    mouseEvent.x = x;
    mouseEvent.y = y;
    mouseEvent.modifiers = 0;

    m_browser->GetHost()->SendMouseWheelEvent(mouseEvent, deltaX, deltaY);
#else
    Q_UNUSED(x); Q_UNUSED(y); Q_UNUSED(deltaX); Q_UNUSED(deltaY);
#endif
}

void CefOsrBrowserInstance::setFocus(bool focus)
{
#ifdef LINDESK_HAS_CEF
    if (m_browser && m_browser->GetHost()) {
        m_browser->GetHost()->SetFocus(focus);
    }
#else
    Q_UNUSED(focus);
#endif
}

void CefOsrBrowserInstance::sendKeyEvent(QKeyEvent* event)
{
#ifdef LINDESK_HAS_CEF
    if (!m_browser || !m_browser->GetHost() || !event) {
        return;
    }

    if (event->key() == Qt::Key_F12) {
        if (event->type() == QEvent::KeyPress) {
            if (hasDevTools()) {
                closeDevTools();
            } else {
                showDevTools();
            }
        }
        return;
    }

    uint32_t modifiers = 0;
    const Qt::KeyboardModifiers qMods = event->modifiers() | QApplication::keyboardModifiers();
#ifdef _WIN32
    if ((qMods & Qt::ShiftModifier) || event->key() == Qt::Key_Shift || ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0)) modifiers |= EVENTFLAG_SHIFT_DOWN;
    if ((qMods & Qt::ControlModifier) || event->key() == Qt::Key_Control || ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0)) modifiers |= EVENTFLAG_CONTROL_DOWN;
    if ((qMods & Qt::AltModifier) || event->key() == Qt::Key_Alt || ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0)) modifiers |= EVENTFLAG_ALT_DOWN;
#else
    if ((qMods & Qt::ShiftModifier) || event->key() == Qt::Key_Shift) modifiers |= EVENTFLAG_SHIFT_DOWN;
    if ((qMods & Qt::ControlModifier) || event->key() == Qt::Key_Control) modifiers |= EVENTFLAG_CONTROL_DOWN;
    if ((qMods & Qt::AltModifier) || event->key() == Qt::Key_Alt) modifiers |= EVENTFLAG_ALT_DOWN;
#endif

    CefKeyEvent cefEvent;
    cefEvent.modifiers = modifiers;
    cefEvent.windows_key_code = static_cast<int>(event->nativeVirtualKey());
    cefEvent.native_key_code = static_cast<int>(event->nativeScanCode());
    cefEvent.is_system_key = (event->modifiers() & Qt::AltModifier) ? 1 : 0;

    if (event->type() == QEvent::KeyPress) {
        cefEvent.type = KEYEVENT_RAWKEYDOWN;
        m_browser->GetHost()->SendKeyEvent(cefEvent);

        const QString text = event->text();
        if (!text.isEmpty()) {
            for (const QChar& ch : text) {
                CefKeyEvent charEvent;
                charEvent.type = KEYEVENT_CHAR;
                charEvent.modifiers = modifiers;
                charEvent.windows_key_code = ch.unicode();
                charEvent.native_key_code = static_cast<int>(event->nativeScanCode());
                charEvent.character = ch.unicode();
                charEvent.unmodified_character = ch.unicode();
                m_browser->GetHost()->SendKeyEvent(charEvent);
            }
        }
    } else if (event->type() == QEvent::KeyRelease) {
        cefEvent.type = KEYEVENT_KEYUP;
        m_browser->GetHost()->SendKeyEvent(cefEvent);
    }
#else
    Q_UNUSED(event);
#endif
}

void CefOsrBrowserInstance::sendInputMethodEvent(QInputMethodEvent* event)
{
#ifdef LINDESK_HAS_CEF
    if (!m_browser || !m_browser->GetHost() || !event) {
        return;
    }

    const QString commit = event->commitString();
    const QString preedit = event->preeditString();

    if (!commit.isEmpty()) {
        m_browser->GetHost()->ImeCommitText(commit.toStdWString(), CefRange(), 0);
    } else if (!preedit.isEmpty()) {
        std::vector<CefCompositionUnderline> underlines;
        m_browser->GetHost()->ImeSetComposition(
            preedit.toStdWString(),
            underlines,
            CefRange(),
            CefRange(static_cast<int>(preedit.length()), static_cast<int>(preedit.length()))
        );
    } else {
        m_browser->GetHost()->ImeCancelComposition();
    }
#else
    Q_UNUSED(event);
#endif
}

void CefOsrBrowserInstance::onRenderProcessTerminated(int status, int errorCode, const QString& errorString)
{
    qWarning() << "[LinWidget][CefOsrBrowserInstance] Render process terminated for widget:" << m_widgetId
               << "status:" << status << "code:" << errorCode << "reason:" << errorString;
    emit browserCrashed(m_widgetId, status, errorCode, errorString);
}

void CefOsrBrowserInstance::onConsoleMessage(int level, const QString& message, const QString& source, int line)
{
    // 不再向 linwidget.log 写入任何 console 日志，消除无谓的 I/O 和格式化损耗。
    // 如需调试，直接打开 DevTools。
    emit consoleMessageReceived(m_widgetId, level, message, source, line);
}

void CefOsrBrowserInstance::sendMessageToWeb(const QJsonObject& message)
{
#ifdef LINDESK_HAS_CEF
    if (!m_browser || !m_browser->GetMainFrame()) {
        return;
    }
    const QByteArray jsonBytes = QJsonDocument(message).toJson(QJsonDocument::Compact);
    const QString encoded = QString::fromLatin1(QUrl::toPercentEncoding(QString::fromUtf8(jsonBytes)));

    const QString jsCode = QStringLiteral(
        "(function() {"
        "  try {"
        "    const data = JSON.parse(decodeURIComponent('%1'));"
        "    if (window.__linwidget_dispatch_message) {"
        "      window.__linwidget_dispatch_message(data);"
        "    } else if (window.chrome && window.chrome.webview && window.chrome.webview.__dispatch) {"
        "      window.chrome.webview.__dispatch(data);"
        "    }"
        "  } catch (e) { console.error('[LinDesk Bridge] dispatch failed:', e); }"
        "})();"
    ).arg(encoded);

    m_browser->GetMainFrame()->ExecuteJavaScript(jsCode.toStdWString(), m_browser->GetMainFrame()->GetURL(), 0);
#else
    Q_UNUSED(message);
#endif
}

void CefOsrBrowserInstance::onWebMessageReceived(const QJsonObject& message)
{
    emit messageReceived(m_widgetId, message);
}

#ifdef LINDESK_HAS_CEF
void CefOsrBrowserInstance::injectBridgeScript(CefRefPtr<CefFrame> frame)
{
    if (!frame) {
        return;
    }

    const QString escapedWidgetId = QString(m_widgetId)
        .replace(QLatin1Char('\\'), QLatin1String("\\\\"))
        .replace(QLatin1Char('\''), QLatin1String("\\'"));

    const QString jsPolyfill = QString(
        "(function() {"
        "  if (window.__linwidget_bridge_injected) return;"
        "  window.__linwidget_bridge_injected = true;"
        "  const widgetId = '%1';"
        "  const isWallpaper = (widgetId === '__wallpaper__' || widgetId.indexOf('wallpaper:') === 0);"
        "  const messageListeners = [];"
        "  let reqSeq = 0;"
        "  const pendingRequests = new Map();"
        "  const systemInfoWatchers = new Map();"
        "  const wallpaperPropertyWatchers = [];"
        "  const postMessageBridge = function(data) {"
        "    try {"
        "      const body = (typeof data === 'string') ? data : JSON.stringify(data);"
        "      const xhr = new XMLHttpRequest();"
        "      xhr.open('POST', 'linwidget://bridge/postMessage', true);"
        "      xhr.setRequestHeader('Content-Type', 'application/json');"
        "      xhr.send(body);"
        "    } catch (e) { console.error('[LinDesk Bridge] Failed to post message:', e); }"
        "  };"
        "  const sendRequest = function(type, payload) {"
        "    return new Promise(function(resolve, reject) {"
        "      const requestId = 'req-' + Date.now() + '-' + (++reqSeq);"
        "      pendingRequests.set(requestId, { resolve: resolve, reject: reject });"
        "      const msg = Object.assign({"
        "        type: type,"
        "        requestId: requestId,"
        "        instanceId: widgetId,"
        "        widgetId: widgetId"
        "      }, (payload && typeof payload === 'object') ? payload : {});"
        "      postMessageBridge(msg);"
        "    });"
        "  };"
        "  window.__linwidget_dispatch_message = function(data) {"
        "    if (data && typeof data === 'object') {"
        "      const reqId = data.requestId;"
        "      if (reqId && pendingRequests.has(reqId)) {"
        "        const handler = pendingRequests.get(reqId);"
        "        pendingRequests.delete(reqId);"
        "        if (data.ok === false) {"
        "          handler.reject(new Error(data.error || 'Request failed'));"
        "        } else {"
        "          handler.resolve(data.data !== undefined ? data.data : data);"
        "        }"
        "      }"
        "      if (data.type === 'system_info_update') {"
        "        const wid = data.watchId;"
        "        if (wid && systemInfoWatchers.has(wid)) {"
        "          const cb = systemInfoWatchers.get(wid);"
        "          try { cb(data.data || {}); }"
        "          catch (e) { console.error('[LinDesk Bridge] Error in system_info_update callback:', e); }"
        "        }"
        "      }"
        "      if (data.type === 'wallpaper_property_change' || data.type === 'wallpaper_config_update' || data.type === 'wallpaper_property') {"
        "        const prop = { key: data.key || data.property, value: data.value };"
        "        if (prop.key === 'blur') {"
        "          const b = parseFloat(prop.value) || 0;"
        "          const targetEl = document.querySelector('canvas, video, img') || document.body;"
        "          if (targetEl && targetEl.style) {"
        "            targetEl.style.filter = b > 0 ? ('blur(' + b + 'px)') : 'none';"
        "            targetEl.style.transform = b > 0 ? 'scale(1.04)' : 'scale(1)';"
        "            targetEl.style.transition = 'filter 0.15s ease, transform 0.15s ease';"
        "          }"
        "        } else if (prop.key === 'dimming') {"
        "          const d = parseFloat(prop.value) || 0;"
        "          const opacityVal = (d > 1 ? (d / 100.0) : d);"
        "          let dimEl = document.getElementById('dimLayer') || document.getElementById('__lindesk_dim_layer__');"
        "          if (!dimEl && document.body) {"
        "            dimEl = document.createElement('div');"
        "            dimEl.id = '__lindesk_dim_layer__';"
        "            dimEl.style.cssText = 'position:fixed;inset:0;background:#000;opacity:0;pointer-events:none;z-index:9999;transition:opacity 0.15s ease;';"
        "            document.body.appendChild(dimEl);"
        "          }"
        "          if (dimEl) { dimEl.style.opacity = String(opacityVal); }"
        "        } else if (prop.key === 'scaling' || prop.key === 'fitMode') {"
        "          const mediaEl = document.querySelector('video, img');"
        "          if (mediaEl && mediaEl.style && prop.value) { mediaEl.style.objectFit = String(prop.value); }"
        "        } else if (prop.key === 'volume') {"
        "          const vEl = document.querySelector('video, audio');"
        "          if (vEl) { const vol = Math.max(0, Math.min(1, (parseFloat(prop.value) || 0) / 100.0)); vEl.volume = vol; vEl.muted = (vol <= 0); }"
        "        } else if (prop.key === 'playbackRate') {"
        "          const vEl = document.querySelector('video, audio');"
        "          if (vEl) { vEl.playbackRate = Math.max(0.25, Math.min(3.0, parseFloat(prop.value) || 1.0)); }"
        "        }"
        "        wallpaperPropertyWatchers.forEach(function(cb) {"
        "          try { cb(prop); } catch(e) { console.error('[LinDesk Wallpaper] Error in property watcher:', e); }"
        "        });"
        "      }"
        "    }"
        "    const event = new MessageEvent('message', { data: data });"
        "    messageListeners.forEach(listener => {"
        "      try {"
        "        if (typeof listener === 'function') { listener(event); }"
        "        else if (listener && typeof listener.handleEvent === 'function') { listener.handleEvent(event); }"
        "      } catch (e) { console.error('[LinDesk Bridge] Error in listener:', e); }"
        "    });"
        "    window.dispatchEvent(event);"
        "  };"
        "  if (!window.chrome) { window.chrome = {}; }"
        "  if (!window.chrome.webview) {"
        "    window.chrome.webview = {"
        "      postMessage: postMessageBridge,"
        "      addEventListener: function(type, listener) {"
        "        if (type === 'message') { messageListeners.push(listener);"
        "        } else { window.addEventListener(type, listener); }"
        "      },"
        "      removeEventListener: function(type, listener) {"
        "        if (type === 'message') {"
        "          const idx = messageListeners.indexOf(listener);"
        "          if (idx !== -1) messageListeners.splice(idx, 1);"
        "        } else { window.removeEventListener(type, listener); }"
        "      },"
        "      __dispatch: window.__linwidget_dispatch_message"
        "    };"
        "  }"
        "  if (!window.linwidget) {"
        "    window.linwidget = {"
        "      widgetId: widgetId,"
        "      postMessage: postMessageBridge,"
        "      addEventListener: window.chrome.webview.addEventListener,"
        "      removeEventListener: window.chrome.webview.removeEventListener"
        "    };"
        "  }"
        "  const systemInfoApi = {"
        "    getSnapshot: function() { return sendRequest('system_info_request', { action: 'snapshot' }); },"
        "    getCpu: function() { return sendRequest('system_info_request', { action: 'cpu' }); },"
        "    getMemory: function() { return sendRequest('system_info_request', { action: 'memory' }); },"
        "    getGpu: function() { return sendRequest('system_info_request', { action: 'gpu' }); },"
        "    getDisk: function() { return sendRequest('system_info_request', { action: 'disk' }); },"
        "    getNetwork: function() { return sendRequest('system_info_request', { action: 'network' }); },"
        "    getBattery: function() { return sendRequest('system_info_request', { action: 'battery' }); },"
        "    getOs: function() { return sendRequest('system_info_request', { action: 'os' }); },"
        "    getUptime: function() { return sendRequest('system_info_request', { action: 'uptime' }); },"
        "    watch: function(callback, intervalMs) {"
        "      const watchId = 'watch-' + Date.now() + '-' + (++reqSeq);"
        "      if (typeof callback === 'function') { systemInfoWatchers.set(watchId, callback); }"
        "      sendRequest('system_info_request', {"
        "        action: 'watch',"
        "        watchId: watchId,"
        "        intervalMs: Number(intervalMs) || 1000"
        "      }).then(function(initialData) {"
        "        const cb = systemInfoWatchers.get(watchId);"
        "        if (cb && initialData) { try { cb(initialData); } catch (e) { console.error(e); } }"
        "      }).catch(function(err) {"
        "        console.error('[LinDesk Bridge] systemInfo.watch failed:', err);"
        "      });"
        "      return watchId;"
        "    },"
        "    unwatch: function(watchId) {"
        "      const id = String(watchId || '');"
        "      systemInfoWatchers.delete(id);"
        "      return sendRequest('system_info_request', { action: 'unwatch', watchId: id });"
        "    }"
        "  };"
        "  const desktopApi = (window.desktop && typeof window.desktop === 'object') ? window.desktop : {};"
        "  desktopApi.context = isWallpaper ? 'wallpaper' : 'widget';"
        "  desktopApi.widgetId = widgetId;"
        "  desktopApi.instanceId = widgetId;"
        "  desktopApi.ready = Promise.resolve();"
        "  desktopApi.systemInfo = systemInfoApi;"
        "  desktopApi.resizeWidget = function(size) {"
        "    postMessageBridge({"
        "      type: 'resize_widget',"
        "      instanceId: widgetId,"
        "      widgetId: widgetId,"
        "      width: size ? size.width : undefined,"
        "      height: size ? size.height : undefined"
        "    });"
        "  };"
        "  desktopApi.setWidgetGeometry = function(geom) {"
        "    postMessageBridge(Object.assign({"
        "      type: 'widget_geometry_changed',"
        "      instanceId: widgetId,"
        "      widgetId: widgetId"
        "    }, geom || {}));"
        "  };"
        "  desktopApi.setResizeUiVisible = function(visible) {"
        "    postMessageBridge({"
        "      type: 'set_resize_ui_visible',"
        "      instanceId: widgetId,"
        "      widgetId: widgetId,"
        "      visible: !!visible"
        "    });"
        "  };"
        "  desktopApi.openSettings = function() {"
        "    postMessageBridge({"
        "      type: 'open_widget_settings',"
        "      instanceId: widgetId,"
        "      widgetId: widgetId"
        "    });"
        "  };"
        "  desktopApi.instances = {"
        "    create: function(options) {"
        "      return sendRequest('create_widget_instance_request', (options && typeof options === 'object') ? { geometry: options } : {});"
        "    },"
        "    list: function() {"
        "      return sendRequest('list_widget_instances_request', {});"
        "    },"
        "    close: function(targetId) {"
        "      return sendRequest('close_widget_instance_request', { targetInstanceId: targetId || widgetId });"
        "    },"
        "    remove: function(targetId) {"
        "      return sendRequest('remove_widget_instance_request', { targetInstanceId: targetId || widgetId });"
        "    }"
        "  };"
        "  desktopApi.instance = {"
        "    close: function() { return sendRequest('close_widget_instance_request', { targetInstanceId: widgetId }); },"
        "    remove: function() { return sendRequest('remove_widget_instance_request', { targetInstanceId: widgetId }); },"
        "    multiInstance: true"
        "  };"
        "  window.desktop = desktopApi;"
        "  window.LinDesk = {"
        "    version: '2.0.0',"
        "    type: isWallpaper ? 'wallpaper' : 'widget',"
        "    system: {"
        "      getMetrics: function() {"
        "        return systemInfoApi.getSnapshot().then(function(res) {"
        "          return {"
        "            cpu: (res && typeof res.cpu === 'object') ? (res.cpu.usagePercent || 0) : 0,"
        "            memory: (res && typeof res.memory === 'object') ? {"
        "              usedMB: res.memory.usedMB || Math.round((res.memory.totalMB || 0) * (res.memory.percent || 0) / 100),"
        "              totalMB: res.memory.totalMB || 0,"
        "              percent: res.memory.percent || 0"
        "            } : { usedMB: 0, totalMB: 0, percent: 0 },"
        "            battery: (res && typeof res.battery === 'object') ? (res.battery.percent || 100) : 100,"
        "            network: (res && typeof res.network === 'object') ? {"
        "              upSpeed: res.network.upSpeed || 0,"
        "              downSpeed: res.network.downSpeed || 0"
        "            } : { upSpeed: 0, downSpeed: 0 },"
        "            uptime: (res && res.uptime) ? res.uptime.uptimeSeconds : 0,"
        "            raw: res"
        "          };"
        "        });"
        "      },"
        "      onMetrics: function(callback) {"
        "        if (typeof callback !== 'function') return function() {};"
        "        const watchId = systemInfoApi.watch(function(res) {"
        "          const metrics = {"
        "            cpu: (res && typeof res.cpu === 'object') ? (res.cpu.usagePercent || 0) : 0,"
        "            memory: (res && typeof res.memory === 'object') ? {"
        "              usedMB: res.memory.usedMB || Math.round((res.memory.totalMB || 0) * (res.memory.percent || 0) / 100),"
        "              totalMB: res.memory.totalMB || 0,"
        "              percent: res.memory.percent || 0"
        "            } : { usedMB: 0, totalMB: 0, percent: 0 },"
        "            battery: (res && typeof res.battery === 'object') ? (res.battery.percent || 100) : 100,"
        "            network: (res && typeof res.network === 'object') ? {"
        "              upSpeed: res.network.upSpeed || 0,"
        "              downSpeed: res.network.downSpeed || 0"
        "            } : { upSpeed: 0, downSpeed: 0 },"
        "            uptime: (res && res.uptime) ? res.uptime.uptimeSeconds : 0,"
        "            raw: res"
        "          };"
        "          callback(metrics);"
        "        }, 1000);"
        "        return function() {"
        "          systemInfoApi.unwatch(watchId);"
        "        };"
        "      }"
        "    },"
        "    shell: {"
        "      openPath: function(filePath) {"
        "        return sendRequest('shell_open_path', { path: String(filePath || '') }).then(function(res) {"
        "          return !!(res && res.ok);"
        "        }).catch(function() { return false; });"
        "      },"
        "      openUrl: function(url) {"
        "        return sendRequest('shell_open_url', { url: String(url || '') }).then(function(res) {"
        "          if (!res || !res.ok) throw new Error(res && res.error ? res.error : 'Failed to open URL');"
        "          return true;"
        "        });"
        "      }"
        "    },"
        "    widget: isWallpaper ? null : {"
        "      close: function() {"
        "        return sendRequest('close_widget', { targetInstanceId: widgetId });"
        "      },"
        "      openSettings: function() {"
        "        desktopApi.openSettings();"
        "      },"
        "      resize: function(width, height) {"
        "        desktopApi.resizeWidget({ width: width, height: height });"
        "      }"
        "    },"
        "    wallpaper: isWallpaper ? {"
        "      onPropertyChange: function(callback) {"
        "        if (typeof callback !== 'function') return function() {};"
        "        wallpaperPropertyWatchers.push(callback);"
        "        return function() {"
        "          const idx = wallpaperPropertyWatchers.indexOf(callback);"
        "          if (idx !== -1) wallpaperPropertyWatchers.splice(idx, 1);"
        "        };"
        "      }"
        "    } : null"
        "  };"
        "})();"
    ).arg(escapedWidgetId);

    frame->ExecuteJavaScript(jsPolyfill.toStdWString(), frame->GetURL(), 0);
}

void CefOsrBrowserInstance::onBrowserAfterCreated(CefRefPtr<CefBrowser> browser)
{
    m_browser = browser;
    if (m_browser && m_browser->GetHost()) {
        m_browser->GetHost()->SetZoomLevel(0.0);
        m_browser->GetHost()->WasHidden(!m_visible);
        m_browser->GetHost()->SetWindowlessFrameRate(m_visible ? m_framerate : 1);
        m_browser->GetHost()->Invalidate(PET_VIEW);
    }
    emit browserCreated(m_widgetId);
    qInfo() << "[LinWidget][CefOsrBrowserInstance] Browser successfully created and invalidated for:" << m_widgetId;
}

void CefOsrBrowserInstance::onBrowserBeforeClose(CefRefPtr<CefBrowser> browser)
{
    Q_UNUSED(browser);
    m_browser = nullptr;
    emit browserClosed(m_widgetId);
    qInfo() << "[LinWidget][CefOsrBrowserInstance] Browser closed callback received for:" << m_widgetId;
}

void CefOsrBrowserInstance::onBrowserLoadingStateChange(bool isLoading, bool canGoBack, bool canGoForward)
{
    Q_UNUSED(canGoBack);
    Q_UNUSED(canGoForward);
    qInfo() << "[LinWidget][CefOsrBrowserInstance] LoadingStateChange:" << m_widgetId << "isLoading=" << isLoading;
}

void CefOsrBrowserInstance::onBrowserLoadEnd(const QString& frameUrl, int httpStatusCode)
{
    qInfo() << "[LinWidget][CefOsrBrowserInstance] OnLoadEnd:" << m_widgetId << "url=" << frameUrl << "statusCode=" << httpStatusCode;
    if (m_browser) {
        injectBridgeScript(m_browser->GetMainFrame());
        if (m_browser->GetHost()) {
            m_browser->GetHost()->Invalidate(PET_VIEW);
        }
    }
}

void CefOsrBrowserInstance::onBrowserLoadError(int errorCode, const QString& errorText, const QString& failedUrl)
{
    qCritical() << "[LinWidget][CefOsrBrowserInstance] OnLoadError:" << m_widgetId
                << "code=" << errorCode
                << "error=" << errorText
                << "url=" << failedUrl;
}

bool CefOsrBrowserInstance::hasDevTools() const
{
    return m_browser && m_browser->GetHost() && m_browser->GetHost()->HasDevTools();
}

void CefOsrBrowserInstance::showDevTools()
{
    if (!m_browser || !m_browser->GetHost()) {
        return;
    }
    if (m_browser->GetHost()->HasDevTools()) {
        return;
    }
    CefWindowInfo windowInfo;
#ifdef Q_OS_WIN
    const std::string title = ("LinWidget DevTools - " + m_widgetId).toStdString();
    windowInfo.SetAsPopup(nullptr, title);
#endif
    CefBrowserSettings settings;
    CefPoint inspectElementAt;
    m_browser->GetHost()->ShowDevTools(windowInfo, m_clientHandler, settings, inspectElementAt);
    qInfo() << "[LinWidget][CefOsrBrowserInstance] ShowDevTools opened for:" << m_widgetId;
}

void CefOsrBrowserInstance::closeDevTools()
{
    if (m_browser && m_browser->GetHost() && m_browser->GetHost()->HasDevTools()) {
        m_browser->GetHost()->CloseDevTools();
        qInfo() << "[LinWidget][CefOsrBrowserInstance] CloseDevTools for:" << m_widgetId;
    }
}
#endif

} // namespace LinCef
