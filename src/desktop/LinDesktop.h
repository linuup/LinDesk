#pragma once
#include <QWidget>
#include <QDialog>
#include <QLabel>
#include <QApplication>
#include <QScreen>
#include <QList>
#include <QDir>
#include "desktop/ListItem.h"
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
#include <windows.h>
class LinDesktop : public QWidget {
    Q_OBJECT
    Q_PROPERTY(float transitionOpacity READ transitionOpacity WRITE setTransitionOpacity)

public:
    explicit LinDesktop(QWidget* parent = nullptr);
    SelectArea* selectArea; //选择区域对象

    //First Floor
    ListView* listView;
    DesktopInfoMonitor* m_monitor = nullptr;
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
    void rebuildScaledWallpaperPixmaps();
	// 请求绘制鼠标悬浮效果
    void requestHoverEffect();
    void checkFirstLaunchPrompt();
    void initializeTrayIcon();
    bool shouldMinimizeToTray() const;
    void restoreFromTray();
    void exitFromTray();
    
    void embedToDesktop();
    void unembedFromDesktop();
    bool isEmbedded() const { return m_isEmbedded; }

private:
    QString m_configPath;
    QPixmap m_backImagePixmap;
    QPixmap m_prevBackImagePixmap;
    QPixmap m_scaledBackImagePixmap;
    QPixmap m_prevScaledBackImagePixmap;
    float m_transitionOpacity = 1.0f;
    int m_transitionDuration = 1000; // Default 500ms
    QVariantAnimation* m_transitionAnimation = nullptr;
    QTimer* m_wallpaperLoadTimer = nullptr;
    QString m_currentWallpaperPath;
    QString m_pendingWallpaperPath;
    quint64 m_wallpaperLoadRequestId = 0;
    HANDLE m_singleInstanceMutex = nullptr;
    QSystemTrayIcon* m_trayIcon = nullptr;
    QMenu* m_trayMenu = nullptr;
    QAction* m_showAction = nullptr;
    QAction* m_exitAction = nullptr;
    bool m_isQuittingFromTray = false;
    bool m_hasShownTrayHint = false;
    
    QPushButton* m_embedBtn = nullptr;
    bool m_isEmbedded = false;
    bool m_firstLaunchPromptShown = false;
    bool m_isFirstLaunch = true;

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

