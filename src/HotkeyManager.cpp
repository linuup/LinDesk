#include "HotkeyManager.h"
#include "ListView.h" // To get the main HWND
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

    // Fix: Unregister old hotkey for the same actionId if it exists
    QList<int> oldIds = m_hotkeyActions.keys(actionId);
    for (int oldId : oldIds) {
        UnregisterHotKey(hwnd, oldId);
        m_hotkeyActions.remove(oldId);
        qDebug() << "[HotkeyManager] Unregistered old hotkey ID:" << oldId << "for action:" << actionId;
    }

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

void HotkeyManager::unregisterAll() {
    ListView* lv = ListView::instance();
    if (!lv) return;

    HWND hwnd = (HWND)lv->winId();
    for (auto it = m_hotkeyActions.begin(); it != m_hotkeyActions.end(); ++it) {
        UnregisterHotKey(hwnd, it.key());
    }
    m_hotkeyActions.clear();
    m_nextHotkeyId = 100;
}

QString HotkeyManager::getActionId(int hotkeyId) const {
    return m_hotkeyActions.value(hotkeyId, "");
}
