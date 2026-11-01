#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QStackedWidget>
#include <QScrollArea>
#include <QSlider>
#include <QCheckBox>
#include <QComboBox>
#include <QProgressBar>
#include <QPointer>
#include <QTimer>
#include <QPaintEvent>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QMap>
#include <QList>
#include "ui/Fluent/FluentBaseWidget.h"
#include "ui/Fluent/FluentToggleSwitch.h"

namespace LinHub {

class StudioPreviewCanvas;

struct WallpaperItem {
    QString id;
    QString name;
    QString tag;
    QString description;
    QString packagePath;
    QString thumbStyle;
    QJsonObject propertiesSchema;
};

struct WidgetHubItem {
    QString id;
    QString name;
    QString version;
    QString author;
    QString description;
    QString role;
    QString packagePath;
    QString iconPath;
    QString source; // "official", "installed", "dev"
    bool isDevelopment = false;
    bool enabled = true;
    bool isRunning = false;
    QString statusText;
    qint64 storageUsedBytes = 0;
    QStringList declaredPermissions;
    QStringList grantedPermissions;
    bool hasSettings = false;
    QStringList runningInstanceIds;
};

class WallpaperStudioWindow : public FluentBaseWidget {
    Q_OBJECT

public:
    static WallpaperStudioWindow* instance();
    static void showStudio(int defaultTab = 0);

    explicit WallpaperStudioWindow(QWidget* parent = nullptr);
    ~WallpaperStudioWindow() override = default;

public slots:
    void switchToTab(int tabIndex);
    void applyWallpaperPreset(const QString& wallpaperId);
    void stopCurrentWallpaper();
    void toggleMousePhysics(bool enabled);
    void importLocalMedia();
    void exportDiagnosticsPackage();
    void selectWallpaperForStudio(const QString& wallpaperId);

    // Widget Management Slots
    void refreshWidgetsList();
    void showCreateWidgetDialog();
    void addDevelopmentWidget();
    void toggleWidgetPermission(const QString& widgetId, const QString& permission, bool currentGranted);
    void clearWidgetStorage(const QString& widgetId, const QString& widgetName);
    void uninstallWidget(const QString& widgetId, const QString& widgetName, const QString& packagePath, const QStringList& runningInstanceIds);
    void showQuickWidgetSettingsDialog(const QString& widgetId, const QString& widgetName, const QString& packagePath);
    void showToast(const QString& message, bool success = true);

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void initUI();
    void setupHeader();
    void setupNavPills();
    QWidget* createGalleryPage();
    QWidget* createStudioPage();
    QWidget* createWidgetsPage();
    QWidget* createDevAdminPage();

    // Wallpaper Logic
    void scanWallpapers();
    void populateGalleryCards();
    void refreshActiveWallpaperStatus();
    void updateStudioDimming(int val);
    void updateStudioBlur(int val);

    void loadAndGeneratePropertyControls(const QString& wallpaperId);
    void dispatchPropertyUpdate(const QString& key, const QJsonValue& val);
    QJsonObject loadSavedProperties(const QString& wallpaperId) const;
    void saveProperties(const QString& wallpaperId, const QJsonObject& props);
    QString resolveWallpaperPath(const QString& wallpaperId) const;
    void appendIpcLog(const QString& direction, const QString& msg);

    // Widget Management Logic
    void renderWidgetCards();
    void setWidgetFilter(int filterIndex);

private:
    static QPointer<WallpaperStudioWindow> s_instance;

    QWidget* m_hubWebWidget = nullptr;
    QStackedWidget* m_stack = nullptr;
    QPushButton* m_tabGalleryBtn = nullptr;
    QPushButton* m_tabStudioBtn = nullptr;
    QPushButton* m_tabWidgetsBtn = nullptr;
    QPushButton* m_tabDevBtn = nullptr;

    // Gallery elements
    QLabel* m_heroStatusDot = nullptr;
    QLabel* m_heroStatusText = nullptr;
    QLabel* m_heroTitle = nullptr;
    QLabel* m_heroDesc = nullptr;
    FluentToggleSwitch* m_heroMousePhysicsToggle = nullptr;
    QPushButton* m_heroStopBtn = nullptr;
    QGridLayout* m_galleryGridLayout = nullptr;
    QWidget* m_galleryGridContainer = nullptr;

    // Studio elements
    StudioPreviewCanvas* m_studioCanvas = nullptr;
    QSlider* m_dimSlider = nullptr;
    QLabel* m_dimValLabel = nullptr;
    QSlider* m_blurSlider = nullptr;
    QLabel* m_blurValLabel = nullptr;
    QCheckBox* m_mockIconsCheckbox = nullptr;
    QString m_currentEditingWallpaper = QStringLiteral("lindesk.wallpaper.gargantua");

    // Dynamic properties panel in Studio
    QWidget* m_dynamicPropertiesContainer = nullptr;
    QVBoxLayout* m_dynamicPropertiesLayout = nullptr;
    QLabel* m_propertiesTitle = nullptr;
    QMap<QString, QJsonValue> m_currentPropertyValues;

    // Available wallpapers
    QList<WallpaperItem> m_availableWallpapers;

    // Widget Management elements
    QList<WidgetHubItem> m_allWidgets;
    QString m_widgetSearchText;
    int m_widgetFilterMode = 0; // 0: All, 1: Running, 2: Official, 3: Dev
    QWidget* m_widgetsContainer = nullptr;
    QVBoxLayout* m_widgetsListLayout = nullptr;
    QLineEdit* m_widgetSearchEdit = nullptr;
    FluentToggleSwitch* m_runtimeMasterSwitch = nullptr;
    FluentToggleSwitch* m_showBoundsSwitch = nullptr;
    QPushButton* m_filterAllBtn = nullptr;
    QPushButton* m_filterRunningBtn = nullptr;
    QPushButton* m_filterOfficialBtn = nullptr;
    QPushButton* m_filterDevBtn = nullptr;

    // Dev elements
    QLabel* m_ipcLogView = nullptr;
    QTimer* m_statusTimer = nullptr;



    // In-window toast notification
    QWidget* m_toastWidget = nullptr;
    QLabel* m_toastLabel = nullptr;
    QLabel* m_toastDot = nullptr;
    QTimer* m_toastTimer = nullptr;
};

// 16:9 实时画板部件 (模拟屏幕视口、桌面图标浮层与滤镜)
class StudioPreviewCanvas : public QWidget {
    Q_OBJECT
public:
    explicit StudioPreviewCanvas(QWidget* parent = nullptr);
    void setDimmingOpacity(qreal opacity);
    void setBlurRadius(int blurPx);
    void setShowDesktopIcons(bool show);
    void setWallpaperType(const QString& type);
    void setParticleDensity(int density);
    void setAccentColor(const QColor& color);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    qreal m_dimOpacity = 0.20;
    int m_blurRadius = 0;
    bool m_showDesktopIcons = true;
    QString m_wallpaperType = QStringLiteral("gargantua");
    int m_particleDensity = 30;
    QColor m_accentColor = QColor(255, 140, 20);
    QTimer* m_animTimer = nullptr;
    qreal m_animTime = 0.0;
};

} // namespace LinHub
