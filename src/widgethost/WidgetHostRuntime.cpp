#include "widgethost/WidgetHostRuntime.h"

#include "desktop/widgets/linwidget/PermissionManager.h"
#include "desktop/widgets/linwidget/WidgetIpcProtocol.h"
#include "desktop/widgets/linwidget/WidgetPackage.h"
#include "services/AppPaths.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLabel>
#include <QLocalSocket>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPoint>
#include <QGuiApplication>
#include <QScreen>
#include <QSize>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifdef LINDESK_HAS_WEBVIEW2
#include <WebView2.h>
#include <wrl.h>
#endif

namespace LinWidget {

#ifdef Q_OS_WIN
QString nativeWindowClassName(HWND window)
{
    if (!window) {
        return QString();
    }
    wchar_t className[256] = {};
    GetClassNameW(window, className, 256);
    return QString::fromWCharArray(className);
}

struct WebViewWindowSearch {
    HWND window = nullptr;
    QString className;
    int score = 0;
};

int webViewChildScore(const QString& className)
{
    if (className.compare(QStringLiteral("Chrome_RenderWidgetHostHWND"), Qt::CaseInsensitive) == 0) {
        return 100;
    }
    if (className.startsWith(QStringLiteral("Chrome_RenderWidget"), Qt::CaseInsensitive)) {
        return 95;
    }
    if (className.startsWith(QStringLiteral("Chrome_WidgetWin"), Qt::CaseInsensitive)) {
        return 70;
    }
    if (className.contains(QStringLiteral("WebView"), Qt::CaseInsensitive)) {
        return 60;
    }
    if (className.contains(QStringLiteral("Chrome"), Qt::CaseInsensitive)) {
        return 50;
    }
    return 0;
}

BOOL CALLBACK findWebViewChildWindow(HWND window, LPARAM result)
{
    if (!window || !IsWindowVisible(window) || !IsWindowEnabled(window)) {
        return TRUE;
    }

    const QString className = nativeWindowClassName(window);
    const int score = webViewChildScore(className);
    auto* search = reinterpret_cast<WebViewWindowSearch*>(result);
    if (search && score > search->score) {
        search->window = window;
        search->className = className;
        search->score = score;
    }
    return TRUE;
}

void focusNativeWindow(HWND window)
{
    if (!window) {
        return;
    }

    const DWORD currentThreadId = GetCurrentThreadId();
    DWORD targetProcessId = 0;
    const DWORD targetThreadId = GetWindowThreadProcessId(window, &targetProcessId);
    const HWND foregroundWindow = GetForegroundWindow();
    DWORD foregroundProcessId = 0;
    const DWORD foregroundThreadId = foregroundWindow
        ? GetWindowThreadProcessId(foregroundWindow, &foregroundProcessId)
        : 0;

    const bool attachTarget = targetThreadId != 0 && targetThreadId != currentThreadId;
    const bool attachForeground = foregroundThreadId != 0 &&
        foregroundThreadId != currentThreadId &&
        foregroundThreadId != targetThreadId;

    if (attachTarget) {
        AttachThreadInput(currentThreadId, targetThreadId, TRUE);
    }
    if (attachForeground) {
        AttachThreadInput(currentThreadId, foregroundThreadId, TRUE);
    }

    SetFocus(window);

    if (attachForeground) {
        AttachThreadInput(currentThreadId, foregroundThreadId, FALSE);
    }
    if (attachTarget) {
        AttachThreadInput(currentThreadId, targetThreadId, FALSE);
    }
}

void activateNativeWindow(HWND window)
{
    if (!window) {
        return;
    }

    const DWORD currentThreadId = GetCurrentThreadId();
    DWORD targetProcessId = 0;
    const DWORD targetThreadId = GetWindowThreadProcessId(window, &targetProcessId);
    const HWND foregroundWindow = GetForegroundWindow();
    DWORD foregroundProcessId = 0;
    const DWORD foregroundThreadId = foregroundWindow
        ? GetWindowThreadProcessId(foregroundWindow, &foregroundProcessId)
        : 0;

    const bool attachTarget = targetThreadId != 0 && targetThreadId != currentThreadId;
    const bool attachForeground = foregroundThreadId != 0 &&
        foregroundThreadId != currentThreadId &&
        foregroundThreadId != targetThreadId;

    if (attachTarget) {
        AttachThreadInput(currentThreadId, targetThreadId, TRUE);
    }
    if (attachForeground) {
        AttachThreadInput(currentThreadId, foregroundThreadId, TRUE);
    }

    SetActiveWindow(window);
    SetFocus(window);

    if (attachForeground) {
        AttachThreadInput(currentThreadId, foregroundThreadId, FALSE);
    }
    if (attachTarget) {
        AttachThreadInput(currentThreadId, targetThreadId, FALSE);
    }
}

HWND rootOwnerWindow(HWND window)
{
    HWND current = window;
    while (current) {
        HWND parent = GetParent(current);
        if (!parent) {
            break;
        }
        current = parent;
    }
    return current;
}
#endif

#ifdef LINDESK_HAS_WEBVIEW2
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
#endif

WidgetHostRuntime::WidgetHostRuntime(QString widgetId,
                                     QString packagePath,
                                     QString ipcServerName,
                                     QRect initialGeometry,
                                     quint32 parentProcessId,
                                     quint64 parentWindowId,
                                     QStringList grantedPermissions,
                                     bool isDevelopment,
                                     bool autoReload,
                                     bool sharedRuntime,
                                     QWidget* parent)
    : QWidget(parent)
    , m_widgetId(std::move(widgetId))
    , m_packagePath(std::move(packagePath))
    , m_ipcServerName(std::move(ipcServerName))
    , m_initialGeometry(initialGeometry)
    , m_parentProcessId(parentProcessId)
    , m_parentWindowId(parentWindowId)
    , m_grantedPermissions(isDevelopment
          ? PermissionManager().normalizePermissions({ QStringLiteral("network"), QStringLiteral("storage"), QStringLiteral("desktop") })
          : PermissionManager().normalizePermissions(grantedPermissions))
    , m_isDevelopment(isDevelopment)
    , m_autoReload(autoReload)
    , m_sharedRuntime(sharedRuntime)
{
}

WidgetHostRuntime::~WidgetHostRuntime()
{
#ifdef LINDESK_HAS_WEBVIEW2
    auto* controller = static_cast<ICoreWebView2Controller*>(m_webViewController);
    if (controller) {
        controller->Close();
        controller->Release();
        m_webViewController = nullptr;
    }
    auto* webView = static_cast<ICoreWebView2*>(m_webView);
    if (webView) {
        webView->Release();
        m_webView = nullptr;
    }
    if (m_comInitialized) {
        CoUninitialize();
        m_comInitialized = false;
    }
#endif
}

bool WidgetHostRuntime::initialize(QString* errorMessage)
{
#ifdef LINDESK_HAS_WEBVIEW2
    const HRESULT coResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    m_comInitialized = SUCCEEDED(coResult);
#endif

    if (!m_sharedRuntime && !WidgetPackage::loadManifest(m_packagePath, &m_manifest, errorMessage)) {
        appendLog(QStringLiteral("error"), QStringLiteral("Manifest load failed: %1").arg(errorMessage ? *errorMessage : QString()));
        return false;
    }
    if (m_sharedRuntime) {
        m_manifest.id = QStringLiteral("__shared__");
        m_manifest.name = QStringLiteral("LinWidget Shared Runtime");
        m_manifest.width = m_initialGeometry.width() > 0 ? m_initialGeometry.width() : 1920;
        m_manifest.height = m_initialGeometry.height() > 0 ? m_initialGeometry.height() : 1080;
        m_manifest.minWidth = 1;
        m_manifest.minHeight = 1;
        setWindowTitle(QStringLiteral("LinWidget Shared Runtime"));
    }
    appendLog(QStringLiteral("info"), QStringLiteral("WidgetHost started"));
    appendLog(QStringLiteral("info"), QStringLiteral("package=%1").arg(m_packagePath));
    appendLog(QStringLiteral("info"), QStringLiteral("permissions=%1").arg(m_grantedPermissions.join(QStringLiteral(","))));
    if (m_isDevelopment) {
        setWindowTitle(QStringLiteral("[Dev] %1").arg(m_manifest.name));
        qDebug() << "[LinWidgetDev] starting" << m_widgetId << m_packagePath << "autoReload=" << m_autoReload;
        appendLog(QStringLiteral("info"), QStringLiteral("development=true autoReload=%1").arg(m_autoReload ? QStringLiteral("true") : QStringLiteral("false")));
    }

    setupWindow();
    if (m_sharedRuntime) {
        renderSharedHtmlRuntime();
    } else if (m_manifest.type == RuntimeType::Native) {
        renderNativePlaceholder();
    } else {
        renderHtmlWidget();
    }
    connectIpc();
    startParentMonitor();
    appendLog(QStringLiteral("info"), QStringLiteral("WidgetHost initialized"));
    return true;
}

void WidgetHostRuntime::setupWindow()
{
    if (m_parentWindowId != 0) {
        setWindowFlags(Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground, true);
    } else {
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground, true);
    }
    setAttribute(Qt::WA_ShowWithoutActivating, false);
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setFocusPolicy(m_parentWindowId == 0 ? Qt::StrongFocus : Qt::NoFocus);

