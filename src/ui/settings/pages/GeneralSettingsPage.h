#pragma once

#include <QWidget>

class FluentToggleSwitch;
class FluentComboBox;

class GeneralSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit GeneralSettingsPage(QWidget* parent = nullptr);
    ~GeneralSettingsPage() override = default;

    void updateTranslations() {}

signals:
    void requestNavigate(int pageIndex);

private:
    void initUI();

    static QString startupShortcutPath();
    static bool isLaunchAtLoginEnabled();
    static bool setLaunchAtLoginEnabled(bool enabled);

    FluentToggleSwitch* m_autoStartToggle = nullptr;
    FluentToggleSwitch* m_trayOnlyToggle = nullptr;
    FluentToggleSwitch* m_openGLToggle = nullptr;
    FluentComboBox* m_languageCombo = nullptr;
};
