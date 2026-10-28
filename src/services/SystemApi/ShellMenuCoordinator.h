#pragma once

#include <QObject>
#include <QPoint>
#include <QString>
#include <QVector>
#include <QPointer>
#include <QWidget>
#include <memory>
#include <functional>
#include "services/SystemApi/ShellMenuTypes.h"
#include "services/SystemApi/ShellContextMenu.h"
#include "services/SystemApi/ShellMenuWorker.h"

class ShellMenuCoordinator : public QObject
{
    Q_OBJECT
public:
    static ShellMenuCoordinator& instance();

    // 初始化并预热后台快照缓存与 STA 工作线程
    void initialize();

    // 预热桌面背景与收纳盒菜单快照（后台或轻量启动）
    void prewarmBackgroundSnapshots();

    // 请求展示右键菜单（支持所有 Target 类型，毫秒级快速分发）
    bool requestMenu(const ShellMenuRequest& request, QWidget* parentWidget);

    // 预取图标上下文菜单（当鼠标 hover 或按下右键准备阶段提前解析）
    void prefetchItemMenu(const QString& filePath, HWND ownerHwnd);

    // 检查是否有预热好的桌面背景菜单数据
    bool hasPrewarmedDesktopSnapshot() const { return m_desktopSnapshotReady; }

    ShellMenuWorker* worker() const { return m_worker.get(); }

private:
    explicit ShellMenuCoordinator(QObject* parent = nullptr);
    ~ShellMenuCoordinator() override;

    std::unique_ptr<ShellMenuWorker> m_worker;
    bool m_desktopSnapshotReady = false;
    bool m_boxSnapshotReady = false;
    QString m_prefetchedPath;
    wil::com_ptr_nothrow<IContextMenu3> m_prefetchedContextMenu;
};
