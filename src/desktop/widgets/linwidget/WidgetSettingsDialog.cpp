#include "desktop/widgets/linwidget/WidgetSettingsDialog.h"

#include "desktop/ListView.h"
#include "desktop/widgets/linwidget/PermissionManager.h"
#include "desktop/widgets/linwidget/WidgetManager.h"
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
#include <QPointer>
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

#ifdef LINDESK_HAS_WEBVIEW2
QString formatWebViewError(HRESULT result)
{
    return QStringLiteral("0x%1").arg(static_cast<qulonglong>(static_cast<quint32>(result)), 8, 16, QLatin1Char('0'));
}
#endif


void activateSettingsDialog(WidgetSettingsDialog* dialog)
{
    if (!dialog) {
        return;
    }
    if (dialog->isMinimized()) {
        dialog->setWindowState(dialog->windowState() & ~Qt::WindowMinimized);
    }
    if (!dialog->isVisible()) {
        dialog->show();
    }
    dialog->raise();
    dialog->activateWindow();
#ifdef Q_OS_WIN
    if (const HWND hwnd = reinterpret_cast<HWND>(dialog->winId())) {
        ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
        BringWindowToTop(hwnd);
    }
#endif
}
}

QMap<QString, QPointer<WidgetSettingsDialog>>& WidgetSettingsDialog::openSettingsDialogs()
{
    static QMap<QString, QPointer<WidgetSettingsDialog>> dialogs;
    return dialogs;
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

WidgetSettingsDialog* WidgetSettingsDialog::openOrCreate(QString packagePath,
                                                         WidgetManifest manifest,
                                                         QStringList grantedPermissions,
                                                         bool isDevelopment,
                                                         QString instanceId,
                                                         QWidget* parent,
                                                         QString* errorMessage)
{
    const QString key = dialogKey(packagePath, manifest, isDevelopment, instanceId);
    auto& dialogs = openSettingsDialogs();
    if (WidgetSettingsDialog* existing = dialogs.value(key).data()) {
        activateSettingsDialog(existing);
        if (errorMessage) {
            errorMessage->clear();
        }
        return existing;
    }

    auto* dialog = new WidgetSettingsDialog(std::move(packagePath),
                                            std::move(manifest),
                                            std::move(grantedPermissions),
                                            isDevelopment,
                                            std::move(instanceId),
                                            parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialogs.insert(key, dialog);
    QObject::connect(dialog, &QObject::destroyed, dialog, [key]() {
        openSettingsDialogs().remove(key);
    });
    dialog->initialize(errorMessage);
    activateSettingsDialog(dialog);
    return dialog;
}

QString WidgetSettingsDialog::dialogKey(const QString& packagePath,
                                        const WidgetManifest& manifest,
                                        bool isDevelopment,
                                        const QString& instanceId)
{
    const QString normalizedInstanceId = instanceId.trimmed().toLower();
    if (!normalizedInstanceId.isEmpty()) {
        return QStringLiteral("instance:%1").arg(normalizedInstanceId);
    }

    const QString normalizedId = manifest.id.trimmed().toLower();
    if (!normalizedId.isEmpty()) {
        return QStringLiteral("%1:%2").arg(isDevelopment ? QStringLiteral("dev") : QStringLiteral("installed"), normalizedId);
    }

    return QStringLiteral("package:%1").arg(QFileInfo(packagePath).absoluteFilePath().toLower());
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
                                const QString hostEntry = settingsHostEntryPath();
                                const QString settingsUrl = QStringLiteral("https://%1/%2").arg(hostName, m_manifest.settings);
                                const QString navigateUrl = QStringLiteral("%1?src=%2")
                                    .arg(QUrl::fromLocalFile(hostEntry).toString(),
                                         QString::fromUtf8(QUrl::toPercentEncoding(settingsUrl)));
                                appendLog(QStringLiteral("info"), QStringLiteral("navigate settings url=%1 source=%2").arg(navigateUrl, settingsPath));
                                webView->Navigate(reinterpret_cast<LPCWSTR>(navigateUrl.utf16()));
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
    QString prefix = m_isDevelopment ? QStringLiteral("dev") : QStringLiteral("installed");
    QString widgetId = m_manifest.id;
    QString safe = (prefix + QLatin1Char('-') + widgetId).toLower();
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
        appendLog(QStringLiteral("error"), QStringLiteral("Failed to map widget virtual host for %1: webView is null").arg(hostName));
        return false;
    }
    Microsoft::WRL::ComPtr<ICoreWebView2_3> webView3;
    const HRESULT queryResult = webView->QueryInterface(IID_PPV_ARGS(&webView3));
    if (FAILED(queryResult)) {
        appendLog(QStringLiteral("error"), QStringLiteral("Failed to map widget virtual host for %1: QueryInterface(ICoreWebView2_3) failed with %2")
            .arg(hostName, formatWebViewError(queryResult)));
        return false;
    }
    const QFileInfo folderInfo(QDir::cleanPath(m_packagePath));
    const QString folderPath = QDir::cleanPath(folderInfo.absoluteFilePath());
    if (!folderInfo.exists() || !folderInfo.isDir()) {
        appendLog(QStringLiteral("error"), QStringLiteral("Failed to map widget virtual host for %1: package path is not a directory: %2")
            .arg(hostName, folderPath));
        return false;
    }
    const QString existingFolderPath = m_virtualHostMappings.value(hostName);
    if (existingFolderPath == folderPath) {
        return true;
    }
    const std::wstring host = hostName.toStdWString();
    const std::wstring folder = QDir::toNativeSeparators(folderPath).toStdWString();
    if (!existingFolderPath.isEmpty()) {
        webView3->ClearVirtualHostNameToFolderMapping(host.c_str());
    }
    const HRESULT mapResult = webView3->SetVirtualHostNameToFolderMapping(
        host.c_str(),
        folder.c_str(),
        COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
    if (FAILED(mapResult)) {
        appendLog(QStringLiteral("error"), QStringLiteral("Failed to map widget virtual host for %1: SetVirtualHostNameToFolderMapping failed with %2 host=%3 folder=%4")
            .arg(hostName, formatWebViewError(mapResult), hostName, folderPath));
        return false;
    }
    m_virtualHostMappings.insert(hostName, folderPath);
    appendLog(QStringLiteral("info"), QStringLiteral("Mapped widget virtual host host=%1 folder=%2").arg(hostName, folderPath));
    return true;
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
                    handleSettingsWebMessage(object);
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
        "const post = message => {"
        " if (window.parent && window.parent !== window) {"
        "  window.parent.postMessage(message, '*');"
        "  return;"
        " }"
        " try { chrome.webview.postMessage(message); } catch (_) {}"
        "};"
        "api.context = 'settings';"
        "api.widgetId = %1;"
        "api.instanceId = %2;"
        "api.widgetName = %3;"
        "let instanceSeq = 0;"
        "const instancePending = new Map();"
        "const sendInstanceRequest = (type, value) => new Promise(resolve => {"
        " const requestId = 'settings-instance-' + Date.now() + '-' + (++instanceSeq);"
        " instancePending.set(requestId, resolve);"
        " post(Object.assign({type:type, requestId:requestId}, value && typeof value === 'object' ? value : {}));"
        "});"
        "api.instances = api.instances || {};"
        "api.instances.create = options => sendInstanceRequest('linwidget_create_instance_request', options && typeof options === 'object' ? {geometry:options} : {});"
        "api.instances.list = () => sendInstanceRequest('linwidget_list_instances_request');"
        "api.instances.close = instanceId => sendInstanceRequest('linwidget_close_instance_request', {targetInstanceId:String(instanceId || '')});"
        "api.instances.remove = instanceId => sendInstanceRequest('linwidget_remove_instance_request', {targetInstanceId:String(instanceId || '')});"
        "api.instances.load = instanceId => sendInstanceRequest('linwidget_load_instance_request', {targetInstanceId:String(instanceId || '')});"
        "const handleMessage = event => {"
        " const message = event.data || {};"
        " if (!message || !message.type || !message.type.startsWith('linwidget_') || !message.type.endsWith('_response')) return;"
        " const resolve = instancePending.get(message.requestId || '');"
        " if (!resolve) return;"
        " instancePending.delete(message.requestId || '');"
        " resolve({ok: message.ok !== false, instanceId: message.instanceId || '', instances: Array.isArray(message.instances) ? message.instances : [], error: message.error || ''});"
        "};"
        "try { window.chrome.webview.addEventListener('message', handleMessage); } catch (_) {}"
        "window.addEventListener('message', handleMessage);"
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

void WidgetSettingsDialog::handleSettingsWebMessage(const QJsonObject& object)
{
    const QString type = object.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("console_log")) {
        appendLog(object.value(QStringLiteral("level")).toString(QStringLiteral("log")),
                  object.value(QStringLiteral("message")).toString());
        return;
    }

    const bool isCreate = type == QStringLiteral("linwidget_create_instance_request");
    const bool isList = type == QStringLiteral("linwidget_list_instances_request");
    const bool isClose = type == QStringLiteral("linwidget_close_instance_request");
    const bool isRemove = type == QStringLiteral("linwidget_remove_instance_request");
    const bool isLoad = type == QStringLiteral("linwidget_load_instance_request");
    const bool isSettingsChanged = type == QStringLiteral("linwidget_settings_changed_request");
    if (isSettingsChanged) {
        ListView* view = ListView::instance();
        WidgetManager* manager = view ? view->linWidgetManager() : nullptr;
        QJsonObject notifyMessage;
        notifyMessage.insert(QStringLiteral("type"), QStringLiteral("linwidget_appearance_changed_notification"));
        if (manager) {
            manager->broadcastToWidgetInstances(m_manifest.id.trimmed().toLower(), notifyMessage);
        }
        // Forward notification to other open settings dialogs for the same widget package
        for (const auto& dialog : openSettingsDialogs()) {
            if (dialog && dialog->m_manifest.id.trimmed().toLower() == m_manifest.id.trimmed().toLower()) {
#ifdef LINDESK_HAS_WEBVIEW2
                if (auto* webView = static_cast<ICoreWebView2*>(dialog->m_webView)) {
                    const QString json = QString::fromUtf8(QJsonDocument(notifyMessage).toJson(QJsonDocument::Compact));
                    webView->PostWebMessageAsJson(reinterpret_cast<LPCWSTR>(json.utf16()));
                }
#endif
            }
        }
        return;
    }
    if (!isCreate && !isList && !isClose && !isRemove && !isLoad) {
        return;
    }

    const QString requestId = object.value(QStringLiteral("requestId")).toString();
    QJsonObject response;
    response.insert(QStringLiteral("type"), isCreate
        ? QStringLiteral("linwidget_create_instance_response")
        : (isList
            ? QStringLiteral("linwidget_list_instances_response")
            : (isClose ? QStringLiteral("linwidget_close_instance_response")
                       : (isRemove ? QStringLiteral("linwidget_remove_instance_response")
                                   : QStringLiteral("linwidget_load_instance_response")))));
    response.insert(QStringLiteral("requestId"), requestId);

    QString newInstanceId;
    QString errorMessage;
    bool ok = false;
    if (m_instanceId.trimmed().isEmpty()) {
        errorMessage = QStringLiteral("Settings page is not attached to a running widget instance.");
    } else if (ListView* view = ListView::instance()) {
        if (WidgetManager* manager = view->linWidgetManager()) {
            if (isCreate) {
                QRect requestedGeometry;
                const QJsonObject geometry = object.value(QStringLiteral("geometry")).toObject();
                if (!geometry.isEmpty()) {
                    requestedGeometry = QRect(geometry.value(QStringLiteral("x")).toInt(),
                                              geometry.value(QStringLiteral("y")).toInt(),
                                              geometry.value(QStringLiteral("width")).toInt(),
                                              geometry.value(QStringLiteral("height")).toInt());
                }
                const WidgetInstance source = manager->widgetInstance(m_instanceId);
                if (!source.instanceId.isEmpty() && manager->hasRunningWidget(m_instanceId)) {
                    ok = manager->createWidgetInstance(m_instanceId, requestedGeometry, &newInstanceId, &errorMessage);
                } else {
                    ok = manager->createWidgetInstanceFromPackage(m_packagePath, m_isDevelopment, false, requestedGeometry, &newInstanceId, &errorMessage);
                }
            } else {
                QJsonArray instances;
                const QString targetRegistryId = m_manifest.id.trimmed().toLower();
                for (const WidgetLayoutState& state : manager->layoutStates()) {
                    const bool sameMode = state.isDevelopment == m_isDevelopment || state.instanceId.startsWith(m_isDevelopment ? QStringLiteral("dev:") : QStringLiteral("installed:"));
                    const bool sameRegistry = state.registryId.trimmed().toLower() == targetRegistryId ||
                        state.instanceId == QStringLiteral("%1:%2").arg(m_isDevelopment ? QStringLiteral("dev") : QStringLiteral("installed"), targetRegistryId) ||
                        state.instanceId.startsWith(QStringLiteral("%1:%2:").arg(m_isDevelopment ? QStringLiteral("dev") : QStringLiteral("installed"), targetRegistryId));
                    if (!sameMode || !sameRegistry) {
                        continue;
                    }
                    QJsonObject item;
                    item.insert(QStringLiteral("instanceId"), state.instanceId);
                    item.insert(QStringLiteral("id"), state.instanceId);
                    item.insert(QStringLiteral("visible"), state.visible);
                    item.insert(QStringLiteral("running"), manager->hasRunningWidget(state.instanceId));
                    item.insert(QStringLiteral("parentInstanceId"), state.parentInstanceId);
                    item.insert(QStringLiteral("createdAt"), state.createdAt);
                    QJsonObject geometry;
                    geometry.insert(QStringLiteral("x"), state.geometry.x());
                    geometry.insert(QStringLiteral("y"), state.geometry.y());
                    geometry.insert(QStringLiteral("width"), state.geometry.width());
                    geometry.insert(QStringLiteral("height"), state.geometry.height());
                    item.insert(QStringLiteral("geometry"), geometry);
                    instances.append(item);
                }
                if (isList) {
                    response.insert(QStringLiteral("instances"), instances);
                    ok = true;
                } else {
                    QString targetInstanceId = object.value(QStringLiteral("targetInstanceId")).toString().trimmed();
                    if (targetInstanceId.isEmpty()) {
                        targetInstanceId = m_instanceId;
                    }
                    bool allowed = false;
                    for (const QJsonValue& value : instances) {
                        if (value.toObject().value(QStringLiteral("instanceId")).toString() == targetInstanceId) {
                            allowed = true;
                            break;
                        }
                    }
                    if (allowed) {
                        ok = isClose ? (manager->stopWidget(targetInstanceId), true) : (isLoad ? manager->restartWidget(targetInstanceId) : manager->removeWidgetState(targetInstanceId));
                        newInstanceId = targetInstanceId;
                    } else {
                        errorMessage = QStringLiteral("Target widget instance is outside the caller scope.");
                    }
                }
            }
        } else {
            errorMessage = QStringLiteral("LinWidget manager is unavailable.");
        }
    } else {
        errorMessage = QStringLiteral("Desktop view is unavailable.");
    }

    response.insert(QStringLiteral("ok"), ok);
    response.insert(QStringLiteral("instanceId"), newInstanceId);
    response.insert(QStringLiteral("error"), errorMessage);

#ifdef LINDESK_HAS_WEBVIEW2
    if (auto* webView = static_cast<ICoreWebView2*>(m_webView)) {
        const QString json = QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact));
        webView->PostWebMessageAsJson(reinterpret_cast<LPCWSTR>(json.utf16()));
    }
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
