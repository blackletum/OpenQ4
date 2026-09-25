# Level editor modernization

Target: a modern, integrated authoring environment at an idStudio-like standard.
This describes the desired product standard, not compatibility with proprietary
idStudio code, project files or tools.

## Audit and first architectural decision

The supported Meson build did not compile `src/tools/radiant/`, define
`ID_ALLOW_TOOLS`, or register the legacy `editor` command. Radiant's windowing,
message loop and widgets depend on Windows/MFC; its views manage WGL contexts
and issue direct OpenGL calls. Simply adding its sources to the build would
not restore a supported editor on SDL3 or across renderer modules.

The source audit also found these defects in the retained, unbuilt Radiant:

- Autosave, Save Copy and region export can clear `mapModified`, suppressing
  the warning for unsaved work in the actual working map.
- `Map_SaveFile` ignores the map writer's result, rotates the original before
  writing, calls `strlen` before checking for null, and borrows a save-dialog
  filename after the dialog's lifetime ends.
- Snapshot allocation probes names such as `level.map.0`, while the writer
  replaces that extension with `.map`; successive snapshots can use the same
  actual destination.
- Map loading frees the active document before parsing the replacement.
- Direct mutable globals couple the document, selection, UI, rendering and undo.

These remain **legacy-source defects**, not fixes shipped by this milestone.
The new implementation is a **separate experiment**: a portable authoring core
and an engine-native graphical workspace share commands and transactions.
`editorExperimental` opens it, while `mapEdit` exposes the same document service.
The legacy Radiant source, startup dispatch and `editor` registration remain
unchanged. Restoring its existing tool build is outside this milestone.
The experiment has separate GUI assets, `editorExperimental_*` settings and
an `editor_experimental/recovery/` directory. The broader modernization is paused
after these milestones at the user's request; further features require resuming.

## Milestone 1: document and persistence service

Implemented in `src/tools/leveleditor/`, with engine integration in
`src/framework/LevelEditor.cpp`:

- A portable C++ document with stable entity handles. Opening a new document
  invalidates old handles; deleting and undoing an entity restores its handle.
- Lossless source ownership for map versions 1–3: comments, whitespace, line
  endings, BOMs, primitive text and compiler directives survive unchanged.
  Authoring never runs runtime `Resolve`, `.ent` replacement or `.entx` merging.
- Entity properties and point-entity creation/deletion use undoable text edits.
  Ordinary property history retains changed spans instead of copying world
  geometry. Validated primitive ranges avoid rescanning geometry after ordinary
  property edits. Undo/redo tracks the saved revision, including branches after undo.
- New, Open and Close refuse to replace dirty work without explicit discard.
  A failed parse leaves the current document and history intact.
- Saving stages complete bytes, checks write and sync, then uses the engine's
  atomic file promotion. A changed existing source gets a `.map.bak` first.
  Failed publication keeps the dirty state and previous destination.
- Source-byte conflict detection refuses to overwrite a file changed by another
  editor. Save As and Save Copy refuse unrelated existing destinations.
- Recovery writes are separate from ordinary saves, do not clear dirty state,
  and skip unchanged revisions. Recovery opens as an unnamed dirty document,
  requiring a fresh destination to protect newer external work.
- The command adapter is available in client and dedicated builds and makes no
  graphics or input-capture calls. Periodic recovery is driven by the normal
  engine frame; normal shutdown attempts a final recovery before teardown.

Limits are deliberate: 64 MiB source, 65,536 entities, 16,384 properties per
entity, 256 history entries and 16 MiB history. An edit too large for undo is
rejected before mutation. Structural parsing validates document boundaries and
balanced primitive delimiters; it does **not** validate convex geometry, material
existence, patch dimensions, entity definitions or gameplay references. Those
remain compiler/validator responsibilities. Changing a geometry entity's origin
through a raw property is blocked until a geometry-aware transform exists.

The service does not yet offer brush/patch editing, transform
widgets, rename/reference repair, prefab instances, multi-document
workspaces, an asynchronous build manager or a separate play sandbox. Entity
handles are session-local and are not serialized into stock maps. History is
memory-only. Current source limits and conservative filename rules are initial
bounds, not claims of unlimited production-map support. File conflict checks
are optimistic; they do not acquire an interprocess lock against external writers.

## Milestone 2: initial graphical source workspace

`editorExperimental` opens a stock-art, localized workspace with a searchable entity/map/class
browser, property inspector, point-entity creation/deletion, save/copy, undo/redo
and an unsaved-work confirmation. Browser/inspector widths are resizable and
archived per user; layout changes preserve uncommitted inspector fields.
Confirmation dialogs block underlying controls and focus Cancel. A confirmed
replacement still reads and validates the next document before discarding the
current source or undo history. Console saves update the graphical dirty/path
state even when the source revision does not change.

