#include "services/HotkeyManager.h"
#include "desktop/ListView.h" // To get the main HWND
#include <QDebug>
#include <QAbstractEventDispatcher>

HotkeyManager* HotkeyManager::m_instance = nullptr;

HotkeyManager* HotkeyManager::instance() {
    if (!m_instance) {
        m_instance = new HotkeyManager(qApp);
    }
    return m_instance;
}

HotkeyManager::HotkeyManager(QObject* parent) : QObject(parent) {
}

HotkeyManager::~HotkeyManager() {
    unregisterAll();
}

bool HotkeyManager::registerHotkey(UINT modifiers, UINT vk, const QString& actionId) {
    ListView* lv = ListView::instance();
    if (!lv) return false;

    HWND hwnd = (HWND)lv->winId();
    if (!hwnd) return false;
    m_registeredHwnd = hwnd;

    // Fix: Unregister old hotkey for the same actionId if it exists
    unregisterHotkey(actionId);

    int id = m_nextHotkeyId++;

    if (RegisterHotKey(hwnd, id, modifiers, vk)) {
        m_hotkeyActions.insert(id, actionId);
        qDebug() << "[HotkeyManager] Registered hotkey ID:" << id << "(Mods:" << modifiers << ", VK:" << vk << ") for action:" << actionId;
        return true;
    } else {
        qWarning() << "[HotkeyManager] Failed to register hotkey for action:" << actionId << "Error:" << GetLastError();
        return false;
    }
}

bool HotkeyManager::unregisterHotkey(const QString& actionId) {
    if (actionId.isEmpty()) return false;

    ListView* lv = ListView::instance();
    HWND hwnd = m_registeredHwnd ? m_registeredHwnd : (lv ? (HWND)lv->winId() : nullptr);

    QList<int> oldIds = m_hotkeyActions.keys(actionId);
    if (oldIds.isEmpty()) return false;

    for (int oldId : oldIds) {
        if (hwnd && IsWindow(hwnd)) {
            UnregisterHotKey(hwnd, oldId);
        }
        m_hotkeyActions.remove(oldId);
        qDebug() << "[HotkeyManager] Unregistered hotkey ID:" << oldId << "for action:" << actionId;
    }
    return true;
}

void HotkeyManager::unregisterAll() {
    if (!m_registeredHwnd || !IsWindow(m_registeredHwnd)) {
        m_hotkeyActions.clear();
        m_nextHotkeyId = 100;
        m_registeredHwnd = nullptr;
        return;
    }

    for (auto it = m_hotkeyActions.begin(); it != m_hotkeyActions.end(); ++it) {
        UnregisterHotKey(m_registeredHwnd, it.key());
    }
    m_hotkeyActions.clear();
    m_nextHotkeyId = 100;
    m_registeredHwnd = nullptr;
}

QString HotkeyManager::getActionId(int hotkeyId) const {
    return m_hotkeyActions.value(hotkeyId, "");
}
