#!/usr/bin/env python3
"""Regression checks for validation-runner input and staging hardening."""

from __future__ import annotations

import argparse
import importlib.util
import os
import shutil
import sys
from pathlib import Path
from types import ModuleType


ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / ".tmp" / "validation-hardening-test"


def load_module(name: str, path: Path) -> ModuleType:
    if str(path.parent) not in sys.path:
        sys.path.insert(0, str(path.parent))
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"could not load module from {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


VALIDATOR = load_module("openq4_validation_hardening_test", ROOT / "tools" / "validation" / "openq4_validate.py")


def write_file(path: Path, data: bytes = b"x\n") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def expect_validation_error(callback, text: str, label: str) -> None:
    try:
        callback()
    except VALIDATOR.ValidationError as exc:
        if text not in str(exc):
            raise AssertionError(f"{label} raised {exc!r}, expected text {text!r}") from exc
        return
    raise AssertionError(f"{label} unexpectedly succeeded")


def expect_argument_error(callback, text: str, label: str) -> None:
    try:
        callback()
    except argparse.ArgumentTypeError as exc:
        if text not in str(exc):
            raise AssertionError(f"{label} raised {exc!r}, expected text {text!r}") from exc
        return
    raise AssertionError(f"{label} unexpectedly succeeded")


def with_host_flags(windows: bool, linux: bool, macos: bool, callback) -> None:
    original_windows = VALIDATOR.host_is_windows
    original_linux = VALIDATOR.host_is_linux
    original_macos = VALIDATOR.host_is_macos
    VALIDATOR.host_is_windows = lambda: windows
    VALIDATOR.host_is_linux = lambda: linux
    VALIDATOR.host_is_macos = lambda: macos
    try:
        callback()
    finally:
        VALIDATOR.host_is_windows = original_windows
        VALIDATOR.host_is_linux = original_linux
        VALIDATOR.host_is_macos = original_macos


def validate_positive_integer_arguments() -> None:
    if VALIDATOR.positive_int("4") != 4:
        raise AssertionError("positive_int returned the wrong value")

    for value in ("0", "-1", "not-a-number"):
        expect_argument_error(
            lambda value=value: VALIDATOR.positive_int(value),
            "positive integer" if value != "not-a-number" else "not an integer",
            f"positive_int({value!r})",
        )

    source = (ROOT / "tools" / "validation" / "openq4_validate.py").read_text(encoding="utf-8")
    for token in (
        'parser.add_argument("--jobs", "-j", type=positive_int',
        'parser.add_argument("--runtime-timeout", type=positive_int',
    ):
        if token not in source:
            raise AssertionError(f"validation runner is missing argument hardening token {token!r}")


def validate_source_and_build_dir_guards() -> None:
    VALIDATOR.validate_source_root(ROOT)

    fake_root = WORK / "not-openq4"
    fake_root.mkdir(parents=True, exist_ok=True)
    expect_validation_error(
        lambda: VALIDATOR.validate_source_root(fake_root),
        "missing required openQ4 files",
        "invalid source root",
    )

    expect_validation_error(lambda: VALIDATOR.validate_build_dir(ROOT, ROOT), "source root", "source-root build dir")
    expect_validation_error(
        lambda: VALIDATOR.validate_build_dir(ROOT, ROOT.parent),
        "contain the source root",
        "ancestor build dir",
    )
    expect_validation_error(
        lambda: VALIDATOR.validate_build_dir(ROOT, ROOT / "docs-dev" / "validation-build"),
        "must live under .tmp/ or use a builddir* name",
        "source-controlled build dir",
    )

    file_build_dir = WORK / "build-file"
    write_file(file_build_dir)
    expect_validation_error(
        lambda: VALIDATOR.validate_build_dir(ROOT, file_build_dir),
        "not a directory",
        "file build dir",
    )

    VALIDATOR.validate_build_dir(ROOT, ROOT / "builddir-validation-hardening")
    VALIDATOR.validate_build_dir(ROOT, ROOT / ".tmp" / "validation-hardening" / "build")


def validate_staged_symlink_guard() -> None:
    root = WORK / "symlink-payload"
    install_root = root / ".install"
    target = root / "outside.txt"
    link = install_root / "baseoq4" / "linked.txt"
    write_file(target)
    link.parent.mkdir(parents=True, exist_ok=True)
    try:
        os.symlink(target, link)
    except (OSError, NotImplementedError):
        return

    expect_validation_error(
        lambda: VALIDATOR.validate_no_staged_symlinks(root, install_root),
        "symlink entries",
        "staged symlink guard",
    )


def validate_recursive_non_runtime_scan() -> None:
    root = WORK / "non-runtime"
    install_root = root / ".install"
    game_dir = install_root / "baseoq4"
    write_file(game_dir / "nested" / "debug.lib")
    expect_validation_error(
        lambda: VALIDATOR.validate_no_non_runtime_artifacts(
            root,
            (install_root, game_dir),
            VALIDATOR.NON_RUNTIME_PATTERNS,
            "Non-runtime artifacts remain staged",
        ),
        "nested/debug.lib",
        "recursive non-runtime artifact scan",
    )


