#include "ui/settings/pages/PluginsSettingsPage.h"
#include "ui/settings/SettingsUICommon.h"
#include "plugins/PluginManager.h"
#include "ui/Fluent/FluentToggleSwitch.h"
#include "ui/Fluent/FluentDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>

using namespace SettingsUICommon;

PluginsSettingsPage::PluginsSettingsPage(QWidget* parent)
    : QWidget(parent)
{
    initUI();
}

void PluginsSettingsPage::initUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(36, 12, 36, 32);
    m_mainLayout->setSpacing(0);
    m_mainLayout->setAlignment(Qt::AlignTop);

    m_pluginsPageTitle = createPageTitle(tr("Plugins"), this);
    m_pluginsPageTitle->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_mainLayout->addWidget(m_pluginsPageTitle, 0, Qt::AlignLeft);
    m_mainLayout->addSpacing(4);

    m_pluginsSubtitle = createPageSubtitle(tr("Extend LinDesk with powerful plugins and integrations."), this);
    m_mainLayout->addWidget(m_pluginsSubtitle);
    m_mainLayout->addSpacing(20);

    m_installedTitle = createCardTitle(tr("Installed Plugins"), this);
    m_mainLayout->addWidget(m_installedTitle);
    m_mainLayout->addSpacing(8);

    m_installedContainer = new QWidget(this);
    m_installedContainer->setObjectName(QStringLiteral("installedPluginsContainer"));
    m_installedLayout = new QVBoxLayout(m_installedContainer);
    m_installedLayout->setContentsMargins(0, 0, 0, 0);
    m_installedLayout->setSpacing(8);
    m_mainLayout->addWidget(m_installedContainer);

    rebuildInstalledList();

    m_mainLayout->addSpacing(24);
    m_availableTitle = createCardTitle(tr("Available Plugins"), this);
    m_mainLayout->addWidget(m_availableTitle);
    m_mainLayout->addSpacing(8);

    QWidget* comingPanel = new QWidget(this);
    comingPanel->setObjectName("comingSoonPanel");
    comingPanel->setAttribute(Qt::WA_StyledBackground, true);
    QHBoxLayout* comingLayout = new QHBoxLayout(comingPanel);
    comingLayout->setContentsMargins(20, 16, 20, 16);
    comingLayout->setSpacing(16);
    comingLayout->addWidget(createGlyphLabel(glyph(0xE74C), comingPanel, 18, QColor("#5c5c5c")), 0, Qt::AlignVCenter);

    QVBoxLayout* comingText = new QVBoxLayout();
    comingText->setContentsMargins(0, 0, 0, 0);
    comingText->setSpacing(2);
    m_comingTitle = new QLabel(tr("More Plugins Coming Soon"), comingPanel);
    m_comingTitle->setObjectName("rowTitle");
    m_comingSubtitle = new QLabel(tr("We're working on more powerful plugins and integrations to enhance your LinDesk experience."), comingPanel);
    m_comingSubtitle->setObjectName("rowSubtitle");
    m_comingSubtitle->setWordWrap(true);
    comingText->addWidget(m_comingTitle);
    comingText->addWidget(m_comingSubtitle);
    comingLayout->addLayout(comingText, 1);
    m_mainLayout->addWidget(comingPanel);
    m_mainLayout->addStretch(1);
}

