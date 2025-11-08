// DesktopIconsMonitor.cpp
#include "DesktopIconsMonitor.h"
#include <QDebug>
#include <QTimer>
#include <QCoreApplication>
#include <stdexcept>

// 已知的桌面图标CLSID
static const QMap<QString, QString> KNOWN_DESKTOP_ICONS = {
    {"{645FF040-5081-101B-9F08-00AA002F954E}", "RECYCLE_BIN"},
    {"{20D04FE0-3AEA-1069-A2D8-08002B30309D}", "MY_COMPUTER"},
    {"{5399E694-6CE5-4D6C-8FCE-1D8870FDCBA0}", "CONTROL_PANEL"},
    {"{F02C1A0D-BE21-4350-88B0-7367FC96EF3C}", "NETWORK"},
    {"{59031a47-3f72-44a7-89c5-5595fe6b30ee}", "USER_FOLDER"},
    {"{018D5C66-4533-4307-9B53-224DE2ED1FE6}", "OneDrive"}
};
DesktopIconsMonitor::DesktopIconsMonitor(QObject* parent)
    : IRegistryListener("DesktopIconsMonitor", parent)
    , m_currentHive(HKEY_CURRENT_USER)
    , m_currentKeyPath("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\NewStartPanel")
    , m_watchSubkeys(false)
{
    m_knownIcons = KNOWN_DESKTOP_ICONS;
    logMessage("DesktopIconsMonitor created");
}

DesktopIconsMonitor::~DesktopIconsMonitor() {
    stopListening();
    logMessage("DesktopIconsMonitor destroyed");
}

// ========== IListenerInterface 接口实现 ==========

bool DesktopIconsMonitor::startListening() {
    if (isListening()) {
        logMessage("Already listening to desktop icons");
        return true;
    }

    if (m_currentKeyPath.isEmpty()) {
        emitError("No registry key to watch");
        return false;
    }

    try {
        // 刷新图标状态
        refreshIconsState();

        // 启动Windows API监听
        startRegistryMonitoring();

        m_listening = true;
        logMessage("Started Windows API registry monitoring");
        return true;
    }
    catch (const std::exception& e) {
        emitError(QString("Failed to start monitoring: %1").arg(e.what()));
        return false;
    }
}

void DesktopIconsMonitor::stopListening() {
    if (!isListening()) return;

    // 停止Windows API监听
    stopRegistryMonitoring();

    m_listening = false;
    logMessage("Stopped Windows API registry monitoring");
}


// ========== IRegistryListener 接口实现 ==========

void DesktopIconsMonitor::watchKey(HKEY hive, const QString& keyPath, bool watchSubkeys) {
    QMutexLocker locker(&m_mutex);

    m_currentHive = hive;
    m_currentKeyPath = keyPath;
    m_watchSubkeys = watchSubkeys;

    // 将HKEY转换为可读字符串
    QString hiveStr;
    if (hive == HKEY_CURRENT_USER) {
        hiveStr = "HKEY_CURRENT_USER";
    }
    else if (hive == HKEY_LOCAL_MACHINE) {
        hiveStr = "HKEY_LOCAL_MACHINE";
    }
    else if (hive == HKEY_CLASSES_ROOT) {
        hiveStr = "HKEY_CLASSES_ROOT";
    }
    else if (hive == HKEY_USERS) {
        hiveStr = "HKEY_USERS";
    }
    else if (hive == HKEY_CURRENT_CONFIG) {
        hiveStr = "HKEY_CURRENT_CONFIG";
    }
    else {
        hiveStr = QString("HKEY_0x%1").arg(reinterpret_cast<uintptr_t>(hive), 0, 16);
    }

    logMessage(QString("Watching registry key: %1\\%2").arg(hiveStr).arg(keyPath));
}

void DesktopIconsMonitor::unwatchKey(HKEY hive, const QString& keyPath) {
    QMutexLocker locker(&m_mutex);

    if (hive == m_currentHive && keyPath == m_currentKeyPath) {
        stopListening();
        m_currentKeyPath.clear();
        logMessage("Stopped watching registry key");
    }
}

QList<QPair<HKEY, QString>> DesktopIconsMonitor::getWatchedKeys() const {
    QMutexLocker locker(&m_mutex);

    QList<QPair<HKEY, QString>> result;
    if (!m_currentKeyPath.isEmpty()) {
        result.append(qMakePair(m_currentHive, m_currentKeyPath));
    }
    return result;
}

QVariant DesktopIconsMonitor::getRegistryValue(HKEY hive, const QString& keyPath, const QString& valueName) {
    HKEY hKey = openRegistryKey(hive, keyPath, KEY_READ);
    if (!hKey) {
        return QVariant();
    }

    QVariant value = readRegistryValue(hKey, valueName);
    closeRegistryKey(hKey);

    return value;
}

