# Vulkan gap closure

Status: in progress. Objective: resolve all remaining Vulkan implementation,
compatibility, recovery, and qualification gaps. This does not change the
default renderer or declare Vulkan release-supported before the evidence exists.

The current source and `engine-capability-matrix.md` take precedence over the
older Phase A-J progress notes. Gamma/brightness, classic post processing,
light grids, soft particles, alpha-to-coverage, player outlines, cel shading,
underwater effects, and debug views have already landed.

## Completion requirements

| Area | Remaining work | Evidence required for closure |
|---|---|---|
| Continuous runtime validation | Exercise Vulkan on push/PR, require the module and working validation layers, and fail on skipped or missing required cases. | Harness regression tests, actual startup/rendering and failure drills, and successful hosted workflow artifacts. Software Vulkan is regression evidence, not physical GPU qualification. |
| Startup recovery | Implemented and locally verified for window, surface, swapchain and mandatory renderer resources. Full owner teardown precedes module unload; OpenGL starts in the same process. | All four deterministic failure cases pass repeatedly with clean shutdown, intact ordered logs, and unchanged Vulkan startup. Resource-failure SP/MP stock gameplay also passes. Hosted/platform coverage remains part of the separate qualification requirement. |
| Custom material programs | Execute supported authored ARB and GLSL material programs beyond the recognised stock families, with explicit diagnostics for invalid source. | Compiled shader fixtures and rendered comparisons against OpenGL for real custom programs, including parameter bindings, textures, vertex outputs, and custom lighting. |
| HDR scene and exposure | Provide floating-point scene rendering and automatic exposure with correct post, UI, MSAA, capture, and resize ordering. | Render-target format verification, unclamped highlight/exposure fixtures, OpenGL comparisons, validation-clean gameplay and screenshots, and reset/resize tests. |
| PBR, probes, and clustered decals | Reach the supported OpenGL material, environment/probe, and decal feature scope on Vulkan. | Authored fixture execution, equivalent visible results, resource lifecycle/admission tests, and fallback evidence for exhausted or invalid resources. |
| Temporal motion | Implement the exact eligible rigid-object velocity path; retain explicit reactive handling for genuinely unsupported geometry. | Moving-object and camera-cut fixtures, history invalidation, visible TAA comparisons, and validation-clean runtime evidence. |
| Shadows | Close the translucent-moment extension and remaining complete mapped/stencil/hybrid qualification. | Controlled transparent-caster comparisons plus stock projected, point, CSM, dynamic, cutout, constrained-budget, and fallback gameplay captures. |
| Render targets and tools | Cubemap faces, depth-only targets and multiple color attachments are implemented; finish capture/debug parity qualification, documenting intentional asynchronous diagnostic latency. | Every cube face/aspect, nested capture, resolve, resize and resource-lifetime tests; engine-generated screenshot comparisons. |
| Optimization | Audit the planned indirect/culling, command recording, upload overlap, pipeline warm-up, descriptors, and barriers; implement the remaining applicable Vulkan paths. | Measured CPU/GPU pass times and memory use, performance validation, and correctness comparisons for each enabled path. |
| Release qualification | Complete at least five-run OpenGL/Vulkan comparisons per required scene/preset, SP/MP and long-session testing, clean staged-package evidence, and physical GPU/driver/platform coverage. | Retained provenance-bound reports, engine captures, user visual/soak sign-off, and Windows/Linux/MoltenVK hardware records. No synthetic claim for unavailable hardware. |
| Promotion | Implement the Vulkan-specific evidence/sign-off gate and reconcile all status/usage/release documentation. | Gate negative tests, exact evidence provenance, explicit sign-off, and a requirement-by-requirement completion audit before changing `best` or support status. |

## Work record

- The user paused the original Vulkan task and authorized takeover after the
  scoped GL PBR work. The [PBR audit](2026-09-20-pbr-rendering-audit.md) records
  the continuing Vulkan work and owns build/staging/GPU execution while that
  task stays paused. Existing recovery and attachment changes are preserved.
- The v61-v63 lifecycle fixes submit an open screenshot frame before swapchain
  resize, maintain real multiplayer MSAA color/depth targets through HDR/TAA
  toggles, and reject stale depth resolves. Stock SP/MP HDR transitions and GPU
  attachment/exposure fixtures pass locally; HDR visual parity remains open.
- v68 passes 55 native PBR controls each at 0x and 4x MSAA: scalar/packed/separate
  data, XYZ/RG/AGB normals, matching cutouts and emission, exact fallback,
  restarts and point/projected shadows. Raw FP16 tests prove finite emission
  after texture modulation and preserve faint channels. Its full-map gate
  remains 2/3 because native source-alpha ownership is missing. Environment and
  probe consumers, complete scene linearization, specular antialiasing and
  arbitrary additive overflow still require work. These results do not close
  the broader renderer or promotion requirements above.
