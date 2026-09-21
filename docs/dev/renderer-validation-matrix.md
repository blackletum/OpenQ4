# Renderer Validation Matrix

This matrix is the validation source of truth for staged renderer work. Most safe tier probes remain GL-focused, while shared renderer-contract, classic-domain, and GPU-animation self-tests plus replayable budget evidence cover both OpenGL and Vulkan. Supervised gameplay can use the mode-specific SP/MP launch tasks or the noninteractive gameplay harness described below.

For cross-engine feature status, use the [engine capability matrix](engine-capability-matrix.md). This document owns renderer acceptance evidence and promotion gates; it does not turn an experimental renderer capability into a supported/default one by itself.

## Build And Stage

Use the project wrapper:

```powershell
tools\build\meson_setup.ps1 setup --wipe builddir . --backend ninja --buildtype=debug --wrap-mode=forcefallback
tools\build\meson_setup.ps1 compile -C builddir
tools\build\meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects
```

For incremental validation after an existing setup:

```powershell
tools\build\meson_setup.ps1 compile -C builddir -- -j1
tools\build\meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects
```

## PBR Material Laboratory

The [PBR authoring guide](../user/pbr-materials.md) includes a reproducible command
for the original 24-station map. The generator and harness live in
`tools/validation/generate_pbr_validation_map.py` and
`tools/tests/renderer_pbr_laboratory.py`. Test content stays in an independent
runtime under `.tmp/stock-runtime/`; it is not a shipped asset dependency.

The [PBR audit](plans/2026-09-20-pbr-rendering-audit.md) records numerical BRDF and
environment tests, rendered material/ownership controls, HDR float/display
references, shadow and skinning motion, baked/fog/transparency composition,
reloads, restarts and complete fallback. Every capture uses the engine command.
Linear PFM proof requires a completed HDR frame; encoded-preview export must
be refused. Reports retain binary, fixture, map and harness hashes.

Keep MSAA sample counts fixed within each batch. Include shadow off/on/off
controls before restart: restarting alone misses shadow-resource history leaks.
Use the existing numerical and negative-image oracles; a clean log or plausible
picture alone does not prove the feature. Final retail gameplay/performance and
backend/platform promotion remain separate gates.

## Automated Safe Matrix

The safe matrix starts the staged client, runs renderer self-tests or startup probes, prints `gfxInfo`, then quits. It does not launch maps.

```powershell
python tools\tests\renderer_validation_matrix.py
```

Each invocation uses `<output-dir>/savepath` by default, so archived settings in
the developer `.home` tree cannot silently select Vulkan or enable experimental
renderer paths. Pass `--savepath` only when a case intentionally needs a named,
pre-seeded save tree.

For focused validation without relaunching the full matrix, use `--cases` with one or more case ids:

```powershell
python tools\tests\renderer_validation_matrix.py --cases renderer-default-promotion-selftest
```

The runner writes a timestamped report under `.tmp/renderer-validation/` with per-case logs and a JSON copy for CI or release triage.

Automated coverage:

| Case | Coverage |
|---|---|
| `renderer-foundation-selftests` | context ladder, tier selector, tier workload contract, backend-neutral authored/evaluated pass/clip/layout/buffer contracts, exact four-weight GPU-animation contract, upload manager, GPU timer, scene packet, render graph, render graph resource owner, ordered material resource table, PBR/authored-probe material parsing, specular-probe atlas placement/slot/generation, transactional classic-GUI, classic cinematic/authored-post, render-demo/Raven special-frame, classic-world-ambient, classic-interaction, classic-fog/blend, and capture-backed classic-subview domains, geometry/instance resource records, GL state cache, Shader Library V2 pass-family/permutation/reflection coverage, draw plan, submit plan, modern executor, and shadow planner self-tests |
| `renderer-vk-clear-startup` | Vulkan device, swapchain, GUI executor, and mandatory shared renderer contracts, with a positive marker proving that the validation layer and debug messenger are active. The on-device render-target self-test checks 66 face captures at two sizes with cube and shared 2D depth attachments, including real depth-only draws, exact color/depth readback, complete-layer resolve, MSAA depth resolve, resize, retirement beyond the fixed queue capacity, and invalid face selection. A second mandatory GPU test draws to two color attachments and, where supported, five. It checks mixed RGBA8/RGBA16F formats, unclamped float values, blend masks, scope-load preservation, cube faces, every color/depth resolve, resize, and invalid attachment/alias rejection. The log records effective sample counts so a device fallback is not mistaken for MSAA coverage. |
| `renderer-vk-hdr-selftest` | Mandatory GPU HDR exercise: 24 unclamped RGBA16F scene/capture fixtures cover 0x and 4x MSAA, square and rectangular resize, bright/mixed/dark radiance, logarithmic luminance reduction, synchronous and frame-fenced asynchronous exposure, and stale/duplicate/disabled sample rejection. Active validation and the completed fixture marker are required. |
| `renderer-vk-device-fallback-drill` | Vulkan device break drill: `VK_DRIVER_FILES` and `VK_ICD_FILENAMES` point at an empty manifest, so the module loads but no Vulkan driver exists. Requires `Renderer API: requested=vulkan active=gl disposition=fallback` with a `device probe failed:` reason, and fails if device bring-up (`VK_InitRenderDevice`) was reached. Skipped on macOS, which loads MoltenVK directly rather than through the loader's driver list. The companion `renderer-vk-fallback-drill` hides the module file instead. |
| `renderer-vk-window-recovery`, `renderer-vk-surface-recovery`, `renderer-vk-swapchain-recovery`, `renderer-vk-resources-recovery` | Inject failure at each real startup stage using the non-archived `r_vkStartupFailure` diagnostic (1–4; normal operation is 0). Require ordered engine-log evidence of failure, owner teardown/module unload, fresh OpenGL selection/context, and module self-test success. Surface, swapchain and resource cases also require active Vulkan validation. They run hidden/windowed with isolated config and fail on Vulkan diagnostics or fatal startup. Linux push/PR Vulkan selections require all four cases. |
| `renderer-visible-depth-selftest` | opt-in `r_rendererModernVisibleDepth` coverage for graph-backed scene depth, compatible shadow-depth resources, fallback accounting, depth-overlay readiness, and `gfxInfo` reporting |
| `renderer-gbuffer-selftest` | opt-in `r_rendererModernOpaque` coverage for graph-backed G-buffer resources, MRT setup, opaque/alpha-test draw classification, diffuse texture binding, packing assumptions, fallback accounting, bandwidth metrics, attachment debug-overlay readiness, and `gfxInfo` reporting |
| `renderer-cluster-grid-selftest` | opt-in modern clustered-light preparation coverage for point/projected/fog/ambient/special light classification, budgeted dynamic grid slicing, cluster reference packing, spill/overflow accounting, authored-probe top-two selection, and atomic clustered-decal prepare/seal ownership including malformed, stale, record-overflow, and reference-overflow zero-publication cases; GL 3.3 UBO fallback readiness, GL 4.3+ SSBO upload readiness, cluster debug-overlay texture generation, and `gfxInfo` reporting |
| `renderer-pbr-visible-selftest` | guarded opaque PBR resource admission, linked G-buffer/deferred/forward program readiness, G-buffer command/input packing, scalar propagation, `r_rendererModernQuality` rollback, and clustered-forward exactly-one surface-owner/interaction-consumption accounting without promoting stock materials or GPU-driven visible lighting |
| `renderer-shadow-planner-selftest` | modern shadow planner coverage for projected/point/CSM policy, mapped/stencil-fallback/skipped accounting, benchmark-budgeted shadow resolution/light/pixel caps, render-graph shadow resource reporting, clustered shadow descriptor integration, and `gfxInfo` reporting |
| `renderer-deferred-resolve-selftest` | opt-in `r_rendererModernDeferred` coverage for graph-backed deferred resolve output, G-buffer/depth/cluster buffer inputs, point/projected light accumulation, light-grid contribution, fallback accounting, deferred debug-overlay readiness, GPU timer coverage, and `gfxInfo` reporting |
| `renderer-forward-plus-selftest` | opt-in `r_rendererForwardPlus` coverage for graph-backed scene-color/depth resources, clustered opaque/alpha-test/transparent programs, clustered-light UBO/SSBO reads, transparent sort preservation, fallback accounting, overdraw estimates, GPU timer coverage, and `gfxInfo` reporting |
| `renderer-modern-visible-selftest` | opt-in `r_rendererModernVisible` coverage for the guarded hybrid visible-frame bridge: graph-backed depth, G-buffer, deferred resolve, forward+ source output, graph-owned `hybridSceneColor` composition, HDR/post-process handoff before SSAO/bloom/authored post, depth-copy handoff accounting, shadow-ready handoff/fallback accounting, final GUI/present overlay, GPU timer coverage, and `gfxInfo` reporting |
| `renderer-modern-compatibility-selftest` | Phase 14 modern-visible compatibility coverage for command-category ownership inventory, modern fullscreen GUI readiness, light-grid ownership, explicit post/copy/subview/render-demo/BSE fallback buckets, deterministic render-demo accounting, and `gfxInfo` reporting |
| `renderer-compatibility-gates-selftest` | Phase 15 fallback-gate coverage for missing UBO, broken MRT, missing timer query, missing buffer storage, rejected debug-context fallback, and synthetic driver-quirk downgrades |
| `renderer-default-promotion-selftest` | Phase 8 evidence-gated default-promotion coverage for `r_glTier auto`, explicit `r_renderer arb2` escape behavior, compatibility gates, modern-executor readiness, ARB2 rollback availability, missing/incomplete/complete `r_rendererPromotionEvidence`, and `r_rendererModernAutoPromote` sign-off control |
| `renderer-default-safety-selftest` | Phase 13 conservative-default coverage for ARB2 default visibility, `r_renderer best` or explicit `r_renderer arb2`, `r_glTier auto`, rollback availability, default-off PBR/probe/clustered-decal leaves, `r_rendererModernQuality` master rollback, and default-off shared root/in-world GUI, cinematic/post, special-frame, world-ambient, interaction, fog/blend, subview, and deform corridors plus modern executor, visible, diagnostic, GPU-validation, bindless, shader-reload, and auto-promotion cvars |
| `renderer-benchmark-selftest` | Phase 16 benchmark coverage for rolling P50/P95/P99 frame-time capture, CPU front-end/visibility/packet/graph/submit/present timings, GPU pass timing fields, upload/draw/light/cluster/fallback counters, benchmark presets, and performance-threshold reporting |
| `renderer-gpu-driven-selftest` | forced `r_glTier gl43` coverage for GL 4.3 SSBO submit records, compute scissor culling, clustered-bin validation, compacted indirect command generation, CPU/GPU readback comparison, masked multi-draw indirect execution, GPU timer coverage, and `gfxInfo` reporting |
| `renderer-low-overhead-selftest` | forced `r_glTier gl45` coverage for GL 4.5 DSA graph texture/FBO allocation, DSA sampler creation, named buffer/FBO updates, UBO/SSBO/texture/sampler multi-bind batches, submit-batch compaction, bindless experiment reporting, persistent upload defaults, fence diagnostics, and `gfxInfo` reporting |
| `shader-library-gl33` | forced `r_glTier gl33` coverage for GLSL 330 Shader Library V2 compile, link, exact-version lookup, and sampler reflection |
| `shader-library-gl41` | forced `r_glTier gl41` coverage for GLSL 330/410 Shader Library V2 variants on the macOS-class GL 4.1 portability floor |
| `shader-library-gl43` | forced `r_glTier gl43` coverage for GLSL 330/410/430 Shader Library V2 variants alongside GPU-driven SSBO-capable tiers |
| `shader-library-gl45` | forced `r_glTier gl45` coverage for GLSL 330/410/430/450 Shader Library V2 variants alongside low-overhead DSA-capable tiers |
| `shader-library-gl46` | forced `r_glTier gl46` coverage for top-tier Shader Library V2 coverage with the highest selected GLSL variant and reflected sampler bindings |
| `tier-auto` | default compatibility-preserving startup and `gfxInfo` |
| `tier-legacy` | forced legacy compatibility startup and `gfxInfo` |
| `tier-gl33` | forced GL 3.3 startup and `gfxInfo` |
| `tier-gl41` | forced GL 4.1 startup and `gfxInfo` |
| `tier-gl43` | forced GL 4.3 GPU-driven tier startup and `gfxInfo` |
| `tier-gl45` | forced GL 4.5 low-overhead tier startup and `gfxInfo` |
| `tier-gl46` | forced GL 4.6 top tier startup and `gfxInfo` |
| `tier-gl33-debug-context` | debug-context request with non-debug fallback available |
| `present-vsync0-fps0` | unlocked presentation startup probe |
| `present-vsync1-fps240` | high-refresh capped presentation startup probe |
| `present-vsync1-fps120` | 120 FPS capped presentation startup probe |

