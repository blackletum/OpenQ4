#!/usr/bin/env python3
"""Regression checks for the macOS Metal bridge build contract."""

import importlib.util
import io
import os
import plistlib
import shutil
import tarfile
from pathlib import Path
from types import SimpleNamespace
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def require_before(haystack: str, first: str, second: str, context: str) -> None:
    first_index = haystack.find(first)
    second_index = haystack.find(second)
    if first_index == -1 or second_index == -1 or first_index >= second_index:
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


def expect_runtime_error(fragment: str, callback, context: str) -> None:
    try:
        callback()
    except RuntimeError as exc:
        if fragment not in str(exc):
            raise AssertionError(f"Unexpected error for {context}: {exc}") from exc
        return
    raise AssertionError(f"Expected RuntimeError containing {fragment!r} for {context}")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start == -1:
        raise AssertionError(f"Missing function signature {signature!r}")

    depth = 0
    for index in range(start, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]

    raise AssertionError(f"Could not find end of function {signature!r}")


def load_package_module():
    package_path = ROOT / "tools" / "build" / "package_nightly.py"
    spec = importlib.util.spec_from_file_location("package_nightly_for_macos_test", package_path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"Unable to import package helper: {package_path}")

    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_validation_module():
    validation_path = ROOT / "tools" / "validation" / "openq4_validate.py"
    spec = importlib.util.spec_from_file_location("openq4_validate_for_macos_test", validation_path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"Unable to import validation helper: {validation_path}")

    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def make_macos_plist_bytes(package, version: str) -> bytes:
    plist = dict(package.MACOS_EXPECTED_PLIST_VALUES)
    plist.update(
        {
            "CFBundleShortVersionString": version,
            "CFBundleVersion": version,
            "NSHighResolutionCapable": True,
            "NSSupportsAutomaticGraphicsSwitching": True,
        }
    )
    return plistlib.dumps(plist)


def make_version_manifest_bytes(version: str, version_tag: str, platform: str, arch: str) -> bytes:
    return (
        "openQ4\n"
        f"version={version}\n"
        f"version_tag={version_tag}\n"
        f"platform={platform}\n"
        f"arch={arch}\n"
    ).encode("utf-8")


def make_macos_localized_info_bytes(version: str) -> bytes:
    return (
        "/* Localized versions of Info.plist keys */\n\n"
        'CFBundleName = "openQ4";\n'
        f'CFBundleShortVersionString = "{version}";\n'
        f'CFBundleGetInfoString = "openQ4 version {version}, Copyright 2026 DarkMatter Productions";\n'
        'NSHumanReadableCopyright = "Copyright 2026 DarkMatter Productions";\n'
    ).encode("utf-8")


def make_macos_archive_entries(
    package,
    package_name: str,
    arch: str,
    plist_bytes: bytes,
    *,
    client_mode: int = 0o755,
    dedicated_mode: int = 0o755,
    app_exec_mode: int = 0o755,
    extra_entries: dict[str, tuple[bytes, int]] | None = None,
) -> dict[str, tuple[bytes, int]]:
    prefix = package_name + "/"
    client_bytes = b"client-binary\n"
    version = "0.2.000"
    version_tag = "v0.2.000"
    localized_info = make_macos_localized_info_bytes(version)
    entries = {
        f"{prefix}VERSION.txt": (make_version_manifest_bytes(version, version_tag, "macos", arch), 0o644),
        f"{prefix}openQ4-client_{arch}": (client_bytes, client_mode),
        f"{prefix}openQ4-ded_{arch}": (b"dedicated-binary\n", dedicated_mode),
        f"{prefix}{package.GAME_DIR_NAME}/game-sp_{arch}.dylib": (b"sp-module\n", 0o755),
        f"{prefix}{package.GAME_DIR_NAME}/game-mp_{arch}.dylib": (b"mp-module\n", 0o755),
        f"{prefix}{package.GAME_DIR_NAME}/mod.json": (b'{"version":"0.2.000"}\n', 0o644),
        f"{prefix}{package.GAME_DIR_NAME}/pak0.pk4": (b"pk4\n", 0o644),
        f"{prefix}{package.GAME_DIR_NAME}/pak1.pk4": (b"pk4\n", 0o644),
        f"{prefix}openQ4.app/Contents/Info.plist": (plist_bytes, 0o644),
        f"{prefix}openQ4.app/Contents/PkgInfo": (package.MACOS_PKGINFO_BYTES, 0o644),
        f"{prefix}openQ4.app/Contents/MacOS/openQ4": (client_bytes, app_exec_mode),
        f"{prefix}openQ4.app/Contents/Resources/openQ4.icns": (b"icns\n", 0o644),
        f"{prefix}openQ4.app/Contents/Resources/VERSION.txt": (
            make_version_manifest_bytes(version, version_tag, "macos", arch),
            0o644,
        ),
        f"{prefix}openQ4.app/Contents/Resources/English.lproj/InfoPlist.strings": (localized_info, 0o644),
        f"{prefix}openQ4.app/Contents/Resources/French.lproj/InfoPlist.strings": (localized_info, 0o644),
    }
    if extra_entries:
        entries.update(extra_entries)
    return entries


def write_test_targz_archive(archive_path: Path, entries: dict[str, tuple[bytes, int]]) -> None:
    with tarfile.open(archive_path, "w:gz") as archive:
        for name, (data, mode) in entries.items():
            member = tarfile.TarInfo(name)
            member.mode = mode
            member.size = len(data)
            member.mtime = 0
            archive.addfile(member, io.BytesIO(data))


def write_test_targz_archive_with_symlink(
    archive_path: Path, entries: dict[str, tuple[bytes, int]], link_name: str
) -> None:
    with tarfile.open(archive_path, "w:gz") as archive:
        for name, (data, mode) in entries.items():
            member = tarfile.TarInfo(name)
            member.mode = mode
            member.size = len(data)
            member.mtime = 0
            archive.addfile(member, io.BytesIO(data))
        link = tarfile.TarInfo(link_name)
        link.type = tarfile.SYMTYPE
        link.linkname = "../escaped"
        link.mode = 0o777
        link.mtime = 0
        archive.addfile(link)


def write_test_targz_archive_with_fifo(
    archive_path: Path, entries: dict[str, tuple[bytes, int]], fifo_name: str
) -> None:
    with tarfile.open(archive_path, "w:gz") as archive:
        for name, (data, mode) in entries.items():
            member = tarfile.TarInfo(name)
            member.mode = mode
            member.size = len(data)
            member.mtime = 0
            archive.addfile(member, io.BytesIO(data))
        fifo = tarfile.TarInfo(fifo_name)
        fifo.type = tarfile.FIFOTYPE
        fifo.mode = 0o644
        fifo.mtime = 0
        archive.addfile(fifo)


def write_test_zip_archive(archive_path: Path, entries: dict[str, tuple[bytes, int]]) -> None:
    with ZipFile(archive_path, "w", compression=ZIP_DEFLATED) as archive:
        for name, (data, mode) in entries.items():
            info = ZipInfo(name)
            info.create_system = 3
            info.compress_type = ZIP_DEFLATED
            info.external_attr = (mode & 0o777) << 16
            archive.writestr(info, data)


def write_test_file(path: Path, data: bytes, mode: int = 0o644) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    os.chmod(path, mode)


def validate_macos_app_bundle_validator_runtime() -> None:
    package = load_package_module()
    work = ROOT / ".tmp" / "macos-app-contract"
    package_root = work / "openq4-v0.2.000-macos-arm64-opengl"
    app_root = package_root / "openQ4.app"
    app_contents = app_root / "Contents"
    arch = "arm64"
    version = "0.2.000"

    shutil.rmtree(work, ignore_errors=True)
    try:
        client_bytes = b"client-binary\n"
        write_test_file(package_root / f"openQ4-client_{arch}", client_bytes, 0o755)
        (package_root / package.GAME_DIR_NAME).mkdir(parents=True, exist_ok=True)
        write_test_file(app_contents / "Info.plist", make_macos_plist_bytes(package, version))
        write_test_file(app_contents / "PkgInfo", package.MACOS_PKGINFO_BYTES)
        write_test_file(app_contents / "MacOS" / "openQ4", client_bytes, 0o755)
        write_test_file(app_contents / "Resources" / "openQ4.icns", b"icns\n")
        write_test_file(app_contents / "Resources" / "VERSION.txt", b"openQ4\n")

        package.validate_macos_app_bundle(package_root, app_root, arch, version)

        write_test_file(app_contents / "MacOS" / "openQ4", b"other-binary\n", 0o755)
        expect_runtime_error(
            "macOS app executable does not match packaged client binary",
            lambda: package.validate_macos_app_bundle(package_root, app_root, arch, version),
            "macOS app executable drift from packaged client",
        )

        write_test_file(app_contents / "MacOS" / "openQ4", client_bytes, 0o755)
        write_test_file(app_contents / "PkgInfo", b"BROKEN!!")
        expect_runtime_error(
            "valid PkgInfo",
            lambda: package.validate_macos_app_bundle(package_root, app_root, arch, version),
            "invalid macOS app PkgInfo",
        )
    finally:
        shutil.rmtree(work, ignore_errors=True)