    const QRect fallbackRect(QPoint(0, 0), QSize(m_manifest.width, m_manifest.height));
    QRect initialGeometry = m_initialGeometry.isEmpty() ? fallbackRect : m_initialGeometry;
    initialGeometry.setSize(clampWidgetSize(initialGeometry.size()));
    setGeometry(initialGeometry);
    if (m_sharedRuntime) {
        setSharedWindowGeometry();
    }
#ifdef Q_OS_WIN
    HWND hostWindow = reinterpret_cast<HWND>(winId());
    if (hostWindow) {
        LONG_PTR exStyle = GetWindowLongPtr(hostWindow, GWL_EXSTYLE);
        exStyle &= ~WS_EX_TRANSPARENT;
        if (m_sharedRuntime) {
            exStyle |= WS_EX_TOOLWINDOW;
            exStyle &= ~WS_EX_NOACTIVATE;
        } else {
            exStyle &= ~WS_EX_NOACTIVATE;
        }
        SetWindowLongPtr(hostWindow, GWL_EXSTYLE, exStyle);
    }
#endif
    attachToParentWindow();
}

void WidgetHostRuntime::attachToParentWindow()
{
    if (m_parentWindowId == 0) {
        return;
    }

#ifdef Q_OS_WIN
    HWND hostWindow = reinterpret_cast<HWND>(winId());
    HWND parentWindow = reinterpret_cast<HWND>(static_cast<quintptr>(m_parentWindowId));
    if (!hostWindow || !parentWindow || !IsWindow(parentWindow)) {
        return;
    }

    SetParent(hostWindow, parentWindow);
    LONG_PTR style = GetWindowLongPtr(hostWindow, GWL_STYLE);
    style &= ~WS_POPUP;
    style |= WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    SetWindowLongPtr(hostWindow, GWL_STYLE, style);

    LONG_PTR exStyle = GetWindowLongPtr(hostWindow, GWL_EXSTYLE);
    exStyle &= ~WS_EX_TOPMOST;
    exStyle &= ~WS_EX_TRANSPARENT;
    exStyle &= ~WS_EX_LAYERED;
    exStyle &= ~WS_EX_NOACTIVATE;
    SetWindowLongPtr(hostWindow, GWL_EXSTYLE, exStyle);

    const QRect clampedGeometry(clampToParent(geometry().topLeft()), geometry().size());
    SetWindowPos(hostWindow, nullptr,
                 clampedGeometry.x(), clampedGeometry.y(),
                 clampedGeometry.width(), clampedGeometry.height(),
                 SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    m_embedded = true;
#endif
}

bool WidgetHostRuntime::nativeEvent(const QByteArray& eventType, void* message, long* result)
{
#ifdef Q_OS_WIN
    Q_UNUSED(eventType);
    MSG* msg = static_cast<MSG*>(message);
    if (msg && msg->message == WM_MOUSEACTIVATE) {
        appendLog(QStringLiteral("mouse"), QStringLiteral("WM_MOUSEACTIVATE embedded=%1 hwnd=%2")
            .arg(m_embedded ? QStringLiteral("true") : QStringLiteral("false"),
                 QString::number(reinterpret_cast<quintptr>(reinterpret_cast<HWND>(winId())), 16)));
        if (m_sharedRuntime) {
            QTimer::singleShot(0, this, [this]() {
                focusWebView();
            });
            if (result) {
                *result = MA_ACTIVATE;
            }
        } else if (m_embedded) {
            QTimer::singleShot(0, this, [this]() {
                focusWebView();
            });
            if (result) {
                *result = MA_NOACTIVATE;
            }
        } else {
            QTimer::singleShot(0, this, [this]() {
                focusWebView();
            });
            if (result) {
                *result = MA_ACTIVATE;
            }
        }
        return true;
    }
    if (msg && msg->message == WM_NCHITTEST) {
        const QPoint screenPoint(static_cast<short>(LOWORD(msg->lParam)),
                                 static_cast<short>(HIWORD(msg->lParam)));
        if (m_sharedRuntime) {
            const QPoint localPoint = mapFromGlobal(screenPoint);
            QString matchedInstanceId;
            for (auto it = m_sharedWidgetGeometries.constBegin(); it != m_sharedWidgetGeometries.constEnd(); ++it) {
                if (it.value().contains(localPoint)) {
                    matchedInstanceId = it.key();
                    break;
                }
            }
            appendLog(QStringLiteral("hit"), QStringLiteral("shared hit local=%1,%2 matched=%3 rectCount=%4")
                .arg(QString::number(localPoint.x()),
                     QString::number(localPoint.y()),
                     matchedInstanceId.isEmpty() ? QStringLiteral("<none>") : matchedInstanceId,
                     QString::number(m_sharedWidgetGeometries.size())));
            if (result) {
                *result = matchedInstanceId.isEmpty() ? HTTRANSPARENT : HTCLIENT;
            }
            return true;
        }
        appendLog(QStringLiteral("mouse"), QStringLiteral("WM_NCHITTEST screen=%1,%2 hwnd=%3")
            .arg(QString::number(screenPoint.x()),
                 QString::number(screenPoint.y()),
                 QString::number(reinterpret_cast<quintptr>(reinterpret_cast<HWND>(winId())), 16)));
        if (result) {
            *result = HTCLIENT;
        }
        return true;
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif
    return QWidget::nativeEvent(eventType, message, result);
}

QRect WidgetHostRuntime::parentClientRect() const
{
#ifdef Q_OS_WIN
    if (m_parentWindowId == 0) {
        return QRect();
    }
    HWND parentWindow = reinterpret_cast<HWND>(static_cast<quintptr>(m_parentWindowId));
    if (!parentWindow || !IsWindow(parentWindow)) {
        return QRect();
    }
    RECT rect = {};
    if (!GetClientRect(parentWindow, &rect)) {
        return QRect();
    }
    return QRect(0, 0, rect.right - rect.left, rect.bottom - rect.top);
#else
    return QRect();
#endif
}

QPoint WidgetHostRuntime::clampToParent(const QPoint& requestedTopLeft) const
{
    if (!m_embedded && m_parentWindowId == 0) {
        return requestedTopLeft;
    }
    const QRect parentRect = parentClientRect();
    if (!parentRect.isValid()) {
        return requestedTopLeft;
    }
    const int maxX = qMax(0, parentRect.width() - width());
    const int maxY = qMax(0, parentRect.height() - height());
    return QPoint(qBound(0, requestedTopLeft.x(), maxX),
                  qBound(0, requestedTopLeft.y(), maxY));
}

void WidgetHostRuntime::connectIpc()
{
    m_socket = new QLocalSocket(this);
    connect(m_socket, &QLocalSocket::connected, this, [this]() {
        sendIpcMessage(QString::fromLatin1(IpcType::Ready));
        QJsonObject message;
        message.insert(QStringLiteral("hwnd"), QString::number(static_cast<qulonglong>(winId())));
        sendIpcMessage(QString::fromLatin1(IpcType::HostWindowReady), message);
    });
    connect(m_socket, &QLocalSocket::readyRead, this, &WidgetHostRuntime::handleIpcReadyRead);
    m_socket->connectToServer(m_ipcServerName);

    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(3000);
    connect(m_heartbeatTimer, &QTimer::timeout, this, [this]() {
        sendIpcMessage(QString::fromLatin1(IpcType::Heartbeat));
    });
    m_heartbeatTimer->start();
}

void WidgetHostRuntime::startParentMonitor()
{
    if (m_parentProcessId == 0) {
        return;
    }

    m_parentMonitorTimer = new QTimer(this);
    m_parentMonitorTimer->setInterval(1500);
    connect(m_parentMonitorTimer, &QTimer::timeout, this, [this]() {
        if (!isParentProcessAlive()) {
            close();
        }
    });
    m_parentMonitorTimer->start();
}

bool WidgetHostRuntime::isParentProcessAlive() const
{
    if (m_parentProcessId == 0) {
        return true;
    }

#ifdef Q_OS_WIN
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(m_parentProcessId));
    if (!process) {
        return false;
    }
    const DWORD waitResult = WaitForSingleObject(process, 0);
    CloseHandle(process);
    return waitResult == WAIT_TIMEOUT;
#else
    return true;
#endif
}

void WidgetHostRuntime::handleIpcReadyRead()
{
    if (!m_socket) {
        return;
    }

    while (m_socket->canReadLine()) {
        QJsonObject message;
        QString errorMessage;
        if (!decodeIpcMessage(m_socket->readLine(), &message, &errorMessage)) {
            sendIpcMessage(QString::fromLatin1(IpcType::Error), {
                { QStringLiteral("message"), errorMessage }
            });
            continue;
        }
        handleManagerMessage(message);
    }
}

void WidgetHostRuntime::handleManagerMessage(const QJsonObject& message)
{
    const QString type = message.value(QStringLiteral("type")).toString();
    if (type == QString::fromLatin1(IpcType::CloseWidget)) {
        close();
        return;
    }

    if (m_sharedRuntime) {
        if (type == QString::fromLatin1(IpcType::UpdateHostGeometry)) {
            const QRect hostGeometry(
                message.value(QStringLiteral("x")).toInt(x()),
                message.value(QStringLiteral("y")).toInt(y()),
                message.value(QStringLiteral("width")).toInt(width()),
                message.value(QStringLiteral("height")).toInt(height()));
            if (hostGeometry.isValid()) {
                setGeometry(hostGeometry);
                updateWebViewBounds();
                updateSharedWindowRegion();
                appendLog(QStringLiteral("info"), QStringLiteral("updated shared host geometry %1,%2 %3x%4")
                    .arg(QString::number(hostGeometry.x()),
                         QString::number(hostGeometry.y()),
                         QString::number(hostGeometry.width()),
                         QString::number(hostGeometry.height())));
            }
            return;
        }
        if (type == QString::fromLatin1(IpcType::UpdateHostOptions)) {
            m_showWebViewBounds = message.value(QStringLiteral("showWebViewBounds")).toBool(false);
            updateSharedWindowRegion();
            queueOrSendHostPageMessage({
                { QStringLiteral("type"), QString::fromLatin1(IpcType::UpdateHostOptions) },
                { QStringLiteral("showWebViewBounds"), m_showWebViewBounds }
            });
            appendLog(QStringLiteral("info"), QStringLiteral("updated shared host options showWebViewBounds=%1")
                .arg(m_showWebViewBounds ? QStringLiteral("true") : QStringLiteral("false")));
            return;
        }
        if (type == QString::fromLatin1(IpcType::AddWidget)) {
            const QString instanceId = message.value(QStringLiteral("instanceId")).toString(message.value(QStringLiteral("widgetId")).toString()).trimmed();
            const QString packagePath = message.value(QStringLiteral("packagePath")).toString().trimmed();
            WidgetManifest manifest;
            QString manifestError;
            if (instanceId.isEmpty() || !WidgetPackage::loadManifest(packagePath, &manifest, &manifestError)) {
                sendIpcMessage(QString::fromLatin1(IpcType::WidgetError), {
                    { QStringLiteral("instanceId"), instanceId },
                    { QStringLiteral("message"), manifestError.isEmpty() ? QStringLiteral("Invalid widget package.") : manifestError }
                });
                return;
            }
            const QString hostName = virtualHostNameForInstance(instanceId);
            QString entryUrl;
            if (!mapWidgetVirtualHost(hostName, packagePath)) {
                appendLog(QStringLiteral("warn"), QStringLiteral("Failed to map widget virtual host for %1, falling back to file URL.").arg(instanceId));
                entryUrl = QUrl::fromLocalFile(WidgetPackage::entryPath(packagePath, manifest)).toString();
            } else {
                entryUrl = QStringLiteral("https://%1/%2").arg(hostName, manifest.entry);
            }

            const QJsonObject geometry = message.value(QStringLiteral("geometry")).toObject();
            const QRect rect(
                geometry.value(QStringLiteral("x")).toInt(message.value(QStringLiteral("x")).toInt()),
                geometry.value(QStringLiteral("y")).toInt(message.value(QStringLiteral("y")).toInt()),
                geometry.value(QStringLiteral("width")).toInt(manifest.width),
                geometry.value(QStringLiteral("height")).toInt(manifest.height));
            m_sharedWidgetGeometries.insert(instanceId, rect);
            m_sharedWidgetHosts.insert(instanceId, hostName);
            updateSharedWindowRegion();
            appendLog(QStringLiteral("hit"), QStringLiteral("added shared rect id=%1 rect=%2,%3 %4x%5 rectCount=%6")
                .arg(instanceId,
                     QString::number(rect.x()),
                     QString::number(rect.y()),
                     QString::number(rect.width()),
                     QString::number(rect.height()),
                     QString::number(m_sharedWidgetGeometries.size())));
            QStringList permissions;
            const QJsonArray permissionArray = message.value(QStringLiteral("permissions")).toArray();
            for (const QJsonValue& value : permissionArray) {
                const QString permission = value.toString().trimmed();
                if (!permission.isEmpty()) {
                    permissions.append(permission);
                }
            }
            queueOrSendHostPageMessage(widgetInstancePayload(instanceId,
                                                             packagePath,
                                                             manifest,
                                                             rect,
                                                             permissions,
                                                             message.value(QStringLiteral("development")).toBool(false),
                                                             message.value(QStringLiteral("autoReload")).toBool(false),
                                                             entryUrl));
            return;
        }
        if (type == QString::fromLatin1(IpcType::RemoveWidget)) {
            const QString instanceId = message.value(QStringLiteral("instanceId")).toString(message.value(QStringLiteral("widgetId")).toString()).trimmed();
            m_sharedWidgetGeometries.remove(instanceId);
            m_sharedWidgetHosts.remove(instanceId);
            updateSharedWindowRegion();
            appendLog(QStringLiteral("hit"), QStringLiteral("removed shared rect id=%1 rectCount=%2")
                .arg(instanceId, QString::number(m_sharedWidgetGeometries.size())));
            queueOrSendHostPageMessage({
                { QStringLiteral("type"), QString::fromLatin1(IpcType::RemoveWidget) },
                { QStringLiteral("instanceId"), instanceId },
                { QStringLiteral("widgetId"), instanceId }
            });
            return;
        }
        if (type == QString::fromLatin1(IpcType::UpdateWidgetGeometry)) {
            const QString instanceId = message.value(QStringLiteral("instanceId")).toString(message.value(QStringLiteral("widgetId")).toString()).trimmed();
            QRect rect(
                message.value(QStringLiteral("x")).toInt(),
                message.value(QStringLiteral("y")).toInt(),
                message.value(QStringLiteral("width")).toInt(),
                message.value(QStringLiteral("height")).toInt());
            const QJsonObject geometry = message.value(QStringLiteral("geometry")).toObject();
            if (!geometry.isEmpty()) {
                rect = QRect(geometry.value(QStringLiteral("x")).toInt(rect.x()),
                             geometry.value(QStringLiteral("y")).toInt(rect.y()),
                             geometry.value(QStringLiteral("width")).toInt(rect.width()),
                             geometry.value(QStringLiteral("height")).toInt(rect.height()));
            }
            if (rect.isValid()) {
                m_sharedWidgetGeometries.insert(instanceId, rect);
                updateSharedWindowRegion();
                appendLog(QStringLiteral("hit"), QStringLiteral("updated shared rect id=%1 rect=%2,%3 %4x%5 rectCount=%6")
                    .arg(instanceId,
                         QString::number(rect.x()),
                         QString::number(rect.y()),
                         QString::number(rect.width()),
                         QString::number(rect.height()),
                         QString::number(m_sharedWidgetGeometries.size())));
            }
            queueOrSendHostPageMessage(message);
            return;
        }
        if (type == QString::fromLatin1(IpcType::ReloadWidget)) {
            queueOrSendHostPageMessage(message);
            return;
        }
    }

    if (type == QString::fromLatin1(IpcType::MoveWidget) ||
        type == QString::fromLatin1(IpcType::ResizeWidget)) {
        const int x = message.value(QStringLiteral("x")).toInt(geometry().x());
        const int y = message.value(QStringLiteral("y")).toInt(geometry().y());
        const int width = message.value(QStringLiteral("width")).toInt(geometry().width());
        const int height = message.value(QStringLiteral("height")).toInt(geometry().height());
        const QPoint pos = clampToParent(QPoint(x, y));
        applyHostGeometry(QRect(pos, clampWidgetSize(QSize(width, height))));
    }
}

void WidgetHostRuntime::sendIpcMessage(const QString& type, const QJsonObject& extra)
{
    if (!m_socket || m_socket->state() != QLocalSocket::ConnectedState) {
        return;
    }

    QJsonObject message = makeIpcMessage(type, m_widgetId);
    for (auto it = extra.constBegin(); it != extra.constEnd(); ++it) {
        if (it.key() == QStringLiteral("widgetId")) {
            continue;
        }
        message.insert(it.key(), it.value());
    }
    m_socket->write(encodeIpcMessage(message));
    m_socket->flush();
}

bool WidgetHostRuntime::event(QEvent* event)
{
    if (event->type() == QEvent::Resize) {
        updateWebViewBounds();
        update();
    } else if (event->type() == QEvent::Leave && !m_dragging) {
        unsetCursor();
    }
    return QWidget::event(event);
}

void WidgetHostRuntime::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, 1));

}