The forced tier cases pass when startup succeeds and the selected tier is reported. If a machine cannot support the forced tier, the log must show the selected fallback tier and `Renderer tier contract:` must report `degraded=1`, `failClosed=1`, and a concise `missing=` reason.

For stock-map validation after recovery, run the opt-in gameplay driver against
a freshly staged runtime and an installed retail asset tree:

```powershell
python tools\tests\renderer_vulkan_recovery_gameplay.py --runtime-dir .install --basepath "E:\SteamLibrary\steamapps\common\Quake 4" --output-dir .tmp\vulkan-recovery-gameplay
```

Its default resource-failure case enters `game/airdefense1` and an auto-joined
`mp/q4dm1` listen game after OpenGL recovery. `--stages 1 2 3 4` expands the
failure coverage. Every case uses a separate save tree, hidden windowed output,
disabled input, at least 120 measured gameplay frames, an engine-generated
1280×720 screenshot and clean shutdown. The JSON report retains launch arguments,
logs, image hashes and before/after package hashes. This qualifies recovery;
it supplies neither performance budgets nor renderer-promotion evidence.

Automated safe cases also fail if their logs contain renderer warning signatures such as `idStr::snPrintf` overflow, `WARNING: idStr`, shader compile/program link failures, or OpenGL error markers. The generated Markdown/JSON report records per-case warning-signature counts so the Phase 8 `warnings=0` promotion token cannot be inferred from expected-line checks alone.

The foundation case runs the dependency-light classic-GUI,
classic-cinematic/authored-post, render-demo/Raven special-frame, classic-world-ambient, classic-interaction,
classic-fog/blend, and capture-backed classic-subview contract self-tests. The
Vulkan startup case also requires the cinematic/authored-post, render-demo/Raven special-frame, world-ambient,
interaction, fog/blend, and subview self-test markers so that module
registration and the shared domain contracts are exercised through both
renderer APIs.

The separate `tools/tests/renderer_classic_gui_domain.py` static regression
guards packet-derived GUI material admission, ordered evaluation, opaque
resource ids, GL/Vulkan whole-view handoff ordering, source no-op accounting,
and suppression of the older aggregate GL GUI replay.
`tools/tests/renderer_classic_world_ambient_domain.py` guards the distinct
packet-derived world-pass pool, fixed surface/program/depth semantics,
transactional source/depth matching, explicit non-owned blockers, pre-fog/fog/
post-fog ordering, complete GL/Vulkan preflight before commit, and sealed
consumer bodies with no material-stage or raw-register reinterpretation. It also
checks conservative default/bootstrap state, benchmark/baseline isolation, and
validation workflow registration. Enabled-path image evidence for these first
two domains is the supervised GL/Vulkan capture pair below.

`tools/tests/renderer_classic_interaction_domain.py`,
`renderer_vulkan_world_interaction_compatibility.py`, and
`renderer_vulkan_shadow_compatibility.py` guard the unshadowed and
shadow-coupled interaction contracts. They require exact packet receiver/caster
identity, stencil physical-replay counts, projected single-map and CSM/parallel
state, point cubes, mapped/static/dynamic/perforated casters, complete hybrid
supplements, semantic map hashes, GL cache/update/scratch retention, Vulkan
reservation/commit/abort ordering, exact backend reconciliation, and atomic
fallback with zero committed ownership counters and no shared main-target draw.
The gameplay harness parser also fails a dynamic or
perforated stock target unless a per-map line explicitly reports the matching
sealed feature.

`tools/tests/renderer_classic_fog_blend_domain.py` guards the complete
fog/blend-phase transaction: exact view-light and GLOBAL/LOCAL receiver
identity, ordered active/inactive blend stages, fog receiver/cap accounting,
evaluated texgen/state/resource sealing, stable hashing, complete GL/Vulkan
preflight before the first main-target draw, backend reconciliation, atomic
fallback, conservative default/bootstrap state, benchmark/baseline isolation,
and validation registration. Its dependency-light self-test and static
contract pass, but enabled-path stock engine-screenshot qualification remains
pending.

`tools/tests/renderer_classic_subview_domain.py` guards exact front-end and
legacy child-view/capture association, bounded parent/source/material admission,
remote-camera/refraction-only scope, sealed OpenGL/Vulkan copy arguments,
post-copy ownership reporting, default isolation, diagnostics, and workflow
registration. Its native/static contract passes; fixed-camera engine-screenshot
qualification remains required before enabling the setting beyond focused work.

The focused safe-matrix run retained at
`.tmp/renderer-validation/world-ambient-final-2` passed all three selected
cases: `renderer-foundation-selftests`, `renderer-default-safety-selftest`, and
`renderer-vk-clear-startup`.

The visible-depth, G-buffer, clustered-light, deferred-resolve, forward+, modern-visible, modern-compatibility, compatibility-gates, default-promotion, default-safety, benchmark, GPU-driven, low-overhead, and shader-library tier self-tests intentionally run as their own safe cases instead of being appended to the foundation self-test startup command, because the engine command parser has a fixed startup command list budget.

The shader-library tier cases force `r_glTier gl33`, `gl41`, `gl43`, `gl45`, and `gl46`, run `rendererShaderLibrarySelfTest`, and require `gfxInfo` to report `Modern GL shader library: available` with program, kind, permutation, and sampler-reflection coverage. The runner marks these cases as assetless startup probes, because they only need renderer initialization and should not load game scripts just to validate internal shader variants.

The foundation self-test case also runs `rendererPBRMaterialSelfTest`, while `rendererScenePacketSelfTest` and `rendererMaterialResourceTableSelfTest` include the matching PBR packet/resource cases. Together these assetless contracts are expected to verify that opt-in `pbr {}` metadata leaves classic Quake 4 stages untouched, image usage and scalar registers survive parsing, explicit and approximate classic fallbacks are classified deterministically, packet records preserve PBR metadata, and packed, separate, scalar-only, unsupported-workflow, and missing-map resource records expose deterministic reasons. Packed ORM, separate metallic/roughness/AO, and scalar-only records may become `pbrModernReady`; the separate maps are bound through direct sampler units rather than the four-entry texture-table ABI. The material parser self-test also owns the namespaced authored light-material probe metadata contract without changing the public light/game/save/demo/network ABI.

