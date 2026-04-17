#pragma once
#include <QObject>
#include <QList>
#include <QPluginLoader>
#include "plugins/ILinPlugin.h"

/**
 * @brief Manages the discovery and loading of dynamic DLL plugins
 */
class PluginManager : public QObject {
    Q_OBJECT

public:
    static PluginManager* instance();

    // Discovery
    void loadPlugins();
    void unloadPlugins();

    // Registry Access
    QList<ILinPlugin*> allPlugins() const { return m_allPlugins; }
    QList<IOCRPlugin*> ocrPlugins() const { return m_ocrPlugins; }

    // Hotkey Management
    void registerPluginHotkey(ILinPlugin* plugin);

    // Helper: Find a specific plugin by ID
    ILinPlugin* getPlugin(const QString& id) const;

    // Dispatcher: Trigger a specific plugin's onTrigger()
    void triggerPlugin(const QString& id);

    // State Management
    bool isPluginEnabled(const QString& id) const;
    void setPluginEnabled(const QString& id, bool enabled);

private:
    explicit PluginManager(QObject* parent = nullptr);
    ~PluginManager();

    static PluginManager* m_instance;
    QList<ILinPlugin*> m_allPlugins;
    QList<IOCRPlugin*> m_ocrPlugins;
    QList<QPluginLoader*> m_loaders;
    QMap<QString, bool> m_enabledMap; // PluginID -> Enabled
};
