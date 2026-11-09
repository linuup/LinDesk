#include "WallpaperPowerStateManager.h"
#include <QDebug>
#include <QRegion>
#include <QRect>
#include "desktop/widgets/runtime_export.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#endif

namespace LinPower {

namespace {

constexpr int kSampleIntervalMs = 500;
// 连续 N 次判定“不可见”才暂停（N * 500ms），恢复则立即生效
constexpr int kSuspendDebounceSamples = 2;
// 工作区剩余可见面积低于该比例即视为被完全遮挡
constexpr double kVisibleAreaThreshold = 0.03;

#ifdef Q_OS_WIN

bool isShellOrTransientClass(const wchar_t* cls)
{
    static const wchar_t* const kIgnored[] = {
        L"Shell_TrayWnd",
        L"Shell_SecondaryTrayWnd",
        L"Windows.UI.Core.CoreWindow",          // 开始菜单 / 搜索 / 通知中心
        L"NotifyIconOverflowWindow",
        L"TopLevelWindowForOverflowXamlIsland",
        L"XamlExplorerHostIslandWindow",        // Win11 任务切换 / 小组件浮层
        L"ForegroundStaging",
        L"MultitaskingViewFrame",
        L"TaskListThumbnailWnd",
        L"Xaml_WindowedPopupClass",
        L"tooltips_class32",
        L"SysShadow",
        L"IME",
        L"MSCTFIME UI",
        L"#32768",                              // 系统菜单
    };
    for (const wchar_t* name : kIgnored) {
        if (_wcsicmp(cls, name) == 0) {
            return true;
        }
    }
    return false;
}

bool isDesktopHostClass(const wchar_t* cls)
{
    return _wcsicmp(cls, L"Progman") == 0 || _wcsicmp(cls, L"WorkerW") == 0;
}

bool isOwnProcessWindow(HWND hwnd)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid == GetCurrentProcessId();
}

bool isCloaked(HWND hwnd)
{
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))) {
        return cloaked != 0;
    }
    return false;
}

QRect visibleWindowRect(HWND hwnd)
{
    RECT rc = {};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rc, sizeof(rc)))) {
        if (!GetWindowRect(hwnd, &rc)) {
            return QRect();
        }
    }
    return QRect(QPoint(rc.left, rc.top), QPoint(rc.right - 1, rc.bottom - 1));
}

qint64 regionArea(const QRegion& region)
{
    qint64 area = 0;
    for (const QRect& r : region) {
        area += static_cast<qint64>(r.width()) * r.height();
    }
    return area;
}

struct OcclusionContext {
    QRegion uncovered;
    qint64 totalArea = 0;
    qint64 thresholdArea = 0;
    QString topOccluder;
    int processed = 0;
};

BOOL CALLBACK occlusionEnumProc(HWND hwnd, LPARAM lParam)
{
    auto* ctx = reinterpret_cast<OcclusionContext*>(lParam);

    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) {
        return TRUE;
    }

    wchar_t cls[128] = {0};
    GetClassNameW(hwnd, cls, 127);

    // EnumWindows 按 Z 序自顶向下枚举；到达桌面宿主后，其下方窗口均在壁纸背后
    if (isDesktopHostClass(cls)) {
        return FALSE;
    }

    if (isShellOrTransientClass(cls) || isOwnProcessWindow(hwnd) || isCloaked(hwnd)) {
        return TRUE;
    }

    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    // 点击穿透的透明覆盖层（FPS 计数器、录屏指示器、桌面歌词等）
    if ((exStyle & WS_EX_TRANSPARENT) && (exStyle & WS_EX_LAYERED)) {
        return TRUE;
    }
    // 不可激活的工具浮窗（游戏覆盖层、截图工具条等）
    if ((exStyle & WS_EX_TOOLWINDOW) && (exStyle & WS_EX_NOACTIVATE)) {
        return TRUE;
    }
    // 半透明分层窗口不视为遮挡
    if (exStyle & WS_EX_LAYERED) {
        BYTE alpha = 255;
        DWORD flags = 0;
        if (GetLayeredWindowAttributes(hwnd, nullptr, &alpha, &flags) && (flags & LWA_ALPHA) && alpha < 220) {
            return TRUE;
        }
    }

    const QRect rect = visibleWindowRect(hwnd);
    if (rect.isEmpty() || !ctx->uncovered.intersects(rect)) {
        return TRUE;
    }

    ctx->uncovered = ctx->uncovered.subtracted(QRegion(rect));
    if (ctx->topOccluder.isEmpty()) {
        ctx->topOccluder = QString::fromWCharArray(cls);
    }

    if (regionArea(ctx->uncovered) <= ctx->thresholdArea) {
        return FALSE;
    }
    // 防御：极端情况下限制处理数量
    return ++ctx->processed < 256 ? TRUE : FALSE;
}

bool isInputDesktopLocked()
{
    HDESK desk = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!desk) {
        // 锁屏（Winlogon 桌面）或 UAC 安全桌面时无权访问
        return true;
    }
    wchar_t name[64] = {0};
    DWORD needed = 0;
    const bool ok = GetUserObjectInformationW(desk, UOI_NAME, name, sizeof(name), &needed) != FALSE;
    CloseDesktop(desk);
    return ok && _wcsicmp(name, L"Default") != 0;
}