void PluginsSettingsPage::rebuildInstalledList()
{
    if (!m_installedLayout) return;

    QLayoutItem* item = nullptr;
    while ((item = m_installedLayout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    auto allPlugins = PluginManager::instance()->allPlugins();

    if (allPlugins.isEmpty()) {
        QWidget* emptyPanel = createPanel(m_installedContainer);
        QHBoxLayout* emptyLayout = new QHBoxLayout(emptyPanel);
        emptyLayout->setContentsMargins(20, 16, 20, 16);
        emptyLayout->setSpacing(16);

        QLabel* emptyIcon = createGlyphLabel(glyph(0xE74C), emptyPanel, 18, QColor("#0067c0"));
        emptyIcon->setFixedSize(36, 36);
        emptyIcon->setStyleSheet(QStringLiteral(
            "QLabel {"
            "  background-color: #eff6fc;"
            "  color: #0067c0;"
            "  border-radius: 8px;"
            "  font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets';"
            "  font-size: 18px;"
            "}"));
        emptyLayout->addWidget(emptyIcon, 0, Qt::AlignVCenter);

        QVBoxLayout* emptyText = new QVBoxLayout();
        emptyText->setContentsMargins(0, 0, 0, 0);
        emptyText->setSpacing(2);
        m_pluginsEmptyLabel = new QLabel(tr("No Installed Plugins"), emptyPanel);
        m_pluginsEmptyLabel->setObjectName("rowTitle");
        m_emptySubLabel = new QLabel(tr("Install plugin packages to extend LinDesk capabilities."), emptyPanel);
        m_emptySubLabel->setObjectName("rowSubtitle");
        m_emptySubLabel->setWordWrap(true);
        emptyText->addWidget(m_pluginsEmptyLabel);
        emptyText->addWidget(m_emptySubLabel);
        emptyLayout->addLayout(emptyText, 1);
        m_installedLayout->addWidget(emptyPanel);
    } else {
        for (auto* plugin : allPlugins) {
            if (!plugin) continue;

            QWidget* pluginPanel = createPanel(m_installedContainer);
            QVBoxLayout* pluginLayout = new QVBoxLayout(pluginPanel);
            pluginLayout->setContentsMargins(20, 16, 20, 16);
            pluginLayout->setSpacing(14);

            QHBoxLayout* topLayout = new QHBoxLayout();
            topLayout->setContentsMargins(0, 0, 0, 0);
            topLayout->setSpacing(16);

            uint iconGlyph = 0xE74C;
            if (plugin->pluginId().contains(QStringLiteral("ocr"), Qt::CaseInsensitive)) {
                iconGlyph = 0xE914; // Document / Scan / OCR icon
            }

            QLabel* pluginIcon = createGlyphLabel(glyph(iconGlyph), pluginPanel, 20, QColor("#0067c0"));
            pluginIcon->setObjectName("pluginLargeIcon");
            pluginIcon->setFixedSize(44, 44);
            pluginIcon->setAlignment(Qt::AlignCenter);
            pluginIcon->setStyleSheet(QStringLiteral(
                "QLabel#pluginLargeIcon {"
                "  background-color: #eff6fc;"
                "  color: #0067c0;"
                "  border: 1px solid #d0e4f7;"
                "  border-radius: 10px;"
                "  font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets';"
                "  font-size: 20px;"
                "}"
            ));
            topLayout->addWidget(pluginIcon, 0, Qt::AlignVCenter);

            QVBoxLayout* pluginText = new QVBoxLayout();
            pluginText->setContentsMargins(0, 0, 0, 0);
            pluginText->setSpacing(4);
            QLabel* pluginName = new QLabel(plugin->pluginName(), pluginPanel);
            pluginName->setObjectName("rowTitle");
            QLabel* pluginDescription = new QLabel(plugin->pluginDescription(), pluginPanel);
            pluginDescription->setObjectName("rowSubtitle");
            pluginDescription->setWordWrap(true);
            pluginText->addWidget(pluginName);
            pluginText->addWidget(pluginDescription);

            QHBoxLayout* badgeLayout = new QHBoxLayout();
            badgeLayout->setContentsMargins(0, 4, 0, 0);
            badgeLayout->setSpacing(8);
            const bool configured = plugin->isConfigured();
            const bool pluginEnabled = PluginManager::instance()->isPluginEnabled(plugin->pluginId());
            badgeLayout->addWidget(createPluginBadge(glyph(0xE73E), tr("Installed"), pluginPanel, QColor("#16a34a")));
            if (!configured) {
                badgeLayout->addWidget(createPluginBadge(glyph(0xE7BA), tr("Not Configured"), pluginPanel, QColor("#d97706")));
            }
            badgeLayout->addWidget(createPluginBadge(glyph(0xEA3B), pluginEnabled ? tr("Enabled") : tr("Disabled"), pluginPanel, pluginEnabled ? QColor("#0067c0") : QColor("#6b7280")));

            // Show hotkey badges if configured
            if (configured && pluginEnabled) {
                QString hk = plugin->customHotkey();
                if (!hk.isEmpty()) {
                    badgeLayout->addWidget(createPluginBadge(glyph(0xE765), QStringLiteral("识别: %1").arg(hk), pluginPanel, QColor("#0067c0")));
                }
                QString sHk = plugin->customSecondaryHotkey();
                if (!sHk.isEmpty()) {
                    badgeLayout->addWidget(createPluginBadge(glyph(0xE722), QStringLiteral("截图: %1").arg(sHk), pluginPanel, QColor("#0284c7")));
                }
            }

            badgeLayout->addStretch();
            pluginText->addLayout(badgeLayout);
            topLayout->addLayout(pluginText, 1);

            FluentToggleSwitch* toggle = new FluentToggleSwitch(pluginPanel);
            toggle->setChecked(pluginEnabled, false);
            QString pId = plugin->pluginId();
            connect(toggle, &FluentToggleSwitch::toggled, this, [this, toggle, plugin, pId](bool checked) {
                if (checked && !plugin->isConfigured()) {
                    toggle->setChecked(false, false);
                    FluentDialog::showWarning(this, tr("提示"), tr("该插件尚未配置，请点击【设置】完成配置后再开启。"));
                    return;
                }
                PluginManager::instance()->setPluginEnabled(pId, checked);
                rebuildInstalledList();
            });
            topLayout->addWidget(toggle, 0, Qt::AlignVCenter);

            QPushButton* settingsBtn = createSettingsActionButton(tr("Settings"), pluginPanel);
            connect(settingsBtn, &QPushButton::clicked, this, [this, plugin, pId]() {
                const bool wasConfigured = plugin->isConfigured();
                plugin->onSettingsRequested(this);
                const bool nowConfigured = plugin->isConfigured();
                if (!wasConfigured && nowConfigured) {
                    PluginManager::instance()->setPluginEnabled(pId, true);
                } else if (!nowConfigured) {
                    PluginManager::instance()->setPluginEnabled(pId, false);
                }
                rebuildInstalledList();
            });
            topLayout->addWidget(settingsBtn, 0, Qt::AlignVCenter);
            pluginLayout->addLayout(topLayout);

            m_installedLayout->addWidget(pluginPanel);
        }
    }
}

void PluginsSettingsPage::reloadPlugins()
{
    rebuildInstalledList();
}

void PluginsSettingsPage::updateTranslations()
{
    if (m_pluginsPageTitle) m_pluginsPageTitle->setText(tr("Plugins"));
    if (m_pluginsSubtitle) m_pluginsSubtitle->setText(tr("Extend LinDesk with powerful plugins and integrations."));
    if (m_installedTitle) m_installedTitle->setText(tr("Installed Plugins"));
    if (m_pluginsEmptyLabel) m_pluginsEmptyLabel->setText(tr("No Installed Plugins"));
    if (m_emptySubLabel) m_emptySubLabel->setText(tr("Install plugin packages to extend LinDesk capabilities."));
    if (m_availableTitle) m_availableTitle->setText(tr("Available Plugins"));
    if (m_comingTitle) m_comingTitle->setText(tr("More Plugins Coming Soon"));
    if (m_comingSubtitle) m_comingSubtitle->setText(tr("We're working on more powerful plugins and integrations to enhance your LinDesk experience."));
    rebuildInstalledList();
}
