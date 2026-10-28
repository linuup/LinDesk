#include "services/SystemApi/ShellMenuCoordinator.h"
#include "desktop/RightMenu.h"
#include <QDebug>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

ShellMenuCoordinator& ShellMenuCoordinator::instance()
{
    static ShellMenuCoordinator s_instance;
    return s_instance;
}

ShellMenuCoordinator::ShellMenuCoordinator(QObject* parent)
    : QObject(parent)
{
    m_worker = std::make_unique<ShellMenuWorker>(this);
}

ShellMenuCoordinator::~ShellMenuCoordinator()
{
    if (m_worker) {
        m_worker->stopWorker();
    }
}

void ShellMenuCoordinator::initialize()
{
    if (m_worker && !m_worker->isWorkerRunning()) {
        m_worker->startWorker();
    }

    // 在应用空闲时轻量预热背景菜单数据
    QTimer::singleShot(500, this, [this]() {
        prewarmBackgroundSnapshots();
    });
}

void ShellMenuCoordinator::prewarmBackgroundSnapshots()
{
    RightMenu* r = RightMenu::getInstance();
    if (r) {
        r->filteredDesktopCustomMenuItems();
        r->filteredSharedBoxCustomMenuItems();
    }
    m_desktopSnapshotReady = true;
    m_boxSnapshotReady = true;
}

void ShellMenuCoordinator::prefetchItemMenu(const QString& filePath, HWND ownerHwnd)
{
    if (filePath.isEmpty() || !m_worker) {
        return;
    }
    if (!m_worker->isWorkerRunning()) {
        m_worker->startWorker();
    }
    if (!m_worker->isWorkerRunning()) {
        return;
    }
    if (m_prefetchedPath == filePath) {
        return;
    }

    m_prefetchedPath = filePath;
    ShellMenuRequest req;
    static std::atomic<uint64_t> s_prefetchCounter{900000};
    req.requestId = s_prefetchCounter.fetch_add(1);
    req.targetPath = filePath;
    req.ownerHwnd = ownerHwnd;
    req.showMenu = false;
    if (filePath.endsWith(QStringLiteral(".lnk"), Qt::CaseInsensitive)) {
        req.targetType = ShellTargetType::Shortcut;
    } else {
        req.targetType = ShellTargetType::File;
    }
    m_worker->postRequest(req);
}

bool ShellMenuCoordinator::requestMenu(const ShellMenuRequest& request, QWidget* parentWidget)
{
    Q_UNUSED(parentWidget);
    RightMenu* r = RightMenu::getInstance();
    if (!r) {
        return false;
    }

    if (request.targetType == ShellTargetType::DesktopBackground) {
        r->setShowType(ObjectType::BackGround);
        r->showMenu(request.globalPos, DesktopRootItemModel{}, request.extendedVerbs);
        return true;
    }

    if (m_worker) {
        if (!m_worker->isWorkerRunning()) {
            m_worker->startWorker();
        }
        if (m_worker->isWorkerRunning()) {
            m_worker->postRequest(request);
            return true;
        }
    }

    return false;
}