#endif // Q_OS_WIN

} // namespace

WallpaperPowerStateManager& WallpaperPowerStateManager::instance() {
    static WallpaperPowerStateManager s_mgr;
    return s_mgr;
}

WallpaperPowerStateManager::WallpaperPowerStateManager(QObject* parent)
    : QObject(parent)
{
    m_checkTimer = new QTimer(this);
    m_checkTimer->setInterval(kSampleIntervalMs);
    connect(m_checkTimer, &QTimer::timeout, this, &WallpaperPowerStateManager::evaluateState);
}

void WallpaperPowerStateManager::start() {
    if (!m_checkTimer->isActive()) {
        m_checkTimer->start();
    }
}

void WallpaperPowerStateManager::stop() {
    m_checkTimer->stop();
    m_consecutiveHiddenCount = 0;
    if (m_isSuspended) {
        m_isSuspended = false;
        LinWidgetRuntimeBridge::instance().setWallpaperSuspended(false);
    }
}

QString WallpaperPowerStateManager::detectSuspendReason() const
{
#ifdef Q_OS_WIN
    // 1. 用户正在与 LinDesk 自身交互（桌面框选、拖拽、重命名、Hub、设置）→ 壁纸必然可见
    HWND fg = GetForegroundWindow();
    if (fg && isOwnProcessWindow(fg)) {
        return QString();
    }
    if (fg) {
        wchar_t fgCls[128] = {0};
        GetClassNameW(fg, fgCls, 127);
        if (isDesktopHostClass(fgCls)) {
            return QString();
        }
    }

    // 2. 锁屏 / 安全桌面
    if (isInputDesktopLocked()) {
        return QStringLiteral("Workstation locked / secure desktop");
    }

    // 3. D3D 独占全屏游戏 / 演示模式
    QUERY_USER_NOTIFICATION_STATE quns;
    if (SHQueryUserNotificationState(&quns) == S_OK) {
        if (quns == QUNS_RUNNING_D3D_FULL_SCREEN) {
            return QStringLiteral("D3D exclusive fullscreen");
        }
        if (quns == QUNS_PRESENTATION_MODE) {
            return QStringLiteral("Presentation mode");
        }
    }

    // 4. 壁纸所在主显示器工作区是否被其他应用窗口几乎完全覆盖
    const POINT origin = {0, 0};
    HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!monitor || !GetMonitorInfoW(monitor, &mi)) {
        return QString();
    }
    const QRect work(QPoint(mi.rcWork.left, mi.rcWork.top), QPoint(mi.rcWork.right - 1, mi.rcWork.bottom - 1));
    if (work.isEmpty()) {
        return QString();
    }

    OcclusionContext ctx;
    ctx.uncovered = QRegion(work);
    ctx.totalArea = static_cast<qint64>(work.width()) * work.height();
    ctx.thresholdArea = static_cast<qint64>(ctx.totalArea * kVisibleAreaThreshold);
    EnumWindows(occlusionEnumProc, reinterpret_cast<LPARAM>(&ctx));

    if (regionArea(ctx.uncovered) <= ctx.thresholdArea) {
        return QStringLiteral("Desktop fully covered by other windows (%1)").arg(ctx.topOccluder);
    }
#endif
    return QString();
}

void WallpaperPowerStateManager::applyFramerate()
{
#ifdef Q_OS_WIN
    // 电源感知：电池供电 30 FPS，接通电源 60 FPS
    int targetFps = 60;
    SYSTEM_POWER_STATUS sps;
    const bool onBattery = GetSystemPowerStatus(&sps) && sps.ACLineStatus == 0;
    if (onBattery) {
        targetFps = 30;
    }
    if (targetFps != m_currentFps) {
        m_currentFps = targetFps;
        LinWidgetRuntimeBridge::instance().setWallpaperFramerate(m_currentFps);
        emit wallpaperFramerateChanged(m_currentFps,
            onBattery ? QStringLiteral("Battery (30 FPS)") : QStringLiteral("AC power (60 FPS)"));
    }
#endif
}

void WallpaperPowerStateManager::evaluateState() {
    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (!bridge.isLoaded() || !bridge.hasActiveWallpaper()) {
        m_consecutiveHiddenCount = 0;
        // 下次启动壁纸时重新下发帧率
        m_currentFps = 0;
        return;
    }

    applyFramerate();

    const QString reason = detectSuspendReason();
    const bool hidden = !reason.isEmpty();

    bool shouldSuspend = m_isSuspended;
    if (hidden) {
        if (++m_consecutiveHiddenCount >= kSuspendDebounceSamples) {
            shouldSuspend = true;
        }
    } else {
        m_consecutiveHiddenCount = 0;
        shouldSuspend = false; // 只要有一次可见立即恢复，绝不让用户看到静止壁纸
    }

    if (shouldSuspend != m_isSuspended) {
        m_isSuspended = shouldSuspend;
        m_lastReason = shouldSuspend ? reason : QStringLiteral("Desktop visible");
        bridge.setWallpaperSuspended(m_isSuspended);
        qInfo() << "[WallpaperPower]" << (m_isSuspended ? "suspend:" : "resume:") << m_lastReason;
        emit wallpaperSuspensionChanged(m_isSuspended, m_lastReason);
    }
}

} // namespace LinPower
