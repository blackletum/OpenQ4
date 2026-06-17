#!/usr/bin/env python3
"""Create curated release distributable archives for openQ4."""

from __future__ import annotations

import argparse
import filecmp
import json
import os
import plistlib
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from windows_runtime import (
    RuntimeFlavor,
    infer_runtime_flavor,
    list_staged_runtime_files,
)
from generate_release_docs import GeneratedDocSite, generate_release_docs_site


PRODUCT_NAME = "openQ4"
GAME_DIR_NAME = "baseoq4"
RELEASE_README_PATH = Path("assets") / "release" / "README.html"
LICENSE_PATH = Path("LICENSE")
SUPPORTED_ARCHES = ("x64", "x86", "arm64")

PLATFORM_EXECUTABLE_EXT = {
    "windows": ".exe",
    "linux": "",
    "macos": "",
}

PLATFORM_GAME_MODULE_EXT = {
    "windows": ".dll",
    "linux": ".so",
    "macos": ".dylib",
}

DEFAULT_ARCHIVE_FORMAT = {
    "windows": "zip",
    "linux": "tar.xz",
    "macos": "tar.gz",
}

ARCHIVE_SUFFIX = {
    "zip": ".zip",
    "tar.gz": ".tar.gz",
    "tar.xz": ".tar.xz",
}

OPENQ4_EXCLUDED_DIRS = {"logs", "screenshots"}
OPENQ4_PK4_EXCLUDED_SUFFIXES = {
    ".dll",
    ".so",
    ".dylib",
    ".pdb",
    ".lib",
    ".exp",
    ".ilk",
}
OPENQ4_REQUIRED_PK4_FILES = {
    "gfx/guis/loadscreens/generic.dds",
    "gfx/guis/loadscreens/generic.tga",
    "glprogs/smaa_blend.fs",
    "glprogs/smaa_blend.vs",
    "glprogs/smaa_edge.fs",
    "glprogs/smaa_edge.vs",
    "glprogs/smaa_weights.fs",
    "glprogs/smaa_weights.vs",
    "materials/postprocess_openq4.mtr",
}
OPENQ4_REQUIRED_LOOSE_GAME_FILES = {
    "mod.json",
}
OPENQ4_PK4_EXCLUDED_FILES = {
    relative_path.lower() for relative_path in OPENQ4_REQUIRED_LOOSE_GAME_FILES
}
MACOS_EXPECTED_PLIST_VALUES = {
    "CFBundleExecutable": "openQ4",
    "CFBundleIconFile": "openQ4.icns",
    "CFBundleIdentifier": "com.darkmatter.openq4",
    "CFBundleName": "openQ4",
    "CFBundlePackageType": "APPL",
    "LSMinimumSystemVersion": "11.0",
    "NSPrincipalClass": "NSApplication",
}
MACOS_ALLOWED_RUNTIME_DEPENDENCY_PREFIXES = (
    "/System/Library/",
    "/usr/lib/",
)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Package openQ4 release artifacts into a release archive."
    )
    parser.add_argument(
        "--platform",
        required=True,
        choices=sorted(PLATFORM_GAME_MODULE_EXT.keys()),
        help="Target runner platform (windows/linux/macos).",
    )
    parser.add_argument(
        "--arch",
        default="x64",
        choices=SUPPORTED_ARCHES,
        help="Target binary architecture tag (default: x64).",
    )
    parser.add_argument(
        "--version",
        required=True,
        help="Human-readable release version string.",
    )
    parser.add_argument(
        "--version-tag",
        required=True,
        help="File-safe release version tag.",
    )
    parser.add_argument(
        "--source-root",
        default=".",
        help="openQ4 repository root.",
    )
    parser.add_argument(
        "--install-dir",
        default=None,
        help="Install directory to package (defaults to <source-root>/.install).",
    )
    parser.add_argument(
        "--output-dir",
        required=True,
        help="Output directory for the generated package artifacts.",
    )
    parser.add_argument(
        "--archive-format",
        choices=sorted(ARCHIVE_SUFFIX.keys()),
        default=None,
        help=(
            "Archive format for the top-level package (default: platform-specific; "
            "windows=zip, linux=tar.xz, macos=tar.gz)."
        ),
    )
    parser.add_argument(
        "--package-suffix",
        default="",
        help=(
            "Optional file-name suffix appended after the platform/arch stem, "
            "for release variants such as -opengl or -metal."
        ),
    )
    parser.add_argument(
        "--allow-missing-binaries",
        action="store_true",
        help=(
            "Allow packaging to continue when required platform binaries are missing. "
            "Useful for host bring-up previews."
        ),
    )
    return parser.parse_args(argv[1:])


