#!/usr/bin/env python3
"""
LinDesk Release & Packaging Automation Script (Python)
Integrates translation updates, version management, dual-architecture (x64 + x86) Release build,
Qt runtime deployment with windeployqt, Inno Setup packaging, update.json metadata generation,
and GitHub Releases publication.
"""

import argparse
import datetime
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request
from typing import Dict, List, Optional, Tuple

# Import clean translation update logic from update_translations.py
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from update_translations import update_translations, find_tool

REPO_OWNER = "linuup"
REPO_NAME = "LinDesk_Update"
DEFAULT_CHANNEL = "stable"
DEFAULT_MIN_VERSION = "1.0.0"

API_TOKEN = "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"

ARCH_CONFIG: Dict[str, Dict] = {
    "x64": {
        "generator_arch": "x64",
        "triplet": "x64-windows",
        "build_dir": os.path.join("build", "build_64"),
        "windeployqt": r"F:\Qt\5.15.2\msvc2019_64\bin\windeployqt.exe",
        "qt_bin": r"F:\Qt\5.15.2\msvc2019_64\bin",
        "expected_pe_machine": 0x8664,
        "installer_suffix": "_x64",
        "update_json_name": "update.json",
    },
    "x86": {
        "generator_arch": "Win32",
        "triplet": "x86-windows",
        "build_dir": os.path.join("build", "build_32"),
        "windeployqt": r"F:\Qt\5.15.2\msvc2019\bin\windeployqt.exe",
        "qt_bin": r"F:\Qt\5.15.2\msvc2019\bin",
        "expected_pe_machine": 0x14C,
        "installer_suffix": "_x86",
        "update_json_name": "update_x86.json",
    }
}

def get_repo_root() -> str:
    script_dir = os.path.dirname(os.path.abspath(__file__))
    return os.path.abspath(os.path.join(script_dir, ".."))

def read_version(cmake_path: str) -> Tuple[int, int, int, str]:
    if not os.path.exists(cmake_path):
        raise FileNotFoundError(f"CMakeLists.txt not found: {cmake_path}")

    with open(cmake_path, "r", encoding="utf-8") as f:
        content = f.read()

    major_m = re.search(r"set\(\s*LINDESK_VERSION_MAJOR\s+(\d+)\s*\)", content)
    minor_m = re.search(r"set\(\s*LINDESK_VERSION_MINOR\s+(\d+)\s*\)", content)
    patch_m = re.search(r"set\(\s*LINDESK_VERSION_PATCH\s+(\d+)\s*\)", content)

    if not (major_m and minor_m and patch_m):
        raise ValueError("Could not parse LINDESK_VERSION_MAJOR / MINOR / PATCH from CMakeLists.txt")

    major = int(major_m.group(1))
    minor = int(minor_m.group(1))
    patch = int(patch_m.group(1))
    version_str = f"{major}.{minor}.{patch}"
    return major, minor, patch, version_str

def write_version(cmake_path: str, major: int, minor: int, patch: int):
    with open(cmake_path, "r", encoding="utf-8") as f:
        content = f.read()

    content = re.sub(r"set\(\s*LINDESK_VERSION_MAJOR\s+\d+\s*\)", f"set(LINDESK_VERSION_MAJOR {major})", content)
    content = re.sub(r"set\(\s*LINDESK_VERSION_MINOR\s+\d+\s*\)", f"set(LINDESK_VERSION_MINOR {minor})", content)
    content = re.sub(r"set\(\s*LINDESK_VERSION_PATCH\s+\d+\s*\)", f"set(LINDESK_VERSION_PATCH {patch})", content)

    with open(cmake_path, "w", encoding="utf-8") as f:
        f.write(content)

def bump_version_str(curr_major: int, curr_minor: int, curr_patch: int, bump_type: str) -> Tuple[int, int, int, str]:
    bump_type = bump_type.lower()
    if bump_type == "major":
        return curr_major + 1, 0, 0, f"{curr_major + 1}.0.0"
    elif bump_type == "minor":
        return curr_major, curr_minor + 1, 0, f"{curr_major}.{curr_minor + 1}.0"
    elif bump_type == "patch":
        return curr_major, curr_minor, curr_patch + 1, f"{curr_major}.{curr_minor}.{curr_patch + 1}"
    else:
        raise ValueError(f"Invalid bump type: '{bump_type}'. Choose major, minor, or patch.")

