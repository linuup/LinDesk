#include "ui/settings/pages/AboutSettingsPage.h"
#include "ui/settings/SettingsUICommon.h"
#include "ui/Fluent/FluentDialog.h"
#include "app/LanguageManager.h"
#include "Version.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QIcon>

using namespace SettingsUICommon;

AboutSettingsPage::AboutSettingsPage(QWidget* parent)
    : QWidget(parent)
{
    initUI();
}

void AboutSettingsPage::initUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(36, 12, 36, 32);
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignTop);

    m_pageTitle = createPageTitle(tr("About"), this);
    m_pageTitle->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(m_pageTitle, 0, Qt::AlignLeft);
    layout->addSpacing(4);

    m_pageSubtitle = createPageSubtitle(tr("View application version information and check for online updates."), this);
    layout->addWidget(m_pageSubtitle);
    layout->addSpacing(20);

    // 1. App Identity Hero Card (Win11 Settings > System > About style)
    QWidget* identityPanel = createPanel(this);
    QHBoxLayout* identityLayout = new QHBoxLayout(identityPanel);
    identityLayout->setContentsMargins(20, 18, 20, 18);
    identityLayout->setSpacing(16);

    QLabel* appIconLabel = new QLabel(identityPanel);
    appIconLabel->setFixedSize(48, 48);
    appIconLabel->setAlignment(Qt::AlignCenter);
    const QPixmap appPixmap = QIcon(QStringLiteral(":/Assets/icon.png")).pixmap(44, 44);
    if (!appPixmap.isNull()) {
        appIconLabel->setPixmap(appPixmap);
    } else {
        appIconLabel->setText(glyph(0xE713));
        appIconLabel->setStyleSheet(QStringLiteral(
            "QLabel { background: #eff6fc; color: #0067c0; border-radius: 10px; "
            "font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets'; font-size: 24px; }"));
    }
    identityLayout->addWidget(appIconLabel, 0, Qt::AlignVCenter);

    QVBoxLayout* identityTextLayout = new QVBoxLayout();
    identityTextLayout->setContentsMargins(0, 0, 0, 0);
    identityTextLayout->setSpacing(3);

    m_aboutNameLabel = new QLabel(tr("LinDesk"), identityPanel);
    m_aboutNameLabel->setStyleSheet(QStringLiteral(
        "QLabel { background: transparent; color: #1b1b1b; font-size: 16px; font-weight: 600; }"));
    identityTextLayout->addWidget(m_aboutNameLabel);

    m_aboutVersionLabel = new QLabel(tr("Version %1").arg(LINDESK_VERSION_STRING_WITH_TAG), identityPanel);
    m_aboutVersionLabel->setObjectName("rowSubtitle");
    identityTextLayout->addWidget(m_aboutVersionLabel);

    identityLayout->addLayout(identityTextLayout, 1);

    m_checkUpdateButton = createSettingsActionButton(tr("Check for Updates"), identityPanel, true);
    identityLayout->addWidget(m_checkUpdateButton, 0, Qt::AlignVCenter);

    layout->addWidget(identityPanel);
    layout->addSpacing(24);

    // 2. Online Update Status Card
    m_updateSectionTitle = createCardTitle(tr("Online Update"), this);
    layout->addWidget(m_updateSectionTitle);
    layout->addSpacing(8);

    QWidget* updatePanel = createPanel(this);
    QVBoxLayout* updateLayout = new QVBoxLayout(updatePanel);
    updateLayout->setContentsMargins(20, 16, 20, 16);
    updateLayout->setSpacing(10);

    QHBoxLayout* updateHeaderRow = new QHBoxLayout();
    updateHeaderRow->setContentsMargins(0, 0, 0, 0);
    updateHeaderRow->setSpacing(14);
    updateHeaderRow->addWidget(createGlyphLabel(glyph(0xE895), updatePanel, 18, QColor("#1b1b1b")), 0, Qt::AlignVCenter);

    QVBoxLayout* updateTextCol = new QVBoxLayout();
    updateTextCol->setContentsMargins(0, 0, 0, 0);
    updateTextCol->setSpacing(2);

    m_updateRowTitle = new QLabel(tr("Update Status"), updatePanel);
    m_updateRowTitle->setObjectName("rowTitle");
    updateTextCol->addWidget(m_updateRowTitle);

    m_updateStatusLabel = new QLabel(tr("Current version: %1").arg(LINDESK_VERSION_STRING_WITH_TAG), updatePanel);
    m_updateStatusLabel->setObjectName("rowSubtitle");
    m_updateStatusLabel->setWordWrap(true);
    updateTextCol->addWidget(m_updateStatusLabel);

    updateHeaderRow->addLayout(updateTextCol, 1);
    updateLayout->addLayout(updateHeaderRow);

    m_updateNotesLabel = new QLabel(updatePanel);
    m_updateNotesLabel->setObjectName("rowSubtitle");
    m_updateNotesLabel->setWordWrap(true);
    m_updateNotesLabel->hide();
    updateLayout->addWidget(m_updateNotesLabel);

    m_updateProgressBar = new QProgressBar(updatePanel);
    m_updateProgressBar->setObjectName("updateProgressBar");
    m_updateProgressBar->setRange(0, 100);
    m_updateProgressBar->setValue(0);
    m_updateProgressBar->setFixedHeight(6);
    m_updateProgressBar->setTextVisible(false);
    m_updateProgressBar->hide();
    updateLayout->addWidget(m_updateProgressBar);

    layout->addWidget(updatePanel);

    connect(m_checkUpdateButton, &QPushButton::clicked, this, &AboutSettingsPage::startUpdateCheck);

    layout->addStretch(1);
}