def normalize_package_suffix(raw_suffix: str) -> str:
    suffix = raw_suffix.strip()
    if suffix == "":
        return ""
    if not suffix.startswith("-"):
        suffix = "-" + suffix
    if not re.fullmatch(r"-[A-Za-z0-9][A-Za-z0-9._-]*", suffix):
        raise ValueError(
            "package suffix must be empty or a file-name-safe value such as -opengl or -metal"
        )
    return suffix


def get_required_root_binaries(platform: str, arch: str) -> tuple[str, str]:
    exe_ext = PLATFORM_EXECUTABLE_EXT[platform]
    return (
        f"{PRODUCT_NAME}-client_{arch}{exe_ext}",
        f"{PRODUCT_NAME}-ded_{arch}{exe_ext}",
    )


def get_required_game_module_binaries(platform: str, arch: str) -> tuple[str, str]:
    module_ext = PLATFORM_GAME_MODULE_EXT[platform]
    return (
        f"game-sp_{arch}{module_ext}",
        f"game-mp_{arch}{module_ext}",
    )


def get_required_windows_root_symbols(arch: str) -> tuple[str, str]:
    return (
        f"{PRODUCT_NAME}-client_{arch}.pdb",
        f"{PRODUCT_NAME}-ded_{arch}.pdb",
    )


def get_required_windows_game_symbols(arch: str) -> tuple[str, str]:
    return (
        f"game-sp_{arch}.pdb",
        f"game-mp_{arch}.pdb",
    )


