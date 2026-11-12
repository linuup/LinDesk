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
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariantList>
#include <QTimer>
#include <QDebug>
#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
#ifdef Q_OS_WIN
void configureWin32DropFilterOnWindow(QWidget* win)
{
    if (!win) {
        return;
    }
    HWND hwnd = reinterpret_cast<HWND>(win->winId());
    if (!hwnd || !IsWindow(hwnd)) {
        return;
    }
    // 保留 Qt 原生 OLE IDropTarget (绝不调用 RevokeDragDrop)，同时放行 Win32 拖拽消息过滤
    DragAcceptFiles(hwnd, TRUE);
    ChangeWindowMessageFilterEx(hwnd, WM_DROPFILES, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(hwnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(hwnd, 0x0049 /* WM_COPYGLOBALDATA */, MSGFLT_ALLOW, nullptr);
}
#endif
} // namespace

LinHubWebWidget::LinHubWebWidget(const QString& url, QWidget* parent)
    : QWidget(parent)
    , m_url(url)
{
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAcceptDrops(true);

    if (parent) {
        parent->setAcceptDrops(true);
        parent->installEventFilter(this);
    }
    if (QWidget* win = this->window()) {
        win->setAcceptDrops(true);
        win->installEventFilter(this);
    }

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
                const QString action = message.value(QStringLiteral("action")).toString().trimmed().toLower();
                if (action == QStringLiteral("window_minimize")) {
                    if (QWidget* win = this->window()) {
                        win->showMinimized();
                    }
                    return;
                }
                if (action == QStringLiteral("window_maximize")) {
                    if (QWidget* win = this->window()) {
                        if (win->isMaximized()) {
                            win->showNormal();
                        } else {
                            win->showMaximized();
                        }
                        QJsonObject msg;
                        msg.insert(QStringLiteral("action"), QStringLiteral("window_state_changed"));
                        msg.insert(QStringLiteral("isMaximized"), win->isMaximized());
                        if (m_browser) {
                            m_browser->sendMessageToWeb(msg);
                        }
                        update();
                    }
                    return;
                }
                if (action == QStringLiteral("window_close")) {
                    if (QWidget* win = this->window()) {
                        win->close();
                    }
                    return;
                }
                if (action == QStringLiteral("set_titlebar_rects")) {
                    if (QWidget* win = this->window()) {
                        QVariantList rectList;
                        const QJsonArray arr = message.value(QStringLiteral("rects")).toArray();
                        for (const QJsonValue& val : arr) {
                            const QJsonObject r = val.toObject();
                            rectList.append(QRect(
                                r.value(QStringLiteral("x")).toInt(),
                                r.value(QStringLiteral("y")).toInt(),
                                r.value(QStringLiteral("width")).toInt(),
                                r.value(QStringLiteral("height")).toInt()
                            ));
                        }
                        win->setProperty("webTitleInteractiveRects", rectList);
                    }
                    return;
                }
                if (action == QStringLiteral("window_drag")) {
#ifdef Q_OS_WIN
                    if (QWidget* win = this->window()) {
                        if (m_browser) {
                            m_browser->sendMouseClick(0, 0, Qt::LeftButton, true, 1);
                        }
                        ReleaseCapture();
                        SendMessageW(reinterpret_cast<HWND>(win->winId()), WM_NCLBUTTONDOWN, HTCAPTION, 0);
                    }
#endif
                    return;
                }

                if (auto* mgr = LinWidget::WidgetManager::instance()) {
                    const QJsonObject resp = mgr->handleWidgetManagerRequest(message);
                    if (!resp.isEmpty() && m_browser) {
                        m_browser->sendMessageToWeb(resp);
                    }
                }
            });

    if (auto* mgr = LinWidget::WidgetManager::instance()) {
        connect(mgr, &LinWidget::WidgetManager::runtimeEnabledChanged, this, [this](bool enabled) {
            if (m_browser) {
                QJsonObject msg;
                msg.insert(QStringLiteral("action"), QStringLiteral("runtime_state_changed"));
                msg.insert(QStringLiteral("runtimeEnabled"), enabled);
                m_browser->sendMessageToWeb(msg);
            }
        });
    }
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