`renderer-pbr-visible-selftest` runs `rendererPBRVisibleSelfTest` for guarded opaque resource admission, linked G-buffer/deferred/forward programs, G-buffer command/input packing, scalar propagation, `r_rendererModernQuality 0` rollback, and clustered-forward surface-owner deduplication. It does not itself exercise source-alpha admission, analytic IBL, PBR debug routes, or authored-probe atlas/binding readiness. `renderer_pbr_materials.py` statically pins the GGX/Smith/Schlick source, explicit PBR eligibility marker, direct texture-unit contracts, analytic-IBL path, source-alpha admission, debug routes, probe shader ABI, and the Vulkan direct-material contract. The foundation executor self-test owns probe atlas placement/slot/generation checks; the deferred and forward+ self-tests validate reflected probe sampler/UBO bindings; `rendererClusterGridSelfTest` owns producer-side probe selection and clustered-decal transactions. Vulkan admission additionally requires exactly one declared and active classic bump -> diffuse -> specular sequence, rejects duplicate/invalid/reordered or custom-lighting co-ownership, and replaces only the final interaction submit. The v69 native path supports scalar, packed/separate data, XYZ/RG/AGB normals, matching alpha-tested coverage and single additive emission. The laboratory passes 68 isolated Vulkan controls each at 0x and 4x MSAA, including specular footprint filtering and constant-normal/roughness-ceiling invariants, independent emission references, cutout alpha-to-coverage, mismatched-mask/glow fallback, restarts and point/projected shadows. Cross-report MSAA proof verifies binary/map identity and capture/log hashes. Raw FP16 emission tests at 0x/4x preserve a faint channel while bounding brighter channels after texture modulation. These tests do not qualify arbitrary multi-light overflow. The unchanged full-map ownership requirement still fails source-alpha transparency; native IBL, probes, temporal-shimmer qualification, clustered decals and complete scene color parity remain open.

Milestone F adds three dependency-light contracts. `openq4-advanced-lighting-core-test` covers generation-bound bounded transactions, malformed/NaN input, capacity and reference overflow, deterministic priority/weight/stable-id top-two selection, probe blend weights, and complete master-disabled rejection. `openq4-specular-probe-atlas-packing-test` covers fixed six-face placement for eight cubemaps without needing a GL context. `rendererClusterGridSelfTest` covers the engine-side probe/decal integration, including the 32-probe record limit, top-two cluster indices, and atomic decal prepare/seal ownership capped at 1,024 records and 65,536 references. A rejected probe uses analytic PBR fallback; a rejected decal transaction publishes no modern ownership. These paths are OpenGL-only, and `MODERN_LIGHTING_PARITY_PROVEN_DOMAINS` remains `0`.

The durable visual fixture is generated by `tools/validation/generate_pbr_fixture.py`. It uses only the Python standard library to create original deterministic 24-bit TGA albedo, `tangentXYZ` normal, and packed ORM data, a dual-authored material, and a SHA-256 provenance manifest below `.tmp/`; it reads or copies no external asset. `pbr_procedural_fixture.py` checks determinism, hashes, TGA layout, material wiring, provenance, and output containment. `pbr_orm_packer.py` separately checks that the ORM helper reads an explicitly selected stored channel and writes `R=AO`, `G=roughness`, `B=metallic` without luminance conversion.

### Milestone F current-source Windows evidence

The local implementation exit gate passed on 2026-08-23. The retained root is
`.tmp/milestone-f-evidence/20260823-final/`; it was produced from a
current-source debug x64 build and a retail-PK4 asset root rather than a clean
committed release package.

| Gate | Local result |
|---|---|
| Static/native/engine contracts | PASS: the PBR and advanced-lighting static contracts passed; the dependency-light native run passed 10/10. `engine-selftests-final4` passed its 2/2 focused PBR/modern-visible rerun against the final staged package after the earlier `engine-selftests` set passed 8/8. |
| Retail compatibility and budgets | PASS: the final staged-package rerun in `stock-baseline-final4` passed all four roles (SP capture, SP demo playback, pure MP server, and auto-joined MP client) against 40 retail PK4s and zero loose `q4base`/`q4mp` files, and its recorded asset/runtime/artifact hashes passed replay verification. CPU P95/P99 and GPU P95/P99 were respectively 8,259/8,703 and 2,277/2,743 us for SP, 7,127/7,490 and 2,901/3,896 us for the MP server, and 9,757/10,556 and 3,618/3,898 us for the MP client. Packaged openQ4 overlays were still present, so this is not an overlay-free claim. |
| OpenGL procedural PBR | PASS on forced GL 3.3, 4.1, 4.3, and 4.5 with frame-pacing P95/P99 of 6/6, 5/5, 5/5, and 6/6 ms and zero GL/FBO warning signatures. Each real plan reduced 24 source packets to five depth plus five ambient owners with `planFallback=0`; forward+ executed five draws and the visible bridge reported execution, resources, source, and composition. The GL 4.5 debug-7 capture contained the exact green ownership marker in 639,507 of 921,600 pixels (69.39%). The final GL 4.5 functional capture in `pbr-gl-harness-gate-final5` also passed schema-4 replay with the exact fixture, generated config, loaded map, runtime, process outcome, screenshot, and API-specific telemetry bound. |
| OpenGL master rollback | PASS: `pbr-gl-quality0-rollback-final2` matched the leaf-disabled reference at RMS 0 and maximum delta 0 on all four tiers. |
| Vulkan procedural PBR | PASS with validation enabled: `pbr-vk-enabled-final2` differed non-vacuously from its leaf-disabled control with per-channel RGB RMS 26.374685/20.829991/21.549890 and maximum delta 252. `pbr-vk-quality0-rollback-final2` then matched the leaf-disabled reference at RMS 0 and maximum delta 0. The final API-specific functional capture in `pbr-vk-harness-gate-final5` required a positive native packed-PBR draw marker and passed schema-4 replay with the same provenance bindings as the GL capture. |

This evidence proves the scoped PBR/probe/decal implementation, real PBR shader
ownership, and one-setting rollback. It does not claim RenderDoc coverage,
broad visual-quality acceptance, or broad authored probe/decal scene coverage.
PBR, probe, decal, and modern-visible feature controls remain default-off;
`MODERN_LIGHTING_PARITY_PROVEN_DOMAINS` remains `0`. Final committed-package,
platform/driver, retained-review, and release promotion remain pending.

### Advanced screen-space current-source Windows evidence

The independent froxel-volumetric, SSR, and SSGI leaves closed their scoped
implementation gate on 2026-08-24:

| Gate | Local result |
|---|---|
| Shared contract | PASS: `AdvancedScreenSpaceCoreTest` covers independent/combined masks, finite bounded tuning, disabled-leaf canonicalization, the stable eight-float GL/Vulkan packet, and exact master-off zero initialization. `renderer_advanced_lighting.py` verifies the default-off leaf/master admission and both backend consumers. The final Meson native suite passed 11/11. |
| Vulkan shader provenance | PASS: the checked-in temporal-resolve SPIR-V header was generated from the checked-in GLSL with the local official Vulkan SDK compiler, and `vk_temporal_resolve_shader_pin.py` passes. |
| OpenGL gameplay | PASS: all three leaves together and each leaf independently entered windowed `game/airdefense1` gameplay with an engine TGA and zero shader/FBO/GL/fatal/error counters. Two post-audit combined captures observed 102.9--117.3 Hz; the final hardened run measured 11/11 ms P95/P99 and the earlier run measured 12/13 ms. The enabled image differed non-vacuously from the same-build default control. |
| Vulkan gameplay | PASS: all three leaves together entered the same windowed scene with an engine TGA and zero validation/VUID/Vulkan-call/fatal/error counters. Two post-audit captures observed 114.1--125.8 Hz; the final hardened run measured 10/11 ms P95/P99 and the earlier run measured 11/12 ms. Effect-only presentation retained one accepted configuration generation rather than churning temporal history state. |
| Master rollback | PASS at the exact shared-packet boundary and clean in runtime: `r_rendererModernQuality 0` zeroes the complete eight-float feature packet despite enabled leaves, and the master-off `airdefense1` run enters gameplay without diagnostics. Live BSE smoke and actors make separate process launches unsuitable for an exact screenshot hash. |
| Final regression breadth | PASS: default `game/airdefense2` measured 189.6 Hz with 7/8 ms P95/P99 on OpenGL and 240.2 Hz with 6/6 ms on Vulkan. Pure `mp/q4dm1` listen-server/auto-joined-client roles passed at 174.0/180.7 Hz on OpenGL and 224.8/189.1 Hz on Vulkan. A fresh staged-package baseline then passed SP capture, SP demo playback, MP server, and MP client against all 40 verified retail PK4s with zero loose retail files. Every role retained an engine TGA and clean API/error counters; representative images passed human review. |

This is a bounded scene-colour/depth implementation, not evidence for shadowed
light-injected volumetrics, roughness-aware G-buffer reflections, world-space
GI, or whole-frame modern lighting ownership. All leaves remain default off;
clean committed-package, target-platform/driver, and retained visual-review
promotion remain open. See the [user controls and limitations](../user/advanced-screen-space-lighting.md)
and [`airdefense1` optimization evidence](airdefense1-optimization-evidence.md).

The same foundation case requires `rendererContractsSelfTest` and
`rendererGpuSkinningSelfTest` to pass without a skip. The Vulkan startup case
runs those commands through the Vulkan module and requires the identical
semantic markers plus `GPU skinning:` diagnostics. The dependency-light
`openq4-renderer-contracts` native test covers ordered/repeated material passes,
GL/Vulkan depth and viewport conversion, legacy and exact-skinned layouts, and
typed stale/expired/overflowing buffer slices on every native-test platform.

Milestone E has two dependency-light native tests. `openq4-temporal-presentation-core`
covers delayed sample identity, bounded/aligned scale decisions, hysteretic
recovery, unsupported timing, and capture policy. `openq4-temporal-history-core`
covers view/generation continuity, motion-domain ownership, bounded reactive
regions, camera cuts, depth validity, and capture invalidation.
`renderer_temporal_presentation.py` pins the cross-repository ABI, SP/MP camera
cut signaling, game-owned and backend-owned history swap rules, exact
frame/generation depth stamps, GL rigid velocity, Vulkan conservative reactive
fallback, manual mode-0 TAAU, native UI ownership, and SMAA rollback.
`vk_temporal_resolve_shader_pin.py` rebuilds and compares the embedded temporal
SPIR-V when `glslangValidator` is available. Both scripts are syntax-checked and
run by commit/push CI. `renderer_screenshot_readback.py` additionally requires a
UNORM + `SRGB_NONLINEAR` Vulkan swapchain and rejects arbitrary sRGB/HDR-only
fallback so the legacy SDR code values are not encoded twice.

