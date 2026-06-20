#!/usr/bin/env python3
"""Build a single openQ4 runtime PK4."""

from __future__ import annotations

import argparse
import filecmp
import shutil
import sys
from pathlib import Path

from openq4_pak import OPENQ4_PACK_NAMES, create_game_pk4


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build one openQ4 runtime PK4.")
    parser.add_argument("--pak-name", required=True, choices=OPENQ4_PACK_NAMES, help="Output pack name.")
    parser.add_argument("--source-dir", required=True, help="Source content directory for this pack.")
    parser.add_argument("--out", required=True, help="Output PK4 path.")
    parser.add_argument(
        "--stage-out",
        default="",
        help="Optional additional baseoq4/<pak>.pk4 path to copy for direct builddir launches.",
    )
    return parser.parse_args(argv[1:])


def copy_if_changed(source: Path, destination: Path) -> None:
    if destination.is_file() and filecmp.cmp(source, destination, shallow=False):
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    pak_name = args.pak_name
    source_dir = Path(args.source_dir).resolve()
    pak_out = Path(args.out).resolve()

    try:
        result = create_game_pk4(source_dir, pak_out, pak_name=pak_name)
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if result.added_files == 0:
        print(f"error: {pak_name} packaging found no eligible files after filtering", file=sys.stderr)
        return 1
    if result.missing_required:
        print(f"error: {pak_name} packaging is missing required runtime files:", file=sys.stderr)
        for relative_path in result.missing_required:
            print(f"  - {relative_path}", file=sys.stderr)
        return 1

    if args.stage_out:
        copy_if_changed(pak_out, Path(args.stage_out).resolve())

    print(f"built {pak_name}: md5={result.md5_hex} size={result.size_bytes} files={result.added_files}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
