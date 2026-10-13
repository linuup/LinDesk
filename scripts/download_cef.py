#!/usr/bin/env python3
"""
LinDesk CEF Downloader (Python)
Downloads and extracts prebuilt Chromium Embedded Framework (CEF) Windows x64 binary distribution.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request
import urllib.error

# Match the version currently integrated and tested in LinDesk (see 3rdparty/cef/include/cef_version.h)
DEFAULT_CEF_VERSION = "154.0.34+g14c5a08+chromium-154.0.8037.98"
DEFAULT_CDN_BASE = "https://cef-builds.spotifycdn.com"

def get_repo_root() -> str:
    script_dir = os.path.dirname(os.path.abspath(__file__))
    return os.path.abspath(os.path.join(script_dir, ".."))

def format_size(size_bytes: int) -> str:
    for unit in ['B', 'KB', 'MB', 'GB']:
        if size_bytes < 1024.0:
            return f"{size_bytes:.1f} {unit}"
        size_bytes /= 1024.0
    return f"{size_bytes:.1f} TB"

def download_file(url: str, dest_path: str):
    print(f"[1/3] Downloading CEF archive from:\n      {url}")
    print(f"      Saving to: {dest_path}")
    
    start_time = time.time()
    
    try:
        req = urllib.request.Request(
            url,
            headers={
                "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
                "Accept": "*/*",
            }
        )
        
        with urllib.request.urlopen(req) as response, open(dest_path, "wb") as out_file:
            total_size = int(response.headers.get("Content-Length", 0))
            downloaded = 0
            chunk_size = 1024 * 1024  # 1 MB chunks
            
            while True:
                chunk = response.read(chunk_size)
                if not chunk:
                    break
                out_file.write(chunk)
                downloaded += len(chunk)
                elapsed = time.time() - start_time
                speed = downloaded / elapsed if elapsed > 0 else 0
                
                if total_size > 0:
                    percent = min(100.0, downloaded * 100.0 / total_size)
                    sys.stdout.write(
                        f"\r      [{percent:5.1f}%] {format_size(downloaded)} / {format_size(total_size)} "
                        f"({format_size(speed)}/s)   "
                    )
                else:
                    sys.stdout.write(f"\r      Downloaded: {format_size(downloaded)} ({format_size(speed)}/s)   ")
                sys.stdout.flush()

        print("\n      Download completed successfully.")
    except Exception as e:
        print(f"\n[ERROR] Download failed: {e}", file=sys.stderr)
        print(f"\nYou can manually download the file from:\n  {url}", file=sys.stderr)
        print(f"And place it at:\n  {dest_path}", file=sys.stderr)
        raise

def extract_archive(archive_path: str, extract_dir: str):
    print(f"\n[2/3] Extracting archive to:\n      {extract_dir}...")
    os.makedirs(extract_dir, exist_ok=True)

    # Prefer system tar.exe for high performance decompression on Windows 10/11
    tar_exe = shutil.which("tar") or shutil.which("tar.exe")
    if tar_exe:
        print(f"      Using system tar: {tar_exe}")
        cmd = [tar_exe, "-xf", archive_path, "-C", extract_dir]
        res = subprocess.run(cmd)
        if res.returncode == 0:
            print("      Extraction completed successfully with system tar.")
            return

    # Fallback to python built-in tarfile
    print("      Using Python tarfile module (bzip2 decompression)...")
    import tarfile
    with tarfile.open(archive_path, "r:bz2") as tar:
        tar.extractall(path=extract_dir)
    print("      Extraction completed successfully.")

def deploy_to_target(extract_dir: str, package_name: str, target_dir: str):
    print(f"\n[3/3] Deploying to target directory:\n      {target_dir}")
    os.makedirs(os.path.dirname(target_dir), exist_ok=True)

    extracted_pkg_dir = os.path.join(extract_dir, package_name)
    source_dir = extracted_pkg_dir if os.path.isdir(extracted_pkg_dir) else extract_dir

    if os.path.exists(target_dir):
        print("      Removing existing destination directory...")
        shutil.rmtree(target_dir, ignore_errors=True)

    print(f"      Moving extracted files into {target_dir}...")
    shutil.move(source_dir, target_dir)

def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True)

    parser = argparse.ArgumentParser(description="Download and extract CEF Windows x64 binaries for LinDesk.")
    parser.add_argument("--version", default=DEFAULT_CEF_VERSION, help=f"CEF version tag (default: {DEFAULT_CEF_VERSION})")
    parser.add_argument("--target-dir", default="", help="Target directory (default: <repo_root>/3rdparty/cef)")
    parser.add_argument("--url", default="", help="Custom download URL override")
    parser.add_argument("--force", action="store_true", help="Force re-download even if already present")
    parser.add_argument("--clean-cache", action="store_true", help="Remove cached download archive")

    args = parser.parse_args()

    repo_root = get_repo_root()
    target_dir = os.path.abspath(args.target_dir) if args.target_dir else os.path.join(repo_root, "3rdparty", "cef")
    cef_version = args.version.strip()

    package_name = f"cef_binary_{cef_version}_windows64"
    archive_name = f"{package_name}.tar.bz2"
    download_url = args.url if args.url else f"{DEFAULT_CDN_BASE}/{archive_name}"

    print("=" * 60)
    print(" LinDesk - CEF Windows x64 Dependency Downloader (Python)")
    print("=" * 60)
    print(f"Target Directory : {target_dir}")
    print(f"CEF Version      : {cef_version}")

    header_file = os.path.join(target_dir, "include", "cef_app.h")
    if os.path.exists(header_file) and not args.force:
        print(f"\n[OK] CEF SDK is already installed at: {target_dir}")
        print("To re-download, pass --force parameter.")
        return 0

    temp_cache_dir = os.path.join(tempfile.gettempdir(), "lindesk_cef_download")
    os.makedirs(temp_cache_dir, exist_ok=True)
    archive_path = os.path.join(temp_cache_dir, archive_name)

    if args.clean_cache and os.path.exists(archive_path):
        print(f"Cleaning cache: {archive_path}")
        os.remove(archive_path)

    if not os.path.exists(archive_path) or args.force:
        download_file(download_url, archive_path)
    else:
        print(f"\nCached archive found at:\n  {archive_path}\nSkipping download.")

    extract_temp = os.path.join(temp_cache_dir, "extracted")
    if os.path.exists(extract_temp):
        shutil.rmtree(extract_temp, ignore_errors=True)
    os.makedirs(extract_temp, exist_ok=True)

    try:
        extract_archive(archive_path, extract_temp)
        deploy_to_target(extract_temp, package_name, target_dir)
    finally:
        if os.path.exists(extract_temp):
            shutil.rmtree(extract_temp, ignore_errors=True)

    if os.path.exists(header_file):
        print(f"\n[SUCCESS] CEF SDK successfully installed to: {target_dir}")
        print("Ready to configure CMake and build LinDesk with CEF OSR support!")
        return 0
    else:
        print(f"\n[WARNING] Extraction completed but {header_file} was not found.", file=sys.stderr)
        return 1

if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as e:
        print(f"\n[FATAL ERROR] {e}", file=sys.stderr)
        sys.exit(1)
