#include "desktop/widgets/linwidget/WidgetSettingsDialog.h"

#include "desktop/widgets/linwidget/PermissionManager.h"
#include "services/AppPaths.h"

#include <QDateTime>
#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QResizeEvent>
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
QString jsonStringLiteral(const QString& value)
{
    QString json = QString::fromUtf8(QJsonDocument(QJsonArray{ value }).toJson(QJsonDocument::Compact));
    if (json.size() >= 2) {
        json = json.mid(1, json.size() - 2);
    }
    return json;
}

QString permissionHostSegment(const QStringList& permissions, bool isDevelopment)
{
    if (isDevelopment) {
        return QStringLiteral("pdevnsd");
    }

    QString marker = QStringLiteral("p");
    PermissionManager manager;
    if (manager.canUseNetwork(permissions)) {
        marker.append(QLatin1Char('n'));
    }
    if (manager.canUseStorage(permissions)) {
        marker.append(QLatin1Char('s'));
    }
    if (manager.canUseDesktopApi(permissions)) {
        marker.append(QLatin1Char('d'));
    }
    return marker;
}
}

WidgetSettingsDialog::WidgetSettingsDialog(QString packagePath,
                                           WidgetManifest manifest,
                                           QStringList grantedPermissions,
                                           bool isDevelopment,
                                           QString instanceId,
                                           QWidget* parent)
    : QDialog(parent)
    , m_packagePath(std::move(packagePath))
    , m_manifest(std::move(manifest))
    , m_grantedPermissions(PermissionManager().normalizePermissions(grantedPermissions))
    , m_isDevelopment(isDevelopment)
    , m_instanceId(std::move(instanceId))
{
    setWindowTitle(tr("%1 Settings").arg(m_manifest.name.isEmpty() ? m_manifest.id : m_manifest.name));
    resize(860, 640);
    setMinimumSize(520, 360);
}

WidgetSettingsDialog::~WidgetSettingsDialog()
{
#ifdef LINDESK_HAS_WEBVIEW2
    if (m_webViewController) {
        static_cast<ICoreWebView2Controller*>(m_webViewController)->Close();
        static_cast<ICoreWebView2Controller*>(m_webViewController)->Release();
        m_webViewController = nullptr;
    }
    if (m_webView) {
        static_cast<ICoreWebView2*>(m_webView)->Release();
        m_webView = nullptr;
    }
#endif
#ifdef Q_OS_WIN
    if (m_comInitialized) {
        CoUninitialize();
    }
#endif
}

bool WidgetSettingsDialog::initialize(QString* errorMessage)
{
    if (m_manifest.settings.trimmed().isEmpty()) {
        const QString message = tr("This widget does not provide a settings page.");
        if (errorMessage) {
            *errorMessage = message;
        }
        renderPlaceholder(message);
        return false;
    }

    const QString settingsPath = QDir(m_packagePath).filePath(m_manifest.settings);
    if (!QFileInfo::exists(settingsPath)) {
        const QString message = tr("Settings page is missing: %1").arg(settingsPath);
        if (errorMessage) {
            *errorMessage = message;
        }
        renderPlaceholder(message);
        return false;
    }

#ifdef Q_OS_WIN
    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    m_comInitialized = SUCCEEDED(initHr);
#endif
    renderWebView(settingsPath);
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool WidgetSettingsDialog::event(QEvent* event)
{
    if (event->type() == QEvent::Resize) {
        updateWebViewBounds();
    }
    return QDialog::event(event);
}

void WidgetSettingsDialog::renderPlaceholder(const QString& message)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    auto* label = new QLabel(message, this);
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(QStringLiteral(
        "QLabel {"
        "background: #ffffff;"
        "border: 1px solid rgba(15, 23, 42, 0.12);"
        "border-radius: 10px;"
        "color: #374151;"
        "font-size: 14px;"
        "padding: 20px;"
        "}"));
    layout->addWidget(label, 1);
}

