import os
import subprocess

orig = subprocess.check_output(['git', 'show', 'HEAD:src/desktop/ListView.cpp'], text=True, encoding='utf-8', errors='ignore').splitlines(True)

# 1. ListView_Internal.h (lines 0 to 87)
internal_header = ['#pragma once\n\n'] + orig[:87]
with open('src/desktop/ListView_Internal.h', 'w', encoding='utf-8') as f:
    f.writelines(internal_header)

# 2. ListView_Widgets.cpp
widgets = [
    '#include "desktop/ListView_Internal.h"\n\n',
    '// ============================================================================\n',
    '// ListView LinWidgets Host Integration & Blocked Cells Overlay Implementation\n',
    '// ============================================================================\n\n'
]
widgets += orig[151:260]
widgets += ['\n']
widgets += orig[320:330]
widgets += ['\n']
widgets += orig[480:796]
with open('src/desktop/ListView_Widgets.cpp', 'w', encoding='utf-8') as f:
    f.writelines(widgets)

# 3. ListView_Paint.cpp
paint = [
    '#include "desktop/ListView_Internal.h"\n\n',
    '// ============================================================================\n',
    '// ListView Paint & Rendering Implementation\n',
    '// ============================================================================\n\n'
]
paint += orig[1506:1815]
paint += ['\n']
paint += orig[4126:4159]
paint += ['\n']
paint += orig[4493:4546]
with open('src/desktop/ListView_Paint.cpp', 'w', encoding='utf-8') as f:
    f.writelines(paint)

# 4. ListView_Input.cpp
input_events = [
    '#include "desktop/ListView_Internal.h"\n\n',
    '// ============================================================================\n',
    '// ListView Mouse, Keyboard, Wheel & Window Event Implementation\n',
    '// ============================================================================\n\n'
]
input_events += orig[883:947]
input_events += ['\n']
input_events += orig[1815:2825]
with open('src/desktop/ListView_Input.cpp', 'w', encoding='utf-8') as f:
    f.writelines(input_events)

# 5. ListView_Items.cpp
items = [
    '#include "desktop/ListView_Internal.h"\n\n',
    '// ============================================================================\n',
    '// ListView Item CRUD, Sorting & Attribute Filtering Implementation\n',
    '// ============================================================================\n\n'
]
items += orig[808:883]
items += ['\n']
items += orig[3000:3289]
items += ['\n']
items += orig[3977:4126]
with open('src/desktop/ListView_Items.cpp', 'w', encoding='utf-8') as f:
    f.writelines(items)

# 6. ListView.cpp (Main Core)
main_core = [
    '#include "desktop/ListView_Internal.h"\n\n',
    '// ============================================================================\n',
    '// ListView Core Lifecycle, Geometry & Selection State Implementation\n',
    '// ============================================================================\n\n'
]
main_core += orig[87:151]
main_core += ['\n']
main_core += orig[260:320]
main_core += ['\n']
main_core += orig[330:480]
main_core += ['\n']
main_core += orig[796:808]
main_core += ['\n']
main_core += orig[947:1506]
main_core += ['\n']
main_core += orig[2825:3000]
main_core += ['\n']
main_core += orig[3289:3977]
main_core += ['\n']
main_core += orig[4159:4493]
with open('src/desktop/ListView.cpp', 'w', encoding='utf-8') as f:
    f.writelines(main_core)

print('Split ListView generated successfully!')