def check_pe_machine(file_path: str) -> Optional[int]:
    """Reads the PE machine architecture from an executable or DLL."""
    try:
        with open(file_path, "rb") as f:
            f.seek(0x3C)
            pe_offset = int.from_bytes(f.read(4), byteorder="little")
            f.seek(pe_offset + 4)
            machine = int.from_bytes(f.read(2), byteorder="little")
            return machine
    except Exception:
        return None

def verify_release_architecture(release_dir: str, expected_arch: str):
    """Verifies that all binaries in release_dir match the expected PE Machine type."""
    expected_machine = ARCH_CONFIG[expected_arch]["expected_pe_machine"]
    print(f"    Verifying PE Machine architecture for {release_dir} (expected {expected_arch}: {hex(expected_machine)})...")
    mismatches = []
    checked_count = 0
    for root, _, files in os.walk(release_dir):
        for f in files:
            # Skip bootstrapper vc_redist in release dir if any
            if f.lower().startswith("vc_redist"):
                continue
            if f.lower().endswith((".exe", ".dll")):
                full_path = os.path.join(root, f)
                machine = check_pe_machine(full_path)
                if machine is not None:
                    checked_count += 1
                    if machine != expected_machine:
                        mismatches.append((os.path.relpath(full_path, release_dir), hex(machine), hex(expected_machine)))
    if mismatches:
        err_msg = "\n".join([f"      [MISMATCH] {rel}: got {got}, expected {exp}" for rel, got, exp in mismatches])
        raise RuntimeError(f"Architecture verification failed for {expected_arch} in {release_dir}!\n{err_msg}")
    print(f"    [OK] All {checked_count} binaries verified matching architecture {expected_arch}.")

def deploy_qt(release_dir: str, arch: str):
    """
    Deploys Qt runtime DLLs and plugins using windeployqt.
    Isolates PATH to prevent cross-architecture contamination between 32-bit and 64-bit Qt.
    """
    print(f"\n>>> Deploying Qt runtime for {arch} into {release_dir}...")
    # Step 1: Clean stale Qt*.dll and plugin directories
    for f in glob.glob(os.path.join(release_dir, "Qt*.dll")):
        try:
            os.remove(f)
        except OSError:
            pass

    for f in ["D3Dcompiler_47.dll", "libEGL.dll", "libGLESv2.dll", "opengl32sw.dll", "vc_redist.x64.exe", "vc_redist.x86.exe"]:
        fp = os.path.join(release_dir, f)
        if os.path.exists(fp):
            try:
                os.remove(fp)
            except OSError:
                pass

    for d in ["plugins", "platforms", "imageformats", "styles", "bearer", "iconengines"]:
        dp = os.path.join(release_dir, d)
        if os.path.isdir(dp):
            shutil.rmtree(dp, ignore_errors=True)

    # Step 2: Set isolated PATH for windeployqt
    qt_bin = ARCH_CONFIG[arch]["qt_bin"]
    windeployqt_path = ARCH_CONFIG[arch]["windeployqt"]
    if not os.path.isfile(windeployqt_path):
        raise FileNotFoundError(f"windeployqt not found: {windeployqt_path}")

    env = os.environ.copy()
    raw_paths = env.get("PATH", "").split(";")
    if arch == "x64":
        # Keep msvc2019_64, remove plain msvc2019
        clean_paths = [p for p in raw_paths if "msvc2019" not in p.lower() or "msvc2019_64" in p.lower()]
    else:
        # Keep plain msvc2019, remove msvc2019_64
        clean_paths = [p for p in raw_paths if "msvc2019_64" not in p.lower()]
    clean_paths.insert(0, qt_bin)
    env["PATH"] = ";".join(clean_paths)

    main_exe = os.path.join(release_dir, "LinDesk.exe")
    cmd = [windeployqt_path, "--release", "--no-translations", main_exe]
    print(f"    Running windeployqt ({arch}): {windeployqt_path}")
    res = subprocess.run(cmd, env=env, cwd=release_dir)
    if res.returncode != 0:
        raise RuntimeError(f"windeployqt failed with code {res.returncode}")

    # Step 3: Verify platforms/qwindows.dll exists
    qwindows = os.path.join(release_dir, "platforms", "qwindows.dll")
    if not os.path.isfile(qwindows):
        raise FileNotFoundError(f"Qt platform plugin missing after deployment: {qwindows}")

    # Step 4: Verify architecture of all binaries
    verify_release_architecture(release_dir, arch)
    print(f"    [OK] Qt deployment and architecture verification succeeded for {arch}.")

