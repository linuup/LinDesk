#include <Windows.h>
#include <ole2.h>
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
#include "plugins/PluginManager.h"
#include "desktop/widgets/runtime_export.h"
#include "services/CrashHandler.h"
#include "services/LogManager.h"
#include "desktop/RightMenu.h"
#include "desktop/Win11MenuBridge.h"
using namespace ShortcutArrowManager;

#ifdef LINDESK_SOURCE_DIR
#include <QDateTime>

// Copy development resources from source tree to application output directory.
// This allows running and debugging the application directly from the build directory
// without needing a CMake post-build copy script.
static bool copyDirRecursively(const QString& sourcePath, const QString& targetPath)
{
    QDir sourceDir(sourcePath);
    if (!sourceDir.exists()) {
        return false;
    }
    QDir targetDir(targetPath);
    if (!targetDir.exists()) {
        if (!targetDir.mkpath(QStringLiteral("."))) {
            return false;
        }
    }

    const QFileInfoList entries = sourceDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries, QDir::Name);
    for (const QFileInfo& entry : entries) {
        const QString name = entry.fileName();
        // Skip common version control or cache files/folders
        if (name == QStringLiteral(".git") ||
            name == QStringLiteral("node_modules") ||
            name.compare(QStringLiteral("cache"), Qt::CaseInsensitive) == 0 ||
            name.compare(QStringLiteral("tmp"), Qt::CaseInsensitive) == 0 ||
            name.compare(QStringLiteral("temp"), Qt::CaseInsensitive) == 0) {
            continue;
        }

        const QString targetEntryPath = targetDir.filePath(name);
        if (entry.isDir()) {
            if (!copyDirRecursively(entry.absoluteFilePath(), targetEntryPath)) {
                return false;
            }
        } else {
            if (QFile::exists(targetEntryPath)) {
                const QFileInfo targetInfo(targetEntryPath);
                if (entry.lastModified() <= targetInfo.lastModified() && entry.size() == targetInfo.size()) {
                    continue; // Skip copying if same size and not newer
                }
                QFile::remove(targetEntryPath);
            }
            QFile::copy(entry.absoluteFilePath(), targetEntryPath);
        }
    }
    return true;
}

static void ensureDevelopmentResources()
{
    const QString sourceDir = QString::fromUtf8(LINDESK_SOURCE_DIR) + QStringLiteral("/src/resources");
    const QString targetDir = QCoreApplication::applicationDirPath() + QStringLiteral("/resources");
    if (QDir(sourceDir).exists()) {
        copyDirRecursively(sourceDir, targetDir);
    }
}
#endif

int main(int argc, char *argv[])
{
    const HRESULT oleHr = OleInitialize(nullptr);
    CrashHandler::initialize();
    LogManager::initialize();

    SetConsoleOutputCP(CP_UTF8);
    // Set Windows 1:1 Desktop Standard font
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    // Enable high-resolution icons
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QSettings startupSettings(AppPaths::configFilePath(), QSettings::IniFormat);
    const bool useOpenGL = startupSettings.value("General/UseOpenGL", false).toBool();
    if (useOpenGL) {
        QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    }
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    //qSetMessagePattern("%{time yyyy-MM-dd hh:mm:ss}--[%{type}]--%{file}--%{function}--%{line}:%{message}");
    //qSetMessagePattern("[%{type}]--%{file}--%{function}--%{line}:%{message}");
    
    QApplication app(argc, argv);
#ifdef LINDESK_SOURCE_DIR
    ensureDevelopmentResources();
#endif
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
    if (linDesktop.isSecondaryInstance()) {
        int result = app.exec();
        LogManager::shutdown();
        if (SUCCEEDED(oleHr)) {
            OleUninitialize();
        }
        return result;
    }

    LinDeskHoveredItemServer hoveredItemServer;
    launchOptions.apply(&linDesktop);
    linDesktop.show();
    hoveredItemServer.startListening();
    if (RightMenu::isWindows11SystemContextMenuSupported()) {
        Win11MenuBridge::warmupHost();
    }
    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        PluginManager::shutdown();
        LinWidgetRuntimeBridge::instance().shutdown();
    });
    qDebug() << isShortcutArrowEnabled();
    qDebug() << isUACShieldEnabled();
    int result = app.exec();
    LogManager::shutdown();
    if (SUCCEEDED(oleHr)) {
        OleUninitialize();
    }
    return result;
}
