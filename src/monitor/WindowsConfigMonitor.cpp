#include "monitor/WindowsConfigMonitor.h"

#include <QDebug>

namespace {
constexpr wchar_t kExplorerAdvancedKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";
constexpr wchar_t kHiddenValueName[] = L"Hidden";
}

WindowsConfigMonitor::WindowsConfigMonitor(QObject* parent)
    : QObject(parent)
{
}

WindowsConfigMonitor::~WindowsConfigMonitor()
{
    stopListening();
}

bool WindowsConfigMonitor::startListening()
{
    if (m_advancedNotifier) {
        return true;
    }

    m_showHiddenFiles = readShowHiddenFiles();

    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, kExplorerAdvancedKey, 0, KEY_READ | KEY_NOTIFY, &m_advancedKey);
    if (result != ERROR_SUCCESS) {
        qDebug() << "[WindowsConfigMonitor] Failed to open Explorer Advanced key:" << result;
        return false;
    }

    m_advancedEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_advancedEvent) {
        RegCloseKey(m_advancedKey);
        m_advancedKey = nullptr;
        return false;
    }

    m_advancedNotifier = new QWinEventNotifier(m_advancedEvent, this);
    connect(m_advancedNotifier, &QWinEventNotifier::activated, this, &WindowsConfigMonitor::onAdvancedKeyChanged);
    registerChangeNotification();
    return true;
}

void WindowsConfigMonitor::stopListening()
{
    if (m_advancedNotifier) {
        m_advancedNotifier->setEnabled(false);
        delete m_advancedNotifier;
        m_advancedNotifier = nullptr;
    }

    if (m_advancedEvent) {
        CloseHandle(m_advancedEvent);
        m_advancedEvent = nullptr;
    }

    if (m_advancedKey) {
        RegCloseKey(m_advancedKey);
        m_advancedKey = nullptr;
    }
}

void WindowsConfigMonitor::refreshState()
{
    const bool nextShowHiddenFiles = readShowHiddenFiles();
    if (m_showHiddenFiles == nextShowHiddenFiles) {
        return;
    }

    m_showHiddenFiles = nextShowHiddenFiles;
    emit showHiddenFilesChanged(m_showHiddenFiles);
}

void WindowsConfigMonitor::onAdvancedKeyChanged()
{
    refreshState();
    registerChangeNotification();
}

bool WindowsConfigMonitor::readShowHiddenFiles() const
{
    HKEY key = nullptr;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, kExplorerAdvancedKey, 0, KEY_READ, &key);
    if (result != ERROR_SUCCESS) {
        return false;
    }

    DWORD type = 0;
    DWORD value = 2;
    DWORD valueSize = sizeof(value);
    result = RegQueryValueExW(key, kHiddenValueName, nullptr, &type, reinterpret_cast<LPBYTE>(&value), &valueSize);
    RegCloseKey(key);

    if (result != ERROR_SUCCESS || type != REG_DWORD) {
        return false;
    }

    return value == 1;
}

void WindowsConfigMonitor::registerChangeNotification()
{
    if (!m_advancedKey || !m_advancedEvent) {
        return;
    }

    RegNotifyChangeKeyValue(m_advancedKey, FALSE, REG_NOTIFY_CHANGE_LAST_SET, m_advancedEvent, TRUE);
}
