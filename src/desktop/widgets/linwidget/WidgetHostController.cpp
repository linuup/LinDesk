#include "desktop/widgets/linwidget/WidgetHostController.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>

namespace LinWidget {

WidgetHostController::WidgetHostController(WidgetInstance instance,
                                           QString ipcServerName,
                                           quintptr parentWindowId,
                                           QObject* parent)
    : QObject(parent)
    , m_instance(std::move(instance))
    , m_ipcServerName(std::move(ipcServerName))
    , m_parentWindowId(parentWindowId)
    , m_process(new QProcess(this))
{
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus status) {
        emit exited(m_instance.instanceId, exitCode, status);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        emit failed(m_instance.instanceId, m_process->errorString());
    });
    markHeartbeat();
}

WidgetHostController::~WidgetHostController()
{
    stop();
}

bool WidgetHostController::start()
{
    if (m_process->state() != QProcess::NotRunning) {
        return true;
    }

    m_process->setProgram(hostExecutablePath());
    m_process->setArguments(hostArguments());
    m_process->setWorkingDirectory(QFileInfo(hostExecutablePath()).absolutePath());
    m_process->start();
    markHeartbeat();
    return m_process->waitForStarted(1500);
}

void WidgetHostController::stop()
{
    if (!m_process || m_process->state() == QProcess::NotRunning) {
        return;
    }

    m_process->terminate();
    if (!m_process->waitForFinished(1200)) {
        m_process->kill();
        m_process->waitForFinished(1200);
    }
}

void WidgetHostController::restart()
{
    stop();
    start();
}

void WidgetHostController::setGeometry(const QRect& geometry)
{
    if (geometry.isValid()) {
        m_instance.geometry = geometry;
    }
}

void WidgetHostController::markHeartbeat()
{
    m_lastHeartbeatMs = QDateTime::currentMSecsSinceEpoch();
}

bool WidgetHostController::heartbeatTimedOut(qint64 nowMs, qint64 timeoutMs) const
{
    return m_process &&
        m_process->state() != QProcess::NotRunning &&
        nowMs - m_lastHeartbeatMs > timeoutMs;
}

QString WidgetHostController::hostExecutablePath() const
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("WidgetHost.exe"));
}

QStringList WidgetHostController::hostArguments() const
{
    QStringList args;
    args << QStringLiteral("--widget-id") << m_instance.instanceId
         << QStringLiteral("--package") << QDir::toNativeSeparators(m_instance.packagePath)
         << QStringLiteral("--ipc") << m_ipcServerName
         << QStringLiteral("--parent-pid") << QString::number(QCoreApplication::applicationPid())
         << QStringLiteral("--x") << QString::number(m_instance.geometry.x())
         << QStringLiteral("--y") << QString::number(m_instance.geometry.y())
         << QStringLiteral("--width") << QString::number(m_instance.geometry.width())
         << QStringLiteral("--height") << QString::number(m_instance.geometry.height());
    if (!m_instance.grantedPermissions.isEmpty()) {
        args << QStringLiteral("--permissions") << m_instance.grantedPermissions.join(QStringLiteral(","));
    }
    if (m_instance.isDevelopment) {
        args << QStringLiteral("--development");
    }
    if (m_instance.autoReload) {
        args << QStringLiteral("--auto-reload");
    }
    if (m_instance.instanceId == QStringLiteral("__shared__")) {
        args << QStringLiteral("--shared-runtime");
    }
    if (m_parentWindowId != 0) {
        args << QStringLiteral("--parent-hwnd") << QString::number(static_cast<qulonglong>(m_parentWindowId));
    }
    return args;
}

}