The 2026-08-23 Windows x64 local exit gate used the staged `.install` runtime,
the retail PK4s, a bordered 1280x720 window, engine-written TGAs, and no injected
mouse or keyboard input. OpenGL passed all six required SP scenes plus both MP
listen/connect cases; the MP server roles were rerun with a two-second wall-clock
sample after the first frame-wait schedule outlived the harness timeout. Vulkan
passed the complete eight-case matrix in one run. A forced `0.1` ms controller
target produced valid delayed samples and reduced OpenGL to 50--75% and Vulkan
to 50%, while every passing role retained zero renderer, GL, Vulkan-validation,
fatal, and map-state diagnostic counters.

Targeted runs then covered the boundaries outside the main matrix. A same-path
60% spatial/TAAU comparison and a stable `game/medlabs` comparison retained the
same scene tonality; the initially suspected gray lift was the authored
five-second white fade in `game/storage1`, while mode 0 was an intentionally
cropped legacy path and not a full-output reference. GL and Vulkan save commands
issued at 50% scale each wrote a valid 320x240 preview, froze capture feedback,
advanced history generation from 4 to 6, and reported
`historyReset=synchronous readback`. Disconnecting from 50% gameplay produced a
native 1280x720 main menu on both backends with `historyReset=session unload`.
Finally, separate GL and Vulkan gameplay runs with both temporal controls off
reported 100% scale and the requested SMAA-high path. Engine screenshots enter
the current-frame capture branch by design, so these TGAs prove presentation,
native UI, and capture isolation; accumulated-history admission and blending are
covered by the native/static contracts above rather than claimed from a capture
that deliberately bypasses history.

Gameplay benchmark acceptance should use wall-clock sampling for FPS claims. The `--sample-msec` option emits `waitMsec` into the generated cfg so the measurement window is a real duration rather than a frame count:

```powershell
python tools\tests\renderer_gameplay_benchmark.py --profile smoke --maxfps 0 --swap-intervals 0 --display-modes fullscreen --autoexec-delay-ms 2000 --settle-frames 1 --sample-msec 3000 --pacing-only --min-pacing-hz 120 --max-p95-ms 12 --max-p99-ms 20
```

## Compatibility Gates

`rendererCompatibilityGatesSelfTest` is the Phase 15 fallback-gate test. It does not need a map load; it simulates the driver/capability cases that must never promote the wrong visible path:

| Gate | Expected behavior |
|---|---|
| missing UBO | GL 3.3+ modern baseline is rejected and startup falls back to the legacy compatibility tier when fixed-function compatibility exists |
| broken MRT | G-buffer/deferred ownership is blocked and the tier selector falls back below modern visible ownership |
| missing timer query | renderer GPU timers report unavailable without downgrading an otherwise valid modern tier |
| missing buffer storage | GL 4.5/4.6 low-overhead tier downgrades to the GL 4.3 GPU-driven tier while retaining SSBO/compute coverage |
| rejected debug context | the shared context ladder proves a non-debug fallback candidate exists after debug candidates |
| driver quirk table | known-bad or synthetic driver matches can mask unsafe features before tier selection so `gfxInfo` and renderer bootstrap agree |

`gfxInfo` prints both `Renderer driver quirks:` and `Renderer compatibility gates:`. The quirk line records matched rules and cap changes; the gate line records selected tier, UBO/MRT/timer/buffer-storage readiness, low-overhead readiness, debug fallback, and forced-tier support.

## Default Promotion Criteria

`r_rendererModernAutoPromote` is the sign-off switch for making the guarded modern visible path the automatic choice under `r_glTier auto`. Its default is `0`, and the engine also requires `r_rendererPromotionEvidence` to contain the complete Phase 8 evidence token, so ARB2 remains the default visible renderer until the evidence below is complete. `gfxInfo` prints `Renderer default promotion:` with the current reason, evidence coverage, missing evidence fields, and `Renderer default safety:` with the current conservative-default audit. `rendererDefaultPromotionSelfTest` verifies the promotion gate logic without loading a map, while `rendererDefaultSafetySelfTest` verifies the clean-startup default contract.

| Criterion | Required evidence |
|---|---|
| tier | `r_glTier auto` selects a modern GL 3.3+ tier after driver quirks and compatibility gates are applied |
| renderer escape | `r_renderer best` leaves promotion available; explicit `r_renderer arb2` keeps the ARB2 bridge |
| compatibility gates | modern baseline features, UBOs, MRT, scene packets, render graph, and Shader Library V2 readiness are available |
| fallback escape | the ARB2 compatibility bridge remains selectable through `r_renderer arb2` and `r_glTier legacy` |
| conservative defaults | `r_renderer best` or explicit `r_renderer arb2` keeps ARB2 visible; `r_rendererSharedGui`, `r_rendererSharedInWorldGui`, `r_rendererSharedCinematicPost`, `r_rendererSharedSpecialFrame`, `r_rendererSharedWorldAmbient`, `r_rendererSharedWorldInteraction`, `r_rendererSharedWorldFogBlend`, `r_rendererSharedSubview`, `r_rendererSharedDeform`, `r_vkShadowFallbackTest`, `r_rendererModernAutoPromote`, modern executor/submit/visible/pass/debug paths, GPU validation, bindless, and shader reload all remain off in a clean startup |
| validation evidence | `r_rendererPromotionEvidence` carries the complete Phase 8 token after zero-warning visual, gameplay, RenderDoc, performance, presentation, rollback, and debug-off checks pass |
| manual sign-off | `r_rendererModernAutoPromote 1` is used only together with a complete `r_rendererPromotionEvidence` token |

Required promotion token:

```cfg
r_rendererPromotionEvidence "phase8=complete;warnings=0;visual=pass;gameplay=pass;renderdoc=pass;perf=arb2-or-better;presentation=pass;rollback=pass;debug=off"
```

## Deterministic Capture Matrix

These image captures are the comparison set for scenes where deterministic output is practical. Capture paths should live under `.tmp/renderer-captures/<date>/`, and any checked-in references must be approved separately so the repo does not accumulate accidental binary churn.