bool DesktopIconsMonitor::setRegistryValue(HKEY hive, const QString& keyPath,
    const QString& valueName, const QVariant& value) {
    HKEY hKey = openRegistryKey(hive, keyPath, KEY_WRITE);
    if (!hKey) {
        return false;
    }

    bool success = writeRegistryValue(hKey, valueName, value);
    closeRegistryKey(hKey);

    if (success) {
        logMessage(QString("Set registry value: %1\\%2 = %3").arg(keyPath).arg(valueName).arg(value.toString()));
    }

    return success;
}

bool DesktopIconsMonitor::deleteRegistryValue(HKEY hive, const QString& keyPath, const QString& valueName) {
    HKEY hKey = openRegistryKey(hive, keyPath, KEY_WRITE);
    if (!hKey) {
        return false;
    }

    bool success = deleteRegistryValue(hKey, valueName);
    closeRegistryKey(hKey);

    if (success) {
        logMessage(QString("Deleted registry value: %1\\%2").arg(keyPath).arg(valueName));
    }

    return success;
}


// ========== 桌面图标特定功能 ==========

QList<DesktopIconsMonitor::DesktopIcon> DesktopIconsMonitor::getCurrentIcons() const {
    QMutexLocker locker(&m_mutex);
    return m_currentIcons.values();
}

bool DesktopIconsMonitor::setIconVisibility(const QString& clsid, bool hidden) {
    if (!m_knownIcons.contains(clsid)) {
        emitError(QString("Unknown icon CLSID: %1").arg(clsid));
        return false;
    }

    DWORD value = hidden ? 1 : 0;
    return setRegistryValue(m_currentHive, m_currentKeyPath, clsid, QVariant(static_cast<uint>(value)));
}

bool DesktopIconsMonitor::isIconHidden(const QString& clsid) const {
    QMutexLocker locker(&m_mutex);
    if (m_currentIcons.contains(clsid)) {
        QVariant value = m_currentIcons[clsid].value;
        return value.isValid() ? value.toUInt() == 1 : false;
    }
    return false;
}

QString DesktopIconsMonitor::getIconName(const QString& clsid) const {
    return m_knownIcons.value(clsid, "未知图标");
}

QMap<QString, QString> DesktopIconsMonitor::getKnownDesktopIcons() {
    return KNOWN_DESKTOP_ICONS;
}

// ========== Windows API 监听实现 ==========

void DesktopIconsMonitor::startRegistryMonitoring() {
    if (m_monitoring) {
        return;
    }

    // 创建事件对象
    m_registryEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!m_registryEvent) {
        emitError("Failed to create registry event object");
        return;
    }

    m_monitoring = true;
    m_monitorThread = std::thread(&DesktopIconsMonitor::registryMonitoringWorker, this);
}

void DesktopIconsMonitor::stopRegistryMonitoring() {
    if (!m_monitoring) {
        return;
    }

    m_monitoring = false;

    // 触发事件唤醒线程
    if (m_registryEvent) {
        SetEvent(m_registryEvent);
    }

    // 等待线程结束
    if (m_monitorThread.joinable()) {
        m_monitorThread.join();
    }

    // 清理资源
    if (m_registryEvent) {
        CloseHandle(m_registryEvent);
        m_registryEvent = nullptr;
    }

    logMessage("Stopped Windows API registry monitoring");
}

void DesktopIconsMonitor::registryMonitoringWorker() {
    HKEY hKey = nullptr;

    // 打开注册表键
    LONG result = RegOpenKeyEx(m_currentHive,
        reinterpret_cast<LPCWSTR>(m_currentKeyPath.utf16()),
        0, KEY_NOTIFY, &hKey);

    if (result != ERROR_SUCCESS) {
        emitError(QString("Failed to open registry key for monitoring: %1").arg(result));
        return;
    }

    logMessage("Registry monitoring thread started");

    while (m_monitoring) {
        // 使用Windows API注册表变化通知
        result = RegNotifyChangeKeyValue(
            hKey,                      // 要监控的键
            m_watchSubkeys,            // 是否监控子键
            REG_NOTIFY_CHANGE_NAME |   // 监控名称变化
            REG_NOTIFY_CHANGE_ATTRIBUTES |
            REG_NOTIFY_CHANGE_LAST_SET |
            REG_NOTIFY_CHANGE_SECURITY,
            m_registryEvent,           // 事件对象
            TRUE                       // 异步通知
        );

        if (result != ERROR_SUCCESS) {
            emitError(QString("RegNotifyChangeKeyValue failed: %1").arg(result));
            break;
        }

        // 等待变化事件
        DWORD waitResult = WaitForSingleObject(m_registryEvent, 1000); // 1秒超时

        if (waitResult == WAIT_OBJECT_0) {
            // 注册表发生变化
            if (m_monitoring) {
                QMetaObject::invokeMethod(this, "processRegistryChanges", Qt::QueuedConnection);
            }
        }
        else if (waitResult == WAIT_TIMEOUT) {
            // 超时，继续检查
            continue;
        }
        else {
            // 错误
            emitError(QString("WaitForSingleObject failed: %1").arg(waitResult));
            break;
        }
    }

    // 清理
    if (hKey) {
        RegCloseKey(hKey);
    }

    logMessage("Registry monitoring thread stopped");
}

