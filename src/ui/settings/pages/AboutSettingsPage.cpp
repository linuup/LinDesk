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
    updateLayout->setSpacing(12);

    QHBoxLayout* updateHeaderRow = new QHBoxLayout();
    updateHeaderRow->setContentsMargins(0, 0, 0, 0);
    updateHeaderRow->setSpacing(14);

    QWidget* iconBadge = new QWidget(updatePanel);
    iconBadge->setFixedSize(36, 36);
    iconBadge->setStyleSheet(QStringLiteral(
        "QWidget { background: #eff6fc; border-radius: 8px; border: 1px solid rgba(0, 103, 192, 0.08); }"));
    QHBoxLayout* badgeLayout = new QHBoxLayout(iconBadge);
    badgeLayout->setContentsMargins(0, 0, 0, 0);
    QLabel* badgeIcon = createGlyphLabel(glyph(0xE895), iconBadge, 18, QColor("#0067c0"));
    badgeLayout->addWidget(badgeIcon, 0, Qt::AlignCenter);
    updateHeaderRow->addWidget(iconBadge, 0, Qt::AlignVCenter);

    QVBoxLayout* updateTextCol = new QVBoxLayout();
    updateTextCol->setContentsMargins(0, 0, 0, 0);
    updateTextCol->setSpacing(3);

    m_updateRowTitle = new QLabel(tr("Update Status"), updatePanel);
    m_updateRowTitle->setObjectName("rowTitle");
    updateTextCol->addWidget(m_updateRowTitle);

    m_updateStatusLabel = new QLabel(tr("Current version: %1").arg(LINDESK_VERSION_STRING_WITH_TAG), updatePanel);
    m_updateStatusLabel->setObjectName("rowSubtitle");
    m_updateStatusLabel->setWordWrap(true);
    updateTextCol->addWidget(m_updateStatusLabel);

    updateHeaderRow->addLayout(updateTextCol, 1);

    // Right action buttons
    QHBoxLayout* actionBtnLayout = new QHBoxLayout();
    actionBtnLayout->setContentsMargins(0, 0, 0, 0);
    actionBtnLayout->setSpacing(8);

    m_updateActionBtn = createSettingsActionButton(tr("Update Now"), updatePanel, true);
    m_updateActionBtn->hide();
    connect(m_updateActionBtn, &QPushButton::clicked, this, &AboutSettingsPage::startUpdateDownload);
    actionBtnLayout->addWidget(m_updateActionBtn);

    m_cancelDownloadBtn = createSettingsActionButton(tr("Cancel"), updatePanel, false);
    m_cancelDownloadBtn->hide();
    connect(m_cancelDownloadBtn, &QPushButton::clicked, this, &AboutSettingsPage::cancelUpdateDownload);
    actionBtnLayout->addWidget(m_cancelDownloadBtn);

    updateHeaderRow->addLayout(actionBtnLayout, 0);
    updateLayout->addLayout(updateHeaderRow);

    // Release Notes container
    m_notesContainer = new QWidget(updatePanel);
    m_notesContainer->setObjectName("settingsInnerRow");
    m_notesContainer->setStyleSheet(QStringLiteral(
        "QWidget#settingsInnerRow { background: #f9f9f9; border: 1px solid #e5e5e5; border-radius: 6px; }"));
    QVBoxLayout* notesLayout = new QVBoxLayout(m_notesContainer);
    notesLayout->setContentsMargins(14, 12, 14, 12);
    notesLayout->setSpacing(6);

    m_notesHeaderLabel = new QLabel(m_notesContainer);
    m_notesHeaderLabel->setStyleSheet(QStringLiteral(
        "QLabel { background: transparent; color: #1b1b1b; font-size: 13px; font-weight: 600; }"));
    notesLayout->addWidget(m_notesHeaderLabel);

    m_updateNotesLabel = new QLabel(m_notesContainer);
    m_updateNotesLabel->setStyleSheet(QStringLiteral(
        "QLabel { background: transparent; color: #5c5c5c; font-size: 12px; font-weight: 400; line-height: 1.4; }"));
    m_updateNotesLabel->setWordWrap(true);
    notesLayout->addWidget(m_updateNotesLabel);

    m_notesContainer->hide();
    updateLayout->addWidget(m_notesContainer);

    // Progress container (WinUI 3 progress bar + MB stats + Percent label)
    m_progressContainer = new QWidget(updatePanel);
    m_progressContainer->setStyleSheet(QStringLiteral("QWidget { background: transparent; }"));
    QVBoxLayout* progressLayout = new QVBoxLayout(m_progressContainer);
    progressLayout->setContentsMargins(0, 4, 0, 0);
    progressLayout->setSpacing(6);

    m_updateProgressBar = new QProgressBar(m_progressContainer);
    m_updateProgressBar->setObjectName("updateProgressBar");
    m_updateProgressBar->setRange(0, 100);
    m_updateProgressBar->setValue(0);
    m_updateProgressBar->setFixedHeight(4);
    m_updateProgressBar->setTextVisible(false);
    progressLayout->addWidget(m_updateProgressBar);

    QHBoxLayout* progressInfoRow = new QHBoxLayout();
    progressInfoRow->setContentsMargins(0, 0, 0, 0);

    m_progressStatsLabel = new QLabel(m_progressContainer);
    m_progressStatsLabel->setStyleSheet(QStringLiteral(
        "QLabel { background: transparent; color: #5c5c5c; font-size: 12px; font-weight: 400; }"));
    progressInfoRow->addWidget(m_progressStatsLabel, 1);

    m_progressPercentLabel = new QLabel(m_progressContainer);
    m_progressPercentLabel->setStyleSheet(QStringLiteral(
        "QLabel { background: transparent; color: #0067c0; font-size: 12px; font-weight: 600; }"));
    progressInfoRow->addWidget(m_progressPercentLabel, 0, Qt::AlignRight);

    progressLayout->addLayout(progressInfoRow);
    m_progressContainer->hide();
    updateLayout->addWidget(m_progressContainer);

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
            m_checkUpdateButton->setText(tr("Check for Updates"));

            if (!updateAvailable) {
                m_updateStatusLabel->setText(tr("LinDesk is up to date (Version %1).").arg(LINDESK_VERSION_STRING_WITH_TAG));
                if (m_notesContainer) m_notesContainer->hide();
                if (m_progressContainer) m_progressContainer->hide();
                if (m_updateActionBtn) m_updateActionBtn->hide();
                if (m_cancelDownloadBtn) m_cancelDownloadBtn->hide();
                return;
            }

            m_updateStatusLabel->setText(tr("New version available: LinDesk %1").arg(info.version));
            if (m_notesContainer) {
                if (m_notesHeaderLabel) {
                    m_notesHeaderLabel->setText(tr("What's new in v%1").arg(info.version));
                }
                if (m_updateNotesLabel) {
                    const QString notesText = info.notes.trimmed();
                    m_updateNotesLabel->setText(notesText.isEmpty() ? tr("No release notes provided.") : notesText);
                }
                m_notesContainer->show();
            }

            if (m_updateActionBtn) {
                m_updateActionBtn->setText(tr("Update Now"));
                m_updateActionBtn->show();
            }
            if (m_cancelDownloadBtn) {
                m_cancelDownloadBtn->hide();
            }

            FluentDialog* dialog = new FluentDialog(this);
            dialog->setDialogTitle(tr("New Version Available"));
            dialog->setMessage(tr("Version: %1\n\n%2").arg(info.version, info.notes));
            dialog->addActionButton(tr("Update Now"), FluentDialog::ActionType::Primary, [this, info]() {
                m_pendingUpdate = info;
                startUpdateDownload();
            });
            dialog->addActionButton(tr("Cancel"), FluentDialog::ActionType::Close, nullptr);
            dialog->exec();
            dialog->deleteLater();
        });
        connect(m_updateChecker, &UpdateChecker::checkFailed, this, [this](const QString& errorMessage) {
            if (m_updateStatusLabel) {
                m_updateStatusLabel->setText(tr("Update check failed: %1").arg(errorMessage));
            }
            if (m_checkUpdateButton) {
                m_checkUpdateButton->setEnabled(true);
                m_checkUpdateButton->setText(tr("Check for Updates"));
            }
        });
    }

    if (m_updateChecker->isChecking()) {
        return;
    }

    m_pendingUpdate = UpdateInfo();
    m_downloadedInstallerPath.clear();
    if (m_updateStatusLabel) {
        m_updateStatusLabel->setText(tr("Checking for online updates..."));
    }
    if (m_notesContainer) {
        m_notesContainer->hide();
    }
    if (m_progressContainer) {
        m_progressContainer->hide();
    }
    if (m_updateActionBtn) {
        m_updateActionBtn->hide();
    }
    if (m_cancelDownloadBtn) {
        m_cancelDownloadBtn->hide();
    }
    if (m_checkUpdateButton) {
        m_checkUpdateButton->setEnabled(false);
        m_checkUpdateButton->setText(tr("Checking..."));
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
            if (m_progressContainer && !m_progressContainer->isVisible()) {
                m_progressContainer->show();
            }
            if (total <= 0) {
                m_updateProgressBar->setRange(0, 0);
                if (m_progressStatsLabel) {
                    const double mbReceived = static_cast<double>(received) / (1024.0 * 1024.0);
                    m_progressStatsLabel->setText(tr("Downloaded %1 MB").arg(QString::number(mbReceived, 'f', 1)));
                }
                if (m_progressPercentLabel) {
                    m_progressPercentLabel->clear();
                }
                return;
            }

            const int pct = qBound(0, static_cast<int>((received * 100) / total), 100);
            m_updateProgressBar->setRange(0, 100);
            m_updateProgressBar->setValue(pct);

            const double mbReceived = static_cast<double>(received) / (1024.0 * 1024.0);
            const double mbTotal = static_cast<double>(total) / (1024.0 * 1024.0);

            if (m_progressStatsLabel) {
                m_progressStatsLabel->setText(tr("Downloading: %1 MB / %2 MB")
                    .arg(QString::number(mbReceived, 'f', 1))
                    .arg(QString::number(mbTotal, 'f', 1)));
            }
            if (m_progressPercentLabel) {
                m_progressPercentLabel->setText(QStringLiteral("%1%").arg(pct));
            }
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
            if (m_progressStatsLabel) {
                m_progressStatsLabel->setText(tr("Verified installer package. Launching..."));
            }
            if (m_progressPercentLabel) {
                m_progressPercentLabel->setText(QStringLiteral("100%"));
            }
            if (m_cancelDownloadBtn) {
                m_cancelDownloadBtn->hide();
            }
            installDownloadedUpdate();
        });
        connect(m_updateDownloader, &UpdateDownloader::downloadFailed, this, [this](const QString& errorMessage) {
            if (m_updateStatusLabel) {
                m_updateStatusLabel->setText(tr("Download failed: %1").arg(errorMessage));
            }
            if (m_progressContainer) {
                m_progressContainer->hide();
            }
            if (m_cancelDownloadBtn) {
                m_cancelDownloadBtn->hide();
            }
            if (m_updateActionBtn) {
                m_updateActionBtn->setText(tr("Retry Download"));
                m_updateActionBtn->show();
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
            m_updateStatusLabel->setText(tr("Verified cached installer package."));
        }
        if (m_progressContainer) {
            m_progressContainer->show();
        }
        if (m_updateProgressBar) {
            m_updateProgressBar->setRange(0, 100);
            m_updateProgressBar->setValue(100);
        }
        if (m_progressStatsLabel) {
            m_progressStatsLabel->setText(tr("Installer ready: %1").arg(QFileInfo(cachedInstallerPath).fileName()));
        }
        if (m_progressPercentLabel) {
            m_progressPercentLabel->setText(QStringLiteral("100%"));
        }
        installDownloadedUpdate();
        return;
    }

    if (m_updateStatusLabel) {
        m_updateStatusLabel->setText(tr("Downloading LinDesk %1 update package...").arg(m_pendingUpdate.version));
    }
    if (m_progressContainer) {
        m_progressContainer->show();
    }
    if (m_updateProgressBar) {
        m_updateProgressBar->setRange(0, 100);
        m_updateProgressBar->setValue(0);
    }
    if (m_progressStatsLabel) {
        m_progressStatsLabel->setText(tr("Connecting to update server..."));
    }
    if (m_progressPercentLabel) {
        m_progressPercentLabel->setText(QStringLiteral("0%"));
    }
    if (m_updateActionBtn) {
        m_updateActionBtn->hide();
    }
    if (m_cancelDownloadBtn) {
        m_cancelDownloadBtn->show();
    }
    if (m_checkUpdateButton) {
        m_checkUpdateButton->setEnabled(false);
    }

    m_updateDownloader->download(m_pendingUpdate);
}

