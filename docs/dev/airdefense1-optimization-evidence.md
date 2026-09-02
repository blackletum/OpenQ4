# `game/airdefense1` Optimization Evidence

This note records the 2026-08-24 Windows x64 optimization pass performed after
the idTech 5 modernization implementation. The complex opening scene in
`game/airdefense1` was used throughout. All game runs were bordered windowed at
1280x720, used the engine's registered screenshot command, and required actual
SP gameplay rather than main-menu startup. No mouse/keyboard input was
controlled or captured.

## Findings and changes

Two modernization regressions were isolated:

1. Presentation interpolation walked every spawned entity on every authoritative
   60 Hz tick. The opening cinematic fast-forward executes more than 11,600
   simulation ticks in one host frame, turning this into an
   `spawned entities x skipped ticks` workload even though none of the
   intermediate poses can be presented. Sampling now exits during cinematic
   fast-forward and, on visible frames, visits active movers, the bounded
   physics-team members those movers own, plus the previous presentation
   members that need one final cleanup sample.
2. Maps without baked light-grid assets built a 22,024-point runtime probe
   layout during ordinary loading. That layout is useful only for baking and
   debug preparation. Ordinary runtime setup now keeps an empty grid; bake
   entry points create the layout on demand.

The classic source-loading path remains the default. This pass did not mask the
regression by enabling `com_levelLoadModernization`; that experiment remains
default off until its own cold/warm qualification is favourable.

## Attributable measurements

The direct `g_airdefense1SkipProbe` holds the simulation rate and endpoint
constant, so it is the most useful causal comparison:

| Measurement | Before | After | Change |
|---|---:|---:|---:|
| Opening fast-forward wall time | 7,113 ms | 5,770 ms | -1,343 ms (-18.9%) |
| Final scripted game time | 187.008 s | 187.008 s | exact |
| Missing-light-grid setup | 357 ms | 1 ms | -356 ms (-99.7%) |

The final implementation preserves the stock 60 Hz simulation step. A bounded
30 Hz experiment reduced the fast-forward to 2,968 ms and reached a
near-identical end state, but it was rejected because broader script, physics,
and frame-command parity was not established.

## End-to-end and frame evidence

The isolated gameplay benchmark reported the following before/after snapshots:

| Measurement | Before | After |
|---|---:|---:|
| Settled gameplay total | 33,121 ms | 25,091 ms |
| Render-world phase | 3,210 ms | 2,323 ms |
| Opening settle phase | 8,324 ms | 5,874 ms |
| Steady presentation rate | 110.8 Hz | 120.4 Hz |
| Frame pacing P95 / P99 | 13 / 13 ms | 11 / 12 ms |

The end-to-end total is indicative rather than wholly attributable: each run
used an isolated save path, generated its own binary-image cache, and could not
reset the operating-system file cache. The controlled measurements above are
the causal acceptance values.

A final diagnostic capture showed CPU P50/P95/P99 of
7,151/9,163/10,533 microseconds and GPU P50/P95/P99 of
2,714/3,531/3,679 microseconds. `airdefense1` is therefore CPU/front-end limited
on this system. The optimization targets simulation/presentation and setup
work instead of lowering GPU image quality.

## Advanced screen-space load

With froxel volumetrics, SSR, and SSGI enabled together, real gameplay passed
with zero shader, framebuffer, OpenGL, Vulkan-validation, VUID, Vulkan-call,
fatal, or engine-error counters:

| Backend | Final hardened rate | Final P95 / P99 | Observed post-audit range |
|---|---:|---:|---|
| OpenGL | 117.3 Hz | 11 / 11 ms | 102.9--117.3 Hz; engine TGA, all leaves together; each leaf also passed independently |
| Vulkan | 125.8 Hz | 10 / 11 ms | 114.1--125.8 Hz; engine TGA, all leaves together |

The live scene contains time-dependent BSE smoke and actors, so independently
launched screenshots are not exact-hash fixtures. The shared native contract
therefore enforces exact master rollback by publishing a zeroed eight-float
packet, while runtime master-off gameplay verifies the complete integration
path. The range retains both final-build samples rather than selecting only the
faster run; scene timing and system cache state vary between process launches.