def validate_engine_architecture_mismatch() -> None:
    root = WORK / "engine-arch"
    install_root = root / ".install"
    game_dir = install_root / "baseoq4"
    client = install_root / "openQ4-client_x64.exe"
    dedicated = install_root / "openQ4-ded_arm64.exe"
    write_file(client)
    write_file(dedicated)

    with_host_flags(
        True,
        False,
        False,
        lambda: expect_validation_error(
            lambda: VALIDATOR.validate_staged_architecture_set(root, game_dir, [client], [dedicated]),
            "engine binary architecture mismatch",
            "engine architecture mismatch",
        ),
    )


def validate_game_module_suffix_guard() -> None:
    root = WORK / "module-suffix"
    install_root = root / ".install"
    game_dir = install_root / "baseoq4"
    client = install_root / "openQ4-client_x64.exe"
    dedicated = install_root / "openQ4-ded_x64.exe"
    write_file(client)
    write_file(dedicated)
    write_file(game_dir / "game-sp_x64.so")
    write_file(game_dir / "game-mp_x64.dll")

    with_host_flags(
        True,
        False,
        False,
        lambda: expect_validation_error(
            lambda: VALIDATOR.validate_staged_architecture_set(root, game_dir, [client], [dedicated]),
            "wrong platform suffix",
            "game module suffix mismatch",
        ),
    )


def validate_game_module_architecture_match() -> None:
    root = WORK / "module-arch"
    install_root = root / ".install"
    game_dir = install_root / "baseoq4"
    client = install_root / "openQ4-client_x64.exe"
    dedicated = install_root / "openQ4-ded_x64.exe"
    write_file(client)
    write_file(dedicated)
    write_file(game_dir / "game-sp_x64.dll")
    write_file(game_dir / "game-mp_arm64.dll")

    with_host_flags(
        True,
        False,
        False,
        lambda: expect_validation_error(
            lambda: VALIDATOR.validate_staged_architecture_set(root, game_dir, [client], [dedicated]),
            "game module architecture mismatch",
            "game module architecture mismatch",
        ),
    )

    (game_dir / "game-mp_arm64.dll").unlink()
    write_file(game_dir / "game-mp_x64.dll")

    def assert_valid_arch_set() -> None:
        arches = VALIDATOR.validate_staged_architecture_set(root, game_dir, [client], [dedicated])
        if arches != {"x64"}:
            raise AssertionError(f"unexpected staged architecture set: {arches!r}")

    with_host_flags(True, False, False, assert_valid_arch_set)


def validate_windows_pdb_architecture_match() -> None:
    root = WORK / "windows-pdb"
    install_root = root / ".install"
    game_dir = install_root / "baseoq4"
    write_file(install_root / "OpenAL32.dll")
    write_file(install_root / "openQ4-client_arm64.pdb")

    with_host_flags(
        True,
        False,
        False,
        lambda: expect_validation_error(
            lambda: VALIDATOR.validate_windows_symbols(root, install_root, game_dir, {"x64"}),
            "missing architecture-matched diagnostic PDB",
            "missing exact PDB architecture",
        ),
    )

    for path in (
        install_root / "openQ4-client_x64.pdb",
        install_root / "openQ4-ded_x64.pdb",
        game_dir / "game-sp_x64.pdb",
        game_dir / "game-mp_x64.pdb",
    ):
        write_file(path)

    with_host_flags(
        True,
        False,
        False,
        lambda: expect_validation_error(
            lambda: VALIDATOR.validate_windows_symbols(root, install_root, game_dir, {"x64"}),
            "stale or architecture-mismatched PDB",
            "stale PDB architecture",
        ),
    )

    (install_root / "openQ4-client_arm64.pdb").unlink()
    with_host_flags(True, False, False, lambda: VALIDATOR.validate_windows_symbols(root, install_root, game_dir, {"x64"}))


def validate_validation_wiring() -> None:
    validator = (ROOT / "tools" / "validation" / "openq4_validate.py").read_text(encoding="utf-8")
    push = (ROOT / ".github" / "workflows" / "push-verification.yml").read_text(encoding="utf-8")
    commit = (ROOT / ".github" / "workflows" / "commit-validation.yml").read_text(encoding="utf-8")
    release_notes = (ROOT / "docs-dev" / "release-completion.md").read_text(encoding="utf-8")

    for token in (
        "positive_int",
        "validate_source_root",
        "validate_build_dir",
        "validate_no_staged_symlinks",
        "validate_staged_architecture_set",
        "validate_windows_symbols",
        "validate_no_non_runtime_artifacts",
    ):
        if token not in validator:
            raise AssertionError(f"validation runner is missing {token}")

    for context, text in (
        ("validation runner", validator),
        ("push workflow", push),
        ("commit workflow", commit),
    ):
        if "validation_hardening.py" not in text:
            raise AssertionError(f"validation_hardening.py is not wired into {context}")

    if "Validation runs now fail earlier" not in release_notes:
        raise AssertionError("release notes do not mention validation hardening")


def main() -> None:
    shutil.rmtree(WORK, ignore_errors=True)
    try:
        validate_positive_integer_arguments()
        validate_source_and_build_dir_guards()
        validate_staged_symlink_guard()
        validate_recursive_non_runtime_scan()
        validate_engine_architecture_mismatch()
        validate_game_module_suffix_guard()
        validate_game_module_architecture_match()
        validate_windows_pdb_architecture_match()
        validate_validation_wiring()
    finally:
        shutil.rmtree(WORK, ignore_errors=True)
    print("validation_hardening: ok")


if __name__ == "__main__":
    main()
