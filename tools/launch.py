import ctypes
from ctypes import wintypes
import os
import sys
import subprocess
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

def launch():
    # 1. 杀掉遗留进程
    subprocess.run(["taskkill", "/F", "/IM", "LinDesk.exe", "/T"], capture_output=True)
    subprocess.run(["taskkill", "/F", "/IM", "LinDeskSubprocess.exe", "/T"], capture_output=True)
    time.sleep(0.5)

    si = STARTUPINFO()
    si.cb = ctypes.sizeof(STARTUPINFO)
    si.lpDesktop = 'WinSta0\\Default'

    pi = PROCESS_INFORMATION()
    exe = r'D:\Desktop\LinDesk\build\Release\LinDesk.exe'
    cmd = f'"{exe}" --studio'

    # DETACHED_PROCESS = 0x00000008, CREATE_NEW_PROCESS_GROUP = 0x00000200
    flags = 0

    res = ctypes.windll.kernel32.CreateProcessW(
        exe, cmd, None, None, False, flags, None, r'D:\Desktop\LinDesk\build\Release', ctypes.byref(si), ctypes.byref(pi)
    )
    if not res:
        err = ctypes.windll.kernel32.GetLastError()
        print(f"Launch failed! Error code: {err}")
        return False

    print(f"SUCCESS: LinDesk.exe launched onto user's Default desktop! PID: {pi.dwProcessId}")
    try:
        while True:
            time.sleep(1)
            exit_code = wintypes.DWORD()
            ctypes.windll.kernel32.GetExitCodeProcess(pi.hProcess, ctypes.byref(exit_code))
            if exit_code.value != 0x103:
                print(f"LinDesk exited with code: {hex(exit_code.value)}")
                break
    except KeyboardInterrupt:
        pass
    return True

if __name__ == '__main__':
    launch()
