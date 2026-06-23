#!/usr/bin/env python3
"""Create curated release distributable archives for openQ4."""

from __future__ import annotations

import argparse
import filecmp
import json
import os
import plistlib
import re
import shutil
import stat
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
from linux_metadata import (
    LinuxMetadataError,
    desktop_entry_exec as parse_linux_desktop_entry_exec,
    desktop_exec_command as parse_linux_desktop_exec_command,
)
from generate_release_docs import GeneratedDocSite, generate_release_docs_site
from openq4_pak import (
    OPENQ4_PK4_FORBIDDEN_FILES,
    OPENQ4_PACK_NAMES,
    OPENQ4_REQUIRED_LOOSE_GAME_FILES,
    PAK0_NAME,
    copy_file_if_changed,
    copy_game_pk4,
    create_game_pk4 as create_openq4_game_pk4,
    is_relative_to,
)


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
    "macos": "dmg",
}

ARCHIVE_SUFFIX = {
    "dmg": ".dmg",
    "zip": ".zip",
    "tar.gz": ".tar.gz",
    "tar.xz": ".tar.xz",
}

MACOS_EXPECTED_PLIST_VALUES = {
    "CFBundleExecutable": "openQ4",
    "CFBundleDisplayName": "openQ4",
    "CFBundleIconFile": "openQ4.icns",
    "CFBundleIdentifier": "com.darkmatter.openq4",
    "CFBundleName": "openQ4",
    "CFBundlePackageType": "APPL",
    "LSMinimumSystemVersion": "11.0",
    "LSApplicationCategoryType": "public.app-category.games",
    "NSPrincipalClass": "NSApplication",
}
MACOS_ALLOWED_RUNTIME_DEPENDENCY_PREFIXES = (
    "/System/Library/",
    "/usr/lib/",
)
MACOS_FORBIDDEN_XATTRS = (
    "com.apple.quarantine",
)
MACOS_FORBIDDEN_ARCHIVE_NAMES = (
    ".DS_Store",
)
MACOS_PKGINFO_BYTES = b"APPL????"
MACOS_LOCALIZED_INFO_LOCALES = (
    "English",
    "French",
)

MACOS_LIPO_ARCHES = {
    "arm64": "arm64",
    "x64": "x86_64",
    "x86": "i386",
}
MACOS_SIGNING_MODES = (
    "ad-hoc",
    "developer-id",
)
MACOS_FORBIDDEN_ENTITLEMENTS = {
    "com.apple.security.app-sandbox": (
        "openQ4 direct-distribution packages are not App Sandbox-ready because "
        "they need explicit access to user-selected Quake 4 assets, saves, logs, "
        "and staged runtime overlays"
    ),
    "com.apple.security.get-task-allow": (
        "get-task-allow is a development/debug entitlement and must not be present "
        "in release signing inputs"
    ),
}


class MacOSSigningConfig:
    def __init__(
        self,
        *,
        mode: str,
        identity: str,
        hardened_runtime: bool,
        timestamp: bool,
        entitlements: Path | None,
        notarize: bool,
        notary_keychain_profile: str,
        notary_keychain: Path | None,
    ) -> None:
        self.mode = mode
        self.identity = identity
        self.hardened_runtime = hardened_runtime
        self.timestamp = timestamp
        self.entitlements = entitlements
        self.notarize = notarize
        self.notary_keychain_profile = notary_keychain_profile
        self.notary_keychain = notary_keychain


