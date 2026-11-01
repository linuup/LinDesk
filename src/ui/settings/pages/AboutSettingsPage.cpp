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

using namespace SettingsUICommon;

AboutSettingsPage::AboutSettingsPage(QWidget* parent)
    : QWidget(parent)
{
    initUI();
}

void AboutSettingsPage::initUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(30, 20, 30, 20);
    layout->setAlignment(Qt::AlignCenter);

    QLabel* icon = new QLabel(u8"\uE713", this);
    icon->setStyleSheet(
        "font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets'; "
        "font-size: 72px; "
        "color: #0078d4; "
        "background: transparent;"
    );
    layout->addWidget(icon, 0, Qt::AlignCenter);

    m_aboutNameLabel = new QLabel(tr("LinDesk"), this);
    m_aboutNameLabel->setStyleSheet(
        "font-family: 'Microsoft YaHei UI'; "
        "font-size: 28px; "
        "font-weight: 600; "
        "color: #1f2937; "
        "letter-spacing: -0.01em;"
    );
    layout->addWidget(m_aboutNameLabel, 0, Qt::AlignCenter);

    m_aboutVersionLabel = new QLabel(tr("Version %1").arg(LINDESK_VERSION_STRING_WITH_TAG), this);
    m_aboutVersionLabel->setStyleSheet(
        "font-family: 'Microsoft YaHei UI'; "
        "font-size: 13px; "
        "color: #6b7280;"
    );
    layout->addWidget(m_aboutVersionLabel, 0, Qt::AlignCenter);
    
    layout->addSpacing(24);

    QWidget* updatePanel = createPanel(this);
    QVBoxLayout* updateLayout = new QVBoxLayout(updatePanel);
    updateLayout->setContentsMargins(22, 18, 22, 18);
    updateLayout->setSpacing(10);

    updateLayout->addWidget(createCardTitle(tr("Online Update"), updatePanel));

    m_updateStatusLabel = new QLabel(tr("Current version: %1").arg(LINDESK_VERSION_STRING_WITH_TAG), updatePanel);
    m_updateStatusLabel->setObjectName("rowSubtitle");
    m_updateStatusLabel->setWordWrap(true);
    updateLayout->addWidget(m_updateStatusLabel);

    m_updateNotesLabel = new QLabel(updatePanel);
    m_updateNotesLabel->setObjectName("rowSubtitle");
    m_updateNotesLabel->setWordWrap(true);
    m_updateNotesLabel->hide();
    updateLayout->addWidget(m_updateNotesLabel);

    m_updateProgressBar = new QProgressBar(updatePanel);
    m_updateProgressBar->setObjectName("updateProgressBar");
    m_updateProgressBar->setRange(0, 100);
    m_updateProgressBar->setValue(0);
    m_updateProgressBar->hide();
    updateLayout->addWidget(m_updateProgressBar);

    QHBoxLayout* updateButtons = new QHBoxLayout();
    updateButtons->setContentsMargins(0, 4, 0, 0);
    updateButtons->setSpacing(10);

    m_checkUpdateButton = new QPushButton(tr("Check for Updates"), updatePanel);
    m_checkUpdateButton->setObjectName("updatePrimaryButton");
    m_checkUpdateButton->setFixedHeight(34);
    updateButtons->addWidget(m_checkUpdateButton);

    updateButtons->addStretch(1);
    updateLayout->addLayout(updateButtons);

    layout->addWidget(updatePanel);

    connect(m_checkUpdateButton, &QPushButton::clicked, this, &AboutSettingsPage::startUpdateCheck);
    
    layout->addStretch();
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
    if (m_aboutNameLabel) {
        m_aboutNameLabel->setText(tr("LinDesk"));
    }
    if (m_aboutVersionLabel) {
        m_aboutVersionLabel->setText(tr("Version %1").arg(LINDESK_VERSION_STRING_WITH_TAG));
    }
    if (m_updateStatusLabel) {
        m_updateStatusLabel->setText(tr("Current version: %1").arg(LINDESK_VERSION_STRING_WITH_TAG));
    }
    if (m_checkUpdateButton) {
        m_checkUpdateButton->setText(tr("Check for Updates"));
    }
}
