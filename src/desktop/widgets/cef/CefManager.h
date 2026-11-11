#pragma once

#include <QObject>
#include <QString>

class QTimer;

namespace LinCef {

class CefManager : public QObject {
    Q_OBJECT

public:
    static CefManager* instance();

    bool initialize(QString* errorMessage = nullptr);
    void shutdown();

    bool isInitialized() const { return m_initialized; }
    bool isCefAvailable() const;
    void doMessageLoopWork();

    QString subprocessExecutablePath() const;
    QString resourcesDirectoryPath() const;
    QString localesDirectoryPath() const;
    QString cacheStoragePath() const;

public slots:
    void scheduleMessagePumpWork(qint64 delayMs);

signals:
    void cefInitialized();
    void cefShutdown();

private:
    explicit CefManager(QObject* parent = nullptr);
    ~CefManager() override;

    void startMessageLoopPump();
    void stopMessageLoopPump();

    static CefManager* s_instance;
    bool m_initialized = false;
    QTimer* m_pumpWorkTimer = nullptr;
    QTimer* m_idleWatchdogTimer = nullptr;
};

} // namespace LinCef
