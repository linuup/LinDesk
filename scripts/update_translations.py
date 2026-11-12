#!/usr/bin/env python3
"""
LinDesk Translation Update Tool (Python)
Extracts translatable strings from C++ sources into Qt Linguist TS files.
"""

import argparse
import os
import shutil
import subprocess
import sys
from typing import List, Optional

def get_repo_root() -> str:
    script_dir = os.path.dirname(os.path.abspath(__file__))
    return os.path.abspath(os.path.join(script_dir, ".."))

def find_tool(names: List[str], extra_dirs: Optional[List[str]] = None) -> Optional[str]:
    # 1. Search in PATH
    for name in names:
        p = shutil.which(name)
        if p and os.path.isfile(p):
            return os.path.abspath(p)

    # 2. Search in extra dirs & Qt standard paths
    search_dirs = []
    if extra_dirs:
        search_dirs.extend(extra_dirs)

    for env_var in ["QT_BIN", "QTDIR", "Qt5_DIR", "Qt6_DIR"]:
        val = os.environ.get(env_var)
        if val:
            search_dirs.append(val)
            search_dirs.append(os.path.join(val, "bin"))

    # Common default Qt paths on Windows
    default_qt_roots = [
        r"C:\Qt",
        r"D:\Qt",
        r"E:\Qt",
        r"F:\Qt",
    ]
    for root in default_qt_roots:
        if os.path.isdir(root):
            for entry in os.listdir(root):
                sub = os.path.join(root, entry)
                if os.path.isdir(sub):
                    for msvc_entry in os.listdir(sub):
                        msvc_dir = os.path.join(sub, msvc_entry, "bin")
                        if os.path.isdir(msvc_dir):
                            search_dirs.append(msvc_dir)

    for s_dir in search_dirs:
        for name in names:
            candidate = os.path.join(s_dir, name)
            if os.path.isfile(candidate):
                return os.path.abspath(candidate)

    return None

def update_translations(repo_root: str, open_linguist: bool = False, run_lrelease: bool = False):
    print("=" * 60)
    print(" LinDesk - Qt Translation Extraction & Update Tool")
    print("=" * 60)

    lupdate_exe = find_tool(["lupdate", "lupdate.exe"])
    if not lupdate_exe:
        raise RuntimeError(
            "Cannot find lupdate.exe.\n"
            "Please ensure Qt bin directory is in your PATH or set QT_BIN / QTDIR environment variable."
        )

    ts_files = [
        os.path.join(repo_root, "translations", "lindesk_en_US.ts"),
        os.path.join(repo_root, "translations", "lindesk_zh_CN.ts"),
    ]

    for ts in ts_files:
        os.makedirs(os.path.dirname(ts), exist_ok=True)

    # Explicitly scan only C++ source directories to avoid scanning web widget scripts in src/resources/LinWidgets
    source_candidates = [
        os.path.join(repo_root, "src", "app"),
        os.path.join(repo_root, "src", "desktop"),
        os.path.join(repo_root, "src", "models"),
        os.path.join(repo_root, "src", "services"),
        os.path.join(repo_root, "src", "ui"),
    ]
    scan_dirs = [d for d in source_candidates if os.path.isdir(d)]
    if not scan_dirs:
        scan_dirs = [os.path.join(repo_root, "src")]

    print(f"[INFO] Using lupdate : {lupdate_exe}")
    print("[INFO] Scanning source directories:")
    for d in scan_dirs:
        rel = os.path.relpath(d, repo_root)
        print(f"       - {rel}")

    cmd = [lupdate_exe] + scan_dirs + ["-extensions", "cpp,cxx,cc,h,hpp,ui", "-ts"] + ts_files
    print(f"\n[INFO] Running: {' '.join(cmd)}")
    sys.stdout.flush()

    res = subprocess.run(cmd, cwd=repo_root)
    if res.returncode != 0:
        raise RuntimeError(f"lupdate failed with exit code {res.returncode}")

    print("\n[OK] Translation source files updated successfully:")
    for ts in ts_files:
        rel = os.path.relpath(ts, repo_root)
        print(f"     - {rel}")

    if run_lrelease:
        lrelease_exe = find_tool(["lrelease", "lrelease.exe"])
        if lrelease_exe:
            print(f"\n[INFO] Running lrelease ({lrelease_exe})...")
            sys.stdout.flush()
            subprocess.run([lrelease_exe] + ts_files, cwd=repo_root)

    if open_linguist:
        linguist_exe = find_tool(["linguist", "linguist.exe"])
        if linguist_exe:
            print(f"\n[INFO] Opening Qt Linguist: {linguist_exe}")
            subprocess.Popen([linguist_exe] + ts_files, cwd=repo_root)
        else:
            print("\n[WARN] linguist.exe not found. Qt Linguist was not opened.", file=sys.stderr)

def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True)

    parser = argparse.ArgumentParser(description="Update LinDesk Qt translation files (.ts).")
    parser.add_argument("--open-linguist", action="store_true", help="Launch Qt Linguist after updating .ts files")
    parser.add_argument("--compile", action="store_true", help="Also compile .qm files using lrelease")
    args = parser.parse_args()

    repo_root = get_repo_root()
    try:
        update_translations(repo_root, open_linguist=args.open_linguist, run_lrelease=args.compile)
        print("\n[SUCCESS] Translation update completed!")
        return 0
    except Exception as e:
        print(f"\n[ERROR] {e}", file=sys.stderr)
        return 1

if __name__ == "__main__":
    sys.exit(main())
