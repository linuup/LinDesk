#pragma once

#include <string>
#include <vector>

struct ShellMenuExclusionEntry
{
    std::wstring rawText;
    std::wstring normalizedText;
    std::wstring matchText;
    std::vector<std::wstring> menuPath;
    std::vector<std::wstring> matchPath;
    bool enabled = true;
};