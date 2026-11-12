#pragma once
#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QMap>
#include <Windows.h>

/**
 * @brief Manages Windows native Global Hotkeys (RegisterHotKey)
 */
class HotkeyManager : public QObject {
    Q_OBJECT

public:
    static HotkeyManager* instance();

    // Register a key combination for a specific plugin or action
    // modifiers: MOD_ALT, MOD_CONTROL, MOD_SHIFT, MOD_WIN
    // vk: VK_A, VK_F1, etc.
    bool registerHotkey(UINT modifiers, UINT vk, const QString& actionId);
    
    // Unregister hotkey for a specific action
    bool unregisterHotkey(const QString& actionId);

    // Unregister all
    void unregisterAll();

    // Map a Windows Hotkey ID to an action ID
    QString getActionId(int hotkeyId) const;

private:
    explicit HotkeyManager(QObject* parent = nullptr);
    ~HotkeyManager();

    static HotkeyManager* m_instance;
    HWND m_registeredHwnd = nullptr;
    QMap<int, QString> m_hotkeyActions; // ID -> ActionID
    int m_nextHotkeyId = 100; // IDs for RegisterHotKey starting from 100
};
