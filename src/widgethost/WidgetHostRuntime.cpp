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

namespace {
QStringList normalizedPermissionList(const QStringList& permissions)
{
    return PermissionManager().normalizePermissions(permissions);
}

bool hasNetworkPermission(const QStringList& permissions)
{
    return PermissionManager().canUseNetwork(permissions);
}

#ifdef LINDESK_HAS_WEBVIEW2
QString webResourceHeaderValue(ICoreWebView2HttpRequestHeaders* headers, const wchar_t* name)
{
    if (!headers || !name) {
        return QString();
    }
    LPWSTR rawValue = nullptr;
    if (FAILED(headers->GetHeader(name, &rawValue)) || !rawValue) {
        return QString();
    }
    const QString value = QString::fromWCharArray(rawValue);
    CoTaskMemFree(rawValue);
    return value;
}
#endif

QString permissionHostSegment(const QStringList& permissions, bool isDevelopment)
{
    if (isDevelopment) {
        return QStringLiteral("pdevnsd");
    }

    QString marker = QStringLiteral("p");
    if (PermissionManager().canUseNetwork(permissions)) {
        marker.append(QLatin1Char('n'));
    }
    if (PermissionManager().canUseStorage(permissions)) {
        marker.append(QLatin1Char('s'));
    }
    if (PermissionManager().canUseDesktopApi(permissions)) {
        marker.append(QLatin1Char('d'));
    }
    return marker;
}
}

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

HWINEVENTHOOK g_focusEventHook = nullptr;
HWND g_trackedWebViewWindow = nullptr;
QString g_focusEventLogPath;
QString g_lastFocusEventKey;

void appendFocusEventLog(const QString& message)
{
    if (g_focusEventLogPath.isEmpty()) {
        return;
    }
    QFile file(g_focusEventLogPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }
    const QString line = QStringLiteral("[%1] [focus.event] %2\n")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz")),
             message);
    file.write(line.toUtf8());
}

void CALLBACK linWidgetFocusEventProc(HWINEVENTHOOK,
                                      DWORD event,
                                      HWND hwnd,
                                      LONG idObject,
                                      LONG idChild,
                                      DWORD eventThread,
                                      DWORD eventTime)
{
    if (event != EVENT_OBJECT_FOCUS || !hwnd) {
        return;
    }

    DWORD processId = 0;
    const DWORD windowThread = GetWindowThreadProcessId(hwnd, &processId);
    const HWND foreground = GetForegroundWindow();
    const HWND parent = GetParent(hwnd);
    const HWND root = GetAncestor(hwnd, GA_ROOT);
    const bool tracked = hwnd == g_trackedWebViewWindow;
    const QString className = nativeWindowClassName(hwnd);
    const QString key = QStringLiteral("%1:%2:%3:%4")
        .arg(QString::number(reinterpret_cast<quintptr>(hwnd), 16),
             QString::number(idObject),
             QString::number(idChild),
             className);
    if (key == g_lastFocusEventKey) {
        return;
    }
    g_lastFocusEventKey = key;

    appendFocusEventLog(QStringLiteral("hwnd=%1 class=%2 trackedWebView=%3 parent=%4 root=%5 foreground=%6 process=%7 thread=%8 eventThread=%9 object=%10 child=%11 time=%12")
        .arg(QString::number(reinterpret_cast<quintptr>(hwnd), 16),
             className,
             tracked ? QStringLiteral("true") : QStringLiteral("false"),
             QString::number(reinterpret_cast<quintptr>(parent), 16),
             QString::number(reinterpret_cast<quintptr>(root), 16),
             QString::number(reinterpret_cast<quintptr>(foreground), 16),
             QString::number(processId),
             QString::number(windowThread),
             QString::number(eventThread),
             QString::number(idObject),
             QString::number(idChild),
             QString::number(eventTime)));
}

void installFocusEventDiagnostics(HWND webViewWindow, const QString& logPath)
{
    if (!webViewWindow || !IsWindow(webViewWindow)) {
        return;
    }
    g_trackedWebViewWindow = webViewWindow;
    g_focusEventLogPath = logPath;
    if (g_focusEventHook) {
        return;
    }
    g_focusEventHook = SetWinEventHook(EVENT_OBJECT_FOCUS,
                                       EVENT_OBJECT_FOCUS,
                                       nullptr,
                                       linWidgetFocusEventProc,
                                       0,
                                       0,
                                       WINEVENT_OUTOFCONTEXT);
    appendFocusEventLog(QStringLiteral("hook installed tracked=%1 class=%2 hook=%3")
        .arg(QString::number(reinterpret_cast<quintptr>(webViewWindow), 16),
             nativeWindowClassName(webViewWindow),
             QString::number(reinterpret_cast<quintptr>(g_focusEventHook), 16)));
}

