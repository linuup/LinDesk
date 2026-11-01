#pragma once

#include <QWidget>
#include <QColor>

class QLabel;
class QLineEdit;
class FluentToggleSwitch;

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
    QWidget* createWindows11MenuPanel(QWidget* parent);
    static void reloadSelectionOverlays();

    QColor m_selectionPreviewColor;
    QLabel* m_desktopSelectionPreview = nullptr;
    QLineEdit* m_selectionColorValueEdit = nullptr;
    QWidget* m_desktopPreviewContainer = nullptr;
    FluentToggleSwitch* m_fluentSelectionToggle = nullptr;
};