def write_text_file(path: Path, lines: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def ensure_posix_executable(path: Path) -> None:
    if os.name == "nt":
        return
    os.chmod(path, path.stat().st_mode | 0o755)


def write_version_manifest(
    destination: Path,
    *,
    version: str,
    version_tag: str,
    platform: str,
    arch: str,
) -> None:
    write_text_file(
        destination,
        [
            PRODUCT_NAME,
            f"version={version}",
            f"version_tag={version_tag}",
            f"platform={platform}",
            f"arch={arch}",
        ],
    )


def parse_stable_base_version(version: str) -> tuple[int, int, int] | None:
    parts = version.split(".")
    if len(parts) != 3 or not all(part.isdigit() for part in parts):
        return None
    return tuple(int(part) for part in parts)


def is_stable_base_version(version: str) -> bool:
    return parse_stable_base_version(version) is not None


def validate_packaged_mod_manifest(package_game_dir: Path, version: str) -> None:
    release_version = parse_stable_base_version(version)
    if release_version is None:
        return

    manifest_path = package_game_dir / "mod.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise RuntimeError(f"packaged mod manifest is unreadable: {manifest_path}") from exc

    mismatches = []
    if manifest.get("version") != version:
        mismatches.append(f"version={manifest.get('version')!r}")

    required_version_text = manifest.get("requiredopenQ4Version")
    required_version = (
        parse_stable_base_version(required_version_text)
        if isinstance(required_version_text, str)
        else None
    )
    if required_version is None:
        mismatches.append(f"requiredopenQ4Version={required_version_text!r}")
    elif required_version > release_version:
        mismatches.append(
            f"requiredopenQ4Version={required_version_text!r} "
            f"(requires newer than package {version})"
        )

    if mismatches:
        joined = ", ".join(mismatches)
        raise RuntimeError(
            f"packaged {GAME_DIR_NAME}/mod.json does not satisfy release {version}: {joined}"
        )


def write_macos_localized_info_strings(app_contents: Path, version: str) -> None:
    localized_info_lines = [
        "/* Localized versions of Info.plist keys */",
        "",
        'CFBundleName = "openQ4";',
        f'CFBundleShortVersionString = "{version}";',
        f'CFBundleGetInfoString = "openQ4 version {version}, Copyright 2026 DarkMatter Productions";',
        'NSHumanReadableCopyright = "Copyright 2026 DarkMatter Productions";',
    ]

    for locale in ("English", "French"):
        write_text_file(
            app_contents / "Resources" / f"{locale}.lproj" / "InfoPlist.strings",
            localized_info_lines,
        )


def copy_release_collateral(source_root: Path, package_root: Path) -> None:
    collateral = (
        (source_root / RELEASE_README_PATH, package_root / "README.html"),
        (source_root / LICENSE_PATH, package_root / "LICENSE"),
    )

    for source, destination in collateral:
        if not source.is_file():
            raise FileNotFoundError(f"required release collateral not found: {source}")
        shutil.copy2(source, destination)


def generate_release_documentation(
    *,
    source_root: Path,
    package_root: Path,
    version: str,
    platform: str,
    arch: str,
) -> GeneratedDocSite:
    return generate_release_docs_site(
        source_root=source_root,
        output_root=package_root / "docs",
        version=version,
        platform=platform,
        arch=arch,
    )


def copy_required_binaries(
    platform: str,
    arch: str,
    install_dir: Path,
    package_root: Path,
    allow_missing_binaries: bool,
) -> list[str]:
    missing_required: list[str] = []

    for filename in get_required_root_binaries(platform, arch):
        source = install_dir / filename
        if not source.is_file():
            if allow_missing_binaries:
                missing_required.append(filename)
                continue
            raise FileNotFoundError(f"required distributable not found: {source}")
        destination = package_root / filename
        shutil.copy2(source, destination)
        if platform in ("linux", "macos"):
            ensure_posix_executable(destination)

    if platform == "windows":
        missing_required.extend(
            copy_required_windows_runtime(
                arch=arch,
                install_dir=install_dir,
                package_root=package_root,
                allow_missing_binaries=allow_missing_binaries,
            )
        )

    return missing_required


def copy_required_windows_runtime(
    arch: str,
    install_dir: Path,
    package_root: Path,
    allow_missing_binaries: bool,
) -> list[str]:
    missing_required: list[str] = []
    runtime_files = list_staged_runtime_files(install_dir)

    runtime_flavor = infer_runtime_flavor(install_dir)
    if runtime_flavor != RuntimeFlavor.NONE:
        raise RuntimeError(
            "Windows package staging detected MSVC/UCRT runtime imports. "
            "Public openQ4 packages must be built with the static CRT policy."
        )

    for source in runtime_files:
        shutil.copy2(source, package_root / source.name)

    if not runtime_files:
        if allow_missing_binaries:
            missing_required.append("OpenAL32.dll")
        else:
            raise FileNotFoundError(
                f"required Windows runtime not found for {arch}: {install_dir / 'OpenAL32.dll'}"
            )

    return missing_required


def copy_required_game_binaries(
    platform: str,
    arch: str,
    install_game_dir: Path,
    package_game_dir: Path,
    allow_missing_binaries: bool,
) -> list[str]:
    missing_required: list[str] = []

    for filename in get_required_game_module_binaries(platform, arch):
        source = install_game_dir / filename
        if not source.is_file():
            if allow_missing_binaries:
                missing_required.append(filename)
                continue
            raise FileNotFoundError(f"required game module not found: {source}")
        shutil.copy2(source, package_game_dir / filename)

    return missing_required


def copy_required_windows_symbols(
    arch: str,
    install_dir: Path,
    package_root: Path,
    install_game_dir: Path,
    package_game_dir: Path,
    allow_missing_binaries: bool,
) -> list[str]:
    missing_required: list[str] = []

    for filename in get_required_windows_root_symbols(arch):
        source = install_dir / filename
        if not source.is_file():
            if allow_missing_binaries:
                missing_required.append(filename)
                continue
            raise FileNotFoundError(f"required Windows diagnostic symbol not found: {source}")
        shutil.copy2(source, package_root / filename)

    for filename in get_required_windows_game_symbols(arch):
        source = install_game_dir / filename
        if not source.is_file():
            if allow_missing_binaries:
                missing_required.append(f"{GAME_DIR_NAME}/{filename}")
                continue
            raise FileNotFoundError(f"required Windows game diagnostic symbol not found: {source}")
        shutil.copy2(source, package_game_dir / filename)

    return missing_required


def copy_required_loose_game_files(
    install_game_dir: Path,
    package_game_dir: Path,
    allow_missing_binaries: bool,
) -> list[str]:
    missing_required: list[str] = []

    for relative_path in sorted(OPENQ4_REQUIRED_LOOSE_GAME_FILES):
        source = install_game_dir / Path(relative_path)
        if not source.is_file():
            if allow_missing_binaries:
                missing_required.append(relative_path)
                continue
            raise FileNotFoundError(f"required loose game file not found: {source}")

        destination = package_game_dir / Path(relative_path)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)

    return missing_required


def create_game_pk4(
    install_game_dir: Path, destination_pk4: Path
) -> tuple[int, list[str], list[str]]:
    added_files = 0
    skipped_samples: list[str] = []
    added_paths: set[str] = set()

    with ZipFile(destination_pk4, "w", compression=ZIP_DEFLATED, compresslevel=9) as pk4:
        for path in sorted(install_game_dir.rglob("*")):
            if not path.is_file():
                continue

            rel = path.relative_to(install_game_dir)
            rel_parts_lower = {part.lower() for part in rel.parts}
            rel_posix_lower = rel.as_posix().lower()

            if rel_parts_lower & OPENQ4_EXCLUDED_DIRS:
                if len(skipped_samples) < 5:
                    skipped_samples.append(rel.as_posix())
                continue

            if rel_posix_lower in OPENQ4_PK4_EXCLUDED_FILES:
                if len(skipped_samples) < 5:
                    skipped_samples.append(rel.as_posix())
                continue

            if path.suffix.lower() in OPENQ4_PK4_EXCLUDED_SUFFIXES:
                if len(skipped_samples) < 5:
                    skipped_samples.append(rel.as_posix())
                continue

            arcname = rel.as_posix()
            pk4.write(path, arcname=arcname)
            added_paths.add(arcname.lower())
            added_files += 1

    missing_required = sorted(
        required_path
        for required_path in OPENQ4_REQUIRED_PK4_FILES
        if required_path.lower() not in added_paths
    )

    return added_files, skipped_samples, missing_required


