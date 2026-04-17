#pragma once
#include "monitor/IRegistryListenerInterface.h"
#include <QMap>
#include <QMutex>
#include <QVariant>
#include <atomic>
#include <thread>
#include <QWinEventNotifier>

class DesktopInfoMonitor : public IRegistryListener {
    Q_OBJECT

public:
    struct DesktopIcon {
        QString clsid;
        QString name;
        bool isHidden;
        QVariant value;

        DesktopIcon() : isHidden(false) {}
        DesktopIcon(const QString& id, const QString& n) : clsid(id), name(n), isHidden(false) {}
    };

    DesktopInfoMonitor(QObject* parent = nullptr);
    virtual ~DesktopInfoMonitor();

    bool startListening() override;
    void stopListening() override;

    void watchKey(HKEY hive, const QString& keyPath, bool watchSubkeys = false) override;
    void unwatchKey(HKEY hive, const QString& keyPath) override;
    QList<QPair<HKEY, QString>> getWatchedKeys() const override;

    QVariant getRegistryValue(HKEY hive, const QString& keyPath, const QString& valueName) override;
    bool setRegistryValue(HKEY hive, const QString& keyPath, const QString& valueName, const QVariant& value) override;
    bool deleteRegistryValue(HKEY hive, const QString& keyPath, const QString& valueName) override;

    QList<DesktopIcon> getCurrentIcons() const;
    bool setIconVisibility(const QString& clsid, bool hidden);
    bool isIconHidden(const QString& clsid) const;
    QString getIconName(const QString& clsid) const;

    static QMap<QString, QString> getKnownDesktopIcons();
    void refreshIconsState(); // Force fresh registry read

signals:
    void iconVisibilityChanged(const QString& clsid, bool hidden, const QString& iconName);
    void desktopIconsRefreshed(const QList<DesktopIcon>& icons);
    void wallpaperChanged();

private slots:
    void processRegistryChanges();
    void onWallpaperKeyChanged();

private:
    void startRegistryMonitoring();
    void stopRegistryMonitoring();
    void registryMonitoringWorker();

    void setupWallpaperMonitoring();
    void cleanupWallpaperMonitoring();

    HKEY openRegistryKey(HKEY hive, const QString& keyPath, REGSAM access);
    void closeRegistryKey(HKEY key);
    QVariant readRegistryValue(HKEY key, const QString& valueName);
    bool writeRegistryValue(HKEY key, const QString& valueName, const QVariant& value);
    bool deleteRegistryValue(HKEY key, const QString& valueName);

    // void refreshIconsState(); // Already moved to public

    // Desktop Icons (Thread-based)
    std::thread m_monitorThread;
    std::atomic<bool> m_monitoring{ false };
    HANDLE m_registryEvent{ nullptr };
    std::atomic<bool> m_listening{ false };

    HKEY m_currentHive;
    QString m_currentKeyPath;
    bool m_watchSubkeys{ false };

    QMap<QString, DesktopIcon> m_currentIcons;
    QMap<QString, QString> m_knownIcons;

    // Wallpaper (Event-based)
    HKEY m_wallpaperKey = nullptr;
    HANDLE m_wallpaperEvent = nullptr;
    QWinEventNotifier* m_wallpaperNotifier = nullptr;

    mutable QMutex m_mutex;
};
