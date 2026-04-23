#include "app/AppLaunchOptions.h"
#include "desktop/LinDesktop.h"

AppLaunchOptions AppLaunchOptions::fromArguments(const QStringList& arguments)
{
    AppLaunchOptions options;

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
    Q_UNUSED(linDesktop);
}