bool LinHubWebWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (!event) {
        return QWidget::eventFilter(watched, event);
    }
    switch (event->type()) {
    case QEvent::DragEnter:
        if (handleDragEnter(static_cast<QDragEnterEvent*>(event))) {
            return true;
        }
        break;
    case QEvent::DragMove:
        if (handleDragMove(static_cast<QDragMoveEvent*>(event))) {
            return true;
        }
        break;
    case QEvent::DragLeave:
        if (handleDragLeave(static_cast<QDragLeaveEvent*>(event))) {
            return true;
        }
        break;
    case QEvent::Drop:
        if (handleDrop(static_cast<QDropEvent*>(event))) {
            return true;
        }
        break;
    case QEvent::WindowStateChange:
        if (QWidget* win = this->window()) {
            QJsonObject msg;
            msg.insert(QStringLiteral("action"), QStringLiteral("window_state_changed"));
            msg.insert(QStringLiteral("isMaximized"), win->isMaximized());
            if (m_browser) {
                m_browser->sendMessageToWeb(msg);
            }
            update();
        }
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

void LinHubWebWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    startBrowser();
    if (QWidget* p = this->parentWidget()) {
        p->setAcceptDrops(true);
        p->removeEventFilter(this);
        p->installEventFilter(this);
    }
    if (QWidget* win = this->window()) {
        win->setAcceptDrops(true);
        win->removeEventFilter(this);
        win->installEventFilter(this);
    }
#ifdef Q_OS_WIN
    QTimer::singleShot(0, this, [this]() {
        configureWin32DropFilterOnWindow(this->window());
    });
#endif
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
        painter.fillRect(rect(), QColor(248, 250, 252));
        painter.setPen(QColor(100, 116, 139));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("Loading LinDesk Hub..."));
    }

    // Windows 10 标准窗体边框 (1px #707070，最大化时隐藏，解决白色窗体混淆问题)
    if (QWidget* win = this->window()) {
        if (!win->isMaximized()) {
            painter.setPen(QPen(QColor(112, 112, 112), 1));
            painter.drawRect(rect().adjusted(0, 0, -1, -1));
        }
    }
}

void LinHubWebWidget::mousePressEvent(QMouseEvent* event)
{
    setFocus();
    if (m_browser) {
        m_browser->sendMouseClick(event->x(), event->y(), event->button(), false, 1);
    }
}

void LinHubWebWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    setFocus();
    if (m_browser) {
        m_browser->sendMouseClick(event->x(), event->y(), event->button(), false, 2);
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

void LinHubWebWidget::notifyDragState(bool dragging)
{
    if (m_isDraggingOver == dragging) {
        return;
    }
    m_isDraggingOver = dragging;
    if (m_browser) {
        QJsonObject msg;
        msg.insert(QStringLiteral("action"), QStringLiteral("drag_state_changed"));
        msg.insert(QStringLiteral("dragging"), dragging);
        m_browser->sendMessageToWeb(msg);
    }
}

bool LinHubWebWidget::handleDragEnter(QDragEnterEvent* event)
{
    if (event && event->mimeData() && event->mimeData()->hasUrls()) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        notifyDragState(true);
        return true;
    }
    return false;
}

bool LinHubWebWidget::handleDragMove(QDragMoveEvent* event)
{
    if (event && event->mimeData() && event->mimeData()->hasUrls()) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        notifyDragState(true);
        return true;
    }
    return false;
}

bool LinHubWebWidget::handleDragLeave(QDragLeaveEvent* event)
{
    if (event) {
        event->accept();
    }
    notifyDragState(false);
    return true;
}

bool LinHubWebWidget::handleDrop(QDropEvent* event)
{
    notifyDragState(false);
    if (!event || !event->mimeData() || !event->mimeData()->hasUrls()) {
        return false;
    }

    QJsonArray pathsArray;
    for (const QUrl& url : event->mimeData()->urls()) {
        const QString localPath = url.toLocalFile().trimmed();
        if (!localPath.isEmpty()) {
            pathsArray.append(localPath);
        }
    }

    if (pathsArray.isEmpty()) {
        return false;
    }

    event->setDropAction(Qt::CopyAction);
    event->accept();

    qInfo() << "[LinHubWebWidget] Dropped external paths:" << pathsArray;

    if (auto* mgr = LinWidget::WidgetManager::instance()) {
        QJsonObject req;
        req.insert(QStringLiteral("action"), QStringLiteral("stage_wallpaper_media"));
        req.insert(QStringLiteral("paths"), pathsArray);
        const QJsonObject resp = mgr->handleWidgetManagerRequest(req);
        if (m_browser && !resp.isEmpty()) {
            m_browser->sendMessageToWeb(resp);
        }
    }
    return true;
}

void LinHubWebWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (!handleDragEnter(event) && event) {
        event->ignore();
    }
}

void LinHubWebWidget::dragMoveEvent(QDragMoveEvent* event)
{
    if (!handleDragMove(event) && event) {
        event->ignore();
    }
}

void LinHubWebWidget::dragLeaveEvent(QDragLeaveEvent* event)
{
    handleDragLeave(event);
}

void LinHubWebWidget::dropEvent(QDropEvent* event)
{
    if (!handleDrop(event) && event) {
        event->ignore();
    }
}
