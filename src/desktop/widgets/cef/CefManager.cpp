#include "desktop/widgets/cef/CefManager.h"
#include "services/AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QThread>
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
    stopMessageLoopPump();
    m_initialized = false;
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
        // 关键开关：在以管理员身份运行（如 VS 调试）时防止 Chromium 尝试提权/降权导致 Exit Code 38
        command_line->AppendSwitch("do-not-de-elevate");
        // 沙箱开关：与 settings.no_sandbox = 1 呼应
        command_line->AppendSwitch("no-sandbox");
        command_line->AppendSwitch("disable-site-isolation-trials");
        command_line->AppendSwitch("disable-features=RendererCodeIntegrity");

        // OSR 离屏渲染关键开关：禁用 GPU 合成，使 Chromium 走软件渲染管线将帧数据回调到 OnPaint
        command_line->AppendSwitch("disable-gpu");
        command_line->AppendSwitch("disable-gpu-compositing");
        command_line->AppendSwitch("allow-file-access-from-files");

        // 统一配置 CEF 崩溃转储目录
        const QString crashDumpsDir = AppPaths::userCrashDumpsDir();
        command_line->AppendSwitchWithValue("crash-dumps-dir", QDir::toNativeSeparators(crashDumpsDir).toStdString());
    }

    IMPLEMENT_REFCOUNTING(LinCefApp);
};

#ifdef Q_OS_WIN
static bool callCefInitializeRaw(const CefMainArgs* args, const CefSettings* settings, CefApp* app, void* sandboxInfo)
{
    return CefInitialize(*args, *settings, app, sandboxInfo);
}

static bool doCefInitializeSEH(const CefMainArgs* args, const CefSettings* settings, CefApp* app, void* sandboxInfo, bool* outSuccess)
{
    __try {
        *outSuccess = callCefInitializeRaw(args, settings, app, sandboxInfo);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *outSuccess = false;
        return false;
    }
}
#endif

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

static QString libcefModuleDirectory()
{
#ifdef Q_OS_WIN
    if (HMODULE hCef = GetModuleHandleW(L"libcef.dll")) {
        wchar_t path[MAX_PATH] = {0};
        if (GetModuleFileNameW(hCef, path, MAX_PATH) > 0) {
            return QFileInfo(QString::fromWCharArray(path)).absolutePath();
        }
    }
#endif
    return currentModuleDirectory();
}

static QStringList assetSearchDirectories()
{
    const QString cefDir = libcefModuleDirectory();
    const QString modDir = currentModuleDirectory();
    const QString appDir = QCoreApplication::applicationDirPath();

    QStringList dirs = {
        cefDir,
        QDir(cefDir).filePath(QStringLiteral("Resources")),
        modDir,
        QDir(modDir).filePath(QStringLiteral("Resources")),
        QDir(appDir).filePath(QStringLiteral("runtime/linwidget")),
        QDir(appDir).filePath(QStringLiteral("runtime/linwidget/Resources")),
        QDir(appDir).filePath(QStringLiteral("Resources")),
        appDir
    };
#ifdef LINDESK_SOURCE_DIR
    dirs.append(QDir(QStringLiteral(LINDESK_SOURCE_DIR)).filePath(QStringLiteral("3rdparty/cef/Resources")));
    dirs.append(QDir(QStringLiteral(LINDESK_SOURCE_DIR)).filePath(QStringLiteral("3rdparty/cef/Release")));
#endif
    return dirs;
}

static bool ensureAssetInModuleDirectory(const QString& targetDir, const QString& fileName)
{
    const QString targetPath = QDir(targetDir).filePath(fileName);
    const QFileInfo existingInfo(targetPath);
    if (existingInfo.exists() && existingInfo.size() > 0) {
        return true;
    }

    for (const QString& candidateDir : assetSearchDirectories()) {
        const QString sourcePath = QDir(candidateDir).filePath(fileName);
        const QFileInfo sourceInfo(sourcePath);
        if (sourceInfo.exists() && sourceInfo.size() > 0 &&
            QFileInfo(sourcePath).canonicalFilePath() != existingInfo.canonicalFilePath()) {
            if (QFile::exists(targetPath)) {
                QFile::remove(targetPath);
            }
            if (QFile::copy(sourcePath, targetPath)) {
                qInfo() << "[LinWidget][CefManager] Staged required asset into libcef module dir:" << targetPath << "from" << sourcePath;
                return true;
            }
        }
    }

    return false;
}

