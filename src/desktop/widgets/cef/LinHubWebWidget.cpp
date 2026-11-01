#include "LinHubWebWidget.h"
#include "desktop/widgets/cef/CefOsrBrowserInstance.h"
#include "desktop/widgets/linwidget/WidgetManager.h"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QShowEvent>
#include <QResizeEvent>
#include <QDebug>

LinHubWebWidget::LinHubWebWidget(const QString& url, QWidget* parent)
    : QWidget(parent)
    , m_url(url)
{
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    m_browser = new LinCef::CefOsrBrowserInstance(QStringLiteral("lindesk.hub"), this);

    connect(m_browser, &LinCef::CefOsrBrowserInstance::frameReady, this,
            [this](const QString&, const QRect&, const QImage& image, const QVector<QRect>& dirtyRects) {
                m_frame = image;
                if (dirtyRects.isEmpty()) {
                    update();
                } else {
                    for (const QRect& r : dirtyRects) {
                        update(r);
                    }
                }
            });

    connect(m_browser, &LinCef::CefOsrBrowserInstance::messageReceived, this,
            [this](const QString&, const QJsonObject& message) {
                if (auto* mgr = LinWidget::WidgetManager::instance()) {
                    const QJsonObject resp = mgr->handleWidgetManagerRequest(message);
                    if (!resp.isEmpty() && m_browser) {
                        m_browser->sendMessageToWeb(resp);
                    }
                }
            });
}

LinHubWebWidget::~LinHubWebWidget()
{
    if (m_browser) {
        m_browser->close();
        m_browser = nullptr;
    }
}

void LinHubWebWidget::startBrowser()
{
    if (m_browser && !m_browser->isCreated()) {
        const int w = qMax(400, width());
        const int h = qMax(300, height());
        m_browser->create(m_url, QRect(0, 0, w, h));
    }
}

void LinHubWebWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    startBrowser();
}

void LinHubWebWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_browser && m_browser->isCreated()) {
        m_browser->resize(width(), height());
    }
}

void LinHubWebWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    if (!m_frame.isNull()) {
        painter.drawImage(event->rect(), m_frame, event->rect());
    } else {
        painter.fillRect(rect(), QColor(7, 10, 18));
        painter.setPen(QColor(148, 163, 184));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("Loading LinDesk Hub Web UI..."));
    }
}

void LinHubWebWidget::mousePressEvent(QMouseEvent* event)
{
    setFocus();
    if (m_browser) {
        m_browser->sendMouseClick(event->x(), event->y(), event->button(), false, 1);
    }
}

void LinHubWebWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_browser) {
        m_browser->sendMouseClick(event->x(), event->y(), event->button(), true, 1);
    }
}

void LinHubWebWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_browser) {
        m_browser->sendMouseMove(event->x(), event->y(), false);
    }
}

void LinHubWebWidget::wheelEvent(QWheelEvent* event)
{
    if (m_browser) {
        const QPoint angle = event->angleDelta();
        const QPoint pos = event->pos();
        m_browser->sendMouseWheel(pos.x(), pos.y(), angle.x(), angle.y());
    }
}

void LinHubWebWidget::keyPressEvent(QKeyEvent* event)
{
    if (m_browser) {
        m_browser->sendKeyEvent(event);
    }
}

void LinHubWebWidget::keyReleaseEvent(QKeyEvent* event)
{
    if (m_browser) {
        m_browser->sendKeyEvent(event);
    }
}
