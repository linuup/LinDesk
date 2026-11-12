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
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QMap>
#include <QList>
#include "ui/Fluent/FluentBaseWidget.h"
#include "ui/Fluent/FluentToggleSwitch.h"
#include <QTcpSocket>

namespace LinHub {

class StudioPreviewCanvas;

struct WallpaperItem {
    QString id;
    QString name;
    QString tag;
    QString description;
    QString packagePath;
    QString thumbStyle;
    QString previewPath;
    QString wallpaperType = QStringLiteral("html"); // html, video, image
    bool isDevelopment = false;
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
    static void hideIfOpen();

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
    bool createAndInstallQuickWallpaper(const QString& filePath, QString* outId = nullptr);

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
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void initUI();
    void setupHeader();
    void setupNavPills();
    QWidget* createWallpaperHubPage(); // Master-Detail 二合一核心页面 (58% 画廊瀑布流 + 42% 16:9 画板与调参)
    QWidget* createWidgetsPage();
    QWidget* createDevAdminPage();
    void checkAndSwitchHubSourceIfNeeded(bool forceReload = false);
    QString determineTargetHubUrl();
    bool setupHtmlHub();

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
    QString m_loadedHubUrl;
    QStackedWidget* m_stack = nullptr;
    QPushButton* m_tabWallpaperHubBtn = nullptr;
    QPushButton* m_tabWidgetsBtn = nullptr;
    QPushButton* m_tabDevBtn = nullptr;

    // Master-Detail Wallpaper Elements
    QLabel* m_heroStatusDot = nullptr;
    QLabel* m_heroStatusText = nullptr;
    QLabel* m_heroTitle = nullptr;
    QLabel* m_heroDesc = nullptr;
    FluentToggleSwitch* m_heroMousePhysicsToggle = nullptr;
    QPushButton* m_heroStopBtn = nullptr;
    QGridLayout* m_galleryGridLayout = nullptr;
    QWidget* m_galleryGridContainer = nullptr;

    // Detail Panel Elements (Preview Canvas & Dual-Tier Properties)
    StudioPreviewCanvas* m_studioCanvas = nullptr;
    QLabel* m_detailWallpaperName = nullptr;
    QLabel* m_detailWallpaperMeta = nullptr;
    QPushButton* m_detailApplyBtn = nullptr;

    // Custom properties container
    QWidget* m_dynamicPropertiesContainer = nullptr;
    QVBoxLayout* m_dynamicPropertiesLayout = nullptr;
    QMap<QString, QJsonValue> m_currentPropertyValues;

    // Host 4-piece filter and audio controls
    QSlider* m_volumeSlider = nullptr;
    QLabel* m_volumeValLabel = nullptr;
    FluentToggleSwitch* m_muteOnSoundToggle = nullptr;
    QSlider* m_dimSlider = nullptr;
    QLabel* m_dimValLabel = nullptr;
    QSlider* m_blurSlider = nullptr;
    QLabel* m_blurValLabel = nullptr;
    QComboBox* m_fitModeCombo = nullptr;
    QCheckBox* m_mockIconsCheckbox = nullptr;

    // Developer exclusive box
    QWidget* m_devToolsBox = nullptr;
    QPushButton* m_devOpenFolderBtn = nullptr;
    QPushButton* m_devInspectBtn = nullptr;
    QPushButton* m_devReloadBtn = nullptr;

    QString m_currentEditingWallpaper = QStringLiteral("lindesk.wallpaper");

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
    void setPreviewImage(const QPixmap& pixmap);
    void setParticleDensity(int density);
    void setAccentColor(const QColor& color);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    qreal m_dimOpacity = 0.20;
    int m_blurRadius = 0;
    bool m_showDesktopIcons = true;
    QString m_wallpaperType = QStringLiteral("living");
    QPixmap m_customPreviewPixmap;
    int m_particleDensity = 30;
    QColor m_accentColor = QColor(255, 140, 20);
    QTimer* m_animTimer = nullptr;
    qreal m_animTime = 0.0;
};

} // namespace LinHub
