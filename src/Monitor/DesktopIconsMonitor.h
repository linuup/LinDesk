#pragma once
#include "IRegistryListenerInterface.h"
#include <QMap>
#include <QMutex>
#include <QVariant>
#include <atomic>
#include <thread>

class DesktopIconsMonitor : public IRegistryListener {
    Q_OBJECT

public:
    // 桌面图标信息结构
    struct DesktopIcon {
        QString clsid;      // 类ID
        QString name;       // 显示名称
        bool isHidden;      // 是否隐藏
        QVariant value;     // 注册表值

        DesktopIcon() : isHidden(false) {}
        DesktopIcon(const QString& id, const QString& n) : clsid(id), name(n), isHidden(false) {}
    };

    DesktopIconsMonitor(QObject* parent = nullptr);
    virtual ~DesktopIconsMonitor();
    // ========== IListenerInterface 接口实现 ==========
    bool startListening() override;
    void stopListening() override;

    // ========== IRegistryListener 接口实现 ==========
    void watchKey(HKEY hive, const QString& keyPath, bool watchSubkeys = false) override;
    void unwatchKey(HKEY hive, const QString& keyPath) override;
    QList<QPair<HKEY, QString>> getWatchedKeys() const override;

    QVariant getRegistryValue(HKEY hive, const QString& keyPath, const QString& valueName) override;
    bool setRegistryValue(HKEY hive, const QString& keyPath, const QString& valueName, const QVariant& value) override;
    bool deleteRegistryValue(HKEY hive, const QString& keyPath, const QString& valueName) override;


    // ========== 桌面图标特定功能 ==========
    QList<DesktopIcon> getCurrentIcons() const;
    bool setIconVisibility(const QString& clsid, bool hidden);
    bool isIconHidden(const QString& clsid) const;
    QString getIconName(const QString& clsid) const;

    // 获取已知的桌面图标
    static QMap<QString, QString> getKnownDesktopIcons();

signals:
    // 桌面图标特定信号
    void iconVisibilityChanged(const QString& clsid, bool hidden, const QString& iconName);
    void desktopIconsRefreshed(const QList<DesktopIcon>& icons);

private slots:
    void processRegistryChanges();

private:
    // Windows API 监听相关
    void startRegistryMonitoring();
    void stopRegistryMonitoring();
    void registryMonitoringWorker();

    // 注册表操作辅助方法
    HKEY openRegistryKey(HKEY hive, const QString& keyPath, REGSAM access);
    void closeRegistryKey(HKEY key);
    QVariant readRegistryValue(HKEY key, const QString& valueName);
    bool writeRegistryValue(HKEY key, const QString& valueName, const QVariant& value);
    bool deleteRegistryValue(HKEY key, const QString& valueName);

    // 图标状态管理
    void refreshIconsState();

    // 成员变量
    std::thread m_monitorThread;
    std::atomic<bool> m_monitoring{ false };
    HANDLE m_registryEvent{ nullptr };
    std::atomic<bool> m_listening{ false };

    HKEY m_currentHive;
    QString m_currentKeyPath;
    bool m_watchSubkeys{ false };

    QMap<QString, DesktopIcon> m_currentIcons;
    QMap<QString, QString> m_knownIcons;

    mutable QMutex m_mutex;
};