void WidgetSettingsDialog::renderWebView(const QString& settingsPath)
{
#ifndef LINDESK_HAS_WEBVIEW2
    Q_UNUSED(settingsPath);
    renderPlaceholder(tr("WebView2 SDK is not enabled in this build."));
#else
    setAttribute(Qt::WA_NativeWindow, true);
    const HWND parentWindow = reinterpret_cast<HWND>(winId());
    if (!parentWindow) {
        renderPlaceholder(tr("Cannot create native settings window."));
        return;
    }

    const QString userDataDir = QDir(AppPaths::userLocalConfigDir()).filePath(QStringLiteral("LinWidgets/shared-webview2"));
    const std::wstring userData = QDir::toNativeSeparators(userDataDir).toStdWString();
    const HRESULT result = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,
        userData.c_str(),
        nullptr,
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, parentWindow, settingsPath](HRESULT envResult, ICoreWebView2Environment* environment) -> HRESULT {
                if (FAILED(envResult) || !environment) {
                    renderPlaceholder(tr("Failed to initialize WebView2 environment."));
                    return S_OK;
                }

                environment->CreateCoreWebView2Controller(
                    parentWindow,
                    Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this, settingsPath](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(controllerResult) || !controller) {
                                renderPlaceholder(tr("Failed to create WebView2 controller."));
                                return S_OK;
                            }

                            controller->AddRef();
                            m_webViewController = controller;
                            ICoreWebView2* webView = nullptr;
                            controller->get_CoreWebView2(&webView);
                            m_webView = webView;
                            updateWebViewBounds();

                            Microsoft::WRL::ComPtr<ICoreWebView2Controller2> controller2;
                            if (SUCCEEDED(controller->QueryInterface(IID_PPV_ARGS(&controller2)))) {
                                COREWEBVIEW2_COLOR white = { 255, 255, 255, 255 };
                                controller2->put_DefaultBackgroundColor(white);
                            }

                            Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
                            if (webView && SUCCEEDED(webView->get_Settings(&settings))) {
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_AreDevToolsEnabled(TRUE);
                            }

                            if (webView) {
                                const QString hostName = virtualHostName();
                                if (!mapVirtualHost(hostName)) {
                                    renderPlaceholder(tr("Failed to map widget settings host."));
                                    return S_OK;
                                }
                                installConsoleLogging();
                                installSettingsBridge();
                                installPermissionGuards();
                                const QString settingsUrl = QStringLiteral("https://%1/%2").arg(hostName, m_manifest.settings);
                                const QString hostPath = settingsHostEntryPath();
                                if (!QFileInfo::exists(hostPath)) {
                                    renderPlaceholder(tr("Settings host page is missing: %1").arg(hostPath));
                                    return S_OK;
                                }
                                QUrl hostUrl = QUrl::fromLocalFile(hostPath);
                                hostUrl.setQuery(QStringLiteral("src=%1").arg(QString::fromLatin1(QUrl::toPercentEncoding(settingsUrl))));
                                const QString url = hostUrl.toString();
                                appendLog(QStringLiteral("info"), QStringLiteral("navigate settings host=%1 frame=%2 source=%3").arg(url, settingsUrl, settingsPath));
                                webView->Navigate(reinterpret_cast<LPCWSTR>(url.utf16()));
                                if (m_isDevelopment) {
                                    webView->OpenDevToolsWindow();
                                }
                            }
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());

    if (FAILED(result)) {
        renderPlaceholder(tr("Failed to start WebView2 environment."));
    }
#endif
}

QString WidgetSettingsDialog::settingsHostEntryPath() const
{
    const QString relativePath = QStringLiteral("resources/LinWidgetRuntime/settings_host.html");
    const QString appDir = QCoreApplication::applicationDirPath();
    return QDir::cleanPath(QDir(appDir).filePath(relativePath));
}

QString WidgetSettingsDialog::virtualHostName() const
{
    QString safe = m_instanceId.trimmed().isEmpty()
        ? m_manifest.id.toLower()
        : m_instanceId.trimmed().toLower();
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
    return safe + QLatin1Char('.') + permissionHostSegment(m_grantedPermissions, m_isDevelopment) + QStringLiteral(".linwidget.local");
}