def build_release(repo_root: str, arch: str, custom_build_dir: str = "") -> str:
    print(f"\n>>> Building Release configuration for [{arch}] with CMake...")
    build_dir = custom_build_dir if custom_build_dir else os.path.join(repo_root, ARCH_CONFIG[arch]["build_dir"])
    abs_build_dir = os.path.abspath(build_dir)
    os.makedirs(abs_build_dir, exist_ok=True)
    sys.stdout.flush()

    print(f"    Configuring CMake ({arch}, BUILD_TESTING=OFF) in {abs_build_dir}...")
    sys.stdout.flush()
    cfg_cmd = [
        "cmake", "-S", repo_root, "-B", abs_build_dir,
        "-G", "Visual Studio 17 2022",
        "-A", ARCH_CONFIG[arch]["generator_arch"],
        "-DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake",
        f"-DVCPKG_TARGET_TRIPLET={ARCH_CONFIG[arch]['triplet']}",
        "-DBUILD_TESTING=OFF"
    ]
    cfg_res = subprocess.run(cfg_cmd, cwd=repo_root)
    if cfg_res.returncode != 0:
        raise RuntimeError(f"CMake configure failed for {arch} with exit code {cfg_res.returncode}")

    build_cmd = [
        "cmake", "--build", abs_build_dir,
        "--config", "Release",
        "--target", "LinDesk", "LinDeskUpdate", "LinDeskSubprocess", "LinWidgetRuntime",
        "-j", "8"
    ]
    print(f"    Building targets LinDesk, LinDeskUpdate, LinDeskSubprocess, LinWidgetRuntime for {arch}...")
    sys.stdout.flush()
    build_res = subprocess.run(build_cmd, cwd=repo_root)
    if build_res.returncode != 0:
        raise RuntimeError(f"Release build failed for {arch} with exit code {build_res.returncode}")

    release_dir = os.path.join(abs_build_dir, "Release")
    required = ["LinDesk.exe", "LinDeskUpdate.exe", "LinDeskSubprocess.exe", "resources", "translations"]
    if arch == "x64":
        required.append("LinWidgetRuntime.dll")
    missing = [item for item in required if not os.path.exists(os.path.join(release_dir, item))]
    if missing:
        raise RuntimeError(f"Missing required build outputs in {release_dir}: {missing}")

    # Deploy Qt runtime and verify PE machine types
    deploy_qt(release_dir, arch)

    print(f"    [OK] Release build ({arch}) completed, deployed, and verified.")
    sys.stdout.flush()
    return release_dir

def package_installer(repo_root: str, version: str, arch: str, release_dir: str, installer_dir: str, iss_path: str) -> str:
    print(f"\n>>> Compiling installer with Inno Setup (version: {version}, arch: {arch})...")
    sys.stdout.flush()

    iscc_exe = find_tool(["ISCC", "ISCC.exe"], extra_dirs=[r"D:\Inno Setup 6"])
    if not iscc_exe:
        raise RuntimeError(
            "ISCC.exe not found! Please install Inno Setup 6 or add it to PATH."
        )

    print(f"    Using Inno Setup compiler: {iscc_exe}")
    print(f"    Script path: {iss_path}")
    print(f"    Target architecture: {arch}")
    sys.stdout.flush()

    cmd = [
        iscc_exe,
        f"/DAppVersion={version}",
        f"/DMyArch={arch}",
        f"/DBuildDir={os.path.abspath(release_dir)}",
        f"/DPackageOutputDir={os.path.abspath(installer_dir)}",
        iss_path
    ]

    res = subprocess.run(cmd, cwd=repo_root)
    if res.returncode != 0:
        raise RuntimeError(f"Inno Setup compilation failed for {arch} with exit code {res.returncode}")

    installer_name = f"LinDesk_Setup_{version}_{arch}.exe"
    installer_path = os.path.join(installer_dir, installer_name)
    if not os.path.isfile(installer_path):
        raise FileNotFoundError(f"Expected installer not generated: {installer_path}")

    size_mb = os.path.getsize(installer_path) / (1024 * 1024)
    print(f"    [OK] Installer ({arch}) compiled successfully: {installer_path} ({size_mb:.2f} MB)")
    sys.stdout.flush()
    return installer_path

