import ctypes
from ctypes import wintypes
from PIL import Image
import os

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
kernel32 = ctypes.windll.kernel32

hDesk = user32.OpenDesktopW('Default', 0, False, 0x01FF)
user32.SetThreadDesktop(hDesk)

candidates = []

def enum_cb(hwnd, lparam):
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    buf = ctypes.create_unicode_buffer(512)
    user32.GetWindowTextW(hwnd, buf, 512)
    title = buf.value
    rect = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    w = rect.right - rect.left
    h = rect.bottom - rect.top
    is_vis = user32.IsWindowVisible(hwnd)
    if is_vis and w > 400 and h > 300:
        candidates.append((hwnd, pid.value, title, w, h))
    return True

WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user32.EnumDesktopWindows(hDesk, WNDENUMPROC(enum_cb), 0)

print(f"Found {len(candidates)} visible window candidates:")
for item in candidates:
    print(f"HWND: {hex(item[0])}, PID: {item[1]}, Title: '{item[2]}', Size: {item[3]}x{item[4]}")

target_hwnd = None
for item in candidates:
    if '壁纸' in item[2] or 'LinDesk' in item[2] or '工坊' in item[2] or item[3] >= 1000:
        target_hwnd = item[0]
        break

if not target_hwnd and candidates:
    target_hwnd = candidates[0][0]

if target_hwnd:
    rect = wintypes.RECT()
    user32.GetWindowRect(target_hwnd, ctypes.byref(rect))
    w = rect.right - rect.left
    h = rect.bottom - rect.top
    print(f"Capturing HWND {hex(target_hwnd)} ({w}x{h})...")

    hdcScreen = user32.GetWindowDC(target_hwnd)
    hdcMemDC = gdi32.CreateCompatibleDC(hdcScreen)
    hbmScreen = gdi32.CreateCompatibleBitmap(hdcScreen, w, h)
    gdi32.SelectObject(hdcMemDC, hbmScreen)

    user32.PrintWindow(target_hwnd, hdcMemDC, 2)

    class BITMAPINFOHEADER(ctypes.Structure):
        _fields_ = [
            ('biSize', wintypes.DWORD), ('biWidth', wintypes.LONG), ('biHeight', wintypes.LONG),
            ('biPlanes', wintypes.WORD), ('biBitCount', wintypes.WORD), ('biCompression', wintypes.DWORD),
            ('biSizeImage', wintypes.DWORD), ('biXPelsPerMeter', wintypes.LONG), ('biYPelsPerMeter', wintypes.LONG),
            ('biClrUsed', wintypes.DWORD), ('biClrImportant', wintypes.DWORD)
        ]
    
    bi = BITMAPINFOHEADER()
    bi.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bi.biWidth = w
    bi.biHeight = -h
    bi.biPlanes = 1
    bi.biBitCount = 32
    bi.biCompression = 0

    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(hdcMemDC, hbmScreen, 0, h, buf, ctypes.byref(bi), 0)

    im = Image.frombuffer('RGBA', (w, h), buf.raw, 'raw', 'BGRA', 0, 1)
    out_path = 'd:/Desktop/LinDesk/tools/shadcn_hub_capture.png'
    im.convert('RGB').save(out_path)
    print("SUCCESS: Screenshot saved to", out_path)
else:
    print("ERROR: No suitable window found to capture.")
