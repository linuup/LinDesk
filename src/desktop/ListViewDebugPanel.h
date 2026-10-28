#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QWidget>

#include "ai/client/AIProviderBase.h"

class QLabel;
class QCheckBox;
class QComboBox;
class QFileSystemWatcher;
class QGridLayout;
class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QResizeEvent;
class QSlider;
class QTimer;
class ListView;
class FluentToggleSwitch;
namespace AI {
class AICoreService;
struct ChatRequest;
}

class ListViewDebugPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ListViewDebugPanel(ListView* listView, QWidget* parent = nullptr);

private:
    void initUI();
    void updateAdaptiveLayout();
    void updateLabels();
    void updateOpenGLStatus();
    void reloadLinWidgetsList();
    void updateLinWidgetsButtons();
    void updateLinWidgetLogViewer();
    void updateLinWidgetPermissionControls();
    void refreshLinWidgetRunningMaps();
    QString selectedLinWidgetRunningInstanceId() const;
    QString selectedLinWidgetPackagePath() const;
    QString selectedLinWidgetRegistryId() const;
    QString selectedLinWidgetRegistryKey() const;
    QString selectedLinWidgetInstanceIdForLog() const;
    QString linWidgetLogFilePathForInstance(const QString& instanceId) const;
    bool selectedLinWidgetIsDevelopment() const;
    bool selectedLinWidgetIsRunning() const;
    void updateLinWidgetItemText(QListWidgetItem* item) const;
    void updateSelectedLinWidgetItemText();
    void updateAllLinWidgetItemWidgets();
    QWidget* createLinWidgetItemWidget(QListWidgetItem* item);
    bool loadSelectedLinWidget();
    bool unloadSelectedLinWidget();
    void exportLinWidgetDiagnostics();
    QListWidget* activeLinWidgetList() const;
    QString linWidgetPackageKey(const QString& packagePath) const;
    QString linWidgetRegistryKey(const QString& id, bool isDevelopment) const;
    QStringList selectedLinWidgetRequestedPermissions() const;
    QStringList selectedLinWidgetGrantedPermissions() const;
    void setSelectedLinWidgetGrantedPermission(const QString& permission, bool granted);
    QPushButton* createActionButton(const QString& text, bool accent = false, bool destructive = false) const;
    QWidget* createSliderSettingRow(const QString& labelText,
                                    int minValue,
                                    int maxValue,
                                    QSlider*& slider,
                                    QLabel*& valueLabel);
    QLabel* createPageTitleLabel(const QString& text) const;
    QLabel* createPageSubtitleLabel(const QString& text) const;

    QWidget* createGeneralPage();
    QWidget* createLinWidgetsPage();
    QWidget* createAIPage();
    QWidget* createLayoutPage();
    QWidget* wrapScrollablePage(QWidget* content);
    void loadAIConfig();
    void saveAIConfig();
    AI::ChatRequest buildAIChatRequest() const;
    void appendAIChatLine(const QString& role, const QString& content);
    void clearAIConversation();
    void setAIRequestRunning(bool running);
    void applyAIProviderDefaults(bool forceBaseUrl);
    void loadAIProviderConfig(int providerIndex);
    void saveAIProviderConfig(int providerIndex);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    ListView* m_listView = nullptr;

    // Spacing sliders
    QSlider* m_rowSlider = nullptr;
    QSlider* m_colSlider = nullptr;
    QLabel* m_rowValueLabel = nullptr;
    QLabel* m_colValueLabel = nullptr;

    class QTabBar* m_tabBar = nullptr;
    class QStackedWidget* m_contentStack = nullptr;
    QGridLayout* m_generalGrid = nullptr;
    struct GeneralSection {
        QWidget* header = nullptr;
        QList<QWidget*> cards;
    };
    QList<GeneralSection> m_generalSections;
    QList<QWidget*> m_generalCards;
    QHash<QWidget*, bool> m_generalWideCards;
    QGridLayout* m_linWidgetsContentGrid = nullptr;
    QWidget* m_linWidgetsListsPanel = nullptr;
    QWidget* m_linWidgetsLogPanel = nullptr;
    QGridLayout* m_linWidgetsCompactSwitchGrid = nullptr;
    QList<QWidget*> m_linWidgetsCompactSwitchItems;
    QGridLayout* m_linWidgetsActionGroupsGrid = nullptr;
    QList<QWidget*> m_linWidgetActionGroups;

    // Toggles
    FluentToggleSwitch* m_embedToggle = nullptr;
    FluentToggleSwitch* m_persistToggle = nullptr;
    FluentToggleSwitch* m_linWidgetEnabledToggle = nullptr;
    FluentToggleSwitch* m_linWidgetBoundsToggle = nullptr;
    FluentToggleSwitch* m_linWidgetBlockedCellsToggle = nullptr;
    FluentToggleSwitch* m_defaultWallpaperToggle = nullptr;
    QCheckBox* m_linWidgetNetworkPermissionCheck = nullptr;
    QCheckBox* m_linWidgetStoragePermissionCheck = nullptr;
    QCheckBox* m_linWidgetDesktopPermissionCheck = nullptr;
    QLabel* m_openGLStatusLabel = nullptr;
    QLabel* m_linWidgetsPageStatusLabel = nullptr;
    QLabel* m_linWidgetPermissionsLabel = nullptr;
    QLabel* m_linWidgetsLogTitleLabel = nullptr;
    QPlainTextEdit* m_linWidgetsLogView = nullptr;
    QFileSystemWatcher* m_linWidgetsLogWatcher = nullptr;
    QTimer* m_linWidgetsLogReloadTimer = nullptr;
    QString m_currentLinWidgetLogPath;
    QListWidget* m_installedLinWidgetsList = nullptr;
    QListWidget* m_developmentLinWidgetsList = nullptr;
    class QTabBar* m_linWidgetListTabBar = nullptr;
    class QStackedWidget* m_linWidgetListStack = nullptr;
    QPushButton* m_linWidgetsOpenFolderButton = nullptr;
    QPushButton* m_linWidgetsOpenLogsButton = nullptr;
    QPushButton* m_linWidgetsAddDevButton = nullptr;
    QPushButton* m_linWidgetsPackageButton = nullptr;
    QPushButton* m_linWidgetsResetBundledButton = nullptr;
    QPushButton* m_linWidgetsRemoveDevButton = nullptr;
    QPushButton* m_linWidgetsDeleteButton = nullptr;
    QString m_lastLinWidgetInstanceId;
    QHash<QString, QString> m_linWidgetInstancesByPackage;
    QHash<QString, QString> m_linWidgetInstancesById;

    // AI debug
    AI::AICoreService* m_aiCoreService = nullptr;
    QComboBox* m_aiProviderCombo = nullptr;
    QLineEdit* m_aiBaseUrlEdit = nullptr;
    QLineEdit* m_aiApiKeyEdit = nullptr;
    QComboBox* m_aiModelCombo = nullptr;
    QPlainTextEdit* m_aiSystemPromptEdit = nullptr;
    QPlainTextEdit* m_aiUserPromptEdit = nullptr;
    QPlainTextEdit* m_aiOutputView = nullptr;
    QLabel* m_aiStatusLabel = nullptr;
    QPushButton* m_aiSaveButton = nullptr;
    QPushButton* m_aiSendButton = nullptr;
    QPushButton* m_aiStreamButton = nullptr;
    QPushButton* m_aiStopButton = nullptr;
    QPushButton* m_aiClearButton = nullptr;
    QString m_aiActiveRequestId;
    QList<AI::ChatMessage> m_aiConversationMessages;
    QString m_aiStreamingAssistantText;
    int m_aiCurrentProviderIndex = 0;
};