void WidgetHostRuntime::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragMoveStarted = false;
        m_dragStartScreenPos = event->globalPos();
        m_dragStartTopLeft = pos();
        m_lastDragGlobalPos = event->globalPos();
        m_dragOffset = event->globalPos() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void WidgetHostRuntime::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        if (!m_dragMoveStarted) {
            const QPoint totalDelta = event->globalPos() - m_dragStartScreenPos;
            if (qAbs(totalDelta.x()) + qAbs(totalDelta.y()) < 4) {
                event->accept();
                return;
            }
            m_dragMoveStarted = true;
        }
        const QPoint topLeft = m_embedded
            ? clampToParent(pos() + (event->globalPos() - m_lastDragGlobalPos))
            : event->globalPos() - m_dragOffset;
        m_lastDragGlobalPos = event->globalPos();
        applyHostPosition(topLeft);
        QJsonObject extra;
        extra.insert(QStringLiteral("x"), topLeft.x());
        extra.insert(QStringLiteral("y"), topLeft.y());
        sendIpcMessage(QString::fromLatin1(IpcType::MoveWidget), extra);
        event->accept();
        return;
    }
    unsetCursor();
    QWidget::mouseMoveEvent(event);
}

void WidgetHostRuntime::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        m_dragMoveStarted = false;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void WidgetHostRuntime::renderNativePlaceholder()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    m_placeholderLabel = new QLabel(tr("Native Widget\n%1").arg(m_manifest.name), this);
    m_placeholderLabel->setAlignment(Qt::AlignCenter);
    m_placeholderLabel->setStyleSheet(QStringLiteral(
        "QLabel {"
        "background: rgba(255,255,255,210);"
        "border: 1px solid rgba(120,130,150,120);"
        "border-radius: 10px;"
        "color: #142033;"
        "font-weight: 700;"
        "}"));
    layout->addWidget(m_placeholderLabel);
}

