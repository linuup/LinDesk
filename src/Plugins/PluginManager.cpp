#include "plugins/PluginManager.h"
#include <QDir>
#include <QCoreApplication>
#include <QDebug>
#include <QPluginLoader> // Added as per instruction snippet
#include "services/HotkeyManager.h" // Added as per instruction
#include <Windows.h> // Added as per instruction snippet

PluginManager* PluginManager::m_instance = nullptr;

PluginManager::PluginManager(QObject* parent) : QObject(parent) {
}

PluginManager::~PluginManager() {
    unloadPlugins();
}

PluginManager* PluginManager::instance() {
    if (!m_instance) {
        m_instance = new PluginManager(qApp);
    }
    return m_instance;
}

void PluginManager::loadPlugins() {
    if (!qApp) {
        qWarning() << "[PluginManager] Cannot load plugins: qApp is null!";
        return;
    }

    QString pluginsPath = QCoreApplication::applicationDirPath() + "/plugins";
    qDebug() << "[PluginManager] Target plugins directory:" << pluginsPath;
    
    QDir dir(pluginsPath);
    if (!dir.exists()) {
        if (dir.mkpath(".")) {
            qDebug() << "[PluginManager] Created fresh plugins directory.";
        } else {
            qWarning() << "[PluginManager] Failed to create plugins directory!";
            return;
        }
    }

    // Clear old state safely
    unloadPlugins();

    QStringList filters;
    filters << "*.dll";

    qDebug() << "[PluginManager] Querying directory content...";
    const QFileInfoList entryList = dir.entryInfoList(filters, QDir::Files);
    qDebug() << "[PluginManager] Found" << entryList.size() << "potential DLL files.";
    for (const QFileInfo& info : entryList) {
        QPluginLoader* loader = new QPluginLoader(info.absoluteFilePath(), this);
        QObject* pluginObject = loader->instance();

        if (pluginObject) {
            // Check for ILinPlugin base compliance
            ILinPlugin* basePlugin = qobject_cast<ILinPlugin*>(pluginObject);
            if (basePlugin) {
                // Check if this ID is already loaded
                QString id = basePlugin->pluginId();
                bool exists = false;
                for (auto* p : m_allPlugins) {
                    if (p->pluginId() == id) {
                        exists = true;
                        break;
                    }
                }

                if (exists) {
                    qWarning() << "[PluginManager] Duplicate plugin ID detected:" << id << ". Skipping file:" << info.fileName();
                    loader->unload();
                    delete loader;
                    continue;
                }

                m_allPlugins.append(basePlugin);
                m_loaders.append(loader);
                basePlugin->onInit();

                // Register global hotkey
                registerPluginHotkey(basePlugin);

                qDebug() << "[PluginManager] Successfully loaded plugin:" << basePlugin->pluginName() << "(ID:" << basePlugin->pluginId() << ")";

                // Categorize by functional interface
                IOCRPlugin* ocr = qobject_cast<IOCRPlugin*>(pluginObject);
                if (ocr) {
                    m_ocrPlugins.append(ocr);
                    qDebug() << "[PluginManager]   -> Categorized as OCR Plugin";
                }
            } else {
                qWarning() << "[PluginManager] Plugin DLL found but does not implement ILinPlugin interface:" << info.fileName();
                loader->unload();
                delete loader;
            }
        } else {
            qWarning() << "[PluginManager] Failed to load DLL instance:" << loader->errorString();
            delete loader;
        }
    }

    qDebug() << "[PluginManager] Load complete. Total plugins:" << m_allPlugins.size();
}

void PluginManager::unloadPlugins() {
    for (auto* plugin : m_allPlugins) {
        if (plugin) plugin->onUnload();
    }
    m_allPlugins.clear();
    m_ocrPlugins.clear();

    for (auto* loader : m_loaders) {
        if (loader) {
            loader->unload();
            delete loader;
        }
    }
    m_loaders.clear();
}

ILinPlugin* PluginManager::getPlugin(const QString& id) const {
    for (auto* p : m_allPlugins) {
        if (p && p->pluginId() == id) return p;
    }
    return nullptr;
}

#include <QDateTime>

void PluginManager::triggerPlugin(const QString& id) {
    if (!isPluginEnabled(id)) {
        qWarning() << "[PluginManager] Skip trigger: Plugin is disabled:" << id;
        return;
    }

    ILinPlugin* plugin = getPlugin(id);
    if (plugin) {
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
        qDebug() << "[" << timestamp << "][PluginManager] Triggering plugin:" << plugin->pluginName();
        plugin->onTrigger();
    } else {
        qWarning() << "[PluginManager] Failed to trigger: Plugin ID not found:" << id;
    }
}

bool PluginManager::isPluginEnabled(const QString& id) const {
    return m_enabledMap.value(id, true); // Default to enabled
}

void PluginManager::setPluginEnabled(const QString& id, bool enabled) {
    m_enabledMap[id] = enabled;
    qDebug() << "[PluginManager] Plugin" << id << "set to" << (enabled ? "Enabled" : "Disabled");
}

void PluginManager::registerPluginHotkey(ILinPlugin* plugin) {
    if (!plugin || !isPluginEnabled(plugin->pluginId())) return;

    QString hk = plugin->customHotkey();
    if (hk.isEmpty()) return;

    UINT mods = 0;
    UINT vk = 0;
    QStringList parts = hk.split('+', QString::SkipEmptyParts);
    for (const QString& p : parts) {
        QString p_up = p.trimmed().toUpper();
        if (p_up == "ALT") mods |= MOD_ALT;
        else if (p_up == "CTRL") mods |= MOD_CONTROL;
        else if (p_up == "SHIFT") mods |= MOD_SHIFT;
        else if (p_up == "WIN") mods |= MOD_WIN;
        else if (p_up.length() == 1) vk = (UINT)p_up[0].toLatin1();
        else if (p_up.startsWith("F") && p_up.length() > 1) {
            vk = VK_F1 + (p_up.mid(1).toInt() - 1);
        }
    }

    if (vk > 0) {
        // HotkeyManager will automatically unregister the old ID if it exists and replace it
        HotkeyManager::instance()->registerHotkey(mods, vk, plugin->pluginId());
        qDebug() << "[PluginManager] Registered hotkey for" << plugin->pluginId() << ":" << hk;
    }
}