def create_release_archive(
    package_root: Path,
    archive_path: Path,
    archive_format: str,
    executable_relative_paths: set[Path] | None = None,
) -> None:
    if archive_path.exists():
        archive_path.unlink()

    executable_archive_paths = {
        relative_path.as_posix()
        for relative_path in (executable_relative_paths or set())
    }

    if archive_format == "zip":
        with ZipFile(archive_path, "w", compression=ZIP_DEFLATED, compresslevel=9) as archive:
            for path in sorted(package_root.rglob("*")):
                if not path.is_file():
                    continue
                relative_path = path.relative_to(package_root)
                arcname = (Path(package_root.name) / relative_path).as_posix()
                info = ZipInfo.from_file(path, arcname)
                info.compress_type = ZIP_DEFLATED
                if relative_path.as_posix() in executable_archive_paths:
                    mode = ((info.external_attr >> 16) | 0o100755) & 0o177777
                    info.external_attr = mode << 16
                    info.create_system = 3
                archive.writestr(info, path.read_bytes(), compresslevel=9)
        return

    mode = {"tar.gz": "w:gz", "tar.xz": "w:xz"}[archive_format]
    with tarfile.open(archive_path, mode) as archive:
        for path in sorted(package_root.rglob("*")):
            if not path.is_file():
                continue
            relative_path = path.relative_to(package_root)
            arcname = (Path(package_root.name) / relative_path).as_posix()
            info = archive.gettarinfo(path, arcname=arcname)
            if relative_path.as_posix() in executable_archive_paths:
                info.mode = 0o755
            with path.open("rb") as handle:
                archive.addfile(info, handle)


def get_package_executable_archive_paths(
    platform: str, arch: str, copied_linux_launchers: list[str]
) -> set[Path]:
    if platform not in ("linux", "macos"):
        return set()

    executable_paths = {Path(filename) for filename in get_required_root_binaries(platform, arch)}
    if platform == "linux":
        executable_paths.update(Path(filename) for filename in copied_linux_launchers)
    elif platform == "macos":
        executable_paths.add(Path("openQ4.app") / "Contents" / "MacOS" / "openQ4")

    return executable_paths


def validate_macos_plist_values(plist: dict, label: str, version: str | None = None) -> None:
    for key, expected in MACOS_EXPECTED_PLIST_VALUES.items():
        if plist.get(key) != expected:
            raise RuntimeError(f"{label} {key} is {plist.get(key)!r}; expected {expected!r}")

    if version is not None:
        for key in ("CFBundleShortVersionString", "CFBundleVersion"):
            if plist.get(key) != version:
                raise RuntimeError(f"{label} {key} is {plist.get(key)!r}; expected {version!r}")

    if plist.get("NSHighResolutionCapable") is not True:
        raise RuntimeError(f"{label} must set NSHighResolutionCapable=true")
    if plist.get("NSSupportsAutomaticGraphicsSwitching") is not True:
        raise RuntimeError(f"{label} must set NSSupportsAutomaticGraphicsSwitching=true")


