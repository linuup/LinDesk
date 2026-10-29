#ifndef SETTINGUI_H
#define SETTINGUI_H

#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QColor>
#include <QGraphicsDropShadowEffect>
#include <QMouseEvent>
#include <QStyleOption>
#include <QPainter>
#include <QListWidget>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QPointer>
#include <QHash>
#include "services/update/UpdateTypes.h"
#include "ui/Fluent/FluentBaseWidget.h"
#include <QCloseEvent>

class LinBox;
class QCheckBox;
class QFileSystemWatcher;
class QPlainTextEdit;
class QProgressBar;
class QStackedWidget;
class QTabWidget;
class QTimer;
class UpdateChecker;
class UpdateDownloader;
class FluentToggleSwitch;
class FluentCard;
class FluentComboBox;
class FluentColorWheel;
namespace LinWidget {
struct WidgetManifest;
}

class SettingUI : public FluentBaseWidget {
    Q_OBJECT
public:
    explicit SettingUI(LinBox* targetBox, const QVector<LinBox*>& allBoxes, QWidget* parent = nullptr);
    void setTargetBox(LinBox* target);
    void restoreAndActivate();

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void initUI();
    void setupStyles();
    void updateSidebarItemHeights();
    void scheduleResizeRefresh(int delayMs = 24);
    void refreshAfterResize();
    int livePreviewHeightForWindow() const;
    void updateLivePreviewHeights();
    void syncScrollablePageWidths();
    void updateWindowControlButtons();
    QString startupShortcutPath() const;
    bool isLaunchAtLoginEnabled() const;
    bool setLaunchAtLoginEnabled(bool enabled);

    QWidget* createGeneralPage();
    QWidget* createDesktopPage();
    QWidget* createWindows11MenuPanel(QWidget* page);
    QWidget* createContextMenuPage();
    QWidget* createGridPage();
    QWidget* createLinWidgetsPage();
    QWidget* createPluginsPage();
    QWidget* createAboutPage();
    QWidget* wrapScrollablePage(QWidget* content);
    QWidget* getOrCreatePage(int index);

    void updateControlsFromTarget();
    void updatePreviewStyle();
    void updateDesktopSelectionPreview();
    LinBox* controlSourceBox() const;
    bool shouldMinimizeToTray() const;
    void ensureTrayIcon();
    void restoreFromTray();
    void exitFromTray();
    void startUpdateCheck();
    void startUpdateDownload();
    void installDownloadedUpdate();
    void resetUpdatePanel();
    void reloadLinWidgetsSettingsPage();
    void refreshLinWidgetSettingsButtons();
    void refreshLinWidgetSettingsInspector();
    void refreshLinWidgetSettingsRunningMaps();
    void openLinWidgetSettingsDrawer(QListWidgetItem* item);
    void selectAndOpenLinWidgetSettingsItem(QListWidgetItem* item);
    void loadSelectedSettingsLinWidget();
    void unloadSelectedSettingsLinWidget();
    void reloadSelectedSettingsLinWidget();
    void uninstallSelectedSettingsLinWidget();
    void installOfficialSettingsLinWidget(const QString& packagePath);
    void resetOfficialSettingsLinWidget(const QString& packagePath);
    bool launchInstalledSettingsLinWidget(const QString& packagePath,
                                          const LinWidget::WidgetManifest& manifest,
                                          QString* instanceId,
                                          QString* errorMessage);
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
    QString selectedSettingsLinWidgetPackagePath() const;
    QString selectedSettingsLinWidgetRegistryId() const;
    bool selectedSettingsLinWidgetIsDevelopment() const;
    QString selectedSettingsLinWidgetInstanceId() const;
    QString selectedSettingsLinWidgetRegistryKey() const;
    QString settingsLinWidgetRegistryKey(const QString& id, bool isDevelopment) const;
    QString settingsLinWidgetPackageKey(const QString& packagePath) const;
    QString settingsLinWidgetLogPath(const QString& instanceId) const;
    bool m_preserveLinWidgetDrawerOnReload = false;
    QWidget* createWallpaperSubsystemPage(QWidget* parent);
    void refreshWallpaperSubsystemState();
    void applyWallpaperPackage(const QString& packagePath, bool isDevelopment);
    void stopActiveWallpaper();

    QPointer<LinBox> m_targetBox;
    QVector<QPointer<LinBox>> m_allBoxes;