void uninstallFocusEventDiagnostics()
{
    if (g_focusEventHook) {
        UnhookWinEvent(g_focusEventHook);
        appendFocusEventLog(QStringLiteral("hook uninstalled"));
        g_focusEventHook = nullptr;
    }
    g_trackedWebViewWindow = nullptr;
    g_lastFocusEventKey.clear();
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
#ifdef Q_OS_WIN
    if (m_sharedRuntime) {
        uninstallFocusEventDiagnostics();
    }
#endif
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
    const bool sharedChildRuntime = m_sharedRuntime && m_parentWindowId != 0;
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
    setFocusPolicy((m_parentWindowId == 0 || sharedChildRuntime) ? Qt::StrongFocus : Qt::NoFocus);

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
    if (m_sharedRuntime) {
        exStyle &= ~WS_EX_NOACTIVATE;
        exStyle |= WS_EX_TOOLWINDOW;
    } else {
        exStyle &= ~WS_EX_NOACTIVATE;
    }
    SetWindowLongPtr(hostWindow, GWL_EXSTYLE, exStyle);

    const QRect clampedGeometry = m_sharedRuntime
        ? QRect(QPoint(0, 0), parentClientRect().size())
        : QRect(clampToParent(geometry().topLeft()), geometry().size());
    SetWindowPos(hostWindow, HWND_TOP,
                 clampedGeometry.x(), clampedGeometry.y(),
                 clampedGeometry.width(), clampedGeometry.height(),
                 SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
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
                syncSharedRegion(QStringLiteral("host geometry"));
            }
            return;
        }
        if (type == QString::fromLatin1(IpcType::UpdateHostOptions)) {
            m_showWebViewBounds = message.value(QStringLiteral("showWebViewBounds")).toBool(false);
            syncSharedRegion(QStringLiteral("host options"));
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
            QStringList permissions;
            const QJsonArray permissionArray = message.value(QStringLiteral("permissions")).toArray();
            for (const QJsonValue& value : permissionArray) {
                const QString permission = value.toString().trimmed();
                if (!permission.isEmpty()) {
                    permissions.append(permission);
                }
            }
            permissions = normalizedPermissionList(permissions);
            const bool isDevelopment = message.value(QStringLiteral("development")).toBool(false);
            WidgetManifest manifest;
            QString manifestError;
            if (instanceId.isEmpty() || !WidgetPackage::loadManifest(packagePath, &manifest, &manifestError)) {
                sendIpcMessage(QString::fromLatin1(IpcType::WidgetError), {
                    { QStringLiteral("instanceId"), instanceId },
                    { QStringLiteral("message"), manifestError.isEmpty() ? QStringLiteral("Invalid widget package.") : manifestError }
                });
                return;
            }
            const QString hostName = virtualHostNameForInstance(instanceId, permissions, isDevelopment);
            QString entryUrl;
            if (!mapWidgetVirtualHost(hostName, packagePath)) {
                const QString error = QStringLiteral("Failed to map widget virtual host for %1.").arg(instanceId);
                appendLog(QStringLiteral("error"), error);
                appendInstanceLog(instanceId, QStringLiteral("error"), error);
                sendIpcMessage(QString::fromLatin1(IpcType::WidgetError), {
                    { QStringLiteral("instanceId"), instanceId },
                    { QStringLiteral("message"), error }
                });
                return;
            }
            entryUrl = QStringLiteral("https://%1/%2").arg(hostName, manifest.entry);

            const QJsonObject geometry = message.value(QStringLiteral("geometry")).toObject();
            const QRect rect(
                geometry.value(QStringLiteral("x")).toInt(message.value(QStringLiteral("x")).toInt()),
                geometry.value(QStringLiteral("y")).toInt(message.value(QStringLiteral("y")).toInt()),
                geometry.value(QStringLiteral("width")).toInt(manifest.width),
                geometry.value(QStringLiteral("height")).toInt(manifest.height));
            m_sharedWidgetGeometries.insert(instanceId, rect);
            m_sharedWidgetHosts.insert(instanceId, hostName);
            syncSharedRegion(QStringLiteral("add widget"));
            appendLog(QStringLiteral("hit"), QStringLiteral("added shared rect id=%1 rect=%2,%3 %4x%5 rectCount=%6")
                .arg(instanceId,
                     QString::number(rect.x()),
                     QString::number(rect.y()),
                     QString::number(rect.width()),
                     QString::number(rect.height()),
                     QString::number(m_sharedWidgetGeometries.size())));
            m_sharedWidgetPermissions.insert(instanceId, permissions);
            queueOrSendHostPageMessage(widgetInstancePayload(instanceId,
                                                             packagePath,
                                                             manifest,
                                                             rect,
                                                             permissions,
                                                             isDevelopment,
                                                             message.value(QStringLiteral("autoReload")).toBool(false),
                                                             entryUrl));
            if (isDevelopment) {
                m_pendingOpenDevTools = true;
                openDevToolsWhenReady();
            }
            return;
        }
        if (type == QString::fromLatin1(IpcType::RemoveWidget)) {
            const QString instanceId = message.value(QStringLiteral("instanceId")).toString(message.value(QStringLiteral("widgetId")).toString()).trimmed();
            m_sharedWidgetGeometries.remove(instanceId);
            m_sharedWidgetHosts.remove(instanceId);
            m_sharedWidgetPermissions.remove(instanceId);
            syncSharedRegion(QStringLiteral("remove widget"));
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
                syncSharedRegion(QStringLiteral("update widget geometry"));
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
        if (type == QString::fromLatin1(IpcType::SystemInfoResponse) ||
            type == QString::fromLatin1(IpcType::SystemInfoUpdate)) {
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
                        [this, entryPath, environment](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT {
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
                            openDevToolsWhenReady();

                            ComPtr<ICoreWebView2Controller2> controller2;
                            if (SUCCEEDED(controller->QueryInterface(IID_PPV_ARGS(&controller2)))) {
                                COREWEBVIEW2_COLOR transparent = { 0, 0, 0, 0 };
                                controller2->put_DefaultBackgroundColor(transparent);
                            }

                            ComPtr<ICoreWebView2Settings> settings;
                            if (webView && SUCCEEDED(webView->get_Settings(&settings))) {
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_AreDevToolsEnabled(TRUE);
                                settings->put_IsZoomControlEnabled(FALSE);
                            }
                            controller->put_ZoomFactor(1.0);

                            if (webView) {
                                appendLog(QStringLiteral("info"), QStringLiteral("WebView2 initialized"));
                                installWebConsoleLogging();
                                installWebZoomGuards();
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
                        [this, entryPath, environment](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT {
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
                                settings->put_IsZoomControlEnabled(FALSE);
                            }
                            controller->put_ZoomFactor(1.0);

                            if (webView) {
                                installWebConsoleLogging();
                                installWebZoomGuards();
                                installSharedWidgetApiBridge();
                                installSharedWebResourceGuards(environment);
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
                                EventRegistrationToken acceleratorToken = {};
                                controller->add_AcceleratorKeyPressed(
                                    Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
                                        [this](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
                                            COREWEBVIEW2_KEY_EVENT_KIND kind = COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN;
                                            UINT key = 0;
                                            INT lparam = 0;
                                            BOOL handled = FALSE;
                                            if (args) {
                                                args->get_KeyEventKind(&kind);
                                                args->get_VirtualKey(&key);
                                                args->get_KeyEventLParam(&lparam);
                                                args->get_Handled(&handled);
                                            }
                                            appendLog(QStringLiteral("key"),
                                                      QStringLiteral("shared AcceleratorKeyPressed kind=%1 vk=%2 lparam=%3 handled=%4")
                                                          .arg(QString::number(static_cast<int>(kind)),
                                                               QString::number(key),
                                                               QString::number(lparam),
                                                               handled ? QStringLiteral("true") : QStringLiteral("false")));
                                            return S_OK;
                                        }).Get(),
                                    &acceleratorToken);
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

void WidgetHostRuntime::openDevToolsWhenReady()
{
#ifdef LINDESK_HAS_WEBVIEW2
    if (!m_pendingOpenDevTools) {
        return;
    }
    auto* webView = static_cast<ICoreWebView2*>(m_webView);
    if (!webView) {
        return;
    }
    if (SUCCEEDED(webView->OpenDevToolsWindow())) {
        m_pendingOpenDevTools = false;
    }
#endif
}

QString WidgetHostRuntime::sharedRuntimeEntryPath() const
{
    const QString relativePath = QStringLiteral("resources/LinWidgetRuntime/host.html");
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString entryPath = QDir::cleanPath(QDir(appDir).filePath(relativePath));
    if (QFileInfo::exists(entryPath)) {
        appendLog(QStringLiteral("info"), QStringLiteral("shared runtime page=%1").arg(entryPath));
        return entryPath;
    }

    appendLog(QStringLiteral("error"),
              QStringLiteral("shared runtime page missing. path=%1").arg(entryPath));
    return entryPath;
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
        "const host = String(location.hostname || '').toLowerCase();"
        "const labels = host.split('.');"
        "const marker = labels.length >= 3 ? labels[labels.length - 3] : '';"
        "const isWidget = host.endsWith('.linwidget.local') && marker.startsWith('p');"
        "if (isWidget) {"
        " const installSelectionStyle = () => {"
        "  try {"
        "   if (document.getElementById('linwidget-selection-style')) return;"
        "   const style = document.createElement('style');"
        "   style.id = 'linwidget-selection-style';"
        "   style.textContent = 'html,body,#app,.lin-widget-root,body *{user-select:none!important;-webkit-user-select:none!important;}input,textarea,select,[contenteditable=\"true\"],[contenteditable=\"true\"] *,.selectable-text,.selectable-text *,.linwidget-selectable-text,.linwidget-selectable-text *{user-select:text!important;-webkit-user-select:text!important;}';"
        "   const target = document.head || document.documentElement;"
        "   if (target) target.appendChild(style);"
        "  } catch (_) {}"
        " };"
        " installSelectionStyle();"
        " if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', installSelectionStyle, {once:true});"
        "}"
        "const hasNetwork = marker.includes('n');"
        "const hasStorage = marker.includes('s');"
        "const postDenied = (permission, url) => {"
        " try {"
        "  const payload = {type:'permission_denied', permission:permission};"
        "  if (url) payload.url = String(url);"
        "  if (window.parent && window.parent !== window) window.parent.postMessage(payload, '*');"
        "  else if (window.chrome && chrome.webview) chrome.webview.postMessage(payload);"
        " } catch (_) {}"
        "};"
        "if (isWidget && !hasNetwork) {"
        " const resourceUrl = value => { try { return new URL(typeof value === 'string' ? value : (value && value.url) || '', location.href).href; } catch (_) { return ''; } };"
        " const denyNetwork = value => { postDenied('network', resourceUrl(value)); throw new Error('LinWidget network permission denied'); };"
        " const rejectNetwork = value => { postDenied('network', resourceUrl(value)); return Promise.reject(new Error('LinWidget network permission denied')); };"
        " const isOwnResource = value => {"
        "  try { const url = new URL(typeof value === 'string' ? value : (value && value.url) || '', location.href); return url.origin === location.origin && url.hostname === location.hostname; } catch (_) { return false; }"
        " };"
        " try {"
        "  const originalFetch = window.fetch;"
        "  window.fetch = (resource, init) => isOwnResource(resource) ? originalFetch.call(window, resource, init) : rejectNetwork(resource);"
        " } catch (_) {}"
        " try {"
        "  const OriginalXMLHttpRequest = window.XMLHttpRequest;"
        "  window.XMLHttpRequest = function(){"
        "   const xhr = new OriginalXMLHttpRequest();"
        "   const originalOpen = xhr.open;"
        "   xhr.open = function(method, url){ if (!isOwnResource(url)) denyNetwork(url); return originalOpen.apply(xhr, arguments); };"
        "   return xhr;"
        "  };"
        " } catch (_) {}"
        " try { window.WebSocket = function(url){ denyNetwork(url); }; } catch (_) {}"
        " try { window.EventSource = function(url){ denyNetwork(url); }; } catch (_) {}"
        " try {"
        "  const originalSendBeacon = navigator.sendBeacon;"
        "  navigator.sendBeacon = (url, data) => isOwnResource(url) ? originalSendBeacon.call(navigator, url, data) : (postDenied('network', resourceUrl(url)), false);"
        " } catch (_) {}"
        "}"
        "if (isWidget && !hasStorage) {"
        " const denyStorage = () => { postDenied('storage'); throw new Error('LinWidget storage permission denied'); };"
        " try { Object.defineProperty(window, 'localStorage', { configurable:false, get: denyStorage }); } catch (_) {}"
        " try { Object.defineProperty(window, 'sessionStorage', { configurable:false, get: denyStorage }); } catch (_) {}"
        " try { Object.defineProperty(window, 'indexedDB', { configurable:false, get: denyStorage }); } catch (_) {}"
        "}"
        "const postToHost = (type, value) => {"
        " const payload = Object.assign({type:type}, value && typeof value === 'object' ? value : {});"
        " if (window.parent && window.parent !== window) {"
        "  window.parent.postMessage(payload, '*');"
        "  return;"
        " }"
        " try { chrome.webview.postMessage(payload); } catch (_) {}"
        "};"
        "const describeLinWidgetTarget = target => target ? ((target.tagName||'').toLowerCase() + (target.id ? '#'+target.id : '') + (target.className ? '.'+String(target.className).replace(/\\s+/g,'.') : '')) : '';"
        "let lastLinWidgetFocusRequest=0;"
        "const requestLinWidgetFocus = (focusSource, force) => {"
        " const now = Date.now();"
        " if (!force && now - lastLinWidgetFocusRequest < 150) return;"
        " lastLinWidgetFocusRequest = now;"
        " postToHost('linwidget_focus_request', {focusSource:focusSource||''});"
        "};"
        "window.addEventListener('pointerdown', () => requestLinWidgetFocus('iframe_pointerdown', false), true);"
        "window.addEventListener('focusin', () => setTimeout(() => requestLinWidgetFocus('iframe_focusin', true), 0), true);"
        "window.addEventListener('keydown', event => {"
        " postToHost('linwidget_keydown', {key:event.key||'', code:event.code||'', target:describeLinWidgetTarget(event.target), defaultPrevented:!!event.defaultPrevented});"
        "}, true);"
        "let systemInfoSeq = 0;"
        "const systemInfoPending = new Map();"
        "const systemInfoWatchers = new Map();"
        "const sendSystemInfo = (action, value) => new Promise((resolve, reject) => {"
        " const requestId = 'system-info-' + Date.now() + '-' + (++systemInfoSeq);"
        " systemInfoPending.set(requestId, {resolve:resolve, reject:reject});"
        " postToHost('linwidget_system_info_request', Object.assign({action:action, requestId:requestId}, value && typeof value === 'object' ? value : {}));"
        "});"
        "const installSystemInfoApi = target => {"
        " const systemInfo = target.systemInfo && typeof target.systemInfo === 'object' ? target.systemInfo : {};"
        " systemInfo.getSnapshot = () => sendSystemInfo('snapshot');"
        " systemInfo.cpu = () => sendSystemInfo('cpu');"
        " systemInfo.memory = () => sendSystemInfo('memory');"
        " systemInfo.gpu = () => sendSystemInfo('gpu');"
        " systemInfo.disk = () => sendSystemInfo('disk');"
        " systemInfo.network = () => sendSystemInfo('network');"
        " systemInfo.battery = () => sendSystemInfo('battery');"
        " systemInfo.os = () => sendSystemInfo('os');"
        " systemInfo.uptime = () => sendSystemInfo('uptime');"
        " systemInfo.watch = (callback, intervalMs) => {"
        "  const watchId = 'watch-' + Date.now() + '-' + (++systemInfoSeq);"
        "  if (typeof callback === 'function') systemInfoWatchers.set(watchId, callback);"
        "  sendSystemInfo('watch', {watchId:watchId, intervalMs:Number(intervalMs) || 1000}).then(data => {"
        "   const cb = systemInfoWatchers.get(watchId);"
        "   if (cb) { try { cb(data); } catch (error) { setTimeout(() => { throw error; }, 0); } }"
        "  }).catch(error => { try { console.error(error); } catch (_) {} });"
        "  return watchId;"
        " };"
        " systemInfo.unwatch = watchId => {"
        "  const id = String(watchId || '');"
        "  systemInfoWatchers.delete(id);"
        "  return sendSystemInfo('unwatch', {watchId:id});"
        " };"
        " target.systemInfo = systemInfo;"
        "};"
        "const api = window.desktop && typeof window.desktop === 'object' ? window.desktop : {};"
        "api.context = 'widget';"
        "api.widgetId = api.widgetId || '';"
        "api.instanceId = api.instanceId || '';"
        "api.widgetName = api.widgetName || '';"
        "api.resizeWidget = value => postToHost('linwidget_resize_widget', value);"
        "api.setWidgetGeometry = value => postToHost('linwidget_set_widget_geometry', value);"
        "api.setResizeUiVisible = value => postToHost('linwidget_set_resize_ui_visible', {visible: !!value});"
        "api.openSettings = () => postToHost('linwidget_open_settings');"
        "installSystemInfoApi(api);"
        "window.desktop = api;"
        "window.addEventListener('message', event => {"
        " const message = event.data || {};"
        " if (!message || !message.type) return;"
        " if (message.type === 'linwidget_system_info_response') {"
        "  const pending = systemInfoPending.get(message.requestId || '');"
        "  if (!pending) return;"
        "  systemInfoPending.delete(message.requestId || '');"
        "  if (message.ok === false) pending.reject(new Error(message.error || 'System information request failed'));"
        "  else pending.resolve(message.data || {});"
        "  return;"
        " }"
        " if (message.type === 'linwidget_system_info_update') {"
        "  const cb = systemInfoWatchers.get(message.watchId || '');"
        "  if (cb) { try { cb(message.data || {}); } catch (error) { setTimeout(() => { throw error; }, 0); } }"
        "  return;"
        " }"
        " if (message.type !== 'linwidget_context') return;"
        " const nextApi = window.desktop && typeof window.desktop === 'object' ? window.desktop : api;"
        " nextApi.context = message.context || 'widget';"
        " nextApi.widgetId = message.widgetId || '';"
        " nextApi.instanceId = message.instanceId || '';"
        " nextApi.widgetName = message.widgetName || '';"
        " nextApi.openSettings = nextApi.openSettings || (() => postToHost('linwidget_open_settings'));"
        " installSystemInfoApi(nextApi);"
        " window.desktop = nextApi;"
        "});"
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
    payload.insert(QStringLiteral("manifestId"), manifest.id);
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

QString WidgetHostRuntime::virtualHostNameForInstance(const QString& instanceId,
                                                      const QStringList& permissions,
                                                      bool isDevelopment) const
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
    return safe + QLatin1Char('.') + permissionHostSegment(permissions, isDevelopment) + QStringLiteral(".linwidget.local");
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

void WidgetHostRuntime::installSharedWebResourceGuards(void* environment)
{
#ifdef LINDESK_HAS_WEBVIEW2
    auto* webView = static_cast<ICoreWebView2*>(m_webView);
    auto* webEnvironment = static_cast<ICoreWebView2Environment*>(environment);
    if (!webView || !webEnvironment) {
        return;
    }

    webView->AddWebResourceRequestedFilter(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
    EventRegistrationToken token = {};
    webView->add_WebResourceRequested(
        Callback<ICoreWebView2WebResourceRequestedEventHandler>(
            [this, webEnvironment](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
                if (!args) {
                    return S_OK;
                }
                ComPtr<ICoreWebView2WebResourceRequest> request;
                if (FAILED(args->get_Request(&request)) || !request) {
                    return S_OK;
                }
                LPWSTR rawUri = nullptr;
                if (FAILED(request->get_Uri(&rawUri)) || !rawUri) {
                    return S_OK;
                }
                const QString uriText = QString::fromWCharArray(rawUri);
                CoTaskMemFree(rawUri);

                const QUrl url(uriText);
                const QString scheme = url.scheme().toLower();
                if (scheme == QStringLiteral("file")) {
                    const QString runtimePath = QDir::cleanPath(QFileInfo(sharedRuntimeEntryPath()).absoluteFilePath());
                    const QString runtimeDir = QDir::cleanPath(QFileInfo(runtimePath).absolutePath()).toLower();
                    const QString requestedPath = QDir::cleanPath(url.toLocalFile()).toLower();
                    if (requestedPath == runtimePath.toLower() ||
                        requestedPath.startsWith(runtimeDir + QLatin1Char('/')) ||
                        requestedPath.startsWith(runtimeDir + QLatin1Char('\\'))) {
                        return S_OK;
                    }
                }

                QString refererHost;
                QString originHost;
                ComPtr<ICoreWebView2HttpRequestHeaders> headers;
                if (SUCCEEDED(request->get_Headers(&headers)) && headers) {
                    refererHost = QUrl(webResourceHeaderValue(headers.Get(), L"Referer")).host().toLower();
                    originHost = QUrl(webResourceHeaderValue(headers.Get(), L"Origin")).host().toLower();
                }
                const QString host = url.host().toLower();
                const QString requestInstanceId = m_sharedWidgetHosts.key(host);
                const QString refererInstanceId = refererHost.isEmpty() ? QString() : m_sharedWidgetHosts.key(refererHost);
                const QString originInstanceId = originHost.isEmpty() ? QString() : m_sharedWidgetHosts.key(originHost);
                QString instanceId = !refererInstanceId.isEmpty() ? refererInstanceId : (!originInstanceId.isEmpty() ? originInstanceId : requestInstanceId);
                if (!instanceId.isEmpty()) {
                    if (scheme == QStringLiteral("https") && host.endsWith(QStringLiteral(".linwidget.local"))) {
                        const QString ownHost = m_sharedWidgetHosts.value(instanceId);
                        if (host == ownHost) {
                            return S_OK;
                        }
                        appendLog(QStringLiteral("permission"), QStringLiteral("%1 permission denied: network url=%2").arg(instanceId, uriText));
                        appendInstanceLog(instanceId, QStringLiteral("permission"), QStringLiteral("%1 permission denied: network url=%2").arg(instanceId, uriText));
                    } else if ((scheme == QStringLiteral("http") || scheme == QStringLiteral("https")) &&
                               hasNetworkPermission(m_sharedWidgetPermissions.value(instanceId))) {
                        return S_OK;
                    } else {
                        appendLog(QStringLiteral("permission"), QStringLiteral("%1 permission denied: network url=%2").arg(instanceId, uriText));
                        appendInstanceLog(instanceId, QStringLiteral("permission"), QStringLiteral("%1 permission denied: network url=%2").arg(instanceId, uriText));
                    }
                } else if (scheme == QStringLiteral("about") || scheme == QStringLiteral("data") || scheme == QStringLiteral("blob")) {
                    return S_OK;
                } else {
                    appendLog(QStringLiteral("permission"), QStringLiteral("permission denied: unknown resource url=%1").arg(uriText));
                }

                ComPtr<ICoreWebView2WebResourceResponse> response;
                const std::wstring reason = L"LinWidget Permission Denied";
                const std::wstring responseHeaders = L"Content-Type: text/plain\r\nAccess-Control-Allow-Origin: *";
                if (SUCCEEDED(webEnvironment->CreateWebResourceResponse(nullptr, 403, reason.c_str(), responseHeaders.c_str(), &response)) && response) {
                    args->put_Response(response.Get());
                }
                return S_OK;
            }).Get(),
        &token);
#else
    Q_UNUSED(environment);
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
    syncSharedRegion(QStringLiteral("legacy update"));
}

void WidgetHostRuntime::syncSharedRegion(const QString& reason)
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
        const quint64 webViewWindowId = static_cast<quint64>(reinterpret_cast<quintptr>(webViewSearch.window));
        if (webViewWindowId != m_lastReportedWebViewWindow) {
            m_lastReportedWebViewWindow = webViewWindowId;
            installFocusEventDiagnostics(webViewSearch.window, logFilePath());
            sendIpcMessage(QString::fromLatin1(IpcType::WebViewWindowReady), {
                { QStringLiteral("hwnd"), QString::number(webViewWindowId) },
                { QStringLiteral("className"), webViewSearch.className },
                { QStringLiteral("reason"), reason }
            });
        }
    } else if (m_lastReportedWebViewWindow != 0) {
        m_lastReportedWebViewWindow = 0;
        uninstallFocusEventDiagnostics();
        sendIpcMessage(QString::fromLatin1(IpcType::WebViewWindowLost), {
            { QStringLiteral("reason"), reason }
        });
    }
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
            "window.EventSource=function(){denyNetwork();};"
            "try{navigator.sendBeacon=()=>false;}catch(_){ }");
    }
    if (!permissions.canUseStorage(m_grantedPermissions)) {
        script += QStringLiteral(
            "const denyStorage=()=>{throw new Error('LinWidget storage permission denied');};"
            "try{Object.defineProperty(window,'localStorage',{configurable:false,get:denyStorage});}catch(_){ }"
            "try{Object.defineProperty(window,'sessionStorage',{configurable:false,get:denyStorage});}catch(_){ }"
            "try{Object.defineProperty(window,'indexedDB',{configurable:false,get:denyStorage});}catch(_){ }");
    }
    script += QStringLiteral("})();");

    webView->AddScriptToExecuteOnDocumentCreated(reinterpret_cast<LPCWSTR>(script.utf16()), nullptr);
#endif
}

void WidgetHostRuntime::installWebZoomGuards()
{
#ifdef LINDESK_HAS_WEBVIEW2
    auto* webView = static_cast<ICoreWebView2*>(m_webView);
    if (!webView) {
        return;
    }

    const QString script = QStringLiteral(
        "(() => {"
        "try {"
        "window.addEventListener('wheel', event => {"
        " if (event.ctrlKey) { event.preventDefault(); event.stopPropagation(); }"
        "}, { passive:false, capture:true });"
        "window.addEventListener('keydown', event => {"
        " if (event.ctrlKey && (event.key === '+' || event.key === '-' || event.key === '=' || event.key === '0')) {"
        "  event.preventDefault(); event.stopPropagation();"
        " }"
        "}, { capture:true });"
        "} catch (_) {}"
        "})();");

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
        "  post({type:'focus_request'});"
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
            syncSharedRegion(QStringLiteral("live widget geometry"));
        }
        if (!object.value(QStringLiteral("live")).toBool(false)) {
            sendIpcMessage(QString::fromLatin1(IpcType::WidgetGeometryChanged), object);
        }
        return;
    }
    if (m_sharedRuntime && type == QStringLiteral("linwidget_open_settings")) {
        const QString instanceId = object.value(QStringLiteral("instanceId")).toString(object.value(QStringLiteral("widgetId")).toString()).trimmed();
        if (instanceId.isEmpty()) {
            return;
        }
        sendIpcMessage(QString::fromLatin1(IpcType::OpenWidgetSettings), {
            { QStringLiteral("instanceId"), instanceId }
        });
        return;
    }
    if (m_sharedRuntime && type == QStringLiteral("linwidget_system_info_request")) {
        const QString instanceId = object.value(QStringLiteral("instanceId")).toString(object.value(QStringLiteral("widgetId")).toString()).trimmed();
        if (instanceId.isEmpty()) {
            return;
        }
        QJsonObject message = object;
        message.insert(QStringLiteral("instanceId"), instanceId);
        message.insert(QStringLiteral("widgetId"), instanceId);
        message.remove(QStringLiteral("type"));
        sendIpcMessage(QString::fromLatin1(IpcType::SystemInfoRequest), message);
        return;
    }
    if (type == QStringLiteral("console_log")) {
        const QString instanceId = object.value(QStringLiteral("instanceId")).toString(object.value(QStringLiteral("widgetId")).toString()).trimmed();
        const QString level = QStringLiteral("console.") + object.value(QStringLiteral("level")).toString(QStringLiteral("log"));
        const QString message = object.value(QStringLiteral("message")).toString();
        appendLog(level, message);
        if (!instanceId.isEmpty()) {
            appendInstanceLog(instanceId, level, message);
        }
        return;
    }
    if (type == QStringLiteral("permission_denied")) {
        const QString instanceId = object.value(QStringLiteral("instanceId")).toString(object.value(QStringLiteral("widgetId")).toString()).trimmed();
        const QString permission = object.value(QStringLiteral("permission")).toString();
        const QString url = object.value(QStringLiteral("url")).toString();
        const QString message = url.isEmpty()
            ? QStringLiteral("%1 permission denied: %2")
                .arg(instanceId.isEmpty() ? QStringLiteral("unknown") : instanceId,
                     permission.isEmpty() ? QStringLiteral("unknown") : permission)
            : QStringLiteral("%1 permission denied: %2 url=%3")
                .arg(instanceId.isEmpty() ? QStringLiteral("unknown") : instanceId,
                     permission.isEmpty() ? QStringLiteral("unknown") : permission,
                     url);
        appendLog(QStringLiteral("permission"), message);
        if (!instanceId.isEmpty()) {
            appendInstanceLog(instanceId, QStringLiteral("permission"), message);
        }
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
        focusWebView(object.value(QStringLiteral("focusSource")).toString());
        return;
    }
    if (type == QStringLiteral("dom_focusin")) {
        appendLog(QStringLiteral("dom"), QStringLiteral("focusin target=%1")
            .arg(object.value(QStringLiteral("target")).toString()));
        if (m_sharedRuntime) {
            focusWebView();
        }
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
    if (type == QStringLiteral("host_keydown")) {
        const QString instanceId = object.value(QStringLiteral("instanceId")).toString(object.value(QStringLiteral("widgetId")).toString()).trimmed();
        const QString message = QStringLiteral("keydown instance=%1 key=%2 code=%3 target=%4 prevented=%5")
            .arg(instanceId.isEmpty() ? QStringLiteral("<host>") : instanceId,
                 object.value(QStringLiteral("key")).toString(),
                 object.value(QStringLiteral("code")).toString(),
                 object.value(QStringLiteral("target")).toString(),
                 object.value(QStringLiteral("defaultPrevented")).toBool(false) ? QStringLiteral("true") : QStringLiteral("false"));
        appendLog(QStringLiteral("host.key"), message);
        if (!instanceId.isEmpty()) {
            appendInstanceLog(instanceId, QStringLiteral("host.key"), message);
        }
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
    return logFilePathForId(m_widgetId);
}

void WidgetHostRuntime::appendInstanceLog(const QString& instanceId, const QString& level, const QString& message) const
{
    const QString line = QStringLiteral("[%1] [%2] %3\n")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
             level,
             message);
    QFile file(logFilePathForId(instanceId));
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        file.write(line.toUtf8());
    }
}

QString WidgetHostRuntime::logFilePathForId(const QString& id) const
{
    QString safeId = id.trimmed();
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

void WidgetHostRuntime::focusWebView(const QString& source)
{
#ifdef Q_OS_WIN
    HWND hostWindow = reinterpret_cast<HWND>(winId());
    HWND rootWindow = rootOwnerWindow(hostWindow);
    if (rootWindow && !m_embedded) {
        SetForegroundWindow(rootWindow);
    }
    if (m_embedded && hostWindow) {
        focusNativeWindow(hostWindow);
    }
    WebViewWindowSearch webViewSearch;
    if (hostWindow) {
        EnumChildWindows(hostWindow, findWebViewChildWindow, reinterpret_cast<LPARAM>(&webViewSearch));
    }
    if (webViewSearch.window && GetFocus() == webViewSearch.window) {
        appendLog(QStringLiteral("focus"), QStringLiteral("source=%1 skipped because WebView already has focus webview=%2 foreground=%3")
            .arg(source.isEmpty() ? QStringLiteral("<native>") : source,
                 QString::number(reinterpret_cast<quintptr>(webViewSearch.window), 16),
                 QString::number(reinterpret_cast<quintptr>(GetForegroundWindow()), 16)));
        return;
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
        if (m_embedded || m_parentWindowId != 0) {
            focusNativeWindow(webViewSearch.window);
        } else {
            activateNativeWindow(webViewSearch.window);
        }
    }
    const HWND focusedWindow = GetFocus();
    appendLog(QStringLiteral("focus"), QStringLiteral("source=%1 root=%2 host=%3 webview=%4 webviewClass=%5 focused=%6 focusedClass=%7 foreground=%8")
        .arg(source.isEmpty() ? QStringLiteral("<native>") : source,
             QString::number(reinterpret_cast<quintptr>(rootWindow), 16),
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
    Q_UNUSED(result);
    if (m_sharedRuntime) {
        syncSharedRegion(QStringLiteral("webview bounds"));
    }
#endif
}

}
