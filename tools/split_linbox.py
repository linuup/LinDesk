import os

with open('src/box/LinBox.cpp', 'r', encoding='utf-8', errors='ignore') as f:
    orig = f.readlines()

# 1. LinBox_Internal.h
internal_header = ['#pragma once\n\n'] + orig[:643]
with open('src/box/LinBox_Internal.h', 'w', encoding='utf-8') as f:
    f.writelines(internal_header)

# 2. LinBox_MappedFolder.cpp
mapped = [
    '#include "box/LinBox_Internal.h"\n\n',
    '// ============================================================================\n',
    '// LinBox Mapped Folder Implementation\n',
    '// ============================================================================\n\n'
]
mapped += orig[1304:1395]
mapped += ['\n']
mapped += orig[1545:1794]
with open('src/box/LinBox_MappedFolder.cpp', 'w', encoding='utf-8') as f:
    f.writelines(mapped)

# 3. LinBox_Events.cpp
events = [
    '#include "box/LinBox_Internal.h"\n\n',
    '// ============================================================================\n',
    '// LinBox Mouse, Focus, Keyboard & Selection Event Implementation\n',
    '// ============================================================================\n\n'
]
events += orig[1794:2466]
events += ['\n']
events += orig[2809:3071]
events += ['\n']
events += orig[4139:4356]
with open('src/box/LinBox_Events.cpp', 'w', encoding='utf-8') as f:
    f.writelines(events)

# 4. LinBox_DragDrop.cpp
dragdrop = [
    '#include "box/LinBox_Internal.h"\n\n',
    '// ============================================================================\n',
    '// LinBox Drag & Drop and Reorder Implementation\n',
    '// ============================================================================\n\n'
]
dragdrop += orig[3160:4139]
dragdrop += ['\n']
dragdrop += orig[4955:4983]
with open('src/box/LinBox_DragDrop.cpp', 'w', encoding='utf-8') as f:
    f.writelines(dragdrop)

# 5. LinBox_Paint.cpp
paint = [
    '#include "box/LinBox_Internal.h"\n\n',
    '// ============================================================================\n',
    '// LinBox Paint, Blur & Visual Effects Implementation\n',
    '// ============================================================================\n\n'
]
paint += orig[2466:2809]
paint += ['\n']
paint += orig[4587:4813]
paint += ['\n']
paint += orig[4983:5023]
with open('src/box/LinBox_Paint.cpp', 'w', encoding='utf-8') as f:
    f.writelines(paint)

# 6. LinBox.cpp (Core)
main_core = [
    '#include "box/LinBox_Internal.h"\n\n',
    '// ============================================================================\n',
    '// LinBox Core Lifecycle, Layout & Window State Implementation\n',
    '// ============================================================================\n\n'
]
main_core += orig[643:1304]
main_core += ['\n']
main_core += orig[1395:1545]
main_core += ['\n']
main_core += orig[3071:3160]
main_core += ['\n']
main_core += orig[4356:4587]
main_core += ['\n']
main_core += orig[4813:4955]
with open('src/box/LinBox.cpp', 'w', encoding='utf-8') as f:
    f.writelines(main_core)

print('Split LinBox completed successfully!')
