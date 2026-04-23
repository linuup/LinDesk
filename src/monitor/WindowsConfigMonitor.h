#pragma once

#include <QObject>
#include <QWinEventNotifier>
#include <windows.h>

class WindowsConfigMonitor : public QObject
{
    Q_OBJECT

public:
    explicit WindowsConfigMonitor(QObject* parent = nullptr);
    ~WindowsConfigMonitor() override;

    bool startListening();
    void stopListening();
    bool showHiddenFiles() const { return m_showHiddenFiles; }
    void refreshState();

signals:
    void showHiddenFilesChanged(bool showHiddenFiles);

private slots:
    void onAdvancedKeyChanged();

private:
    bool readShowHiddenFiles() const;
    void registerChangeNotification();

    HKEY m_advancedKey = nullptr;
    HANDLE m_advancedEvent = nullptr;
    QWinEventNotifier* m_advancedNotifier = nullptr;
    bool m_showHiddenFiles = false;
};
