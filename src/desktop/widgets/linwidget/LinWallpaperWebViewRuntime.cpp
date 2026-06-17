#include "desktop/widgets/linwidget/LinWallpaperWebViewRuntime.h"
#include "desktop/widgets/linwidget/WidgetPackage.h"
#include "desktop/ListView.h"
#include "desktop/widgets/linwidget/WidgetManager.h"
#include "services/AppPaths.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QUrl>
#include <QResizeEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifdef LINDESK_HAS_WEBVIEW2
#include <WebView2.h>
#include <wrl.h>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
#endif

namespace LinWidget {

LinWallpaperWebViewRuntime::LinWallpaperWebViewRuntime(QString wallpaperId,
                                                     QString packagePath,
                                                     QWidget* parent)
    : QWidget(parent)
    , m_wallpaperId(std::move(wallpaperId))
    , m_packagePath(std::move(packagePath))
{
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_NoSystemBackground, false);
}

LinWallpaperWebViewRuntime::~LinWallpaperWebViewRuntime()
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

bool LinWallpaperWebViewRuntime::initialize(QString* errorMessage)
{
#ifdef LINDESK_HAS_WEBVIEW2
    const HRESULT coResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    m_comInitialized = SUCCEEDED(coResult);
#endif

    if (!WidgetPackage::loadManifest(m_packagePath, &m_manifest, errorMessage)) {
        qWarning() << "[WallpaperWebView] Manifest load failed:" << (errorMessage ? *errorMessage : "");
        return false;
    }

    setupWindow();
    renderHtmlWallpaper();
    return true;
}

void LinWallpaperWebViewRuntime::setupWindow()
{
    setAttribute(Qt::WA_NativeWindow, true);
    setWindowFlags(Qt::FramelessWindowHint | Qt::SubWindow);
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        setGeometry(screen->geometry());
    }
}

void LinWallpaperWebViewRuntime::renderHtmlWallpaper()
{
#ifndef LINDESK_HAS_WEBVIEW2
    qWarning() << "[WallpaperWebView] WebView2 SDK not enabled.";
#else
    const QString entryPath = WidgetPackage::entryPath(m_packagePath, m_manifest);
    if (!QFileInfo::exists(entryPath)) {
        qWarning() << "[WallpaperWebView] Entry file is missing:" << entryPath;
        return;
    }

    const HWND parentWindow = reinterpret_cast<HWND>(winId());
    if (!parentWindow) {
        qWarning() << "[WallpaperWebView] Cannot create native widget window.";
        return;
    }

    const QString userDataDir = QDir(AppPaths::userLocalConfigDir()).filePath(QStringLiteral("LinWallpapers/.webview2"));
    const std::wstring userData = QDir::toNativeSeparators(userDataDir).toStdWString();
    
    const HRESULT result = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,
        userData.c_str(),
        nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, parentWindow, entryPath](HRESULT envResult, ICoreWebView2Environment* environment) -> HRESULT {
                if (FAILED(envResult) || !environment) {
                    qWarning() << "[WallpaperWebView] Failed to initialize WebView2 environment";
                    return S_OK;
                }

                environment->CreateCoreWebView2Controller(
                    parentWindow,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this, entryPath, environment](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(controllerResult) || !controller) {
                                qWarning() << "[WallpaperWebView] Failed to create WebView2 controller";
                                return S_OK;
                            }

                            controller->AddRef();
                            m_webViewController = controller;
                            ICoreWebView2* webView = nullptr;
                            controller->get_CoreWebView2(&webView);
                            m_webView = webView;
                            
                            ComPtr<ICoreWebView2_3> webView3;
                            if (SUCCEEDED(webView->QueryInterface(IID_PPV_ARGS(&webView3)))) {
                                const std::wstring host = L"wallpaper.local.lindesk";
                                const std::wstring folder = QDir::toNativeSeparators(m_packagePath).toStdWString();
                                webView3->SetVirtualHostNameToFolderMapping(
                                    host.c_str(),
                                    folder.c_str(),
                                    COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
                            }

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
                                settings->put_IsZoomControlEnabled(FALSE);
                            }
                            controller->put_ZoomFactor(1.0);

                            if (webView) {
                                webView->Navigate(L"https://wallpaper.local.lindesk/index.html");
                                m_webViewReady = true;
                            }

                            // Re-sync Z-order after async WebView2 init to ensure
                            // wallpaper stays below ListView
                            QTimer::singleShot(0, this, [this]() {
                                ListView* lv = ListView::instance();
                                if (lv && lv->linWidgetManager()) {
                                    lv->linWidgetManager()->syncWallpaperWindowLayer();
                                }
                            });
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());

    if (FAILED(result)) {
        qWarning() << "[WallpaperWebView] Failed to start WebView2 environment";
    }