void WidgetHostRuntime::renderHtmlWidget()
{
#ifndef LINDESK_HAS_WEBVIEW2
    renderHtmlPlaceholder(tr("WebView2 SDK is not enabled in this build."));
#else
    const QString entryPath = WidgetPackage::entryPath(m_packagePath, m_manifest);
    if (!QFileInfo::exists(entryPath)) {
        renderHtmlPlaceholder(tr("Entry file is missing: %1").arg(entryPath));
        return;
    }

    setAttribute(Qt::WA_NativeWindow, true);
    const HWND parentWindow = reinterpret_cast<HWND>(winId());
    if (!parentWindow) {
        renderHtmlPlaceholder(tr("Cannot create native widget window."));
        return;
    }

    const QString userDataDir = QDir(m_packagePath).filePath(QStringLiteral(".webview2"));
    const std::wstring userData = QDir::toNativeSeparators(userDataDir).toStdWString();
    const HRESULT result = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,
        userData.c_str(),
        nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, parentWindow, entryPath](HRESULT envResult, ICoreWebView2Environment* environment) -> HRESULT {
                if (FAILED(envResult) || !environment) {
                    appendLog(QStringLiteral("error"), QStringLiteral("Failed to initialize WebView2 environment"));
                    renderHtmlPlaceholder(tr("Failed to initialize WebView2 environment."));
                    return S_OK;
                }

                environment->CreateCoreWebView2Controller(
                    parentWindow,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this, entryPath](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(controllerResult) || !controller) {
                                appendLog(QStringLiteral("error"), QStringLiteral("Failed to create WebView2 controller"));
                                renderHtmlPlaceholder(tr("Failed to create WebView2 controller."));
                                return S_OK;
                            }

                            controller->AddRef();
                            m_webViewController = controller;
                            ICoreWebView2* webView = nullptr;
                            controller->get_CoreWebView2(&webView);
                            m_webView = webView;
                            updateWebViewBounds();

                            ComPtr<ICoreWebView2Controller2> controller2;
                            if (SUCCEEDED(controller->QueryInterface(IID_PPV_ARGS(&controller2)))) {
                                COREWEBVIEW2_COLOR transparent = { 0, 0, 0, 0 };
                                controller2->put_DefaultBackgroundColor(transparent);
                            }

                            ComPtr<ICoreWebView2Settings> settings;
                            if (webView && SUCCEEDED(webView->get_Settings(&settings))) {
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_AreDevToolsEnabled(TRUE);
                            }

                            if (webView) {
                                appendLog(QStringLiteral("info"), QStringLiteral("WebView2 initialized"));
                                installWebConsoleLogging();
                                installWebViewDragBridge();
                                installWebPermissionGuards();
                                EventRegistrationToken acceleratorToken = {};
                                controller->add_AcceleratorKeyPressed(
                                    Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
                                        [this](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
                                            COREWEBVIEW2_KEY_EVENT_KIND kind = COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN;
                                            UINT key = 0;
                                            INT lparam = 0;
                                            if (args) {
                                                args->get_KeyEventKind(&kind);
                                                args->get_VirtualKey(&key);
                                                args->get_KeyEventLParam(&lparam);
                                            }
                                            appendLog(QStringLiteral("key"), QStringLiteral("AcceleratorKeyPressed kind=%1 vk=%2 lparam=%3")
                                                .arg(QString::number(static_cast<int>(kind)),
                                                     QString::number(key),
                                                     QString::number(lparam)));
                                            return S_OK;
                                        }).Get(),
                                    &acceleratorToken);
                                const QString uri = QUrl::fromLocalFile(entryPath).toString();
                                appendLog(QStringLiteral("info"), QStringLiteral("navigate=%1").arg(uri));
                                webView->Navigate(reinterpret_cast<LPCWSTR>(uri.utf16()));
                                if (m_isDevelopment) {
                                    webView->OpenDevToolsWindow();
                                }
                            }
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());

    if (FAILED(result)) {
        appendLog(QStringLiteral("error"), QStringLiteral("Failed to start WebView2 environment"));
        renderHtmlPlaceholder(tr("Failed to start WebView2 environment."));
    }
#endif
}

