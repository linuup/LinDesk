#pragma once

#include <QWidget>
#include <QString>
#include <QPointer>

class QLabel;
class QPushButton;
class QProgressBar;
class QStackedWidget;
class FluentToggleSwitch;

class LinWidgetsSettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit LinWidgetsSettingsPage(QWidget* parent = nullptr);
    ~LinWidgetsSettingsPage() override;

    void reloadPage();
    void updateTranslations();

private slots:
    void onInstallRuntimeClicked();
    void onInstallFromZipClicked();
    void onUninstallRuntimeClicked();
    void onOpenHubClicked();
    void onClearCacheClicked();
    void updateRuntimeStatusView();

private:
    void initUI();
    QWidget* createOnboardingView(QWidget* parent);
    QWidget* createInstalledView(QWidget* parent);

    // Header
    QLabel* m_pageTitle = nullptr;
    QLabel* m_pageSubtitle = nullptr;

    // View stack (0 = Onboarding Uninstalled View, 1 = Full Installed View)
    QStackedWidget* m_viewStack = nullptr;
    QWidget* m_onboardingView = nullptr;
    QWidget* m_installedView = nullptr;

    // Onboarding View controls
    QPushButton* m_onboardingInstallBtn = nullptr;
    QPushButton* m_onboardingZipBtn = nullptr;
    QProgressBar* m_onboardingProgressBar = nullptr;
    QLabel* m_onboardingStageLabel = nullptr;

    // Installed View controls
    QLabel* m_hubStatusSubtitle = nullptr;
    QPushButton* m_openHubBtn = nullptr;
    FluentToggleSwitch* m_masterToggle = nullptr;

    // Performance toggles
    FluentToggleSwitch* m_gpuAccelToggle = nullptr;
    FluentToggleSwitch* m_fullscreenPauseToggle = nullptr;
    FluentToggleSwitch* m_batterySaverToggle = nullptr;

    // Storage & Runtime maintenance controls
    QLabel* m_cacheStatusSubtitle = nullptr;
    QPushButton* m_clearCacheBtn = nullptr;
    QLabel* m_runtimeStatusSubtitle = nullptr;
    QPushButton* m_installZipBtn = nullptr;
    QPushButton* m_uninstallBtn = nullptr;
    QProgressBar* m_installProgressBar = nullptr;
    QLabel* m_progressStageLabel = nullptr;
};