def compute_sha256(file_path: str) -> str:
    hasher = hashlib.sha256()
    with open(file_path, "rb") as f:
        while chunk := f.read(65536):
            hasher.update(chunk)
    return hasher.hexdigest().lower()

def generate_update_json(installer_path: str, version: str, sha256_hash: str, notes: str, output_path: str):
    print(f"\n>>> Generating update metadata: {os.path.basename(output_path)}...")
    sys.stdout.flush()

    installer_name = os.path.basename(installer_path)
    tag = f"v{version}"
    download_url = f"https://github.com/{REPO_OWNER}/{REPO_NAME}/releases/download/{tag}/{installer_name}"
    published_at = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    metadata = {
        "version": version,
        "channel": DEFAULT_CHANNEL,
        "url": download_url,
        "sha256": sha256_hash,
        "notes": notes,
        "publishedAt": published_at,
        "minSupportedVersion": DEFAULT_MIN_VERSION
    }

    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(metadata, f, indent=2, ensure_ascii=False)

    print(f"    [OK] Saved to: {output_path}")
    print(f"         SHA-256 : {sha256_hash}")
    print(f"         URL     : {download_url}")
    sys.stdout.flush()
    return metadata

def api_github_request(method: str, url: str, token: str,
                       data: Optional[bytes] = None,
                       content_type: str = "application/json"):
    headers = {
        "Authorization": f"Bearer {token}",
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
    }
    if data is not None:
        headers["Content-Type"] = content_type
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req) as resp:
            raw = resp.read()
            return json.loads(raw) if raw else None
    except urllib.error.HTTPError as e:
        if e.code == 404:
            return None
        body = e.read().decode("utf-8", "replace")[:500]
        raise RuntimeError(f"GitHub API {method} {url} failed ({e.code}): {body}")

def api_publish_release(repo_owner: str, repo_name: str, tag: str, version: str,
                        installer_items: List[Tuple[str, str]], update_json_paths: List[str], notes: str):
    """
    Publishes a release to GitHub via REST API.
    installer_items: list of (installer_path, sha256_hash)
    update_json_paths: list of update json file paths
    """
    print(f"\n>>> Publishing Release to GitHub via REST API ({repo_owner}/{repo_name} {tag})...")
    sys.stdout.flush()

    base = f"https://api.github.com/repos/{repo_owner}/{repo_name}"
    uploads_base = f"https://uploads.github.com/repos/{repo_owner}/{repo_name}"
    
    asset_paths = [path for path, _ in installer_items] + update_json_paths
    asset_names = [os.path.basename(p) for p in asset_paths]

    final_notes = notes.strip()
    checksum_lines = []
    for path, sha in installer_items:
        name = os.path.basename(path)
        checksum_lines.append(f"- **{name}**: `{sha}`")
    
    if checksum_lines and "### Checksum" not in final_notes:
        final_notes += "\n\n### Checksum\n" + "\n".join(checksum_lines) + "\n"

    # 1) Check if tag already exists
    release = api_github_request("GET", f"{base}/releases/tags/{tag}", API_TOKEN)
    if release:
        release_id = release["id"]
        print(f"    Release {tag} already exists (id={release_id}). Updating assets...")
        sys.stdout.flush()
        # Delete stale matching assets before re-uploading
        assets = api_github_request("GET", f"{base}/releases/{release_id}/assets", API_TOKEN) or []
        for asset in assets:
            if asset.get("name") in asset_names:
                api_github_request("DELETE", f"{base}/releases/assets/{asset['id']}", API_TOKEN)
                print(f"    Deleted old asset {asset['name']}")
                sys.stdout.flush()
    else:
        # 2) Create new release
        payload = json.dumps({
            "tag_name": tag,
            "name": f"LinDesk {version}",
            "body": final_notes,
            "draft": False,
            "prerelease": False,
        }).encode("utf-8")
        release = api_github_request("POST", f"{base}/releases", API_TOKEN, data=payload)
        if not release:
            raise RuntimeError("GitHub API returned empty body when creating release")
        release_id = release["id"]
        print(f"    Created release {tag} (id={release_id})")
        sys.stdout.flush()

    # 3) Upload all assets
    for file_path in asset_paths:
        name = os.path.basename(file_path)
        with open(file_path, "rb") as f:
            body = f.read()
        qname = urllib.parse.quote(name)
        asset = api_github_request(
            "POST", f"{uploads_base}/releases/{release_id}/assets?name={qname}",
            API_TOKEN, data=body, content_type="application/octet-stream")
        if not asset:
            raise RuntimeError(f"Failed to upload asset {name}")
        print(f"    [OK] Uploaded {name} ({len(body) / (1024 * 1024):.2f} MB)")
        sys.stdout.flush()

    print(f"    [OK] Successfully published to https://github.com/{repo_owner}/{repo_name}/releases/tag/{tag}")
    sys.stdout.flush()
    return True

