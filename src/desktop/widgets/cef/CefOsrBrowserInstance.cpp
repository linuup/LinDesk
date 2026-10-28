#include "desktop/widgets/cef/CefOsrBrowserInstance.h"
#include "desktop/widgets/cef/CefOsrRenderHandler.h"
#include "desktop/widgets/cef/CefManager.h"

#include <QDebug>

#ifdef LINDESK_HAS_CEF
#include <include/cef_life_span_handler.h>
#include <include/cef_load_handler.h>
#include <include/cef_context_menu_handler.h>
#include <include/cef_request_handler.h>
#include <include/cef_resource_request_handler.h>
#include <include/cef_display_handler.h>
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
                            public CefDisplayHandler
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

    // CefDisplayHandler interface (console logging)
    bool OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                          cef_log_severity_t level,
                          const CefString& message,
                          const CefString& source,
                          int line) override
    {
        Q_UNUSED(browser);
        if (m_owner) {
            const int lvl = static_cast<int>(level);
            const QString msg = QString::fromStdWString(message.ToWString());
            const QString src = QString::fromStdWString(source.ToWString());
            QMetaObject::invokeMethod(m_owner, [owner = m_owner, lvl, msg, src, line]() {
                if (owner) {
                    owner->onConsoleMessage(lvl, msg, src, line);
                }
            }, Qt::QueuedConnection);
        }
        return false;
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
            qWarning() << "[LinWidget] Path traversal blocked for widget"
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
                qWarning() << "[LinWidget Sandbox] Blocked network request for widget"
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
        qWarning() << "[CefOsrBrowserInstance] Cannot create browser, CEF runtime not available for:" << m_widgetId;
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
    browserSettings.windowless_frame_rate = 60;
    browserSettings.background_color = 0x00000000; // Transparent background

    const std::wstring urlW = url.toStdWString();
    const bool success = CefBrowserHost::CreateBrowser(windowInfo, m_clientHandler, urlW, browserSettings, nullptr, nullptr);
    if (!success) {
        qCritical() << "[CefOsrBrowserInstance] CefBrowserHost::CreateBrowser failed for:" << m_widgetId;
        m_clientHandler->detach();
        m_clientHandler = nullptr;
        m_renderHandler = nullptr;
        return false;
    }

    m_created = true;
    qInfo() << "[CefOsrBrowserInstance] Browser creation initiated for:" << m_widgetId << "url=" << url;
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

    CefMouseEvent mouseEvent;
    mouseEvent.x = x;
    mouseEvent.y = y;
    mouseEvent.modifiers = 0;

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

    CefMouseEvent mouseEvent;
    mouseEvent.x = x;
    mouseEvent.y = y;
    mouseEvent.modifiers = 0;

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

    uint32_t modifiers = 0;
    if (event->modifiers() & Qt::ShiftModifier) modifiers |= EVENTFLAG_SHIFT_DOWN;
    if (event->modifiers() & Qt::ControlModifier) modifiers |= EVENTFLAG_CONTROL_DOWN;
    if (event->modifiers() & Qt::AltModifier) modifiers |= EVENTFLAG_ALT_DOWN;

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
    qWarning() << "[CefOsrBrowserInstance] Render process terminated for widget:" << m_widgetId
               << "status:" << status << "code:" << errorCode << "reason:" << errorString;
    emit browserCrashed(m_widgetId, status, errorCode, errorString);
}

