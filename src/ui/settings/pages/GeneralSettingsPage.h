#pragma once

#include <QWidget>

class QLabel;
class FluentToggleSwitch;
class FluentComboBox;

class GeneralSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit GeneralSettingsPage(QWidget* parent = nullptr);
    ~GeneralSettingsPage() override = default;

    void updateTranslations();

signals:
    void requestNavigate(int pageIndex);

private:
    void initUI();

    static QString startupShortcutPath();
    static bool isLaunchAtLoginEnabled();
    static bool setLaunchAtLoginEnabled(bool enabled);

    QLabel* m_pageTitle = nullptr;
    QLabel* m_pageSubtitle = nullptr;
    FluentToggleSwitch* m_autoStartToggle = nullptr;
    FluentToggleSwitch* m_trayOnlyToggle = nullptr;
    FluentToggleSwitch* m_openGLToggle = nullptr;
    FluentComboBox* m_languageCombo = nullptr;
};