| Case | Mode | Scene | Purpose |
|---|---|---|---|
| `capture-startup-mainmenu` | SP | main menu after logo skip | deterministic GUI composition, font/material atlas, and widescreen expansion |
| `capture-shared-gui-mainmenu` | SP | same static main-menu view with `r_rendererSharedGui 0` then `1`, separately on GL and Vulkan | engine-TGA equivalence, identical stable domain coverage/hash for the same authored view, backend-owned count, and zero mixed/dropped pass accounting |
| `capture-shared-gui-rollback` | SP | loading/logo/camera-preview view containing a cinematic, dynamic, current-render, or otherwise unsupported stage | one explicit whole-view fallback, zero shared draws for that view, and classic screenshot equivalence |
| `capture-shared-inworld-gui-owned` | SP | a stable stock 3D GUI surface with `r_rendererSharedInWorldGui 0` then `1`, separately on GL and Vulkan | engine-TGA equivalence, stable tagged-subset hash, nonzero backend-owned tagged subset, correct pre-ambient depth ordering, and zero duplicate GUI draws |
| `capture-shared-inworld-gui-rollback` | SP | the same stable stock 3D GUI surface forced through an unsupported tagged stage/resource/view condition | one named tagged-subset fallback, zero shared tagged draws, and classic screenshot equivalence |
| `capture-shared-world-ambient-owned` | SP | `sp-mv2-ambient` on stock `maps/tools/mv2`, using the `world-ambient` profile with `r_rendererSharedWorldAmbient 0` then `1`, separately on GL and Vulkan | **Passed locally:** exact engine-TGA equivalence, stable domain hash `dc18ed8c0539bbfc`, view hash `bad7344c6394edf8`, one backend-owned pre-fog draw, and zero mixed/dropped pass accounting |
| `capture-shared-world-ambient-rollback` | SP | same `sp-mv2-ambient` view with stock `r_materialOverride shaderDemos/move` to force a deform blocker | **Passed locally:** exact engine-TGA equivalence, `failure=sourceSurfaceFallback detail=13 sourceClass=fallbackDeform`, zero shared draws, and complete classic fallback on GL and Vulkan |
| `capture-shared-interaction-owned` | SP | current `sp-mv2-interaction` controlled stock-asset fixture with fixed camera, two crates, projected test light, and point test light, using `r_rendererSharedWorldInteraction 0` then `1` and `r_shadows 0`, separately on GL and Vulkan | **Passed locally:** same-backend shared/classic TGAs match exactly; both runs own four lights and 14 global receiver surfaces with 28 primitives (14 draws plus 14 no-ops) and exact backend accounting |
| `capture-shared-interaction-stencil` | SP | controlled `sp-mv2-interaction` fixed camera, stock crate, projected test light, and point test light with the `stencil` preset, separately on GL and Vulkan | **Passed locally:** shared/classic TGAs match exactly on both backends; diagnostics reconcile nonzero casters and physical volumes; shadows-off deltas are RMS `5.5301` on GL and `5.5613` on Vulkan |
| `capture-shared-interaction-mapped` | SP | the same controlled scene with the `mapped` preset | **Passed locally:** both backends own projected and six-face point passes with exact record/backend reconciliation and exact classic image parity; shadows-off deltas are RMS `5.4817` on GL and `5.5625` on Vulkan. The synthetic projected light remains single-map, so multi-cascade qualification stays in the stock row below. |
| `capture-shared-interaction-mixed` | SP | the same controlled scene with point shadow maps disabled by the `mixed` preset | **Passed locally:** both backends combine projected mapped ownership with point-light stencil work, reconcile physical replay, match the classic TGAs exactly, and differ from shadows-off at RMS `5.5313` on GL and `5.5625` on Vulkan |
| `capture-shared-interaction-map-fallback` | SP | the same controlled scene with `map-budget-fallback`, which disables the static cache and permits only one update | **Passed locally:** GL and Vulkan each report one named backend fallback, zero committed primitive/shadow/volume/map/hybrid counters, no shared main-target draw, complete fallback coverage, and exact same-settings classic TGA parity |
| `capture-shared-interaction-stock-shadow` | SP | `interaction-shadow-stock`: stock-map qualification candidates for projected, point-cube at ordinary `game/airdefense2` spawn, CSM/parallel, dynamic mapped caster, perforated/cutout, same-light hybrid, and translucent-moment fallback | **Required stock acceptance:** retain the final six-component pose; exact projected/point/multi-cascade classes must be present; dynamic and cutout targets must explicitly report `features` dynamic/alpha; hybrid must report supplements plus physical volumes; translucent moments must fall back atomically; owned shared/classic TGAs must match and their shadowed/shadows-off pairs must materially differ |
| `capture-shared-fog-blend-owned` | SP | controlled `sp-mv2-fog-blend` stock-asset fixture on `maps/tools/mv2`, using shipped `lights/fog_generic` and deterministic `lights/fog_ambient` declarations with `r_rendererSharedWorldFogBlend 0` then `1`, separately on GL and Vulkan | **Passed locally:** exact same-backend engine-TGA parity on GL and Vulkan; each shared run owns one ready view, one fog and one blend light, six GLOBAL receivers, two active stages, seven primitives, three fog receivers, one cap, and three blend draws with exact backend reconciliation and zero mismatch/duplicate/untracked counters |
| `capture-shared-fog-blend-effect-deltas` | SP | the same fixed controlled scene with matching established `r_skipFogLights 1` and owned `r_skipBlendLights 1` references | **Passed locally:** mixed versus fog-only changes `1,921,110` RGB channels at RMS `36.7624` / maximum `71` on both backends; fog-only versus the outer phase skip changes `2,764,380` channels on GL and `2,764,369` on Vulkan at RMS `61.3216` / maximum `162` |
| `capture-shared-fog-blend-rollback` | SP | the same controlled stock-asset view with intentional `r_singleTriangle 1` admission blocker | **Passed locally:** `failure=unsupported-state detail=200`, one complete-phase fallback on the active backend, zero committed shared content counters, no shared main-target fog/blend draw, and exact same-settings classic engine-TGA parity on GL and Vulkan |
| `capture-shared-fog-authored-stock` | SP/MP | a fixed retained camera in an authored stock fog scene such as `game/storage2` or `mp/q4dm10` | **Required; pending:** exact same-backend classic/shared TGA parity, nonempty reconciled fog receiver/cap ownership, material fog-on/off delta, final six-component pose, and clean Vulkan validation results; this does not substitute for controlled blend-light qualification |
| `capture-shared-subview` | SP | fixed direct-mirror, dynamic mirror/reflection, x-ray, remote-camera, refraction, multi-level nested special-view chains, and qualifying 2D/cubemap color/depth captures with `r_rendererSharedSubview 0` then `1`, separately on GL and Vulkan | **Required; pending:** exact same-settings engine-TGA parity, nonzero direct/capture and root-tree ownership, plus named zero-commit malformed-semantic, target-aspect, cube-face, and descendant rollback. R6 qualifies only the single color-2D capture-backed mirror coupled to the dynamic tail in the next row; it does not qualify these broader forms. In-world GUI retains its separate tagged-subset boundary. |
| `capture-shared-cinematic-root` | SP | a frozen eligible root `videoMap`/`soundMap` view and an ordinary-root authored-post view with `r_rendererSharedCinematicPost 0` then `1`, separately on GL and Vulkan | **Required; pending:** exact classic/shared engine-TGA parity, fixed cinematic clock, nonzero root/stage/backend ownership, zero mismatch/duplicate counters, and named zero-commit timing/source/backend fallback. The R6 nested fixture is not root-view evidence. |
| `capture-shared-nested-cinematic-post` | SP | generated `maps/tools/milestone_d_nested_dynamic`: one stock-material capture-backed color-2D mirror plus a frozen stock ROQ and one `_currentRender` authored-post stage visible only in the child view | **Passed final controlled Windows GL/Vulkan acceptance:** all 12 cases pass; all four exact comparisons per API have RMS `0`, maximum delta `0`, and zero differing channels. Both-on normal owns one mirror and one nested cinematic/post transaction on the active backend, with cinematic owned/fallback/mismatch/duplicate `1/0/0/0` and subview owned/fallback `1/0`; both-on skip reports `nestedCinematicPostFallback` with `0/1/0/0` and `0/1`; the inactive backend remains all-zero and independent-CVar cases remain classic-equivalent. Normal versus skip changes 46,359 channels at RMS `2.0073` / maximum `72` on GL and 3,329 channels at RMS `0.2506` / maximum `18` on Vulkan. `currentDepth=0`, so `_currentDepth` remains pending. |
| `capture-shared-render-demo` | SP | a retained stock render-demo with `r_rendererSharedSpecialFrame 0` then `1` | **Required; pending:** exact classic/shared engine-TGA parity, active-session/demo-version provenance, complete view ownership, and named zero-commit incomplete-session/source/backend fallback |
| `capture-shared-raven-special-frame` | SP | fixed Raven blur-only, AL-only, and combined blur/AL controller cases | **Required; pending:** exact classic/shared engine-TGA parity, exact nonempty controller mask, all-effects completion before ownership, and named zero-commit resolve/effect/backend fallback |
| `capture-renderer-visible-selftest` | safe startup | `rendererModernVisibleSelfTest` | synthetic modern-visible depth/G-buffer/deferred/forward+/hybrid-scene/present composition with shadow-policy handoff |
| `capture-renderer-compatibility-selftest` | safe startup | `rendererModernCompatibilitySelfTest` | known fallback inventory for GUI/post/subview/render-demo/BSE categories |
| `capture-sp-airdefense1-static` | SP | `game/airdefense1` fixed spawn, no input for 3 seconds | outdoor lighting, terrain decals, BSE smoke, and stock material parity |

The nested Milestone D case is generated and retained only under `.tmp`; it is
not distributable game content. The fixture tool hash-verifies and retrieves
the user's extracted retail `maps/tools/mv2.map` and `video/idlogo.roq`, appends
original validation-only geometry/material text, compiles `.proc`/`.cm` with
openQ4's `dmap`, and stages an isolated runtime below `.tmp/stock-runtime`.
No retail map, ROQ, or generated fixture is committed or shipped. The map is a
visual renderer fixture with no AI actors; missing renamed AAS files are an
explicit non-navigation warning allowance for this case, not warning-clean
release evidence.

The final controlled evidence is the R6 acceptance report at
`.tmp/renderer-milestone-d/acceptance-20260823-r6-all/renderer_milestone_d_acceptance_report.json`
(SHA-256
`b49031c48267fbfa86c295082707525a390d6090c24f600d00644237a67b252a`),
bound to fixture manifest
`.tmp/renderer-milestone-d/qualification-20260823-r6/fixture_manifest.json`
(SHA-256
`24a2a4926f4b1263e311ad762bf40d391b33de225732d12154ebd7625705282d`)
and runtime
`.tmp/stock-runtime/milestone-d-qualification-20260823-r6`, whose manifest
SHA-256 is
`dbece44597ec5e0686eed215f6ae99c5d6ba4cdd959c1efa7c4acaf1bc2ca673`.
The 3,884-file, 7,281,560,043-byte stock dependency inventory remained
immutable under seal
`13abad18f70eb8b4bf6ea0e9697b317718bb065ee76f86070787005b045dda7a`;
the eight-file, 25,330,583-byte fixture evidence inventory remained immutable
under seal
`0572fb8d9c132f7e97060345464f97750e59e69819871fce90eed13d1348d8ae`.
Runtime, fixture, and stock verification failures were empty. The matching
foundation report at
`.tmp/renderer-validation/milestone-d-foundation-20260823-r6/renderer_validation_report.json`
(SHA-256
`0659f1f2ee5108a422cd6c41bdc78809731f25ad7eda4b5ee84328a9163ce7ac`)
passed; its accompanying Markdown has SHA-256
`a619169ee97f2d6e8c7dfab991adbab5b5b282fd2e1197e2d59eb3609c87cddf`.
The fixed capture script disabled input and used actual windowed mode `-1` at
1280x720, pacing-only sampling, GPU timers off, and 1280x720
engine-render-target TGA screenshots.
This is controlled development-runtime evidence, not final-package, human-review,
non-Windows, broad subview/in-world-GUI/special-frame, root cinematic/post, or
`_currentDepth` qualification.

```powershell
python tools/tests/renderer_milestone_d_fixture.py `
  --output-dir .tmp/renderer-milestone-d/<fixture-run> `
  --runtime-dir .tmp/stock-runtime/<fixture-runtime>
python tools/tests/renderer_milestone_d_acceptance.py `
  --runtime-dir .tmp/stock-runtime/<fixture-runtime> `
  --fixture-manifest .tmp/renderer-milestone-d/<fixture-run>/fixture_manifest.json `
  --render-api all `
  --output-dir .tmp/renderer-milestone-d/<acceptance-run>
```

## RenderDoc Tier Checklist

Capture with `r_rendererMetrics 2`, `r_rendererGpuTimers 1` when available, and the matching forced tier. Every capture should show named debug scopes and object labels for graph resources, modern executor buffers/programs, and pass-owned FBOs.