void AboutSettingsPage::cancelUpdateDownload()
{
    if (m_updateDownloader && m_updateDownloader->isDownloading()) {
        m_updateDownloader->cancel();
    }
    if (m_updateStatusLabel) {
        m_updateStatusLabel->setText(tr("Download cancelled."));
    }
    if (m_progressContainer) {
        m_progressContainer->hide();
    }
    if (m_cancelDownloadBtn) {
        m_cancelDownloadBtn->hide();
    }
    if (m_updateActionBtn) {
        m_updateActionBtn->setText(tr("Download Again"));
        m_updateActionBtn->show();
    }
    if (m_checkUpdateButton) {
        m_checkUpdateButton->setEnabled(true);
        m_checkUpdateButton->setText(tr("Check for Updates"));
    }
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
    if (m_notesContainer) {
        m_notesContainer->hide();
    }
    if (m_updateNotesLabel) {
        m_updateNotesLabel->clear();
    }
    if (m_progressContainer) {
        m_progressContainer->hide();
    }
    if (m_updateProgressBar) {
        m_updateProgressBar->setRange(0, 100);
        m_updateProgressBar->setValue(0);
    }
    if (m_updateActionBtn) {
        m_updateActionBtn->hide();
    }
    if (m_cancelDownloadBtn) {
        m_cancelDownloadBtn->hide();
    }
    if (m_checkUpdateButton) {
        m_checkUpdateButton->setEnabled(true);
        m_checkUpdateButton->setText(tr("Check for Updates"));
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
    if (m_updateStatusLabel && m_pendingUpdate.url.isEmpty()) {
        m_updateStatusLabel->setText(tr("Current version: %1").arg(LINDESK_VERSION_STRING_WITH_TAG));
    }
    if (m_checkUpdateButton) {
        m_checkUpdateButton->setText(tr("Check for Updates"));
    }
    if (m_updateActionBtn && !m_updateActionBtn->isHidden()) {
        m_updateActionBtn->setText(tr("Update Now"));
    }
    if (m_cancelDownloadBtn) {
        m_cancelDownloadBtn->setText(tr("Cancel"));
    }
}
