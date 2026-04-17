#pragma once

#include <QStringList>

class LinDesktop;

class AppLaunchOptions {
public:
    static AppLaunchOptions fromArguments(const QStringList& arguments);

    bool debugEnabled() const { return m_debugEnabled; }
    void apply(LinDesktop* linDesktop = nullptr) const;

private:
    void handleDebugOption(LinDesktop* linDesktop) const;

    bool m_debugEnabled = false;
};