void AboutSettingsPage::startUpdateCheck()
{
    if (!m_updateChecker) {
        m_updateChecker = new UpdateChecker(this);
        connect(m_updateChecker, &UpdateChecker::checkSucceeded, this, [this](const UpdateInfo& info, bool updateAvailable) {
            if (!m_checkUpdateButton) {
                return;
            }

            m_pendingUpdate = info;
            m_downloadedInstallerPath.clear();
            m_checkUpdateButton->setEnabled(true);

            if (!updateAvailable) {
                m_updateStatusLabel->setText(tr("Already up to date. Current version: %1").arg(LINDESK_VERSION_STRING_WITH_TAG));
                m_updateNotesLabel->hide();
                return;
            }

            m_updateStatusLabel->setText(tr("New version found: %1").arg(info.version));
            m_updateNotesLabel->hide();

            FluentDialog* dialog = new FluentDialog(this);
            dialog->setDialogTitle(tr("New Version Available"));
            dialog->setMessage(tr("Version: %1\n\n%2").arg(info.version, info.notes));
            dialog->addActionButton(tr("Update Now"), FluentDialog::ActionType::Primary, [this, info]() {
                m_pendingUpdate = info;
                startUpdateDownload();
            });
            dialog->addActionButton(tr("Cancel"), FluentDialog::ActionType::Close, [this]() {
                if (m_updateStatusLabel) {
                    m_updateStatusLabel->setText(tr("Update cancelled."));
                }
            });
            dialog->exec();
            dialog->deleteLater();
        });
        connect(m_updateChecker, &UpdateChecker::checkFailed, this, [this](const QString& errorMessage) {
            if (m_updateStatusLabel) {
                m_updateStatusLabel->setText(tr("Update check failed: %1").arg(errorMessage));
            }
            if (m_checkUpdateButton) {
                m_checkUpdateButton->setEnabled(true);
            }
        });
    }

    if (m_updateChecker->isChecking()) {
        return;
    }

    m_pendingUpdate = UpdateInfo();
    m_downloadedInstallerPath.clear();
    if (m_updateStatusLabel) {
        m_updateStatusLabel->setText(tr("Checking for updates..."));
    }
    if (m_updateNotesLabel) {
        m_updateNotesLabel->clear();
        m_updateNotesLabel->hide();
    }
    if (m_updateProgressBar) {
        m_updateProgressBar->hide();
        m_updateProgressBar->setRange(0, 100);
        m_updateProgressBar->setValue(0);
    }
    if (m_checkUpdateButton) {
        m_checkUpdateButton->setEnabled(false);
    }

    m_updateChecker->checkForUpdates();
}

