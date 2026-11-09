import ctypes
from ctypes import wintypes
import time

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

si = STARTUPINFO()
si.cb = ctypes.sizeof(STARTUPINFO)
si.lpDesktop = 'Default'

pi = PROCESS_INFORMATION()
exe = r'D:\Desktop\LinDesk\build\Release\LinDesk.exe'
cmd = f'"{exe}" --studio'
res = ctypes.windll.kernel32.CreateProcessW(
    exe, cmd, None, None, False, 0, None, r'D:\Desktop\LinDesk\build\Release', ctypes.byref(si), ctypes.byref(pi)
)
print('Launched PID:', pi.dwProcessId)

t0 = time.time()
while time.time() - t0 < 30:
    ret = ctypes.windll.kernel32.WaitForSingleObject(pi.hProcess, 1000)
    exit_code = wintypes.DWORD()
    ctypes.windll.kernel32.GetExitCodeProcess(pi.hProcess, ctypes.byref(exit_code))
    elapsed = time.time() - t0
    if exit_code.value != 0x103:
        print(f'Exited after {elapsed:.1f}s with code: {hex(exit_code.value)}')
        break
    else:
        print(f'Still alive at {elapsed:.1f}s')
