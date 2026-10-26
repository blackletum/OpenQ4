# Level-Load Cache

> **Development documentation:** This page follows the current source tree and may describe changes newer than the public download. See [release status and released-version guides](release-status.md) before using it with a release package.

openQ4 learns which source files a map actually uses and can prepare those
bytes during a later matching load. Worker jobs read or inflate the selected
source, validate supported container framing and whole-buffer integrity, then
hand immutable bytes back to the normal owner. openQ4 also keeps validated local
binary caches for parsed models, classic map worlds, collision data, and MD5
animations.

The experimental cache is disabled by default because current stock-map
measurements do not yet prove a reliable improvement over classic loading. If
enabled, the first visit still uses the installed Quake 4 or mod sources and may
take longer while private cache files are written. A later visit can reuse work
only when the map, game mode, entity filter, active search/PK4 set, source
identity, and load-affecting settings still match.

## Safety and compatibility

The retail or mod file selected by the normal virtual filesystem always remains
authoritative. openQ4 resolves that file through the usual search and pure-PK4
rules before it may substitute already prepared, immutable bytes. Parsing,
validation, and renderer/audio upload stay with the normal resource owner.
The worker framing step recognizes RIFF/WAVE, Ogg, PNG, and JPEG sources; DDS
and other formats remain opaque until their ordinary owner parses them.

Generated model, world, collision, and animation readers build private state
first and publish it only after the whole payload is valid. A stale, truncated,
corrupt, oversized, wrong-version, or otherwise mismatched cache is ignored;
the affected private file is removed where appropriate and openQ4 loads the
original source instead. Packed MD5RProc map worlds remain on their source path
rather than being flattened into an incompatible world cache.

These files are disposable runtime data. They are not replacement assets, do
not change pure-server package negotiation, and must not be copied into retail
Quake 4 PK4s.

## Location

Caches are written below the active game directory under `fs_savepath`, normally
`<fs_savepath>/baseoq4/`:

```text
generated/manifests/       learned per-map source lists
generated/models/          parsed render-model data
generated/worlds/          finalized classic .proc world data
generated/collision/       parsed/generated collision data
generated/animations/      parsed MD5 animation data
```

With the standard unified game directory, the full animation root is
`<fs_savepath>/baseoq4/generated/animations/`.

Manifest and model/world/collision filenames are opaque hashes of their
validated keys. Animation files retain the normalized source path and add a
`.banim` suffix. Moving cache files between installations is neither necessary
nor recommended; a different source package or setting can intentionally cause
a clean miss and rebuild.

Animation v3 stores fixed-width values and floating-point arrays in a portable
byte order; x64 and ARM64 builds use the same format. It identifies the exact
source that the parser would select (`<name>c` when binary reads are enabled and
that compiled source exists, otherwise the requested `.md5anim`), uses a content
CRC for loose sources, and verifies a whole-record length/CRC trailer before
parsing cache fields. A stale, truncated, corrupt, or mismatched cache is ignored
and the selected source animation is parsed.

Learned preload identity for a loose file uses its normalized path, size, and
timestamp. A tool that deliberately preserves both size and timestamp while
rewriting a loose file can evade that preparation check; close openQ4 and remove
the generated data, or disable `com_levelLoadCache`, when testing such edits.
PK4 members remain tied to the containing archive checksum.

## Controls

The master switch defaults off. The individual controls retain their enabled
defaults so an intentional experiment needs only the master switch:

```text
com_levelLoadModernization 0
com_levelLoadCache 1
com_levelLoadCacheWrite 1
com_levelLoadPreload 1
com_levelLoadCacheReport 0
```

- Set `com_levelLoadModernization 1` before loading a map to opt into all
  individually enabled cache paths. This also permits the SP/MP animation-cache
  controls below to take effect.
- Set `com_levelLoadModernization 0` for the classic loading path. This master
  rollback overrides archived values from builds that previously enabled the
  individual controls by default.
- Set `com_levelLoadPreload 0` before loading a map to disable learned source
  preparation while retaining valid generated model/world/collision caches.
- Set `com_levelLoadCacheWrite 0` to stop new manifests and generated
  model/world/collision files from being written while valid existing files can
  still be read.
- Set `com_levelLoadCache 0` before loading a map to bypass learned manifests
  and generated model/world/collision caches completely.
- Set `com_levelLoadCacheReport 1` for per-map hit, miss, replay, byte,
  framing/integrity decode, cancellation, and staging/decoded-memory diagnostics
  in the log.

Advanced staging limits are available as
`com_levelLoadPreloadMaxEntries`, `com_levelLoadPreloadMaxFileMB`,
`com_levelLoadPreloadMaxStagingMB`, `com_levelLoadPreloadMaxDecodeMB`,
`com_levelLoadPreloadReadChunkKB`, and
`com_levelLoadPreloadDecodeChunkKB`. Their defaults are deliberately bounded;
increasing them can raise transient memory use and is not normally needed. The
normal owner can allocate its parsed representation in addition to the retained
source bytes, so these controls are pipeline limits rather than a cap on total
level-load memory.

Animation caches are owned separately by the SP and MP game modules. Their
individual controls default to enabled, but reads and writes occur only while
`com_levelLoadModernization 1`:

```text
g_useGeneratedAnimCache 1
g_writeGeneratedAnimCache 1
```

- Set `g_useGeneratedAnimCache 0` to bypass existing `.banim` files.
- Set `g_writeGeneratedAnimCache 0` to prevent new `.banim` files.

## Clean rebuild and rollback

To force a clean rebuild, close openQ4 and delete one or more of the
`generated/` subdirectories listed above under the active `fs_savepath`. openQ4
will recreate needed private data from the installed sources.

For a complete source-path comparison, leave `com_levelLoadModernization 0`.
No framework or animation cache is read or written in that mode, regardless of
older archived values for the individual controls.

Never delete the original `.proc`, `.cm`, model, image, sound, or
`models/**/*.md5anim` files from the Quake 4 installation. If a map still fails
with all generated caches disabled, the problem is not caused by this cache
layer; include the active `fs_savepath` and `logs/openq4.log` when reporting it.

Developers and testers can find the exact key, ownership, validation, and
pending evidence contract in
[Level-Load Cache Modernization](../dev/loading-cache-modernization.md).
