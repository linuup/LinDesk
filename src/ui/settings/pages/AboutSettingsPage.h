#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include "services/update/UpdateChecker.h"
#include "services/update/UpdateDownloader.h"

class AboutSettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit AboutSettingsPage(QWidget* parent = nullptr);
    ~AboutSettingsPage() override = default;

    void resetUpdatePanel();
    void updateTranslations();

private:
    void initUI();
    void startUpdateCheck();
    void startUpdateDownload();
    void cancelUpdateDownload();
    void installDownloadedUpdate();

private:
    QLabel* m_pageTitle = nullptr;
    QLabel* m_pageSubtitle = nullptr;
    QLabel* m_aboutNameLabel = nullptr;
    QLabel* m_aboutVersionLabel = nullptr;
    QLabel* m_updateSectionTitle = nullptr;
    QLabel* m_updateRowTitle = nullptr;
    QLabel* m_updateStatusLabel = nullptr;
    QPushButton* m_checkUpdateButton = nullptr;

    // Release Notes preview card
    QWidget* m_notesContainer = nullptr;
    QLabel* m_notesHeaderLabel = nullptr;
    QLabel* m_updateNotesLabel = nullptr;

    // Progress bar and detailed statistics
    QWidget* m_progressContainer = nullptr;
    QProgressBar* m_updateProgressBar = nullptr;
    QLabel* m_progressStatsLabel = nullptr;
    QLabel* m_progressPercentLabel = nullptr;

    // Action buttons
    QPushButton* m_updateActionBtn = nullptr;
    QPushButton* m_cancelDownloadBtn = nullptr;

    UpdateChecker* m_updateChecker = nullptr;
    UpdateDownloader* m_updateDownloader = nullptr;
    UpdateInfo m_pendingUpdate;
    QString m_downloadedInstallerPath;
};