void CefOsrBrowserInstance::onConsoleMessage(int level, const QString& message, const QString& source, int line)
{
    const QString widgetTag = QStringLiteral("[widget:%1]").arg(m_widgetId);
    const QString loc = source.isEmpty() ? QString() : QStringLiteral(" (%1:%2)").arg(source).arg(line);
    const QString fullMsg = QStringLiteral("%1 %2%3").arg(widgetTag, message, loc);

    if (level >= 4) {
        qCritical().noquote() << fullMsg;
    } else if (level == 3) {
        qWarning().noquote() << fullMsg;
    } else {
        qInfo().noquote() << fullMsg;
    }

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

    const QString jsPolyfill = QString(
        "(function() {"
        "  if (window.__linwidget_bridge_injected) return;"
        "  window.__linwidget_bridge_injected = true;"
        "  const messageListeners = [];"
        "  window.__linwidget_dispatch_message = function(data) {"
        "    const event = new MessageEvent('message', { data: data });"
        "    messageListeners.forEach(listener => {"
        "      try {"
        "        if (typeof listener === 'function') { listener(event); }"
        "        else if (listener && typeof listener.handleEvent === 'function') { listener.handleEvent(event); }"
        "      } catch (e) { console.error('[LinDesk Bridge] Error in listener:', e); }"
        "    });"
        "    window.dispatchEvent(event);"
        "  };"
        "  const postMessageBridge = function(data) {"
        "    try {"
        "      const body = (typeof data === 'string') ? data : JSON.stringify(data);"
        "      const xhr = new XMLHttpRequest();"
        "      xhr.open('POST', 'linwidget://bridge/postMessage', true);"
        "      xhr.setRequestHeader('Content-Type', 'application/json');"
        "      xhr.send(body);"
        "    } catch (e) { console.error('[LinDesk Bridge] Failed to post message:', e); }"
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
        "      widgetId: '%1',"
        "      postMessage: postMessageBridge,"
        "      addEventListener: window.chrome.webview.addEventListener,"
        "      removeEventListener: window.chrome.webview.removeEventListener"
        "    };"
        "  }"
        "})();"
    ).arg(m_widgetId);

    frame->ExecuteJavaScript(jsPolyfill.toStdWString(), frame->GetURL(), 0);
}

void CefOsrBrowserInstance::onBrowserAfterCreated(CefRefPtr<CefBrowser> browser)
{
    m_browser = browser;
    if (m_browser && m_browser->GetHost()) {
        m_browser->GetHost()->SetZoomLevel(0.0);
        m_browser->GetHost()->WasHidden(!m_visible);
        m_browser->GetHost()->Invalidate(PET_VIEW);
    }
    emit browserCreated(m_widgetId);
    qInfo() << "[CefOsrBrowserInstance] Browser successfully created and invalidated for:" << m_widgetId;
}

void CefOsrBrowserInstance::onBrowserBeforeClose(CefRefPtr<CefBrowser> browser)
{
    Q_UNUSED(browser);
    m_browser = nullptr;
    emit browserClosed(m_widgetId);
    qInfo() << "[CefOsrBrowserInstance] Browser closed callback received for:" << m_widgetId;
}

void CefOsrBrowserInstance::onBrowserLoadingStateChange(bool isLoading, bool canGoBack, bool canGoForward)
{
    Q_UNUSED(canGoBack);
    Q_UNUSED(canGoForward);
    qInfo() << "[CefOsrBrowserInstance] LoadingStateChange:" << m_widgetId << "isLoading=" << isLoading;
}

void CefOsrBrowserInstance::onBrowserLoadEnd(const QString& frameUrl, int httpStatusCode)
{
    qInfo() << "[CefOsrBrowserInstance] OnLoadEnd:" << m_widgetId << "url=" << frameUrl << "statusCode=" << httpStatusCode;
    if (m_browser) {
        injectBridgeScript(m_browser->GetMainFrame());
        if (m_browser->GetHost()) {
            m_browser->GetHost()->Invalidate(PET_VIEW);
        }
    }
}

void CefOsrBrowserInstance::onBrowserLoadError(int errorCode, const QString& errorText, const QString& failedUrl)
{
    qCritical() << "[CefOsrBrowserInstance] OnLoadError:" << m_widgetId
                << "code=" << errorCode
                << "error=" << errorText
                << "url=" << failedUrl;
}
#endif

} // namespace LinCef
