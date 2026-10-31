#pragma once

#include <QWidget>
#include <QPointer>
#include <QVector>
#include <QColor>

class LinBox;
class QLabel;
class QSlider;
class QLineEdit;
class FluentToggleSwitch;

class BoxSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit BoxSettingsPage(LinBox* targetBox, const QVector<LinBox*>& allBoxes, QWidget* parent = nullptr);
    ~BoxSettingsPage() override = default;

    void updateTranslations() {}

    void setTargetBox(LinBox* target);
    void setAllBoxes(const QVector<LinBox*>& boxes);
    void updateControlsFromTarget();
    void updatePreview();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void initUI();
    LinBox* controlSourceBox() const;

    QPointer<LinBox> m_targetBox;
    QVector<QPointer<LinBox>> m_allBoxes;

    QColor m_previewColor;
    float m_previewOpacity = 0.45f;
    bool m_previewGlass = false;
    bool m_previewLayeredDepth = true;
    bool m_previewDragPulse = true;

    QLabel* m_previewBackground = nullptr;
    QLineEdit* m_boxColorValueEdit = nullptr;
    QWidget* m_boxColorPresetsRow = nullptr;
    QSlider* m_opacitySlider = nullptr;
    QLabel* m_opacityLabelValue = nullptr;
    FluentToggleSwitch* m_glassToggle = nullptr;
    FluentToggleSwitch* m_layeredDepthToggle = nullptr;
    FluentToggleSwitch* m_dragPulseToggle = nullptr;
    QWidget* m_boxPreviewContainer = nullptr;
};
