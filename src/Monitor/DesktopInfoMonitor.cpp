#include "DesktopInfoMonitor.h"
#include <QDebug>
#include <QTimer>
#include <QCoreApplication>
#include <stdexcept>

static const QMap<QString, QString> KNOWN_DESKTOP_ICONS = {
    {"{645FF040-5081-101B-9F08-00AA002F954E}", "RECYCLE_BIN"},
    {"{20D04FE0-3AEA-1069-A2D8-08002B30309D}", "MY_COMPUTER"},
    {"{5399E694-6CE5-4D6C-8FCE-1D8870FDCBA0}", "CONTROL_PANEL"},
    {"{F02C1A0D-BE21-4350-88B0-7367FC96EF3C}", "NETWORK"},
    {"{59031a47-3f72-44a7-89c5-5595fe6b30ee}", "USER_FOLDER"},
    {"{018D5C66-4533-4307-9B53-224DE2ED1FE6}", "OneDrive"}
};

DesktopInfoMonitor::DesktopInfoMonitor(QObject* parent)
    : IRegistryListener("DesktopInfoMonitor", parent)
    , m_currentHive(HKEY_CURRENT_USER)
    , m_currentKeyPath("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\NewStartPanel")
    , m_watchSubkeys(false)
{
    m_knownIcons = KNOWN_DESKTOP_ICONS;
    logMessage("DesktopInfoMonitor created");
}

DesktopInfoMonitor::~DesktopInfoMonitor() {
    stopListening();
    logMessage("DesktopInfoMonitor destroyed");
}

bool DesktopInfoMonitor::startListening() {
    if (isListening()) {
        return true;
    }

    if (m_currentKeyPath.isEmpty()) {
        emitError("No registry key to watch");
        return false;
    }

    try {
        refreshIconsState();
        startRegistryMonitoring();
        setupWallpaperMonitoring(); // Added wallpaper monitoring

        setListening(true);
        logMessage("Started monitoring desktop info (icons + wallpaper)");
        return true;
    }
    catch (const std::exception& e) {
        emitError(QString("Failed to start monitoring: %1").arg(e.what()));
        return false;
    }
}

void DesktopInfoMonitor::stopListening() {
    if (!isListening()) return;

    stopRegistryMonitoring();
    cleanupWallpaperMonitoring();

    setListening(false);
    logMessage("Stopped monitoring desktop info");
}

void DesktopInfoMonitor::setupWallpaperMonitoring() {
    if (m_wallpaperNotifier) return;

    LONG result = RegOpenKeyEx(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, KEY_NOTIFY, &m_wallpaperKey);
    if (result != ERROR_SUCCESS) {
        logMessage("Failed to open wallpaper registry key for monitoring");
        return;
    }

    m_wallpaperEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_wallpaperEvent) {
        RegCloseKey(m_wallpaperKey);
        m_wallpaperKey = nullptr;
        return;
    }

    m_wallpaperNotifier = new QWinEventNotifier(m_wallpaperEvent, this);
    connect(m_wallpaperNotifier, &QWinEventNotifier::activated, this, &DesktopInfoMonitor::onWallpaperKeyChanged);
    
    // Initial register
    RegNotifyChangeKeyValue(m_wallpaperKey, FALSE, REG_NOTIFY_CHANGE_LAST_SET, m_wallpaperEvent, TRUE);
}

void DesktopInfoMonitor::cleanupWallpaperMonitoring() {
    if (m_wallpaperNotifier) {
        m_wallpaperNotifier->setEnabled(false);
        delete m_wallpaperNotifier;
        m_wallpaperNotifier = nullptr;
    }
    if (m_wallpaperEvent) {
        CloseHandle(m_wallpaperEvent);
        m_wallpaperEvent = nullptr;
    }
    if (m_wallpaperKey) {
        RegCloseKey(m_wallpaperKey);
        m_wallpaperKey = nullptr;
    }
}

void DesktopInfoMonitor::onWallpaperKeyChanged() {
    logMessage("Wallpaper registry change detected");
    emit wallpaperChanged();
    
    // Re-register for next change
    if (m_wallpaperKey && m_wallpaperEvent) {
        RegNotifyChangeKeyValue(m_wallpaperKey, FALSE, REG_NOTIFY_CHANGE_LAST_SET, m_wallpaperEvent, TRUE);
    }
}

