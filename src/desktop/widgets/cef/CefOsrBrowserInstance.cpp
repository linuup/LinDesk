#include "desktop/widgets/cef/CefOsrBrowserInstance.h"
#include "desktop/widgets/cef/CefOsrRenderHandler.h"
#include "desktop/widgets/cef/CefManager.h"

#include <QDebug>

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

    CefWindowInfo windowInfo;
    windowInfo.SetAsWindowless(nullptr);
    windowInfo.shared_texture_enabled = 0;

    CefBrowserSettings browserSettings;
    browserSettings.windowless_frame_rate = 60;
    browserSettings.background_color = 0x00000000; // Transparent background

    const std::wstring urlW = url.toStdWString();
    const bool success = CefBrowserHost::CreateBrowser(windowInfo, this, urlW, browserSettings, nullptr, nullptr);
    if (!success) {
        qCritical() << "[CefOsrBrowserInstance] CefBrowserHost::CreateBrowser failed for:" << m_widgetId;
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

#ifdef LINDESK_HAS_CEF
    if (m_browser && m_browser->GetHost()) {
        m_browser->GetHost()->CloseBrowser(true);
    }
    m_browser = nullptr;
    m_renderHandler = nullptr;
#endif

    m_created = false;
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
CefRefPtr<CefRenderHandler> CefOsrBrowserInstance::GetRenderHandler()
{
    return m_renderHandler;
}

void CefOsrBrowserInstance::OnAfterCreated(CefRefPtr<CefBrowser> browser)
{
    m_browser = browser;
    emit browserCreated(m_widgetId);
    qInfo() << "[CefOsrBrowserInstance] Browser successfully created for:" << m_widgetId;
}

void CefOsrBrowserInstance::OnBeforeClose(CefRefPtr<CefBrowser> browser)
{
    Q_UNUSED(browser);
    m_browser = nullptr;
    emit browserClosed(m_widgetId);
    qInfo() << "[CefOsrBrowserInstance] Browser closed for:" << m_widgetId;
}
#endif

} // namespace LinDesk
