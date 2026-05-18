#pragma once

#include <QString>
#include <QStringList>
#include <vector>

#include <ShellContextMenu.h>

class ShellMenuExclusionStore
{
public:
    static std::vector<ShellMenuExclusionEntry> load(const QString& key);
    static std::vector<ShellMenuExclusionEntry> load(const std::wstring& key);
    static std::vector<ShellMenuExclusionEntry> loadForKeys(const std::vector<std::wstring>& keys);
    static void save(const QString& key, const std::vector<ShellMenuExclusionEntry>& entries);
    static void save(const std::wstring& key, const std::vector<ShellMenuExclusionEntry>& entries);
    static void add(const QString& key, const ShellMenuExclusionEntry& entry);
    static void add(const QString& key, const QString& rawText);
    static void add(const std::wstring& key, const std::wstring& rawText);
    static void remove(const QString& key, const ShellMenuExclusionEntry& entry);
    static void removeByText(const QString& key,
        const QString& rawText,
        const QString& normalizedText = QString(),
        const QString& matchText = QString(),
        const QStringList& matchPath = QStringList());
    static bool contains(const std::wstring& key, const std::wstring& rawText);

private:
    static void ensureMigrated();
};