void WidgetHostRuntime::renderSharedHtmlRuntime()
{
#ifndef LINDESK_HAS_WEBVIEW2
    renderHtmlPlaceholder(tr("WebView2 SDK is not enabled in this build."));
#else
    const QString entryPath = sharedRuntimeEntryPath();
    if (!QFileInfo::exists(entryPath)) {
        renderHtmlPlaceholder(tr("Shared runtime page is missing: %1").arg(entryPath));
        return;
    }

    setAttribute(Qt::WA_NativeWindow, true);
    const HWND parentWindow = reinterpret_cast<HWND>(winId());
    if (!parentWindow) {
        renderHtmlPlaceholder(tr("Cannot create native widget window."));
        return;
    }

    const QString userDataDir = QDir(AppPaths::userLocalConfigDir()).filePath(QStringLiteral("LinWidgets/shared-webview2"));
    const std::wstring userData = QDir::toNativeSeparators(userDataDir).toStdWString();
    const HRESULT result = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,
        userData.c_str(),
        nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, parentWindow, entryPath](HRESULT envResult, ICoreWebView2Environment* environment) -> HRESULT {
                if (FAILED(envResult) || !environment) {
                    appendLog(QStringLiteral("error"), QStringLiteral("Failed to initialize shared WebView2 environment"));
                    renderHtmlPlaceholder(tr("Failed to initialize WebView2 environment."));
                    return S_OK;
                }

                environment->CreateCoreWebView2Controller(
                    parentWindow,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this, entryPath](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(controllerResult) || !controller) {
                                appendLog(QStringLiteral("error"), QStringLiteral("Failed to create shared WebView2 controller"));
                                renderHtmlPlaceholder(tr("Failed to create WebView2 controller."));
                                return S_OK;
                            }

                            controller->AddRef();
                            m_webViewController = controller;
                            ICoreWebView2* webView = nullptr;
                            controller->get_CoreWebView2(&webView);
                            m_webView = webView;
                            updateWebViewBounds();

                            ComPtr<ICoreWebView2Controller2> controller2;
                            if (SUCCEEDED(controller->QueryInterface(IID_PPV_ARGS(&controller2)))) {
                                COREWEBVIEW2_COLOR transparent = { 0, 0, 0, 0 };
                                controller2->put_DefaultBackgroundColor(transparent);
                            }
                            ComPtr<ICoreWebView2Controller4> controller4;
                            if (SUCCEEDED(controller->QueryInterface(IID_PPV_ARGS(&controller4)))) {
                                controller4->put_AllowExternalDrop(FALSE);
                            }

                            ComPtr<ICoreWebView2Settings> settings;
                            if (webView && SUCCEEDED(webView->get_Settings(&settings))) {
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_AreDevToolsEnabled(TRUE);
                            }

                            if (webView) {
                                installWebConsoleLogging();
                                installSharedWidgetApiBridge();
                                EventRegistrationToken token = {};
                                webView->add_WebMessageReceived(
                                    Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                        [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                            LPWSTR rawMessage = nullptr;
                                            if (SUCCEEDED(args->get_WebMessageAsJson(&rawMessage)) && rawMessage) {
                                                handleWebMessageJson(QString::fromWCharArray(rawMessage));
                                                CoTaskMemFree(rawMessage);
                                            }
                                            return S_OK;
                                        }).Get(),
                                    &token);
                                const QString uri = QUrl::fromLocalFile(entryPath).toString();
                                appendLog(QStringLiteral("info"), QStringLiteral("navigate shared runtime=%1").arg(uri));
                                webView->Navigate(reinterpret_cast<LPCWSTR>(uri.utf16()));
                            }
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());

    if (FAILED(result)) {
        appendLog(QStringLiteral("error"), QStringLiteral("Failed to start shared WebView2 environment"));
        renderHtmlPlaceholder(tr("Failed to start WebView2 environment."));
    }
#endif
}

QString WidgetHostRuntime::sharedRuntimeEntryPath() const
{
    const QString relativePath = QStringLiteral("resources/LinWidgetRuntime/host.html");
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        AppPaths::userLinWidgetRuntimeHostFilePath(),
        QDir(appDir).filePath(relativePath),
        QDir(QDir::currentPath()).filePath(relativePath),
        QDir(appDir).filePath(QStringLiteral("../resources/LinWidgetRuntime/host.html")),
        QDir(appDir).filePath(QStringLiteral("../../resources/LinWidgetRuntime/host.html"))
    };

    QStringList checkedPaths;
    for (const QString& candidate : candidates) {
        const QString cleanPath = QDir::cleanPath(candidate);
        if (checkedPaths.contains(cleanPath, Qt::CaseInsensitive)) {
            continue;
        }
        checkedPaths.append(cleanPath);
        if (QFileInfo::exists(cleanPath)) {
            appendLog(QStringLiteral("info"), QStringLiteral("shared runtime page=%1").arg(cleanPath));
            return cleanPath;
        }
    }

    appendLog(QStringLiteral("error"),
              QStringLiteral("shared runtime page missing. appDir=%1 cwd=%2 checked=%3")
                  .arg(appDir, QDir::currentPath(), checkedPaths.join(QStringLiteral("; "))));
    return checkedPaths.isEmpty() ? QDir(appDir).filePath(relativePath) : checkedPaths.first();
}

void WidgetHostRuntime::installSharedWidgetApiBridge()
{
#ifdef LINDESK_HAS_WEBVIEW2
    auto* webView = static_cast<ICoreWebView2*>(m_webView);
    if (!webView) {
        return;
    }

    const QString script = QStringLiteral(
        "(() => {"
        "try {"
        "const postToHost = (type, value) => {"
        " const payload = Object.assign({type:type}, value && typeof value === 'object' ? value : {});"
        " if (window.parent && window.parent !== window) {"
        "  window.parent.postMessage(payload, '*');"
        "  return;"
        " }"
        " try { chrome.webview.postMessage(payload); } catch (_) {}"
        "};"
        "const api = window.desktop && typeof window.desktop === 'object' ? window.desktop : {};"
        "api.resizeWidget = value => postToHost('linwidget_resize_widget', value);"
        "api.setWidgetGeometry = value => postToHost('linwidget_set_widget_geometry', value);"
        "window.desktop = api;"
        "} catch (error) { try { console.error('LinWidget desktop bridge failed', error); } catch (_) {} }"
        "})();");

    webView->AddScriptToExecuteOnDocumentCreated(reinterpret_cast<LPCWSTR>(script.utf16()), nullptr);
#endif
}

void WidgetHostRuntime::sendHostPageMessage(const QJsonObject& payload)
{
#ifdef LINDESK_HAS_WEBVIEW2
    auto* webView = static_cast<ICoreWebView2*>(m_webView);
    if (!webView) {
        return;
    }
    const QString json = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    webView->PostWebMessageAsJson(reinterpret_cast<LPCWSTR>(json.utf16()));
#else
    Q_UNUSED(payload);
#endif
}

void WidgetHostRuntime::queueOrSendHostPageMessage(const QJsonObject& payload)
{
    if (!m_hostPageReady) {
        m_pendingHostPageMessages.append(payload);
        appendLog(QStringLiteral("info"), QStringLiteral("queued host page message type=%1 pending=%2")
            .arg(payload.value(QStringLiteral("type")).toString(),
                 QString::number(m_pendingHostPageMessages.size())));
        return;
    }
    sendHostPageMessage(payload);
}

void WidgetHostRuntime::flushPendingHostPageMessages()
{
    if (!m_hostPageReady || m_pendingHostPageMessages.isEmpty()) {
        return;
    }
    const QVector<QJsonObject> pending = m_pendingHostPageMessages;
    m_pendingHostPageMessages.clear();
    appendLog(QStringLiteral("info"), QStringLiteral("flushing host page messages count=%1").arg(pending.size()));
    for (const QJsonObject& payload : pending) {
        sendHostPageMessage(payload);
    }
}