| Forced tier | Capture focus |
|---|---|
| `r_glTier gl33` | VAO/VBO/UBO baseline, graph resources, visible-depth/G-buffer/forward+ passes |
| `r_glTier gl41` | macOS-class GLSL path and GL 4.1 context fallback behavior |
| `r_glTier gl43` | SSBO scene records, compute validation dispatch, indirect-command generation |
| `r_glTier gl45` | DSA texture/FBO updates, persistent upload defaults, and multi-bind groups |
| `r_glTier gl46` | top-tier selection plus GL SPIR-V/bindless availability reporting without default use |

## Shader Library Tier Checklist

These safe cases run `rendererShaderLibrarySelfTest` under forced GL tiers and require `gfxInfo` to expose Shader Library V2 program/reflection coverage. They use assetless startup automatically inside `renderer_validation_matrix.py`.

| Case | Forced tier | Coverage |
|---|---|---|
| `shader-library-gl33` | `r_glTier gl33` | GLSL 330 Shader Library V2 compile, link, exact-version lookup, and sampler reflection |
| `shader-library-gl41` | `r_glTier gl41` | GLSL 330/410 Shader Library V2 coverage for the macOS-class GL 4.1 portability floor |
| `shader-library-gl43` | `r_glTier gl43` | GLSL 330/410/430 Shader Library V2 coverage alongside GPU-driven SSBO-capable tiers |
| `shader-library-gl45` | `r_glTier gl45` | GLSL 330/410/430/450 Shader Library V2 coverage alongside low-overhead DSA-capable tiers |
| `shader-library-gl46` | `r_glTier gl46` | top-tier Shader Library V2 coverage with the highest selected GLSL variant and reflected sampler bindings |

## Long-Run Matrix

These are manual long-run sign-off loops. They are intentionally outside the safe runner until map startup is reliable enough to automate.

| Case | Mode | Purpose |
|---|---|---|
| `longrun-vid-restart-10x` | SP | repeat `vid_restart` ten times under `r_glTier auto`, `gl33`, and the highest supported forced tier; inspect logs after each cycle |
| `longrun-map-transition-sp` | SP | transition between `game/airdefense1`, `game/storage2`, and `game/medlabs` without restarting the process |
| `longrun-mp-listen-reconnect` | MP | `mp/q4dm1` listen server with local client connect, disconnect, reconnect, then map restart |

## Performance Regression Thresholds

`rendererBenchmarkCapture` prints a rolling benchmark line when `r_rendererMetrics` is enabled. The safe matrix records the threshold table in its Markdown and JSON reports so hardware-specific performance triage can compare the same budget shape across runs. Local threshold cvars override the preset defaults for target-machine experiments.

| Preset | P95 target | P99 target | Screen | Cluster grid | Material batch | Light batch | Shadow budget | Post budget |
|---|---:|---:|---:|---|---:|---:|---|---:|
| `low` | 33 ms | 50 ms | 75% | 4x3x8 | 32 | 16 | 512 px / every 2 frames | 0 |
| `baseline` | 20 ms | 28 ms | 100% | 6x4x12 | 64 | 32 | 1024 px / every frame | 1 |
| `modern` | 16 ms | 24 ms | 100% | 8x6x16 | 96 | 64 | 1024 px / every frame | 2 |
| `high-end` | 12 ms | 18 ms | 100% | 8x6x16 | 128 | 96 | 2048 px / every frame | 3 |

These preset values are renderer workload/scalability defaults, not per-map
measurements. Promotion evidence uses the separate, versioned
`tools/validation/renderer_per_map_budgets.json` contract. Each row is selected
by the exact active map, launch-derived backend (`opengl` or `vulkan`), and
benchmark profile, and carries minimum sample counts plus independent CPU and
whole-frame GPU P95/P99 ceilings in integer microseconds. The initial v1 rows
deliberately apply the baseline 20/28 ms target to every listed stock scene on
both backends. They are cross-map target ceilings, not a claim that every row
was measured at those values; a captured report retains each row's actual
samples and percentiles so confirmed target-machine results can tighten a row
without changing the schema.

`rendererBenchmarkCapture` supplies the budget tools with one backend-neutral
line of this exact shape:

```text
OPENQ4_FRAME_TIMING_V1 map=game/storage1 backend=opengl profile=baseline cpuSamples=256 cpuP50Us=7000 cpuP95Us=11000 cpuP99Us=14000 gpuAvailable=1 gpuSamples=252 gpuP50Us=5000 gpuP95Us=8000 gpuP99Us=10000
```

CPU and GPU percentiles must be monotonic. GPU values come from nonblocking
whole-frame backend timestamps rather than a sum of overlapping pass timers.
An unsupported/unresolved backend reports `gpuAvailable=0`, zero samples, and
`-1` for all three GPU percentiles; because promotion rows require GPU timing,
that representation fails closed. The verifier rejects malformed lines,
multiple markers in one source, conflicting mirrored sources, identity or
sample-count mismatches, missing exact budget rows, exceeded ceilings, changed
contract hashes, altered runtime/artifact bytes, and recorded measurements that
do not recompute from the retained log streams.

### Milestone A current-build evidence snapshot

The 2026-08-19 development snapshot verifies that the implementation works as
an integrated system, but it is not final-package or universal performance
evidence:

| Gate | Current development-build result |
|---|---|
| Job parity | PASS: jobs-on and jobs-off `game/storage1` produced identical engine TGA bytes and matching game-state evidence. |
| Repeated lifecycle | PASS: jobs-on/off OpenGL and jobs-on Vulkan completed the five-map campaign through `game/tram1`; each run ended with zero initialized, queued, or running jobs. |
| Dedicated lifecycle | PASS: five independent dedicated-server runs exited normally, each with one synchronous job self-test and one clean shutdown marker. |
| Backend timing | PASS for the exercised storage and campaign captures: OpenGL and Vulkan returned delayed whole-frame GPU samples without a current-frame wait. |
| Retail baseline | PASS locally: the schema-10 four-role OpenGL capture and its immediate replay retained pure MP, `ui_autoJoin 1`, canonical display/budget evidence, artifacts, and source/runtime identities; engine screenshots and the save preview also passed local human review. |
| Required map budgets | PASS locally: the immutable `milestone-a-20260819-final3` runtime passes and replay-verifies all eight OpenGL cases in `ma-a-gl3` and all eight Vulkan cases in `ma-a-vk3`. The corrected `game/medlabs` debug-context run records zero GL errors after depth bounds are ordered and clamped before submission. |

The renderer sweep uses the immutable current-build runtime named above, staged
from an uncommitted development tree. Promotion still requires clean committed
source provenance, a freshly staged final package, retained reports/artifacts
and release review, and separate platform/driver qualification. The local 8/8
results do not establish a universal performance level.

## Manual Gameplay Matrix

Gameplay validation remains mandatory before renderer release sign-off, but it is not run by the safe matrix by default because map loads need target-hardware supervision. Use the SP launch task for single-player maps, the MP launch task or `tools\debug\start_listen_server_client.ps1` for multiplayer, or the opt-in gameplay benchmark harness below when you want a repeatable logged capture set.

| Case | Mode | Map | Purpose |
|---|---|---|---|
| `sp-storage1` | SP | `game/storage1` | primary high-FPS renderer acceptance scene, dense indoor lighting, and early-game storage visual parity |
| `sp-airdefense1` | SP | `game/airdefense1` | stock SP baseline, outdoor lighting, BSE smoke |
| `sp-airdefense2` | SP | `game/airdefense2` | flashlight, projected shadows, animated characters |
| `sp-storage2` | SP | `game/storage2` | indoor materials and post-process coverage |
| `sp-bse-heavy` | SP | `game/medlabs` | stress BSE effects without replacement content |
| `sp-cinematic-subview` | SP | `game/mcc_landing` | subviews, remote cameras, cinematic and GUI interaction |
| `sp-mv2-ambient` | SP | `maps/tools/mv2` | controlled stock fixed-function world-ambient ownership and whole-view rollback evidence |
| `sp-mv2-interaction` | SP | `maps/tools/mv2` | controlled stock unshadowed, stencil, projected/point mapped, mixed map/stencil, and map-budget whole-view fallback evidence |
| `sp-mv2-fog-blend` | SP | `maps/tools/mv2` | locally passed controlled stock-declaration fog, blend, mixed-phase ownership, visible effect-delta, and complete-phase rollback evidence; authored-stock and release promotion remain open |
| `mp-q4dm1-listen` | MP | `mp/q4dm1` | listen-server and local-client MP parity |
| `mp-q4dm9-listen` | MP | `mp/q4dm9` | default-off load-cache timing and forced Vulkan shadow-ownership fallback regression |

For each gameplay case, validate the matrix variants that the hardware supports:

| Dimension | Values |
|---|---|
| `r_glTier` | `auto`, `legacy`, `gl33`, `gl41`, `gl43`, `gl45`, `gl46` |
| renderer escape | `r_renderer best`, `r_renderer arb2`, `r_glTier legacy` |
| `r_swapInterval` | `0`, `1` |
| `com_maxfps` | `120`, `240`, `0` |
| GPU animation | paired `r_gpuSkinning 0` CPU reference and `r_gpuSkinning 1` exact-contract run on each backend; require admitted vertices and a classified CPU stencil fallback |
| display mode | windowed, fullscreen |
| renderer diagnostics | `r_rendererMetrics 1`, `r_rendererMetrics 2`, `r_rendererModernAutoPromote 0`, and one signed `r_rendererModernAutoPromote 1` candidate run with the complete `r_rendererPromotionEvidence` token after the other rows are clean |

After each gameplay smoke, inspect the configured log file under `fs_savepath\<gameDir>\logs\openq4.log` or the case-specific log emitted by the launch tool. Fix errors and warnings, then repeat the loop until the case is clean.