def print_banner(current_version: str, repo_root: str):
    print("=" * 68)
    print("      LinDesk 双架构发版与安装包打包助手 (Dual-Arch Release Assistant)")
    print("=" * 68)
    print("【当前项目环境】")
    print(f"  * 项目根目录   : {repo_root}")
    print(f"  * 当前版本号   : {current_version} (读取自 CMakeLists.txt)")
    print(f"  * GitHub 仓库  : {REPO_OWNER}/{REPO_NAME}")
    print(f"  * 支持架构     : x64, x86 (自动隔离 Qt 运行时环境)")
    print("-" * 68)

def resolve_existing_packages(repo_root: str, arch_list: List[str], expected_version: str = "") -> Dict[str, str]:
    """Finds existing installer packages for requested architectures."""
    installer_dir = os.path.join(repo_root, "installer")
    results = {}
    if not os.path.isdir(installer_dir):
        raise FileNotFoundError(f"installer directory not found: {installer_dir}")

    for arch in arch_list:
        pattern = f"LinDesk_Setup_{expected_version}_{arch}.exe" if expected_version else f"LinDesk_Setup_*_{arch}.exe"
        matches = glob.glob(os.path.join(installer_dir, pattern))
        if not matches:
            # Fallback check for legacy non-suffixed package if arch is x64
            if arch == "x64":
                legacy_pattern = f"LinDesk_Setup_{expected_version}.exe" if expected_version else "LinDesk_Setup_*.exe"
                matches = [m for m in glob.glob(os.path.join(installer_dir, legacy_pattern)) if "_x86" not in m and "_x64" not in m]
        if not matches:
            raise FileNotFoundError(f"未找到 {arch} 架构安装包 (版本: {expected_version or 'any'}): {installer_dir}")
        matches.sort(key=os.path.getmtime, reverse=True)
        results[arch] = matches[0]

    return results

