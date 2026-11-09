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
    void installDownloadedUpdate();

private:
    QLabel* m_pageTitle = nullptr;
    QLabel* m_pageSubtitle = nullptr;
    QLabel* m_aboutNameLabel = nullptr;
    QLabel* m_aboutVersionLabel = nullptr;
    QLabel* m_updateSectionTitle = nullptr;
    QLabel* m_updateRowTitle = nullptr;
    QLabel* m_updateStatusLabel = nullptr;
    QLabel* m_updateNotesLabel = nullptr;
    QProgressBar* m_updateProgressBar = nullptr;
    QPushButton* m_checkUpdateButton = nullptr;

    UpdateChecker* m_updateChecker = nullptr;
    UpdateDownloader* m_updateDownloader = nullptr;
    UpdateInfo m_pendingUpdate;
    QString m_downloadedInstallerPath;
};
