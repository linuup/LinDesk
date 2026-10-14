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

#ifdef LINDESK_HAS_CEF
#include <include/cef_scheme.h>

class LinCefApp : public CefApp {
public:
    LinCefApp() = default;

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

static CefRefPtr<LinCefApp> s_cefApp;
#endif

static QString currentModuleDirectory()
{
#ifdef Q_OS_WIN
    HMODULE hMod = nullptr;
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&currentModuleDirectory),
            &hMod) && hMod) {
        wchar_t path[MAX_PATH] = {0};
        if (GetModuleFileNameW(hMod, path, MAX_PATH) > 0) {
            return QFileInfo(QString::fromWCharArray(path)).absolutePath();
        }
    }
#endif
    return QCoreApplication::applicationDirPath();
}

QString CefManager::subprocessExecutablePath() const
{
    const QString modDir = currentModuleDirectory();
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString subprocessName = QStringLiteral("LinDeskSubprocess.exe");

    const QStringList candidatePaths = {
        QDir(modDir).filePath(subprocessName),
        QDir(appDir).filePath(QStringLiteral("runtime/linwidget/%1").arg(subprocessName)),
        QDir(appDir).filePath(subprocessName),
        QDir(appDir).filePath(QStringLiteral("../%1").arg(subprocessName))
    };

    for (const QString& candidate : candidatePaths) {
        if (QFileInfo::exists(candidate)) {
            return QDir::toNativeSeparators(QDir::cleanPath(candidate));
        }
    }

    return QDir::toNativeSeparators(QDir::cleanPath(QDir(modDir).filePath(subprocessName)));
}

QString CefManager::resourcesDirectoryPath() const
{
    const QString modDir = currentModuleDirectory();
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString icuFile = QStringLiteral("icudtl.dat");

    const QStringList candidateDirs = {
        modDir,
        QDir(modDir).filePath(QStringLiteral("Resources")),
        QDir(appDir).filePath(QStringLiteral("runtime/linwidget")),
        QDir(appDir).filePath(QStringLiteral("runtime/linwidget/Resources")),
        QDir(appDir).filePath(QStringLiteral("Resources")),
        appDir
    };

    for (const QString& dirPath : candidateDirs) {
        if (QFileInfo::exists(QDir(dirPath).filePath(icuFile))) {
            return QDir::toNativeSeparators(QDir::cleanPath(dirPath));
        }
    }

    return QString();
}

QString CefManager::localesDirectoryPath() const
{
    const QString modDir = currentModuleDirectory();
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString resDir = resourcesDirectoryPath();

    QStringList candidateDirs;
    if (!resDir.isEmpty()) {
        candidateDirs.append(QDir(resDir).filePath(QStringLiteral("locales")));
    }
    candidateDirs.append({
        QDir(modDir).filePath(QStringLiteral("locales")),
        QDir(modDir).filePath(QStringLiteral("Resources/locales")),
        QDir(appDir).filePath(QStringLiteral("runtime/linwidget/locales")),
        QDir(appDir).filePath(QStringLiteral("runtime/linwidget/Resources/locales")),
        QDir(appDir).filePath(QStringLiteral("locales")),
        QDir(appDir).filePath(QStringLiteral("Resources/locales"))
    });

    for (const QString& dirPath : candidateDirs) {
        if (QDir(dirPath).exists()) {
            return QDir::toNativeSeparators(QDir::cleanPath(dirPath));
        }
    }

    return QString();
}

