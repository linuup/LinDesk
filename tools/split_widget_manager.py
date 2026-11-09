import os
import subprocess

orig = subprocess.check_output(['git', 'show', 'HEAD:src/desktop/widgets/linwidget/WidgetManager.cpp'], text=True, encoding='utf-8', errors='ignore').splitlines(True)

# 1. WidgetManager_Internal.h
# lines 0 to 439 + resolveOptimalPackagePath (lines 4173 to 4285)
resolve_func = orig[4173:4286]
# prepend 'inline ' to resolveOptimalPackagePath definition
resolve_func[0] = 'inline ' + resolve_func[0]

internal_header = (
    ['#pragma once\n\n'] +
    orig[:439] +
    ['\n'] +
    resolve_func +
    ['\n} // namespace LinWidget\n']
)
with open('src/desktop/widgets/linwidget/WidgetManager_Internal.h', 'w', encoding='utf-8') as f:
    f.writelines(internal_header)

# 2. WidgetManager_Wallpaper.cpp
# forwardMouseMoveToWallpaper to forwardWheelToWallpaper: 1570 to 1625 (lines 1571 to 1625)
# startWallpaper to saveWallpaperConfig: 4286 to 4586 (lines 4287 to 4586)
wallpaper = [
    '#include "desktop/widgets/linwidget/WidgetManager_Internal.h"\n\n',
    'namespace LinWidget {\n\n',
    '// ============================================================================\n',
    '// WidgetManager Dynamic Wallpaper Subsystem Implementation\n',
    '// ============================================================================\n\n'
]
wallpaper += orig[1570:1625]
wallpaper += ['\n']
wallpaper += orig[4286:4586]
wallpaper += ['\n} // namespace LinWidget\n']
with open('src/desktop/widgets/linwidget/WidgetManager_Wallpaper.cpp', 'w', encoding='utf-8') as f:
    f.writelines(wallpaper)

# 3. WidgetManager_Config.cpp
# restoreFromConfig to layoutStates: 896 to 1068 (lines 897 to 1068)
# scheduleSaveToConfig to saveToDefaultConfig: 4153 to 4173 (lines 4154 to 4173)
config_part = [
    '#include "desktop/widgets/linwidget/WidgetManager_Internal.h"\n\n',
    'namespace LinWidget {\n\n',
    '// ============================================================================\n',
    '// WidgetManager Layout & Configuration Persistence Implementation\n',
    '// ============================================================================\n\n'
]
config_part += orig[896:1068]
config_part += ['\n']
config_part += orig[4153:4173]
config_part += ['\n} // namespace LinWidget\n']
with open('src/desktop/widgets/linwidget/WidgetManager_Config.cpp', 'w', encoding='utf-8') as f:
    f.writelines(config_part)

# 4. WidgetManager_SystemApi.cpp
# handleSystemInfoRequest to stopSystemInfoWatchesForInstance: 2126 to 2341 (lines 2127 to 2341)
sysapi_part = [
    '#include "desktop/widgets/linwidget/WidgetManager_Internal.h"\n\n',
    'namespace LinWidget {\n\n',
    '// ============================================================================\n',
    '// WidgetManager SystemInfo & Storage Request Implementation\n',
    '// ============================================================================\n\n'
]
sysapi_part += orig[2126:2341]
sysapi_part += ['\n} // namespace LinWidget\n']
with open('src/desktop/widgets/linwidget/WidgetManager_SystemApi.cpp', 'w', encoding='utf-8') as f:
    f.writelines(sysapi_part)

# 5. WidgetManager_Rpc.cpp
# handleWidgetManagerRequest to broadcastToWidgetInstances: 2346 to 4149 (lines 2347 to 4149)
rpc_part = [
    '#include "desktop/widgets/linwidget/WidgetManager_Internal.h"\n\n',
    'namespace LinWidget {\n\n',
    '// ============================================================================\n',
    '// WidgetManager JSON-RPC Protocol Dispatcher Implementation\n',
    '// ============================================================================\n\n'
]
rpc_part += orig[2346:4149]
rpc_part += ['\n} // namespace LinWidget\n']
with open('src/desktop/widgets/linwidget/WidgetManager_Rpc.cpp', 'w', encoding='utf-8') as f:
    f.writelines(rpc_part)

# 6. WidgetManager.cpp (Main Core)
main_core = [
    '#include "desktop/widgets/linwidget/WidgetManager_Internal.h"\n\n',
    'namespace LinWidget {\n\n',
    '// ============================================================================\n',
    '// WidgetManager Core Singleton, Lifecycle & Geometry Implementation\n',
    '// ============================================================================\n\n'
]
main_core += orig[440:896]
main_core += ['\n']
main_core += orig[1068:1570]
main_core += ['\n']
main_core += orig[1625:2126]
main_core += ['\n']
main_core += orig[2341:2346] # makeInstanceId
main_core += ['\n']
main_core += orig[4149:4153] # checkRuntimeHealth
main_core += ['\n']
main_core += orig[4586:]     # showDevTools + end of namespace LinWidget
with open('src/desktop/widgets/linwidget/WidgetManager.cpp', 'w', encoding='utf-8') as f:
    f.writelines(main_core)

print('Split WidgetManager generated successfully!')