def interactive_wizard(repo_root: str, current_version: str, curr_major: int, curr_minor: int, curr_patch: int):
    print("\n【请选择执行模式】")
    print("  [1] 正式发布到 GitHub (构建 + 打包 + 更新 update.json + GitHub 发布)")
    print("  [2] 仅本地构建与打包 (构建 + Inno Setup 打包 + update.json，不上传)")
    print("  [3] 仅打包现有产物   (跳过编译，直接使用已有的 Release 产物制作安装包)")
    print("  [4] 仅更新国际化翻译 (提取并编译 C++/QML 翻译 .ts -> .qm)")
    print("  [5] 仅推送已有安装包 (跳过编译与打包，直接上传 installer/ 下的现成包)")
    print("  [0] 退出")
    print("-" * 68)

    while True:
        choice = input("请输入选项编号 [默认: 1]: ").strip()
        if choice == "":
            choice = "1"
        if choice in ["0", "1", "2", "3", "4", "5"]:
            break
        print("[提示] 输入无效，请输入 0、1、2、3、4 或 5。")

    if choice == "0":
        print("\n已退出发版助手。")
        sys.exit(0)

    if choice == "4":
        open_ling = input("\n是否在更新翻译后打开 Qt Linguist 进行校对? [y/N]: ").strip().lower() in ["y", "yes"]
        update_translations(repo_root, open_linguist=open_ling)
        print("\n[SUCCESS] 翻译文件处理完毕。")
        sys.exit(0)

    # 架构选择
    print("\n【目标构建架构】")
    print("  [1] 双架构发布 (x64 + x86) [推荐，同时支持 32/64 位系统]")
    print("  [2] 仅 64 位 (x64)")
    print("  [3] 仅 32 位 (x86)")
    while True:
        arch_choice = input("请选择目标架构 [默认: 1]: ").strip()
        if arch_choice in ["", "1"]:
            target_arch = "both"
            break
        elif arch_choice == "2":
            target_arch = "x64"
            break
        elif arch_choice == "3":
            target_arch = "x86"
            break
        print("[提示] 请输入 1、2 或 3。")

    skip_package = False
    notes = ""
    if choice == "5":
        target_version = current_version
        skip_trans = True
        print("\n【Release Notes / 更新日志】")
        user_notes = input("请输入本次更新说明 (直接回车将使用默认说明): ").strip()
        if user_notes:
            notes = user_notes
        else:
            rn_file = os.path.join(repo_root, "docs", "release-notes.md")
            if os.path.isfile(rn_file):
                with open(rn_file, "r", encoding="utf-8") as f:
                    notes = f.read().strip()
            if not notes:
                notes = f"LinDesk {target_version} update."
    else:
        # 模式 1, 2, 3 的版本号配置
        next_patch_ver = f"{curr_major}.{curr_minor}.{curr_patch + 1}"
        next_minor_ver = f"{curr_major}.{curr_minor + 1}.0"

        print("\n【版本号设置】")
        print(f"  [1] 自动增加补丁号 Patch (+1) : {current_version} -> {next_patch_ver} (推荐，日常修复/小优化)")
        print(f"  [2] 保持当前版本不变           : {current_version}")
        print(f"  [3] 自动增加次版本号 Minor (+1) : {current_version} -> {next_minor_ver} (新增功能)")
        print(f"  [4] 手动输入自定义版本号")

        while True:
            ver_choice = input("请选择版本方案 [默认: 1]: ").strip()
            if ver_choice == "":
                ver_choice = "1"
            if ver_choice == "1":
                target_version = next_patch_ver
                break
            elif ver_choice == "2":
                target_version = current_version
                break
            elif ver_choice == "3":
                target_version = next_minor_ver
                break
            elif ver_choice == "4":
                custom_ver = input("请输入自定义版本号 (格式 X.Y.Z，例如 1.1.2): ").strip()
                if re.match(r"^\d+\.\d+\.\d+$", custom_ver):
                    target_version = custom_ver
                    break
                print("[错误] 版本号格式不合规，必须为三段数字，如 1.1.2")
            else:
                print("[提示] 请输入 1、2、3 或 4。")

        # Release Notes 配置
        if choice == "1":
            print("\n【Release Notes / 更新日志】")
            user_notes = input("请输入本次更新说明 (直接回车将使用默认说明): ").strip()
            if user_notes:
                notes = user_notes
            else:
                rn_file = os.path.join(repo_root, "docs", "release-notes.md")
                if os.path.isfile(rn_file):
                    with open(rn_file, "r", encoding="utf-8") as f:
                        notes = f.read().strip()
                if not notes:
                    notes = f"LinDesk {target_version} update."
        else:
            notes = f"LinDesk {target_version} local package."

        # 是否跳过翻译更新
        skip_trans = False
        if choice in ["1", "2"]:
            skip_trans_in = input("\n是否跳过翻译提取与编译? [y/N, 默认 N]: ").strip().lower()
            skip_trans = skip_trans_in in ["y", "yes"]

    no_build = (choice == "3") or (choice == "5")
    skip_upload = (choice in ["2", "3"])
    skip_package = (choice == "5")

    mode_name_map = {
        "1": "正式发布到 GitHub (构建 + 打包 + 上传)",
        "2": "仅本地构建与打包 (不上传 GitHub)",
        "3": "仅打包现有产物 (跳过编译，直接生成安装包)",
        "5": "仅推送已有安装包 (跳过编译与打包，直接上传)"
    }

    print("\n" + "=" * 68)
    print("                      【执行配置确认】")
    print("=" * 68)
    print(f"  * 执行模式     : {mode_name_map[choice]}")
    print(f"  * 目标架构     : {target_arch}")
    print(f"  * 目标版本号   : {target_version} (当前: {current_version})")
    print(f"  * CMake 编译   : {'跳过' if no_build else '执行 (Release, -DBUILD_TESTING=OFF)'}")
    print(f"  * 安装包打包   : {'跳过 (使用已有安装包)' if skip_package else '执行 (Inno Setup)'}")
    print(f"  * 更新翻译     : {'跳过' if skip_trans or no_build else '执行 (lupdate & lrelease)'}")
    print(f"  * GitHub 发布  : {'否 (离线/仅本地)' if skip_upload else f'是 (目标仓库: {REPO_OWNER}/{REPO_NAME})'}")
    if not skip_upload:
        print(f"  * 更新日志     : {notes}")
    print("=" * 68)

    confirm = input("确认开始执行上述流程? [Y/n]: ").strip().lower()
    if confirm not in ["", "y", "yes"]:
        print("\n[CANCELLED] 用户取消操作。")
        sys.exit(0)

    return target_version, target_arch, notes, skip_upload, no_build, skip_trans, skip_package