void DesktopIconsMonitor::processRegistryChanges() {
    if (!m_monitoring) {
        return;
    }

    logMessage("Registry change detected - processing changes");

    // 保存旧状态
    QMap<QString, DesktopIcon> oldSnapshot = m_currentIcons;

    // 刷新当前状态
    refreshIconsState();

    // 比较变化并发出信号
    for (const auto& clsid : m_knownIcons.keys()) {
        if (m_currentIcons.contains(clsid) && oldSnapshot.contains(clsid)) {
            bool oldHidden = oldSnapshot[clsid].isHidden;
            bool newHidden = m_currentIcons[clsid].isHidden;

            if (oldHidden != newHidden) {
                QString iconName = m_knownIcons[clsid];
                emit iconVisibilityChanged(clsid, newHidden, iconName);
                logMessage(QString("Icon visibility changed: %1 -> %2")
                    .arg(iconName).arg(newHidden ? "Hidden" : "Visible"));
            }
        }
    }
}

// ========== 注册表操作辅助方法 ==========

HKEY DesktopIconsMonitor::openRegistryKey(HKEY hive, const QString& keyPath, REGSAM access) {
    HKEY hKey = nullptr;
    LONG result = RegOpenKeyEx(hive,
        reinterpret_cast<LPCWSTR>(keyPath.utf16()),
        0, access, &hKey);

    if (result != ERROR_SUCCESS) {
        emitError(QString("Failed to open registry key: %1 (Error: %2)").arg(keyPath).arg(result));
        return nullptr;
    }

    return hKey;
}

void DesktopIconsMonitor::closeRegistryKey(HKEY key) {
    if (key) {
        RegCloseKey(key);
    }
}

QVariant DesktopIconsMonitor::readRegistryValue(HKEY key, const QString& valueName) {
    if (!key) return QVariant();

    DWORD type = 0;
    DWORD dataSize = 0;

    // 获取数据大小
    LONG result = RegQueryValueEx(key,
        reinterpret_cast<LPCWSTR>(valueName.utf16()),
        nullptr, &type, nullptr, &dataSize);

    if (result != ERROR_SUCCESS) {
        return QVariant();
    }

    QByteArray buffer(dataSize, 0);
    result = RegQueryValueEx(key,
        reinterpret_cast<LPCWSTR>(valueName.utf16()),
        nullptr, &type,
        reinterpret_cast<LPBYTE>(buffer.data()), &dataSize);

    if (result != ERROR_SUCCESS) {
        return QVariant();
    }

    // 根据类型转换数据
    switch (type) {
    case REG_DWORD:
        if (dataSize >= sizeof(DWORD)) {
            DWORD dwValue = *reinterpret_cast<const DWORD*>(buffer.constData());
            return QVariant(static_cast<uint>(dwValue));
        }
        break;

    case REG_SZ:
    case REG_EXPAND_SZ:
        if (dataSize > 0) {
            int length = (dataSize / sizeof(wchar_t)) - 1;
            const wchar_t* wideStr = reinterpret_cast<const wchar_t*>(buffer.constData());
            return QVariant(QString::fromWCharArray(wideStr, length));
        }
        break;

    case REG_BINARY:
        return QVariant(buffer);

    default:
        break;
    }

    return QVariant();
}

bool DesktopIconsMonitor::writeRegistryValue(HKEY key, const QString& valueName, const QVariant& value) {
    if (!key) return false;

    if (value.canConvert<uint>()) {
        DWORD dwValue = value.toUInt();
        LONG result = RegSetValueEx(key,
            reinterpret_cast<LPCWSTR>(valueName.utf16()),
            0, REG_DWORD,
            reinterpret_cast<const BYTE*>(&dwValue),
            sizeof(DWORD));
        return result == ERROR_SUCCESS;
    }

    return false;
}

bool DesktopIconsMonitor::deleteRegistryValue(HKEY key, const QString& valueName) {
    if (!key) return false;

    LONG result = RegDeleteValue(key, reinterpret_cast<LPCWSTR>(valueName.utf16()));
    return result == ERROR_SUCCESS;
}

void DesktopIconsMonitor::refreshIconsState() {
    QMutexLocker locker(&m_mutex);

    HKEY hKey = openRegistryKey(m_currentHive, m_currentKeyPath, KEY_READ);
    if (!hKey) {
        return;
    }

    m_currentIcons.clear();

    // 读取所有已知图标的值
    for (auto it = m_knownIcons.constBegin(); it != m_knownIcons.constEnd(); ++it) {
        const QString& clsid = it.key();
        const QString& name = it.value();

        QVariant value = readRegistryValue(hKey, clsid);
        bool isHidden = value.isValid() ? value.toUInt() == 1 : false;

        DesktopIcon icon;
        icon.clsid = clsid;
        icon.name = name;
        icon.isHidden = isHidden;
        icon.value = value;

        m_currentIcons[clsid] = icon;
    }

    closeRegistryKey(hKey);

    // 发射刷新信号
    emit desktopIconsRefreshed(m_currentIcons.values());

};