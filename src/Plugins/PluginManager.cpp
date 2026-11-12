#include "plugins/PluginManager.h"
#include <QDir>
#include <QCoreApplication>
#include <QDebug>
#include <QPluginLoader> // Added as per instruction snippet
#include "services/HotkeyManager.h"
#include "services/AppPaths.h" // Added as per instruction
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

void PluginManager::shutdown() {
    if (m_instance) {
        m_instance->unloadPlugins();
    }
}

void PluginManager::loadPlugins() {
    if (!qApp) {
        qWarning() << "[PluginManager] Cannot load plugins: qApp is null!";
        return;
    }

    // 收集所有候选插件搜索目录
    QStringList searchDirs;

    // 1. 系统/安装包内置插件目录（位于 applicationDirPath()/plugins）
    const QString appPluginsPath = QCoreApplication::applicationDirPath() + "/plugins";
    QDir appDir(appPluginsPath);
    if (appDir.exists()) {
        searchDirs << appPluginsPath;
    } else {
        if (appDir.mkpath(".")) {
            qDebug() << "[PluginManager] Created app plugins directory:" << appPluginsPath;
            searchDirs << appPluginsPath;
        } else {
            qDebug() << "[PluginManager] App plugins directory does not exist or is read-only:" << appPluginsPath;
        }
    }

    // 2. 用户级插件目录（位于 APPDATA/LinDesk/plugins，始终拥有读写权限）
    const QString userPluginsPath = AppPaths::userPluginConfigDir();
    if (!searchDirs.contains(userPluginsPath)) {
        searchDirs << userPluginsPath;
    }

    // Clear old state safely
    unloadPlugins();

    QStringList filters;
    filters << "*.dll";

    for (const QString& pluginDir : searchDirs) {
        QDir dir(pluginDir);
        if (!dir.exists()) continue;

        qDebug() << "[PluginManager] Querying directory content in:" << pluginDir;
        const QFileInfoList entryList = dir.entryInfoList(filters, QDir::Files);
        qDebug() << "[PluginManager] Found" << entryList.size() << "potential DLL files in" << pluginDir;
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

                // Register global hotkey if enabled and configured
                if (isPluginEnabled(basePlugin->pluginId()) && basePlugin->isConfigured()) {
                    registerPluginHotkey(basePlugin);
                } else {
                    qDebug() << "[PluginManager] Skipping hotkey registration for unconfigured/disabled plugin:" << basePlugin->pluginId();
                }

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

    }

    qDebug() << "[PluginManager] Load complete. Total plugins:" << m_allPlugins.size();
}

void PluginManager::unloadPlugins() {
    if (m_unloading) {
        return;
    }

    m_unloading = true;
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
    m_unloading = false;
}

ILinPlugin* PluginManager::getPlugin(const QString& id) const {
    for (auto* p : m_allPlugins) {
        if (p && p->pluginId() == id) return p;
    }
    return nullptr;
}

#include <QDateTime>

static bool parseHotkeyCombination(const QString& hk, UINT& outMods, UINT& outVk) {
    outMods = 0;
    outVk = 0;
    if (hk.trimmed().isEmpty()) return false;

    QStringList parts = hk.split('+', QString::SkipEmptyParts);
    for (const QString& p : parts) {
        QString p_up = p.trimmed().toUpper();
        if (p_up == "ALT") outMods |= MOD_ALT;
        else if (p_up == "CTRL") outMods |= MOD_CONTROL;
        else if (p_up == "SHIFT") outMods |= MOD_SHIFT;
        else if (p_up == "WIN") outMods |= MOD_WIN;
        else if (p_up.length() == 1) outVk = (UINT)p_up[0].toLatin1();
        else if (p_up.startsWith("F") && p_up.length() > 1) {
            outVk = VK_F1 + (p_up.mid(1).toInt() - 1);
        }
    }
    return (outVk > 0);
}

