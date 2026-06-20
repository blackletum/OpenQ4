#!/usr/bin/env python3
"""Incrementally stage local runtime files from builddir into .install."""

from __future__ import annotations

import argparse
import filecmp
import shutil
from pathlib import Path


ROOT_RUNTIME_PATTERNS = (
    "openQ4-client_*.exe",
    "openQ4-client_*.pdb",
    "openQ4-ded_*.exe",
    "openQ4-ded_*.pdb",
    "OpenAL32.dll",
)
GAME_RUNTIME_PATTERNS = (
    "game-sp_*.dll",
    "game-sp_*.pdb",
    "game-mp_*.dll",
    "game-mp_*.pdb",
    "mod.json",
    "pak0.pk4",
    "pak1.pk4",
)
NON_RUNTIME_PATTERNS = (
    "*.exp",
    "*.ilk",
    "*.lib",
    "*.map",
    "*.zip",
)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Stage changed local build outputs into .install without running meson install."
    )
    parser.add_argument("--build-dir", required=True, help="Meson build directory.")
    parser.add_argument("--install-dir", required=True, help="Runtime staging root, usually .install.")
    return parser.parse_args(argv[1:])


def copy_if_changed(source: Path, destination: Path) -> bool:
    if destination.is_file() and filecmp.cmp(source, destination, shallow=False):
        return False
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    return True


def remove_matches(root: Path, patterns: tuple[str, ...]) -> list[Path]:
    removed: list[Path] = []
    if not root.is_dir():
        return removed
    for pattern in patterns:
        for path in sorted(root.glob(pattern)):
            if path.is_file():
                path.unlink()
                removed.append(path)
    return removed


def copy_matches(source_root: Path, destination_root: Path, patterns: tuple[str, ...]) -> list[Path]:
    copied: list[Path] = []
    if not source_root.is_dir():
        return copied
    for pattern in patterns:
        for source in sorted(source_root.glob(pattern)):
            if not source.is_file():
                continue
            destination = destination_root / source.name
            if copy_if_changed(source, destination):
                copied.append(destination)
    return copied


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    build_dir = Path(args.build_dir).resolve()
    install_dir = Path(args.install_dir).resolve()
    build_game_dir = build_dir / "baseoq4"
    install_game_dir = install_dir / "baseoq4"

    install_dir.mkdir(parents=True, exist_ok=True)
    install_game_dir.mkdir(parents=True, exist_ok=True)

    removed = remove_matches(install_dir, NON_RUNTIME_PATTERNS)
    removed += remove_matches(install_game_dir, NON_RUNTIME_PATTERNS + ("*.so", "*.dylib"))
    copied = copy_matches(build_dir, install_dir, ROOT_RUNTIME_PATTERNS)
    copied += copy_matches(build_game_dir, install_game_dir, GAME_RUNTIME_PATTERNS)

    print(
        f"fast-staged .install: copied={len(copied)} "
        f"removed_non_runtime={len(removed)}"
    )
    for path in copied[:20]:
        print(f"  copied {path}")
    if len(copied) > 20:
        print(f"  ... {len(copied) - 20} more")
    return 0


if __name__ == "__main__":
    import sys

    raise SystemExit(main(sys.argv))