void DesktopInfoMonitor::watchKey(HKEY hive, const QString& keyPath, bool watchSubkeys) {
    QMutexLocker locker(&m_mutex);
    m_currentHive = hive;
    m_currentKeyPath = keyPath;
    m_watchSubkeys = watchSubkeys;
    logMessage(QString("Watching registry key for icons: %1").arg(keyPath));
}

void DesktopInfoMonitor::unwatchKey(HKEY hive, const QString& keyPath) {
    QMutexLocker locker(&m_mutex);
    if (hive == m_currentHive && keyPath == m_currentKeyPath) {
        stopRegistryMonitoring();
        m_currentKeyPath.clear();
    logMessage("Stopped watching registry key for icons");
    }
}

QList<QPair<HKEY, QString>> DesktopInfoMonitor::getWatchedKeys() const {
    QMutexLocker locker(&m_mutex);
    QList<QPair<HKEY, QString>> result;
    if (!m_currentKeyPath.isEmpty()) {
        result.append(qMakePair(m_currentHive, m_currentKeyPath));
    }
    return result;
}

QVariant DesktopInfoMonitor::getRegistryValue(HKEY hive, const QString& keyPath, const QString& valueName) {
    HKEY hKey = openRegistryKey(hive, keyPath, KEY_READ);
    if (!hKey) return QVariant();
    QVariant value = readRegistryValue(hKey, valueName);
    closeRegistryKey(hKey);
    return value;
}

bool DesktopInfoMonitor::setRegistryValue(HKEY hive, const QString& keyPath, const QString& valueName, const QVariant& value) {
    HKEY hKey = openRegistryKey(hive, keyPath, KEY_WRITE);
    if (!hKey) return false;
    bool success = writeRegistryValue(hKey, valueName, value);
    closeRegistryKey(hKey);
    return success;
}

bool DesktopInfoMonitor::deleteRegistryValue(HKEY hive, const QString& keyPath, const QString& valueName) {
    HKEY hKey = openRegistryKey(hive, keyPath, KEY_WRITE);
    if (!hKey) return false;
    bool success = deleteRegistryValue(hKey, valueName);
    closeRegistryKey(hKey);
    return success;
}

QList<DesktopInfoMonitor::DesktopIcon> DesktopInfoMonitor::getCurrentIcons() const {
    QMutexLocker locker(&m_mutex);
    return m_currentIcons.values();
}

bool DesktopInfoMonitor::setIconVisibility(const QString& clsid, bool hidden) {
    if (!m_knownIcons.contains(clsid)) return false;
    DWORD value = hidden ? 1 : 0;
    return setRegistryValue(m_currentHive, m_currentKeyPath, clsid, QVariant(static_cast<uint>(value)));
}

bool DesktopInfoMonitor::isIconHidden(const QString& clsid) const {
    QMutexLocker locker(&m_mutex);
    if (m_currentIcons.contains(clsid)) {
        QVariant value = m_currentIcons[clsid].value;
        return value.isValid() ? value.toUInt() == 1 : false;
    }
    return false;
}

QString DesktopInfoMonitor::getIconName(const QString& clsid) const {
    return m_knownIcons.value(clsid, "未知图标");
}

QMap<QString, QString> DesktopInfoMonitor::getKnownDesktopIcons() {
    return KNOWN_DESKTOP_ICONS;
}

void DesktopInfoMonitor::startRegistryMonitoring() {
    if (m_monitoring) return;
    m_registryEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!m_registryEvent) return;
    m_monitoring = true;
    m_monitorThread = std::thread(&DesktopInfoMonitor::registryMonitoringWorker, this);
}

void DesktopInfoMonitor::stopRegistryMonitoring() {
    if (!m_monitoring) return;
    m_monitoring = false;
    if (m_registryEvent) SetEvent(m_registryEvent);
    if (m_monitorThread.joinable()) m_monitorThread.join();
    if (m_registryEvent) {
        CloseHandle(m_registryEvent);
        m_registryEvent = nullptr;
    }
}