    QLabel* m_titleBarTitle = nullptr;
    QListWidget* m_sidebar = nullptr;
    QStackedWidget* m_contentStack = nullptr;
    QWidget* m_pageCache[7] = { nullptr };
    QListWidgetItem* m_generalTabItem = nullptr;
    QListWidgetItem* m_desktopTabItem = nullptr;
    QListWidgetItem* m_contextMenuTabItem = nullptr;
    QPointer<QListWidget> m_shellTypeList;
    QListWidgetItem* m_boxTabItem = nullptr;
    QListWidgetItem* m_linWidgetsTabItem = nullptr;
    QListWidgetItem* m_pluginsTabItem = nullptr;
    QListWidgetItem* m_aboutTabItem = nullptr;

    QLabel* m_generalPageTitle = nullptr;
    QLabel* m_desktopPageTitle = nullptr;
    QLabel* m_contextMenuPageTitle = nullptr;
    QLabel* m_boxPageTitle = nullptr;
    QLabel* m_aboutNameLabel = nullptr;
    QLabel* m_aboutVersionLabel = nullptr;
    QLabel* m_pluginsPageTitle = nullptr;
    QLabel* m_linWidgetsPageTitle = nullptr;
    QLabel* m_linWidgetsStatusLabel = nullptr;
    QLabel* m_linWidgetsInspectorLabel = nullptr;
    QLabel* m_linWidgetsErrorsLabel = nullptr;
    QLabel* m_pluginsEmptyLabel = nullptr;
    QLabel* m_previewLabel = nullptr;
    QLabel* m_previewBackground = nullptr;
    QLabel* m_desktopSelectionPreview = nullptr;
    QWidget* m_desktopPreviewContainer = nullptr;
    QWidget* m_boxPreviewContainer = nullptr;
    QWidget* m_desktopSelectionPanel = nullptr;
    QLabel* m_selectionColorSectionTitle = nullptr;
    QLabel* m_selectionColorSectionSubtitle = nullptr;
    QLabel* m_boxColorSectionTitle = nullptr;
    QLabel* m_boxColorSectionSubtitle = nullptr;
    QWidget* m_boxColorPresetsRow = nullptr;

    FluentCard* m_autoStartCard = nullptr;
    FluentCard* m_trayOnlyCard = nullptr;
    FluentCard* m_languageCard = nullptr;
    FluentCard* m_openGLCard = nullptr;
    FluentCard* m_selectionColorCard = nullptr;
    FluentCard* m_shortcutArrowCard = nullptr;
    FluentCard* m_hideIconsCard = nullptr;
    FluentCard* m_windows11ContextMenuCard = nullptr;
    FluentCard* m_colorCard = nullptr;
    FluentCard* m_opacityCard = nullptr;
    FluentCard* m_glassCard = nullptr;

    FluentComboBox* m_languageCombo = nullptr;
    FluentToggleSwitch* m_autoStartToggle = nullptr;
    FluentToggleSwitch* m_openGLToggle = nullptr;
    FluentToggleSwitch* m_windows11ContextMenuToggle = nullptr;
    FluentColorWheel* m_selectionColorWheel = nullptr;
    QLineEdit* m_selectionColorValueEdit = nullptr;
    QLineEdit* m_boxColorValueEdit = nullptr;

    QSlider* m_opacitySlider = nullptr;
    QLabel* m_opacityLabelValue = nullptr;
    FluentToggleSwitch* m_glassToggle = nullptr;

    QColor m_previewColor;
    float m_previewOpacity = 0.45f;
    bool m_previewGlass = false;
    QColor m_selectionPreviewColor;
    QSystemTrayIcon* m_trayIcon = nullptr;
    QMenu* m_trayMenu = nullptr;
    QAction* m_showTrayAction = nullptr;
    QAction* m_exitTrayAction = nullptr;
    UpdateChecker* m_updateChecker = nullptr;
    UpdateDownloader* m_updateDownloader = nullptr;
    UpdateInfo m_pendingUpdate;
    QString m_downloadedInstallerPath;
    QLabel* m_updateStatusLabel = nullptr;
    QLabel* m_updateNotesLabel = nullptr;
    QProgressBar* m_updateProgressBar = nullptr;
    QPushButton* m_checkUpdateButton = nullptr;
    QPushButton* m_settingsMinBtn = nullptr;
    QPushButton* m_settingsMaxBtn = nullptr;
    QPushButton* m_settingsCloseBtn = nullptr;
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
    QHash<QString, QString> m_linWidgetInstancesByPackage;
    QHash<QString, QString> m_linWidgetInstancesById;
    QHash<QString, int> m_linWidgetInstanceCountsById;
    QTimer* m_resizeRefreshTimer = nullptr;
    bool m_isQuittingFromTray = false;
    bool m_hasShownTrayHint = false;
};

#endif // SETTINGUI_H
