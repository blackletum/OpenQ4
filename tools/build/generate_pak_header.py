#!/usr/bin/env python3
"""Generate the openQ4 runtime PK4 integrity header."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from openq4_pak import PAK0_NAME, PAK1_NAME, format_openq4_paks_header, inspect_game_pk4, write_text_if_changed


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate openQ4 PK4 checksum macros.")
    parser.add_argument("--pak0", required=True, help="Built pak0.pk4 path.")
    parser.add_argument("--pak1", required=True, help="Built pak1.pk4 path.")
    parser.add_argument("--header-out", required=True, help="Generated C/C++ header path.")
    return parser.parse_args(argv[1:])


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    try:
        pak0_result = inspect_game_pk4(Path(args.pak0).resolve(), pak_name=PAK0_NAME)
        pak1_result = inspect_game_pk4(Path(args.pak1).resolve(), pak_name=PAK1_NAME)
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    write_text_if_changed(
        Path(args.header_out).resolve(),
        format_openq4_paks_header(pak0_result, pak1_result),
    )
    print(
        f"generated openq4_paks_generated.h: "
        f"pak0={pak0_result.md5_hex} pak1={pak1_result.md5_hex}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
