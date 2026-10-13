#!/usr/bin/env python3
"""
LinDesk Release & Packaging Automation Script (Python)
Integrates translation updates, version management, Release build, Inno Setup packaging,
update.json metadata generation, and GitHub Releases publication.
"""

import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from typing import Optional, Tuple

# Import clean translation update logic from update_translations.py
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from update_translations import update_translations, find_tool

REPO_OWNER = "linuup"
REPO_NAME = "LinDesk_Update"
DEFAULT_CHANNEL = "stable"
DEFAULT_MIN_VERSION = "1.0.0"

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

def build_release(repo_root: str, build_dir: str):
    print("\n>>> [2/6] Building Release configuration with CMake...")
    abs_build_dir = os.path.abspath(build_dir)
    sys.stdout.flush()

    print(f"    Configuring CMake (BUILD_TESTING=OFF) in {abs_build_dir}...")
    sys.stdout.flush()
    cfg_cmd = ["cmake", "-S", repo_root, "-B", abs_build_dir, "-DBUILD_TESTING=OFF"]
    cfg_res = subprocess.run(cfg_cmd, cwd=repo_root)
    if cfg_res.returncode != 0:
        raise RuntimeError(f"CMake configure failed with exit code {cfg_res.returncode}")

    build_cmd = [
        "cmake", "--build", abs_build_dir,
        "--config", "Release",
        "--target", "LinDesk", "LinDeskUpdate", "LinDeskSubprocess",
        "-j", "8"
    ]
    print(f"    Building targets LinDesk, LinDeskUpdate, LinDeskSubprocess...")
    sys.stdout.flush()
    build_res = subprocess.run(build_cmd, cwd=repo_root)
    if build_res.returncode != 0:
        raise RuntimeError(f"Release build failed with exit code {build_res.returncode}")

    release_dir = os.path.join(abs_build_dir, "Release")
    required = ["LinDesk.exe", "LinDeskUpdate.exe", "LinDeskSubprocess.exe", "resources", "translations"]
    missing = [item for item in required if not os.path.exists(os.path.join(release_dir, item))]
    if missing:
        raise RuntimeError(f"Missing required build outputs in {release_dir}: {missing}")

    print("    [OK] Release build completed and verified.")
    sys.stdout.flush()
    return release_dir

def package_installer(repo_root: str, version: str, build_dir: str, installer_dir: str, iss_path: str) -> str:
    print(f"\n>>> [3/6] Compiling installer with Inno Setup (version: {version})...")
    sys.stdout.flush()

    iscc_exe = find_tool(["ISCC", "ISCC.exe"], extra_dirs=[r"D:\Inno Setup 6"])
    if not iscc_exe:
        raise RuntimeError(
            "ISCC.exe not found! Please install Inno Setup 6 or add it to PATH."
        )

    print(f"    Using Inno Setup compiler: {iscc_exe}")
    print(f"    Script path: {iss_path}")
    sys.stdout.flush()

    cmd = [
        iscc_exe,
        f"/DAppVersion={version}",
        f"/DBuildDir={os.path.abspath(build_dir)}",
        f"/DPackageOutputDir={os.path.abspath(installer_dir)}",
        iss_path
    ]

    res = subprocess.run(cmd, cwd=repo_root)
    if res.returncode != 0:
        raise RuntimeError(f"Inno Setup compilation failed with exit code {res.returncode}")

    installer_name = f"LinDesk_Setup_{version}.exe"
    installer_path = os.path.join(installer_dir, installer_name)
    if not os.path.isfile(installer_path):
        raise FileNotFoundError(f"Expected installer not generated: {installer_path}")

    size_mb = os.path.getsize(installer_path) / (1024 * 1024)
    print(f"    [OK] Installer compiled successfully: {installer_path} ({size_mb:.2f} MB)")
    sys.stdout.flush()
    return installer_path

def compute_sha256(file_path: str) -> str:
    hasher = hashlib.sha256()
    with open(file_path, "rb") as f:
        while chunk := f.read(65536):
            hasher.update(chunk)
    return hasher.hexdigest().lower()

