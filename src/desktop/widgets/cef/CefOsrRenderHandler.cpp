#include "desktop/widgets/cef/CefOsrRenderHandler.h"

namespace LinCef {

#ifdef LINDESK_HAS_CEF

CefOsrRenderHandler::CefOsrRenderHandler(QString widgetId, int width, int height, QObject* parent)
    : QObject(parent)
    , m_widgetId(std::move(widgetId))
    , m_width(qMax(1, width))
    , m_height(qMax(1, height))
{
}

void CefOsrRenderHandler::resize(int width, int height)
{
    m_width = qMax(1, width);
    m_height = qMax(1, height);
}

void CefOsrRenderHandler::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect)
{
    Q_UNUSED(browser);
    rect.Set(0, 0, m_width, m_height);
}

void CefOsrRenderHandler::OnPaint(CefRefPtr<CefBrowser> browser,
                                  PaintElementType type,
                                  const RectList& dirtyRects,
                                  const void* buffer,
                                  int width,
                                  int height)
{
    Q_UNUSED(browser);
    if (type != PET_VIEW || !buffer || width <= 0 || height <= 0) {
        return;
    }

    // Format_ARGB32_Premultiplied corresponds directly to Chromium's BGRA32 pre-multiplied surface on Windows
    const QImage frame(static_cast<const uchar*>(buffer),
                       width,
                       height,
                       width * 4,
                       QImage::Format_ARGB32_Premultiplied);

    const QImage clonedFrame = frame.copy();

    QVector<QRect> qtDirtyRects;
    qtDirtyRects.reserve(static_cast<int>(dirtyRects.size()));
    for (const auto& r : dirtyRects) {
        qtDirtyRects.append(QRect(r.x, r.y, r.width, r.height));
    }

    emit frameReady(m_widgetId, clonedFrame, qtDirtyRects);
}

#endif

} // namespace LinCef