## Final staged-package regression

The final x64 rebuild compiled all 1,210 engine/client/dedicated and GL/Vulkan
targets plus both companion game modules, staged `.install/`, and passed all
11 Meson native tests. The post-rebuild windowed gameplay audit retained an
engine TGA for every role and reported no shader, framebuffer, GL,
Vulkan-validation, VUID, Vulkan-call, fatal, or engine-error counters:

| Route | Rate | P95 / P99 |
|---|---:|---:|
| OpenGL `game/airdefense2` defaults | 189.6 Hz | 7 / 8 ms |
| Vulkan `game/airdefense2` defaults | 240.2 Hz | 6 / 6 ms |
| OpenGL `mp/q4dm1` listen server / auto-joined client | 174.0 / 180.7 Hz | 7 / 7 and 7 / 8 ms |
| Vulkan `mp/q4dm1` listen server / auto-joined client | 224.8 / 189.1 Hz | 6 / 6 and 7 / 7 ms |

The final clean-asset compatibility run used all 40 verified retail PK4s and
zero loose `q4base`/`q4mp` files. SP capture, SP demo playback, the pure MP
server, and its auto-joined client all passed from the staged package. The
packaged openQ4 overlay PK4s were intentionally present, so this proves retail
asset compatibility rather than an overlay-free executable. Representative
OpenGL/Vulkan SP, screen-effect, and MP engine screenshots were reviewed for
black output, invalid geometry, missing HUD, and obvious presentation defects;
none were found.

## 2026-08-31 camera-sweep pass

This pass added a repeatable camera-motion capture for the same scene, then ran
optimization and robustness rounds against it. All runs are Windows x64,
bordered windowed, OpenGL, uncapped presentation, shipping renderer defaults,
and stock retail assets. No mouse or keyboard input was synthesized.

### The sweep capture

Measuring a fixed spawn view only exercises one frustum. `benchmarkViewSweep`
pans the local player's view a requested arc over a requested duration, driven
entirely from game time inside `idPlayer::UpdateViewAngles`, so the same sweep
is reproduced regardless of host frame rate and no operating-system input is
involved. The completion line reports the arc it actually walked.

The acceptance capture skips the loading-screen continue gate
(`com_skipLoadingContinue 1`) and the opening cinematic
(`g_autoSkipCinematics 1`), settles, then samples a full turn:

```
python tools/tests/renderer_gameplay_benchmark.py --cases sp-airdefense1 \
  --tiers auto --maxfps 0 --swap-intervals 0 --display-modes windowed \
  --render-api gl --pacing-only --no-gpu-timers --settle-frames 360 \
  --exec-command "benchmarkViewSweep 360 12000" --sample-msec 13000
```

### Frame rate over a full 360-degree turn

Three consecutive passing captures on the final build at 1280x720, each
sampling one complete turn from the starting area:

| Run | Samples | Average | P50 | P95 | P99 | Worst frame |
|---|---:|---:|---:|---:|---:|---:|
| 1 | 3,472 | 284.1 Hz | 4 ms | 6 ms | 7 ms | 12 ms |
| 2 | 3,544 | 298.7 Hz | 4 ms | 6 ms | 7 ms | 11 ms |
| 3 | 3,363 | 282.8 Hz | 4 ms | 6 ms | 7 ms | 8 ms |

Median 284.1 Hz, and six passing captures taken across this pass span
282.8--313.1 Hz with an unchanged 6 / 7 ms P95 / P99. The worst single
frame in any capture was 14 ms, so the turn holds well above the 100 FPS
acceptance target throughout rather than averaging over a stall. The same
sweep at 1920x1080 reported 310.8 Hz with an
identical 6 / 7 ms P95 / P99 and a 10 ms worst frame, which matches the earlier
finding that this scene is CPU/front-end limited rather than fill limited.
Every run reported the sweep completing its full arc, and the end-of-sweep
engine screenshots show the expected geometry, lighting, and HUD.

### Where the load time goes

