#pragma once

#include <QObject>
#include <QImage>
#include <QRect>
#include <QVector>
#include <QString>

#ifdef LINDESK_HAS_CEF
#ifdef GetNextSibling
#undef GetNextSibling
#endif
#ifdef GetPrevSibling
#undef GetPrevSibling
#endif
#ifdef GetFirstChild
#undef GetFirstChild
#endif
#ifdef GetLastChild
#undef GetLastChild
#endif
#include <include/cef_render_handler.h>
#endif

namespace LinCef {

#ifdef LINDESK_HAS_CEF
class CefOsrRenderHandler : public QObject, public CefRenderHandler {
    Q_OBJECT

public:
    explicit CefOsrRenderHandler(QString widgetId, int width, int height, QObject* parent = nullptr);

    void resize(int width, int height);

    // CefRenderHandler interface
    bool GetScreenInfo(CefRefPtr<CefBrowser> browser, CefScreenInfo& screen_info) override;
    void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
    void OnPaint(CefRefPtr<CefBrowser> browser,
                 PaintElementType type,
                 const RectList& dirtyRects,
                 const void* buffer,
                 int width,
                 int height) override;
    void OnImeCompositionRangeChanged(CefRefPtr<CefBrowser> browser,
                                      const CefRange& selected_range,
                                      const RectList& character_bounds) override;

signals:
    void frameReady(const QString& widgetId, const QImage& image, const QVector<QRect>& dirtyRects);
    void imeCompositionRangeChanged(const QString& widgetId, const QRect& cursorRect);

private:
    QString m_widgetId;
    int m_width = 300;
    int m_height = 200;

    IMPLEMENT_REFCOUNTING(CefOsrRenderHandler);
};
#else
class CefOsrRenderHandler : public QObject {
    Q_OBJECT

public:
    explicit CefOsrRenderHandler(QString widgetId, int width, int height, QObject* parent = nullptr)
        : QObject(parent), m_widgetId(widgetId), m_width(width), m_height(height) {}
    void resize(int width, int height) { m_width = width; m_height = height; }

signals:
    void frameReady(const QString& widgetId, const QImage& image, const QVector<QRect>& dirtyRects);

private:
    QString m_widgetId;
    int m_width = 300;
    int m_height = 200;
};
#endif

} // namespace LinCef