def validate_macos_archive_validator_runtime() -> None:
    package = load_package_module()
    work = ROOT / ".tmp" / "macos-archive-contract"
    package_root = work / "openq4-v0.2.000-macos-arm64-opengl"
    arch = "arm64"
    version = "0.2.000"
    plist_bytes = make_macos_plist_bytes(package, version)

    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True, exist_ok=True)
    try:
        entries = make_macos_archive_entries(package, package_root.name, arch, plist_bytes)
        for archive_format, archive_name, writer in (
            ("tar.gz", "good.tar.gz", write_test_targz_archive),
            ("zip", "good.zip", write_test_zip_archive),
        ):
            archive_path = work / archive_name
            writer(archive_path, entries)
            package.validate_macos_archive_contents(
                package_root,
                archive_path,
                archive_format,
                arch,
                version,
            )

        bad_exec_archive = work / "bad-exec.tar.gz"
        write_test_targz_archive(
            bad_exec_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                client_mode=0o644,
            ),
        )
        expect_runtime_error(
            "macOS archive entry is not executable",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_exec_archive,
                "tar.gz",
                arch,
                version,
            ),
            "non-executable macOS archive client",
        )

        bad_mode_archive = work / "bad-mode.tar.gz"
        write_test_targz_archive(
            bad_mode_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={f"{package_root.name}/world-writable.txt": (b"bad\n", 0o666)},
            ),
        )
        expect_runtime_error(
            "group/other writable",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_mode_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with unsafe mode bits",
        )

        bad_metadata_archive = work / "bad-metadata.tar.gz"
        write_test_targz_archive(
            bad_metadata_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={f"{package_root.name}/.DS_Store": (b"metadata\n", 0o644)},
            ),
        )
        expect_runtime_error(
            "non-runtime metadata/debug entries",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_metadata_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with Finder metadata",
        )

        bad_duplicate_archive = work / "bad-duplicate.tar.gz"
        duplicate_entries = make_macos_archive_entries(package, package_root.name, arch, plist_bytes)
        with tarfile.open(bad_duplicate_archive, "w:gz") as archive:
            for name, (data, mode) in duplicate_entries.items():
                for _ in range(2 if name.endswith("VERSION.txt") else 1):
                    member = tarfile.TarInfo(name)
                    member.mode = mode
                    member.size = len(data)
                    member.mtime = 0
                    archive.addfile(member, io.BytesIO(data))
        expect_runtime_error(
            "duplicate entry",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_duplicate_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with duplicate entry",
        )

        bad_path_archive = work / "bad-path.tar.gz"
        write_test_targz_archive(
            bad_path_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={f"{package_root.name}\\bad": (b"bad\n", 0o644)},
            ),
        )
        expect_runtime_error(
            "unsafe",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_path_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with backslash path",
        )

        bad_pkginfo_archive = work / "bad-pkginfo.tar.gz"
        write_test_targz_archive(
            bad_pkginfo_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={
                    f"{package_root.name}/openQ4.app/Contents/PkgInfo": (b"BROKEN!!", 0o644)
                },
            ),
        )
        expect_runtime_error(
            "PkgInfo",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_pkginfo_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with invalid PkgInfo",
        )

        bad_manifest_archive = work / "bad-manifest.tar.gz"
        write_test_targz_archive(
            bad_manifest_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={
                    f"{package_root.name}/VERSION.txt": (
                        make_version_manifest_bytes(version, "wrong-tag", "macos", arch),
                        0o644,
                    )
                },
            ),
        )
        expect_runtime_error(
            "version_tag",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_manifest_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with mismatched version manifest",
        )

        bad_localized_archive = work / "bad-localized.tar.gz"
        write_test_targz_archive(
            bad_localized_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={
                    f"{package_root.name}/openQ4.app/Contents/Resources/English.lproj/InfoPlist.strings": (
                        b'CFBundleName = "openQ4";\n',
                        0o644,
                    )
                },
            ),
        )
        expect_runtime_error(
            "CFBundleShortVersionString",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_localized_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with incomplete localized plist strings",
        )

        bad_symlink_archive = work / "bad-symlink.tar.gz"
        write_test_targz_archive_with_symlink(
            bad_symlink_archive,
            make_macos_archive_entries(package, package_root.name, arch, plist_bytes),
            f"{package_root.name}/openQ4.app/Contents/MacOS/linked",
        )
        expect_runtime_error(
            "symlink entry",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_symlink_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with symlink entry",
        )

        bad_special_archive = work / "bad-special.tar.gz"
        write_test_targz_archive_with_fifo(
            bad_special_archive,
            make_macos_archive_entries(package, package_root.name, arch, plist_bytes),
            f"{package_root.name}/openQ4.app/Contents/MacOS/fifo",
        )
        expect_runtime_error(
            "non-regular entry",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_special_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with non-regular entry",
        )

        bad_stale_module_archive = work / "bad-stale-module.tar.gz"
        write_test_targz_archive(
            bad_stale_module_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={f"{package_root.name}/{package.GAME_DIR_NAME}/game-sp_x64.dylib": (b"stale\n", 0o755)},
            ),
        )
        expect_runtime_error(
            "stale or mismatched game modules",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_stale_module_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with stale game module",
        )

        bad_plist_archive = work / "bad-plist-version.tar.gz"
        write_test_targz_archive(
            bad_plist_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                make_macos_plist_bytes(package, "0.1.000"),
            ),
        )
        expect_runtime_error(
            "CFBundleShortVersionString",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_plist_archive,
                "tar.gz",
                arch,
                version,
            ),
            "mismatched macOS archive plist version",
        )

        bad_app_exec_archive = work / "bad-app-exec.tar.gz"
        write_test_targz_archive(
            bad_app_exec_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={
                    f"{package_root.name}/openQ4.app/Contents/MacOS/openQ4": (
                        b"other-binary\n",
                        0o755,
                    )
                },
            ),
        )
        expect_runtime_error(
            "macOS archive app executable does not match packaged client binary",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_app_exec_archive,
                "tar.gz",
                arch,
                version,
            ),
            "mismatched macOS archive app executable",
        )

        unsafe_archive = work / "unsafe.tar.gz"
        write_test_targz_archive(
            unsafe_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={"other-package/escaped": (b"bad\n", 0o644)},
            ),
        )
        expect_runtime_error(
            "macOS archive contains unsafe or out-of-package path",
            lambda: package.validate_macos_archive_contents(
                package_root,
                unsafe_archive,
                "tar.gz",
                arch,
                version,
            ),
            "out-of-package macOS archive entry",
        )
    finally:
        shutil.rmtree(work, ignore_errors=True)


def validate_macos_signing_config_runtime() -> None:
    package = load_package_module()

    def args(**overrides):
        values = {
            "macos_signing_mode": "ad-hoc",
            "macos_code_sign_identity": "",
            "macos_entitlements": "",
            "macos_notarize": False,
            "macos_notary_keychain_profile": "",
            "macos_notary_keychain": "",
        }
        values.update(overrides)
        return SimpleNamespace(**values)

    ad_hoc = package.resolve_macos_signing_config(args())
    if ad_hoc.mode != "ad-hoc" or ad_hoc.identity != "-" or ad_hoc.hardened_runtime or ad_hoc.timestamp:
        raise AssertionError("macOS ad-hoc signing config should keep local/debug signing lightweight")

    expect_runtime_error(
        "notarization requires --macos-signing-mode=developer-id",
        lambda: package.resolve_macos_signing_config(args(macos_notarize=True)),
        "ad-hoc macOS notarization rejection",
    )
    expect_runtime_error(
        "requires --macos-code-sign-identity",
        lambda: package.resolve_macos_signing_config(args(macos_signing_mode="developer-id")),
        "Developer ID signing without identity",
    )

    work = ROOT / ".tmp" / "macos-entitlements-contract"
    shutil.rmtree(work, ignore_errors=True)
    try:
        work.mkdir(parents=True)
        valid_entitlements = work / "valid.entitlements"
        invalid_entitlements = work / "invalid.entitlements"
        list_entitlements = work / "list.entitlements"
        sandbox_entitlements = work / "sandbox.entitlements"
        debug_entitlements = work / "debug.entitlements"

        valid_entitlements.write_bytes(plistlib.dumps({"com.apple.security.files.user-selected.read-only": True}))
        invalid_entitlements.write_text("not a plist\n", encoding="utf-8")
        list_entitlements.write_bytes(plistlib.dumps(["not", "a", "dict"]))
        sandbox_entitlements.write_bytes(plistlib.dumps({"com.apple.security.app-sandbox": True}))
        debug_entitlements.write_bytes(plistlib.dumps({"com.apple.security.get-task-allow": True}))

        expect_runtime_error(
            "not a valid plist",
            lambda: package.resolve_macos_signing_config(args(macos_entitlements=str(invalid_entitlements))),
            "malformed macOS entitlements rejection",
        )
        expect_runtime_error(
            "dictionary root",
            lambda: package.resolve_macos_signing_config(args(macos_entitlements=str(list_entitlements))),
            "non-dictionary macOS entitlements rejection",
        )
        expect_runtime_error(
            "com.apple.security.app-sandbox",
            lambda: package.resolve_macos_signing_config(args(macos_entitlements=str(sandbox_entitlements))),
            "unsupported macOS App Sandbox entitlement rejection",
        )
        expect_runtime_error(
            "com.apple.security.get-task-allow",
            lambda: package.resolve_macos_signing_config(args(macos_entitlements=str(debug_entitlements))),
            "unsupported macOS debug entitlement rejection",
        )

        developer_id = package.resolve_macos_signing_config(
            args(
                macos_signing_mode="developer-id",
                macos_code_sign_identity="Developer ID Application: DarkMatter Productions (TEAMID1234)",
                macos_entitlements=str(valid_entitlements),
                macos_notarize=True,
                macos_notary_keychain_profile="openq4-release-notary",
                macos_notary_keychain=".tmp/notary.keychain-db",
            )
        )
        if (
            developer_id.mode != "developer-id"
            or not developer_id.hardened_runtime
            or not developer_id.timestamp
            or developer_id.entitlements != valid_entitlements.resolve()
            or not developer_id.notarize
            or developer_id.notary_keychain_profile != "openq4-release-notary"
        ):
            raise AssertionError("Developer ID macOS signing config did not enable release signing requirements")
    finally:
        shutil.rmtree(work, ignore_errors=True)