QJsonObject WidgetHostRuntime::widgetInstancePayload(const QString& instanceId,
                                                     const QString& packagePath,
                                                     const WidgetManifest& manifest,
                                                     const QRect& geometry,
                                                     const QStringList& permissions,
                                                     bool isDevelopment,
                                                     bool autoReload,
                                                     const QString& entryUrl) const
{
    QJsonArray permissionArray;
    for (const QString& permission : permissions) {
        permissionArray.append(permission);
    }
    QJsonObject geometryObject;
    geometryObject.insert(QStringLiteral("x"), geometry.x());
    geometryObject.insert(QStringLiteral("y"), geometry.y());
    geometryObject.insert(QStringLiteral("width"), geometry.width());
    geometryObject.insert(QStringLiteral("height"), geometry.height());

    QJsonObject payload;
    payload.insert(QStringLiteral("type"), QString::fromLatin1(IpcType::AddWidget));
    payload.insert(QStringLiteral("instanceId"), instanceId);
    payload.insert(QStringLiteral("widgetId"), instanceId);
    payload.insert(QStringLiteral("packagePath"), packagePath);
    payload.insert(QStringLiteral("name"), manifest.name);
    payload.insert(QStringLiteral("resizable"), manifest.resizable);
    payload.insert(QStringLiteral("minWidth"), manifest.minWidth);
    payload.insert(QStringLiteral("minHeight"), manifest.minHeight);
    payload.insert(QStringLiteral("maxWidth"), manifest.maxWidth);
    payload.insert(QStringLiteral("maxHeight"), manifest.maxHeight);
    payload.insert(QStringLiteral("development"), isDevelopment);
    payload.insert(QStringLiteral("autoReload"), autoReload);
    payload.insert(QStringLiteral("permissions"), permissionArray);
    payload.insert(QStringLiteral("geometry"), geometryObject);
    payload.insert(QStringLiteral("entryUrl"), entryUrl);
    return payload;
}

QString WidgetHostRuntime::virtualHostNameForInstance(const QString& instanceId) const
{
    QString safe = instanceId.toLower();
    for (QChar& ch : safe) {
        if (!ch.isLetterOrNumber()) {
            ch = QLatin1Char('-');
        }
    }
    while (safe.contains(QStringLiteral("--"))) {
        safe.replace(QStringLiteral("--"), QStringLiteral("-"));
    }
    safe = safe.trimmed();
    if (safe.startsWith(QLatin1Char('-'))) {
        safe.remove(0, 1);
    }
    if (safe.endsWith(QLatin1Char('-'))) {
        safe.chop(1);
    }
    if (safe.isEmpty()) {
        safe = QStringLiteral("widget");
    }
    return safe + QStringLiteral(".linwidget.local");
}

bool WidgetHostRuntime::mapWidgetVirtualHost(const QString& hostName, const QString& packagePath)
{
#ifdef LINDESK_HAS_WEBVIEW2
    auto* webView = static_cast<ICoreWebView2*>(m_webView);
    if (!webView) {
        return false;
    }
    ComPtr<ICoreWebView2_3> webView3;
    if (FAILED(webView->QueryInterface(IID_PPV_ARGS(&webView3)))) {
        return false;
    }
    const std::wstring host = hostName.toStdWString();
    const std::wstring folder = QDir::toNativeSeparators(packagePath).toStdWString();
    return SUCCEEDED(webView3->SetVirtualHostNameToFolderMapping(
        host.c_str(),
        folder.c_str(),
        COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS));
#else
    Q_UNUSED(hostName);
    Q_UNUSED(packagePath);
    return false;
#endif
}

void WidgetHostRuntime::setSharedWindowGeometry()
{
    const QRect virtualRect = QGuiApplication::primaryScreen()
        ? QGuiApplication::primaryScreen()->virtualGeometry()
        : QRect(0, 0, 1920, 1080);
    setGeometry(virtualRect);
    appendLog(QStringLiteral("info"), QStringLiteral("initial shared host geometry %1,%2 %3x%4")
        .arg(QString::number(virtualRect.x()),
             QString::number(virtualRect.y()),
             QString::number(virtualRect.width()),
             QString::number(virtualRect.height())));
    updateWebViewBounds();
}

void WidgetHostRuntime::updateSharedWindowRegion()
{
    if (!m_sharedRuntime) {
        return;
    }
#ifdef Q_OS_WIN
    HWND hostWindow = reinterpret_cast<HWND>(winId());
    updateNativeWindowRegion(hostWindow);

    WebViewWindowSearch webViewSearch;
    if (hostWindow) {
        EnumChildWindows(hostWindow, findWebViewChildWindow, reinterpret_cast<LPARAM>(&webViewSearch));
    }
    if (webViewSearch.window) {
        updateNativeWindowRegion(webViewSearch.window);
    }
    appendLog(QStringLiteral("hit"), QStringLiteral("synced shared window region rectCount=%1 webview=%2 class=%3")
        .arg(QString::number(m_sharedWidgetGeometries.size()),
             QString::number(reinterpret_cast<quintptr>(webViewSearch.window), 16),
             webViewSearch.className));
#endif
}

void WidgetHostRuntime::updateNativeWindowRegion(void* windowHandle)
{
    if (!m_sharedRuntime) {
        return;
    }
#ifdef Q_OS_WIN
    HWND window = reinterpret_cast<HWND>(windowHandle);
    if (!window) {
        return;
    }

    if (m_showWebViewBounds) {
        SetWindowRgn(window, nullptr, TRUE);
        return;
    }

    HRGN combinedRegion = CreateRectRgn(0, 0, 0, 0);
    for (const QRect& rect : m_sharedWidgetGeometries) {
        if (!rect.isValid()) {
            continue;
        }
        HRGN widgetRegion = CreateRectRgn(rect.left(),
                                          rect.top(),
                                          rect.right() + 1,
                                          rect.bottom() + 1);
        CombineRgn(combinedRegion, combinedRegion, widgetRegion, RGN_OR);
        DeleteObject(widgetRegion);
    }
    SetWindowRgn(window, combinedRegion, TRUE);
#else
    Q_UNUSED(windowHandle);
#endif
}

