import ctypes
from ctypes import wintypes
import sys

class STARTUPINFO(ctypes.Structure):
    _fields_ = [
        ('cb', wintypes.DWORD), ('lpReserved', wintypes.LPWSTR), ('lpDesktop', wintypes.LPWSTR),
        ('lpTitle', wintypes.LPWSTR), ('dwX', wintypes.DWORD), ('dwY', wintypes.DWORD),
        ('dwXSize', wintypes.DWORD), ('dwYSize', wintypes.DWORD), ('dwXCountChars', wintypes.DWORD),
        ('dwYCountChars', wintypes.DWORD), ('dwFillAttribute', wintypes.DWORD), ('dwFlags', wintypes.DWORD),
        ('wShowWindow', wintypes.WORD), ('cbReserved2', wintypes.WORD), ('lpReserved2', ctypes.c_void_p),
        ('hStdInput', wintypes.HANDLE), ('hStdOutput', wintypes.HANDLE), ('hStdError', wintypes.HANDLE),
    ]

class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [
        ('hProcess', wintypes.HANDLE), ('hThread', wintypes.HANDLE),
        ('dwProcessId', wintypes.DWORD), ('dwThreadId', wintypes.DWORD),
    ]

def launch_on_default_desktop(cmd_args):
    si = STARTUPINFO()
    si.cb = ctypes.sizeof(STARTUPINFO)
    si.lpDesktop = 'WinSta0\\Default'
    
    pi = PROCESS_INFORMATION()
    app_path = r'D:\Desktop\LinDesk\build\Release\LinDesk.exe'
    full_cmd = f'"{app_path}" {cmd_args}'.strip()
    
    # 0x00000010 = CREATE_NEW_CONSOLE or 0
    res = ctypes.windll.kernel32.CreateProcessW(
        None, full_cmd, None, None, False, 0, None, r'D:\Desktop\LinDesk\build\Release', ctypes.byref(si), ctypes.byref(pi)
    )
    if not res:
        err = ctypes.windll.kernel32.GetLastError()
        print(f"Failed to launch on Default desktop, err={err}")
        return False
    print(f"Successfully launched PID {pi.dwProcessId} on interactive desktop WinSta0\\Default")
    return True

if __name__ == '__main__':
    args = ' '.join(sys.argv[1:]) if len(sys.argv) > 1 else '--studio'
    launch_on_default_desktop(args)
