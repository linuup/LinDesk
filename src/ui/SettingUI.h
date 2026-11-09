#ifndef SETTINGUI_H
#define SETTINGUI_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QPointer>
#include <QCloseEvent>
#include <QShowEvent>
#include <QResizeEvent>
#include "ui/Fluent/FluentBaseWidget.h"

class LinBox;
class FluentNavigationPanel;
class GeneralSettingsPage;
class DesktopSettingsPage;
class ContextMenuSettingsPage;
class BoxSettingsPage;
class LinWidgetsSettingsPage;
class PluginsSettingsPage;
class AboutSettingsPage;

class SettingUI : public FluentBaseWidget {
    Q_OBJECT
public:
    explicit SettingUI(LinBox* targetBox, const QVector<LinBox*>& allBoxes, QWidget* parent = nullptr);
    ~SettingUI() override = default;

    void setTargetBox(LinBox* target);
    void restoreAndActivate();
    void switchToPage(int index);

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void initUI();
    void setupStyles();
    QWidget* getOrCreatePage(int index);
    QWidget* wrapScrollablePage(QWidget* content);

    void updateWindowControlButtons();

private:
    QPointer<LinBox> m_targetBox;
    QVector<QPointer<LinBox>> m_allBoxes;

    FluentNavigationPanel* m_navPanel = nullptr;
    QStackedWidget* m_contentStack = nullptr;
    QWidget* m_pageCache[7] = { nullptr };

    QPointer<GeneralSettingsPage> m_generalPage;
    QPointer<DesktopSettingsPage> m_desktopPage;
    QPointer<ContextMenuSettingsPage> m_contextMenuPage;
    QPointer<BoxSettingsPage> m_boxPage;
    QPointer<LinWidgetsSettingsPage> m_linWidgetsPage;
    QPointer<PluginsSettingsPage> m_pluginsPage;
    QPointer<AboutSettingsPage> m_aboutPage;

    QPushButton* m_settingsMinBtn = nullptr;
    QPushButton* m_settingsMaxBtn = nullptr;
    QPushButton* m_settingsCloseBtn = nullptr;
};

#endif // SETTINGUI_H