QString CefManager::subprocessExecutablePath() const
{
    const QString cefDir = libcefModuleDirectory();
    const QString modDir = currentModuleDirectory();
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString subprocessName = QStringLiteral("LinDeskSubprocess.exe");

    const QStringList candidatePaths = {
        QDir(cefDir).filePath(subprocessName),
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
    const QString pakFile = QStringLiteral("resources.pak");
    const QString icuFile = QStringLiteral("icudtl.dat");
    const QStringList candidateDirs = assetSearchDirectories();

    // CefSettings.resources_dir_path 用于定位 .pak 资源包（resources.pak / chrome_*_percent.pak）
    for (const QString& dirPath : candidateDirs) {
        if (QFileInfo::exists(QDir(dirPath).filePath(pakFile))) {
            return QDir::toNativeSeparators(QDir::cleanPath(dirPath));
        }
    }

    for (const QString& dirPath : candidateDirs) {
        if (QFileInfo::exists(QDir(dirPath).filePath(icuFile))) {
            return QDir::toNativeSeparators(QDir::cleanPath(dirPath));
        }
    }

    return QString();
}

QString CefManager::localesDirectoryPath() const
{
    const QString cefDir = libcefModuleDirectory();
    const QString modDir = currentModuleDirectory();
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString resDir = resourcesDirectoryPath();

    QStringList candidateDirs;
    if (!resDir.isEmpty()) {
        candidateDirs.append(QDir(resDir).filePath(QStringLiteral("locales")));
    }
    candidateDirs.append({
        QDir(cefDir).filePath(QStringLiteral("locales")),
        QDir(cefDir).filePath(QStringLiteral("Resources/locales")),
        QDir(modDir).filePath(QStringLiteral("locales")),
        QDir(modDir).filePath(QStringLiteral("Resources/locales")),
        QDir(appDir).filePath(QStringLiteral("runtime/linwidget/locales")),
        QDir(appDir).filePath(QStringLiteral("runtime/linwidget/Resources/locales")),
        QDir(appDir).filePath(QStringLiteral("locales")),
        QDir(appDir).filePath(QStringLiteral("Resources/locales"))
    });
#ifdef LINDESK_SOURCE_DIR
    candidateDirs.append(QDir(QStringLiteral(LINDESK_SOURCE_DIR)).filePath(QStringLiteral("3rdparty/cef/Resources/locales")));
#endif

    for (const QString& dirPath : candidateDirs) {
        const QDir dir(dirPath);
        if (dir.exists() &&
            (QFileInfo::exists(dir.filePath(QStringLiteral("zh-CN.pak"))) ||
             QFileInfo::exists(dir.filePath(QStringLiteral("en-US.pak"))))) {
            return QDir::toNativeSeparators(QDir::cleanPath(dirPath));
        }
    }

    for (const QString& dirPath : candidateDirs) {
        if (QDir(dirPath).exists()) {
            return QDir::toNativeSeparators(QDir::cleanPath(dirPath));
        }
    }

    return QString();
}

#ifdef Q_OS_WIN
static HANDLE s_hPrimaryCacheMutex = nullptr;
#endif

static bool isCefCacheDirectoryLocked(const QString& cacheDir)
{
    if (!QDir(cacheDir).exists()) {
        return false;
    }
#ifdef Q_OS_WIN
    const QStringList candidateFiles = {
        QDir(cacheDir).filePath(QStringLiteral("lockfile")),
        QDir(cacheDir).filePath(QStringLiteral("Local State")),
        QDir(cacheDir).filePath(QStringLiteral("Default/LOCK")),
        QDir(cacheDir).filePath(QStringLiteral("Default/Preferences")),
        QDir(cacheDir).filePath(QStringLiteral("first_party_sets.db"))
    };

    for (const QString& filePath : candidateFiles) {
        if (!QFileInfo::exists(filePath)) {
            continue;
        }
        const std::wstring filePathW = QDir::toNativeSeparators(filePath).toStdWString();
        HANDLE hFile = CreateFileW(
            filePathW.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (hFile == INVALID_HANDLE_VALUE) {
            const DWORD err = GetLastError();
            if (err == ERROR_SHARING_VIOLATION || err == ERROR_LOCK_VIOLATION || err == ERROR_ACCESS_DENIED) {
                return true;
            }
        } else {
            CloseHandle(hFile);
        }
    }

    // 检查 BrowserMetrics 下的持久内存映射文件是否被另一 Chromium 进程独占
    const QDir metricsDir(QDir(cacheDir).filePath(QStringLiteral("BrowserMetrics")));
    if (metricsDir.exists()) {
        const QFileInfoList metricFiles = metricsDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
        for (const QFileInfo& fi : metricFiles) {
            const std::wstring filePathW = QDir::toNativeSeparators(fi.absoluteFilePath()).toStdWString();
            HANDLE hFile = CreateFileW(
                filePathW.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);
            if (hFile == INVALID_HANDLE_VALUE) {
                const DWORD err = GetLastError();
                if (err == ERROR_SHARING_VIOLATION || err == ERROR_LOCK_VIOLATION || err == ERROR_ACCESS_DENIED) {
                    return true;
                }
            } else {
                CloseHandle(hFile);
            }
        }
    }
#endif
    return false;
}

QString CefManager::cacheStoragePath() const
{
    const QDir linWidgetsLocalDir(QDir(AppPaths::userLocalConfigDir()).filePath(QStringLiteral("LinWidgets")));
    const QString defaultCacheDir = linWidgetsLocalDir.filePath(QStringLiteral("cef-cache"));

    // 清理之前遗留且未被锁定的进程级临时缓存目录
    const QFileInfoList staleDirs = linWidgetsLocalDir.entryInfoList(
        QStringList{QStringLiteral("cef-cache-pid-*")},
        QDir::Dirs | QDir::NoDotAndDotDot);
    const QString currentPidSuffix = QStringLiteral("cef-cache-pid-%1").arg(QCoreApplication::applicationPid());
    for (const QFileInfo& info : staleDirs) {
        if (info.fileName() != currentPidSuffix && !isCefCacheDirectoryLocked(info.absoluteFilePath())) {
            QDir(info.absoluteFilePath()).removeRecursively();
        }
    }

    bool primaryLocked = false;
#ifdef Q_OS_WIN
    if (!s_hPrimaryCacheMutex) {
        s_hPrimaryCacheMutex = CreateMutexW(nullptr, FALSE, L"Local\\LinDesk_CefCache_Primary_Lock");
    }
    if (s_hPrimaryCacheMutex) {
        const DWORD waitRes = WaitForSingleObject(s_hPrimaryCacheMutex, 0);
        if (waitRes == WAIT_TIMEOUT) {
            primaryLocked = true;
        } else if (waitRes == WAIT_ABANDONED) {
            // 前序持有进程异常终止，安全清理残留 lock 标记
            QFile::remove(QDir(defaultCacheDir).filePath(QStringLiteral("Default/LOCK")));
            QFile::remove(QDir(defaultCacheDir).filePath(QStringLiteral("lockfile")));
        }
    }
#endif

    // Chromium 在 cache_path 被另一进程锁定时会返回 RESULT_CODE_PROFILE_IN_USE (21) 并在 CefInitialize 内部触发 0xc0000005 崩溃
    if (primaryLocked || isCefCacheDirectoryLocked(defaultCacheDir)) {
        const QString fallbackCacheDir = linWidgetsLocalDir.filePath(currentPidSuffix);
        qWarning() << "[LinWidget][CefManager] Primary CEF cache dir is locked by another process (primaryLocked="
                   << primaryLocked << "):" << defaultCacheDir
                   << "- falling back to per-process cache dir:" << fallbackCacheDir;
        return QDir::toNativeSeparators(QDir::cleanPath(fallbackCacheDir));
    }

    return QDir::toNativeSeparators(QDir::cleanPath(defaultCacheDir));
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
    qWarning() << "[LinWidget][CefManager]" << msg;
    return false;
#else
    // Chromium 在 Windows 下强制从 libcef.dll 所在目录 (DIR_MODULE / DIR_ASSETS) 加载 icudtl.dat 与 v8_context_snapshot.bin，
    // 不受 CefSettings.resources_dir_path 控制。若缺失会直接触发 base/i18n/icu_util.cc FATAL CHECK 崩溃。
    const QString cefModuleDir = libcefModuleDirectory();
    const bool hasIcuData = ensureAssetInModuleDirectory(cefModuleDir, QStringLiteral("icudtl.dat"));
    const bool hasV8Snapshot = ensureAssetInModuleDirectory(cefModuleDir, QStringLiteral("v8_context_snapshot.bin"));

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
    settings.log_severity = LOGSEVERITY_VERBOSE;
#else
    settings.log_severity = LOGSEVERITY_WARNING;
#endif

    const bool hasSubProcess = !subProcessPath.isEmpty() && QFileInfo::exists(subProcessPath);
    if (hasSubProcess) {
        const std::wstring subProcessW = subProcessPath.toStdWString();
        CefString(&settings.browser_subprocess_path).FromWString(subProcessW);
    } else {
        qWarning() << "[LinWidget][CefManager] Subprocess executable not found at:" << subProcessPath;
    }

    const bool hasResourcesPak = !resourcesPath.isEmpty() && QFileInfo::exists(QDir(resourcesPath).filePath(QStringLiteral("resources.pak")));
    if (!resourcesPath.isEmpty() && QDir(resourcesPath).exists()) {
        const std::wstring resourcesW = resourcesPath.toStdWString();
        CefString(&settings.resources_dir_path).FromWString(resourcesW);
    } else {
        qWarning() << "[LinWidget][CefManager] CEF Resources dir (resources.pak) not found!";
    }

    const bool hasLocalesPak = !localesPath.isEmpty() &&
        (QFileInfo::exists(QDir(localesPath).filePath(QStringLiteral("zh-CN.pak"))) ||
         QFileInfo::exists(QDir(localesPath).filePath(QStringLiteral("en-US.pak"))));
    if (!localesPath.isEmpty() && QDir(localesPath).exists()) {
        const std::wstring localesW = localesPath.toStdWString();
        CefString(&settings.locales_dir_path).FromWString(localesW);
    } else {
        qWarning() << "[LinWidget][CefManager] CEF Locales dir not found!";
    }

    const std::wstring cacheW = cachePath.toStdWString();
    CefString(&settings.cache_path).FromWString(cacheW);
    CefString(&settings.root_cache_path).FromWString(cacheW);

    const std::wstring localeW = L"zh-CN";
    CefString(&settings.locale).FromWString(localeW);

    const QString icuInCefDir = QDir::toNativeSeparators(QDir(cefModuleDir).filePath(QStringLiteral("icudtl.dat")));
    const QString v8InCefDir = QDir::toNativeSeparators(QDir(cefModuleDir).filePath(QStringLiteral("v8_context_snapshot.bin")));

    // 将初始化关键参数写盘作为第一现场 trace
    const QString tracePath = QDir(AppPaths::userLogsDir()).filePath(QStringLiteral("cef_init_trace.log"));
    if (FILE* fp = _wfopen(reinterpret_cast<const wchar_t*>(tracePath.utf16()), L"w")) {
        fwprintf(fp, L"=== CefInitialize Parameters ===\n");
        fwprintf(fp, L"cefModuleDir: %ls\n", (const wchar_t*)cefModuleDir.utf16());
        fwprintf(fp, L"icudtlPath: %ls (exists: %d)\n", (const wchar_t*)icuInCefDir.utf16(), hasIcuData ? 1 : 0);
        fwprintf(fp, L"v8SnapshotPath: %ls (exists: %d)\n", (const wchar_t*)v8InCefDir.utf16(), hasV8Snapshot ? 1 : 0);
        fwprintf(fp, L"subProcessPath: %ls (exists: %d)\n", (const wchar_t*)subProcessPath.utf16(), hasSubProcess ? 1 : 0);
        fwprintf(fp, L"resourcesPath: %ls (exists: %d, resources.pak: %d)\n",
                 (const wchar_t*)resourcesPath.utf16(),
                 QDir(resourcesPath).exists() ? 1 : 0,
                 hasResourcesPak ? 1 : 0);
        fwprintf(fp, L"localesPath: %ls (exists: %d, pak: %d)\n",
                 (const wchar_t*)localesPath.utf16(),
                 QDir(localesPath).exists() ? 1 : 0,
                 hasLocalesPak ? 1 : 0);
        fwprintf(fp, L"cachePath: %ls\n", (const wchar_t*)cachePath.utf16());
        fwprintf(fp, L"cefLogPath: %ls\n", (const wchar_t*)cefLogPath.utf16());
        fwprintf(fp, L"hInstance: %p\n", mainArgs.instance);
        fwprintf(fp, L"settings.size: %zu (sizeof struct: %zu)\n", settings.size, sizeof(cef_settings_t));
        fflush(fp);
        fclose(fp);
    }

    if (!hasIcuData || !hasV8Snapshot || !hasSubProcess || !hasResourcesPak || !hasLocalesPak) {
        const QString err = tr("CEF runtime required assets missing (icudtl=%1, v8=%2, subprocess=%3, resources=%4, locales=%5).")
                                .arg(hasIcuData)
                                .arg(hasV8Snapshot)
                                .arg(hasSubProcess)
                                .arg(hasResourcesPak)
                                .arg(hasLocalesPak);
        if (errorMessage) {
            *errorMessage = err;
        }
        qCritical() << "[LinWidget][CefManager] Aborting CefInitialize to prevent fatal CHECK crash:" << err;
        return false;
    }

    qInfo() << "[LinWidget][CefManager] Initializing CEF OSR runtime..."
            << "\n  cefModuleDir=" << cefModuleDir
            << "\n  subProcess=" << subProcessPath
            << "\n  resources=" << resourcesPath
            << "\n  locales=" << localesPath
            << "\n  cache=" << cachePath;

    s_cefApp = new LinCefApp();
    bool success = false;
#ifdef Q_OS_WIN
    const bool sehOk = doCefInitializeSEH(&mainArgs, &settings, s_cefApp.get(), nullptr, &success);
    if (!sehOk) {
        s_cefApp = nullptr;
        const QString err = tr("CefInitialize crashed with an unhandled exception.");
        if (errorMessage) {
            *errorMessage = err;
        }
        qCritical() << "[LinWidget][CefManager]" << err;
        return false;
    }
#else
    success = CefInitialize(mainArgs, settings, s_cefApp, nullptr);
#endif
    if (!success) {
        s_cefApp = nullptr;
        const QString err = tr("CefInitialize failed.");
        if (errorMessage) {
            *errorMessage = err;
        }
        qCritical() << "[LinWidget][CefManager]" << err;
        return false;
    }

    m_initialized = true;
    startMessageLoopPump();
    emit cefInitialized();
    qInfo() << "[LinWidget][CefManager] CEF OSR runtime successfully initialized.";
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
    qInfo() << "[LinWidget][CefManager] Shutting down CEF OSR runtime...";
    // Flush pending message loop work so browser cleanup tasks complete before shutdown
    for (int i = 0; i < 10; ++i) {
        CefDoMessageLoopWork();
    }
    CefShutdown();
    s_cefApp = nullptr;

    const QString pidCacheDir = QDir(AppPaths::userLocalConfigDir())
        .filePath(QStringLiteral("LinWidgets/cef-cache-pid-%1").arg(QCoreApplication::applicationPid()));
    if (QDir(pidCacheDir).exists() && !isCefCacheDirectoryLocked(pidCacheDir)) {
        QDir(pidCacheDir).removeRecursively();
    }

#ifdef Q_OS_WIN
    if (s_hPrimaryCacheMutex) {
        ReleaseMutex(s_hPrimaryCacheMutex);
        CloseHandle(s_hPrimaryCacheMutex);
        s_hPrimaryCacheMutex = nullptr;
    }
#endif
#endif

    m_initialized = false;
    emit cefShutdown();
}

void CefManager::scheduleMessagePumpWork(qint64 delayMs)
{
    Q_UNUSED(delayMs);
}

void CefManager::startMessageLoopPump()
{
    if (!m_pumpWorkTimer) {
        m_pumpWorkTimer = new QTimer(this);
        m_pumpWorkTimer->setInterval(10); // 100Hz cooperative dispatch
        connect(m_pumpWorkTimer, &QTimer::timeout, this, []() {
#ifdef LINDESK_HAS_CEF
            if (CefManager::instance() && CefManager::instance()->isInitialized()) {
                CefDoMessageLoopWork();
            }
#endif
        });
    }
    m_pumpWorkTimer->start();
}

void CefManager::stopMessageLoopPump()
{
    if (m_pumpWorkTimer) {
        m_pumpWorkTimer->stop();
        m_pumpWorkTimer->deleteLater();
        m_pumpWorkTimer = nullptr;
    }
    if (m_idleWatchdogTimer) {
        m_idleWatchdogTimer->stop();
        m_idleWatchdogTimer->deleteLater();
        m_idleWatchdogTimer = nullptr;
    }
}

} // namespace LinCef
