#include "desktop/widgets/cef/CefManager.h"
#include "services/AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

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
#include <include/cef_app.h>
#endif

namespace LinCef {

CefManager* CefManager::s_instance = nullptr;

CefManager* CefManager::instance()
{
    if (!s_instance) {
        s_instance = new CefManager(QCoreApplication::instance());
    }
    return s_instance;
}

CefManager::CefManager(QObject* parent)
    : QObject(parent)
{
}

CefManager::~CefManager()
{
    shutdown();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

bool CefManager::isCefAvailable() const
{
#ifdef LINDESK_HAS_CEF
    return true;
#else
    return false;
#endif
}

QString CefManager::subprocessExecutablePath() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString subprocessName = QStringLiteral("LinDeskSubprocess.exe");
    
    // Check next to main executable
    QString path = QDir(appDir).filePath(subprocessName);
    if (QFileInfo::exists(path)) {
        return path;
    }

    // Check build directory layouts
    path = QDir(appDir).filePath(QStringLiteral("../LinDeskSubprocess.exe"));
    if (QFileInfo::exists(path)) {
        return QFileInfo(path).canonicalFilePath();
    }

    return QDir(appDir).filePath(subprocessName);
}

QString CefManager::cacheStoragePath() const
{
    return QDir(AppPaths::userLocalConfigDir()).filePath(QStringLiteral("LinWidgets/cef-cache"));
}

bool CefManager::initialize(QString* errorMessage)
{
    if (m_initialized) {
        return true;
    }

#ifndef LINDESK_HAS_CEF
    const QString msg = tr("CEF SDK is not integrated in this build.");
    if (errorMessage) {
        *errorMessage = msg;
    }
    qWarning() << "[CefManager]" << msg;
    return false;
#else
    const QString subProcessPath = subprocessExecutablePath();
    const QString cachePath = cacheStoragePath();

    QDir().mkpath(cachePath);

#ifdef LINDESK_HAS_CEF
#include <include/cef_scheme.h>
class LinCefApp : public CefApp, public CefBrowserProcessHandler {
public:
    LinCefApp() = default;

    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
        return this;
    }

    void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override {
        registrar->AddCustomScheme("linwidget",
            CEF_SCHEME_OPTION_STANDARD |
            CEF_SCHEME_OPTION_SECURE |
            CEF_SCHEME_OPTION_CORS_ENABLED |
            CEF_SCHEME_OPTION_FETCH_ENABLED);
    }

    void OnBeforeCommandLineProcessing(const CefString& process_type, CefRefPtr<CefCommandLine> command_line) override {
        Q_UNUSED(process_type);
        // OSR 离屏渲染关键开关：禁用 GPU 合成，使 Chromium 走软件渲染管线将帧数据回调到 OnPaint
        command_line->AppendSwitch("disable-gpu");
        command_line->AppendSwitch("disable-gpu-compositing");
        command_line->AppendSwitch("enable-begin-frame-scheduling");
        command_line->AppendSwitch("allow-file-access-from-files");

        // 统一配置 CEF 崩溃转储目录
        const QString crashDumpsDir = AppPaths::userCrashDumpsDir();
        command_line->AppendSwitchWithValue("crash-dumps-dir", QDir::toNativeSeparators(crashDumpsDir).toStdString());
    }

    IMPLEMENT_REFCOUNTING(LinCefApp);
};
#endif

    CefMainArgs mainArgs(GetModuleHandleW(nullptr));
    CefSettings settings;

    settings.windowless_rendering_enabled = 1;
    settings.multi_threaded_message_loop = 0; // Single UI thread cooperative loop
    settings.no_sandbox = 1;
    settings.command_line_args_disabled = 0;
    settings.background_color = 0x00000000; // Transparent default ARGB

    // 独立日志文件 logs/cef.log 与日志级别
    const QString cefLogPath = QDir(AppPaths::userLogsDir()).filePath(QStringLiteral("cef.log"));
    const std::wstring cefLogW = QDir::toNativeSeparators(cefLogPath).toStdWString();
    CefString(&settings.log_file).FromWString(cefLogW);

#ifdef QT_DEBUG
    settings.log_severity = LOGSEVERITY_DEFAULT;
#else
    settings.log_severity = LOGSEVERITY_WARNING;
#endif

    const std::wstring subProcessW = QDir::toNativeSeparators(subProcessPath).toStdWString();
    if (QFileInfo::exists(subProcessPath)) {
        CefString(&settings.browser_subprocess_path).FromWString(subProcessW);
    }

    const std::wstring cacheW = QDir::toNativeSeparators(cachePath).toStdWString();
    CefString(&settings.cache_path).FromWString(cacheW);
    CefString(&settings.root_cache_path).FromWString(cacheW);

    const std::wstring localeW = L"zh-CN";
    CefString(&settings.locale).FromWString(localeW);

    qInfo() << "[CefManager] Initializing CEF OSR runtime..."
            << "subProcess=" << subProcessPath
            << "cache=" << cachePath;

    CefRefPtr<LinCefApp> app = new LinCefApp();
    const bool success = CefInitialize(mainArgs, settings, app, nullptr);
    if (!success) {
        const QString err = tr("CefInitialize failed.");
        if (errorMessage) {
            *errorMessage = err;
        }
        qCritical() << "[CefManager]" << err;
        return false;
    }

    m_initialized = true;
    startMessageLoopPump();
    emit cefInitialized();
    qInfo() << "[CefManager] CEF OSR runtime successfully initialized.";
    return true;
#endif
}

void CefManager::shutdown()
{
    if (!m_initialized) {
        return;
    }

    stopMessageLoopPump();

#ifdef LINDESK_HAS_CEF
    qInfo() << "[CefManager] Shutting down CEF OSR runtime...";
    // Flush pending message loop work so browser cleanup tasks complete before shutdown
    for (int i = 0; i < 10; ++i) {
        CefDoMessageLoopWork();
    }
    CefShutdown();
#endif

    m_initialized = false;
    emit cefShutdown();
}

void CefManager::startMessageLoopPump()
{
    if (!m_messagePumpTimer) {
        m_messagePumpTimer = new QTimer(this);
        m_messagePumpTimer->setInterval(10); // 100Hz cooperative dispatch
        connect(m_messagePumpTimer, &QTimer::timeout, this, []() {
#ifdef LINDESK_HAS_CEF
            CefDoMessageLoopWork();
#endif
        });
    }
    m_messagePumpTimer->start();
}

void CefManager::stopMessageLoopPump()
{
    if (m_messagePumpTimer) {
        m_messagePumpTimer->stop();
        m_messagePumpTimer->deleteLater();
        m_messagePumpTimer = nullptr;
    }
}

} // namespace LinCef