Load timings need a save path that survives between runs. The gameplay
benchmark deliberately requires a fresh output directory, so each of its runs
regenerates the binary image cache and reports a first-visit load. A player
pays that once. Warm medians of three runs, stock defaults:

| Phase | msec |
|---|---:|
| Game media precache (models, sounds, animations, entity defs) | 4,140 |
| Level image loading (1,718 files, 213 MiB) | 2,538 |
| Cinematic fast-forward, only paid when the opening cinematic is skipped | 1,876 |
| Render-world `.proc` parse | 1,355 |
| Collision `.cm` parse | 805 |
| Player spawn and remainder | ~570 |
| **Total** | **11,282** |

The load is almost entirely single-threaded CPU work: one complete run measured
18.8 s of process CPU against 21.8 s of wall time on a 20-core host, so roughly
nineteen cores are idle throughout. Parallel asset decode, not a faster parser,
is the structural lever here.

### Optimizations landed

Both fixes are in the level-load cache manager and are behaviour neutral.

1. **Semantic-hint matching was quadratic.** Every opened source was matched
   against the whole recorded hint vector, and each comparison built up to
   three temporary `std::string`s. `game/airdefense1` records 4,428 hints, so
   the recording pass scaled as sources times hints. Hints are now indexed by
   the three keys the match rules actually use (exact name, compiled-suffix
   name, extension-stripped stem), and lookup takes the highest matching hint
   index, which is the hint the old reverse scan stopped on.
2. **Each semantic record re-opened its source.** A name already resolved in
   the current generation is no longer resolved a second time; the repeat open
   could only yield the identity the first one already learned.

Measured on the level-load cache path (`com_levelLoadModernization 1`), warm
medians of three runs:

| | Before | After | Change |
|---|---:|---:|---:|
| Total load | 11,893 ms | 9,010 ms | -2,883 ms (-24.2%) |
| Level image loading | 4,442 ms | 2,209 ms | -2,233 ms (-50.3%) |
| First-visit total | 21,040 ms | 18,044 ms | -2,996 ms (-14.2%) |

### `com_levelLoadModernization` remains default off

With the fixes above, that path is now the fastest warm configuration measured
for this map: 9,010 ms against 11,282 ms for the shipping default, a 20.1%
reduction, driven by the generated world (1,355 -> 392 ms) and collision
(805 -> 152 ms) caches.

It is still slower on a first visit: 18,044 ms against 11,418 ms, because that
visit both writes the generated caches and learns the replay manifest. Setting
`com_levelLoadCacheWrite 0` only recovers about 1.3 s of that, so the remaining
first-visit cost is the cache-miss and identity resolution work rather than the
writes themselves.

That trade -- roughly 6.6 s worse once against 2.3 s better every time after --
is exactly the cold/warm qualification this feature's promotion gate is waiting
on, so the default is unchanged. Reducing the first-visit cost is the work that
would justify promoting it.

### Robustness

- The classic `.proc` `ParseModel` path now range-checks its file-provided
  vertex and index counts and every index it reads. `ParseShadowModel` and the
  binary render-world cache already applied those predicates; the text draw
  surface path did not, and `FinishSurfaces` dereferences every index while
  deriving tangents and silhouette edges. Verified against `game/airdefense1`,
  `game/airdefense2`, `game/storage1`, `game/medlabs`, and `game/mcc_landing`:
  no stock surface is rejected.
- `idImageManager::LoadLevelImages` bounds its fill loop against the array it
  pre-sized from `CountPendingLevelLoads`. The two share a predicate today, so
  the bound cannot trip; without it a future divergence would be a silent heap
  overflow rather than a dropped image.
- `benchmarkViewSweep` clamps both console arguments, so a typo cannot produce
  a NaN yaw or a sweep that never ends.

### Diagnostics

- `g_frametime` now prints the per-frame section breakdown that previously only
  the airdefense1 skip probe could collect (view setup, AI, PVS, network event
  queue, gravity, BSE start/end, active-list sort). This is what identified the
  1.85 s "first settle frame" as the cinematic fast-forward loop -- roughly
  11,600 simulation ticks inside one host frame -- rather than a slow frame.