The native GUI list now accepts a bounded `maxitems` setting. Existing GUIs keep
their 1,024-item default; the editor can expose all 65,536 supported entities.
Entity-definition browsing reads names without parsing every definition or
loading its models and textures.

The renderer-independent preview constructs brush windings and bounded patch
tessellation from source primitives. It owns a separate render world and its
models, without registering or replacing the game's global `_areaN` models.
It never runs authoring source through runtime entity replacement/Resolve.
Point entities use colored markers. Picking, orbit, pan, zoom and framing use
the document's stable entity handles; selection updates only the old/new render
entities. This preview uses a camera light, not compiled map lighting.

Preview updates compare exact primitive bytes and reuse unchanged models.
Property edits, undo and redo do not retessellate the map; point-origin changes
update the render entity. Flat patch rows/columns are collapsed. Discrete
materials retain separate surfaces so flare and sprite deformation is not
applied to merged geometry. Preview input is bounded (2 million source vertices,
128 sides per brush, odd patch dimensions 3–65 and bounded subdivisions).
Unsupported or incomplete primitives remain losslessly stored, are counted as
skipped in the preview, and do not terminate editing.

This work also fixed a pre-existing `Lexer` memory-constructor defect: its file
pointer was uninitialized, so destruction could attempt to close an invalid
file. The constructor now uses the normal initialization path, and text errors
honor the delegated lexer's nonfatal policy and error state.

This is still an **initial workspace**, not the completed idStudio-standard
milestone. Pane resizing is not docking. Geometry creation/transforms, actual
entity-model previews, orthographic views, camera persistence, typed properties, scene layers,
asynchronous indexing/builds, a play sandbox and full diagnostics are pending.
The first source preview build is synchronous. High-DPI, native mouse/keyboard
interaction, complete localized/responsive toolbar layouts and Linux/macOS
graphical operation still need qualification. Compact toolbar text has been
reduced to fit the English Save copy label at the tested sizes; other languages
still need visual qualification.

## Implementation order toward the target

1. **Complete the workspace.** Build on the integrated browser, inspector and
   source viewport: add docking, richer asset previews and linked diagnostics,
   reduce initial-load stalls, and qualify native input/high DPI and the other
   desktop platforms. Preserve layout per user through engine GUI/SDL3 events,
   with no WGL/global-window dependencies. Keep stock art and localization.
2. **Selection and geometry tools.** Add typed brush/patch adapters that preserve
   untouched source. Implement picking, orthographic and 3D views, grid/snapping,
   transform previews and one undo transaction per completed gesture. Test
   texture locking, mixed primitive selections, cancellation and large maps.
3. **Asset and entity workflows.** Add asynchronous material/model/entity-def
   search and previews, typed property validation, safe entity rename with
   reference updates, layers/visibility, groups and prefab workflows. Asset
   indexing must not block frame presentation or eagerly load all GPU resources.
4. **Build, diagnostics and play.** Run dmap/AAS through a job manager with
   progress, cancellation and entity/primitive-linked diagnostics. Use isolated
   runtime state for play testing, restore editor camera/selection on exit, and
   keep a failed build from replacing the previous playable output.
5. **Production qualification.** Exercise retail SP/MP round trips, large maps,
   forced I/O failures, external edits, recovery, repeated undo/redo, renderer
   switches and Windows/Linux/macOS workflows. Record startup, selection,
   viewport and edit latency budgets before describing the editor as ready for
   production. Graphical interaction requires separately authorized manual or
   automated input tests.

Each milestone must deliver a usable end-to-end workflow. A panel mockup or a
menu-only startup does not satisfy the graphical milestone.

## Validation

`openq4-editor-document` compiles the production portable document and workspace
with deterministic fault-injected storage. It covers byte-exact round trips,
stable handles, history eviction, randomized edits, dirty/savepoint semantics,
malformed replacement loads, invalid paths, partial-write/sync failures, backup
and promotion failures, external-change conflicts, save copies and recovery.
Additional `.map` arguments to its executable verify lossless stock-map load,
world-property edit and undo.

`tools/tests/level_editor_runtime.py` runs the staged client with stock SP assets,
an isolated save/development root and disabled mouse/controller input. It edits
and saves `game/airdefense1`, checks the original/copy/undo/backup/recovery bytes,
enters that saved map and requests an engine screenshot. It neither sends input
events nor captures the desktop. It also rejects a malformed numeric entity
handle and verifies that the failed edit leaves the source unchanged. See the
[user guide](../user/level-editor.md)
for command usage.

Local qualification on 24 September 2026:

- Full Windows x64 build and staging passed. The Meson native target passed
  2,928 assertions for the initial document milestone; a separate Clang build
  and Linux GCC ASan/UBSan build passed.
- All 79 distinct map paths found in the installed PK4s passed byte-exact
  load/serialize, world-property edit and undo checks (3,323 assertions including
  the core suite). These cover source documents, not full-map gameplay parity.