def main():
    if hasattr(sys.stdout, "reconfigure"):
        try:
            sys.stdout.reconfigure(encoding="utf-8", line_buffering=True)
        except Exception:
            pass
    if hasattr(sys.stderr, "reconfigure"):
        try:
            sys.stderr.reconfigure(encoding="utf-8", line_buffering=True)
        except Exception:
            pass

    parser = argparse.ArgumentParser(description="LinDesk unified release & dual-architecture packaging assistant.")
    parser.add_argument("--version", default="", help="Release version (e.g. 1.1.2). Defaults to current in CMakeLists.txt")
    parser.add_argument("--arch", choices=["both", "x64", "x86"], default="both", help="Target architecture (both, x64, or x86). Default: both")
    parser.add_argument("--bump", choices=["major", "minor", "patch"], default="", help="Bump version automatically")
    parser.add_argument("--skip-translations", action="store_true", help="Skip running lupdate on translations")
    parser.add_argument("--update-translations-only", action="store_true", help="Only run lupdate/lrelease and exit")
    parser.add_argument("--open-linguist", action="store_true", help="Open Qt Linguist after updating translations")
    parser.add_argument("--no-build", action="store_true", help="Skip Release building step")
    parser.add_argument("--notes", default="", help="Release notes text")
    parser.add_argument("--notes-file", default="", help="File containing release notes")
    parser.add_argument("--skip-upload", action="store_true", help="Build and generate metadata without uploading to GitHub")
    parser.add_argument("--skip-package", action="store_true", help="Skip build & packaging, push existing packages from installer/ folder")
    parser.add_argument("--build-dir", default="", help="Custom CMake build directory override (used when single arch)")
    parser.add_argument("--interactive", "-i", action="store_true", help="Force interactive wizard")

    args = parser.parse_args()
    repo_root = get_repo_root()
    cmake_path = os.path.join(repo_root, "CMakeLists.txt")

    curr_major, curr_minor, curr_patch, current_version = read_version(cmake_path)

    if args.update_translations_only:
        update_translations(repo_root, open_linguist=args.open_linguist)
        print("[SUCCESS] Translations updated.")
        return 0

    print_banner(current_version, repo_root)

    has_cli_ops = bool(args.version or args.bump or args.skip_upload or args.no_build or args.notes or args.notes_file)
    use_interactive = args.interactive or (not has_cli_ops and sys.stdin.isatty())

    if use_interactive:
        target_version, target_arch, notes, skip_upload, no_build, skip_trans, skip_package = interactive_wizard(
            repo_root, current_version, curr_major, curr_minor, curr_patch
        )
    else:
        target_version = current_version
        target_arch = args.arch
        if args.bump:
            _, _, _, target_version = bump_version_str(curr_major, curr_minor, curr_patch, args.bump)
        elif args.version:
            target_version = args.version.strip()
            if not re.match(r"^\d+\.\d+\.\d+$", target_version):
                raise ValueError(f"Invalid version format '{target_version}'. Expected X.Y.Z")

        notes = args.notes.strip()
        if not notes and args.notes_file and os.path.isfile(args.notes_file):
            with open(args.notes_file, "r", encoding="utf-8") as f:
                notes = f.read().strip()
        if not notes:
            rn_file = os.path.join(repo_root, "docs", "release-notes.md")
            if os.path.isfile(rn_file):
                with open(rn_file, "r", encoding="utf-8") as f:
                    notes = f.read().strip()
        if not notes:
            notes = f"LinDesk {target_version} update."

        skip_upload = args.skip_upload
        no_build = args.no_build
        skip_trans = args.skip_translations
        skip_package = args.skip_package

    arch_list = ["x64", "x86"] if target_arch == "both" else [target_arch]

    print(f"\n>>> 正在准备启动流水线...")
    print(f"    当前版本 : {current_version}")
    print(f"    目标版本 : {target_version}")
    print(f"    目标架构 : {', '.join(arch_list)}")
    print(f"    更新日志 : {notes}")
    sys.stdout.flush()

    # Step 1: Synchronize version to CMakeLists.txt if changed
    if target_version != current_version and not skip_package:
        print(f"\n>>> [1/6] 正在同步 CMakeLists.txt 版本号至 {target_version}...")
        parts = [int(p) for p in target_version.split(".")]
        write_version(cmake_path, parts[0], parts[1], parts[2])
        print("    [OK] CMakeLists.txt 已更新。")

    # Step 2: Update translations (clean C++/QML extraction, once for all archs)
    if not skip_trans and not no_build:
        update_translations(repo_root, open_linguist=args.open_linguist)
    else:
        print("\n>>> [2/6] 跳过翻译更新步骤。")

    # Step 3 & 4: Build, Deploy & Package for each target architecture
    installer_dir = os.path.join(repo_root, "installer")
    iss_path = os.path.join(installer_dir, "package.iss")
    
    installer_items: List[Tuple[str, str]] = []  # list of (installer_path, sha256_hash)
    update_json_paths: List[str] = []

    if skip_package:
        print("\n>>> 跳过构建与打包，定位已有安装包...")
        existing_map = resolve_existing_packages(repo_root, arch_list, target_version)
        for arch in arch_list:
            inst_path = existing_map[arch]
            sha256_hash = compute_sha256(inst_path)
            installer_items.append((inst_path, sha256_hash))
            uj_path = os.path.join(installer_dir, ARCH_CONFIG[arch]["update_json_name"])
            generate_update_json(inst_path, target_version, sha256_hash, notes, uj_path)
            update_json_paths.append(uj_path)
    else:
        for arch in arch_list:
            print("\n" + "-" * 68)
            print(f"           【处理架构: {arch}】")
            print("-" * 68)

            custom_bdir = args.build_dir if (args.build_dir and len(arch_list) == 1) else ""
            if not no_build:
                release_dir = build_release(repo_root, arch, custom_bdir)
            else:
                build_dir = custom_bdir if custom_bdir else os.path.join(repo_root, ARCH_CONFIG[arch]["build_dir"])
                release_dir = os.path.join(build_dir, "Release")
                if not os.path.isdir(release_dir):
                    raise FileNotFoundError(f"未找到现有的 Release 目录: {release_dir}，请先执行编译！")
                deploy_qt(release_dir, arch)

            # Inno Setup packaging
            installer_path = package_installer(repo_root, target_version, arch, release_dir, installer_dir, iss_path)
            sha256_hash = compute_sha256(installer_path)
            installer_items.append((installer_path, sha256_hash))

            # Update JSON generation
            update_json_path = os.path.join(installer_dir, ARCH_CONFIG[arch]["update_json_name"])
            generate_update_json(installer_path, target_version, sha256_hash, notes, update_json_path)
            update_json_paths.append(update_json_path)

    # Step 6: Publish release to GitHub (REST API)
    tag = f"v{target_version}"
    if not skip_upload:
        api_publish_release(
            repo_owner=REPO_OWNER,
            repo_name=REPO_NAME,
            tag=tag,
            version=target_version,
            installer_items=installer_items,
            update_json_paths=update_json_paths,
            notes=notes
        )
    else:
        print("\n>>> [6/6] 跳过 GitHub 上传 (离线或本地打包模式)。")

    print("\n" + "=" * 68)
    print(f" [SUCCESS] LinDesk {target_version} ({target_arch}) 全部处理完成！")
    for inst_path, h in installer_items:
        print(f" - 安装包 : {inst_path}")
        print(f"   SHA256 : {h}")
    for uj in update_json_paths:
        print(f" - 元数据 : {uj}")
    print("=" * 68)
    return 0

if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\n[CANCELLED] Release workflow interrupted by user.")
        sys.exit(130)
    except Exception as e:
        print(f"\n[FATAL ERROR] {e}", file=sys.stderr)
        sys.exit(1)