void AboutSettingsPage::startUpdateDownload()
{
    if (m_pendingUpdate.url.isEmpty()) {
        return;
    }

    if (!m_updateDownloader) {
        m_updateDownloader = new UpdateDownloader(this);
        connect(m_updateDownloader, &UpdateDownloader::progressChanged, this, [this](qint64 received, qint64 total) {
            if (!m_updateProgressBar) {
                return;
            }
            m_updateProgressBar->show();
            if (total <= 0) {
                m_updateProgressBar->setRange(0, 0);
                return;
            }
            m_updateProgressBar->setRange(0, 100);
            m_updateProgressBar->setValue(static_cast<int>((received * 100) / total));
        });
        connect(m_updateDownloader, &UpdateDownloader::downloadSucceeded, this, [this](const QString& installerPath) {
            m_downloadedInstallerPath = installerPath;
            if (m_updateStatusLabel) {
                m_updateStatusLabel->setText(tr("Download complete. Starting installer..."));
            }
            if (m_updateProgressBar) {
                m_updateProgressBar->setRange(0, 100);
                m_updateProgressBar->setValue(100);
            }
            installDownloadedUpdate();
        });
        connect(m_updateDownloader, &UpdateDownloader::downloadFailed, this, [this](const QString& errorMessage) {
            if (m_updateStatusLabel) {
                m_updateStatusLabel->setText(tr("Download failed: %1").arg(errorMessage));
            }
            if (m_updateProgressBar) {
                m_updateProgressBar->setRange(0, 100);
                m_updateProgressBar->setValue(0);
            }
            if (m_checkUpdateButton) {
                m_checkUpdateButton->setEnabled(true);
            }
        });
    }

    if (m_updateDownloader->isDownloading()) {
        return;
    }

    m_downloadedInstallerPath.clear();
    QString cachedInstallerPath;
    if (m_updateDownloader->hasValidCachedInstaller(m_pendingUpdate, &cachedInstallerPath)) {
        m_downloadedInstallerPath = cachedInstallerPath;
        if (m_updateStatusLabel) {
            m_updateStatusLabel->setText(tr("Using cached installer: %1").arg(cachedInstallerPath));
        }
        if (m_updateProgressBar) {
            m_updateProgressBar->setRange(0, 100);
            m_updateProgressBar->setValue(100);
            m_updateProgressBar->show();
        }
        installDownloadedUpdate();
        return;
    }

    if (m_updateStatusLabel) {
        m_updateStatusLabel->setText(tr("Downloading update to: %1").arg(m_updateDownloader->cachedInstallerPath(m_pendingUpdate)));
    }
    if (m_updateProgressBar) {
        m_updateProgressBar->setRange(0, 100);
        m_updateProgressBar->setValue(0);
        m_updateProgressBar->show();
    }
    if (m_checkUpdateButton) {
        m_checkUpdateButton->setEnabled(false);
    }

    m_updateDownloader->download(m_pendingUpdate);
}

void AboutSettingsPage::installDownloadedUpdate()
{
    if (m_downloadedInstallerPath.isEmpty() || !QFileInfo::exists(m_downloadedInstallerPath)) {
        if (m_updateStatusLabel) {
            m_updateStatusLabel->setText(tr("Installer file is missing. Please download again."));
        }
        return;
    }

    const QString updaterPath = QCoreApplication::applicationDirPath() + QStringLiteral("/LinDeskUpdate.exe");
    if (!QFileInfo::exists(updaterPath)) {
        if (m_updateStatusLabel) {
            m_updateStatusLabel->setText(tr("LinDeskUpdate.exe was not found in the application directory."));
        }
        return;
    }

    QStringList args;
    const QString languageArg = LanguageManager::configuredLanguageCode().startsWith(QStringLiteral("zh"))
                                    ? QStringLiteral("zh")
                                    : QStringLiteral("en");
    args << QStringLiteral("--installer") << m_downloadedInstallerPath
         << QStringLiteral("--app") << QCoreApplication::applicationFilePath()
         << QStringLiteral("--process") << QStringLiteral("LinDesk.exe")
         << QStringLiteral("--language") << languageArg
         << QStringLiteral("--restart") << QStringLiteral("true");

    if (!QProcess::startDetached(updaterPath, args)) {
        if (m_updateStatusLabel) {
            m_updateStatusLabel->setText(tr("Failed to start updater."));
        }
        return;
    }

    QCoreApplication::quit();
}

void AboutSettingsPage::resetUpdatePanel()
{
    m_pendingUpdate = UpdateInfo();
    m_downloadedInstallerPath.clear();
    if (m_updateStatusLabel) {
        m_updateStatusLabel->setText(tr("Current version: %1").arg(LINDESK_VERSION_STRING_WITH_TAG));
    }
    if (m_updateNotesLabel) {
        m_updateNotesLabel->clear();
        m_updateNotesLabel->hide();
    }
    if (m_updateProgressBar) {
        m_updateProgressBar->setRange(0, 100);
        m_updateProgressBar->setValue(0);
        m_updateProgressBar->hide();
    }
    if (m_checkUpdateButton) {
        m_checkUpdateButton->setEnabled(true);
    }
}

void AboutSettingsPage::updateTranslations()
{
    if (m_pageTitle) {
        m_pageTitle->setText(tr("About"));
    }
    if (m_pageSubtitle) {
        m_pageSubtitle->setText(tr("View application version information and check for online updates."));
    }
    if (m_aboutNameLabel) {
        m_aboutNameLabel->setText(tr("LinDesk"));
    }
    if (m_aboutVersionLabel) {
        m_aboutVersionLabel->setText(tr("Version %1").arg(LINDESK_VERSION_STRING_WITH_TAG));
    }
    if (m_updateSectionTitle) {
        m_updateSectionTitle->setText(tr("Online Update"));
    }
    if (m_updateRowTitle) {
        m_updateRowTitle->setText(tr("Update Status"));
    }
    if (m_updateStatusLabel) {
        m_updateStatusLabel->setText(tr("Current version: %1").arg(LINDESK_VERSION_STRING_WITH_TAG));
    }
    if (m_checkUpdateButton) {
        m_checkUpdateButton->setText(tr("Check for Updates"));
    }
}