def validate_macos_archive_contents(
    package_root: Path, archive_path: Path, archive_format: str, arch: str, version: str
) -> None:
    package_prefix = package_root.name + "/"
    client_entry = f"{package_prefix}openQ4-client_{arch}"
    dedicated_entry = f"{package_prefix}openQ4-ded_{arch}"
    app_executable_entry = f"{package_prefix}openQ4.app/Contents/MacOS/openQ4"
    expected_entries = {
        client_entry,
        dedicated_entry,
        f"{package_prefix}{GAME_DIR_NAME}/mod.json",
        f"{package_prefix}{GAME_DIR_NAME}/pak0.pk4",
        f"{package_prefix}openQ4.app/Contents/Info.plist",
        app_executable_entry,
        f"{package_prefix}openQ4.app/Contents/Resources/openQ4.icns",
        f"{package_prefix}openQ4.app/Contents/Resources/VERSION.txt",
        f"{package_prefix}openQ4.app/Contents/Resources/English.lproj/InfoPlist.strings",
        f"{package_prefix}openQ4.app/Contents/Resources/French.lproj/InfoPlist.strings",
    }
    executable_entries = {
        client_entry,
        dedicated_entry,
        app_executable_entry,
    }
    plist_entry = f"{package_prefix}openQ4.app/Contents/Info.plist"

    modes: dict[str, int] = {}
    entry_names: set[str] = set()
    plist_bytes: bytes | None = None
    client_bytes: bytes | None = None
    app_executable_bytes: bytes | None = None

    if archive_format == "zip":
        with ZipFile(archive_path, "r") as archive:
            for info in archive.infolist():
                name = info.filename.rstrip("/")
                if not name:
                    continue
                entry_names.add(name)
                modes[name] = (info.external_attr >> 16) & 0o777
            if plist_entry in entry_names:
                plist_bytes = archive.read(plist_entry)
            if client_entry in entry_names:
                client_bytes = archive.read(client_entry)
            if app_executable_entry in entry_names:
                app_executable_bytes = archive.read(app_executable_entry)
    else:
        with tarfile.open(archive_path, "r:*") as archive:
            for member in archive.getmembers():
                name = member.name.rstrip("/")
                if not name:
                    continue
                entry_names.add(name)
                modes[name] = member.mode
                if name == plist_entry:
                    extracted = archive.extractfile(member)
                    if extracted is not None:
                        plist_bytes = extracted.read()
                elif name == client_entry:
                    extracted = archive.extractfile(member)
                    if extracted is not None:
                        client_bytes = extracted.read()
                elif name == app_executable_entry:
                    extracted = archive.extractfile(member)
                    if extracted is not None:
                        app_executable_bytes = extracted.read()

    bad_names = [
        name
        for name in sorted(entry_names)
        if name.startswith("/") or "/../" in f"/{name}/" or not name.startswith(package_prefix)
    ]
    if bad_names:
        joined = ", ".join(bad_names[:5])
        raise RuntimeError(f"macOS archive contains unsafe or out-of-package paths: {joined}")

    missing_entries = sorted(expected_entries - entry_names)
    if missing_entries:
        joined = ", ".join(missing_entries)
        raise RuntimeError(f"macOS archive is missing required entries: {joined}")

    for entry in sorted(executable_entries):
        if modes.get(entry, 0) & 0o111 == 0:
            raise RuntimeError(f"macOS archive entry is not executable: {entry}")

    if plist_bytes is None:
        raise RuntimeError(f"macOS archive Info.plist is unreadable: {plist_entry}")
    if client_bytes is None or app_executable_bytes is None:
        raise RuntimeError("macOS archive executable payloads are unreadable")
    if client_bytes != app_executable_bytes:
        raise RuntimeError(
            "macOS archive app executable does not match packaged client binary"
        )
    try:
        validate_macos_plist_values(
            plistlib.loads(plist_bytes),
            "macOS archive Info.plist",
            version,
        )
    except plistlib.InvalidFileException as exc:
        raise RuntimeError(f"macOS archive Info.plist is invalid: {plist_entry}") from exc


