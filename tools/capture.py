import ctypes
from ctypes import wintypes
from PIL import Image
import os

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
kernel32 = ctypes.windll.kernel32

hDesk = user32.OpenDesktopW('Default', 0, False, 0x01FF)
oldDesk = user32.GetThreadDesktop(kernel32.GetCurrentThreadId())
user32.SetThreadDesktop(hDesk)

hwnd = 0x210d18
if not user32.IsWindow(hwnd):
    # Enum windows on desktop
    target = []
    def cb(h, l):
        buf = ctypes.create_unicode_buffer(512)
        user32.GetWindowTextW(h, buf, 512)
        if '动态壁纸' in buf.value or 'LinDesk' in buf.value:
            target.append(h)
        return True
    WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.EnumDesktopWindows(hDesk, WNDENUMPROC(cb), 0)
    if target:
        hwnd = target[0]

print('Target HWND:', hex(hwnd) if hwnd else None)
if hwnd:
    rect = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    w = rect.right - rect.left
    h = rect.bottom - rect.top
    print(f'Window rect: ({rect.left}, {rect.top}, {w}x{h})')

    hdcScreen = user32.GetWindowDC(hwnd)
    hdcMemDC = gdi32.CreateCompatibleDC(hdcScreen)
    hbmScreen = gdi32.CreateCompatibleBitmap(hdcScreen, w, h)
    gdi32.SelectObject(hdcMemDC, hbmScreen)

    # PrintWindow PW_RENDERFULLCONTENT = 2
    res = user32.PrintWindow(hwnd, hdcMemDC, 2)
    print('PrintWindow result:', res)

    # Convert to PIL Image
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
    bi.biHeight = -h  # top-down
    bi.biPlanes = 1
    bi.biBitCount = 32
    bi.biCompression = 0

    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(hdcMemDC, hbmScreen, 0, h, buf, ctypes.byref(bi), 0)

    im = Image.frombuffer('RGBA', (w, h), buf.raw, 'raw', 'BGRA', 0, 1)
    out_path = 'd:/Desktop/LinDesk/tools/studio_capture.png'
    im.convert('RGB').save(out_path)
    print('Saved screenshot to:', out_path)

    gdi32.DeleteObject(hbmScreen)
    gdi32.DeleteDC(hdcMemDC)
    user32.ReleaseDC(hwnd, hdcScreen)

user32.SetThreadDesktop(oldDesk)
user32.CloseDesktop(hDesk)
