#pragma once

#include <QObject>
#include <QTimer>
#include <QString>

namespace LinPower {

// 动态壁纸能效状态机
//
// 暂停（WasHidden + 1 FPS）只在“用户确实看不到壁纸”时触发：
//   1. 系统处于 D3D 独占全屏 / 演示模式（SHQueryUserNotificationState）；
//   2. 工作站已锁定或输入桌面不是 Default（锁屏、UAC 安全桌面）；
//   3. 壁纸所在显示器的工作区被其他进程的可见窗口（最大化 / 全屏 / 拼接平铺）几乎完全覆盖。
//
// 以下情况一律不暂停：
//   - 前台窗口属于 LinDesk 自身进程（在桌面框选、拖拽、重命名、打开 Hub / 设置等）；
//   - 前台为桌面宿主（Progman / WorkerW）或任务栏、开始菜单、通知中心等 Shell 浮层；
//   - 被最小化、被 DWM Cloak（其他虚拟桌面 / UWP 挂起）、点击穿透的透明覆盖层窗口。
class WallpaperPowerStateManager : public QObject {
    Q_OBJECT

public:
    static WallpaperPowerStateManager& instance();

    void start();
    void stop();

    bool isSuspended() const { return m_isSuspended; }
    int currentFramerate() const { return m_currentFps; }
    QString lastReason() const { return m_lastReason; }

signals:
    void wallpaperSuspensionChanged(bool suspended, const QString& reason);
    void wallpaperFramerateChanged(int fps, const QString& reason);

private slots:
    void evaluateState();

private:
    explicit WallpaperPowerStateManager(QObject* parent = nullptr);
    ~WallpaperPowerStateManager() override = default;
    WallpaperPowerStateManager(const WallpaperPowerStateManager&) = delete;
    WallpaperPowerStateManager& operator=(const WallpaperPowerStateManager&) = delete;

    // 返回非空字符串表示应当暂停，内容为原因
    QString detectSuspendReason() const;
    void applyFramerate();

    QTimer* m_checkTimer = nullptr;
    bool m_isSuspended = false;
    int m_currentFps = 0;
    int m_consecutiveHiddenCount = 0;
    QString m_lastReason;
};

} // namespace LinPower
