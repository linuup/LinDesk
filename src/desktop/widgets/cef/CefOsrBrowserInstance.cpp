#include "desktop/widgets/cef/CefOsrBrowserInstance.h"
#include "desktop/widgets/cef/CefOsrRenderHandler.h"
#include "desktop/widgets/cef/CefManager.h"

#include <QDebug>

#ifdef LINDESK_HAS_CEF
#include <include/cef_life_span_handler.h>
#include <include/cef_load_handler.h>
#include <include/cef_context_menu_handler.h>

namespace LinCef {

class CefOsrClientHandler : public CefClient,
                            public CefLifeSpanHandler,
                            public CefLoadHandler,
                            public CefContextMenuHandler
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

#ifdef LINDESK_HAS_CEF
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
    if (m_browser && m_browser->GetHost()) {
        m_browser->GetHost()->Invalidate(PET_VIEW);
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