void DesktopInfoMonitor::registryMonitoringWorker() {
    HKEY hKey = nullptr;
    LONG result = RegOpenKeyEx(m_currentHive, reinterpret_cast<LPCWSTR>(m_currentKeyPath.utf16()), 0, KEY_NOTIFY, &hKey);
    if (result != ERROR_SUCCESS) return;

    while (m_monitoring) {
        result = RegNotifyChangeKeyValue(hKey, m_watchSubkeys, REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_ATTRIBUTES | REG_NOTIFY_CHANGE_LAST_SET | REG_NOTIFY_CHANGE_SECURITY, m_registryEvent, TRUE);
        if (result != ERROR_SUCCESS) break;
        DWORD waitResult = WaitForSingleObject(m_registryEvent, 1000);
        if (waitResult == WAIT_OBJECT_0 && m_monitoring) {
            ResetEvent(m_registryEvent); // Fix: Reset manual-reset event
            QMetaObject::invokeMethod(this, "processRegistryChanges", Qt::QueuedConnection);
        }
    }
    if (hKey) RegCloseKey(hKey);
}

void DesktopInfoMonitor::processRegistryChanges() {
    if (!m_monitoring) return;
    QMap<QString, DesktopIcon> oldSnapshot = m_currentIcons;
    refreshIconsState();
    for (const auto& clsid : m_knownIcons.keys()) {
        if (m_currentIcons.contains(clsid) && oldSnapshot.contains(clsid)) {
            if (oldSnapshot[clsid].isHidden != m_currentIcons[clsid].isHidden) {
                emit iconVisibilityChanged(clsid, m_currentIcons[clsid].isHidden, m_knownIcons[clsid]);
            }
        }
    }
}

HKEY DesktopInfoMonitor::openRegistryKey(HKEY hive, const QString& keyPath, REGSAM access) {
    HKEY hKey = nullptr;
    LONG result = RegOpenKeyEx(hive, reinterpret_cast<LPCWSTR>(keyPath.utf16()), 0, access, &hKey);
    return (result == ERROR_SUCCESS) ? hKey : nullptr;
}

void DesktopInfoMonitor::closeRegistryKey(HKEY key) {
    if (key) RegCloseKey(key);
}

QVariant DesktopInfoMonitor::readRegistryValue(HKEY key, const QString& valueName) {
    if (!key) return QVariant();
    DWORD type = 0, dataSize = 0;
    LONG result = RegQueryValueEx(key, reinterpret_cast<LPCWSTR>(valueName.utf16()), nullptr, &type, nullptr, &dataSize);
    if (result != ERROR_SUCCESS) return QVariant();
    QByteArray buffer(dataSize, 0);
    result = RegQueryValueEx(key, reinterpret_cast<LPCWSTR>(valueName.utf16()), nullptr, &type, reinterpret_cast<LPBYTE>(buffer.data()), &dataSize);
    if (result != ERROR_SUCCESS) return QVariant();
    if (type == REG_DWORD && dataSize >= sizeof(DWORD)) return QVariant(static_cast<uint>(*reinterpret_cast<const DWORD*>(buffer.constData())));
    if ((type == REG_SZ || type == REG_EXPAND_SZ) && dataSize > 0) return QVariant(QString::fromWCharArray(reinterpret_cast<const wchar_t*>(buffer.constData()), (dataSize / sizeof(wchar_t)) - 1));
    return QVariant();
}

bool DesktopInfoMonitor::writeRegistryValue(HKEY key, const QString& valueName, const QVariant& value) {
    if (!key || !value.canConvert<uint>()) return false;
    DWORD dwValue = value.toUInt();
    return RegSetValueEx(key, reinterpret_cast<LPCWSTR>(valueName.utf16()), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dwValue), sizeof(DWORD)) == ERROR_SUCCESS;
}

bool DesktopInfoMonitor::deleteRegistryValue(HKEY key, const QString& valueName) {
    return key && RegDeleteValue(key, reinterpret_cast<LPCWSTR>(valueName.utf16())) == ERROR_SUCCESS;
}

void DesktopInfoMonitor::refreshIconsState() {
    QMutexLocker locker(&m_mutex);
    HKEY hKey = openRegistryKey(m_currentHive, m_currentKeyPath, KEY_READ);
    if (!hKey) return;
    m_currentIcons.clear();
    for (auto it = m_knownIcons.constBegin(); it != m_knownIcons.constEnd(); ++it) {
        QVariant value = readRegistryValue(hKey, it.key());
        DesktopIcon icon(it.key(), it.value());
        icon.isHidden = value.isValid() ? value.toUInt() == 1 : false;
        icon.value = value;
        m_currentIcons[it.key()] = icon;
    }
    closeRegistryKey(hKey);
    emit desktopIconsRefreshed(m_currentIcons.values());
}
