# DDS Texture Replacements

openQ4 can use DDS texture-pack replacements without changing the original Quake 4 materials. This includes dhewm3-style `dds/` packs and same-folder replacements used by some Quake 4 packs.

## Enable replacements

Set `image_usePrecompressedTextures` before loading a map:

- `0` disables automatic DDS replacements.
- `1` uses any supported DDS replacement. DXT1, DXT5, and BC7 data can upload directly when the renderer supports the format.
- `2` uses only BC7/BPTC replacements and falls back to the original image when a BC7 replacement is unavailable or invalid.

BC7 requires a renderer with BPTC support. Run `gfxInfo` and look for the BC7/BPTC capability line if a pack is not being selected.

## Where files belong

The preferred layout is the dhewm3-compatible `dds/` shadow tree inside the active game directory or a PK4:

```text
baseoq4/
  dds/
    textures/
      stone/
        wall01.dds
```

For an image named `textures/stone/wall01`, openQ4 first checks `dds/textures/stone/wall01.dds`. A loose file therefore belongs at `baseoq4/dds/textures/stone/wall01.dds`; a PK4 should contain the same `dds/textures/...` path at its root.

openQ4 uses dhewm3's complete image-program conversion too. For example:

```text
heightmap(models/monsters/foo_local, 4)
    -> dds/heightmap/models/monsters/foo_local 4.dds
```

Texture-pack tools should preserve that mapping for composed programs such as `heightmap`, `addnormals`, `add`, and `scale`. For simple image references, openQ4 also checks the extension-normalized shadow path and then a `.dds` beside the original `.tga` or `.jpg`. The canonical `dds/` path wins when more than one replacement exists.

## BC7 file requirements

Supported BC7 files are single 2D textures using either:

- a DX10 header with `DXGI_FORMAT_BC7_UNORM` or `DXGI_FORMAT_BC7_UNORM_SRGB`; or
- a common `BC7` FourCC variant.

Texture arrays, cube maps, volume textures, invalid dimensions, and truncated declared mip chains are rejected safely. Authored mip levels are uploaded directly. Texture downsizing skips leading authored levels, and images using an unmipped sampler upload only their base level.

Doom 3-style bump maps store the red normal channel in alpha (RXGB/XGBR layout). BC7 normal maps intended for this path must use the same layout; Daniel Gibson's customized `bc7enc` supports it with `-r2a`.

## Troubleshooting

Use these commands when checking a pack:

```text
image_usePrecompressedTextures 2
image_showPrecompressedTextures 1
reloadImages all
```

The log then prints the exact DDS path selected for each replacement. `listImages` identifies loaded BC7 images, while `imageDDSSelfTest` validates the built-in dhewm3 naming conversion and BC7 header checks.

During development, an automatic DDS replacement older than its original source is ignored so stale generated data cannot hide a newer texture edit. Rebuild or retimestamp the DDS after changing its source image.