def macos_otool_dependencies(binary_path: Path) -> list[str]:
    if sys.platform != "darwin":
        return []

    otool_path = shutil.which("otool")
    if otool_path is None:
        raise RuntimeError("macOS dependency validation requires otool")

    completed = subprocess.run(
        [otool_path, "-L", str(binary_path)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode != 0:
        message = completed.stderr.strip() or completed.stdout.strip()
        raise RuntimeError(f"otool failed for macOS binary {binary_path}: {message}")

    dependencies: list[str] = []
    for line in completed.stdout.splitlines()[1:]:
        dependency = line.strip().split(" (", 1)[0].strip()
        if dependency:
            dependencies.append(dependency)

    if binary_path.suffix == ".dylib" and dependencies:
        # The first entry for a dylib is its install name, not a library it loads.
        dependencies = dependencies[1:]

    return dependencies


def validate_macos_binary_dependencies(package_root: Path, arch: str) -> None:
    if sys.platform != "darwin":
        return

    binary_paths = [
        package_root / f"{PRODUCT_NAME}-client_{arch}",
        package_root / f"{PRODUCT_NAME}-ded_{arch}",
        package_root / "openQ4.app" / "Contents" / "MacOS" / "openQ4",
        package_root / GAME_DIR_NAME / f"game-sp_{arch}.dylib",
        package_root / GAME_DIR_NAME / f"game-mp_{arch}.dylib",
    ]

    for binary_path in binary_paths:
        require_packaged_executable(binary_path, "macOS dependency validation binary")
        rejected_dependencies = [
            dependency
            for dependency in macos_otool_dependencies(binary_path)
            if not dependency.startswith(MACOS_ALLOWED_RUNTIME_DEPENDENCY_PREFIXES)
        ]
        if rejected_dependencies:
            joined = ", ".join(rejected_dependencies)
            raise RuntimeError(
                f"macOS binary has unbundled non-system dependencies: {binary_path}: {joined}"
            )


def copy_optional_share_tree(platform: str, install_dir: Path, package_root: Path) -> bool:
    if platform != "linux":
        return False

    share_source = install_dir / "share"
    if not share_source.is_dir():
        return False

    share_dest = package_root / "share"
    shutil.copytree(share_source, share_dest, dirs_exist_ok=True)
    return True


def copy_optional_linux_launchers(install_dir: Path, package_root: Path) -> list[str]:
    copied: list[str] = []

    for filename in ("openQ4-steamdeck",):
        source = install_dir / filename
        if not source.is_file():
            continue

        destination = package_root / filename
        shutil.copy2(source, destination)
        os.chmod(destination, 0o755)
        copied.append(filename)

    return copied


def desktop_entry_exec(path: Path) -> str:
    try:
        for line in path.read_text(encoding="utf-8").splitlines():
            if line.startswith("Exec="):
                return line[5:].strip()
    except OSError as exc:
        raise RuntimeError(f"Linux desktop entry is unreadable: {path}") from exc
    return ""


def desktop_exec_command(exec_line: str) -> str:
    if not exec_line:
        return ""
    try:
        parts = shlex.split(exec_line, posix=True)
    except ValueError:
        parts = exec_line.split()
    return parts[0] if parts else ""


def require_packaged_executable(path: Path, label: str, *, allow_missing: bool = False) -> None:
    if not path.is_file():
        if allow_missing:
            return
        raise RuntimeError(f"{label} is missing from the package: {path}")
    if not os.access(path, os.X_OK):
        raise RuntimeError(f"{label} is not executable in the package: {path}")


def validate_linux_steamdeck_launcher(path: Path, expected_client: str) -> None:
    try:
        launcher = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise RuntimeError(f"Linux Steam Deck launcher is unreadable: {path}") from exc

    required_tokens = (
        "OPENQ4_STEAMDECK",
        "OPENQ4_FORCE_X11",
        "SDL_VIDEO_DRIVER=x11",
        "SDL_VIDEODRIVER=x11",
        "+set com_platformProfile steamdeck",
    )
    for token in required_tokens:
        if token not in launcher:
            raise RuntimeError(f"Linux Steam Deck launcher is missing {token!r}: {path}")

    if "WAYLAND_DISPLAY" in launcher:
        raise RuntimeError(f"Linux Steam Deck launcher still forces X11 from Wayland session state: {path}")

    if expected_client not in launcher:
        raise RuntimeError(
            f"Linux Steam Deck launcher does not exec the packaged client {expected_client!r}: {path}"
        )


def validate_linux_package_metadata(package_root: Path, arch: str, *, allow_missing_binaries: bool = False) -> None:
    client_binary = f"{PRODUCT_NAME}-client_{arch}"
    require_packaged_executable(
        package_root / client_binary,
        "Linux client binary",
        allow_missing=allow_missing_binaries,
    )
    require_packaged_executable(
        package_root / f"{PRODUCT_NAME}-ded_{arch}",
        "Linux dedicated-server binary",
        allow_missing=allow_missing_binaries,
    )
    steamdeck_launcher = package_root / "openQ4-steamdeck"
    require_packaged_executable(steamdeck_launcher, "Linux Steam Deck launcher")
    validate_linux_steamdeck_launcher(steamdeck_launcher, client_binary)

    desktop_dir = package_root / "share" / "applications"
    expected_exec = {
        "openq4.desktop": client_binary,
        "openq4-steamdeck.desktop": "openQ4-steamdeck",
    }
    for filename, expected_command in expected_exec.items():
        desktop_path = desktop_dir / filename
        if not desktop_path.is_file():
            raise RuntimeError(f"Linux desktop entry is missing from the package: {desktop_path}")

        exec_command = desktop_exec_command(desktop_entry_exec(desktop_path))
        if exec_command != expected_command:
            raise RuntimeError(
                f"Linux desktop entry {desktop_path} points at {exec_command or '<empty>'!r}; "
                f"expected {expected_command!r}"
            )


def validate_macos_app_bundle(package_root: Path, app_root: Path, arch: str, version: str) -> None:
    client_binary = package_root / f"{PRODUCT_NAME}-client_{arch}"
    require_packaged_executable(client_binary, "macOS client binary")

    package_game_dir = package_root / GAME_DIR_NAME
    if not package_game_dir.is_dir():
        raise RuntimeError(f"macOS package is missing {GAME_DIR_NAME}/ beside the app bundle: {package_game_dir}")

    app_contents = app_root / "Contents"
    app_plist = app_contents / "Info.plist"
    app_executable = app_contents / "MacOS" / "openQ4"
    app_icon = app_contents / "Resources" / "openQ4.icns"
    app_version = app_contents / "Resources" / "VERSION.txt"

    if not app_plist.is_file():
        raise RuntimeError(f"macOS app bundle is missing Info.plist: {app_plist}")
    if not app_executable.is_file() or not os.access(app_executable, os.X_OK):
        raise RuntimeError(f"macOS app executable is missing or not executable: {app_executable}")
    if not app_icon.is_file():
        raise RuntimeError(f"macOS app bundle is missing its icon: {app_icon}")
    if not app_version.is_file():
        raise RuntimeError(f"macOS app bundle is missing its version manifest: {app_version}")

    try:
        plist = plistlib.loads(app_plist.read_bytes())
    except (OSError, plistlib.InvalidFileException) as exc:
        raise RuntimeError(f"macOS app Info.plist is unreadable: {app_plist}") from exc

    validate_macos_plist_values(plist, "macOS app Info.plist", version)

    if not filecmp.cmp(client_binary, app_executable, shallow=False):
        raise RuntimeError(
            f"macOS app executable does not match packaged client binary: {app_executable}"
        )


def create_macos_app_bundle(
    package_root: Path,
    install_dir: Path,
    arch: str,
    version: str,
    version_tag: str,
) -> Path:
    app_root = package_root / "openQ4.app"
    app_contents = app_root / "Contents"
    app_macos = app_contents / "MacOS"
    app_resources = app_contents / "Resources"

    app_macos.mkdir(parents=True, exist_ok=True)
    app_resources.mkdir(parents=True, exist_ok=True)

    client_binary = package_root / f"{PRODUCT_NAME}-client_{arch}"
    if not client_binary.is_file():
        raise RuntimeError(f"macOS client binary is missing before app bundle creation: {client_binary}")

    app_executable = app_macos / "openQ4"
    shutil.copy2(client_binary, app_executable)
    os.chmod(app_executable, 0o755)

    icns_candidates = [
        install_dir / "openQ4.icns",
        install_dir / "quake4.icns",
    ]
    for icns_source in icns_candidates:
        if icns_source.is_file():
            shutil.copy2(icns_source, app_resources / "openQ4.icns")
            break

    info_plist = app_contents / "Info.plist"
    write_text_file(
        info_plist,
        [
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>",
            "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">",
            "<plist version=\"1.0\">",
            "<dict>",
            "<key>CFBundleDevelopmentRegion</key>",
            "<string>English</string>",
            "<key>CFBundleExecutable</key>",
            "<string>openQ4</string>",
            "<key>CFBundleIconFile</key>",
            "<string>openQ4.icns</string>",
            "<key>CFBundleIdentifier</key>",
            "<string>com.darkmatter.openq4</string>",
            "<key>CFBundleInfoDictionaryVersion</key>",
            "<string>6.0</string>",
            "<key>CFBundleName</key>",
            "<string>openQ4</string>",
            "<key>CFBundlePackageType</key>",
            "<string>APPL</string>",
            "<key>CFBundleShortVersionString</key>",
            f"<string>{version}</string>",
            "<key>CFBundleVersion</key>",
            f"<string>{version}</string>",
            "<key>LSMinimumSystemVersion</key>",
            "<string>11.0</string>",
            "<key>NSPrincipalClass</key>",
            "<string>NSApplication</string>",
            "<key>NSHighResolutionCapable</key>",
            "<true/>",
            "<key>NSSupportsAutomaticGraphicsSwitching</key>",
            "<true/>",
            "</dict>",
            "</plist>",
        ],
    )
    write_macos_localized_info_strings(app_contents, version)
    write_version_manifest(
        app_resources / "VERSION.txt",
        version=version,
        version_tag=version_tag,
        platform="macos",
        arch=arch,
    )

    return app_root


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    archive_format = args.archive_format or DEFAULT_ARCHIVE_FORMAT[args.platform]
    archive_suffix = ARCHIVE_SUFFIX[archive_format]

    source_root = Path(args.source_root).resolve()
    install_dir = (
        Path(args.install_dir).resolve()
        if args.install_dir is not None
        else (source_root / ".install").resolve()
    )
    output_dir = Path(args.output_dir).resolve()

    if not install_dir.is_dir():
        print(f"error: install directory not found: {install_dir}", file=sys.stderr)
        return 1

    install_game_dir = install_dir / GAME_DIR_NAME
    if not install_game_dir.is_dir():
        print(f"error: {GAME_DIR_NAME} directory not found: {install_game_dir}", file=sys.stderr)
        return 1

    output_dir.mkdir(parents=True, exist_ok=True)

    try:
        package_suffix = normalize_package_suffix(args.package_suffix)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    package_stem = f"openq4-{args.version_tag}-{args.platform}-{args.arch}{package_suffix}"
    package_root = output_dir / package_stem
    if package_root.exists():
        shutil.rmtree(package_root)
    package_root.mkdir(parents=True, exist_ok=True)

    try:
        copy_release_collateral(source_root, package_root)
    except FileNotFoundError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    try:
        generated_docs = generate_release_documentation(
            source_root=source_root,
            package_root=package_root,
            version=args.version,
            platform=args.platform,
            arch=args.arch,
        )
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    if generated_docs.page_count <= 0 or not generated_docs.index_path.is_file():
        print("error: release HTML documentation was not generated correctly", file=sys.stderr)
        return 1

    write_version_manifest(
        package_root / "VERSION.txt",
        version=args.version,
        version_tag=args.version_tag,
        platform=args.platform,
        arch=args.arch,
    )
    missing_required = copy_required_binaries(
        args.platform,
        args.arch,
        install_dir,
        package_root,
        args.allow_missing_binaries,
    )

    package_game_dir = package_root / GAME_DIR_NAME
    package_game_dir.mkdir(parents=True, exist_ok=True)
    missing_game_modules = copy_required_game_binaries(
        args.platform,
        args.arch,
        install_game_dir,
        package_game_dir,
        args.allow_missing_binaries,
    )
    missing_symbols: list[str] = []
    if args.platform == "windows":
        missing_symbols = copy_required_windows_symbols(
            args.arch,
            install_dir,
            package_root,
            install_game_dir,
            package_game_dir,
            args.allow_missing_binaries,
        )
    missing_loose_game_files = copy_required_loose_game_files(
        install_game_dir,
        package_game_dir,
        args.allow_missing_binaries,
    )
    try:
        validate_packaged_mod_manifest(package_game_dir, args.version)
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    game_pk4_path = package_game_dir / "pak0.pk4"

    added_files, skipped_samples, missing_required_pk4_files = create_game_pk4(
        install_game_dir, game_pk4_path
    )
    if added_files == 0:
        print(
            f"error: {GAME_DIR_NAME} pk4 packaging found no eligible files after filtering",
            file=sys.stderr,
        )
        return 1
    if missing_required_pk4_files:
        print(
            f"error: {GAME_DIR_NAME} pk4 packaging is missing required runtime files:",
            file=sys.stderr,
        )
        for rel in missing_required_pk4_files:
            print(f"  - {rel}", file=sys.stderr)
        return 1

    copied_share = copy_optional_share_tree(args.platform, install_dir, package_root)
    copied_linux_launchers: list[str] = []
    if args.platform == "linux":
        copied_linux_launchers = copy_optional_linux_launchers(install_dir, package_root)
        try:
            validate_linux_package_metadata(
                package_root,
                args.arch,
                allow_missing_binaries=args.allow_missing_binaries,
            )
        except RuntimeError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 1

    macos_app_bundle = None
    if args.platform == "macos":
        macos_app_bundle = create_macos_app_bundle(
            package_root,
            install_dir,
            args.arch,
            args.version,
            args.version_tag,
        )
        try:
            validate_macos_app_bundle(package_root, macos_app_bundle, args.arch, args.version)
            validate_macos_binary_dependencies(package_root, args.arch)
        except RuntimeError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 1

    archive_executable_paths = get_package_executable_archive_paths(
        args.platform,
        args.arch,
        copied_linux_launchers,
    )

    archive_path = output_dir / f"{package_stem}{archive_suffix}"
    create_release_archive(
        package_root,
        archive_path,
        archive_format,
        archive_executable_paths,
    )
    if args.platform == "macos":
        try:
            validate_macos_archive_contents(
                package_root,
                archive_path,
                archive_format,
                args.arch,
                args.version,
            )
        except RuntimeError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 1

    print(f"Packaged openQ4 release {args.version} for {args.platform}")
    print(f"Package directory: {package_root}")
    print(f"Release archive: {archive_path}")
    print(f"Archive format: {archive_format}")
    print(f"Version manifest: {package_root / 'VERSION.txt'}")
    print(f"Documentation portal: {generated_docs.index_path} ({generated_docs.page_count} pages)")
    print(f"openQ4 pk4: {game_pk4_path} ({added_files} files)")
    if copied_share:
        print(f"Share payload: {package_root / 'share'}")
    if copied_linux_launchers:
        print("Linux launchers:")
        for filename in copied_linux_launchers:
            print(f"  - {package_root / filename}")
    if missing_symbols:
        print("Missing Windows diagnostic symbols allowed by --allow-missing-binaries:")
        for filename in missing_symbols:
            print(f"  - {filename}")
    if macos_app_bundle is not None:
        print(f"macOS app bundle: {macos_app_bundle}")
    if missing_required:
        print("Missing required runtime binaries:")
        for filename in missing_required:
            print(f"  - {filename}")
    if missing_game_modules:
        print("Missing required game modules:")
        for filename in missing_game_modules:
            print(f"  - {filename}")
    if missing_loose_game_files:
        print("Missing required loose game files:")
        for relative_path in missing_loose_game_files:
            print(f"  - {relative_path}")
    if skipped_samples:
        print("Filtered sample paths:")
        for rel in skipped_samples:
            print(f"  - {rel}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
