#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image


TILE_SUFFIXES = {
    "": (1, 1),
    "_left": (0, 1),
    "_right": (2, 1),
    "_top": (1, 0),
    "_bottom": (1, 2),
}

SOURCE_IMAGES = {
    "railgun_scope": {
        "default": Path(r"E:\railgun_scope_fuller.tif"),
        "mode": "RGBA",
        "dds_pixel_format": "DXT5",
    },
    "railgun_scope_glow": {
        "default": Path(r"E:\railgun_scope_glow_fuller.tif"),
        "mode": "RGB",
        "dds_pixel_format": "DXT1",
    },
}


def crop_tile(image: Image.Image, tile_x: int, tile_y: int) -> Image.Image:
    if image.width % 3 != 0 or image.height % 3 != 0:
        raise ValueError(f"expected 3x3 source image, got {image.width}x{image.height}")

    tile_width = image.width // 3
    tile_height = image.height // 3
    left = tile_x * tile_width
    top = tile_y * tile_height
    return image.crop((left, top, left + tile_width, top + tile_height))


def write_texture_pair(image: Image.Image, output_base: Path, dds_pixel_format: str) -> None:
    output_base.parent.mkdir(parents=True, exist_ok=True)

    tga_path = output_base.with_suffix(".tga")
    dds_path = output_base.with_suffix(".dds")

    image.save(tga_path, format="TGA")
    image.save(dds_path, format="DDS", pixel_format=dds_pixel_format)

    print(f"wrote {tga_path}")
    print(f"wrote {dds_path}")


def export_tiles(source_path: Path, stem: str, mode: str, dds_pixel_format: str, output_dir: Path) -> None:
    if not source_path.is_file():
        raise FileNotFoundError(f"missing source image: {source_path}")

    with Image.open(source_path) as image:
        source_image = image.convert(mode)
        for suffix, (tile_x, tile_y) in TILE_SUFFIXES.items():
            tile = crop_tile(source_image, tile_x, tile_y)
            write_texture_pair(tile, output_dir / f"{stem}{suffix}", dds_pixel_format)


def main() -> int:
    repo_root = Path(__file__).resolve().parents[2]
    source_game_dir = repo_root / "content" / "baseoq4"
    staged_game_dir = repo_root / ".install" / "baseoq4"

    parser = argparse.ArgumentParser(
        description="Export railgun scope 3x3 source art into center/edge TGA and DDS tiles."
    )
    parser.add_argument(
        "--scope-source",
        type=Path,
        default=SOURCE_IMAGES["railgun_scope"]["default"],
        help="path to the expanded 3x3 railgun scope source image",
    )
    parser.add_argument(
        "--glow-source",
        type=Path,
        default=SOURCE_IMAGES["railgun_scope_glow"]["default"],
        help="path to the expanded 3x3 railgun glow source image",
    )
    parser.add_argument(
        "--output-dir",
        action="append",
        type=Path,
        default=[],
        help="tile output directory; may be specified more than once",
    )
    args = parser.parse_args()

    output_dirs = args.output_dir or [
        source_game_dir / "gfx" / "guis" / "hud" / "reticles",
        staged_game_dir / "gfx" / "guis" / "hud" / "reticles",
    ]

    source_paths = {
        "railgun_scope": args.scope_source,
        "railgun_scope_glow": args.glow_source,
    }

    for output_dir in output_dirs:
        for stem, settings in SOURCE_IMAGES.items():
            export_tiles(
                source_paths[stem],
                stem,
                settings["mode"],
                settings["dds_pixel_format"],
                output_dir,
            )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