void WidgetHostRuntime::renderHtmlPlaceholder(const QString& reason)
{
    PermissionManager permissions;
    const QString permissionText = tr("network: %1\nstorage: %2\ndesktop: %3")
        .arg(permissions.canUseNetwork(m_grantedPermissions) ? QStringLiteral("allowed") : QStringLiteral("blocked"),
             permissions.canUseStorage(m_grantedPermissions) ? QStringLiteral("allowed") : QStringLiteral("blocked"),
             permissions.canUseDesktopApi(m_grantedPermissions) ? QStringLiteral("allowed") : QStringLiteral("blocked"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    const QString reasonText = reason.isEmpty() ? QString() : QStringLiteral("\n%1").arg(reason);
    m_placeholderLabel = new QLabel(tr("HTML Widget Host\n%1\n%2%3").arg(m_manifest.name, permissionText, reasonText), this);
    m_placeholderLabel->setAlignment(Qt::AlignCenter);
    m_placeholderLabel->setStyleSheet(QStringLiteral(
        "QLabel {"
        "background: rgba(18,24,38,190);"
        "border: 1px solid rgba(255,255,255,90);"
        "border-radius: 10px;"
        "color: white;"
        "font-weight: 700;"
        "}"));
    layout->addWidget(m_placeholderLabel);
}

void WidgetHostRuntime::installWebConsoleLogging()
{
#ifdef LINDESK_HAS_WEBVIEW2
    auto* webView = static_cast<ICoreWebView2*>(m_webView);
    if (!webView) {
        return;
    }

    const QString script = QStringLiteral(
        "(() => {"
        "try {"
        "const stringify = value => {"
        " try {"
        "  if (value instanceof Error) return value.stack || value.message;"
        "  if (typeof value === 'string') return value;"
        "  return JSON.stringify(value);"
        " } catch (_) { return String(value); }"
        "};"
        "const postLog = (type, payload) => {"
        " try { chrome.webview.postMessage(Object.assign({type:type}, payload)); } catch (_) {}"
        "};"
        "['log','info','warn','error'].forEach(level => {"
        " const original = console[level];"
        " console[level] = (...args) => {"
        "  postLog('console_log', {level:level, message:args.map(stringify).join(' ')});"
        "  if (original) original.apply(console, args);"
        " };"
        "});"
        "window.addEventListener('error', e => {"
        " postLog('js_error', {message:e.message || '', source:e.filename || '', line:e.lineno || 0, column:e.colno || 0});"
        "});"
        "window.addEventListener('unhandledrejection', e => {"
        " postLog('js_rejection', {message:stringify(e.reason)});"
        "});"
        "} catch (error) { try { console.error('LinWidget logging bridge failed', error); } catch (_) {} }"
        "})();");

    webView->AddScriptToExecuteOnDocumentCreated(reinterpret_cast<LPCWSTR>(script.utf16()), nullptr);
#endif
}

void WidgetHostRuntime::installWebPermissionGuards()
{
#ifdef LINDESK_HAS_WEBVIEW2
    auto* webView = static_cast<ICoreWebView2*>(m_webView);
    if (!webView) {
        return;
    }

    PermissionManager permissions;
    QString script = QStringLiteral("(() => {");
    if (!permissions.canUseNetwork(m_grantedPermissions)) {
        script += QStringLiteral(
            "const denyNetwork=()=>{throw new Error('LinWidget network permission denied');};"
            "window.fetch=(...args)=>Promise.reject(new Error('LinWidget network permission denied'));"
            "window.XMLHttpRequest=function(){denyNetwork();};"
            "window.WebSocket=function(){denyNetwork();};"
            "window.EventSource=function(){denyNetwork();};");
    }
    if (!permissions.canUseStorage(m_grantedPermissions)) {
        script += QStringLiteral(
            "const denyStorage=()=>{throw new Error('LinWidget storage permission denied');};"
            "try{Object.defineProperty(window,'localStorage',{get:denyStorage});}catch(_){ }"
            "try{Object.defineProperty(window,'sessionStorage',{get:denyStorage});}catch(_){ }"
            "try{Object.defineProperty(window,'indexedDB',{get:denyStorage});}catch(_){ }");
    }
    script += QStringLiteral("})();");

    webView->AddScriptToExecuteOnDocumentCreated(reinterpret_cast<LPCWSTR>(script.utf16()), nullptr);
#endif
}

void WidgetHostRuntime::installWebViewDragBridge()
{
#ifdef LINDESK_HAS_WEBVIEW2
    auto* webView = static_cast<ICoreWebView2*>(m_webView);
    if (!webView) {
        return;
    }

    const QString script = QStringLiteral(
        "(() => {"
        "try {"
        "let dragCandidate=false,dragging=false,lastX=0,lastY=0,startX=0,startY=0;"
        "const post = message => { try { chrome.webview.postMessage(message); } catch (_) {} };"
        "const isInteractiveTarget = target => !!(target && target.closest && target.closest("
        "\"input,textarea,select,button,a,[contenteditable='true'],[contenteditable=''],[data-no-drag]\"));"
        "const describeTarget = target => target ? ((target.tagName||'').toLowerCase() + (target.id ? '#'+target.id : '') + (target.className ? '.'+String(target.className).replace(/\\s+/g,'.') : '')) : '';"
        "document.addEventListener('focusin', e => { post({type:'dom_focusin', target:describeTarget(e.target)}); }, true);"
        "document.addEventListener('keydown', e => { post({type:'dom_keydown', key:e.key||'', code:e.code||'', target:describeTarget(e.target), defaultPrevented:!!e.defaultPrevented}); }, true);"
        "document.addEventListener('input', e => { post({type:'dom_input', target:describeTarget(e.target), value:e.target && typeof e.target.value === 'string' ? e.target.value : ''}); }, true);"
        "window.addEventListener('pointerdown', e => {"
        " if (e.button !== 0) return;"
        " if (isInteractiveTarget(e.target)) {"
        "  return;"
        " }"
        " post({type:'focus_request'});"
        " dragCandidate=true; dragging=false; startX=e.screenX; startY=e.screenY; lastX=e.screenX; lastY=e.screenY;"
        " try { e.target.setPointerCapture && e.target.setPointerCapture(e.pointerId); } catch (_) {}"
        "});"
        "window.addEventListener('pointermove', e => {"
        " if (!dragCandidate && !dragging) return;"
        " if (dragCandidate && !dragging) {"
        "  const totalDx=e.screenX-startX, totalDy=e.screenY-startY;"
        "  if (Math.abs(totalDx) + Math.abs(totalDy) < 4) return;"
        "  dragging=true; dragCandidate=false; post({type:'drag_start'});"
        " }"
        " const dx=e.screenX-lastX, dy=e.screenY-lastY; lastX=e.screenX; lastY=e.screenY;"
        " post({type:'drag_move', dx:dx, dy:dy});"
        "});"
        "window.addEventListener('pointerup', () => { dragCandidate=false; dragging=false; post({type:'drag_end'}); });"
        "window.addEventListener('pointercancel', () => { dragCandidate=false; dragging=false; post({type:'drag_end'}); });"
        "} catch (error) { console.error('LinWidget bridge failed', error); }"
        "})();");

    webView->AddScriptToExecuteOnDocumentCreated(reinterpret_cast<LPCWSTR>(script.utf16()), nullptr);
    EventRegistrationToken token = {};
    webView->add_WebMessageReceived(
        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                LPWSTR rawMessage = nullptr;
                if (SUCCEEDED(args->get_WebMessageAsJson(&rawMessage)) && rawMessage) {
                    handleWebMessageJson(QString::fromWCharArray(rawMessage));
                    CoTaskMemFree(rawMessage);
                }
                return S_OK;
            }).Get(),
        &token);
#endif
}

void WidgetHostRuntime::handleWebMessageJson(const QString& json)
{
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
    if (!document.isObject()) {
        return;
    }
    const QJsonObject object = document.object();
    const QString type = object.value(QStringLiteral("type")).toString();
    if (m_sharedRuntime && type == QString::fromLatin1(IpcType::Ready)) {
        m_hostPageReady = true;
        appendLog(QStringLiteral("info"), QStringLiteral("shared host page ready"));
        flushPendingHostPageMessages();
        sendIpcMessage(QString::fromLatin1(IpcType::Ready));
        return;
    }
    if (m_sharedRuntime && type == QString::fromLatin1(IpcType::WidgetGeometryChanged)) {
        const QString instanceId = object.value(QStringLiteral("instanceId")).toString(object.value(QStringLiteral("widgetId")).toString()).trimmed();
        if (instanceId.isEmpty()) {
            return;
        }
        const QRect rect(object.value(QStringLiteral("x")).toInt(),
                         object.value(QStringLiteral("y")).toInt(),
                         object.value(QStringLiteral("width")).toInt(),
                         object.value(QStringLiteral("height")).toInt());
        if (rect.isValid()) {
            m_sharedWidgetGeometries.insert(instanceId, rect);
            updateSharedWindowRegion();
        }
        if (!object.value(QStringLiteral("live")).toBool(false)) {
            sendIpcMessage(QString::fromLatin1(IpcType::WidgetGeometryChanged), object);
        }
        return;
    }
    if (type == QStringLiteral("console_log")) {
        appendLog(QStringLiteral("console.") + object.value(QStringLiteral("level")).toString(QStringLiteral("log")),
                  object.value(QStringLiteral("message")).toString());
        return;
    }
    if (type == QStringLiteral("js_error")) {
        appendLog(QStringLiteral("js.error"),
                  QStringLiteral("%1 at %2:%3:%4")
                      .arg(object.value(QStringLiteral("message")).toString(),
                           object.value(QStringLiteral("source")).toString(),
                           QString::number(object.value(QStringLiteral("line")).toInt()),
                           QString::number(object.value(QStringLiteral("column")).toInt())));
        return;
    }
    if (type == QStringLiteral("js_rejection")) {
        appendLog(QStringLiteral("js.rejection"), object.value(QStringLiteral("message")).toString());
        return;
    }
    if (type == QStringLiteral("focus_request")) {
        focusWebView();
        return;
    }
    if (type == QStringLiteral("dom_focusin")) {
        appendLog(QStringLiteral("dom"), QStringLiteral("focusin target=%1")
            .arg(object.value(QStringLiteral("target")).toString()));
        return;
    }
    if (type == QStringLiteral("dom_keydown")) {
        appendLog(QStringLiteral("dom"), QStringLiteral("keydown key=%1 code=%2 target=%3 prevented=%4")
            .arg(object.value(QStringLiteral("key")).toString(),
                 object.value(QStringLiteral("code")).toString(),
                 object.value(QStringLiteral("target")).toString(),
                 object.value(QStringLiteral("defaultPrevented")).toBool(false) ? QStringLiteral("true") : QStringLiteral("false")));
        return;
    }
    if (type == QStringLiteral("dom_input")) {
        appendLog(QStringLiteral("dom"), QStringLiteral("input target=%1 valueLength=%2")
            .arg(object.value(QStringLiteral("target")).toString(),
                 QString::number(object.value(QStringLiteral("value")).toString().length())));
        return;
    }
    if (type == QStringLiteral("linwidget_resize_widget") ||
        type == QStringLiteral("linwidget_set_widget_geometry")) {
        if (!m_manifest.resizable) {
            return;
        }
        const bool includePosition = type == QStringLiteral("linwidget_set_widget_geometry");
        const QJsonObject geometry = object.value(QStringLiteral("geometry")).toObject();
        const QJsonObject source = geometry.isEmpty() ? object : geometry;
        QRect nextGeometry(
            includePosition ? source.value(QStringLiteral("x")).toInt(x()) : x(),
            includePosition ? source.value(QStringLiteral("y")).toInt(y()) : y(),
            source.value(QStringLiteral("width")).toInt(width()),
            source.value(QStringLiteral("height")).toInt(height()));
        applyHostGeometry(nextGeometry);
        QJsonObject extra;
        extra.insert(QStringLiteral("x"), this->x());
        extra.insert(QStringLiteral("y"), this->y());
        extra.insert(QStringLiteral("width"), width());
        extra.insert(QStringLiteral("height"), height());
        sendIpcMessage(QString::fromLatin1(IpcType::WidgetGeometryChanged), extra);
        return;
    }
    if (type == QStringLiteral("drag_move")) {
        moveByDelta(QPoint(object.value(QStringLiteral("dx")).toInt(),
                           object.value(QStringLiteral("dy")).toInt()));
    }
}

