#pragma once

#include <QWidget>
#include <QColor>

class QLabel;
class QLineEdit;
class QSlider;
class FluentToggleSwitch;
class FluentSlider;
class QTimer;

class DesktopSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit DesktopSettingsPage(QWidget* parent = nullptr);
    ~DesktopSettingsPage() override = default;

    void updateTranslations() {}

    void updatePreview();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void initUI();
    static void reloadSelectionOverlays();
    void queueSettingsSync();
    QTimer* m_settingsSyncTimer = nullptr;

    QColor m_selectionPreviewColor;
    QLabel* m_desktopSelectionPreview = nullptr;
    QLineEdit* m_selectionColorValueEdit = nullptr;
    QWidget* m_desktopPreviewContainer = nullptr;
    FluentToggleSwitch* m_fluentSelectionToggle = nullptr;
    FluentToggleSwitch* m_selectionCountBadgeToggle = nullptr;
    FluentSlider* m_marqueeRadiusSlider = nullptr;
    QLabel* m_marqueeRadiusValue = nullptr;
    FluentSlider* m_marqueeBorderSlider = nullptr;
    QLabel* m_marqueeBorderValue = nullptr;
    FluentSlider* m_badgeRadiusSlider = nullptr;
    QLabel* m_badgeRadiusValue = nullptr;
    QWidget* m_marqueeRadiusRow = nullptr;
    QWidget* m_badgeRadiusRow = nullptr;
    QWidget* m_advancedContainer = nullptr;
    QLabel* m_advancedChevron = nullptr;
};
