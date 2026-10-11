#pragma once

#include <QObject>
#include <QString>
#include <QRect>
#include <QImage>
#include <QVector>

#ifdef LINDESK_HAS_CEF
#include <include/cef_client.h>
#include <include/cef_life_span_handler.h>
#endif

namespace LinCef {

class CefOsrRenderHandler;

class CefOsrBrowserInstance : public QObject
#ifdef LINDESK_HAS_CEF
    , public CefClient
    , public CefLifeSpanHandler
#endif
{
    Q_OBJECT

public:
    explicit CefOsrBrowserInstance(QString widgetId, QObject* parent = nullptr);
    ~CefOsrBrowserInstance() override;

    const QString& widgetId() const { return m_widgetId; }
    const QRect& geometry() const { return m_geometry; }
    void setGeometry(const QRect& geometry);

    bool isCreated() const { return m_created; }
    bool isVisible() const { return m_visible; }
    void setVisible(bool visible);

    bool create(const QString& url, const QRect& geometry);
    void close();

    void loadUrl(const QString& url);
    void resize(int width, int height);

    // Input event routing
    void sendMouseClick(int x, int y, Qt::MouseButton button, bool mouseUp, int clickCount = 1);
    void sendMouseMove(int x, int y, bool mouseLeave = false);
    void sendMouseWheel(int x, int y, int deltaX, int deltaY);

#ifdef LINDESK_HAS_CEF
    // CefClient interfaces
    CefRefPtr<CefRenderHandler> GetRenderHandler() override;
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }

    // CefLifeSpanHandler interfaces
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
#endif

signals:
    void frameReady(const QString& widgetId, const QRect& widgetGeometry, const QImage& image, const QVector<QRect>& dirtyRects);
    void browserCreated(const QString& widgetId);
    void browserClosed(const QString& widgetId);

private:
    QString m_widgetId;
    QRect m_geometry;
    bool m_created = false;
    bool m_visible = true;

#ifdef LINDESK_HAS_CEF
    CefRefPtr<CefBrowser> m_browser;
    CefRefPtr<CefOsrRenderHandler> m_renderHandler;
    IMPLEMENT_REFCOUNTING(CefOsrBrowserInstance);
#else
    CefOsrRenderHandler* m_renderHandler = nullptr;
#endif
};

} // namespace LinCef
