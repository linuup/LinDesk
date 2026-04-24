# Win11MenuIsland

This project now lives directly in `D:\Desktop\LinDesk\W11MenuUI`.

It is a native Win32 plus XAML Islands prototype inspired by `D:\Desktop\LinDesk\Win11ContextMenu.exe`. The current implementation uses a persistent XAML panel hosted inside a top level Win32 popup window.

## Structure

1. Win32 popup host window
2. `WindowsXamlManager`
3. `DesktopWindowXamlSource`
4. Root XAML `Grid` and `Border`
5. Runtime built menu rows with expandable sections for `View`, `Sort by`, and `New`

## Key Files

`main.cpp`
Host window, XAML island setup, panel layout, and command handlers

`pch.h`
WinRT and XAML Islands includes

`app.manifest`
Compatibility metadata required by XAML Islands

`Win11MenuIsland.vcxproj`
Visual Studio 2022 x64 native project

`Win11ContextMenu`
Icon assets for the Windows 11 menu UI

## Build

Open `Win11MenuIsland.sln` in Visual Studio 2022 and build `x64 Release` or `x64 Debug`.

You can also build from a Developer Command Prompt:

```bat
MSBuild Win11MenuIsland.vcxproj /p:Configuration=Release /p:Platform=x64
```
