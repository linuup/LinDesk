#pragma once

#include <QHash>
#include <QString>
#include <QWidget>

class QLabel;
class QCheckBox;
class QFileSystemWatcher;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QTimer;
class ListView;
class SoftDesktopTextPreview;
class FluentToggleSwitch;
namespace LinWidget {
class WidgetManager;
}

class ListViewDebugPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ListViewDebugPanel(ListView* listView, QWidget* parent = nullptr);

private:
    void initUI();
    void updateLabels();
    void updateOpenGLStatus();
    void applyTextPreviewOptions();
    LinWidget::WidgetManager* linWidgetManager();
    void reloadLinWidgetsList();
    void updateLinWidgetsButtons();
    void updateLinWidgetLogViewer();
    QString selectedLinWidgetPackagePath() const;
    QString selectedLinWidgetRegistryId() const;
    QString selectedLinWidgetRegistryKey() const;
    QString selectedLinWidgetInstanceIdForLog() const;
    QString linWidgetLogFilePathForInstance(const QString& instanceId) const;
    bool selectedLinWidgetIsDevelopment() const;
    QListWidget* activeLinWidgetList() const;
    QString linWidgetPackageKey(const QString& packagePath) const;
    QString linWidgetRegistryKey(const QString& id, bool isDevelopment) const;
    QPushButton* createActionButton(const QString& text, bool accent = false, bool destructive = false) const;
    QWidget* createSliderSettingRow(const QString& labelText,
                                    int minValue,
                                    int maxValue,
                                    QSlider*& slider,
                                    QLabel*& valueLabel);
    QLabel* createPageTitleLabel(const QString& text) const;
    QLabel* createPageSubtitleLabel(const QString& text) const;

    QWidget* createGeneralPage();
    QWidget* createLinWidgetsPage();
    QWidget* createLayoutPage();
    QWidget* createTextDiagnosticsPage();
    QWidget* wrapScrollablePage(QWidget* content);

    ListView* m_listView = nullptr;

    // Spacing sliders
    QSlider* m_rowSlider = nullptr;
    QSlider* m_colSlider = nullptr;
    QLabel* m_rowValueLabel = nullptr;
    QLabel* m_colValueLabel = nullptr;

    class QTabBar* m_tabBar = nullptr;
    class QStackedWidget* m_contentStack = nullptr;

    // Toggles
    FluentToggleSwitch* m_embedToggle = nullptr;
    FluentToggleSwitch* m_persistToggle = nullptr;
    FluentToggleSwitch* m_linWidgetBoundsToggle = nullptr;
    QLabel* m_openGLStatusLabel = nullptr;
    QLabel* m_linWidgetsPageStatusLabel = nullptr;
    QLabel* m_linWidgetsLogTitleLabel = nullptr;
    QPlainTextEdit* m_linWidgetsLogView = nullptr;
    QFileSystemWatcher* m_linWidgetsLogWatcher = nullptr;
    QTimer* m_linWidgetsLogReloadTimer = nullptr;
    QString m_currentLinWidgetLogPath;
    QListWidget* m_installedLinWidgetsList = nullptr;
    QListWidget* m_developmentLinWidgetsList = nullptr;
    QPushButton* m_linWidgetsStartButton = nullptr;
    QPushButton* m_linWidgetsStopButton = nullptr;
    QPushButton* m_linWidgetsOpenFolderButton = nullptr;
    QPushButton* m_linWidgetsOpenLogsButton = nullptr;
    QPushButton* m_linWidgetsAddDevButton = nullptr;
    QPushButton* m_linWidgetsPackageButton = nullptr;
    QPushButton* m_linWidgetsResetBundledButton = nullptr;
    QPushButton* m_linWidgetsRemoveDevButton = nullptr;
    QPushButton* m_linWidgetsDeleteButton = nullptr;
    LinWidget::WidgetManager* m_linWidgetManager = nullptr;
    QString m_lastLinWidgetInstanceId;
    QHash<QString, QString> m_linWidgetInstancesByPackage;
    QHash<QString, QString> m_linWidgetInstancesById;

    // Text preview
    SoftDesktopTextPreview* m_textPreview = nullptr;
    QSlider* m_largeGlowSlider = nullptr;
    QSlider* m_tightGlowSlider = nullptr;
    QSlider* m_glowAlphaSlider = nullptr;
    QSlider* m_innerAlphaSlider = nullptr;
    QSlider* m_glowOffsetXSlider = nullptr;
    QSlider* m_glowOffsetYSlider = nullptr;
    QSlider* m_innerOffsetXSlider = nullptr;
    QSlider* m_innerOffsetYSlider = nullptr;
    QLabel* m_largeGlowValueLabel = nullptr;
    QLabel* m_tightGlowValueLabel = nullptr;
    QLabel* m_glowAlphaValueLabel = nullptr;
    QLabel* m_innerAlphaValueLabel = nullptr;
    QLabel* m_glowOffsetXValueLabel = nullptr;
    QLabel* m_glowOffsetYValueLabel = nullptr;
    QLabel* m_innerOffsetXValueLabel = nullptr;
    QLabel* m_innerOffsetYValueLabel = nullptr;
};
