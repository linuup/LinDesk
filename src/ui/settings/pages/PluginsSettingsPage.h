#pragma once

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>

class PluginsSettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit PluginsSettingsPage(QWidget* parent = nullptr);
    ~PluginsSettingsPage() override = default;

    void reloadPlugins();
    void updateTranslations();

private:
    void initUI();
    void rebuildInstalledList();

private:
    QLabel* m_pluginsPageTitle = nullptr;
    QLabel* m_pluginsSubtitle = nullptr;
    QLabel* m_installedTitle = nullptr;
    QLabel* m_pluginsEmptyLabel = nullptr;
    QLabel* m_emptySubLabel = nullptr;
    QLabel* m_availableTitle = nullptr;
    QLabel* m_comingTitle = nullptr;
    QLabel* m_comingSubtitle = nullptr;
    QVBoxLayout* m_mainLayout = nullptr;
    QWidget* m_installedContainer = nullptr;
    QVBoxLayout* m_installedLayout = nullptr;
};
