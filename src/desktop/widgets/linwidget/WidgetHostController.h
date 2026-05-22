#pragma once

#include "desktop/widgets/linwidget/LinWidgetTypes.h"

#include <QObject>
#include <QProcess>

namespace LinWidget {

class WidgetHostController : public QObject {
    Q_OBJECT

public:
    explicit WidgetHostController(WidgetInstance instance,
                                  QString ipcServerName,
                                  quintptr parentWindowId = 0,
                                  QObject* parent = nullptr);
    ~WidgetHostController() override;

    const WidgetInstance& instance() const { return m_instance; }
    QString widgetId() const { return m_instance.instanceId; }
    bool start();
    void stop();
    void restart();
    void setGeometry(const QRect& geometry);
    void markHeartbeat();
    bool heartbeatTimedOut(qint64 nowMs, qint64 timeoutMs) const;

signals:
    void exited(const QString& widgetId, int exitCode, QProcess::ExitStatus status);
    void failed(const QString& widgetId, const QString& errorMessage);

private:
    QString hostExecutablePath() const;
    QStringList hostArguments() const;

    WidgetInstance m_instance;
    QString m_ipcServerName;
    quintptr m_parentWindowId = 0;
    QProcess* m_process = nullptr;
    qint64 m_lastHeartbeatMs = 0;
};

}
