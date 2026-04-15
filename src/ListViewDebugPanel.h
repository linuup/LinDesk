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
    void updateLabels();

    ListView* m_listView = nullptr;

    // Spacing sliders
    QSlider* m_rowSlider = nullptr;
    QSlider* m_colSlider = nullptr;
    QLabel* m_rowValueLabel = nullptr;
    QLabel* m_colValueLabel = nullptr;

    // Toggles
    FluentToggleSwitch* m_embedToggle = nullptr;
    FluentToggleSwitch* m_persistToggle = nullptr;

    // Text preview
    SoftDesktopTextPreview* m_textPreview = nullptr;
};
