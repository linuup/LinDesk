import ctypes
from ctypes import wintypes
user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32

hDesk = user32.OpenDesktopW('Default', 0, False, 0x01FF)
oldDesk = user32.GetThreadDesktop(kernel32.GetCurrentThreadId())
user32.SetThreadDesktop(hDesk)

pid = 42580
found = []

WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

def check_hwnd(hwnd, lparam):
    dpid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(dpid))
    if dpid.value == pid:
        buf_title = ctypes.create_unicode_buffer(512)
        user32.GetWindowTextW(hwnd, buf_title, 512)
        buf_cls = ctypes.create_unicode_buffer(512)
        user32.GetClassNameW(hwnd, buf_cls, 512)
        vis = user32.IsWindowVisible(hwnd)
        parent = user32.GetParent(hwnd)
        rect = wintypes.RECT()
        user32.GetWindowRect(hwnd, ctypes.byref(rect))
        found.append((hex(hwnd), buf_cls.value, buf_title.value, vis, hex(parent), (rect.left, rect.top, rect.right, rect.bottom)))
    return True

# Recursively check children
def scan_all_top_level(hwnd, lparam):
    check_hwnd(hwnd, lparam)
    user32.EnumChildWindows(hwnd, WNDENUMPROC(check_hwnd), 0)
    return True

user32.EnumDesktopWindows(hDesk, WNDENUMPROC(scan_all_top_level), 0)

user32.SetThreadDesktop(oldDesk)
user32.CloseDesktop(hDesk)

print(f'Total windows for PID {pid} across all trees on Default desktop: {len(found)}')
for f in found:
    print(f)