void WidgetHostRuntime::appendLog(const QString& level, const QString& message) const
{
    const QString line = QStringLiteral("[%1] [%2] %3\n")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
             level,
             message);
    qDebug().noquote() << QStringLiteral("[LinWidgetRuntime][%1] %2").arg(level, message);

    QFile file(logFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qDebug().noquote() << QStringLiteral("[LinWidgetRuntime][warn] cannot write log file: %1 error=%2")
            .arg(logFilePath(), file.errorString());
        return;
    }
    file.write(line.toUtf8());
}

QString WidgetHostRuntime::logFilePath() const
{
    QString safeId = m_widgetId.trimmed();
    for (QChar& ch : safeId) {
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('.') && ch != QLatin1Char('-') && ch != QLatin1Char('_')) {
            ch = QLatin1Char('_');
        }
    }
    if (safeId.isEmpty()) {
        safeId = QStringLiteral("widget");
    }
    return QDir(AppPaths::linWidgetLogsDir()).filePath(safeId + QStringLiteral(".log"));
}

void WidgetHostRuntime::focusWebView()
{
#ifdef Q_OS_WIN
    HWND hostWindow = reinterpret_cast<HWND>(winId());
    HWND rootWindow = rootOwnerWindow(hostWindow);
    if (rootWindow && !m_embedded) {
        SetForegroundWindow(rootWindow);
    }
    if (m_embedded && hostWindow) {
        SetActiveWindow(hostWindow);
    }
    WebViewWindowSearch webViewSearch;
    if (hostWindow) {
        EnumChildWindows(hostWindow, findWebViewChildWindow, reinterpret_cast<LPARAM>(&webViewSearch));
    }
#endif
#ifdef LINDESK_HAS_WEBVIEW2
    auto* controller = static_cast<ICoreWebView2Controller*>(m_webViewController);
    if (controller) {
        controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    }
#endif
#ifdef Q_OS_WIN
    if (webViewSearch.window) {
        activateNativeWindow(webViewSearch.window);
    }
    const HWND focusedWindow = GetFocus();
    appendLog(QStringLiteral("focus"), QStringLiteral("root=%1 host=%2 webview=%3 webviewClass=%4 focused=%5 focusedClass=%6 foreground=%7")
        .arg(QString::number(reinterpret_cast<quintptr>(rootWindow), 16),
             QString::number(reinterpret_cast<quintptr>(hostWindow), 16),
             QString::number(reinterpret_cast<quintptr>(webViewSearch.window), 16),
             webViewSearch.className,
             QString::number(reinterpret_cast<quintptr>(focusedWindow), 16),
             nativeWindowClassName(focusedWindow),
             QString::number(reinterpret_cast<quintptr>(GetForegroundWindow()), 16)));
#endif
}

void WidgetHostRuntime::moveByDelta(const QPoint& delta)
{
    if (delta.isNull()) {
        return;
    }
    const QPoint topLeft = m_embedded ? clampToParent(pos() + delta) : pos() + delta;
    applyHostPosition(topLeft);

    QJsonObject extra;
    extra.insert(QStringLiteral("x"), topLeft.x());
    extra.insert(QStringLiteral("y"), topLeft.y());
    sendIpcMessage(QString::fromLatin1(IpcType::MoveWidget), extra);
}

QSize WidgetHostRuntime::clampWidgetSize(const QSize& requestedSize) const
{
    QSize finalSize = requestedSize.isValid()
        ? requestedSize
        : QSize(m_manifest.width, m_manifest.height);
    if (!m_manifest.resizable) {
        return QSize(m_manifest.width, m_manifest.height);
    }

    finalSize.setWidth(qMax(finalSize.width(), m_manifest.minWidth));
    finalSize.setHeight(qMax(finalSize.height(), m_manifest.minHeight));
    if (m_manifest.maxWidth > 0) {
        finalSize.setWidth(qMin(finalSize.width(), m_manifest.maxWidth));
    }
    if (m_manifest.maxHeight > 0) {
        finalSize.setHeight(qMin(finalSize.height(), m_manifest.maxHeight));
    }
    if (m_embedded || m_parentWindowId != 0) {
        const QRect parentRect = parentClientRect();
        if (parentRect.isValid()) {
            finalSize.setWidth(qMin(finalSize.width(), qMax(m_manifest.minWidth, parentRect.width() - x())));
            finalSize.setHeight(qMin(finalSize.height(), qMax(m_manifest.minHeight, parentRect.height() - y())));
        }
    }
    return finalSize;
}

void WidgetHostRuntime::applyHostPosition(const QPoint& topLeft)
{
    const QPoint finalTopLeft = m_embedded ? clampToParent(topLeft) : topLeft;
#ifdef Q_OS_WIN
    if (m_embedded) {
        HWND hostWindow = reinterpret_cast<HWND>(winId());
        if (hostWindow) {
            SetWindowPos(hostWindow, nullptr,
                         finalTopLeft.x(), finalTopLeft.y(),
                         width(), height(),
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
#endif
    move(finalTopLeft);
}

void WidgetHostRuntime::applyHostGeometry(const QRect& geometry)
{
    const QSize finalSize = clampWidgetSize(geometry.size());
    const QPoint finalTopLeft = m_embedded ? clampToParent(geometry.topLeft()) : geometry.topLeft();
#ifdef Q_OS_WIN
    if (m_embedded) {
        HWND hostWindow = reinterpret_cast<HWND>(winId());
        if (hostWindow) {
            SetWindowPos(hostWindow, nullptr,
                         finalTopLeft.x(), finalTopLeft.y(),
                         finalSize.width(), finalSize.height(),
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
#endif
    m_applyingManagerGeometry = true;
    setGeometry(QRect(finalTopLeft, finalSize));
    m_applyingManagerGeometry = false;
    updateWebViewBounds();
}

void WidgetHostRuntime::updateWebViewBounds()
{
#ifdef LINDESK_HAS_WEBVIEW2
    auto* controller = static_cast<ICoreWebView2Controller*>(m_webViewController);
    if (!controller) {
        appendLog(QStringLiteral("bounds"), QStringLiteral("skip WebView bounds update because controller is null, widget=%1 size=%2x%3")
            .arg(m_widgetId, QString::number(width()), QString::number(height())));
        return;
    }
    const RECT bounds = { 0, 0, width(), height() };
    const HRESULT result = controller->put_Bounds(bounds);
    appendLog(QStringLiteral("bounds"), QStringLiteral("WebView bounds set widget=%1 size=%2x%3 result=0x%4")
        .arg(m_widgetId,
             QString::number(width()),
             QString::number(height()),
             QString::number(static_cast<qulonglong>(result), 16)));
    if (m_sharedRuntime) {
        updateSharedWindowRegion();
    }
#endif
}

}
