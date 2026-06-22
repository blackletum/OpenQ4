#!/usr/bin/env python3
"""Write a content manifest for an openQ4 runtime PK4 source directory."""

from __future__ import annotations

import sys
from pathlib import Path

from openq4_pak import format_pk4_source_manifest, write_text_if_changed


def normalize(path: str) -> str:
    return path.replace("\\", "/").strip("/")


def main(argv: list[str]) -> int:
    if len(argv) < 5:
        print(f"usage: {argv[0]} <source-root> <pak-name> <content-subdir> <manifest-out>", file=sys.stderr)
        return 2

    source_root = Path(argv[1]).resolve()
    pak_name = argv[2].strip()
    subdir = normalize(argv[3])
    content_root = (source_root / subdir).resolve()
    manifest_out = Path(argv[4]).resolve()

    if not content_root.is_dir():
        print(f"error: pack source directory not found: {content_root}", file=sys.stderr)
        return 1

    try:
        manifest = format_pk4_source_manifest(source_root, content_root, pak_name)
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if len(manifest.splitlines()) <= 3:
        print(f"error: pack source directory has no eligible files: {content_root}", file=sys.stderr)
        return 1

    write_text_if_changed(manifest_out, manifest)
    print(f"wrote {pak_name} source manifest: {manifest_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
