#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <QPoint>
#include <QString>
#include <QVector>
#include <QMetaType>
#include "CommonEnum.h"

// Exclusion rule definition
struct ShellMenuExclusionEntry
{
    std::wstring rawText;
    std::wstring normalizedText;
    std::wstring matchText;
    std::vector<std::wstring> menuPath;
    std::vector<std::wstring> matchPath;
    bool enabled = true;
};

// Target classification
enum class ShellTargetType {
    DesktopBackground,
    BoxBackground,
    File,
    Folder,
    Shortcut,
    DiskLetter,
    SpecialFolder,
    MultiObject
};

// Custom menu item definition (decoupled from ShellContextMenu)
struct ShellCustomMenuItem {
    std::wstring text;
    std::function<void()> callback;
    std::vector<ShellCustomMenuItem> children;
    bool isSeparator = false;
    bool checkable = false;
    bool checked = false;
    bool enabled = true;
    HBITMAP hBitmap = nullptr;
};

// Helper for dual value / function call access syntax on globalPos
struct ShellMenuPos : public QPoint {
    using QPoint::QPoint;
    ShellMenuPos() = default;
    ShellMenuPos(const QPoint& pt) : QPoint(pt) {}
    ShellMenuPos(int xpos, int ypos) : QPoint(xpos, ypos) {}
    ShellMenuPos& operator=(const QPoint& pt) { QPoint::operator=(pt); return *this; }
    const QPoint& operator()() const { return *this; }
    QPoint& operator()() { return *this; }
};

// Context menu request contract
struct ShellMenuRequest {
    uint64_t requestId = 0;
    ShellTargetType targetType = ShellTargetType::DesktopBackground;
    QString targetPath;
    QVector<QString> multiPaths;
    SpecialFolders specialFolder = SpecialFolders::UnknownFolder;
    ShellMenuPos globalPos;
    bool extendedVerbs = false;
    HWND ownerHwnd = nullptr;
    std::vector<ShellCustomMenuItem> customItems;
    std::vector<std::wstring> exclusionKeys;
    bool showMenu = false;
};

// Context menu execution result contract
struct ShellMenuResult {
    uint64_t requestId = 0;
    bool executed = false;
    std::string verb;
    uint32_t commandId = 0;
    bool isCustomAction = false;
    std::function<void()> customCallback;
};

Q_DECLARE_METATYPE(ShellMenuRequest)
Q_DECLARE_METATYPE(ShellMenuResult)
