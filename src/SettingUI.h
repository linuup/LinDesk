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
#include "Fluent/FluentBaseWidget.h"
#include <QCloseEvent>
#include <QTextEdit>

class LinBox;
class QStackedWidget;
class FluentToggleSwitch;

class SettingUI : public FluentBaseWidget {
    Q_OBJECT
public:
    explicit SettingUI(LinBox* targetBox, const QVector<LinBox*>& allBoxes, QWidget* parent = nullptr);
    void setTargetBox(LinBox* target);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void initUI();
    void setupStyles();
    
    // Page Creation Helpers
    QWidget* createGeneralPage();
    QWidget* createDesktopPage();
    QWidget* createGridPage();
    QWidget* createComponentsPage();

    QWidget* createPluginsPage();
    QWidget* createConsolePage();
    QWidget* createAboutPage();

    void updateControlsFromTarget();

    LinBox* m_targetBox;
    QVector<LinBox*> m_allBoxes;
    
    // UI Infrastructure
    QListWidget* m_sidebar;
    QStackedWidget* m_contentStack;
    
    // Grid Page Controls
    QSlider* m_opacitySlider = nullptr;
    QLabel* m_opacityLabelValue = nullptr;
    FluentToggleSwitch* m_glassToggle = nullptr;
    
    // Console Page Control
    QTextEdit* m_logView = nullptr;
};

#endif // SETTINGUI_H
