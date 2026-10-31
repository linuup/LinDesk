#pragma once

#include <QWidget>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QPair>

class QLabel;
class QPushButton;
class QStackedWidget;
class QListWidget;
class QListWidgetItem;
class QTabWidget;
class QPlainTextEdit;
class QCheckBox;
class QFileSystemWatcher;
class QTimer;
class FluentToggleSwitch;

namespace LinWidget {
    struct WidgetManifest;
}

class LinWidgetsSettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit LinWidgetsSettingsPage(QWidget* parent = nullptr);
    ~LinWidgetsSettingsPage() override;

    void reloadPage();
    void updateTranslations();

private:
    void initUI();
    QWidget* createWallpaperSubsystemPage(QWidget* parent);
    void refreshWallpaperSubsystemState();
    void applyWallpaperPackage(const QString& packagePath, bool isDevelopment);
    void stopActiveWallpaper();
    void reloadLinWidgetsSettingsPage();
    void refreshLinWidgetSettingsButtons();
    void refreshLinWidgetSettingsInspector();
    void openLinWidgetSettingsDrawer(QListWidgetItem* item);
    void selectAndOpenLinWidgetSettingsItem(QListWidgetItem* item);
    void refreshLinWidgetSettingsRunningMaps();
    void loadSelectedSettingsLinWidget();
    void unloadSelectedSettingsLinWidget();
    void reloadSelectedSettingsLinWidget();
    void uninstallSelectedSettingsLinWidget();
    void installOfficialSettingsLinWidget(const QString& packagePath);
    bool launchInstalledSettingsLinWidget(const QString& packagePath,
                                          const LinWidget::WidgetManifest& manifest,
                                          QString* instanceId,
                                          QString* errorMessage);
    void resetOfficialSettingsLinWidget(const QString& packagePath);
    void addDevelopmentSettingsLinWidget();
    void createDevelopmentSettingsLinWidget();
    void exportDevelopmentSettingsLinWidget();
    void deleteSelectedSettingsLinWidget();
    void setSelectedSettingsLinWidgetPermission(const QString& permission, bool granted);
    void setSettingsLinWidgetPermission(const QString& registryId,
                                        const QString& packagePath,
                                        const QString& selectedKey,
                                        const QString& permission,
                                        bool granted);

    QListWidget* activeSettingsLinWidgetList() const;

    void openDrawer(QWidget* content, const QString& title = QString(), int width = 420);
    void closeDrawer();

    QString selectedSettingsLinWidgetPackagePath() const;
    QString selectedSettingsLinWidgetRegistryId() const;
    bool selectedSettingsLinWidgetIsDevelopment() const;
    QString selectedSettingsLinWidgetInstanceId() const;
    QString selectedSettingsLinWidgetRegistryKey() const;
    QString settingsLinWidgetRegistryKey(const QString& id, bool isDevelopment) const;
    QString settingsLinWidgetPackageKey(const QString& packagePath) const;
    QString settingsLinWidgetLogPath(const QString& instanceId) const;

private:
    QLabel* m_linWidgetsPageTitle = nullptr;
    QLabel* m_linWidgetsStatusLabel = nullptr;
    QLabel* m_linWidgetsInspectorLabel = nullptr;
    QLabel* m_linWidgetsErrorsLabel = nullptr;

    QStackedWidget* m_linWidgetSubsystemStack = nullptr;
    QPushButton* m_subsystemWidgetsBtn = nullptr;
    QPushButton* m_subsystemWallpaperBtn = nullptr;
    QWidget* m_wallpaperSubsystemPage = nullptr;
    QListWidget* m_wallpaperList = nullptr;
    QLabel* m_wallpaperActiveTitle = nullptr;
    QLabel* m_wallpaperActiveDesc = nullptr;
    QLabel* m_wallpaperPreviewLabel = nullptr;
    QPushButton* m_wallpaperStopBtn = nullptr;
    QTabWidget* m_linWidgetsTabs = nullptr;
    QListWidget* m_officialLinWidgetsList = nullptr;
    QListWidget* m_marketplaceLinWidgetsList = nullptr;
    QListWidget* m_installedLinWidgetsList = nullptr;
    QListWidget* m_developmentLinWidgetsList = nullptr;
    QPlainTextEdit* m_linWidgetsLogView = nullptr;
    QCheckBox* m_linWidgetNetworkPermissionCheck = nullptr;
    QCheckBox* m_linWidgetStoragePermissionCheck = nullptr;
    QCheckBox* m_linWidgetDesktopPermissionCheck = nullptr;
    FluentToggleSwitch* m_linWidgetsRuntimeToggle = nullptr;
    QPushButton* m_linWidgetLoadButton = nullptr;
    QPushButton* m_linWidgetUnloadButton = nullptr;
    QPushButton* m_linWidgetReloadButton = nullptr;
    QPushButton* m_linWidgetOpenFolderButton = nullptr;
    QPushButton* m_linWidgetDeleteButton = nullptr;
    QPushButton* m_linWidgetAddDevButton = nullptr;
    QPushButton* m_linWidgetNewProjectButton = nullptr;
    QPushButton* m_linWidgetExportZipButton = nullptr;
    QPushButton* m_linWidgetRemoveDevButton = nullptr;
    QFileSystemWatcher* m_linWidgetsLogWatcher = nullptr;
    QTimer* m_linWidgetsLogReloadTimer = nullptr;
    QString m_currentLinWidgetLogPath;
    bool m_preserveLinWidgetDrawerOnReload = false;
    QHash<QString, QString> m_linWidgetInstancesByPackage;
    QHash<QString, QString> m_linWidgetInstancesById;
    QHash<QString, int> m_linWidgetInstanceCountsById;
};