def generate_update_json(installer_path: str, version: str, sha256_hash: str, notes: str, output_path: str):
    print(f"\n>>> [4/6] Generating update.json metadata...")
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

    print(f"    [OK] update.json saved to: {output_path}")
    print(f"         SHA-256 : {sha256_hash}")
    print(f"         URL     : {download_url}")
    sys.stdout.flush()
    return metadata

def publish_github_release(repo_owner: str, repo_name: str, tag: str, version: str, 
                           installer_path: str, update_json_path: str, notes: str, sha256_hash: str):
    print(f"\n>>> [5/6] Publishing Release to GitHub ({repo_owner}/{repo_name} {tag})...")
    sys.stdout.flush()

    final_notes = notes.strip()
    if sha256_hash not in final_notes:
        final_notes += f"\n\n### Checksum\n- **Installer SHA-256**: `{sha256_hash}`\n"

    gh_exe = find_tool(["gh", "gh.exe"])
    if not gh_exe:
        print("\n[NOTE] GitHub CLI (gh) not detected on this system.")
        print(f"You can upload the release assets manually:")
        print(f"  1. Go to : https://github.com/{repo_owner}/{repo_name}/releases/new?tag={tag}")
        print(f"  2. Title : LinDesk {version}")
        print(f"  3. Attach: {installer_path}")
        print(f"             {update_json_path}")
        print(f"  4. Notes : (SHA-256 is already recorded in update.json: {sha256_hash})")
        sys.stdout.flush()
        return False

    full_repo = f"{repo_owner}/{repo_name}"
    
    # Check if release already exists
    check_cmd = [gh_exe, "release", "view", tag, "--repo", full_repo]
    check_res = subprocess.run(check_cmd, capture_output=True, text=True)
    
    if check_res.returncode == 0:
        # Release exists: upload / overwrite assets
        print(f"    Release {tag} already exists. Uploading/updating assets (--clobber)...")
        sys.stdout.flush()
        cmd = [
            gh_exe, "release", "upload", tag,
            installer_path,
            update_json_path,
            "--repo", full_repo,
            "--clobber"
        ]
    else:
        # Create new release
        cmd = [
            gh_exe, "release", "create", tag,
            installer_path,
            update_json_path,
            "--repo", full_repo,
            "--title", f"LinDesk {version}",
            "--notes", final_notes
        ]

    print(f"    Running GitHub CLI: {' '.join(cmd[:4])} ...")
    sys.stdout.flush()
    res = subprocess.run(cmd)
    if res.returncode != 0:
        print(f"\n[WARN] gh execution exited with code {res.returncode}. You may upload files manually.", file=sys.stderr)
        return False

    print(f"    [OK] Successfully published to https://github.com/{full_repo}/releases/tag/{tag}")
    sys.stdout.flush()
    return True

def print_banner(current_version: str, repo_root: str):
    print("=" * 68)
    print("        LinDesk 发版与安装包打包助手 (Release Assistant)")
    print("=" * 68)
    print("【当前项目环境】")
    print(f"  * 项目根目录   : {repo_root}")
    print(f"  * 当前版本号   : {current_version} (读取自 CMakeLists.txt)")
    print(f"  * GitHub 仓库  : {REPO_OWNER}/{REPO_NAME}")
    print(f"  * CMake 优化   : 自动附加 -DBUILD_TESTING=OFF (排除单元测试工程)")
    print("-" * 68)