#endif
}

void LinWallpaperWebViewRuntime::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateWebViewBounds();
}

void LinWallpaperWebViewRuntime::updateWebViewBounds()
{
#ifdef LINDESK_HAS_WEBVIEW2
    auto* controller = static_cast<ICoreWebView2Controller*>(m_webViewController);
    if (controller) {
        RECT rect = { 0, 0, width(), height() };
        controller->put_Bounds(rect);
    }
#endif
}

bool LinWallpaperWebViewRuntime::nativeEvent(const QByteArray& eventType, void* message, long* result)
{
#ifdef Q_OS_WIN
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_NCHITTEST) {
        if (!m_interactive) {
            *result = HTTRANSPARENT;
            return true;
        }
    } else if (msg->message == WM_MOUSEACTIVATE) {
        if (result) {
            *result = MA_NOACTIVATE;
        }
        return true;
    } else if (msg->message == WM_WINDOWPOSCHANGING) {
        WINDOWPOS* wp = reinterpret_cast<WINDOWPOS*>(msg->lParam);
        // Completely lock Z-order — prevent any Z-order changes
        // that might bring the wallpaper above ListView
        wp->flags |= SWP_NOZORDER;
    }
#endif
    return QWidget::nativeEvent(eventType, message, result);
}

void LinWallpaperWebViewRuntime::setInteractive(bool interactive)
{
    m_interactive = interactive;
}

void LinWallpaperWebViewRuntime::postWebMessage(const QJsonObject& payload)
{
#ifdef LINDESK_HAS_WEBVIEW2
    auto* webView = static_cast<ICoreWebView2*>(m_webView);
    if (webView && m_webViewReady) {
        const QString json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
        webView->PostWebMessageAsJson(reinterpret_cast<LPCWSTR>(json.utf16()));
    }
#endif
}

void LinWallpaperWebViewRuntime::suspend()
{
    if (!m_suspended) {
        m_suspended = true;
        postWebMessage({
            { QStringLiteral("type"), QStringLiteral("suspend") }
        });
    }
}

void LinWallpaperWebViewRuntime::resume()
{
    if (m_suspended) {
        m_suspended = false;
        postWebMessage({
            { QStringLiteral("type"), QStringLiteral("resume") }
        });
    }
}

void LinWallpaperWebViewRuntime::mirrorMouseMove(int x, int y)
{
    postWebMessage({
        { QStringLiteral("type"), QStringLiteral("desktop_mousemove") },
        { QStringLiteral("x"), x },
        { QStringLiteral("y"), y }
    });
}

void LinWallpaperWebViewRuntime::mirrorMouseDown(int x, int y, const QString& button)
{
    postWebMessage({
        { QStringLiteral("type"), QStringLiteral("desktop_mousedown") },
        { QStringLiteral("x"), x },
        { QStringLiteral("y"), y },
        { QStringLiteral("button"), button }
    });
}

void LinWallpaperWebViewRuntime::mirrorMouseUp(int x, int y, const QString& button)
{
    postWebMessage({
        { QStringLiteral("type"), QStringLiteral("desktop_mouseup") },
        { QStringLiteral("x"), x },
        { QStringLiteral("y"), y },
        { QStringLiteral("button"), button }
    });
}

void LinWallpaperWebViewRuntime::pushAudioData(const QVector<float>& frequencies)
{
    QJsonArray freqArray;
    for (float f : frequencies) {
        freqArray.append(f);
    }
    postWebMessage({
        { QStringLiteral("type"), QStringLiteral("audio_data") },
        { QStringLiteral("frequencies"), freqArray }
    });
}

void LinWallpaperWebViewRuntime::pushSystemInfo(const QJsonObject& info)
{
    QJsonObject payload = info;
    payload.insert(QStringLiteral("type"), QStringLiteral("system_info"));
    postWebMessage(payload);
}

}
