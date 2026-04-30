#pragma once

#include <QWidget>

class QLabel;
class QCheckBox;
class QPushButton;
class QSlider;
class ListView;
class SoftDesktopTextPreview;
class FluentToggleSwitch;

class ListViewDebugPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ListViewDebugPanel(ListView* listView, QWidget* parent = nullptr);

private:
    void initUI();
    void updateLabels();
    void updateOpenGLStatus();
    void applyTextPreviewOptions();
    QPushButton* createActionButton(const QString& text, bool accent = false, bool destructive = false) const;
    QWidget* createSliderSettingRow(const QString& labelText,
                                    int minValue,
                                    int maxValue,
                                    QSlider*& slider,
                                    QLabel*& valueLabel);
    QLabel* createPageTitleLabel(const QString& text) const;
    QLabel* createPageSubtitleLabel(const QString& text) const;

    QWidget* createGeneralPage();
    QWidget* createLayoutPage();
    QWidget* createTextDiagnosticsPage();
    QWidget* wrapScrollablePage(QWidget* content);

    ListView* m_listView = nullptr;

    // Spacing sliders
    QSlider* m_rowSlider = nullptr;
    QSlider* m_colSlider = nullptr;
    QLabel* m_rowValueLabel = nullptr;
    QLabel* m_colValueLabel = nullptr;

    class QTabBar* m_tabBar = nullptr;
    class QStackedWidget* m_contentStack = nullptr;

    // Toggles
    FluentToggleSwitch* m_embedToggle = nullptr;
    FluentToggleSwitch* m_persistToggle = nullptr;
    QLabel* m_openGLStatusLabel = nullptr;

    // Text preview
    SoftDesktopTextPreview* m_textPreview = nullptr;
    QSlider* m_largeGlowSlider = nullptr;
    QSlider* m_tightGlowSlider = nullptr;
    QSlider* m_glowAlphaSlider = nullptr;
    QSlider* m_innerAlphaSlider = nullptr;
    QSlider* m_glowOffsetXSlider = nullptr;
    QSlider* m_glowOffsetYSlider = nullptr;
    QSlider* m_innerOffsetXSlider = nullptr;
    QSlider* m_innerOffsetYSlider = nullptr;
    QLabel* m_largeGlowValueLabel = nullptr;
    QLabel* m_tightGlowValueLabel = nullptr;
    QLabel* m_glowAlphaValueLabel = nullptr;
    QLabel* m_innerAlphaValueLabel = nullptr;
    QLabel* m_glowOffsetXValueLabel = nullptr;
    QLabel* m_glowOffsetYValueLabel = nullptr;
    QLabel* m_innerOffsetXValueLabel = nullptr;
    QLabel* m_innerOffsetYValueLabel = nullptr;
};
