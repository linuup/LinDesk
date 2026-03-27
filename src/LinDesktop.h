#pragma once
#include <QWidget>
#include <QDialog>
#include <QLabel>
#include <QApplication>
#include <QScreen>
#include <QList>
#include <QDir>
#include "ListItem.h"
#include <QUuid>
#include <QVector>
#include <QSharedPointer>
#include <RightMenu.h>
#include <QtConcurrent/qtconcurrentrun>
#include "ListView.h"
#include "Monitor/IRegistryListenerInterface.h"
#include "Monitor/DesktopInfoMonitor.h"
#include <QScreen>
#include <QVariantAnimation>
#include <QPainter>
#include <QTimer>
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
	QPoint m_startPos;    //鼠标点击坐标
	
    float transitionOpacity() const { return m_transitionOpacity; }
    void setTransitionOpacity(float opacity);

    int transitionDuration() const { return m_transitionDuration; }
    void setTransitionDuration(int ms);

    void onWallpaperChanged();

	//QRect drawingMaskRect;    //拖拽选择框矩形
    void loadBackImage();
	// 请求绘制鼠标悬浮效果
    void requestHoverEffect();
    
    void embedToDesktop();
    void unembedFromDesktop();
    bool isEmbedded() const { return m_isEmbedded; }

private:
    QPixmap m_backImagePixmap;
    QPixmap m_prevBackImagePixmap;
    float m_transitionOpacity = 1.0f;
    int m_transitionDuration = 1000; // Default 500ms
    QVariantAnimation* m_transitionAnimation = nullptr;
    QTimer* m_wallpaperLoadTimer = nullptr;
    QString m_currentWallpaperPath;
    
    QPushButton* m_embedBtn = nullptr;
    bool m_isEmbedded = false;

private slots:

protected:
    void mousePressEvent(QMouseEvent* event);
    void mouseMoveEvent(QMouseEvent* event);
    void mouseReleaseEvent(QMouseEvent* event);
    void paintEvent(QPaintEvent* event);
    void resizeEvent(QResizeEvent* event) override;

    void contextMenuEvent(QContextMenuEvent* event) override;
    bool nativeEvent(const QByteArray & eventType, void* message, long* result) override;
    
};