- The optimized standalone Windows build measured a median 1.071 ms and maximum
  6.503 ms for one world-property edit across those maps. This is a core-operation
  measurement on this machine, not a viewport or end-to-end editor latency claim.
- The staged client passed the authoring/save/recovery sequence and subsequent
  `game/airdefense1` SP gameplay on OpenGL and Vulkan. Both produced engine
  captures, returned exit code zero, and logged no `WARNING:` or `ERROR:` lines.
  The tested source has 2,195 entities and 11,793 primitives.
- Initial visual inspection of the Vulkan capture found an **inverted 3D scene with an
  upright HUD** in the currently modified renderer checkout. The OpenGL capture
  is correctly oriented. Those initial results qualified document/persistence
  commands and gameplay entry only. Subsequent workspace captures use the
  concurrent renderer fixes and are upright. No renderer source was changed
  for this editor work.
- Logs, launch arguments, output hashes and captures are retained under
  `.tmp/editor-modernization/final-gl/` and `final-vk/`; native and stock
  round-trip logs are in the parent directory. Generated caches, extracted stock
  sources and test map outputs are also retained: automatic approval review
  rejected the attempted recursive cleanup with the reason "blocked by policy".

An unrelated loose `q4base/maps/game/airdefense1.map` in the retail installation
differs from the packaged source: `func_static_53232` has origin Z `36.125` instead
of `36`. It was left untouched. The final runtime fixture extracts the original
PK4 source into its isolated profile so this override cannot skew the check.
Earlier harness attempts stopped at the loading-continue screen, or used an
unquoted screenshot path; the final fixture uses the established after-map
script/auto-continue options and a quoted engine screenshot command.

### Graphical workspace qualification

`tools/tests/level_editor_workspace.py` drives the same semantic controller
actions as the GUI widgets. It does not inject input events. On 24 September
2026, `.tmp/editor-workspace/closeout-gl/` and `closeout-vk/` passed the
automated controller/document checks below. Visual qualification is partial,
with the restart defect recorded separately:

- Full Windows build/staging and unchanged runtime manifests during each run.
- All 2,195 stock entities are exposed by the native list. The preview contains
  2,097 render entities and 142,982 triangles with no skipped stock primitives.
  Removing redundant patch subdivisions reduced the earlier 363,142 triangles.
- Framed-marker picking, orbit/pan controller calls, saved pane limits and
  preservation of draft inspector fields during resizing.
- Property edit/undo/redo reuse all preview models. The final concurrent runs
  recorded 24–37 ms for these preview updates, versus initial builds of 2.63–4.45
  seconds. Earlier isolated runs recorded 10 ms updates. These measure preview
  synchronization only, not input-to-frame latency or a production performance
  guarantee; initial-load work remains synchronous.
- Invalid geometry and overflowing texture coordinates are skipped without
  ending the session. A confirmed attempt to open an invalid replacement leaves
  the dirty source intact. The native suite also checks retained undo/savepoints.
- Lossless undo, saved edits, copy/backup bytes, cancellation of unsaved-work
  replacement, protected modal actions, renderer restart and resize to 1600x900.
- Saved-map SP gameplay, reopening the workspace while that game remains loaded,
  and return to its menu. Each run writes nine engine screenshots, including
  settled menu, immediate/settled restart and workspace reactivation views.
- Runtime command listings verify `editorExperimental` is separate and has not
  replaced `editor`. The unchanged Radiant launcher/source check is recorded in
  `.tmp/editor-workspace/legacy-preservation.json`. The final document workflow
  in `closeout-document/` also passes save/copy/backup/recovery and SP gameplay
  with no warnings or errors. Recovery uses the portable lowercase directory
  `editor_experimental/recovery/`.
- The standalone Windows and Linux GCC ASan/UBSan suites pass 2,938 assertions.
  Language-table encoding and the existing native menu-control contract pass.

The harness reports the known retail menu warning for the absent
`models/monsters/burn_misc_sm` image separately; all other unexpected warnings,
errors or rejected controller actions fail. The malformed-primitive and invalid
replacement diagnostics are intentionally asserted. The OpenGL/Vulkan workspace
captures are upright. This does not establish all-material renderer parity.

Final visual review found a remaining **full-video-restart label defect** on
both renderers: some workspace labels disappear and stay missing after 30 more
frames. Reactivating the workspace restored them on Vulkan but did not reliably
restore them on OpenGL. This is not qualified as fixed; the initial and resumed
gameplay workflows still pass. The English Save copy label fits, and the new
experimental badge is visible before restart. The broader work is paused;
diagnosis of the restart-specific text failure remains a follow-up item.

Earlier failed runs, the lexer-crash dump/stack, and build logs remain in
`.tmp/editor-workspace/`. Native pointer/keyboard interaction was not automated:
the project requires specific input-control permission, which was not granted.
Only controller calls and engine screenshots were used. No OS capture was taken.
Automatic approval review also rejected cleanup of this area's generated test
caches with "blocked by policy"; no alternative deletion was attempted.
