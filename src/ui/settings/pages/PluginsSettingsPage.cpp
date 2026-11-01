#include "ui/settings/pages/PluginsSettingsPage.h"
#include "ui/settings/SettingsUICommon.h"
#include "plugins/PluginManager.h"
#include "ui/Fluent/FluentToggleSwitch.h"
#include <QScrollArea>
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
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet("background: #fbfcfe;");

    QVBoxLayout* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("QScrollArea { background-color: #fbfcfe; border: none; }");
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    if (scroll->viewport()) {
        scroll->viewport()->setAutoFillBackground(true);
        QPalette p = scroll->viewport()->palette();
        p.setColor(QPalette::Window, QColor(251, 252, 254));
        scroll->viewport()->setPalette(p);
    }

    m_contentWidget = new QWidget(scroll);
    m_mainLayout = new QVBoxLayout(m_contentWidget);
    m_mainLayout->setContentsMargins(54, 34, 54, 36);
    m_mainLayout->setSpacing(0);
    m_mainLayout->setAlignment(Qt::AlignTop);

    m_pluginsPageTitle = createPageTitle(tr("Plugins"), m_contentWidget);
    m_pluginsPageTitle->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_mainLayout->addWidget(m_pluginsPageTitle, 0, Qt::AlignLeft);
    m_mainLayout->addSpacing(8);

    m_pluginsSubtitle = createPageSubtitle(tr("Extend LinDesk with powerful plugins and integrations."), m_contentWidget);
    m_mainLayout->addWidget(m_pluginsSubtitle);
    m_mainLayout->addSpacing(8);

    m_installedTitle = createCardTitle(tr("Installed Plugins"), m_contentWidget);
    m_mainLayout->addWidget(m_installedTitle);
    m_mainLayout->addSpacing(8);

    auto allPlugins = PluginManager::instance()->allPlugins();

    if (allPlugins.isEmpty()) {
        QWidget* emptyPanel = createPanel(m_contentWidget);
        QHBoxLayout* emptyLayout = new QHBoxLayout(emptyPanel);
        emptyLayout->setContentsMargins(22, 22, 22, 22);
        emptyLayout->setSpacing(16);
        emptyLayout->addWidget(createGlyphLabel(glyph(0xE74C), emptyPanel, 38, QColor("#8b96aa")), 0, Qt::AlignVCenter);
        
        QVBoxLayout* emptyText = new QVBoxLayout();
        emptyText->setContentsMargins(0, 0, 0, 0);
        emptyText->setSpacing(4);
        m_pluginsEmptyLabel = new QLabel(tr("No Installed Plugins"), emptyPanel);
        m_pluginsEmptyLabel->setObjectName("pluginName");
        m_emptySubLabel = new QLabel(tr("Install plugin packages to extend LinDesk capabilities."), emptyPanel);
        m_emptySubLabel->setObjectName("rowSubtitle");
        m_emptySubLabel->setWordWrap(true);
        emptyText->addWidget(m_pluginsEmptyLabel);
        emptyText->addWidget(m_emptySubLabel);
        emptyLayout->addLayout(emptyText, 1);
        m_mainLayout->addWidget(emptyPanel);
    } else {
        for (auto* plugin : allPlugins) {
            if (!plugin) continue;

            QWidget* pluginPanel = createPanel(m_contentWidget);
            QVBoxLayout* pluginLayout = new QVBoxLayout(pluginPanel);
            pluginLayout->setContentsMargins(24, 22, 24, 22);
            pluginLayout->setSpacing(18);

            QHBoxLayout* topLayout = new QHBoxLayout();
            topLayout->setContentsMargins(0, 0, 0, 0);
            topLayout->setSpacing(18);

            QLabel* pluginIcon = createGlyphLabel(glyph(0xE74C), pluginPanel, 42);
            pluginIcon->setObjectName("pluginLargeIcon");
            topLayout->addWidget(pluginIcon, 0, Qt::AlignTop);

            QVBoxLayout* pluginText = new QVBoxLayout();
            pluginText->setContentsMargins(0, 0, 0, 0);
            pluginText->setSpacing(6);
            QLabel* pluginName = new QLabel(plugin->pluginName(), pluginPanel);
            pluginName->setObjectName("pluginName");
            QLabel* pluginDescription = new QLabel(plugin->pluginDescription(), pluginPanel);
            pluginDescription->setObjectName("pluginDescription");
            pluginDescription->setWordWrap(true);
            pluginText->addWidget(pluginName);
            pluginText->addWidget(pluginDescription);

            QHBoxLayout* badgeLayout = new QHBoxLayout();
            badgeLayout->setContentsMargins(0, 0, 0, 0);
            badgeLayout->setSpacing(8);
            const bool pluginEnabled = PluginManager::instance()->isPluginEnabled(plugin->pluginId());
            badgeLayout->addWidget(createPluginBadge(glyph(0xE73E), tr("Installed"), pluginPanel, QColor("#16a34a")));
            badgeLayout->addWidget(createPluginBadge(glyph(0xEA3B), pluginEnabled ? tr("Enabled") : tr("Disabled"), pluginPanel, pluginEnabled ? QColor("#0078d4") : QColor("#6b7280")));
            badgeLayout->addStretch();
            pluginText->addLayout(badgeLayout);
            topLayout->addLayout(pluginText, 1);

            FluentToggleSwitch* toggle = new FluentToggleSwitch(pluginPanel);
            toggle->setChecked(pluginEnabled, false);
            QString pId = plugin->pluginId();
            connect(toggle, &FluentToggleSwitch::toggled, this, [pId](bool checked) {
                PluginManager::instance()->setPluginEnabled(pId, checked);
            });
            topLayout->addWidget(toggle, 0, Qt::AlignVCenter);

            QPushButton* settingsBtn = new QPushButton(tr("Settings"), pluginPanel);
            settingsBtn->setObjectName("pluginSettingsButton");
            settingsBtn->setFixedSize(86, 36);
            settingsBtn->setCursor(Qt::PointingHandCursor);
            connect(settingsBtn, &QPushButton::clicked, this, [plugin]() {
                plugin->onSettingsRequested();
                PluginManager::instance()->registerPluginHotkey(plugin);
            });
            topLayout->addWidget(settingsBtn, 0, Qt::AlignVCenter);
            pluginLayout->addLayout(topLayout);

            m_mainLayout->addWidget(pluginPanel);
            m_mainLayout->addSpacing(16);
        }
    }

    m_mainLayout->addSpacing(12);
    m_availableTitle = createCardTitle(tr("Available Plugins"), m_contentWidget);
    m_mainLayout->addWidget(m_availableTitle);
    m_mainLayout->addSpacing(8);

    QWidget* comingPanel = new QWidget(m_contentWidget);
    comingPanel->setObjectName("comingSoonPanel");
    QHBoxLayout* comingLayout = new QHBoxLayout(comingPanel);
    comingLayout->setContentsMargins(26, 22, 26, 22);
    comingLayout->setSpacing(18);
    comingLayout->addWidget(createGlyphLabel(glyph(0xE74C), comingPanel, 34, QColor("#8b96aa")), 0, Qt::AlignVCenter);
    
    QVBoxLayout* comingText = new QVBoxLayout();
    comingText->setContentsMargins(0, 0, 0, 0);
    comingText->setSpacing(4);
    m_comingTitle = new QLabel(tr("More Plugins Coming Soon"), comingPanel);
    m_comingTitle->setObjectName("rowTitle");
    m_comingSubtitle = new QLabel(tr("We're working on more powerful plugins and integrations to enhance your LinDesk experience."), comingPanel);
    m_comingSubtitle->setObjectName("rowSubtitle");
    m_comingSubtitle->setWordWrap(true);
    comingText->addWidget(m_comingTitle);
    comingText->addWidget(m_comingSubtitle);
    comingLayout->addLayout(comingText, 1);
    m_mainLayout->addWidget(comingPanel);

    scroll->setWidget(m_contentWidget);
    outerLayout->addWidget(scroll);
}

void PluginsSettingsPage::reloadPlugins()
{
    // Re-initialize content if needed
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
}
