#pragma once
#include <windows.h>
#include <QTimer>
#include <QMap>
#include <QObject>
#include <QVariant>
#include <QDateTime>
#include "monitor/IListenerInterface.h"
class IRegistryListener : public IListenerInterface {
    Q_OBJECT

public:
    // 注册表事件类型
    enum class RegistryEventType {
        KeyCreated,      // 键创建
        KeyDeleted,      // 键删除
        ValueChanged,    // 值改变
        ValueAdded,      // 值添加
        ValueRemoved     // 值删除
    };

    // 注册表事件结构
    struct RegistryEvent {
        RegistryEventType type;
        HKEY hive;
        QString keyPath;
        QString valueName;
        QVariant oldValue;
        QVariant newValue;
        QDateTime timestamp;

        RegistryEvent(RegistryEventType t, HKEY h, const QString& path, const QString& name = "")
            : type(t), hive(h), keyPath(path), valueName(name), timestamp(QDateTime::currentDateTime()) {}
    };

    IRegistryListener(const QString& name = "Registry", QObject* parent = nullptr)
        : IListenerInterface(name, "Registry", parent) {}

    // ========== 必须实现的纯虚函数 ==========

    // 监听控制
    virtual bool startListening() override = 0;
    virtual void stopListening() override = 0;

    // 注册表键监控
    virtual void watchKey(HKEY hive, const QString& keyPath, bool watchSubkeys = false) = 0;
    virtual void unwatchKey(HKEY hive, const QString& keyPath) = 0;
    virtual QList<QPair<HKEY, QString>> getWatchedKeys() const = 0;

    // 注册表值操作
    virtual QVariant getRegistryValue(HKEY hive, const QString& keyPath, const QString& valueName) = 0;
    virtual bool setRegistryValue(HKEY hive, const QString& keyPath, const QString& valueName, const QVariant& value) = 0;
    virtual bool deleteRegistryValue(HKEY hive, const QString& keyPath, const QString& valueName) = 0;

    // 检查注册表键是否存在
    virtual bool keyExists(HKEY hive, const QString& keyPath) {
        HKEY hKey;
        LONG result = RegOpenKeyEx(hive, reinterpret_cast<LPCWSTR>(keyPath.utf16()), 0, KEY_READ, &hKey);
        if (result == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return true;
        }
        return false;
    }

    // 创建注册表键
    virtual bool createKey(HKEY hive, const QString& keyPath) {
        HKEY hKey;
        DWORD disposition;
        LONG result = RegCreateKeyEx(hive, reinterpret_cast<LPCWSTR>(keyPath.utf16()),
            0, nullptr, 0, KEY_WRITE, nullptr, &hKey, &disposition);
        if (result == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return true;
        }
        return false;
    }

    // 删除注册表键
    virtual bool deleteKey(HKEY hive, const QString& keyPath) {
        return RegDeleteKey(hive, reinterpret_cast<LPCWSTR>(keyPath.utf16())) == ERROR_SUCCESS;
    }

signals:
    // 注册表变化信号
    void registryKeyChanged(HKEY hive, const QString& keyPath);
    void registryValueChanged(HKEY hive, const QString& keyPath, const QString& valueName,
        const QVariant& oldValue, const QVariant& newValue);
    void registryKeyAdded(HKEY hive, const QString& keyPath);
    void registryKeyDeleted(HKEY hive, const QString& keyPath);
    void registryErrorOccurred(const QString& errorMessage);
};