void PluginManager::triggerPlugin(const QString& id) {
    QString pluginId = id;
    QString subAction = QStringLiteral("default");
    const int colonIdx = id.indexOf(':');
    if (colonIdx > 0) {
        pluginId = id.left(colonIdx);
        subAction = id.mid(colonIdx + 1);
    }

    if (!isPluginEnabled(pluginId)) {
        qWarning() << "[PluginManager] Skip trigger: Plugin is disabled:" << pluginId;
        return;
    }

    ILinPlugin* plugin = getPlugin(pluginId);
    if (!plugin) {
        qWarning() << "[PluginManager] Failed to trigger: Plugin ID not found:" << pluginId;
        return;
    }

    if (!plugin->isConfigured()) {
        qWarning() << "[PluginManager] Skip trigger: Plugin is not configured:" << pluginId;
        return;
    }

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    qDebug() << "[" << timestamp << "][PluginManager] Triggering plugin:" << plugin->pluginName() << "Action:" << subAction;
    plugin->onTriggerAction(subAction);
}

bool PluginManager::isPluginEnabled(const QString& id) const {
    ILinPlugin* plugin = getPlugin(id);
    if (plugin && !plugin->isConfigured()) {
        return false;
    }
    return m_enabledMap.value(id, true); // Default to enabled for configured plugins
}

void PluginManager::setPluginEnabled(const QString& id, bool enabled) {
    ILinPlugin* plugin = getPlugin(id);
    if (enabled && plugin && !plugin->isConfigured()) {
        qWarning() << "[PluginManager] Cannot enable plugin without configuration:" << id;
        m_enabledMap[id] = false;
        HotkeyManager::instance()->unregisterHotkey(id);
        HotkeyManager::instance()->unregisterHotkey(id + QStringLiteral(":ocr"));
        HotkeyManager::instance()->unregisterHotkey(id + QStringLiteral(":screenshot"));
        return;
    }

    m_enabledMap[id] = enabled;
    qDebug() << "[PluginManager] Plugin" << id << "set to" << (enabled ? "Enabled" : "Disabled");

    if (plugin) {
        if (enabled && plugin->isConfigured()) {
            registerPluginHotkey(plugin);
        } else {
            HotkeyManager::instance()->unregisterHotkey(id);
            HotkeyManager::instance()->unregisterHotkey(id + QStringLiteral(":ocr"));
            HotkeyManager::instance()->unregisterHotkey(id + QStringLiteral(":screenshot"));
        }
    }
}

void PluginManager::registerPluginHotkey(ILinPlugin* plugin) {
    if (!plugin) return;

    const QString baseId = plugin->pluginId();
    const QString ocrActionId = baseId + QStringLiteral(":ocr");
    const QString screenshotActionId = baseId + QStringLiteral(":screenshot");

    if (!isPluginEnabled(baseId) || !plugin->isConfigured()) {
        HotkeyManager::instance()->unregisterHotkey(baseId);
        HotkeyManager::instance()->unregisterHotkey(ocrActionId);
        HotkeyManager::instance()->unregisterHotkey(screenshotActionId);
        return;
    }

    // 1. Primary hotkey (OCR)
    QString primaryHk = plugin->customHotkey();
    UINT pMods = 0, pVk = 0;
    if (parseHotkeyCombination(primaryHk, pMods, pVk)) {
        HotkeyManager::instance()->registerHotkey(pMods, pVk, ocrActionId);
        qDebug() << "[PluginManager] Registered primary hotkey for" << ocrActionId << ":" << primaryHk;
    } else {
        HotkeyManager::instance()->unregisterHotkey(ocrActionId);
    }

    // 2. Secondary hotkey (Screenshot Only)
    QString secondaryHk = plugin->customSecondaryHotkey();
    UINT sMods = 0, sVk = 0;
    if (parseHotkeyCombination(secondaryHk, sMods, sVk)) {
        HotkeyManager::instance()->registerHotkey(sMods, sVk, screenshotActionId);
        qDebug() << "[PluginManager] Registered secondary hotkey for" << screenshotActionId << ":" << secondaryHk;
    } else {
        HotkeyManager::instance()->unregisterHotkey(screenshotActionId);
    }
}
