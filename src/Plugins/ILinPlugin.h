#pragma once
#include <QString>
#include <QImage>
#include <QtPlugin>

class QWidget;

/**
 * @brief Base Plugin Interface for LinDesk
 */
class ILinPlugin {
public:
    virtual ~ILinPlugin() {}

    // Required for identification
    virtual QString pluginId() const = 0;
    virtual QString pluginName() const = 0;
    virtual QString pluginDescription() const = 0;
    virtual QString customHotkey() const { return ""; } // Optional: user-configurable primary global hotkey (e.g. OCR)
    virtual QString customSecondaryHotkey() const { return ""; } // Optional: user-configurable secondary global hotkey (e.g. Screenshot)
    virtual bool isConfigured() const { return true; }   // Optional: check if required keys/tokens/settings are configured
    
    // Lifecycle
    virtual void onInit() = 0;
    virtual void onUnload() = 0;
    virtual void onSettingsRequested(QWidget* parent = nullptr) { Q_UNUSED(parent); } // Optional: Request plugin-specific settings UI

    // Activation (Hotkeys, Menu clicks, etc.)
    virtual void onTrigger() = 0;
    virtual void onTriggerAction(const QString& action) { Q_UNUSED(action); onTrigger(); }
};

/**
 * @brief OCR Feature Interface
 */
class IOCRPlugin : public ILinPlugin {
public:
    virtual QString recognizeText(const QImage& source) = 0;
};

// Unique Identifier for the Interface (Standard Qt way)
#define ILinPlugin_IID "com.lindesk.ILinPlugin/1.0"
Q_DECLARE_INTERFACE(ILinPlugin, ILinPlugin_IID)

#define IOCRPlugin_IID "com.lindesk.IOCRPlugin/1.0" 
Q_DECLARE_INTERFACE(IOCRPlugin, IOCRPlugin_IID)
