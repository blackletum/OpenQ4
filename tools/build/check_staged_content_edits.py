#!/usr/bin/env python3
"""Refuse to overwrite newer local edits in the staged baseoq4 content tree."""

from __future__ import annotations

import argparse
import filecmp
import os
import sys
from pathlib import Path

INSTALLED_ROOT_FILES = (
    "default.cfg",
    "openq4_defaults.cfg",
    "openq4_profile_steamdeck.cfg",
)

INSTALLED_SUBDIRS = (
    "materials",
    "def",
    "botfiles",
    "glprogs",
    "guis",
    "gfx",
    "strings",
    "maps",
)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Check staged baseoq4 content before Meson install overwrites it."
    )
    parser.add_argument(
        "--source-root",
        default=".",
        help="Repository root (defaults to current directory).",
    )
    return parser.parse_args(argv[1:])


def iter_installed_source_files(source_game_dir: Path):
    for name in INSTALLED_ROOT_FILES:
        candidate = source_game_dir / name
        if candidate.is_file():
            yield candidate

    for name in INSTALLED_SUBDIRS:
        source_dir = source_game_dir / name
        if source_dir.is_dir():
            yield from sorted(path for path in source_dir.rglob("*") if path.is_file())


def find_newer_staged_edits(source_root: Path) -> list[Path]:
    source_game_dir = source_root / "content" / "baseoq4"
    staged_game_dir = source_root / ".install" / "baseoq4"
    if not source_game_dir.is_dir() or not staged_game_dir.is_dir():
        return []

    conflicts: list[Path] = []
    for source_path in iter_installed_source_files(source_game_dir):
        relative_path = source_path.relative_to(source_game_dir)
        staged_path = staged_game_dir / relative_path
        if not staged_path.is_file():
            continue

        source_stat = source_path.stat()
        staged_stat = staged_path.stat()
        if staged_stat.st_mtime_ns <= source_stat.st_mtime_ns:
            continue

        if filecmp.cmp(source_path, staged_path, shallow=False):
            continue

        conflicts.append(relative_path)

    return conflicts


def main(argv: list[str]) -> int:
    if os.environ.get("OPENQ4_INSTALL_OVERWRITE_STAGED_CONTENT") == "1":
        return 0

    args = parse_args(argv)
    source_root = Path(args.source_root).resolve()
    conflicts = find_newer_staged_edits(source_root)
    if not conflicts:
        return 0

    print(
        "error: refusing to stage install because newer edits under "
        "'.install/baseoq4' would be overwritten.",
        file=sys.stderr,
    )
    print(
        "Move these edits into 'content/baseoq4' or delete the staged file to discard "
        "them. Set OPENQ4_INSTALL_OVERWRITE_STAGED_CONTENT=1 to force an overwrite.",
        file=sys.stderr,
    )
    for relative_path in conflicts[:12]:
        rel = relative_path.as_posix()
        print(f"  .install/baseoq4/{rel} -> content/baseoq4/{rel}", file=sys.stderr)
    if len(conflicts) > 12:
        print(f"  ...and {len(conflicts) - 12} more staged file(s).", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
