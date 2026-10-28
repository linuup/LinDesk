#include "app/AppLaunchOptions.h"
#include "desktop/LinDesktop.h"
#include "services/AppPaths.h"
#include <QDir>
#include <QFile>

namespace {
void ensureDesktopDebugLogExists()
{
    const QString logFilePath = QDir(AppPaths::userLogsDir()).filePath(QStringLiteral("desktop.log"));
    QFile logFile(logFilePath);
    if (logFile.exists()) {
        return;
    }

    if (logFile.open(QIODevice::WriteOnly)) {
        logFile.close();
    }
}
}

AppLaunchOptions AppLaunchOptions::fromArguments(const QStringList& arguments)
{
    AppLaunchOptions options;
#ifdef QT_DEBUG
    options.m_debugEnabled = true;
#endif

    for (int i = 1; i < arguments.size(); ++i) {
        const QString argument = arguments[i].trimmed();
        if (argument.compare("-debug", Qt::CaseInsensitive) == 0) {
            options.m_debugEnabled = true;
        }
    }

    return options;
}

void AppLaunchOptions::apply(LinDesktop* linDesktop) const
{
    if (m_debugEnabled) {
        handleDebugOption(linDesktop);
    }
}

void AppLaunchOptions::handleDebugOption(LinDesktop* linDesktop) const
{
    ensureDesktopDebugLogExists();
    if (linDesktop && linDesktop->listView) {
        linDesktop->listView->showDebugPanel();
    }
}
