#include "desktop/widgets/cef/CefOsrRenderHandler.h"
#include <QGuiApplication>
#include <QScreen>
#include <QDebug>

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

bool CefOsrRenderHandler::GetScreenInfo(CefRefPtr<CefBrowser> browser, CefScreenInfo& screen_info)
{
    Q_UNUSED(browser);
    float dpr = 1.0f;
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        dpr = static_cast<float>(screen->devicePixelRatio());
    }
    screen_info.device_scale_factor = dpr;
    screen_info.rect.x = 0;
    screen_info.rect.y = 0;
    screen_info.rect.width = m_width;
    screen_info.rect.height = m_height;
    screen_info.available_rect = screen_info.rect;
    return true;
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

    QImage clonedFrame = frame.copy();
    float dpr = 1.0f;
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        dpr = static_cast<float>(screen->devicePixelRatio());
    }
    if (dpr > 0.0f) {
        clonedFrame.setDevicePixelRatio(dpr);
    }

    QVector<QRect> qtDirtyRects;
    qtDirtyRects.reserve(static_cast<int>(dirtyRects.size()));
    for (const auto& r : dirtyRects) {
        qtDirtyRects.append(QRect(r.x, r.y, r.width, r.height));
    }

    emit frameReady(m_widgetId, clonedFrame, qtDirtyRects);
}

void CefOsrRenderHandler::OnImeCompositionRangeChanged(CefRefPtr<CefBrowser> browser,
                                                      const CefRange& selected_range,
                                                      const RectList& character_bounds)
{
    Q_UNUSED(browser);
    Q_UNUSED(selected_range);
    if (character_bounds.empty()) {
        emit imeCompositionRangeChanged(m_widgetId, QRect());
        return;
    }

    const auto& lastBound = character_bounds.back();
    const QRect cursorRect(lastBound.x, lastBound.y, lastBound.width, lastBound.height);
    emit imeCompositionRangeChanged(m_widgetId, cursorRect);
}

#endif

} // namespace LinCef