QString CefManager::cacheStoragePath() const
{
    return QDir::toNativeSeparators(QDir::cleanPath(QDir(AppPaths::userLocalConfigDir()).filePath(QStringLiteral("LinWidgets/cef-cache"))));
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
    const QString resourcesPath = resourcesDirectoryPath();
    const QString localesPath = localesDirectoryPath();
    const QString cachePath = cacheStoragePath();

    QDir().mkpath(cachePath);

    CefMainArgs mainArgs(GetModuleHandleW(nullptr));
    CefSettings settings;

    settings.windowless_rendering_enabled = 1;
    settings.multi_threaded_message_loop = 0; // Single UI thread cooperative loop
    settings.no_sandbox = 1;
    settings.command_line_args_disabled = 0;
    settings.background_color = 0x00000000; // Transparent default ARGB

    // 独立日志文件 logs/cef.log 与日志级别
    const QString cefLogPath = QDir::toNativeSeparators(QDir::cleanPath(QDir(AppPaths::userLogsDir()).filePath(QStringLiteral("cef.log"))));
    const std::wstring cefLogW = cefLogPath.toStdWString();
    CefString(&settings.log_file).FromWString(cefLogW);

#ifdef QT_DEBUG
    settings.log_severity = LOGSEVERITY_DEFAULT;
#else
    settings.log_severity = LOGSEVERITY_WARNING;
#endif

    if (!subProcessPath.isEmpty() && QFileInfo::exists(subProcessPath)) {
        const std::wstring subProcessW = subProcessPath.toStdWString();
        CefString(&settings.browser_subprocess_path).FromWString(subProcessW);
    } else {
        qWarning() << "[CefManager] Subprocess executable not found at:" << subProcessPath;
    }

    if (!resourcesPath.isEmpty() && QDir(resourcesPath).exists()) {
        const std::wstring resourcesW = resourcesPath.toStdWString();
        CefString(&settings.resources_dir_path).FromWString(resourcesW);
    } else {
        qWarning() << "[CefManager] CEF Resources dir (icudtl.dat) not found!";
    }

    if (!localesPath.isEmpty() && QDir(localesPath).exists()) {
        const std::wstring localesW = localesPath.toStdWString();
        CefString(&settings.locales_dir_path).FromWString(localesW);
    } else {
        qWarning() << "[CefManager] CEF Locales dir not found!";
    }

    const std::wstring cacheW = cachePath.toStdWString();
    CefString(&settings.cache_path).FromWString(cacheW);

    const std::wstring localeW = L"zh-CN";
    CefString(&settings.locale).FromWString(localeW);

    // 将初始化关键参数写盘作为第一现场 trace
    const QString tracePath = QDir(AppPaths::userLogsDir()).filePath(QStringLiteral("cef_init_trace.log"));
    if (FILE* fp = _wfopen(reinterpret_cast<const wchar_t*>(tracePath.utf16()), L"w")) {
        fwprintf(fp, L"=== CefInitialize Parameters ===\n");
        fwprintf(fp, L"subProcessPath: %ls (exists: %d)\n", (const wchar_t*)subProcessPath.utf16(), QFileInfo::exists(subProcessPath) ? 1 : 0);
        fwprintf(fp, L"resourcesPath: %ls (exists: %d)\n", (const wchar_t*)resourcesPath.utf16(), QDir(resourcesPath).exists() ? 1 : 0);
        fwprintf(fp, L"localesPath: %ls (exists: %d)\n", (const wchar_t*)localesPath.utf16(), QDir(localesPath).exists() ? 1 : 0);
        fwprintf(fp, L"cachePath: %ls\n", (const wchar_t*)cachePath.utf16());
        fwprintf(fp, L"cefLogPath: %ls\n", (const wchar_t*)cefLogPath.utf16());
        fwprintf(fp, L"hInstance: %p\n", mainArgs.instance);
        fwprintf(fp, L"settings.size: %zu (sizeof struct: %zu)\n", settings.size, sizeof(cef_settings_t));
        fflush(fp);
        fclose(fp);
    }

    qInfo() << "[CefManager] Initializing CEF OSR runtime..."
            << "\n  subProcess=" << subProcessPath
            << "\n  resources=" << resourcesPath
            << "\n  locales=" << localesPath
            << "\n  cache=" << cachePath;

    s_cefApp = new LinCefApp();
    const bool success = CefInitialize(mainArgs, settings, s_cefApp, nullptr);
    if (!success) {
        s_cefApp = nullptr;
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
    s_cefApp = nullptr;
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