def validate_macos_signing_preserves_client_app_match_runtime() -> None:
    package = load_package_module()
    work = ROOT / ".tmp" / "macos-signing-match-contract"
    package_root = work / "openq4-v0.2.000-macos-arm64-opengl"
    app_root = package_root / "openQ4.app"
    app_executable = app_root / "Contents" / "MacOS" / "openQ4"
    arch = "arm64"

    original_platform = package.sys.platform
    original_which = package.shutil.which
    original_codesign_target = package.macos_codesign_target

    shutil.rmtree(work, ignore_errors=True)
    try:
        client_binary = package_root / f"openQ4-client_{arch}"
        write_test_file(client_binary, b"unsigned-client\n", 0o755)
        write_test_file(package_root / f"openQ4-ded_{arch}", b"dedicated\n", 0o755)
        write_test_file(package_root / package.GAME_DIR_NAME / f"game-sp_{arch}.dylib", b"sp\n", 0o755)
        write_test_file(package_root / package.GAME_DIR_NAME / f"game-mp_{arch}.dylib", b"mp\n", 0o755)
        write_test_file(app_executable, b"stale-app-executable\n", 0o755)

        calls = []

        def fake_which(tool_name):
            if tool_name == "codesign":
                return "/usr/bin/codesign"
            return original_which(tool_name)

        def fake_codesign_target(codesign_path, target, config):
            del codesign_path, config
            calls.append(target.relative_to(package_root).as_posix())
            if target == app_root:
                write_test_file(app_executable, b"signed-app-executable\n", 0o755)
            else:
                target.write_bytes(target.read_bytes() + b"signed\n")

        package.sys.platform = "darwin"
        package.shutil.which = fake_which
        package.macos_codesign_target = fake_codesign_target
        package.sign_macos_payload(
            package_root,
            arch,
            package.MacOSSigningConfig(
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

        expected_calls = [
            f"openQ4-ded_{arch}",
            f"{package.GAME_DIR_NAME}/game-sp_{arch}.dylib",
            f"{package.GAME_DIR_NAME}/game-mp_{arch}.dylib",
            "openQ4.app",
        ]
        if calls != expected_calls:
            raise AssertionError(f"Unexpected macOS signing order: {calls!r}")
        if client_binary.read_bytes() != app_executable.read_bytes():
            raise AssertionError("macOS signing should preserve loose client/app executable byte match")
        if not os.access(client_binary, os.X_OK):
            raise AssertionError("macOS signing should preserve the loose client executable bit")
    finally:
        package.sys.platform = original_platform
        package.shutil.which = original_which
        package.macos_codesign_target = original_codesign_target
        shutil.rmtree(work, ignore_errors=True)


def validate_macos_pk4_symlink_guard_runtime() -> None:
    if not hasattr(os, "symlink"):
        return

    package = load_package_module()
    work = ROOT / ".tmp" / "macos-pk4-symlink-contract"
    install_game_dir = work / "baseoq4"
    destination_pk4 = work / "pak0.pk4"

    shutil.rmtree(work, ignore_errors=True)
    try:
        write_test_file(install_game_dir / "mod.json", b"{}\n")
        write_test_file(install_game_dir / "normal.txt", b"ok\n")
        try:
            os.symlink("normal.txt", install_game_dir / "linked.txt")
        except (OSError, NotImplementedError):
            return

        expect_runtime_error(
            "refusing to package symlink",
            lambda: package.create_game_pk4(install_game_dir, destination_pk4),
            "macOS package PK4 symlink source",
        )
    finally:
        shutil.rmtree(work, ignore_errors=True)


def validate_legacy_macos_plist_runtime() -> None:
    package = load_package_module()
    info_plist_path = ROOT / "src" / "sys" / "osx" / "Info.plist"
    version_plist_path = ROOT / "src" / "sys" / "osx" / "version.plist"

    with info_plist_path.open("rb") as handle:
        info_plist = plistlib.load(handle)
    with version_plist_path.open("rb") as handle:
        version_plist = plistlib.load(handle)

    version = version_plist["CFBundleVersion"]
    if version_plist.get("CFBundleShortVersionString") != version:
        raise AssertionError("legacy macOS version.plist short version must match CFBundleVersion")

    package.validate_macos_plist_values(info_plist, "legacy macOS Info.plist", version)


def validate_macos_staged_payload_validator_runtime() -> None:
    validator = load_validation_module()
    work = ROOT / ".tmp" / "macos-staged-contract"
    install_root = work / ".install"
    game_dir = install_root / "baseoq4"
    arch = "arm64"
    expected_lipo_arch = validator.macos_expected_lipo_arch(arch)
    real_macos_lipo_arches = validator.macos_lipo_arches
    fake_lipo_arches: dict[Path, set[str]] = {}

    def fake_macos_lipo_arches(binary_path: Path) -> set[str]:
        return fake_lipo_arches.get(binary_path, {expected_lipo_arch})

    shutil.rmtree(work, ignore_errors=True)
    try:
        validator.macos_lipo_arches = fake_macos_lipo_arches
        client = install_root / f"openQ4-client_{arch}"
        dedicated = install_root / f"openQ4-ded_{arch}"
        write_test_file(install_root / "openQ4.icns", b"icns\n")
        write_test_file(install_root / "assets" / "splash" / "quake4_rt_bitmap_4001.bmp", b"bmp\n")
        write_test_file(client, b"client\n", 0o755)
        write_test_file(dedicated, b"ded\n", 0o755)
        sp_module = game_dir / f"game-sp_{arch}.dylib"
        mp_module = game_dir / f"game-mp_{arch}.dylib"
        write_test_file(sp_module, b"sp\n", 0o755)
        write_test_file(mp_module, b"mp\n", 0o755)

        validator.validate_macos_staged_metadata(
            ROOT,
            install_root,
            game_dir,
            [client],
            [dedicated],
        )

        if validator.host_is_macos():
            fake_lipo_arches[client] = {"x86_64"}
            expect_runtime_error(
                "architecture mismatch",
                lambda: validator.validate_macos_staged_metadata(
                    ROOT,
                    install_root,
                    game_dir,
                    [client],
                    [dedicated],
                ),
                "macOS staged payload with mismatched client architecture",
            )
            fake_lipo_arches[client] = {expected_lipo_arch}

        bad_module = game_dir / f"game-sp_{arch}.so"
        write_test_file(bad_module, b"wrong\n")
        expect_runtime_error(
            "non-dylib game modules",
            lambda: validator.validate_macos_staged_metadata(
                ROOT,
                install_root,
                game_dir,
                [client],
                [dedicated],
            ),
            "macOS staged payload with Linux game module",
        )
        bad_module.unlink()

        if os.name != "nt":
            os.chmod(mp_module, 0o644)
            expect_runtime_error(
                "not executable",
                lambda: validator.validate_macos_staged_metadata(
                    ROOT,
                    install_root,
                    game_dir,
                    [client],
                    [dedicated],
                ),
                "macOS staged payload with non-executable game module",
            )
            os.chmod(mp_module, 0o755)

        bad_dsym = install_root / f"openQ4-client_{arch}.dSYM"
        bad_dsym.mkdir()
        expect_runtime_error(
            "non-runtime artifacts",
            lambda: validator.validate_macos_staged_metadata(
                ROOT,
                install_root,
                game_dir,
                [client],
                [dedicated],
            ),
            "macOS staged payload with debug symbol bundle",
        )
        shutil.rmtree(bad_dsym)

        mp_module.unlink()
        expect_runtime_error(
            "architecture-matched game modules",
            lambda: validator.validate_macos_staged_metadata(
                ROOT,
                install_root,
                game_dir,
                [client],
                [dedicated],
            ),
            "macOS staged payload missing matched MP module",
        )
    finally:
        validator.macos_lipo_arches = real_macos_lipo_arches
        shutil.rmtree(work, ignore_errors=True)


def validate_meson_contract() -> None:
    options = read("meson_options.txt")
    meson = read("meson.build")
    baseoq4_meson = read("content/baseoq4/meson.build")
    setup_sh = read("tools/build/meson_setup.sh")
    setup_ps1 = read("tools/build/meson_setup.ps1")

    require(options, "'macos_graphics_bridge'", "Meson options")
    require(options, "choices: ['opengl', 'metal']", "Meson options")
    require(options, "Metal-ready SDL3/Cocoa integration surface", "Meson option description")
    require(options, "'macos_openal_provider'", "Meson options")
    require(options, "choices: ['apple_framework', 'system']", "Meson options")
    require(options, "OpenAL Soft-style AL/... headers", "Meson option description")

    require(meson, "macos_graphics_bridge = get_option('macos_graphics_bridge')", "Meson bridge option")
    require(meson, "macos_openal_provider = get_option('macos_openal_provider')", "Meson OpenAL provider option")
    require(meson, "macos_graphics_bridge != 'opengl'", "non-macOS bridge guard")
    require(meson, "macos_openal_provider != 'apple_framework'", "non-macOS OpenAL provider guard")
    require(meson, "macos_graphics_bridge == 'metal' and platform_backend_requested != 'sdl3'", "SDL3 bridge guard")
    require(meson, "use_macos_metal_bridge", "Metal bridge build predicate")
    require(meson, "dependency('appleframeworks', modules: ['OpenAL'], required: true)", "macOS Apple OpenAL provider")
    require(meson, "dependency('openal', required: true)", "macOS system OpenAL provider")
    require(meson, "-DUSE_OPENAL_SOFT_INCLUDES=1", "macOS system OpenAL include mode")
    require(meson, "modules: ['Metal', 'QuartzCore']", "Metal bridge framework dependency")
    require(meson, "macos_framework_modules = ['Cocoa', 'OpenGL', 'ApplicationServices']", "macOS SDL3 framework list")
    require(meson, "if not use_sdl3_backend\n      macos_framework_modules += ['Carbon']", "native macOS Carbon isolation")
    require(meson, "modules: macos_framework_modules", "macOS conditional framework dependency")
    reject(meson, "modules: ['Cocoa', 'OpenGL', 'ApplicationServices', 'Carbon']", "macOS SDL3 Carbon isolation")
    require(meson, "-DOPENQ4_MACOS_METAL_BRIDGE=1", "Metal bridge compile define")
    require(meson, "shared_objcpp_args += ['-DOPENQ4_MACOS_METAL_BRIDGE=1']", "Metal bridge ObjC++ compile define")
    require(meson, "shared_objc_args += ['-DOPENQ4_MACOS_METAL_BRIDGE=1']", "Metal bridge ObjC compile define")
    require(meson, "-fstack-protector-strong", "macOS compile hardening")
    require(meson, "-D_FORTIFY_SOURCE=2", "macOS compile hardening")
    require(meson, "-Wl,-pie", "macOS executable hardening")
    require(meson, "-Wl,-dead_strip", "macOS link hardening")
    require(meson, "'macOS graphics bridge': macos_graphics_bridge", "Meson summary")
    require(meson, "'macOS OpenAL provider': macos_openal_provider", "Meson summary")

    require(baseoq4_meson, "elif host_system == 'darwin'", "macOS game module source branch")
    require(baseoq4_meson, "shared_library(\n      game_sp_binary_name,", "macOS SP game module dylib target type")
    require(baseoq4_meson, "shared_library(\n      game_mp_binary_name,", "macOS MP game module dylib target type")
    require(baseoq4_meson, "name_suffix: 'dylib'", "macOS game module dylib suffix")
    require(baseoq4_meson, "-Wl,-install_name,@loader_path/", "macOS game module install name")

    require(setup_sh, "macos_graphics_bridge", "Bash Meson wrapper option preservation")
    require(setup_sh, "macos_openal_provider", "Bash Meson wrapper OpenAL provider preservation")
    require(setup_ps1, '"macos_graphics_bridge"', "PowerShell Meson wrapper option preservation")
    require(setup_ps1, '"macos_openal_provider"', "PowerShell Meson wrapper OpenAL provider preservation")


def validate_sdl3_runtime_contract() -> None:
    source = read("src/sys/sdl3/sdl3_backend.cpp")
    syscon = read("src/sys/posix/posix_syscon.cpp")
    hints = function_body(source, "static void SDL3_SetVideoHintDefaults(void) {")
    summary = function_body(source, "static void SDL3_PrintGraphicsBridgeSummary(void) {")
    init = function_body(source, "bool GLimp_Init(glimpParms_t parms) {")
    support_renderer = function_body(syscon, "static SDL_Renderer *Posix_CreateSupportRenderer( SDL_Window *window, const char *purpose ) {")
    console_create = function_body(syscon, "static bool Posix_ConsoleCreateWindow( void ) {")
    splash_create = function_body(syscon, "void Sys_ShowSplash( void ) {")
    splash_drain = function_body(syscon, "static void Posix_SplashDrainEvents( SDL_WindowID windowID ) {")

    require(source, "OPENQ4_MACOS_METAL_BRIDGE", "SDL3 Metal bridge compile guard")
    require(source, "SDL3_IsMacOSMetalBridge", "SDL3 Metal bridge predicate")
    require(source, "macOS Metal bridge (SDL3/Cocoa host, OpenGL renderer compatibility path)", "SDL3 bridge description")
    require(source, "static void SDL3_SetHintDefaultLogged", "SDL3 Metal bridge logged hint helper")
    require(source, "failed to set %s hint", "SDL3 Metal bridge logged hint failure")

    require(hints, "SDL3_SetHintDefaultLogged(SDL_HINT_VIDEO_DRIVER", "macOS Metal bridge SDL video driver hint")
    require(hints, '"cocoa"', "macOS Metal bridge SDL video driver hint")
    require(hints, "SDL3_SetHintDefaultLogged(SDL_HINT_RENDER_DRIVER", "macOS Metal bridge render hint")
    require(hints, "SDL3_SetHintDefaultLogged(SDL_HINT_GPU_DRIVER", "macOS Metal bridge GPU hint")
    require(hints, "SDL3_SetHintDefaultLogged(SDL_HINT_VIDEO_METAL_AUTO_RESIZE_DRAWABLE", "macOS Metal bridge drawable hint")

    require(summary, "no native Metal renderer rewrite is selected", "SDL3 Metal bridge log")
    require(summary, "SDL_VIDEO_METAL_AUTO_RESIZE_DRAWABLE", "SDL3 Metal bridge hint log")
    require(init, "SDL3_PrintGraphicsBridgeSummary();", "SDL3 GL initialization")

    require(support_renderer, "OPENQ4_MACOS_METAL_BRIDGE", "Metal bridge support-window renderer path")
    require(support_renderer, "SDL_CreateRenderer( window, NULL )", "Metal bridge support-window default renderer")
    require(support_renderer, "falling back to software", "Metal bridge support-window renderer fallback")
    require(support_renderer, 'SDL_CreateRenderer( window, "software" )', "Metal bridge support-window software fallback")
    require(console_create, 'Posix_CreateSupportRenderer( s_consoleWindow.window, "system console" )', "Metal bridge system console renderer")
    require(splash_create, 'Posix_CreateSupportRenderer( s_splashWindow.window, "splash" )', "Metal bridge splash renderer")
    require_before(console_create, "if ( s_consoleWindow.windowID == 0 )", 'Posix_CreateSupportRenderer( s_consoleWindow.window, "system console" )', "Metal bridge system console window-id guard")
    require_before(splash_create, "if ( s_splashWindow.windowID == 0 )", 'Posix_CreateSupportRenderer( s_splashWindow.window, "splash" )', "Metal bridge splash window-id guard")
    require(splash_drain, "if ( !SDL_PushEvent( &event ) ) {", "Metal bridge splash event requeue failure guard")
    require(splash_drain, "failed to requeue non-splash event", "Metal bridge splash event requeue failure guard")
    reject(console_create, 'SDL_CreateRenderer( s_consoleWindow.window, "software" )', "Metal bridge system console hard-coded software renderer")
    reject(splash_create, 'SDL_CreateRenderer( s_splashWindow.window, "software" )', "Metal bridge splash hard-coded software renderer")


def validate_packaging_and_release_contract() -> None:
    package = read("tools/build/package_nightly.py")
    pak_helper = read("tools/build/openq4_pak.py")
    plist = read("src/sys/osx/Info.plist")
    release = read(".github/workflows/manual-release.yml")
    compat = read("src/sys/osx/macosx_compat.mm")
    main = read("src/sys/osx/macosx_sdl3_main.cpp")
    posix = read("src/sys/posix/posix_main.cpp")
    validator = read("tools/validation/openq4_validate.py")
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")

    require(package, "--package-suffix", "release packaging variant suffix")
    require(package, "normalize_package_suffix", "release packaging variant suffix")
    require(package, '"macos": "dmg"', "macOS DMG default archive format")
    require(package, '"dmg": ".dmg"', "macOS DMG archive suffix")
    require(package, "import filecmp", "macOS app executable comparison")
    require(package, "import stat", "macOS archive symlink validation")
    require(package, "import subprocess", "macOS binary dependency validation")
    require(package, "filecmp.cmp(client_binary, app_executable, shallow=False)", "macOS app executable comparison")
    require(package, "shutil.copy2(client_binary, app_executable)", "macOS app executable creation")
    require(package, "shutil.copy2(app_executable, client_binary)", "macOS signed app executable recopy")
    require(package, "macOS app executable does not match packaged client binary", "macOS app executable validation")
    require(package, "get_package_executable_archive_paths", "POSIX archive executable mode preservation")
    require(package, "ZipInfo.from_file", "POSIX archive executable mode preservation")
    require(package, "info.mode = 0o755", "POSIX archive executable mode preservation")
    require(package, "create_macos_dmg", "macOS DMG creation")
    require(package, "hdiutil_path", "macOS DMG hdiutil lookup")
    require(package, '"-format",\n            "UDZO"', "macOS compressed DMG format")
    require(package, "validate_macos_dmg_image", "macOS DMG validation")
    require(package, "notarize_macos_dmg_image", "macOS DMG notarization")
    require(package, "archive format 'dmg' is only supported for macOS packages", "macOS DMG platform guard")
    require(package, "MACOS_ALLOWED_RUNTIME_DEPENDENCY_PREFIXES", "macOS binary dependency validation")
    require(package, "macos_otool_dependencies", "macOS binary dependency validation")
    require(package, "macos_otool_install_name", "macOS game module install-name validation")
    require(package, "validate_macos_binary_dependencies", "macOS binary dependency validation")
    require(package, "otool_path, \"-L\"", "macOS binary dependency validation")
    require(package, "otool_path, \"-D\"", "macOS game module install-name validation")
    require(package, "macOS binary has unbundled non-system dependencies", "macOS binary dependency validation")
    require(package, "macOS game module install name is not package-relative", "macOS game module install-name validation")
    require(package, "MACOS_FORBIDDEN_XATTRS", "macOS package quarantine validation")
    require(package, "strip_macos_forbidden_xattrs", "macOS package quarantine validation")
    require(package, "validate_no_macos_forbidden_xattrs", "macOS package quarantine validation")
    require(package, "MACOS_FORBIDDEN_ARCHIVE_NAMES", "macOS archive metadata validation")
    require(package, "MACOS_PKGINFO_BYTES", "macOS app PkgInfo validation")
    require(package, "MACOS_LOCALIZED_INFO_LOCALES", "macOS localized plist validation")
    require(package, "MACOS_LIPO_ARCHES", "macOS binary architecture validation")
    require(package, "validate_macos_package_file_modes", "macOS package mode validation")
    require(package, "validate_no_package_symlinks", "macOS package symlink validation")
    require(package, "validate_no_package_special_files", "macOS package special-file validation")
    require(pak_helper, "refusing to package symlink", "macOS PK4 symlink validation")
    require(package, "macOS archive contains non-regular entry", "macOS archive special-file validation")
    require(package, "validate_version_manifest_bytes", "macOS version manifest validation")
    require(package, "validate_macos_version_manifests", "macOS version manifest validation")
    require(package, "validate_macos_localized_info_bytes", "macOS localized plist validation")
    require(package, "validate_macos_localized_info_files", "macOS localized plist validation")
    require(package, "macos_package_version_tag_from_name", "macOS archive version manifest validation")
    require(package, "validate_macos_archive_name", "macOS archive path validation")
    require(package, "validate_macos_archive_mode", "macOS archive mode validation")
    require(package, "record_macos_archive_entry", "macOS archive duplicate validation")
    require(package, "release archive input contains symlink entries", "macOS package symlink validation")
    require(package, "macOS archive contains duplicate entry", "macOS archive duplicate validation")
    require(package, "macOS archive entry is group/other writable", "macOS archive mode validation")
    require(package, "macOS archive contains symlink entry", "macOS archive symlink validation")
    require(package, "macOS archive contains stale or mismatched game modules", "macOS archive stale module validation")
    require(package, "macOS archive version manifests are unreadable", "macOS archive version manifest validation")
    require(package, "macos_lipo_arches", "macOS binary architecture validation")
    require(package, "validate_macos_binary_architectures", "macOS binary architecture validation")
    require(package, "lipo_path, \"-archs\"", "macOS binary architecture validation")
    require(package, "MACOS_SIGNING_MODES", "macOS signing mode validation")
    require(package, "class MacOSSigningConfig", "macOS signing config")
    require(package, "--macos-signing-mode", "macOS signing CLI")
    require(package, "--macos-code-sign-identity", "macOS Developer ID signing CLI")
    require(package, "--macos-entitlements", "macOS signing entitlements CLI")
    require(package, "--macos-notarize", "macOS notarization CLI")
    require(package, "MACOS_FORBIDDEN_ENTITLEMENTS", "macOS entitlements policy")
    require(package, "validate_macos_entitlements_file", "macOS entitlements validation")
    require(package, "com.apple.security.app-sandbox", "macOS App Sandbox entitlement policy")
    require(package, "com.apple.security.get-task-allow", "macOS debug entitlement policy")
    require(package, "resolve_macos_signing_config", "macOS signing config")
    require(package, "sign_macos_payload", "macOS shared signing")
    require(package, "ad_hoc_sign_macos_payload", "macOS ad-hoc signing")
    require(package, "verify_macos_codesignature", "macOS code-sign validation")
    require(package, "verify_macos_developer_id_signature", "macOS Developer ID validation")
    require(package, "notarize_macos_app_bundle", "macOS notarization")
    require(package, "\"--force\"", "macOS code-sign force option")
    require(package, "\"--timestamp=none\"", "macOS ad-hoc signing timestamp suppression")
    require(package, "\"--options\", \"runtime\"", "macOS Hardened Runtime signing")
    require(package, "\"Authority=Developer ID Application:\"", "macOS Developer ID validation")
    require(package, "\"Runtime Version=\"", "macOS Hardened Runtime validation")
    require(package, "\"xcrun\",\n        \"notarytool\",\n        \"submit\"", "macOS notarytool submission")
    require(package, "\"xcrun\", \"stapler\", \"staple\"", "macOS stapling")
    require(package, "\"spctl\", \"--assess\"", "macOS Gatekeeper assessment")
    require(package, "code signature verification", "macOS code-sign validation")
    require(package, "game-sp_{arch}.dylib", "macOS archive game module validation")
    require(package, "game-mp_{arch}.dylib", "macOS archive game module validation")
    require(package, "pak1.pk4", "macOS archive level pack validation")
    require(package, "macOS archive contains non-runtime metadata/debug entries", "macOS archive metadata validation")
    require(package, "MACOS_EXPECTED_PLIST_VALUES", "macOS package Info.plist validation")
    require(package, "validate_macos_plist_values", "macOS package Info.plist validation")
    require(package, "validate_macos_app_bundle", "macOS package app validation")
    require(package, "validate_macos_archive_contents", "macOS package archive validation")
    require(package, '"CFBundleDisplayName": "openQ4"', "macOS package Info.plist validation")
    require(package, '"CFBundleIconFile": "openQ4.icns"', "macOS package Info.plist validation")
    require(package, '"CFBundleName": "openQ4"', "macOS package Info.plist validation")
    require(package, '"LSApplicationCategoryType": "public.app-category.games"', "macOS package Info.plist validation")
    require(package, '("CFBundleShortVersionString", "CFBundleVersion")', "macOS package Info.plist validation")
    require(package, "openQ4.app/Contents/PkgInfo", "macOS package PkgInfo validation")
    require(package, "openQ4-ded_{arch}", "macOS package archive validation")
    require(package, "English.lproj/InfoPlist.strings", "macOS package archive validation")
    require(package, "French.lproj/InfoPlist.strings", "macOS package archive validation")
    require(package, "macOS archive entry is not executable", "macOS package archive validation")
    require(package, "macOS archive app executable does not match packaged client binary", "macOS package archive validation")
    require(package, "macOS archive contains unsafe or out-of-package path", "macOS package archive validation")
    require(package, "macOS archive Info.plist", "macOS package archive validation")
    require(package, "plistlib.loads", "macOS package Info.plist validation")
    require(package, "NSSupportsAutomaticGraphicsSwitching", "macOS package Info.plist validation")
    require(package, '"LSMinimumSystemVersion": "11.0"', "macOS package compatibility floor")
    require(package, '"NSPrincipalClass": "NSApplication"', "macOS package Cocoa app metadata")

    require(compat, "_NSGetExecutablePath", "macOS executable path resolution")
    require(compat, "Sys_CopyPathIfFits", "macOS executable path truncation guard")
    require(compat, "Sys_CopyExecutablePath", "macOS executable path resolution")
    require(compat, "malloc( bufferSize )", "macOS executable path long-buffer handling")
    require(compat, "realpath( pathBuffer, resolvedPath )", "macOS executable path canonicalization")
    require(compat, "Sys_DirectoryContainsGameDir", "macOS base path validation")
    require(compat, "BASE_GAMEDIR", "macOS base path validation")
    require(compat, "Sys_UseAppBundleParentBasePathCandidate", "macOS app bundle base path validation")
    require(compat, "\"/Contents/MacOS\"", "macOS app bundle base path validation")
    require(compat, "\"app parent\"", "macOS app bundle base path validation")
    require(main, "SDL_MAIN_HANDLED", "macOS SDL3 launch initialization")
    require(main, "static int SDLCALL OpenQ4_Main", "macOS SDL3 launch initialization")
    require(main, "SDL_RunApp(argc, argv, OpenQ4_Main, NULL)", "macOS SDL3 launch initialization")
    require(posix, "realpath( path, resolvedPath )", "macOS dylib path canonicalization")
    require(posix, "RTLD_NOW | RTLD_LOCAL", "macOS dylib local symbol scope")
    require(validator, "host_is_macos", "macOS staged payload validation")
    require(validator, "macos_binary_arch", "macOS staged payload validation")
    require(validator, "validate_macos_staged_metadata", "macOS staged payload validation")
    require(validator, "openQ4.icns", "macOS staged payload validation")
    require(validator, "non-dylib game modules", "macOS staged payload validation")
    require(validator, "architecture-matched game modules", "macOS staged payload validation")
    require(validator, "macOS staged game module is not executable", "macOS staged game module validation")
    require(validator, "MACOS_FORBIDDEN_XATTRS", "macOS staged quarantine validation")
    require(validator, "MACOS_NON_RUNTIME_PATTERNS", "macOS staged debug artifact validation")
    require(validator, "MACOS_LIPO_ARCHES", "macOS staged architecture validation")
    require(validator, "validate_no_macos_symlinks", "macOS staged symlink validation")
    require(validator, "validate_no_macos_unsafe_file_modes", "macOS staged mode validation")
    require(validator, "Staged payload contains symlink entries", "cross-platform staged symlink validation")
    require(validator, "macOS staged payload contains unsafe file modes", "macOS staged mode validation")
    require(validator, "stale or mismatched game modules", "macOS staged stale module validation")
    require(validator, "validate_macos_binary_architectures", "macOS staged architecture validation")
    require(validator, "lipo_path, \"-archs\"", "macOS staged architecture validation")
    require(commit, "macOS ARM64 ${{ matrix.bridge_label }} Commit Validation", "commit validation macOS job")
    require(commit, "macos_graphics_bridge: opengl", "commit validation macOS OpenGL job")
    require(commit, "macos_graphics_bridge: metal", "commit validation macOS Metal job")
    require(commit, "macos_openal_provider: apple_framework", "commit validation macOS OpenAL provider")
    require(commit, "--extra-setup-arg=-Dmacos_graphics_bridge=${{ matrix.macos_graphics_bridge }}", "commit validation macOS bridge setup")
    require(commit, "--extra-setup-arg=-Dmacos_openal_provider=${{ matrix.macos_openal_provider }}", "commit validation macOS OpenAL setup")
    require(commit, "runs-on: macos-15", "commit validation macOS job")
    require(commit, "bash tools/validation/validate_pr.sh \\", "commit validation macOS job")
    require(commit, "--fail-on-dirty \\", "commit validation macOS job")
    require(push, "macOS OpenGL Push Verification", "push verification macOS OpenGL job")
    require(push, "macOS Metal Push Verification", "push verification macOS Metal job")
    require(push, "macos-opengl", "push verification macOS OpenGL artifact")
    require(push, "macos-metal", "push verification macOS Metal artifact")
    require(push, "macos_openal_provider: apple_framework", "push verification macOS OpenAL provider")
    require(push, "--extra-setup-arg=-Dmacos_graphics_bridge=${{ matrix.macos_graphics_bridge }}", "push verification macOS bridge setup")
    require(push, "--extra-setup-arg=-Dmacos_openal_provider=${{ matrix.macos_openal_provider }}", "push verification macOS OpenAL setup")

    for source, context in ((package, "macOS package Info.plist"), (plist, "legacy macOS Info.plist")):
        require(source, "CFBundleDisplayName", context)
        require(source, "CFBundleName", context)
        require(source, "LSApplicationCategoryType", context)
        require(source, "NSHighResolutionCapable", context)
        require(source, "NSPrincipalClass", context)
        require(source, "NSSupportsAutomaticGraphicsSwitching", context)
        require(source, "LSMinimumSystemVersion", context)
        require(source, "11.0", context)

    require(plist, "<string>openQ4.icns</string>", "legacy macOS Info.plist icon")
    require(plist, "CFBundleShortVersionString", "legacy macOS Info.plist version")

    require(release, "macos_signed_release_enabled", "manual release credential-aware macOS matrix")
    require(release, "macOS unsigned/unnotarized tar.gz release artifacts enabled", "manual release macOS credential fallback")
    require(release, "fromJSON(needs.metadata.outputs.release_matrix)", "manual release dynamic matrix")
    require(release, "macOS ARM64 OpenGL{macos_unsigned_label}", "manual release macOS OpenGL matrix")
    require(release, '"macos_graphics_bridge": "opengl"', "manual release OpenGL bridge matrix")
    require(release, '"macos_openal_provider": "apple_framework"', "manual release macOS OpenAL provider")
    require(release, '"macos_release_mode": macos_release_mode', "manual release macOS release mode")
    require(release, '"package_suffix": f"-opengl{macos_unsigned_suffix}"', "manual release OpenGL package suffix")
    require(release, 'macos_archive_format = "dmg" if macos_signed_release_enabled else "tar.gz"', "manual release macOS archive format selection")
    require(release, 'macos_archive_ext = ".dmg" if macos_signed_release_enabled else ".tar.gz"', "manual release macOS archive extension selection")
    require(release, "macOS ARM64 Metal{macos_unsigned_label}", "manual release macOS Metal matrix")
    require(release, '"macos_graphics_bridge": "metal"', "manual release macOS matrix")
    require(release, '"package_suffix": f"-metal{macos_unsigned_suffix}"', "manual release Metal package suffix")
    require(release, "-Dmacos_graphics_bridge=${{ matrix.macos_graphics_bridge }}", "manual release setup")
    require(release, "-Dmacos_openal_provider=${{ matrix.macos_openal_provider }}", "manual release OpenAL setup")
    require(release, "Import macOS Developer ID certificate", "manual release Developer ID setup")
    require(release, "matrix.macos_release_mode == 'signed'", "manual release conditional Developer ID setup")
    require(release, "MACOS_DEVELOPER_ID_APPLICATION_CERTIFICATE_BASE64", "manual release Developer ID certificate secret")
    require(release, "MACOS_DEVELOPER_ID_APPLICATION_IDENTITY", "manual release Developer ID identity secret")
    require(release, "MACOS_NOTARY_APP_PASSWORD", "manual release notarization secret")
    require(release, "security import", "manual release Developer ID certificate import")
    require(release, "xcrun notarytool store-credentials", "manual release notarytool profile")
    require(release, "--package-suffix=\"${{ matrix.package_suffix }}\"", "manual release packaging")
    require(release, "--macos-signing-mode developer-id", "manual release Developer ID package signing")
    require(release, "--macos-signing-mode ad-hoc", "manual release unsigned macOS package signing")
    require(release, "--macos-notarize", "manual release notarized package signing")
    reject(release, "--macos-entitlements", "manual release default entitlements policy")
    require(release, 'cmp -s "${app_exec}" "${client_binary}"', "manual release macOS app validation")
    require(release, "macOS app executable does not match the packaged client binary", "manual release macOS app validation")
    require(release, "Missing or invalid macOS app PkgInfo", "manual release macOS app validation")
    require(release, "Missing or non-executable macOS dedicated binary", "manual release macOS app validation")
    require(release, "Missing or non-executable macOS package game module", "manual release macOS app validation")
    require(release, "sp_module=", "manual release macOS dependency validation")
    require(release, "check_macos_binary_architecture", "manual release macOS architecture validation")
    require(release, "lipo -archs", "manual release macOS architecture validation")
    require(release, "check_macos_codesignature", "manual release macOS signature validation")
    require(release, "codesign --verify --strict", "manual release macOS signature validation")
    require(release, "check_macos_developer_id_signature", "manual release Developer ID signature validation")
    require(release, "check_macos_ad_hoc_signature", "manual release unsigned signature validation")
    require(release, "Authority=Developer ID Application:", "manual release Developer ID authority validation")
    require(release, "Runtime Version=", "manual release Hardened Runtime validation")
    require(release, "xcrun stapler validate", "manual release stapled notarization validation")
    require(release, "macOS unsigned release archive is ad-hoc signed and not notarized.", "manual release unsigned notarization notice")
    require(release, "hdiutil verify", "manual release DMG validation")
    require(release, "hdiutil imageinfo", "manual release DMG validation")
    require(release, "spctl --assess --type execute", "manual release Gatekeeper validation")
    require(release, "check_macos_install_name", "manual release macOS install-name validation")
    require(release, "@loader_path/game-sp_${{ matrix.binary_arch }}.dylib", "manual release macOS install-name validation")
    require(release, "check_macos_binary_dependencies", "manual release macOS dependency validation")
    require(release, "otool -L", "manual release macOS dependency validation")
    require(release, "unbundled non-system dependency", "manual release macOS dependency validation")
    require(release, "Missing macOS staged payload file", "manual release macOS staged validation")
    require(release, ".install/baseoq4/game-sp_${{ matrix.binary_arch }}.dylib", "manual release macOS staged validation")
    require(release, ".install/baseoq4/game-mp_${{ matrix.binary_arch }}.dylib", "manual release macOS staged validation")
    require(release, "macOS staged payload contains non-dylib game modules", "manual release macOS staged validation")
    require(release, "check_plist_value CFBundleDisplayName openQ4", "manual release macOS app validation")
    require(release, "check_plist_value CFBundleIconFile openQ4.icns", "manual release macOS app validation")
    require(release, "check_plist_value CFBundleName openQ4", "manual release macOS app validation")
    require(release, "check_plist_value CFBundleShortVersionString", "manual release macOS app validation")
    require(release, "check_plist_value CFBundleVersion", "manual release macOS app validation")
    require(release, "check_plist_value LSMinimumSystemVersion 11.0", "manual release macOS app validation")
    require(release, "check_plist_value LSApplicationCategoryType public.app-category.games", "manual release macOS app validation")
    require(release, "check_plist_value NSPrincipalClass NSApplication", "manual release macOS app validation")
    require(release, "check_plist_value NSSupportsAutomaticGraphicsSwitching true", "manual release macOS app validation")
    require(release, "openq4-${{ needs.metadata.outputs.version_tag }}-macos-arm64-opengl.dmg", "manual release expected assets")
    require(release, "openq4-${{ needs.metadata.outputs.version_tag }}-macos-arm64-metal.dmg", "manual release expected assets")
    require(release, "openq4-${{ needs.metadata.outputs.version_tag }}-macos-arm64-opengl-unsigned.tar.gz", "manual release unsigned expected assets")
    require(release, "openq4-${{ needs.metadata.outputs.version_tag }}-macos-arm64-metal-unsigned.tar.gz", "manual release unsigned expected assets")


def validate_macos_workflow_security_contract() -> None:
    host = read("tools/macos/Invoke-openQ4MacOSWorkflow.ps1")
    assets = read("tools/macos/guest/openq4-macos-install-quake4-assets.sh")
    guest = read("tools/macos/guest/openq4-macos-sync-build-test.sh")
    signoff_validator = read("tools/macos/validate_signoff_archive.py")
    debug = read(".github/workflows/macos-debug.yml")
    workflow_doc = read("docs-dev/macos-vm-testing-workflow.md")
    validation_runner = read("tools/validation/openq4_validate.py")

    require(host, "Assert-NoArchiveLinks", "macOS host archive symlink guard")
    require(host, "RemoteTempRoot", "macOS host per-run remote temp root")
    require(host, "BatchMode=yes", "macOS host non-interactive SSH")
    require(host, "Unsafe archive path", "macOS host archive path validation")
    require(host, "Archive contains a symlink or special file", "macOS host archive special-file validation")
    require(host, "rsync -a --delete", "macOS host safe temp extraction sync")
    require(host, "openQ4/.git", "macOS host source metadata exclusion")
    require(host, "openQ4/.codex", "macOS host local agent metadata exclusion")
    require(host, "openQ4-game/.git", "macOS host GameLibs metadata exclusion")
    require(host, "$assetRootName/q4base/*.cfg", "macOS host personal config exclusion")
    require(host, "$assetRootName/q4base/q4key", "macOS host private key exclusion")
    require(host, '"Signoff"', "macOS host signoff action")
    require(host, '"CollectResults"', "macOS host result collection action")
    require(host, '-ScriptAction "signoff"', "macOS host signoff action")
    require(host, '[string]$ResultCollectionDir', "macOS host result collection directory")
    require(host, '[string]$MacOSRunId', "macOS host result collection run ID")
    require(host, '[switch]$SkipResultCollection', "macOS host result collection opt-out")
    require(host, '[switch]$SkipResultArchiveValidation', "macOS host result archive validation opt-out")
    require(host, '[switch]$RequireCompletedSignoffChecklist', "macOS host completed checklist validation")
    require(host, "function Copy-FromMac", "macOS host result copy helper")
    require(host, "function Collect-MacOSResults", "macOS host result collection helper")
    require(host, "function Test-MacOSResultArchive", "macOS host result archive validation helper")
    require(host, 'openq4-macos-results-${RunId}.tar.gz', "macOS host result archive naming")
    require(host, "No macOS result directories matched", "macOS host missing result guard")
    require(host, "-Action CollectResults requires -MacOSRunId <id>", "macOS host collect-results run ID guard")
    require(host, "validate_signoff_archive.py", "macOS host result archive validator")
    require(host, '"--run-id"', "macOS host result archive validator run ID")
    require(host, '"--bridges"', "macOS host result archive validator bridge list")
    require(host, '"--require-completed-checklist"', "macOS host completed checklist validator flag")
    require(host, '[ValidateSet("opengl", "metal", "both")]', "macOS host dual bridge selection")
    require(host, '[ValidateSet("apple_framework", "system")]', "macOS host OpenAL provider selection")
    require(host, "function Get-MacOSGraphicsBridgeRuns", "macOS host bridge run expansion")
    require(host, 'return @("opengl", "metal")', "macOS host bridge run expansion")
    require(host, "function Get-MacOSBridgeBuildDir", "macOS host bridge-specific builddir")
    require(host, 'return "${BuildDir}-${Bridge}"', "macOS host bridge-specific builddir")
    require(host, "function Invoke-BuildTestActionForBridge", "macOS host bridge action environment")
    require(host, '"OPENQ4_MACOS_OPENAL_PROVIDER" = $MacOSOpenALProvider', "macOS host OpenAL provider environment")
    require(host, '"OPENQ4_MACOS_RUN_ID" = $MacOSRunId', "macOS host run ID environment")
    require(host, 'foreach ($bridge in (Get-MacOSGraphicsBridgeRuns))', "macOS host multi-bridge loop")
    require(host, "-MacOSGraphicsBridge both is supported for Build, Signoff, and All", "macOS host multi-bridge guard")
    require(host, "Collect-MacOSResults -RunId $MacOSRunId", "macOS host automatic signoff result collection")
    require(host, '"CollectResults" {\n            $shouldCollectResults = $true\n        }', "macOS host collect-results action")
    require(host, "Test-MacOSResultArchive -ArchivePath $archivePath -RunId $MacOSRunId", "macOS host automatic signoff result validation")

    require(assets, "reject_unsafe_tar_entries", "macOS asset tar path validation")
    require(assets, "reject_unsafe_tree_entries", "macOS asset symlink validation")
    require(assets, "Unsafe asset archive path", "macOS asset tar path validation")
    require(assets, "Asset archive contains a symlink or special file", "macOS asset special-file validation")
    require(assets, "multiple q4base directories", "macOS asset ambiguity validation")
    require(assets, "/tmp/openq4-macos-*/*", "macOS asset copied archive cleanup")

    require(guest, "resolve_under_repo", "macOS guest builddir containment")
    require(guest, "Path escapes the openQ4 repository", "macOS guest builddir containment")
    require(guest, "OPENQ4_ALLOW_CROSS_ARCH_CLIENT", "macOS guest cross-arch launch opt-in")
    require(guest, "Missing staged macOS client for host architecture", "macOS guest exact-arch client selection")
    require(guest, 'graphics_bridge="${OPENQ4_MACOS_GRAPHICS_BRIDGE:-opengl}"', "macOS guest bridge environment")
    require(guest, 'openal_provider="${OPENQ4_MACOS_OPENAL_PROVIDER:-apple_framework}"', "macOS guest OpenAL provider environment")
    require(guest, 'stamp="${OPENQ4_MACOS_RUN_ID:-$(date +%Y%m%d-%H%M%S)}"', "macOS guest host-controlled run ID")
    require(guest, "Invalid OPENQ4_MACOS_RUN_ID", "macOS guest run ID validation")
    require(guest, 'run_dir="${results_root}/${stamp}-${action}-${graphics_bridge}"', "macOS guest bridge-specific results")
    require(guest, '"-Dmacos_graphics_bridge=${graphics_bridge}"', "macOS guest bridge setup option")
    require(guest, '"-Dmacos_openal_provider=${openal_provider}"', "macOS guest OpenAL setup option")
    require(guest, "shell_quote", "macOS guest launcher quoting")
    require(guest, "shlex.quote", "macOS guest launcher quoting")
    require(guest, "exec ${client_q} +set fs_basepath ${basepath_q}", "macOS guest launcher quoted exec")
    require(guest, "run_signoff", "macOS guest runtime signoff action")
    require(guest, "macos-runtime-signoff.md", "macOS guest runtime signoff report")
    require(guest, "Manual Hardware Checklist", "macOS guest runtime signoff checklist")
    require(guest, "Graphics bridge: ${graphics_bridge}", "macOS guest signoff bridge metadata")
    require(guest, "OpenAL provider: ${openal_provider}", "macOS guest signoff OpenAL metadata")
    require(guest, "Bridge-specific build and staged install completed.", "macOS guest signoff build evidence")
    require(guest, "SPDisplaysDataType", "macOS guest display inventory")
    require(guest, "SPAudioDataType", "macOS guest audio inventory")
    require(guest, "SPUSBDataType", "macOS guest USB inventory")
    require(guest, "SPBluetoothDataType", "macOS guest Bluetooth inventory")
    require(guest, "build_openq4\n    run_smoke\n    run_renderer_matrix\n    install_launcher\n    write_signoff_report", "macOS guest signoff run order")

    require(signoff_validator, "validate_signoff_archive", "macOS signoff archive validator")
    require(signoff_validator, "Archive path escapes through '..'", "macOS signoff archive path guard")
    require(signoff_validator, "Archive contains a non-regular entry", "macOS signoff archive entry-type guard")
    require(signoff_validator, "macos-runtime-signoff.md", "macOS signoff archive report check")
    require(signoff_validator, "openq4-macos-workflow.log", "macOS signoff archive log check")
    require(signoff_validator, "renderer-smoke", "macOS signoff archive smoke output check")
    require(signoff_validator, "renderer-matrix", "macOS signoff archive matrix output check")
    require(signoff_validator, "require_completed_checklist", "macOS signoff archive completed checklist gate")
    require(validation_runner, "macos_signoff_archive.py", "validation runner macOS signoff archive test")

    require(debug, "bash -n tools/macos/guest/openq4-macos-bootstrap.sh", "macOS debug workflow guest script syntax check")
    require(debug, "bash -n tools/macos/guest/openq4-macos-install-quake4-assets.sh", "macOS debug workflow asset script syntax check")
    require(debug, "bash -n tools/macos/guest/openq4-macos-sync-build-test.sh", "macOS debug workflow build script syntax check")
    require(workflow_doc, "-Action Signoff", "macOS workflow signoff documentation")
    require(workflow_doc, "-Action CollectResults", "macOS workflow collect-results documentation")
    require(workflow_doc, "-MacOSGraphicsBridge both", "macOS workflow dual bridge signoff documentation")
    require(workflow_doc, "<timestamp>-signoff-opengl", "macOS workflow bridge-specific signoff documentation")
    require(workflow_doc, "<timestamp>-signoff-metal", "macOS workflow bridge-specific signoff documentation")
    require(workflow_doc, ".tmp/macos-vm/results/openq4-macos-results-<run-id>.tar.gz", "macOS workflow result collection documentation")
    require(workflow_doc, "-MacOSRunId <id>", "macOS workflow run ID documentation")
    require(workflow_doc, "-ResultCollectionDir", "macOS workflow result directory documentation")
    require(workflow_doc, "-SkipResultCollection", "macOS workflow result collection opt-out documentation")
    require(workflow_doc, "-SkipResultArchiveValidation", "macOS workflow result archive validation opt-out documentation")
    require(workflow_doc, "-RequireCompletedSignoffChecklist", "macOS workflow completed checklist validation documentation")
    require(workflow_doc, "-Action CollectResults -MacOSRunId <run-id>", "macOS workflow collect-results expected validation documentation")
    require(workflow_doc, "tools/macos/validate_signoff_archive.py", "macOS workflow signoff archive validator documentation")
    require(workflow_doc, "--require-completed-checklist", "macOS workflow signoff archive completed checklist documentation")
    require(workflow_doc, "-MacOSOpenALProvider system", "macOS workflow OpenAL provider documentation")
    require(workflow_doc, "macos-runtime-signoff.md", "macOS workflow signoff documentation")
    require(workflow_doc, "keyboard/mouse/controller input", "macOS workflow signoff documentation")


def validate_macos_shell_entrypoints() -> None:
    for relative_path in (
        "tools/build/meson_setup.sh",
        "tools/validation/validate_pr.sh",
        "tools/validation/validate_push.sh",
    ):
        source = read(relative_path)
        reject(source, "dirname --", relative_path)
        require(source, 'case "${BASH_SOURCE[0]}" in', relative_path)
        require(source, 'script_dir="$(CDPATH= cd "${script_dir}" && pwd)"', relative_path)


def validate_docs_and_ci_hooks() -> None:
    building = read("BUILDING.md")
    platform_support = read("docs-dev/platform-support.md")
    migration = read("docs-dev/sdl3-linux-macos-migration.md")
    getting_started = read("docs-user/getting-started.md")
    package_readme = read("assets/release/README.html")
    release_notes = read("docs-dev/release-completion.md")
    validator = read("tools/validation/openq4_validate.py")
    push = read(".github/workflows/push-verification.yml")
    commit = read(".github/workflows/commit-validation.yml")

    require(building, "-Dmacos_graphics_bridge=metal", "build documentation")
    require(building, "without a native renderer rewrite", "build documentation")
    require(building, "when a signed/notarized macOS release is required", "macOS release signing documentation")
    require(building, "MACOS_DEVELOPER_ID_APPLICATION_CERTIFICATE_BASE64", "macOS release signing documentation")
    require(building, "xcrun stapler validate", "macOS release signing documentation")
    require(building, "openq4-<version>-macos-arm64-opengl.dmg", "macOS DMG release asset documentation")
    require(building, "final DMG notarization/stapling", "macOS DMG release documentation")
    require(building, "do not pass a custom entitlements file by default", "macOS entitlement policy documentation")
    require(building, "rejects App Sandbox or `get-task-allow` entitlements", "macOS entitlement policy documentation")
    require(building, "Credentialed runs publish signed/notarized DMGs", "macOS release credential policy documentation")
    require(building, "`-unsigned.tar.gz` archives", "macOS release credential fallback documentation")
    require(building, "Manual macOS release artifacts are Apple Silicon/arm64 only", "macOS architecture policy documentation")
    require(building, "Intel Mac and universal2 packages are not published", "macOS architecture policy documentation")
    require(building, "SDL3 is the default platform path and does not link Carbon", "macOS Carbon isolation documentation")
    require(building, "macos_openal_provider", "macOS OpenAL provider documentation")
    require(building, "system/OpenAL Soft dependency", "macOS OpenAL provider documentation")
    require(getting_started, "signed/notarized OpenGL and Metal bridge DMGs", "getting started guide")
    require(getting_started, "Keep `openQ4.app`, `baseoq4/`, and the loose runtime files together", "getting started guide")
    require(getting_started, "publish clearly labeled `-unsigned.tar.gz` archives", "getting started macOS credential fallback")
    require(getting_started, "ad-hoc signed only for bundle validity", "getting started macOS unsigned policy")
    require(getting_started, "macOS packages are for Apple Silicon/arm64 Macs", "getting started macOS architecture policy")
    require(getting_started, "Intel Mac and universal2 packages are not published yet", "getting started macOS architecture policy")
    require(package_readme, "signed/notarized OpenGL and Metal bridge DMGs", "release package README")
    require(package_readme, "Keep <code>openQ4.app</code>, <code>baseoq4/</code>, and the loose runtime files together", "release package README")
    require(package_readme, "clearly labeled <code>-unsigned.tar.gz</code> archives", "release package macOS credential fallback")
    require(package_readme, "ad-hoc signed only for bundle validity", "release package macOS unsigned policy")
    require(package_readme, "macOS packages are for Apple Silicon/arm64 Macs", "release package macOS architecture policy")
    require(package_readme, "Intel Mac and universal2 packages are not published yet", "release package macOS architecture policy")
    require(release_notes, "retain the OpenGL package while adding a separate Metal bridge package", "release completion notes")
    require(release_notes, "Final macOS package validation", "release completion notes")
    require(release_notes, "Developer ID/notarization lane", "release completion notes")
    require(release_notes, "macOS release entitlement handling is now explicit and fail-fast", "release completion entitlement policy")
    require(release_notes, "Credentialed macOS release artifacts now use compressed DMG images", "release completion DMG packaging")
    require(release_notes, "Manual releases keep macOS downloads visible", "release completion unsigned macOS fallback")
    require(release_notes, "Apple Silicon/arm64-only macOS support policy", "release completion macOS architecture policy")
    require(release_notes, "Intel Mac and universal2 packages are not advertised", "release completion macOS architecture policy")
    require(release_notes, "macOS SDL3 release builds now avoid linking the legacy Carbon framework", "release completion Carbon isolation note")
    require(release_notes, "macOS OpenAL migration now has an explicit build switch", "release completion OpenAL migration note")
    require(platform_support, "Linux and macOS now use the shared SDL3 runtime path", "platform support roadmap")
    require(platform_support, "macOS SDL3 builds select `src/sys/osx/macosx_sdl3.cpp`", "platform support roadmap")
    require(platform_support, "Credentialed release runs publish signed/notarized DMGs", "platform support macOS credential policy")
    require(platform_support, "`-unsigned.tar.gz` archives", "platform support macOS credential fallback")
    require(platform_support, "Manual macOS release artifacts are Apple Silicon/arm64 only", "platform support macOS architecture policy")
    require(platform_support, "Intel Mac and universal2 packages are intentionally not claimed", "platform support macOS architecture policy")
    require(platform_support, "final compressed DMG creation", "platform support DMG packaging")
    require(platform_support, "DMG notarization/stapling", "platform support DMG packaging")
    require(platform_support, "keeps the legacy Carbon framework isolated to `-Dplatform_backend=native`", "platform support Carbon isolation")
    require(platform_support, "Hardened Runtime without custom entitlements by default", "platform support entitlement policy")
    require(platform_support, "App Sandbox or `get-task-allow` entitlements are rejected", "platform support entitlement policy")
    require(platform_support, "macOS audio still defaults to Apple's OpenAL framework", "platform support OpenAL provider policy")
    require(platform_support, "`-Dmacos_openal_provider=system`", "platform support OpenAL provider switch")
    require(migration, "leaves Carbon isolated to the native Cocoa fallback", "SDL3 migration Carbon isolation")
    require(migration, "macOS CI covers OpenGL and Metal bridge configure/build/install/package validation", "SDL3 migration plan")

    require(validator, "macos_metal_bridge.py", "validation runner")
    require(push, "tools/tests/macos_metal_bridge.py", "push verification workflow")
    require(commit, "tools/tests/macos_metal_bridge.py", "commit validation workflow")


def main() -> None:
    validate_meson_contract()
    validate_sdl3_runtime_contract()
    validate_packaging_and_release_contract()
    validate_macos_workflow_security_contract()
    validate_macos_shell_entrypoints()
    validate_macos_app_bundle_validator_runtime()
    validate_macos_archive_validator_runtime()
    validate_macos_signing_config_runtime()
    validate_macos_signing_preserves_client_app_match_runtime()
    validate_macos_pk4_symlink_guard_runtime()
    validate_legacy_macos_plist_runtime()
    validate_macos_staged_payload_validator_runtime()
    validate_docs_and_ci_hooks()
    print("macos_metal_bridge: ok")


if __name__ == "__main__":
    main()
