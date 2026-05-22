#include "widgethost/WidgetHostRuntime.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>

int main(int argc, char* argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(true);

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption widgetIdOption(QStringLiteral("widget-id"), QStringLiteral("Widget instance id."), QStringLiteral("id"));
    const QCommandLineOption packageOption(QStringLiteral("package"), QStringLiteral("Widget package path."), QStringLiteral("path"));
    const QCommandLineOption ipcOption(QStringLiteral("ipc"), QStringLiteral("IPC server name."), QStringLiteral("name"));
    const QCommandLineOption parentPidOption(QStringLiteral("parent-pid"), QStringLiteral("LinDesk parent process id."), QStringLiteral("pid"));
    const QCommandLineOption parentHwndOption(QStringLiteral("parent-hwnd"), QStringLiteral("Parent ListView window handle."), QStringLiteral("hwnd"));
    const QCommandLineOption permissionsOption(QStringLiteral("permissions"), QStringLiteral("Granted widget permissions."), QStringLiteral("permissions"));
    const QCommandLineOption developmentOption(QStringLiteral("development"), QStringLiteral("Run widget in development mode."));
    const QCommandLineOption autoReloadOption(QStringLiteral("auto-reload"), QStringLiteral("Widget is managed by auto reload."));
    const QCommandLineOption sharedRuntimeOption(QStringLiteral("shared-runtime"), QStringLiteral("Run one WebView2 host for multiple widgets."));
    const QCommandLineOption xOption(QStringLiteral("x"), QStringLiteral("Initial x."), QStringLiteral("x"), QStringLiteral("0"));
    const QCommandLineOption yOption(QStringLiteral("y"), QStringLiteral("Initial y."), QStringLiteral("y"), QStringLiteral("0"));
    const QCommandLineOption widthOption(QStringLiteral("width"), QStringLiteral("Initial width."), QStringLiteral("width"), QStringLiteral("300"));
    const QCommandLineOption heightOption(QStringLiteral("height"), QStringLiteral("Initial height."), QStringLiteral("height"), QStringLiteral("120"));
    parser.addOptions({ widgetIdOption, packageOption, ipcOption, parentPidOption, parentHwndOption, permissionsOption, developmentOption, autoReloadOption, sharedRuntimeOption, xOption, yOption, widthOption, heightOption });
    parser.process(app);

    const QString widgetId = parser.value(widgetIdOption).trimmed();
    const QString packagePath = parser.value(packageOption).trimmed();
    const QString ipcServerName = parser.value(ipcOption).trimmed();
    const QStringList grantedPermissions = parser.value(permissionsOption).split(QStringLiteral(","), Qt::SkipEmptyParts);
    const bool isDevelopment = parser.isSet(developmentOption);
    const bool autoReload = parser.isSet(autoReloadOption);
    const bool sharedRuntime = parser.isSet(sharedRuntimeOption);
    const quint32 parentPid = parser.value(parentPidOption).toUInt();
    const quint64 parentWindowId = parser.value(parentHwndOption).toULongLong();
    if (widgetId.isEmpty() || ipcServerName.isEmpty() || (!sharedRuntime && packagePath.isEmpty())) {
        qCritical() << "WidgetHost requires --widget-id, --ipc, and --package unless --shared-runtime is set.";
        return 2;
    }

    const QRect initialGeometry(
        parser.value(xOption).toInt(),
        parser.value(yOption).toInt(),
        parser.value(widthOption).toInt(),
        parser.value(heightOption).toInt());

    LinWidget::WidgetHostRuntime runtime(widgetId, packagePath, ipcServerName, initialGeometry, parentPid, parentWindowId, grantedPermissions, isDevelopment, autoReload, sharedRuntime);
    QString errorMessage;
    if (!runtime.initialize(&errorMessage)) {
        qCritical().noquote() << errorMessage;
        return 3;
    }

    runtime.show();
    return app.exec();
}
