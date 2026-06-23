#!/usr/bin/env python3
"""Regression checks for package, staging, and PK4 safety guards."""

from __future__ import annotations

import importlib.util
import json
import os
import shutil
import stat
import subprocess
import sys
from pathlib import Path
from types import ModuleType
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo


ROOT = Path(__file__).resolve().parents[2]
BUILD_DIR = ROOT / "tools" / "build"
WORK = ROOT / ".tmp" / "packaging-safety-test"


def load_module(name: str, path: Path) -> ModuleType:
    if str(path.parent) not in sys.path:
        sys.path.insert(0, str(path.parent))
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"could not load module from {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


OPENQ4_PAK = load_module("openq4_pak_safety_test", BUILD_DIR / "openq4_pak.py")
PACKAGE = load_module("package_nightly_safety_test", BUILD_DIR / "package_nightly.py")
STAGE_GAMELIBS = load_module("stage_gamelibs_safety_test", BUILD_DIR / "stage_gamelibs.py")


def write_file(path: Path, data: bytes = b"data\n") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def expect_runtime_error(callback, text: str, label: str) -> None:
    try:
        callback()
    except RuntimeError as exc:
        if text not in str(exc):
            raise AssertionError(f"{label} raised {exc!r}, expected text {text!r}") from exc
        return
    raise AssertionError(f"{label} unexpectedly succeeded")


def expect_value_error(callback, text: str, label: str) -> None:
    try:
        callback()
    except ValueError as exc:
        if text not in str(exc):
            raise AssertionError(f"{label} raised {exc!r}, expected text {text!r}") from exc
        return
    raise AssertionError(f"{label} unexpectedly succeeded")


def make_symlink(target: Path, link: Path) -> bool:
    try:
        os.symlink(target, link)
    except (OSError, NotImplementedError):
        return False
    return True


def run_script(script: Path, *args: Path | str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(script), *(str(arg) for arg in args)],
        cwd=ROOT,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


def validate_pk4_source_containment() -> None:
    source_root = WORK / "pak-source-root"
    inside = source_root / "content" / "baseoq4" / "pak0"
    outside = WORK / "outside-pack"
    write_file(inside / "materials" / "ok.mtr")
    write_file(outside / "materials" / "bad.mtr")

    manifest = OPENQ4_PAK.format_pk4_source_manifest(source_root, inside, "pak0.pk4")
    if "materials/ok.mtr" not in manifest:
        raise AssertionError("safe pack source was not listed in manifest")

    expect_runtime_error(
        lambda: OPENQ4_PAK.format_pk4_source_manifest(source_root, outside, "pak0.pk4"),
        "must stay under",
        "pack source containment",
    )
    expect_runtime_error(
        lambda: OPENQ4_PAK.create_game_pk4(source_root / "missing", WORK / "missing.pk4", required_files=set()),
        "source directory not found",
        "missing pack source",
    )

    result = run_script(BUILD_DIR / "list_pak_sources.py", source_root, "pak0.pk4", "../outside-pack")
    if result.returncode == 0 or "must stay under" not in result.stderr:
        raise AssertionError(f"list_pak_sources.py accepted an escaped source dir: {result.stderr}")


