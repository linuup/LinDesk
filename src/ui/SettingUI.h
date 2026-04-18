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
#include "ui/Fluent/FluentBaseWidget.h"
#include <QCloseEvent>

class LinBox;
class QStackedWidget;
class FluentToggleSwitch;
class FluentCard;
class FluentComboBox;

class SettingUI : public FluentBaseWidget {
    Q_OBJECT
public:
    explicit SettingUI(LinBox* targetBox, const QVector<LinBox*>& allBoxes, QWidget* parent = nullptr);
    void setTargetBox(LinBox* target);

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void initUI();
    void setupStyles();
    void retranslateUi();
    
    // Page Creation Helpers
    QWidget* createGeneralPage();
    QWidget* createDesktopPage();
    QWidget* createGridPage();

    QWidget* createPluginsPage();
    QWidget* createAboutPage();
    QWidget* wrapScrollablePage(QWidget* content);

    void updateControlsFromTarget();
    void updatePreviewStyle();
    LinBox* controlSourceBox() const;

    LinBox* m_targetBox;
    QVector<LinBox*> m_allBoxes;
    
    // UI Infrastructure
    QLabel* m_titleBarTitle = nullptr;
    QListWidget* m_sidebar;
    QStackedWidget* m_contentStack;
    QListWidgetItem* m_generalTabItem = nullptr;
    QListWidgetItem* m_desktopTabItem = nullptr;
    QListWidgetItem* m_boxTabItem = nullptr;
    QListWidgetItem* m_pluginsTabItem = nullptr;
    QListWidgetItem* m_aboutTabItem = nullptr;

    QLabel* m_generalPageTitle = nullptr;
    QLabel* m_desktopPageTitle = nullptr;
    QLabel* m_boxPageTitle = nullptr;
    QLabel* m_aboutNameLabel = nullptr;
    QLabel* m_aboutVersionLabel = nullptr;
    QLabel* m_aboutTaglineLabel = nullptr;
    QLabel* m_pluginsPageTitle = nullptr;
    QLabel* m_pluginsEmptyLabel = nullptr;
    QLabel* m_previewLabel = nullptr;

    FluentCard* m_autoStartCard = nullptr;
    FluentCard* m_trayOnlyCard = nullptr;
    FluentCard* m_languageCard = nullptr;
    FluentCard* m_shortcutArrowCard = nullptr;
    FluentCard* m_hideIconsCard = nullptr;
    FluentCard* m_colorCard = nullptr;
    FluentCard* m_opacityCard = nullptr;
    FluentCard* m_glassCard = nullptr;

    FluentComboBox* m_languageCombo = nullptr;
    
    // Grid Page Controls
    QSlider* m_opacitySlider = nullptr;
    QLabel* m_opacityLabelValue = nullptr;
    FluentToggleSwitch* m_glassToggle = nullptr;
    QLabel* m_previewBackground = nullptr;
    
    // Preview sync state (directly from controls)
    QColor m_previewColor;
    float m_previewOpacity = 0.5f;
    bool m_previewGlass = false;
};

#endif // SETTINGUI_H