Milestone C GPU-animation captures follow the stricter backend-local A/B
procedure in [Shared Renderer Contracts and GPU Animation](gpu-skinning-modernization.md):
freeze the pose from launch with `g_stopTime 1`, compare each GPU run only with
its own backend's CPU reference, use engine-written screenshots, retain pure
auto-joined MP evidence, and repeat the animation-heavy
`evaluateMPPerformance` workload at least three times. CPU-frame improvement is
not accepted unless the diagnostics prove that eligible vertices were actually
admitted and every fallback has a named reason.
Validate each retained backend-local CPU/GPU JSON pair with the
`tools/validation/gpu_skinning_evidence.py` verifier at the default one-percent
CPU-P95 gate; the complete command, launch identity, and artifact contract is
documented in the GPU-animation guide.

## Gameplay Benchmark Harness

`tools\tests\renderer_gameplay_benchmark.py` is the Phase 12 map-loading runner. It launches the staged client from `.install` or a named ordinary package below `.tmp\stock-runtime\`, uses isolated save paths under `.tmp\renderer-gameplay\`, enters SP maps or a pure MP listen server plus auto-joining loopback client, waits for streaming, runs a fixed static spawn camera path unless a case is later extended with authored poses, captures screenshots, emits `rendererBenchmarkCapture`, `framePacingSnapshot`, and `gfxInfo`, and writes Markdown/JSON reports. The alternate root deliberately matches `stage_fast_install.py --temporary-runtime` and the stock-baseline harness, so one immutable staged package can be hashed and exercised by both evidence tools. The report binds the selected runtime path, executable, every runtime-file hash, current Git state, budget-contract id/hash, launch-derived backend, thresholds, and measured CPU/GPU percentiles.

Capture output directories must be new or empty. The runner hashes the complete
runtime before launch and again after every case; any package mutation fails the
run, while later `--verify-report` replay requires the same runtime and retained
log/stdout/stderr/screenshot bytes. Generated configs, caches, and screenshots
stay below each role's isolated save path rather than writing into the staged
package.

Per-map CPU/GPU budget evidence has one comparable display contract:
`r_fullscreen 0`, `r_borderless 0`, `r_borderlessDefaultMigrated 1`,
`r_fullscreenDesktop 0`, and a 1280x720 drawable selected through both
`r_windowWidth`/`r_windowHeight` and `r_mode -1` plus `r_customWidth`/
`r_customHeight`. The harness applies those startup values after optional
launch CVars, records the exact contract in every result, and replay rejects a
different size, border policy, or fullscreen result. A passing role must also
contain `gfxInfo` runtime evidence equivalent to `MODE: -1, 1280 x 720
windowed`, and its engine-written TGA must decode at exactly 1280x720; replay
recomputes both from the hashed artifacts instead of trusting report fields.
`--width` and `--height`
are available for pacing-only diagnostics; non-pacing budget runs reject any
size other than 1280x720. Fullscreen coverage remains a presentation/pacing
test and is not promotable CPU/GPU budget evidence.

The runner uses the SP/MP `g_autoExecAfterMapLoad` hook to execute its generated cfg after the map is active, not during loading UI. Renderer metrics are enabled only inside the gameplay capture window, which keeps load-screen logs quiet while still producing benchmark samples, GPU timing where available, frame-pacing output, and a screenshot artifact.

Use `--pacing-only` for high-FPS presentation acceptance after a diagnostic metrics pass is already clean. This keeps renderer metrics, GPU timestamps, and the FPS overlay out of the timed window, still emits `framePacingSnapshot`, and can fail the run with presentation-only thresholds such as `--min-pacing-hz 120 --max-p95-ms 12`. A pacing-only report explicitly records that per-map CPU/GPU budgets were not enforced and cannot pass budget-evidence replay. The `game/storage1` acceptance run should start sampling two seconds after the active map draw with `r_swapInterval 0` and `com_maxfps 0` so the result measures renderer throughput rather than the old low-FPS plan cap.

Common runs:

```powershell
python tools\tests\renderer_gameplay_benchmark.py --list
python tools\tests\renderer_gameplay_benchmark.py --profile smoke
python tools\tests\renderer_gameplay_benchmark.py --profile smoke --render-api gl --runtime-dir .tmp\stock-runtime\current-build
python tools\tests\renderer_gameplay_benchmark.py --profile smoke --render-api vk --runtime-dir .tmp\stock-runtime\current-build
python tools\tests\renderer_gameplay_benchmark.py --profile smoke --pacing-only --autoexec-delay-ms 2000 --min-pacing-hz 120 --max-p95-ms 12
python tools\tests\renderer_gameplay_benchmark.py --profile required
python tools\tests\renderer_gameplay_benchmark.py --profile campaign-split-state-transition --timeout 360
python tools\tests\renderer_gameplay_benchmark.py --profile world-ambient --pacing-only --no-gpu-timers
python tools\tests\renderer_gameplay_benchmark.py --profile fog-blend --pacing-only --no-gpu-timers
python tools\tests\renderer_gameplay_benchmark.py --profile interaction --render-api gl --pacing-only --no-gpu-timers --reference-dir .tmp\renderer-references\interaction\gl\classic\savepaths --require-references --image-rms-threshold 0 --image-max-threshold 0 --difference-reference-dir .tmp\renderer-references\interaction\gl\shadows-off\savepaths
python tools\tests\renderer_gameplay_benchmark.py --profile interaction --render-api vk --pacing-only --no-gpu-timers --reference-dir .tmp\renderer-references\interaction\vk\classic\savepaths --require-references --image-rms-threshold 0 --image-max-threshold 0 --difference-reference-dir .tmp\renderer-references\interaction\vk\shadows-off\savepaths
python tools\tests\renderer_gameplay_benchmark.py --profile interaction-shadow-stock --render-api gl --pacing-only --no-gpu-timers --reference-dir .tmp\renderer-references\interaction-shadow-stock\gl\classic\savepaths --require-references --image-rms-threshold 0 --image-max-threshold 0 --difference-reference-dir .tmp\renderer-references\interaction-shadow-stock\gl\shadows-off\savepaths
python tools\tests\renderer_gameplay_benchmark.py --profile interaction-shadow-stock --render-api vk --pacing-only --no-gpu-timers --reference-dir .tmp\renderer-references\interaction-shadow-stock\vk\classic\savepaths --require-references --image-rms-threshold 0 --image-max-threshold 0 --difference-reference-dir .tmp\renderer-references\interaction-shadow-stock\vk\shadows-off\savepaths
python tools\tests\renderer_gameplay_benchmark.py --profile deform --render-api gl --pacing-only --no-gpu-timers
python tools\tests\renderer_gameplay_benchmark.py --profile deform --render-api vk --pacing-only --no-gpu-timers
python tools\tests\renderer_gameplay_benchmark.py --profile smoke --cases mp-q4dm9-listen --render-api vk --shadow-presets mapped --pacing-only --no-gpu-timers
python tools\tests\renderer_gameplay_benchmark.py --profile tiers
python tools\tests\renderer_gameplay_benchmark.py --profile presentation --pacing-only
python tools\tests\renderer_gameplay_benchmark.py --profile shadows
python tools\tests\renderer_gameplay_benchmark.py --runtime-dir .tmp\stock-runtime\current-build --verify-report .tmp\renderer-gameplay\candidate\renderer_gameplay_benchmark_report.json
```

The runner fails a case when the process times out, no gameplay screenshot is produced, the benchmark/gfxInfo/timing lines are missing, the exact map/backend/profile budget is absent, required CPU/GPU samples are unavailable or over budget, image comparison fails when references are required, or renderer warning markers such as `idStr::snPrintf: overflow`, `WARNING: idStr`, shader compile failures, program link failures, or fatal graphics startup failures appear in the log. MP benchmark roles always use `ui_autoJoin 1`, `si_pure 1`, and `net_serverAllowServerMod 0`.

| Profile | Coverage |
|---|---|
| `smoke` | bounded `game/storage1` SP gameplay smoke with screenshot, metrics, frame-pacing snapshot, and zero-warning log gates |
| `required` | `game/storage1`, `game/airdefense1`, `game/airdefense2`, `game/storage2`, `game/medlabs`, `game/mcc_landing`, and `mp/q4dm1` listen server plus local client |
| `campaign-split-state-transition` | triggers the real SP end-level targets from `game/mcc_2` through `game/storage1 first`, `game/storage2`, `game/storage1 second`, and into `game/tram1`, asserting the active `si_entityFilter` after each load |
| `world-ambient` | controlled bordered 1280x720 `maps/tools/mv2` stock capture; launch sets `ui_showGun 0`, `g_showHud 0`, and `r_multiSamples 0`, then normal spawn runs `noclip` and `setviewpos 0 0 256 80 0 0`; the exact post-map isolation set is recorded in the world-ambient guide and keeps ambient/deform/render enabled while disabling direct-light, subview, light-grid, player-overlay, portal-fade, cel, and debug islands through their structural controls. It does not set `r_skipPostProcess` or `r_skipGuiShaders`. Run with `--pacing-only --no-gpu-timers`, so it is not CPU/GPU budget evidence. |
| `deform` | controlled bordered 1280x720 `maps/tools/mv2` capture with the same fixed camera, the shipped `shaderDemos/move` material override, `r_rendererSharedWorldAmbient 1`, and `r_rendererSharedDeform 1`. The profile starts game time frozen, advances the common settle interval at exactly one game tic per rendered frame, freezes before sampling, and disables mouse input so separate runs retain the same deform value and camera. Separate same-backend classic references must match exactly with nonzero completed deform ownership; an otherwise identical `r_skipDeforms 1` reference must differ, and an unsupported/skipped/failure case must report named zero-commit rollback. Run with `--pacing-only --no-gpu-timers`; it is visual/ownership evidence, not budget evidence. |
| `fog-blend` | controlled bordered 1280x720 `maps/tools/mv2` qualification using only shipped fog and deterministic blend-light declarations, fixed camera/settings, separate GL/Vulkan classic references, positive established `r_skipFogLights` outer-skip and owned `r_skipBlendLights` effect references, exact ownership reconciliation, and a named complete-phase fallback. The development-worktree set passes on both backends. Run with `--pacing-only --no-gpu-timers`; it is visual/ownership evidence, not CPU/GPU budget evidence. Exact results and remaining release gates are recorded in the fog/blend domain guide. |
| `interaction` | controlled bordered 1280x720 `maps/tools/mv2` capture with fixed camera `0 -192 96 20 90 0`, a stock `crate1_small` caster, a stock `crate1_medium` receiver, a projected `testLight`, and a side `testPointLight`; the separated receiver makes stencil and mapped silhouettes materially visible. Five presets cover unshadowed, stencil, projected+point mapped, projected-map+point-stencil mixed, and constrained map-update fallback. Run with `--pacing-only --no-gpu-timers`; require exact same-settings classic references plus the matching unshadowed difference reference. The synthetic projected light remains single-map under the CSM preset, so multi-cascade acceptance stays in the stock profile. |
| `interaction-shadow-stock` | stock-map qualification candidates for projected, point, CSM/parallel, dynamic mapped-caster, perforated/cutout, same-light hybrid-supplement, and translucent-moment fallback coverage. Each run starts frozen at tic zero, hides the unsupported first-person viewmodel, advances the real SP scene through the settle interval, freezes before sampling, and records the final six-component pose. Labels never establish coverage: the parser requires an exact projected class, per-map class/cascade/alias/plan/generation/caster/hash data, and `features=static+dynamic+alpha+translucent`; dynamic and perforated cases fail without their explicit feature bit. |
| `tiers` | forced `r_glTier auto`, `legacy`, `gl33`, `gl41`, `gl43`, `gl45`, and `gl46` gameplay probes |
| `presentation` | pacing-only `r_swapInterval 0/1`, `com_maxfps 0/120/240`, windowed, and fullscreen coverage for uncapped/high-refresh validation; never budget-promotion evidence |
| `shadows` | stencil fallback, mapped shadows, CSM, translucent moments, and debug-overlay modes `1..6` over the shadow correctness scenes |
Optional deterministic image comparison uses TGA references:

```powershell
python tools\tests\renderer_gameplay_benchmark.py --profile smoke --reference-dir .tmp\renderer-references --require-references
```

The two interaction acceptance commands above are intentionally stricter than
the generic optional comparison: `--require-references` binds every capture to
its same-settings shared-off/classic TGA at exact RMS/max delta zero, while
`--difference-reference-dir` binds each owned shadow case to the matching
unshadowed case id and requires a material shadow delta. Generate those two
reference trees in separate shared-off and shadows-off runs before executing an
acceptance command. Generate and consume separate references for each renderer;
replace `<profile>` with `interaction` or `interaction-shadow-stock` and
`<backend>` with `gl` or `vk` in this three-run workflow:

```powershell
python tools\tests\renderer_gameplay_benchmark.py --profile <profile> --render-api <backend> --pacing-only --no-gpu-timers --set-cvar r_rendererSharedWorldInteraction=0 --output-dir .tmp\renderer-references\<profile>\<backend>\classic
python tools\tests\renderer_gameplay_benchmark.py --profile <profile> --render-api <backend> --shadow-presets unshadowed --pacing-only --no-gpu-timers --set-cvar r_rendererSharedWorldInteraction=0 --output-dir .tmp\renderer-references\<profile>\<backend>\shadows-off
python tools\tests\renderer_gameplay_benchmark.py --profile <profile> --render-api <backend> --pacing-only --no-gpu-timers --reference-dir .tmp\renderer-references\<profile>\<backend>\classic\savepaths --require-references --image-rms-threshold 0 --image-max-threshold 0 --difference-reference-dir .tmp\renderer-references\<profile>\<backend>\shadows-off\savepaths
```

The reference roots include `savepaths` because each generated capture lives at
`<output-dir>\savepaths\<case-id>\baseoq4\screenshots\...`. Omitting either
tree is not a passing interaction result.

Nondeterministic BSE, cinematic, and MP scenes need human review in addition to the automated log/screenshot gates:

| Case | Focus | Checks |
|---|---|---|
| `sp-bse-heavy` | BSE-heavy effects in `game/medlabs` | effect sprites/trails animate at the expected cadence, no black quads, no missing additive passes, no warning spam |
| `sp-cinematic-subview` | cinematic/subview flow in `game/mcc_landing` | remote-camera/subview content is visible, GUI overlays composite in the right order, cinematic handoff keeps frame pacing stable |
| `mp-q4dm1-listen` | local MP listen server plus loopback client | client reaches the map, player/world lighting matches host expectations, frame pacing remains uncapped when requested |
| `mp-q4dm9-listen` | local MP q4dm9 load and Vulkan mapped-shadow fallback | ordinary runs force `com_levelLoadModernization 0` and must show no generated-cache writes; explicit cache-on A/B evidence records map phases. A focused run with `r_vkShadowFallbackTest 1` must retain a lit engine TGA, log `affected light receivers fall back unshadowed`, omit the former `receivers are skipped fail-closed` diagnostic, and remain free of Vulkan validation/VUID/call failures. |

## Shadow Correctness Matrix

| Case | Mode | Map | Purpose |
|---|---|---|---|
| `shadow-projected-airdefense2` | SP | `game/airdefense2` | angled projected-light caster/receiver validation |
| `shadow-point-storage2` | SP | `game/storage2` | point-light face coverage and local-light receiver validation |
| `shadow-csm-airdefense1` | SP | `game/airdefense1` | CSM camera sweep readiness and outdoor directional coverage |
| `shadow-cutout-storage2` | SP | `game/storage2` | hashed-alpha cutout fence/grate caster validation at distance |
| `shadow-character-airdefense2` | SP | `game/airdefense2` | dynamic character shadow caster and receiver validation |
| `shadow-translucent-medlabs` | SP | `game/medlabs` | optional translucent moment caster coverage where the selected tier supports it |

## Acceptance

- Automated safe matrix passes after build and install.
- Manual gameplay matrix reaches in-game/map gameplay for every required SP/MP case on supported hardware.
- Logs are inspected after every run.
- No stock-asset compatibility overrides are added as a validation shortcut.
- RenderDoc validation remains limited to forced modern/core bring-up paths until the visible renderer no longer depends on ARB2 compatibility features.
- Benchmark captures report P50/P95/P99 frame pacing, active preset budgets, and threshold pass/fail status before any claim that the modern visible path matches or beats ARB2 on target scenes.
- `rendererDefaultSafetySelfTest` and `rendererDefaultPromotionSelfTest` pass before any default-promotion discussion.
- `r_rendererModernAutoPromote 1` is used only with the complete `r_rendererPromotionEvidence` token after the default-promotion criteria pass; `r_renderer arb2`, `r_glTier legacy`, and the modern-disable cvar set remain documented rollback paths.
- Shared world-ambient local runtime qualification passed: retained GL and Vulkan option-off/on engine screenshots for the same eligible stock `maps/tools/mv2` view match at RMS `0` / maximum delta `0`, enabled diagnostics report one owned pre-fog draw with stable domain/view hashes, and the stock `shaderDemos/move` deform override reports zero shared draws plus the same named complete-view fallback on both backends. Vulkan validation gates are clean. Exact directories and SHA-256 values are recorded in [Shared Classic World Ambient/Material Domain](classic-world-ambient-domain-modernization.md). The domain is implemented and remains default-off pending clean committed-package and target-platform/driver promotion evidence.
- Shared interaction's local qualification passed for the current controlled corridor. Its five-case profile passed 5/5 on GL and 5/5 on Vulkan with exact same-backend classic/shared TGAs, four lights, 14 global receiver surfaces, material shadows-on/off deltas for stencil/mapped/mixed ownership, exact backend accounting, and named map-admission fallbacks with zero committed shared counters and no shared main-target draw. The earlier two-light/four-surface capture is superseded. The stock-map row remains a release-qualification gate: its projected/CSM/dynamic/perforated/hybrid candidates need retained final cameras and fresh classic/shadows-off references before they can pass. Exact scope and deltas are recorded in [Shared Classic Interaction-Lighting Domain](classic-interaction-domain-modernization.md); the option is implemented and remains default-off pending stock, clean committed-package, and target-platform/driver evidence.
- Shared fog/blend's controlled development-worktree qualification passes on GL and Vulkan. Mixed and fog-only shared TGAs match their same-settings classic references exactly; the mixed run owns one fog plus one blend light, six GLOBAL receivers, two stages, seven primitives, three fog receivers, one cap, and three blend draws with exact backend accounting. Mixed/fog-only and fog-only/outer-skip comparisons materially prove the blend and fog contributions, and the intentional blocker produces named zero-commit atomic fallback with exact classic parity. Vulkan warning/VUID/call-failure counters remain clean. Exact hashes, deltas, and local report directories are recorded in [Shared Classic Fog/Blend Domain](classic-fog-blend-domain-modernization.md). Authored-stock fog, clean committed-package recapture, human review, and target-platform/driver retention remain promotion gates.
- Shared material-deform's controlled development-worktree qualification passes on GL and Vulkan. Normal and skipped shared TGAs match their same-settings classic references exactly; normal runs emit two completed records and one owned ambient draw, skipped runs emit two named skipped records and commit zero shared draws, and the normal/skipped comparison materially proves the deform contribution. Native/static and backend diagnostic gates are clean. Exact hashes, deltas, and local report directories are recorded in [Shared Classic Material-Deform Contract](classic-deform-domain-modernization.md). Clean committed-package recapture, human review, and target-platform/driver retention remain promotion gates.