def write_zip(path: Path, entries: list[tuple[ZipInfo, bytes]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with ZipFile(path, "w", compression=ZIP_DEFLATED) as archive:
        for info, data in entries:
            archive.writestr(info, data)


def zip_info(name: str, mode: int = 0o100644) -> ZipInfo:
    info = ZipInfo(name)
    info.compress_type = ZIP_DEFLATED
    info.external_attr = mode << 16
    return info


def validate_pk4_archive_member_guards() -> None:
    for name, message in (
        ("../escape.txt", "unsafe archive path"),
        ("C:/escape.txt", "unsafe archive path"),
        ("folder//escape.txt", "unsafe archive path"),
    ):
        archive_path = WORK / "bad-archives" / f"{name.replace(':', '_').replace('/', '_').replace(chr(92), '_')}.pk4"
        write_zip(archive_path, [(zip_info(name), b"x")])
        expect_runtime_error(
            lambda archive_path=archive_path: OPENQ4_PAK.inspect_game_pk4(archive_path, required_files=set()),
            message,
            f"unsafe PK4 member {name!r}",
        )
    expect_runtime_error(
        lambda: OPENQ4_PAK.validate_pk4_arcname("folder\\escape.txt", "pak0.pk4"),
        "unsafe archive path",
        "PK4 backslash member",
    )

    duplicate_path = WORK / "bad-archives" / "duplicate.pk4"
    write_zip(duplicate_path, [(zip_info("foo.txt"), b"1"), (zip_info("FOO.txt"), b"2")])
    expect_runtime_error(
        lambda: OPENQ4_PAK.inspect_game_pk4(duplicate_path, required_files=set()),
        "duplicate archive path",
        "duplicate PK4 member",
    )

    symlink_path = WORK / "bad-archives" / "symlink.pk4"
    write_zip(symlink_path, [(zip_info("linked.txt", stat.S_IFLNK | 0o777), b"target")])
    expect_runtime_error(
        lambda: OPENQ4_PAK.inspect_game_pk4(symlink_path, required_files=set()),
        "non-regular archive entry",
        "PK4 symlink member",
    )


def validate_copy_helpers_do_not_follow_destination_symlinks() -> None:
    source = WORK / "copy" / "source.txt"
    destination = WORK / "copy" / "dest.txt"
    outside = WORK / "copy-outside.txt"
    write_file(source, b"new\n")
    write_file(outside, b"outside\n")

    if not make_symlink(outside, destination):
        return

    copied = OPENQ4_PAK.copy_file_if_changed(source, destination)
    if not copied:
        raise AssertionError("copy_file_if_changed should replace a destination symlink")
    if outside.read_bytes() != b"outside\n":
        raise AssertionError("copy_file_if_changed followed a destination symlink")
    if destination.is_symlink() or destination.read_bytes() != b"new\n":
        raise AssertionError("copy_file_if_changed did not replace the symlink with the source file")


def validate_list_sources_guards() -> None:
    source_root = WORK / "source-list-root"
    write_file(source_root / "src" / "ok.cpp", b"// ok\n")
    outside = WORK / "source-list-outside"
    write_file(outside / "bad.cpp", b"// bad\n")

    result = run_script(BUILD_DIR / "list_sources.py", source_root, "../source-list-outside")
    if result.returncode == 0 or "must stay under" not in result.stderr:
        raise AssertionError(f"list_sources.py accepted an escaped source dir: {result.stderr}")

    link = source_root / "src" / "linked.cpp"
    if make_symlink(source_root / "src" / "ok.cpp", link):
        result = run_script(BUILD_DIR / "list_sources.py", source_root, "src")
        if result.returncode == 0 or "refusing to list symlinked source file" not in result.stderr:
            raise AssertionError(f"list_sources.py accepted a symlinked source: {result.stderr}")


def validate_fast_stage_guards_and_copy() -> None:
    source_root = WORK / "fast-stage" / "openQ4"
    build_dir = source_root / "builddir"
    install_dir = source_root / ".install"
    write_file(build_dir / "openQ4-client_x64.exe", b"client\n")
    write_file(build_dir / "baseoq4" / "game-sp_x64.dll", b"game\n")
    write_file(build_dir / "baseoq4" / "pak0.pk4", b"pak0\n")

    bad_result = run_script(
        BUILD_DIR / "stage_fast_install.py",
        "--source-root",
        source_root,
        "--build-dir",
        build_dir,
        "--install-dir",
        source_root / ".tmp" / "not-install",
    )
    if bad_result.returncode == 0 or "install directory must be" not in bad_result.stderr:
        raise AssertionError(f"stage_fast_install.py accepted an unsafe install dir: {bad_result.stderr}")

    result = run_script(
        BUILD_DIR / "stage_fast_install.py",
        "--source-root",
        source_root,
        "--build-dir",
        build_dir,
        "--install-dir",
        install_dir,
    )
    if result.returncode != 0:
        raise AssertionError(f"stage_fast_install.py failed safe staging: {result.stderr}")
    if not (install_dir / "openQ4-client_x64.exe").is_file():
        raise AssertionError("fast stage did not copy root runtime binary")
    if not (install_dir / "baseoq4" / "game-sp_x64.dll").is_file():
        raise AssertionError("fast stage did not copy game runtime binary")


def validate_package_name_and_copy_guards() -> None:
    expect_value_error(
        lambda: PACKAGE.validate_package_path_token("0.1/escape", "version tag"),
        "file-name-safe",
        "package version tag slash",
    )
    expect_value_error(
        lambda: PACKAGE.resolve_package_root(WORK / "out", "../escape"),
        "escapes output directory",
        "package root escape",
    )

    source = WORK / "package-copy" / "source.txt"
    outside = WORK / "package-copy-outside.txt"
    link = WORK / "package-copy" / "link.txt"
    write_file(source)
    write_file(outside)
    if make_symlink(outside, link):
        expect_runtime_error(
            lambda: PACKAGE.copy_regular_file(link, WORK / "package-copy" / "dest.txt"),
            "refusing to package symlinked file",
            "package symlinked file source",
        )

    share = WORK / "package-share" / "share"
    write_file(share / "applications" / "openq4.desktop")
    if make_symlink(outside, share / "leak.txt"):
        expect_runtime_error(
            lambda: PACKAGE.copy_regular_tree(share, WORK / "package-share" / "dest"),
            "refusing to package symlink from tree",
            "package share symlink source",
        )


def validate_version_manifest_integrity() -> None:
    expect_runtime_error(
        lambda: PACKAGE.parse_version_manifest_bytes(
            b"openQ4\nversion=1\nversion=2\nversion_tag=1\nplatform=linux\narch=x64\n",
            "test",
        ),
        "duplicate key",
        "duplicate version manifest key",
    )
    expect_runtime_error(
        lambda: PACKAGE.parse_version_manifest_bytes(
            b"openQ4\n=1\nversion_tag=1\nplatform=linux\narch=x64\n",
            "test",
        ),
        "empty key",
        "empty version manifest key",
    )


def validate_stage_manifest_rejects_unsafe_paths() -> None:
    stage_root = WORK / "stage-manifest"
    write_file(stage_root / "src" / "game" / "Game_local.cpp")
    manifest_path = stage_root / STAGE_GAMELIBS.MANIFEST_NAME
    manifest_path.write_text(
        json.dumps(
            {
                "format": 1,
                "fileCount": 1,
                "files": [{"path": "../escape.cpp", "sha256": "0" * 64}],
            }
        ),
        encoding="utf-8",
    )
    expect_runtime_error(
        lambda: STAGE_GAMELIBS.validate_stage_manifest(stage_root),
        "unsafe path",
        "unsafe staged manifest path",
    )


def validate_validation_wiring() -> None:
    validator = (ROOT / "tools" / "validation" / "openq4_validate.py").read_text(encoding="utf-8")
    push = (ROOT / ".github" / "workflows" / "push-verification.yml").read_text(encoding="utf-8")
    commit = (ROOT / ".github" / "workflows" / "commit-validation.yml").read_text(encoding="utf-8")
    for context, text in (
        ("validation runner", validator),
        ("push workflow", push),
        ("commit workflow", commit),
    ):
        if "packaging_safety.py" not in text:
            raise AssertionError(f"packaging_safety.py is not wired into {context}")


def main() -> None:
    shutil.rmtree(WORK, ignore_errors=True)
    try:
        validate_pk4_source_containment()
        validate_pk4_archive_member_guards()
        validate_copy_helpers_do_not_follow_destination_symlinks()
        validate_list_sources_guards()
        validate_fast_stage_guards_and_copy()
        validate_package_name_and_copy_guards()
        validate_version_manifest_integrity()
        validate_stage_manifest_rejects_unsafe_paths()
        validate_validation_wiring()
    finally:
        shutil.rmtree(WORK, ignore_errors=True)
    print("packaging_safety: ok")


if __name__ == "__main__":
    main()
