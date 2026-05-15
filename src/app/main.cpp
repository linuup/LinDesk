#include <Windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <QDebug>
#include <iostream>
#include <QApplication>
#include <QDir>
#include <QFont>
#include <QIcon>
#include <QFileInfo>
#include <QSettings>
#include <QTranslator>
#include <QVector>
#include <QStringList>
#include "app/AppFonts.h"
#include "app/LinDesk.h"
#include "SystemApi/SystemApi.h"
#include "SystemApi/Tool.h"
#include "desktop/LinDesktop.h"
#include "app/AppLaunchOptions.h"
#include "app/LanguageManager.h"
#include "services/AppPaths.h"
#include "services/LinDeskHoveredItemServer.h"
using namespace ShortcutArrowManager;

int main(int argc, char *argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QSettings startupSettings(AppPaths::configFilePath(), QSettings::IniFormat);
    const bool useOpenGL = startupSettings.value("General/UseOpenGL", false).toBool();
    if (useOpenGL) {
        QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    }
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    // 启用缩放支持
    //QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    //// 允许图标使用高分像素图
    //QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    // 获取桌面上所有文件的列表

    //qSetMessagePattern("%{time yyyy-MM-dd hh:mm:ss}--[%{type}]--%{file}--%{function}--%{line}:%{message}");
    //qSetMessagePattern("[%{type}]--%{file}--%{function}--%{line}:%{message}");
    
    QApplication app(argc, argv);
    LanguageManager::instance().initialize(app);
    app.setWindowIcon(QIcon(":/Assets/icon.png"));
    app.setQuitOnLastWindowClosed(false);
    QApplication::setStyle("Fusion");

    const AppLaunchOptions launchOptions = AppLaunchOptions::fromArguments(QCoreApplication::arguments());
    
    // Set Windows 1:1 Desktop Standard font
    QFont defaultFont = AppFonts::uiFont(9);
    app.setFont(defaultFont);
	//qDebug() << "Application Dir:" << app.applicationDirPath();
    //LinDesk *lindesk = LinDesk::instance();
    //lindesk->show();
    LinDesktop linDesktop;
    LinDeskHoveredItemServer hoveredItemServer;
    launchOptions.apply(&linDesktop);
    linDesktop.show();
    hoveredItemServer.startListening();
    qDebug() << isShortcutArrowEnabled();
    qDebug() << isUACShieldEnabled();
    return app.exec();
}