- v69 adds final-normal variance filtering to all native direct PBR variants.
  The expanded suite passes 68 controls each at 0x/4x, including exact filter
  restoration and constant-normal/roughness-ceiling invariance. Attachment/HDR
  GPU checks pass 2/2, while full-map source-alpha ownership remains a retained
  failure. Quantified temporal stability and the broader open requirements are
  unchanged; source-alpha and environment lighting need a native surface pass
  with complete resource admission before it replaces classic ownership.
- Stock `game/storage1` timing exposes CPU/presentation stalls in both v68 and
  v69. GPU medians are 4.985/4.991 ms, but CPU P99 is 424.643/413.892 ms against
  the 28 ms budget. Disabling periodic renderer diagnostics still produces a
  415 ms presentation P99. The hidden-window runs do not establish the cause;
  retain the failures and isolate native presentation/CPU phases before any
  performance promotion. See `.tmp/pbr-audit/qualification-v69.json`.

- 2026-09-20: audited the current tree. Push/PR workflows run Vulkan source
  contracts but omit the existing runtime cases. The default matrix also
  excludes Linux Vulkan regardless of a staged module. Explicit `--cases`
  already bypasses that filter; the new CI selection must remain explicit.
- The Vulkan instance can retry without validation when layer creation fails;
  the runtime case must require a positive layer-and-debug-messenger marker so
  an unvalidated run cannot count as validation-clean.
- Existing worktree edits to gameplay, menus, translations, and shared release
  documents predate this task and must be preserved.
- Linux x64/ARM64 push and ARM64 pull-request workflows now select the Vulkan
  startup/render-target and two fallback cases with lavapipe and validation
  layers. Explicitly required missing modules and empty suites are errors.
  The first hosted results are still required; no hardware coverage is inferred.
- Cubemap attachment views select one face without replacing the full sampled
  cube view. Color and depth captures follow the selected face, post effects
  restore it, and image retirement includes every face view. Depth-only targets
  now support binding, draws, resize, capture and depth resolve. Matching cube
  resolves copy every layer; incompatible layer counts are rejected. Retirement
  queue overflow now remains behind its frame fence instead of freeing resources
  still referenced by the unsubmitted command buffer. The GPU test exceeds the
  fixed queue capacity through repeated cubemap resizing in one frame.
- Local Windows/NVIDIA startup, missing-module fallback and empty-driver
  fallback passed from an isolated copy of the staged package. The GPU test
  checks every captured pixel and draws into depth-only targets, including
  complete-layer resolves and 4x MSAA depth resolves. The expanded 66-capture
  test and retirement overflow stress pass with zero Vulkan validation warnings.
  The three-case report is
  `.tmp/vulkan-gap-closure/retirement-startup/renderer_validation_report.md`;
  the module in `builddir`, `.install` and the isolated package has the same
  SHA-256. Final gameplay evidence is retained in the adjacent
  `retirement-gameplay` directory.
- The runtime matrix previously ignored `--runtime-dir` when choosing its
  default executable, so a missing-module drill could hide the wrong package's
  DLL. The selection now follows the runtime directory, and `fs_devpath` follows
  the isolated evidence/save path. Behavioral regression checks cover both.
- Stock SP `game/airdefense1` and MP `mp/q4dm1` gameplay passed with validation
  enabled and engine screenshots. These hidden-window, short diagnostics do not
  satisfy performance, visual-parity, long-soak or hardware-promotion gates.
  The pacing-only MP harness needs `--set-cvar r_mode=-1` after its game-module
  reload; otherwise the archived machine-spec preset changes the mode selector.

- Multiple color attachments now share one admission check for binding and
  resolves. The limit matches OpenGL's five buffers, bounded by the physical
  device. Ordered formats participate in every pipeline cache key; blend/mask
  state, load barriers, clears and resolves cover all attachments. Invalid
  layouts and cross-index resolve aliases fail before recording partial work.
  Adding an attachment to a current target refreshes its rendering scope.
- The mandatory MRT GPU exercise passes two-buffer mixed-format targets in both
  format orders, six cube faces, and two- and five-buffer 4x MSAA targets at two
  sizes. Exact raw readbacks check unclamped half-float values, per-buffer
  output, blend masks, clears, depth and resumed-scope contents. Invalid counts,
  dimensions, sample combinations and aliases preserve the active target.
  Rectangular cube-target resizes are rejected before image allocation.
