#pragma once
#include <QWidget>
#include <QDialog>
#include <QLabel>
#include <QApplication>
#include <QScreen>
#include <QList>
#include <QDir>
#include <QUuid>
#include <QVector>
#include <QSharedPointer>
#include "desktop/RightMenu.h"
#include <QtConcurrent/qtconcurrentrun>
#include "desktop/ListView.h"
#include "monitor/IRegistryListenerInterface.h"
#include "monitor/DesktopInfoMonitor.h"
#include <QScreen>
#include <QVariantAnimation>
#include <QPainter>
#include <QTimer>
#include <QImage>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QCloseEvent>
#include <QMetaObject>
#include <QPointer>
#include <QElapsedTimer>
#include <windows.h>
class FluentDialog;
class LinDesktop : public QWidget {
    Q_OBJECT
    Q_PROPERTY(float transitionOpacity READ transitionOpacity WRITE setTransitionOpacity)

public:
    explicit LinDesktop(QWidget* parent = nullptr);
    QPointer<SelectArea> selectArea; //选择区域对象

    //First Floor
    QPointer<ListView> listView;
    QPointer<DesktopInfoMonitor> m_monitor;
    ~LinDesktop();

    QRect m_lastSelectRect;
	
    float transitionOpacity() const { return m_transitionOpacity; }
    void setTransitionOpacity(float opacity);

    int transitionDuration() const { return m_transitionDuration; }
    void setTransitionDuration(int ms);

    void onWallpaperChanged();

	//QRect drawingMaskRect;    //拖拽选择框矩形
    void loadBackImage();
    void applyLoadedWallpaper(const QString& path, const QImage& image);
    void applyFallbackWallpaper(const QString& path, const QString& reason);
    void rebuildScaledWallpaperPixmaps();
	// 请求绘制鼠标悬浮效果
    void requestHoverEffect();
    void checkFirstLaunchPrompt();
    void initializeTrayIcon();
    bool shouldMinimizeToTray() const;
    void restoreFromTray();
    void exitFromTray();
    void connectScreenSignals(QScreen* screen);
    void handleDpiOrScreenMetricsChanged();
    void clearSelectionOverlay();
    
    void embedToDesktop();
    void unembedFromDesktop();
    bool isEmbedded() const { return m_isEmbedded; }
    void forceDesktopRepaint();
    bool hasDesktopHostConflict() const { return m_hasDesktopHostConflict; }
    void handleDesktopHostWindowEvent(HWND hwnd, DWORD event);

private:
    void startDesktopHostWindowMonitor();
    void stopDesktopHostWindowMonitor();
    void rescanDesktopHostChildren();
    void scheduleDesktopHostChildrenRescan();
    void scheduleLinWidgetLayerRecovery();
    void showDesktopHostConflictDialog(HWND conflictHwnd);
    void repairDesktopHostPresentation();
    HWND findThirdPartyDesktopHostChild() const;
    bool isDesktopHostWindow(HWND hwnd) const;
    bool isWindowUnderDesktopHost(HWND hwnd) const;
    bool isOwnProcessWindow(HWND hwnd) const;
    bool isOwnWidgetHostWindow(HWND hwnd) const;
    bool isOwnWidgetHostWindowOrDescendant(HWND hwnd) const;
    bool isCandidateThirdPartyWindow(HWND hwnd) const;
    QString windowClassName(HWND hwnd) const;
    QString windowProcessImageName(HWND hwnd) const;

    QString m_configPath;
    QPixmap m_backImagePixmap;
    QPixmap m_prevBackImagePixmap;
    QPixmap m_scaledBackImagePixmap;
    QPixmap m_prevScaledBackImagePixmap;
    float m_transitionOpacity = 1.0f;
    int m_transitionDuration = 1000; // Default 500ms
    QPointer<QVariantAnimation> m_transitionAnimation;
    QPointer<QTimer> m_wallpaperLoadTimer;
    QString m_currentWallpaperPath;
    QString m_pendingWallpaperPath;
    QString m_fallbackWallpaperPath;
    quint64 m_wallpaperLoadRequestId = 0;
    HANDLE m_singleInstanceMutex = nullptr;
    QPointer<QSystemTrayIcon> m_trayIcon;
    QPointer<QMenu> m_trayMenu;
    QPointer<QAction> m_showAction;
    QPointer<QAction> m_exitAction;
    bool m_isQuittingFromTray = false;
    bool m_hasShownTrayHint = false;
    
    QPointer<QPushButton> m_embedBtn;
    bool m_isEmbedded = false;
    bool m_firstLaunchPromptShown = false;
    bool m_isFirstLaunch = true;
    QPointer<QScreen> m_connectedScreen;
    QMetaObject::Connection m_screenGeometryChangedConnection;
    QMetaObject::Connection m_screenDpiChangedConnection;
    QMetaObject::Connection m_windowScreenChangedConnection;
    QVector<HWINEVENTHOOK> m_desktopHostWindowEventHooks;
    bool m_hasDesktopHostConflict = false;
    HWND m_desktopHostConflictHwnd = nullptr;
    bool m_desktopHostRescanPending = false;
    bool m_desktopHostRescanAgain = false;
    bool m_linWidgetLayerRecoveryPending = false;
    UINT m_shellHookMessage = 0;
    bool m_shellHookRegistered = false;
    bool m_desktopHostConflictDialogShown = false;
    QPointer<FluentDialog> m_desktopHostConflictDialog;

private slots:

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event);
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;

    void contextMenuEvent(QContextMenuEvent* event) override;
    bool nativeEvent(const QByteArray & eventType, void* message, long* result) override;
    
};