- The sweep's completion line reports the yaw it started from and reached, so a
  capture proves the camera panned instead of trusting the request.

### Font parity failure found while validating, and fixed

`renderer_validation_matrix` failed its `renderer-foundation-selftests` case
on `uiFontParitySelfTest`: the HUD radio marine cases were a pixel out in line
height, baseline, glyph y, and overhang, the three radio strings were two
pixels narrow, and the loading title measured 234 px against a retail 223 px,
moving its right-aligned x by 11 px.

It was not from the optimization work -- rebuilding with those engine changes
reverted reproduced it exactly -- but from `SetFontByScale` choosing its atlas
from a viewport-enlarged scale. That selection is also what `TextWidth`,
`MaxCharHeight`, and the `DrawText` advances read, so above roughly 1.5x
enlargement the hand-authored retail `.fontdat` atlases started reporting
different metrics. It reproduced only on a large window, which is why 1280x720
and 640x480 both passed and the user's own 2538x1312 window did not.

A scalable font rasterises its three slots from one face at 12/24/48 point, so
their normalised metrics agree and a larger slot only adds resolution. The
enlargement now applies to those fonts only; the retail atlases select on the
authored scale and keep exact parity. The matrix is 36/36 again.

### Save loss found while validating, and where it came from

Autosaving during `game/airdefense1` aborted with
`idWinVec4::WriteToSaveGame: refusing non-finite y`, which unloads the running
map, so the player lost both the save and the session. The refusal was replaced
with per-component sanitizing, and the guards were given enough context to name
the offending variable, which identified it as a GUI script parameter rather
than a window colour. That left the source of the non-finite value open.

It was not an expression result. `idGuiScript::FixupParms` converts the two
value operands of a `transition` into freshly allocated `idWinVec4`s, and
`idWinVec4::Set` fills them through `sscanf` without checking how many
components matched. Stock GUIs transition scalars -- `transition "L1::rotate"
"-180" "-270" "1000"`, `transition "access::forecolor_w" ".7" "1" "0"` -- so one
component parses and three keep whatever the object already held. `idVec2`,
`idVec3` and `idVec4` deliberately leave their members alone in their default
constructors, so what those three components held was the previous contents of
the heap.

That explains the two things the failure report could not. The value was
non-finite rather than merely wrong, because indeterminate bytes read as floats
usually are. And it appeared at parameter 2 on OpenGL against parameter 1 on
Vulkan: an expression that produced a NaN would have produced it at the same
parameter on both backends, while heap contents differ between the two
processes.

The fix is the one-line initializer each affected `idWinVar` now carries. The
sanitizing guards are kept as a backstop for anything else that reaches the
writer non-finite. `savegame_corruption_contract` pins the invariant: every
`idWinVar` whose payload does not zero itself must initialize it in its default
constructor, `idRectangle` must keep zeroing itself for its exemption to hold,
and `idVec2`/`idVec3`/`idVec4` must keep not doing so, since that is why the
initializers are required. Reverting any one initializer fails the contract.

An unresolved `$var` transition operand was silent about the same thing: the
lookup fails, the literal token is kept, none of it parses, and the transition
runs from zero. It now warns and names the window, the GUI source file, and the
parameter index, matching the save-time diagnostic's numbering.

Verified on the current build at 1280x720 windowed, settled gameplay, engine
`saveGame`: OpenGL and Vulkan both complete with zero non-finite warnings, zero
unresolved-transition warnings, and an 8.5 MB save, against a run that
previously warned on every attempt.

### Ranked remaining work

1. Parallel level image decode. 1,718 files and 213 MiB of mostly independent
   inflate and decode work currently run serially ahead of the serial GL
   upload. This is the largest single remaining item and the one the idle core
   count most clearly supports.
2. First-visit cost of the level-load cache path. Closing that gap is what
   turns `com_levelLoadModernization` from a warm-only win into a promotable
   default.
3. Preload replay coverage. The pipeline admits 64 of 4,428 learned sources and
   peaks at 41 MiB of its 384 MiB staging budget, because each admitted source
   holds an open file handle for the whole load. Lazily opening handles as the
   pipeline consumes them would let the cap rise.