def interactive_wizard(repo_root: str, current_version: str, curr_major: int, curr_minor: int, curr_patch: int):
    print("\n【请选择执行模式】")
    print("  [1] 正式发布到 GitHub (构建 + 打包 + 更新 update.json + GitHub 发布)")
    print("  [2] 仅本地构建与打包 (构建 + Inno Setup 打包 + update.json，不上传)")
    print("  [3] 仅打包现有产物   (跳过编译，直接使用已有的 Release 产物制作安装包)")
    print("  [4] 仅更新国际化翻译 (提取并编译 C++/QML 翻译 .ts -> .qm)")
    print("  [0] 退出")
    print("-" * 68)

    while True:
        choice = input("请输入选项编号 [默认: 1]: ").strip()
        if choice == "":
            choice = "1"
        if choice in ["0", "1", "2", "3", "4"]:
            break
        print("[提示] 输入无效，请输入 0、1、2、3 或 4。")

    if choice == "0":
        print("\n已退出发版助手。")
        sys.exit(0)

    if choice == "4":
        open_ling = input("\n是否在更新翻译后打开 Qt Linguist 进行校对? [y/N]: ").strip().lower() in ["y", "yes"]
        update_translations(repo_root, open_linguist=open_ling)
        print("\n[SUCCESS] 翻译文件处理完毕。")
        sys.exit(0)

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
            custom_ver = input("请输入自定义版本号 (格式 X.Y.Z，例如 1.1.0): ").strip()
            if re.match(r"^\d+\.\d+\.\d+$", custom_ver):
                target_version = custom_ver
                break
            print("[错误] 版本号格式不合规，必须为三段数字，如 1.0.9")
        else:
            print("[提示] 请输入 1、2、3 或 4。")

    # Release Notes 配置
    notes = ""
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

    # 确定参数
    no_build = (choice == "3")
    skip_upload = (choice in ["2", "3"])

    mode_name_map = {
        "1": "正式发布到 GitHub (构建 + 打包 + 上传)",
        "2": "仅本地构建与打包 (不上传 GitHub)",
        "3": "仅打包现有产物 (跳过编译，直接生成安装包)"
    }

    # 汇总确认
    print("\n" + "=" * 68)
    print("                      【执行配置确认】")
    print("=" * 68)
    print(f"  * 执行模式     : {mode_name_map[choice]}")
    print(f"  * 目标版本号   : {target_version} (当前: {current_version})")
    print(f"  * CMake 编译   : {'跳过 (仅打包现有 Release 产物)' if no_build else '执行 (Release, -DBUILD_TESTING=OFF)'}")
    print(f"  * 更新翻译     : {'跳过' if skip_trans or no_build else '执行 (lupdate & lrelease)'}")
    print(f"  * GitHub 发布  : {'否 (离线/仅本地)' if skip_upload else f'是 (目标仓库: {REPO_OWNER}/{REPO_NAME})'}")
    if not skip_upload:
        print(f"  * 更新日志     : {notes}")
    print("=" * 68)

    confirm = input("确认开始执行上述流程? [Y/n]: ").strip().lower()
    if confirm not in ["", "y", "yes"]:
        print("\n[CANCELLED] 用户取消操作。")
        sys.exit(0)

    return target_version, notes, skip_upload, no_build, skip_trans

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

    parser = argparse.ArgumentParser(description="LinDesk unified release & packaging assistant.")
    parser.add_argument("--version", default="", help="Release version (e.g. 1.0.9). Defaults to current in CMakeLists.txt")
    parser.add_argument("--bump", choices=["major", "minor", "patch"], default="", help="Bump version automatically")
    parser.add_argument("--skip-translations", action="store_true", help="Skip running lupdate on translations")
    parser.add_argument("--update-translations-only", action="store_true", help="Only run lupdate/lrelease and exit")
    parser.add_argument("--open-linguist", action="store_true", help="Open Qt Linguist after updating translations")
    parser.add_argument("--no-build", action="store_true", help="Skip Release building step")
    parser.add_argument("--notes", default="", help="Release notes text")
    parser.add_argument("--notes-file", default="", help="File containing release notes")
    parser.add_argument("--skip-upload", action="store_true", help="Build and generate metadata without uploading to GitHub")
    parser.add_argument("--build-dir", default="", help="Path to CMake build directory (default: build/build_64)")
    parser.add_argument("--interactive", "-i", action="store_true", help="Force interactive wizard")

    args = parser.parse_args()
    repo_root = get_repo_root()
    cmake_path = os.path.join(repo_root, "CMakeLists.txt")

    curr_major, curr_minor, curr_patch, current_version = read_version(cmake_path)

    # If only updating translations via CLI
    if args.update_translations_only:
        update_translations(repo_root, open_linguist=args.open_linguist)
        print("[SUCCESS] Translations updated.")
        return 0

    print_banner(current_version, repo_root)

    # Check whether to enter interactive mode:
    # If explicitly requested (-i) OR no specific operational arguments were passed in terminal
    has_cli_ops = bool(args.version or args.bump or args.skip_upload or args.no_build or args.notes or args.notes_file)
    use_interactive = args.interactive or (not has_cli_ops and sys.stdin.isatty())

    if use_interactive:
        target_version, notes, skip_upload, no_build, skip_trans = interactive_wizard(
            repo_root, current_version, curr_major, curr_minor, curr_patch
        )
    else:
        # CLI direct mode
        target_version = current_version
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

    print(f"\n>>> 正在准备启动流水线...")
    print(f"    当前版本 : {current_version}")
    print(f"    目标版本 : {target_version}")
    print(f"    更新日志 : {notes}")
    sys.stdout.flush()

    # Synchronize version to CMakeLists.txt if changed
    if target_version != current_version:
        print(f"\n>>> [1/6] 正在同步 CMakeLists.txt 版本号至 {target_version}...")
        parts = [int(p) for p in target_version.split(".")]
        write_version(cmake_path, parts[0], parts[1], parts[2])
        print("    [OK] CMakeLists.txt 已更新。")

        # Reconfigure CMake to update targets
        build_dir = args.build_dir if args.build_dir else os.path.join(repo_root, "build", "build_64")
        if os.path.isdir(build_dir):
            print("    正在重新生成 CMake 配置 (-DBUILD_TESTING=OFF)...")
            subprocess.run(["cmake", "-S", repo_root, "-B", build_dir, "-DBUILD_TESTING=OFF"], cwd=repo_root)

    # Step 2: Update translations (clean C++/QML extraction)
    if not skip_trans and not no_build:
        update_translations(repo_root, open_linguist=args.open_linguist)
    else:
        print("\n>>> [2/6] 跳过翻译更新步骤。")

    # Step 3: Build Release binaries
    build_dir = args.build_dir if args.build_dir else os.path.join(repo_root, "build", "build_64")
    release_dir = os.path.join(build_dir, "Release")
    if not no_build:
        release_dir = build_release(repo_root, build_dir)
    else:
        print("\n>>> [3/6] 跳过 Release 编译步骤 (使用已有构建产物)。")
        if not os.path.isdir(release_dir):
            raise FileNotFoundError(f"未找到现有的 Release 目录: {release_dir}，请先执行编译！")

    # Step 4: Package installer with Inno Setup
    installer_dir = os.path.join(repo_root, "installer")
    iss_path = os.path.join(installer_dir, "package.iss")
    installer_path = package_installer(repo_root, target_version, release_dir, installer_dir, iss_path)

    # Step 5: Compute SHA256 & Generate update.json
    sha256_hash = compute_sha256(installer_path)
    update_json_path = os.path.join(installer_dir, "update.json")
    generate_update_json(installer_path, target_version, sha256_hash, notes, update_json_path)

    # Step 6: Publish release to GitHub
    tag = f"v{target_version}"
    if not skip_upload:
        publish_github_release(
            repo_owner=REPO_OWNER,
            repo_name=REPO_NAME,
            tag=tag,
            version=target_version,
            installer_path=installer_path,
            update_json_path=update_json_path,
            notes=notes,
            sha256_hash=sha256_hash
        )
    else:
        print("\n>>> [6/6] 跳过 GitHub 上传 (离线或本地打包模式)。")

    print("\n" + "=" * 68)
    print(f" [SUCCESS] LinDesk {target_version} 处理完成！")
    print(f" - 安装包路径  : {installer_path}")
    print(f" - 更新元数据  : {update_json_path}")
    print(f" - SHA-256 哈希: {sha256_hash}")
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
