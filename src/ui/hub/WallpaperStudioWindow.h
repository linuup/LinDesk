#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QScrollArea>
#include <QSlider>
#include <QCheckBox>
#include <QPointer>
#include <QTimer>
#include <QPaintEvent>
#include "ui/Fluent/FluentBaseWidget.h"
#include "ui/Fluent/FluentToggleSwitch.h"

namespace LinHub {

class StudioPreviewCanvas;

class WallpaperStudioWindow : public FluentBaseWidget {
    Q_OBJECT

public:
    static WallpaperStudioWindow* instance();
    static void showStudio();

    explicit WallpaperStudioWindow(QWidget* parent = nullptr);
    ~WallpaperStudioWindow() override = default;

public slots:
    void switchToTab(int tabIndex);
    void applyWallpaperPreset(const QString& wallpaperId);
    void stopCurrentWallpaper();
    void toggleMousePhysics(bool enabled);
    void importLocalMedia();
    void exportDiagnosticsPackage();

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void initUI();
    void setupHeader();
    void setupNavPills();
    QWidget* createGalleryPage();
    QWidget* createStudioPage();
    QWidget* createWidgetsPage();
    QWidget* createDevAdminPage();

    void refreshActiveWallpaperStatus();
    void updateStudioDimming(int val);
    void updateStudioBlur(int val);

private:
    static QPointer<WallpaperStudioWindow> s_instance;

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

    // Studio elements
    StudioPreviewCanvas* m_studioCanvas = nullptr;
    QSlider* m_dimSlider = nullptr;
    QLabel* m_dimValLabel = nullptr;
    QSlider* m_blurSlider = nullptr;
    QLabel* m_blurValLabel = nullptr;
    QCheckBox* m_mockIconsCheckbox = nullptr;
    QString m_currentEditingWallpaper = QStringLiteral("gargantua");

    // Dev elements
    QLabel* m_ipcLogView = nullptr;
    QTimer* m_statusTimer = nullptr;
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

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    qreal m_dimOpacity = 0.20;
    int m_blurRadius = 0;
    bool m_showDesktopIcons = true;
    QString m_wallpaperType = QStringLiteral("gargantua");
    QTimer* m_animTimer = nullptr;
    qreal m_animTime = 0.0;
};

} // namespace LinHub