def env_flag(name: str) -> bool:
    return os.environ.get(name, "").strip().lower() in ("1", "true", "yes", "on")


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
            "windows=zip, linux=tar.xz, macos=dmg)."
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
    parser.add_argument(
        "--macos-signing-mode",
        choices=MACOS_SIGNING_MODES,
        default=os.environ.get("OPENQ4_MACOS_SIGNING_MODE", "ad-hoc"),
        help=(
            "macOS signing mode. Use ad-hoc for local/debug packaging and developer-id "
            "for release packages that will be notarized."
        ),
    )
    parser.add_argument(
        "--macos-code-sign-identity",
        default=os.environ.get("OPENQ4_MACOS_CODE_SIGN_IDENTITY", ""),
        help="Developer ID Application identity for --macos-signing-mode=developer-id.",
    )
    parser.add_argument(
        "--macos-entitlements",
        default=os.environ.get("OPENQ4_MACOS_ENTITLEMENTS", ""),
        help="Optional entitlements plist passed to codesign for macOS release signing.",
    )
    parser.add_argument(
        "--macos-notarize",
        action="store_true",
        default=env_flag("OPENQ4_MACOS_NOTARIZE"),
        help="Submit the signed macOS package payload to Apple notarization and staple the app ticket.",
    )
    parser.add_argument(
        "--macos-notary-keychain-profile",
        default=os.environ.get("OPENQ4_MACOS_NOTARY_KEYCHAIN_PROFILE", ""),
        help="notarytool keychain profile used when --macos-notarize is enabled.",
    )
    parser.add_argument(
        "--macos-notary-keychain",
        default=os.environ.get("OPENQ4_MACOS_NOTARY_KEYCHAIN", ""),
        help="Optional keychain path that contains the notarytool keychain profile.",
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


def validate_package_path_token(value: str, label: str) -> str:
    token = value.strip()
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", token):
        raise ValueError(f"{label} must be a file-name-safe token without slashes or spaces")
    return token


def resolve_package_root(output_dir: Path, package_stem: str) -> Path:
    package_root = (output_dir / package_stem).resolve()
    resolved_output_dir = output_dir.resolve()
    if package_root == resolved_output_dir or not is_relative_to(package_root, resolved_output_dir):
        raise ValueError(f"package root escapes output directory: {package_root}")
    return package_root


def copy_regular_file(source: Path, destination: Path) -> None:
    if source.is_symlink():
        raise RuntimeError(f"refusing to package symlinked file: {source}")
    if not source.is_file():
        raise FileNotFoundError(f"required package file not found: {source}")
    copy_file_if_changed(source, destination)


def copy_regular_tree(source_root: Path, destination_root: Path) -> None:
    if source_root.is_symlink():
        raise RuntimeError(f"refusing to package symlinked directory: {source_root}")
    if not source_root.is_dir():
        raise FileNotFoundError(f"required package directory not found: {source_root}")

    for path in sorted(source_root.rglob("*")):
        if path.is_symlink():
            raise RuntimeError(f"refusing to package symlink from tree: {path}")
        relative_path = path.relative_to(source_root)
        destination = destination_root / relative_path
        if path.is_dir():
            destination.mkdir(parents=True, exist_ok=True)
        elif path.is_file():
            copy_regular_file(path, destination)
        else:
            raise RuntimeError(f"refusing to package non-regular file from tree: {path}")


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


def macos_forbidden_xattrs(path: Path) -> list[str]:
    try:
        names = os.listxattr(path)
    except (AttributeError, OSError):
        return []
    return sorted(name for name in names if name in MACOS_FORBIDDEN_XATTRS)


def strip_macos_forbidden_xattrs(root: Path) -> None:
    if sys.platform != "darwin":
        return

    for path in [root, *root.rglob("*")]:
        for attr in macos_forbidden_xattrs(path):
            try:
                os.removexattr(path, attr)
            except OSError as exc:
                raise RuntimeError(f"failed to remove {attr} from macOS package path: {path}") from exc


def validate_no_macos_forbidden_xattrs(root: Path) -> None:
    offenders: list[tuple[Path, list[str]]] = []
    for path in [root, *root.rglob("*")]:
        bad_attrs = macos_forbidden_xattrs(path)
        if bad_attrs:
            offenders.append((path, bad_attrs))

    if offenders:
        joined = ", ".join(
            f"{path.relative_to(root).as_posix() or '.'}: {','.join(attrs)}"
            for path, attrs in offenders[:10]
        )
        raise RuntimeError(f"macOS package contains forbidden extended attributes: {joined}")


def validate_no_package_symlinks(root: Path) -> None:
    symlinks = [path for path in root.rglob("*") if path.is_symlink()]
    if symlinks:
        joined = ", ".join(path.relative_to(root).as_posix() for path in symlinks[:10])
        raise RuntimeError(f"macOS package contains symlink entries: {joined}")


def validate_no_package_special_files(root: Path) -> None:
    if os.name == "nt":
        return

    special_files = [
        path
        for path in root.rglob("*")
        if not path.is_symlink() and not path.is_file() and not path.is_dir()
    ]
    if special_files:
        joined = ", ".join(path.relative_to(root).as_posix() for path in special_files[:10])
        raise RuntimeError(f"macOS package contains special file entries: {joined}")


def validate_macos_package_file_modes(root: Path) -> None:
    if os.name == "nt":
        return

    offenders: list[str] = []
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        mode = path.stat().st_mode & 0o7777
        if mode & 0o7000:
            offenders.append(f"{path.relative_to(root).as_posix()} has special mode bits {mode:o}")
        elif mode & 0o022:
            offenders.append(f"{path.relative_to(root).as_posix()} is group/other writable ({mode:o})")

    if offenders:
        raise RuntimeError("macOS package contains unsafe file modes: " + ", ".join(offenders[:10]))


def parse_version_manifest_bytes(data: bytes, label: str) -> dict[str, str]:
    try:
        lines = data.decode("utf-8").splitlines()
    except UnicodeDecodeError as exc:
        raise RuntimeError(f"{label} version manifest is not UTF-8") from exc

    if not lines or lines[0].strip() != PRODUCT_NAME:
        raise RuntimeError(f"{label} version manifest has invalid product header")

    values: dict[str, str] = {}
    for line in lines[1:]:
        if not line.strip():
            continue
        if "=" not in line:
            raise RuntimeError(f"{label} version manifest contains malformed line: {line!r}")
        key, value = line.split("=", 1)
        key = key.strip()
        if key == "":
            raise RuntimeError(f"{label} version manifest contains an empty key")
        if key in values:
            raise RuntimeError(f"{label} version manifest contains duplicate key: {key}")
        values[key] = value.strip()
    return values


def validate_version_manifest_bytes(
    data: bytes,
    label: str,
    *,
    version: str,
    version_tag: str,
    platform: str,
    arch: str,
) -> None:
    values = parse_version_manifest_bytes(data, label)
    expected = {
        "version": version,
        "version_tag": version_tag,
        "platform": platform,
        "arch": arch,
    }
    for key, expected_value in expected.items():
        actual = values.get(key)
        if actual != expected_value:
            raise RuntimeError(
                f"{label} version manifest {key} is {actual!r}; expected {expected_value!r}"
            )


def validate_macos_localized_info_bytes(data: bytes, label: str, version: str) -> None:
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise RuntimeError(f"{label} is not UTF-8") from exc

    required_tokens = (
        'CFBundleName = "openQ4";',
        f'CFBundleShortVersionString = "{version}";',
        f'CFBundleGetInfoString = "openQ4 version {version}, Copyright 2026 DarkMatter Productions";',
        'NSHumanReadableCopyright = "Copyright 2026 DarkMatter Productions";',
    )
    for token in required_tokens:
        if token not in text:
            raise RuntimeError(f"{label} is missing {token!r}")


def macos_package_version_tag_from_name(package_root: Path, arch: str) -> str:
    name = package_root.name
    prefix = "openq4-"
    marker = f"-macos-{arch}"
    if not name.startswith(prefix) or marker not in name:
        raise RuntimeError(f"macOS package directory name cannot provide version tag: {name}")
    return name[len(prefix) : name.index(marker)]


def validate_macos_version_manifests(package_root: Path, arch: str, version: str, version_tag: str) -> None:
    manifests = {
        "macOS package": package_root / "VERSION.txt",
        "macOS app": package_root / "openQ4.app" / "Contents" / "Resources" / "VERSION.txt",
    }
    for label, manifest_path in manifests.items():
        try:
            data = manifest_path.read_bytes()
        except OSError as exc:
            raise RuntimeError(f"{label} version manifest is unreadable: {manifest_path}") from exc
        validate_version_manifest_bytes(
            data,
            label,
            version=version,
            version_tag=version_tag,
            platform="macos",
            arch=arch,
        )


def validate_macos_localized_info_files(package_root: Path, version: str) -> None:
    for locale in MACOS_LOCALIZED_INFO_LOCALES:
        path = package_root / "openQ4.app" / "Contents" / "Resources" / f"{locale}.lproj" / "InfoPlist.strings"
        try:
            data = path.read_bytes()
        except OSError as exc:
            raise RuntimeError(f"macOS localized InfoPlist.strings is unreadable: {path}") from exc
        validate_macos_localized_info_bytes(data, f"macOS {locale} InfoPlist.strings", version)


def macos_expected_lipo_arch(arch: str) -> str:
    expected = MACOS_LIPO_ARCHES.get(arch)
    if expected is None:
        raise RuntimeError(f"macOS package architecture {arch!r} has no lipo mapping")
    return expected


def macos_lipo_arches(binary_path: Path) -> set[str]:
    if sys.platform != "darwin":
        return set()

    lipo_path = shutil.which("lipo")
    if lipo_path is None:
        raise RuntimeError("macOS architecture validation requires lipo")

    completed = subprocess.run(
        [lipo_path, "-archs", str(binary_path)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode != 0:
        message = completed.stderr.strip() or completed.stdout.strip()
        raise RuntimeError(f"lipo failed for macOS binary {binary_path}: {message}")

    return set(completed.stdout.strip().split())


def validate_macos_binary_architectures(binary_paths: list[Path], arch: str) -> None:
    if sys.platform != "darwin":
        return

    expected_arch = macos_expected_lipo_arch(arch)
    for binary_path in binary_paths:
        actual_arches = macos_lipo_arches(binary_path)
        if expected_arch not in actual_arches:
            actual = ", ".join(sorted(actual_arches)) or "<none>"
            raise RuntimeError(
                f"macOS binary architecture mismatch: {binary_path}: "
                f"expected {expected_arch}, found {actual}"
            )


def run_macos_command(command: list[str], *, label: str) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(
        command,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode != 0:
        message = completed.stderr.strip() or completed.stdout.strip()
        raise RuntimeError(f"{label} failed: {message}")
    return completed


def run_macos_codesign(command: list[str], *, label: str) -> subprocess.CompletedProcess[str]:
    return run_macos_command(command, label=label)


def validate_macos_entitlements_file(entitlements: Path) -> None:
    try:
        with entitlements.open("rb") as handle:
            entitlement_values = plistlib.load(handle)
    except (plistlib.InvalidFileException, TypeError, ValueError) as exc:
        raise RuntimeError(f"macOS entitlements file is not a valid plist: {entitlements}") from exc
    except OSError as exc:
        raise RuntimeError(f"macOS entitlements file is unreadable: {entitlements}") from exc

    if not isinstance(entitlement_values, dict):
        raise RuntimeError(f"macOS entitlements file must contain a dictionary root: {entitlements}")

    forbidden_keys = []
    for key, reason in MACOS_FORBIDDEN_ENTITLEMENTS.items():
        if entitlement_values.get(key):
            forbidden_keys.append(f"{key} ({reason})")

    if forbidden_keys:
        joined = "; ".join(forbidden_keys)
        raise RuntimeError(f"macOS entitlements file contains unsupported release entitlements: {joined}")


def resolve_macos_signing_config(args: argparse.Namespace) -> MacOSSigningConfig:
    entitlements = Path(args.macos_entitlements).resolve() if args.macos_entitlements else None
    if entitlements is not None and not entitlements.is_file():
        raise RuntimeError(f"macOS entitlements file does not exist: {entitlements}")
    if entitlements is not None:
        validate_macos_entitlements_file(entitlements)

    if args.macos_signing_mode == "ad-hoc":
        if args.macos_notarize:
            raise RuntimeError("macOS notarization requires --macos-signing-mode=developer-id")
        return MacOSSigningConfig(
            mode="ad-hoc",
            identity="-",
            hardened_runtime=False,
            timestamp=False,
            entitlements=entitlements,
            notarize=False,
            notary_keychain_profile="",
            notary_keychain=None,
        )

    identity = args.macos_code_sign_identity.strip()
    if identity == "":
        raise RuntimeError("macOS Developer ID signing requires --macos-code-sign-identity")

    notary_keychain_profile = args.macos_notary_keychain_profile.strip()
    if args.macos_notarize and notary_keychain_profile == "":
        raise RuntimeError("macOS notarization requires --macos-notary-keychain-profile")

    return MacOSSigningConfig(
        mode="developer-id",
        identity=identity,
        hardened_runtime=True,
        timestamp=True,
        entitlements=entitlements,
        notarize=args.macos_notarize,
        notary_keychain_profile=notary_keychain_profile,
        notary_keychain=Path(args.macos_notary_keychain).resolve() if args.macos_notary_keychain else None,
    )


def macos_signable_targets(package_root: Path, arch: str) -> list[Path]:
    return [
        package_root / f"{PRODUCT_NAME}-client_{arch}",
        package_root / f"{PRODUCT_NAME}-ded_{arch}",
        package_root / GAME_DIR_NAME / f"game-sp_{arch}.dylib",
        package_root / GAME_DIR_NAME / f"game-mp_{arch}.dylib",
    ]


def macos_codesign_target(codesign_path: str, target: Path, config: MacOSSigningConfig) -> None:
    command = [
        codesign_path,
        "--force",
        "--sign",
        config.identity,
    ]
    if config.hardened_runtime:
        command += ["--options", "runtime"]
    if config.timestamp:
        command.append("--timestamp")
    else:
        command.append("--timestamp=none")
    if config.entitlements is not None:
        command += ["--entitlements", str(config.entitlements)]
    command.append(str(target))

    run_macos_codesign(command, label=f"{config.mode} signing {target}")


def sign_macos_payload(package_root: Path, arch: str, config: MacOSSigningConfig) -> None:
    if sys.platform != "darwin":
        if config.mode == "developer-id" or config.notarize:
            raise RuntimeError("macOS Developer ID signing and notarization require a macOS host")
        return

    codesign_path = shutil.which("codesign")
    if codesign_path is None:
        raise RuntimeError("macOS code-sign validation requires codesign")

    app_root = package_root / "openQ4.app"
    client_binary = package_root / f"{PRODUCT_NAME}-client_{arch}"
    app_executable = app_root / "Contents" / "MacOS" / "openQ4"

    for target in macos_signable_targets(package_root, arch):
        # The app bundle signature can rewrite the copied executable; copy that
        # final signed executable back to the loose client below so both match.
        if target == client_binary:
            continue
        macos_codesign_target(codesign_path, target, config)

    shutil.copy2(client_binary, app_executable)
    ensure_posix_executable(app_executable)
    macos_codesign_target(codesign_path, app_root, config)
    shutil.copy2(app_executable, client_binary)
    ensure_posix_executable(client_binary)


def ad_hoc_sign_macos_payload(package_root: Path, arch: str) -> None:
    sign_macos_payload(
        package_root,
        arch,
        MacOSSigningConfig(
            mode="ad-hoc",
            identity="-",
            hardened_runtime=False,
            timestamp=False,
            entitlements=None,
            notarize=False,
            notary_keychain_profile="",
            notary_keychain=None,
        ),
    )


def verify_macos_codesignature(package_root: Path, arch: str) -> None:
    if sys.platform != "darwin":
        return

    codesign_path = shutil.which("codesign")
    if codesign_path is None:
        raise RuntimeError("macOS code-sign validation requires codesign")

    verify_targets = [
        *macos_signable_targets(package_root, arch),
        package_root / "openQ4.app" / "Contents" / "MacOS" / "openQ4",
        package_root / "openQ4.app",
    ]
    for target in verify_targets:
        run_macos_codesign(
            [codesign_path, "--verify", "--strict", "--verbose=2", str(target)],
            label=f"code signature verification for {target}",
        )


def macos_codesign_details(target: Path) -> str:
    if sys.platform != "darwin":
        return ""

    codesign_path = shutil.which("codesign")
    if codesign_path is None:
        raise RuntimeError("macOS code-sign validation requires codesign")

    completed = run_macos_codesign(
        [codesign_path, "-dv", "--verbose=4", str(target)],
        label=f"code signature detail inspection for {target}",
    )
    return completed.stdout + completed.stderr


def verify_macos_developer_id_signature(package_root: Path, arch: str, config: MacOSSigningConfig) -> None:
    if sys.platform != "darwin" or config.mode != "developer-id":
        return

    verify_targets = [
        *macos_signable_targets(package_root, arch),
        package_root / "openQ4.app" / "Contents" / "MacOS" / "openQ4",
        package_root / "openQ4.app",
    ]
    for target in verify_targets:
        details = macos_codesign_details(target)
        if "Authority=Developer ID Application:" not in details:
            raise RuntimeError(f"macOS target is not Developer ID Application signed: {target}")
        if "Signature=adhoc" in details:
            raise RuntimeError(f"macOS target is still ad-hoc signed: {target}")
        if config.hardened_runtime and "Runtime Version=" not in details:
            raise RuntimeError(f"macOS target is missing Hardened Runtime metadata: {target}")


def notarize_macos_app_bundle(package_root: Path, config: MacOSSigningConfig) -> None:
    if sys.platform != "darwin" or not config.notarize:
        return

    for tool_name in ("ditto", "xcrun", "spctl"):
        if shutil.which(tool_name) is None:
            raise RuntimeError(f"macOS notarization requires {tool_name}")

    app_root = package_root / "openQ4.app"
    notary_archive = package_root.parent / f"{package_root.name}-notary.zip"
    if notary_archive.exists():
        notary_archive.unlink()

    run_macos_command(
        ["ditto", "-c", "-k", "--keepParent", str(package_root), str(notary_archive)],
        label=f"creating notarization archive {notary_archive}",
    )

    notary_command = [
        "xcrun",
        "notarytool",
        "submit",
        str(notary_archive),
        "--keychain-profile",
        config.notary_keychain_profile,
        "--wait",
    ]
    if config.notary_keychain is not None:
        notary_command += ["--keychain", str(config.notary_keychain)]
    try:
        run_macos_command(notary_command, label=f"notarizing {package_root}")
    finally:
        try:
            notary_archive.unlink()
        except OSError:
            pass

    run_macos_command(["xcrun", "stapler", "staple", str(app_root)], label=f"stapling {app_root}")
    run_macos_command(["xcrun", "stapler", "validate", str(app_root)], label=f"validating stapled ticket for {app_root}")
    run_macos_command(
        ["spctl", "--assess", "--type", "execute", "--verbose=4", str(app_root)],
        label=f"Gatekeeper assessment for {app_root}",
    )


def validate_macos_dmg_image(dmg_path: Path) -> None:
    if sys.platform != "darwin":
        raise RuntimeError("macOS DMG validation requires a macOS host")

    hdiutil_path = shutil.which("hdiutil")
    if hdiutil_path is None:
        raise RuntimeError("macOS DMG validation requires hdiutil")

    run_macos_command([hdiutil_path, "verify", str(dmg_path)], label=f"verifying macOS DMG {dmg_path}")
    run_macos_command([hdiutil_path, "imageinfo", str(dmg_path)], label=f"reading macOS DMG info for {dmg_path}")


def notarize_macos_dmg_image(dmg_path: Path, config: MacOSSigningConfig) -> None:
    if sys.platform != "darwin" or not config.notarize:
        return

    xcrun_path = shutil.which("xcrun")
    if xcrun_path is None:
        raise RuntimeError("macOS DMG notarization requires xcrun")

    notary_command = [
        xcrun_path,
        "notarytool",
        "submit",
        str(dmg_path),
        "--keychain-profile",
        config.notary_keychain_profile,
        "--wait",
    ]
    if config.notary_keychain is not None:
        notary_command += ["--keychain", str(config.notary_keychain)]

    run_macos_command(notary_command, label=f"notarizing macOS DMG {dmg_path}")
    run_macos_command([xcrun_path, "stapler", "staple", str(dmg_path)], label=f"stapling macOS DMG {dmg_path}")
    run_macos_command(
        [xcrun_path, "stapler", "validate", str(dmg_path)],
        label=f"validating stapled macOS DMG ticket for {dmg_path}",
    )


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
        copy_regular_file(source, destination)


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
        copy_regular_file(source, destination)
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
        copy_regular_file(source, package_root / source.name)

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
        destination = package_game_dir / filename
        copy_regular_file(source, destination)
        if platform == "macos":
            ensure_posix_executable(destination)

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
        copy_regular_file(source, package_root / filename)

    for filename in get_required_windows_game_symbols(arch):
        source = install_game_dir / filename
        if not source.is_file():
            if allow_missing_binaries:
                missing_required.append(f"{GAME_DIR_NAME}/{filename}")
                continue
            raise FileNotFoundError(f"required Windows game diagnostic symbol not found: {source}")
        copy_regular_file(source, package_game_dir / filename)

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
        copy_regular_file(source, destination)

    return missing_required


def create_game_pk4(
    install_game_dir: Path, destination_pk4: Path, pak_name: str = PAK0_NAME
) -> tuple[int, list[str], list[str]]:
    result = create_openq4_game_pk4(install_game_dir, destination_pk4, pak_name=pak_name)
    return result.added_files, result.skipped_samples, result.missing_required


def create_macos_dmg(package_root: Path, archive_path: Path) -> None:
    if sys.platform != "darwin":
        raise RuntimeError("macOS DMG packaging requires a macOS host")

    hdiutil_path = shutil.which("hdiutil")
    if hdiutil_path is None:
        raise RuntimeError("macOS DMG packaging requires hdiutil")

    run_macos_command(
        [
            hdiutil_path,
            "create",
            "-volname",
            package_root.name,
            "-srcfolder",
            str(package_root),
            "-format",
            "UDZO",
            "-ov",
            str(archive_path),
        ],
        label=f"creating macOS DMG {archive_path}",
    )


def create_release_archive(
    package_root: Path,
    archive_path: Path,
    archive_format: str,
    executable_relative_paths: set[Path] | None = None,
) -> None:
    if archive_path.exists() or archive_path.is_symlink():
        archive_path.unlink()

    symlinks = [path for path in package_root.rglob("*") if path.is_symlink()]
    if symlinks:
        joined = ", ".join(path.relative_to(package_root).as_posix() for path in symlinks[:10])
        raise RuntimeError(f"release archive input contains symlink entries: {joined}")

    executable_archive_paths = {
        relative_path.as_posix()
        for relative_path in (executable_relative_paths or set())
    }

    if archive_format == "dmg":
        create_macos_dmg(package_root, archive_path)
        return

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
                with path.open("rb") as source, archive.open(info, "w") as target:
                    shutil.copyfileobj(source, target, length=1024 * 1024)
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
        executable_paths.update(
            {
                Path(GAME_DIR_NAME) / f"game-sp_{arch}.dylib",
                Path(GAME_DIR_NAME) / f"game-mp_{arch}.dylib",
            }
        )

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


def validate_macos_archive_name(name: str, package_prefix: str) -> None:
    if (
        name.startswith("/")
        or "\\" in name
        or re.match(r"^[A-Za-z]:", name) is not None
        or "/../" in f"/{name}/"
        or not name.startswith(package_prefix)
    ):
        raise RuntimeError(f"macOS archive contains unsafe or out-of-package path: {name}")


def validate_macos_archive_mode(name: str, mode: int) -> None:
    if mode & 0o7000:
        raise RuntimeError(f"macOS archive entry has special mode bits: {name} ({mode:o})")
    if mode & 0o022:
        raise RuntimeError(f"macOS archive entry is group/other writable: {name} ({mode:o})")


def record_macos_archive_entry(entry_names: set[str], name: str, package_prefix: str) -> None:
    validate_macos_archive_name(name, package_prefix)
    if name in entry_names:
        raise RuntimeError(f"macOS archive contains duplicate entry: {name}")
    entry_names.add(name)


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
        f"{package_prefix}{GAME_DIR_NAME}/game-sp_{arch}.dylib",
        f"{package_prefix}{GAME_DIR_NAME}/game-mp_{arch}.dylib",
        f"{package_prefix}{GAME_DIR_NAME}/mod.json",
        f"{package_prefix}{GAME_DIR_NAME}/pak0.pk4",
        f"{package_prefix}{GAME_DIR_NAME}/pak1.pk4",
        f"{package_prefix}VERSION.txt",
        f"{package_prefix}openQ4.app/Contents/Info.plist",
        f"{package_prefix}openQ4.app/Contents/PkgInfo",
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
        f"{package_prefix}{GAME_DIR_NAME}/game-sp_{arch}.dylib",
        f"{package_prefix}{GAME_DIR_NAME}/game-mp_{arch}.dylib",
    }
    plist_entry = f"{package_prefix}openQ4.app/Contents/Info.plist"

    modes: dict[str, int] = {}
    entry_names: set[str] = set()
    plist_bytes: bytes | None = None
    pkginfo_bytes: bytes | None = None
    root_version_bytes: bytes | None = None
    app_version_bytes: bytes | None = None
    localized_info_bytes: dict[str, bytes] = {}
    client_bytes: bytes | None = None
    app_executable_bytes: bytes | None = None
    package_version_tag = macos_package_version_tag_from_name(package_root, arch)

    if archive_format == "zip":
        with ZipFile(archive_path, "r") as archive:
            for info in archive.infolist():
                name = info.filename.rstrip("/")
                if not name:
                    continue
                mode_type = (info.external_attr >> 16) & 0o170000
                if info.create_system == 3 and mode_type == stat.S_IFLNK:
                    raise RuntimeError(f"macOS archive contains symlink entry: {name}")
                if info.create_system == 3 and mode_type not in (0, stat.S_IFREG):
                    raise RuntimeError(f"macOS archive contains non-regular entry: {name}")
                record_macos_archive_entry(entry_names, name, package_prefix)
                modes[name] = (info.external_attr >> 16) & 0o7777
                validate_macos_archive_mode(name, modes[name])
            if plist_entry in entry_names:
                plist_bytes = archive.read(plist_entry)
            pkginfo_entry = f"{package_prefix}openQ4.app/Contents/PkgInfo"
            if pkginfo_entry in entry_names:
                pkginfo_bytes = archive.read(pkginfo_entry)
            root_version_entry = f"{package_prefix}VERSION.txt"
            app_version_entry = f"{package_prefix}openQ4.app/Contents/Resources/VERSION.txt"
            if root_version_entry in entry_names:
                root_version_bytes = archive.read(root_version_entry)
            if app_version_entry in entry_names:
                app_version_bytes = archive.read(app_version_entry)
            for locale in MACOS_LOCALIZED_INFO_LOCALES:
                entry = f"{package_prefix}openQ4.app/Contents/Resources/{locale}.lproj/InfoPlist.strings"
                if entry in entry_names:
                    localized_info_bytes[locale] = archive.read(entry)
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
                if member.issym() or member.islnk():
                    raise RuntimeError(f"macOS archive contains symlink entry: {name}")
                if not member.isfile():
                    raise RuntimeError(f"macOS archive contains non-regular entry: {name}")
                record_macos_archive_entry(entry_names, name, package_prefix)
                modes[name] = member.mode & 0o7777
                validate_macos_archive_mode(name, modes[name])
                if name == plist_entry:
                    extracted = archive.extractfile(member)
                    if extracted is not None:
                        plist_bytes = extracted.read()
                elif name == f"{package_prefix}openQ4.app/Contents/PkgInfo":
                    extracted = archive.extractfile(member)
                    if extracted is not None:
                        pkginfo_bytes = extracted.read()
                elif name == f"{package_prefix}VERSION.txt":
                    extracted = archive.extractfile(member)
                    if extracted is not None:
                        root_version_bytes = extracted.read()
                elif name == f"{package_prefix}openQ4.app/Contents/Resources/VERSION.txt":
                    extracted = archive.extractfile(member)
                    if extracted is not None:
                        app_version_bytes = extracted.read()
                elif name.startswith(f"{package_prefix}openQ4.app/Contents/Resources/") and name.endswith(".lproj/InfoPlist.strings"):
                    locale = Path(name).parts[-2].replace(".lproj", "")
                    extracted = archive.extractfile(member)
                    if extracted is not None:
                        localized_info_bytes[locale] = extracted.read()
                elif name == client_entry:
                    extracted = archive.extractfile(member)
                    if extracted is not None:
                        client_bytes = extracted.read()
                elif name == app_executable_entry:
                    extracted = archive.extractfile(member)
                    if extracted is not None:
                        app_executable_bytes = extracted.read()

    bad_metadata_names = [
        name
        for name in sorted(entry_names)
        if any(
            part in MACOS_FORBIDDEN_ARCHIVE_NAMES
            or part.startswith("._")
            or part.endswith(".dSYM")
            for part in Path(name).parts
        )
    ]
    if bad_metadata_names:
        joined = ", ".join(bad_metadata_names[:5])
        raise RuntimeError(f"macOS archive contains non-runtime metadata/debug entries: {joined}")

    missing_entries = sorted(expected_entries - entry_names)
    if missing_entries:
        joined = ", ".join(missing_entries)
        raise RuntimeError(f"macOS archive is missing required entries: {joined}")

    expected_game_modules = {
        f"{package_prefix}{GAME_DIR_NAME}/game-sp_{arch}.dylib",
        f"{package_prefix}{GAME_DIR_NAME}/game-mp_{arch}.dylib",
    }
    stale_game_modules = sorted(
        name
        for name in entry_names
        if name.startswith(f"{package_prefix}{GAME_DIR_NAME}/game-")
        and name.endswith(".dylib")
        and name not in expected_game_modules
    )
    if stale_game_modules:
        joined = ", ".join(stale_game_modules[:5])
        raise RuntimeError(f"macOS archive contains stale or mismatched game modules: {joined}")

    for entry in sorted(executable_entries):
        if modes.get(entry, 0) & 0o111 == 0:
            raise RuntimeError(f"macOS archive entry is not executable: {entry}")

    if plist_bytes is None:
        raise RuntimeError(f"macOS archive Info.plist is unreadable: {plist_entry}")
    if pkginfo_bytes != MACOS_PKGINFO_BYTES:
        raise RuntimeError("macOS archive PkgInfo is missing or invalid")
    if root_version_bytes is None or app_version_bytes is None:
        raise RuntimeError("macOS archive version manifests are unreadable")
    validate_version_manifest_bytes(
        root_version_bytes,
        "macOS archive package",
        version=version,
        version_tag=package_version_tag,
        platform="macos",
        arch=arch,
    )
    validate_version_manifest_bytes(
        app_version_bytes,
        "macOS archive app",
        version=version,
        version_tag=package_version_tag,
        platform="macos",
        arch=arch,
    )
    for locale in MACOS_LOCALIZED_INFO_LOCALES:
        data = localized_info_bytes.get(locale)
        if data is None:
            raise RuntimeError(f"macOS archive missing {locale} localized InfoPlist.strings")
        validate_macos_localized_info_bytes(data, f"macOS archive {locale} InfoPlist.strings", version)
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


def macos_otool_install_name(binary_path: Path) -> str:
    if sys.platform != "darwin" or binary_path.suffix != ".dylib":
        return ""

    otool_path = shutil.which("otool")
    if otool_path is None:
        raise RuntimeError("macOS install-name validation requires otool")

    completed = subprocess.run(
        [otool_path, "-D", str(binary_path)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode != 0:
        message = completed.stderr.strip() or completed.stdout.strip()
        raise RuntimeError(f"otool failed for macOS dylib {binary_path}: {message}")

    lines = [line.strip() for line in completed.stdout.splitlines() if line.strip()]
    return lines[1] if len(lines) > 1 else ""


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

    validate_macos_binary_architectures(binary_paths, arch)

    for binary_path in binary_paths:
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

    expected_install_names = {
        package_root / GAME_DIR_NAME / f"game-sp_{arch}.dylib": f"@loader_path/game-sp_{arch}.dylib",
        package_root / GAME_DIR_NAME / f"game-mp_{arch}.dylib": f"@loader_path/game-mp_{arch}.dylib",
    }
    for binary_path, expected_install_name in expected_install_names.items():
        actual_install_name = macos_otool_install_name(binary_path)
        if actual_install_name != expected_install_name:
            raise RuntimeError(
                "macOS game module install name is not package-relative: "
                f"{binary_path}: {actual_install_name!r}; expected {expected_install_name!r}"
            )


def copy_optional_share_tree(platform: str, install_dir: Path, package_root: Path) -> bool:
    if platform != "linux":
        return False

    share_source = install_dir / "share"
    if not share_source.is_dir():
        return False

    share_dest = package_root / "share"
    copy_regular_tree(share_source, share_dest)
    return True


def copy_optional_linux_launchers(install_dir: Path, package_root: Path) -> list[str]:
    copied: list[str] = []

    for filename in ("openQ4-steamdeck",):
        source = install_dir / filename
        if not source.is_file():
            continue

        destination = package_root / filename
        copy_regular_file(source, destination)
        os.chmod(destination, 0o755)
        copied.append(filename)

    return copied


def desktop_entry_exec(path: Path) -> str:
    try:
        return parse_linux_desktop_entry_exec(path)
    except LinuxMetadataError as exc:
        raise RuntimeError(str(exc)) from exc


def desktop_exec_command(exec_line: str) -> str:
    try:
        return parse_linux_desktop_exec_command(exec_line)
    except LinuxMetadataError as exc:
        raise ValueError(str(exc)) from exc


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

        try:
            exec_command = desktop_exec_command(desktop_entry_exec(desktop_path))
        except ValueError as exc:
            raise RuntimeError(f"Linux desktop entry {desktop_path} has an invalid Exec line: {exc}") from exc
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
    app_pkginfo = app_contents / "PkgInfo"

    if not app_plist.is_file():
        raise RuntimeError(f"macOS app bundle is missing Info.plist: {app_plist}")
    if not app_executable.is_file() or not os.access(app_executable, os.X_OK):
        raise RuntimeError(f"macOS app executable is missing or not executable: {app_executable}")
    if not app_icon.is_file():
        raise RuntimeError(f"macOS app bundle is missing its icon: {app_icon}")
    if not app_version.is_file():
        raise RuntimeError(f"macOS app bundle is missing its version manifest: {app_version}")
    if not app_pkginfo.is_file() or app_pkginfo.read_bytes() != MACOS_PKGINFO_BYTES:
        raise RuntimeError(f"macOS app bundle is missing a valid PkgInfo file: {app_pkginfo}")

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
    copy_regular_file(client_binary, app_executable)
    os.chmod(app_executable, 0o755)

    icns_candidates = [
        install_dir / "openQ4.icns",
        install_dir / "quake4.icns",
    ]
    for icns_source in icns_candidates:
        if icns_source.is_file():
            copy_regular_file(icns_source, app_resources / "openQ4.icns")
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
            "<key>CFBundleDisplayName</key>",
            "<string>openQ4</string>",
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
            "<key>LSApplicationCategoryType</key>",
            "<string>public.app-category.games</string>",
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
    (app_contents / "PkgInfo").write_bytes(MACOS_PKGINFO_BYTES)
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
    if archive_format == "dmg" and args.platform != "macos":
        print("error: archive format 'dmg' is only supported for macOS packages", file=sys.stderr)
        return 1

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
        version_tag = validate_package_path_token(args.version_tag, "version tag")
        package_suffix = normalize_package_suffix(args.package_suffix)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    package_stem = f"openq4-{version_tag}-{args.platform}-{args.arch}{package_suffix}"
    try:
        package_root = resolve_package_root(output_dir, package_stem)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
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
        version_tag=version_tag,
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

    source_content_dir = source_root / "content" / GAME_DIR_NAME
    pk4_results = []
    for pak_name in OPENQ4_PACK_NAMES:
        game_pk4_path = package_game_dir / pak_name
        staged_game_pk4_path = install_game_dir / pak_name
        source_pack_dir = source_content_dir / Path(pak_name).stem

        if staged_game_pk4_path.is_file():
            pk4_result = copy_game_pk4(staged_game_pk4_path, game_pk4_path, pak_name=pak_name)
        elif source_pack_dir.is_dir():
            pk4_result = create_openq4_game_pk4(source_pack_dir, game_pk4_path, pak_name=pak_name)
        else:
            print(
                f"error: {GAME_DIR_NAME}/{pak_name} was not staged and source pack directory is missing: {source_pack_dir}",
                file=sys.stderr,
            )
            return 1

        if pk4_result.added_files == 0:
            print(
                f"error: {GAME_DIR_NAME}/{pak_name} packaging found no eligible files after filtering",
                file=sys.stderr,
            )
            return 1
        if pk4_result.missing_required:
            print(
                f"error: {GAME_DIR_NAME}/{pak_name} packaging is missing required runtime files:",
                file=sys.stderr,
            )
            for rel in pk4_result.missing_required:
                print(f"  - {rel}", file=sys.stderr)
            return 1

        pk4_results.append((pak_name, pk4_result))

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
            version_tag,
        )
        try:
            validate_no_package_symlinks(package_root)
            validate_no_package_special_files(package_root)
            validate_macos_package_file_modes(package_root)
            strip_macos_forbidden_xattrs(package_root)
            validate_no_macos_forbidden_xattrs(package_root)
            macos_signing = resolve_macos_signing_config(args)
            sign_macos_payload(package_root, args.arch, macos_signing)
            validate_macos_version_manifests(package_root, args.arch, args.version, version_tag)
            validate_macos_localized_info_files(package_root, args.version)
            validate_macos_app_bundle(package_root, macos_app_bundle, args.arch, args.version)
            validate_macos_binary_dependencies(package_root, args.arch)
            verify_macos_codesignature(package_root, args.arch)
            verify_macos_developer_id_signature(package_root, args.arch, macos_signing)
            notarize_macos_app_bundle(package_root, macos_signing)
        except RuntimeError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 1

    archive_executable_paths = get_package_executable_archive_paths(
        args.platform,
        args.arch,
        copied_linux_launchers,
    )

    archive_path = output_dir / f"{package_stem}{archive_suffix}"
    try:
        create_release_archive(
            package_root,
            archive_path,
            archive_format,
            archive_executable_paths,
        )
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    if args.platform == "macos":
        try:
            if archive_format == "dmg":
                validate_macos_dmg_image(archive_path)
                notarize_macos_dmg_image(archive_path, macos_signing)
                validate_macos_dmg_image(archive_path)
            else:
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
    print("openQ4 pk4s:")
    for pak_name, pk4_result in pk4_results:
        print(
            f"  - {package_game_dir / pak_name} "
            f"({pk4_result.added_files} files, md5 {pk4_result.md5_hex})"
        )
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
    skipped_samples = [
        f"{pak_name}:{sample}"
        for pak_name, pk4_result in pk4_results
        for sample in pk4_result.skipped_samples
    ]
    if skipped_samples:
        print("Filtered sample paths:")
        for rel in skipped_samples:
            print(f"  - {rel}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