- MRT build/staging, shader-header regeneration and all three startup/fallback
  cases passed locally with active validation and no Vulkan validation warnings.
  Evidence is in `.tmp/vulkan-gap-closure/mrt-final-startup/`; the tested module
  SHA-256 is `1447ce0eb61d3c9ea7d72a6aa8330a999e03fb93b4f9d0d8d4a123be8012b89e`.
  An earlier negative fixture accidentally allocated a rectangular cubemap;
  the gate rejected its VUID diagnostics, and the corrected run is the accepted
  evidence. This render-target work does not supply the advanced feature
  consumers or close their separate HDR/PBR/temporal qualification requirements.
- The same MRT module passes stock SP `game/airdefense1` and MP `mp/q4dm1`
  listen/client gameplay in `.tmp/vulkan-gap-closure/mrt-gameplay/`, with the
  runtime's 156 hashed files unchanged. Engine screenshots were inspected.
  These remain short windowed diagnostics, not performance or visual-parity
  promotion evidence.

- Recoverable startup preparation now creates the real Vulkan window/device,
  swapchain and mandatory executor resources before UI/session/game startup.
  Failure returns to engine code for ordinary owner teardown before callbacks
  and the failed module unload. A single OpenGL retry then reruns initialization.
  The requested Vulkan preference and the original log survive. Renderer ABI 13
  carries this export; the engine and both renderer modules must match.
- All seven startup/fallback cases pass in `recovery-final-startup`; all four
  late-failure cases pass again in `recovery-repeat-startup`. Both reports are
  under `.tmp/vulkan-gap-closure/`, with active validation required wherever
  instance creation was reached and no Vulkan diagnostics. The first iterations
  exposed log truncation and a breadcrumb printed while the filesystem was down;
  both are fixed, and the retained engine log must prove the complete order.
- The deepest resource failure also recovers into stock SP `game/airdefense1`
  and an auto-joined MP `mp/q4dm1` listen game, with clean shutdown and inspected
  engine screenshots (`recovery-stock-gameplay/report.json`). Ordinary Vulkan
  SP and MP listen/client gameplay passes separately in
  `recovery-normal-vulkan-gameplay`. The same 156-file runtime remains unchanged
  throughout. These are short diagnostic runs, not soak/performance promotion.
- The tested engine, GL module and Vulkan module initially matched builddir,
  staging and the isolated package. `recovery-module-sha256.json` records all
  three binaries; Vulkan SHA-256 is
  `c76c281fe6779c3cc4621b437821a60324934ef293eff0a3f9eb943121eec5e7`.
  The complete checkpoint is `.tmp/vulkan-gap-closure/recovery-verification.md`.
- PBR work is coordinated with the separate PBR audit task: it owns shared/GL
  material and BRDF analysis, probe math, the procedural proof fixture and the
  coordinated Vulkan interaction-shader math update. Build/staging and GPU use
  were released after these recovery tests. That work does not reduce the PBR,
  probe, HDR or other completion requirements above.

## Separate issues found during validation

The authorized takeover closes the immediate screenshot/partial-restart
swapchain lifetime crash and hidden-window extent regression, implements real
MSAA in backend-owned MP scenes, and keeps spatial depth consumers behind a
successful current-generation resolve. v63 passes stock SP/MP at 0x/4x plus
the GPU attachment/MRT suite and 48 HDR fixture executions around full restart,
with active validation and unchanged runtime hashes. Reports:
`.tmp/pbr-audit/vulkan-v63-hdr-gameplay/report.json` and
`.tmp/pbr-audit/vulkan-v63-gpu/renderer_validation_report.json`.
The inspected SP sky/highlights remain overexposed with automatic exposure;
controlled HDR visual parity, native PBR parity and the broader gates stay open.

The user paused this task and authorized the PBR task to take over after its
OpenGL PBR phase. The v60 PBR laboratory passes 74 controls and 15 renderer
self-test markers; retail SP/MP captures are retained, with an MP server CPU
budget miss still open. Vulkan work now continues in the PBR task, beginning
with the known HDR partial-restart crash and native PBR parity. This task
remains inactive to avoid competing edits, builds or GPU runs. See the
[PBR audit](2026-09-20-pbr-rendering-audit.md) for current ownership and evidence.

- The earlier game ABI pin and renderer-header mismatches are reconciled in
  the shared tree: game ABI 48 and renderer ABI 14 now agree with their test
  contracts. The takeover recheck passes `level_load_cache.py`,
  `renderer_temporal_presentation.py` and `renderer_gpu_frame_timing.py`
  (`.tmp/pbr-audit/*static-v61.log`). This resolves those specific static
  blockers, without implying that the entire renderer suite has passed.

- The stock MP smoke also retains missing `q4dm1` AAS-size and non-precached
  declaration warnings. They are separate from Vulkan validation diagnostics
  and do not establish bot/navigation or asset-precache qualification.