bool WidgetSettingsDialog::mapVirtualHost(const QString& hostName)
{
#ifdef LINDESK_HAS_WEBVIEW2
    auto* webView = static_cast<ICoreWebView2*>(m_webView);
    if (!webView) {
        return false;
    }
    Microsoft::WRL::ComPtr<ICoreWebView2_3> webView3;
    if (FAILED(webView->QueryInterface(IID_PPV_ARGS(&webView3)))) {
        return false;
    }
    const std::wstring host = hostName.toStdWString();
    const std::wstring folder = QDir::toNativeSeparators(m_packagePath).toStdWString();
    return SUCCEEDED(webView3->SetVirtualHostNameToFolderMapping(
        host.c_str(),
        folder.c_str(),
        COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS));
#else
    Q_UNUSED(hostName);
    return false;
#endif
}

void WidgetSettingsDialog::installConsoleLogging()
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
        "['log','info','warn','error'].forEach(level => {"
        " const original = console[level];"
        " console[level] = (...args) => {"
        "  try { chrome.webview.postMessage({type:'console_log', level:level, message:args.map(stringify).join(' ')}); } catch (_) {}"
        "  if (original) original.apply(console, args);"
        " };"
        "});"
        "} catch (_) {}"
        "})();");
    webView->AddScriptToExecuteOnDocumentCreated(reinterpret_cast<LPCWSTR>(script.utf16()), nullptr);

    EventRegistrationToken token = {};
    webView->add_WebMessageReceived(
        Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                LPWSTR rawMessage = nullptr;
                if (SUCCEEDED(args->get_WebMessageAsJson(&rawMessage)) && rawMessage) {
                    const QJsonDocument document = QJsonDocument::fromJson(QString::fromWCharArray(rawMessage).toUtf8());
                    const QJsonObject object = document.object();
                    appendLog(object.value(QStringLiteral("level")).toString(QStringLiteral("log")),
                              object.value(QStringLiteral("message")).toString());
                    CoTaskMemFree(rawMessage);
                }
                return S_OK;
            }).Get(),
        &token);
#endif
}

void WidgetSettingsDialog::installSettingsBridge()
{
#ifdef LINDESK_HAS_WEBVIEW2
    auto* webView = static_cast<ICoreWebView2*>(m_webView);
    if (!webView) {
        return;
    }

    const QString script = QStringLiteral(
        "(() => {"
        "try {"
        "const api = window.desktop && typeof window.desktop === 'object' ? window.desktop : {};"
        "api.context = 'settings';"
        "api.widgetId = %1;"
        "api.instanceId = %2;"
        "api.widgetName = %3;"
        "window.desktop = api;"
        "} catch (_) {}"
        "})();")
        .arg(jsonStringLiteral(m_manifest.id),
             jsonStringLiteral(m_instanceId),
             jsonStringLiteral(m_manifest.name));
    webView->AddScriptToExecuteOnDocumentCreated(reinterpret_cast<LPCWSTR>(script.utf16()), nullptr);
#endif
}

void WidgetSettingsDialog::installPermissionGuards()
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
            "window.fetch=(...args)=>Promise.reject(new Error('LinWidget network permission denied'));"
            "window.XMLHttpRequest=function(){throw new Error('LinWidget network permission denied');};"
            "window.WebSocket=function(){throw new Error('LinWidget network permission denied');};"
            "window.EventSource=function(){throw new Error('LinWidget network permission denied');};");
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

void WidgetSettingsDialog::appendLog(const QString& level, const QString& message) const
{
    qDebug().noquote() << QStringLiteral("[LinWidgetSettings][%1] %2").arg(level, message);
}

void WidgetSettingsDialog::updateWebViewBounds()
{
#ifdef LINDESK_HAS_WEBVIEW2
    auto* controller = static_cast<ICoreWebView2Controller*>(m_webViewController);
    if (!controller) {
        return;
    }
    RECT bounds = { 0, 0, width(), height() };
    HWND hostWindow = reinterpret_cast<HWND>(winId());
    RECT clientRect = {};
    if (hostWindow && GetClientRect(hostWindow, &clientRect)) {
        bounds = clientRect;
    }
    controller->put_Bounds(bounds);
#endif
}

}
