# Renderer Validation Matrix

This matrix is the validation source of truth for the staged GL renderer work. It separates safe automated startup/self-test coverage from gameplay smoke coverage that must be run manually with the mode-specific SP/MP launch tasks.

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

## Automated Safe Matrix

The safe matrix starts the staged client, runs renderer self-tests or startup probes, prints `gfxInfo`, then quits. It does not launch maps.

```powershell
python tools\tests\renderer_validation_matrix.py
```

For focused validation without relaunching the full matrix, use `--cases` with one or more case ids:

```powershell
python tools\tests\renderer_validation_matrix.py --cases renderer-default-promotion-selftest
```

The runner writes a timestamped report under `.tmp/renderer-validation/` with per-case logs and a JSON copy for CI or release triage.

Automated coverage:

| Case | Coverage |
|---|---|
| `renderer-foundation-selftests` | context ladder, tier selector, tier workload contract, upload manager, GPU timer, lens-flare settings contract, lens-flare accumulation/composite runtime contract, lens-flare Shader Library V2 compile/reflection contract, scene packet, lens-flare post command categorization, render graph, lens-flare transient resource edges, render graph resource owner, material resource table, geometry/instance resource records, GL state cache, Shader Library V2 pass-family/permutation/reflection coverage, draw plan, submit plan, modern executor, and shadow planner self-tests |
| `renderer-vertex-cache-budget-selftest` | focused vertex-cache residency coverage proving the opt-in LRU budget gate purges old static VBO/IBO blocks while preserving recently touched blocks |
| `renderer-visible-depth-selftest` | opt-in `r_rendererModernVisibleDepth` coverage for graph-backed scene depth, compatible shadow-depth resources, fallback accounting, depth-overlay readiness, and `gfxInfo` reporting |
| `renderer-gbuffer-selftest` | opt-in `r_rendererModernOpaque` coverage for graph-backed G-buffer resources, MRT setup, opaque/alpha-test draw classification, diffuse texture binding, packing assumptions, fallback accounting, bandwidth metrics, attachment debug-overlay readiness, and `gfxInfo` reporting |
| `renderer-cluster-grid-selftest` | opt-in modern clustered-light preparation coverage for point/projected/fog/ambient/special light classification, budgeted dynamic grid slicing, cluster reference packing, spill/overflow accounting, GL 3.3 UBO fallback readiness, GL 4.3+ SSBO upload readiness, cluster debug-overlay texture generation, and `gfxInfo` reporting |
| `renderer-shadow-planner-selftest` | modern shadow planner coverage for projected/point/CSM policy, mapped/stencil-fallback/skipped accounting, benchmark-budgeted shadow resolution/light/pixel caps, render-graph shadow resource reporting, clustered shadow descriptor integration, and `gfxInfo` reporting |
| `renderer-deferred-resolve-selftest` | opt-in `r_rendererModernDeferred` coverage for graph-backed deferred resolve output, G-buffer/depth/cluster buffer inputs, point/projected light accumulation, light-grid contribution, fallback accounting, deferred debug-overlay readiness, GPU timer coverage, and `gfxInfo` reporting |
| `renderer-forward-plus-selftest` | opt-in `r_rendererForwardPlus` coverage for graph-backed scene-color/depth resources, clustered opaque/alpha-test/transparent programs, clustered-light UBO/SSBO reads, transparent sort preservation, fallback accounting, overdraw estimates, GPU timer coverage, and `gfxInfo` reporting |
| `renderer-modern-visible-selftest` | opt-in `r_rendererModernVisible` coverage for the guarded hybrid visible-frame bridge: graph-backed depth, G-buffer, deferred resolve, forward+ source output, graph-owned `hybridSceneColor` composition, HDR/post-process handoff before SSAO/bloom/authored post, depth-copy handoff accounting, shadow-ready handoff/fallback accounting, final GUI/present overlay, GPU timer coverage, and `gfxInfo` reporting |
| `renderer-modern-compatibility-selftest` | Phase 14 modern-visible compatibility coverage for command-category ownership inventory, modern fullscreen GUI readiness, light-grid ownership, explicit post/copy/subview/render-demo/BSE fallback buckets, deterministic render-demo accounting, and `gfxInfo` reporting |
| `renderer-compatibility-gates-selftest` | Phase 15 fallback-gate coverage for missing UBO, broken MRT, missing timer query, missing buffer storage, rejected debug-context fallback, and synthetic driver-quirk downgrades |
| `renderer-default-promotion-selftest` | Phase 8 evidence-gated default-promotion coverage for `r_glTier auto`, explicit `r_renderer arb2` escape behavior, compatibility gates, modern-executor readiness, ARB2 rollback availability, missing/incomplete/complete `r_rendererPromotionEvidence`, and `r_rendererModernAutoPromote` sign-off control |
| `renderer-default-safety-selftest` | Phase 13 conservative-default coverage for ARB2 default visibility, `r_renderer best` or explicit `r_renderer arb2`, `r_glTier auto`, rollback availability, and default-off modern executor, visible, diagnostic, GPU-validation, bindless, shader-reload, and auto-promotion cvars |
| `renderer-benchmark-selftest` | Phase 16 benchmark coverage for rolling P50/P95/P99 frame-time capture, CPU front-end/visibility/packet/graph/submit/present timings, GPU pass timing fields, upload/draw/light/cluster/fallback counters, benchmark presets, and performance-threshold reporting |
| `renderer-gpu-driven-selftest` | forced `r_glTier gl43` coverage for GL 4.3 SSBO submit records, compute scissor culling, clustered-bin validation, compacted indirect command generation, CPU/GPU readback comparison, masked multi-draw indirect execution, GPU timer coverage, and `gfxInfo` reporting |
| `renderer-low-overhead-selftest` | forced `r_glTier gl45` coverage for GL 4.5 DSA graph texture/FBO allocation, DSA sampler creation, named buffer/FBO updates, UBO/SSBO/texture/sampler multi-bind batches, submit-batch compaction, bindless experiment reporting, persistent upload defaults, fence diagnostics, and `gfxInfo` reporting |
| `renderer-no-multibind-fallback-selftest` | focused GL state-cache proof that the no-multibind texture fallback handles mixed 2D/cube targets and records fallback batches |
| `renderer-modern-cvar-vid-restart` | assetless `vid_restart` coverage after toggling forced GL tier and opt-in modern executor, visible, visible-depth, opaque, deferred, and forward+ cvars |
| `renderer-resource-count-vid-restart` | assetless repeated `vid_restart` coverage that compares post-restart render-graph texture/FBO, upload/static-buffer/fence, and shader-program counts for drift |
| `renderer-shutdown-leak-audit-vid-restart` | assetless repeated `vid_restart` and final-shutdown coverage that requires renderer live-object audit counters to reach zero after teardown |
| `dedicated-disabled-bse-manager-startup` | dedicated-server startup coverage using `openQ4-ded_<arch>` that preserves the integrated effect-decl allocator with the disabled BSE runtime manager and rejects renderer vertex-cache shutdown errors |
| `shader-lensflare-gl33` | forced `r_glTier gl33` coverage for GLSL 330 lens-flare accumulation/composite compile, link, exact-version lookup, and sampler reflection |
| `shader-lensflare-gl41` | forced `r_glTier gl41` coverage for GLSL 330/410 lens-flare accumulation/composite variants on the macOS-class GL 4.1 portability floor |
| `shader-lensflare-gl43` | forced `r_glTier gl43` coverage for GLSL 330/410/430 lens-flare accumulation/composite variants alongside GPU-driven SSBO-capable tiers |
| `shader-lensflare-gl45` | forced `r_glTier gl45` coverage for GLSL 330/410/430/450 lens-flare accumulation/composite variants alongside low-overhead DSA-capable tiers |
| `shader-lensflare-gl46` | forced `r_glTier gl46` coverage for top-tier lens-flare shader compile/reflection with the highest selected GLSL variant and all reflected sampler bindings |
| `lensflare-signoff` | opt-in gameplay profile, not a safe startup case: cross-platform storage/outdoor lens-flare captures across off/corona/high presets and auto/macOS-floor/low-overhead GL tiers |
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

Automated safe cases also fail if their logs contain renderer warning signatures such as `idStr::snPrintf` overflow, `WARNING: idStr`, shader compile/program link failures, or OpenGL error markers. The generated Markdown/JSON report records per-case warning-signature counts so the Phase 8 `warnings=0` promotion token cannot be inferred from expected-line checks alone.

The visible-depth, G-buffer, clustered-light, deferred-resolve, forward+, modern-visible, modern-compatibility, compatibility-gates, default-promotion, default-safety, benchmark, GPU-driven, low-overhead, vertex-cache budget, modern-cvar restart, resource-count restart, shutdown leak-audit restart, and lens-flare shader-tier self-tests intentionally run as their own safe cases instead of being appended to the foundation self-test startup command, because the engine command parser has a fixed startup command list budget.

The `renderer-modern-cvar-vid-restart` case is assetless restart coverage. It prints `gfxInfo`, requests `r_glTier gl45` plus the opt-in modern executor, visible, visible-depth, opaque, deferred, and forward+ cvars, runs `vid_restart`, then requires a second `gfxInfo` with the forced-tier request, modern-path cvar state, executor summaries, and zero warning signatures.

The `renderer-resource-count-vid-restart` case exercises the same safe assetless restart path across three `vid_restart` cycles. It uses runner-level `valueChecks` to compare the last three post-restart `gfxInfo` summaries for stable render-graph texture/FBO counts, upload/static-buffer/fence counts, and shader-program counts. The generated Markdown and JSON reports include a `Value Stability Checks` section so the compared values can be reviewed without re-parsing the raw log.

The `renderer-shutdown-leak-audit-vid-restart` case enables `r_rendererShutdownAudit`, runs three full `vid_restart` cycles under forced `r_glTier gl45` with modern side-path cvars enabled, then exits. It requires every `vid_restart` post-teardown audit and the final shutdown post-teardown audit to report `live=0 status=pass` across render-graph physical allocations/textures/FBOs, upload frame/mapped/fence/static/pool objects, modern executor VAO/buffer/FBO/sampler/helper-program/shader-library/sync objects, and clustered-lighting GL objects.

The `dedicated-disabled-bse-manager-startup` case launches the staged dedicated server executable instead of the client. It requires the dedicated startup banner, the disabled BSE runtime manager marker, and MP game-module loading, while rejecting generic `ERROR:` markers, `idVertexCache Free: NULL pointer`, BSE-unavailable fallback, and accidental full client BSE attachment.

The `renderer-vertex-cache-budget-selftest` case runs the assetless `rendererVertexCacheBudgetSelfTest` command, which temporarily enables `r_vertexBufferBudget`, creates old and recently touched static vertex-cache blocks, and requires the LRU budget pass to purge an old block while leaving the touched block resident. `gfxInfo` also prints `Renderer vertex cache:` with budget status, fixed/purgable/protected bytes, the last purge, and any remaining over-budget amount.

The lens-flare shader-tier cases force `r_glTier gl33`, `gl41`, `gl43`, `gl45`, and `gl46`, run `rendererShaderLibrarySelfTest`, and require `gfxInfo` to report `Modern GL shader library: available` plus the `lensFlare(programs=...)` coverage tail. The runner marks these cases as assetless startup probes, because they only need renderer initialization and should not load game scripts just to validate internal shader variants. The runtime self-test verifies both lens-flare accumulation and composite programs for every compiled GLSL tier, including `uMainTexture`, `uSceneDepth`, and `uLensFlareAccum` sampler reflection, so a backend can no longer pass by compiling only the highest available variant.

`tools\tests\renderer_shader_inventory.py` is the static pre-runtime companion for shader-source maintenance. It does not compile GL programs; it audits the internal C++ shader-source inventory, GLSL tier list, program capacity, draw-record bounds guard, forced shader-tier validation cases, and opt-in reload gate before runtime validation is collected.

```powershell
python tools\tests\renderer_shader_inventory.py --output-dir .tmp\renderer-validation\<shader-inventory-run> --require-clean --print-summary
```

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
| conservative defaults | `r_renderer best` or explicit `r_renderer arb2` keeps ARB2 visible; `r_rendererModernAutoPromote`, modern executor/submit/visible/pass/debug paths, GPU validation, bindless, and shader reload all remain off in a clean startup |
| validation evidence | `r_rendererPromotionEvidence` carries the complete Phase 8 token after zero-warning visual, gameplay, RenderDoc, performance, presentation, rollback, and debug-off checks pass |
| manual sign-off | `r_rendererModernAutoPromote 1` is used only together with a complete `r_rendererPromotionEvidence` token |

Required promotion token:

```cfg
r_rendererPromotionEvidence "phase8=complete;warnings=0;visual=pass;gameplay=pass;renderdoc=pass;perf=arb2-or-better;presentation=pass;rollback=pass;debug=off"
```

Use the promotion evidence aggregator to turn the separate safe, gameplay, visual, capture, performance, presentation, rollback, default-safety, and platform artifacts into one reviewed bundle before setting that token:

```powershell
python tools\tests\renderer_platform_summary.py --output-dir .tmp\renderer-validation\<platform-summary-run> --write-template
python tools\tests\renderer_platform_summary.py --output-dir .tmp\renderer-validation\<platform-summary-run> --manifest .tmp\renderer-validation\<platform-summary-run>\platform_evidence_manifest_template.json
python tools\tests\renderer_candidate_summary.py --output-dir .tmp\renderer-validation\<candidate-run> --write-template
python tools\tests\renderer_candidate_summary.py --output-dir .tmp\renderer-validation\<candidate-run> --manifest .tmp\renderer-validation\<candidate-run>\candidate_manifest_template.json
python tools\tests\renderer_promotion_evidence.py --output-dir .tmp\renderer-validation\<promotion-run> --safe-report .tmp\renderer-validation\<safe-run>\renderer_validation_report.json --required-gameplay-report .tmp\renderer-gameplay\<required-run>\renderer_gameplay_benchmark_report.json --visual-report .tmp\renderer-gameplay\<visual-run>\renderer_gameplay_benchmark_report.json --capture-summary .tmp\renderer-captures\<capture-run>\capture_summary.md --performance-report .tmp\renderer-gameplay\<performance-run>\renderer_gameplay_benchmark_report.json --candidate-summary .tmp\renderer-validation\<candidate-run>\candidate_summary.json --presentation-report .tmp\renderer-gameplay\<presentation-run>\renderer_gameplay_benchmark_report.json --rollback-report .tmp\renderer-gameplay\<rollback-run>\renderer_gameplay_benchmark_report.json --platform-summary .tmp\renderer-validation\<platform-summary-run>\platform_summary.json
```

The platform summary remains `blocked` until Windows x64, Linux x64, and macOS GL 4.1 each have safe validation, required SP/MP gameplay, reference-backed visual comparison, presentation pacing, complete hardware/driver/GL metadata, and review approval. The candidate summary remains `blocked` or `fail` until a selected modern path satisfies the ARB2-or-better, fallback, warning, visual, capture, rollback, default-safety, and platform gates. The promotion summary remains `blocked` or `fail` until every gate is `pass` and a final `promote` or `no-promotion` decision is recorded. `--require-complete` makes these tools exit nonzero when any gate is still pending.

## Deterministic Capture Matrix

These image captures are the comparison set for scenes where deterministic output is practical. Capture paths should live under `.tmp/renderer-captures/<date>/`, and any checked-in references must be approved separately so the repo does not accumulate accidental binary churn.

| Case | Mode | Scene | Purpose |
|---|---|---|---|
| `capture-startup-mainmenu` | SP | main menu after logo skip | deterministic GUI composition, font/material atlas, and widescreen expansion |
| `capture-renderer-visible-selftest` | safe startup | `rendererModernVisibleSelfTest` | synthetic modern-visible depth/G-buffer/deferred/forward+/hybrid-scene/present composition with shadow-policy handoff |
| `capture-renderer-compatibility-selftest` | safe startup | `rendererModernCompatibilitySelfTest` | known fallback inventory for GUI/post/subview/render-demo/BSE categories |
| `capture-sp-airdefense1-static` | SP | `game/airdefense1` fixed spawn, no input for 3 seconds | outdoor lighting, terrain decals, BSE smoke, and stock material parity |
| `capture-lensflare-storage1-off` | SP | `game/storage1` fixed spawn with `r_lensFlare 0` | baseline image for lens-flare screenshot comparisons without flare contribution |
| `capture-lensflare-storage1-corona` | SP | `game/storage1` fixed spawn with `r_lensFlare 1` | corona-tier lens-flare screenshot comparison against the no-flare baseline and references |
| `capture-lensflare-storage1-high` | SP | `game/storage1` fixed spawn with `r_lensFlare 2` | high-tier corona/ghost/streak lens-flare screenshot comparison against references |

## RenderDoc Tier Checklist

Capture with `r_rendererMetrics 2`, `r_rendererGpuTimers 1` when available, and the matching forced tier. Every capture should show named debug scopes and object labels for graph resources, modern executor buffers/programs, and pass-owned FBOs.

| Forced tier | Capture focus |
|---|---|
| `r_glTier gl33` | VAO/VBO/UBO baseline, graph resources, visible-depth/G-buffer/forward+ passes |
| `r_glTier gl41` | macOS-class GLSL path and GL 4.1 context fallback behavior |
| `r_glTier gl43` | SSBO scene records, compute validation dispatch, indirect-command generation |
| `r_glTier gl45` | DSA texture/FBO updates, persistent upload defaults, and multi-bind groups |
| `r_glTier gl46` | top-tier selection plus GL SPIR-V/bindless availability reporting without default use |

`tools\tests\renderer_capture_summary.py` defines the long-term RenderDoc/API capture matrix and writes the run-level summary consumed by promotion review. The standard evidence layout is `.tmp\renderer-captures\<run>\<case-id>\`, with `<case-id>.rdc` for RenderDoc, `<case-id>.api-trace.json` for API/driver trace evidence, and `review.json` for per-case review metadata.

```powershell
python tools\tests\renderer_capture_summary.py --print-matrix
python tools\tests\renderer_capture_summary.py --output-dir .tmp\renderer-captures\<capture-run> --write-template
python tools\tests\renderer_capture_summary.py --output-dir .tmp\renderer-captures\<capture-run> --manifest .tmp\renderer-captures\<capture-run>\capture_manifest_template.json --require-complete
```

The default ARB2 and explicit ARB2 rollback rows are API/driver-trace rows because RenderDoc is still unreliable for the compatibility renderer on known test systems. Forced core-profile modern rows use RenderDoc where supported, and the graph-invalidation and low-overhead rows also require API trace evidence before invalidation or bind-count claims can be made.

## Shader Library Tier Checklist

These safe cases run `rendererShaderLibrarySelfTest` under forced GL tiers and require `gfxInfo` to expose the lens-flare program/reflection coverage tail. They use assetless startup automatically inside `renderer_validation_matrix.py`.

| Case | Forced tier | Coverage |
|---|---|---|
| `shader-lensflare-gl33` | `r_glTier gl33` | GLSL 330 lens-flare accumulation/composite compile, link, exact-version lookup, and sampler reflection |
| `shader-lensflare-gl41` | `r_glTier gl41` | GLSL 330/410 lens-flare coverage for the macOS-class GL 4.1 portability floor |
| `shader-lensflare-gl43` | `r_glTier gl43` | GLSL 330/410/430 lens-flare coverage alongside GPU-driven SSBO-capable tiers |
| `shader-lensflare-gl45` | `r_glTier gl45` | GLSL 330/410/430/450 lens-flare coverage alongside low-overhead DSA-capable tiers |
| `shader-lensflare-gl46` | `r_glTier gl46` | top-tier lens-flare shader coverage with the highest selected GLSL variant and all reflected sampler bindings |

Use the source-inventory audit when changing the internal shader library or considering any generated/externalized shader-source flow:

```powershell
python tools\tests\renderer_shader_inventory.py --output-dir .tmp\renderer-validation\<shader-inventory-run> --require-clean --print-summary
```

## Lens Flare Cross-Platform Sign-Off

`tools\tests\renderer_gameplay_benchmark.py --profile lensflare-signoff` is the release-facing lens-flare profile. It runs `game/storage1` and `game/airdefense1` with `r_lensFlare` off, corona, and high-quality presets under `r_glTier auto`, `gl41`, and `gl45`. Use `--limit` only for local smoke checks; release evidence should run the full profile on each target platform that is being claimed.

Run visual evidence with platform-specific approved references:

```powershell
python tools\tests\renderer_gameplay_benchmark.py --profile lensflare-signoff --reference-dir .tmp\renderer-references\lensflare-signoff\windows-x64 --require-references
```

Run pacing evidence with wall-clock sampling and target-machine thresholds:

```powershell
python tools\tests\renderer_gameplay_benchmark.py --profile lensflare-signoff --sample-msec 3000 --pacing-only --maxfps 0 --swap-intervals 0 --autoexec-delay-ms 2000 --min-pacing-hz 120 --max-p95-ms 12 --max-p99-ms 20
```

Platform evidence requirements:

| Platform | Shader coverage | Visual evidence | Performance evidence |
|---|---|---|---|
| Windows x64 | `shader-lensflare-gl33`, `gl41`, `gl43`, `gl45`, and `gl46`, plus `lensflare-signoff` on `auto`, `gl41`, and `gl45` | Windows-specific approved references, or an explicitly reviewed replacement reference bundle | uncapped `lensflare-signoff` pacing-only run with target-machine P95/P99 thresholds |
| Linux x64/arm64 | assetless shader-tier coverage on supported GL tiers plus SDL3 gameplay capture on `auto` and the highest supported forced tier | Linux-specific references because Mesa/proprietary-driver output and desktop color paths can differ | desktop Linux and Steam Deck profile pacing evidence when those targets are in scope |
| macOS | `shader-lensflare-gl41` plus `auto` fallback; GL 4.3+ is not expected on Apple OpenGL | macOS-specific `lensflare-signoff --tiers gl41,auto` references | off/corona/high pacing evidence on the GL 4.1 path, with reduced-quality fallback notes when needed |

For renderer-promotion or portability claims, summarize those per-platform reports with `tools\tests\renderer_platform_summary.py`. The manifest records the executable, OS, GPU/driver, GL vendor/renderer/version, selected/requested tier, context profile, debug context, timer-query, upload-manager, persistent-map, map-range/subdata fallback, DSA, multibind, and GPU-driven fields; Linux and macOS rows must use evidence from those platforms, not Windows substitutions.

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
| `mp-q4dm1-listen` | MP | `mp/q4dm1` | listen-server and local-client MP parity |

For each gameplay case, validate the matrix variants that the hardware supports:

| Dimension | Values |
|---|---|
| `r_glTier` | `auto`, `legacy`, `gl33`, `gl41`, `gl43`, `gl45`, `gl46` |
| renderer escape | `r_renderer best`, `r_renderer arb2`, `r_glTier legacy` |
| `r_swapInterval` | `0`, `1` |
| `com_maxfps` | `120`, `240`, `0` |
| display mode | windowed, fullscreen |
| renderer diagnostics | `r_rendererMetrics 1`, `r_rendererMetrics 2`, `r_rendererModernAutoPromote 0`, and one signed `r_rendererModernAutoPromote 1` candidate run with the complete `r_rendererPromotionEvidence` token after the other rows are clean |

After each gameplay smoke, inspect the configured log file under `fs_savepath\<gameDir>\logs\openq4.log` or the case-specific log emitted by the launch tool. Fix errors and warnings, then repeat the loop until the case is clean.

## Gameplay Benchmark Harness

`tools\tests\renderer_gameplay_benchmark.py` is the Phase 12 map-loading runner. It launches the staged client from `.install`, uses isolated save paths under `.tmp\renderer-gameplay\`, enters SP maps or an MP listen server plus loopback client, waits for streaming, runs a fixed static spawn camera path unless a case is later extended with authored poses, captures screenshots, emits `rendererBenchmarkCapture`, `framePacingSnapshot`, and `gfxInfo`, and writes Markdown/JSON reports.

The runner uses the SP/MP `g_autoExecAfterMapLoad` hook to execute its generated cfg after the map is active, not during loading UI. Renderer metrics are enabled only inside the gameplay capture window, which keeps load-screen logs quiet while still producing benchmark samples, GPU timing where available, frame-pacing output, and a screenshot artifact.

Use `--pacing-only` for high-FPS acceptance after a diagnostic metrics pass is already clean. This keeps `r_rendererMetrics`, GL timer queries, and the FPS overlay out of the timed window, still emits `framePacingSnapshot`, and can fail the run with parsed thresholds such as `--min-pacing-hz 120 --max-p95-ms 12`. The `game/storage1` acceptance run should start sampling two seconds after the active map draw with `r_swapInterval 0` and `com_maxfps 0` so the result measures renderer throughput rather than the old low-FPS plan cap.

Common runs:

```powershell
python tools\tests\renderer_gameplay_benchmark.py --list
python tools\tests\renderer_gameplay_benchmark.py --profile smoke
python tools\tests\renderer_gameplay_benchmark.py --profile smoke --pacing-only --autoexec-delay-ms 2000 --min-pacing-hz 120 --max-p95-ms 12
python tools\tests\renderer_gameplay_benchmark.py --profile required
python tools\tests\renderer_gameplay_benchmark.py --profile tiers
python tools\tests\renderer_gameplay_benchmark.py --profile presentation
python tools\tests\renderer_gameplay_benchmark.py --profile shadows
python tools\tests\renderer_gameplay_benchmark.py --profile lensflare
python tools\tests\renderer_gameplay_benchmark.py --profile lensflare-signoff
python tools\tests\renderer_gameplay_benchmark.py --profile map-transition-resource-count
python tools\tests\renderer_gameplay_benchmark.py --profile mode-transition-resource-count
python tools\tests\renderer_gameplay_benchmark.py --profile static-residency-budget
python tools\tests\renderer_gameplay_benchmark.py --profile low-overhead-submit
python tools\tests\renderer_gameplay_benchmark.py --profile graph-invalidation-modern
```

The runner fails a case when the process times out, no gameplay screenshot is produced, the benchmark/gfxInfo lines are missing, image comparison fails when references are required, or renderer warning markers such as `idStr::snPrintf: overflow`, `WARNING: idStr`, shader compile failures, program link failures, or fatal OpenGL startup failures appear in the log.

| Profile | Coverage |
|---|---|
| `smoke` | bounded `game/storage1` SP gameplay smoke with screenshot, metrics, frame-pacing snapshot, and zero-warning log gates |
| `required` | `game/storage1`, `game/airdefense1`, `game/airdefense2`, `game/storage2`, `game/medlabs`, `game/mcc_landing`, and `mp/q4dm1` listen server plus local client |
| `tiers` | forced `r_glTier auto`, `legacy`, `gl33`, `gl41`, `gl43`, `gl45`, and `gl46` gameplay probes |
| `presentation` | `r_swapInterval 0/1`, `com_maxfps 0/120/240`, windowed, and fullscreen coverage for uncapped/high-refresh validation |
| `shadows` | stencil fallback, mapped shadows, CSM, translucent moments, and debug-overlay modes `1..6` over the shadow correctness scenes |
| `lensflare` | stable SP lens-flare screenshot captures over `game/storage1`, `game/storage2`, and `game/airdefense1` with `r_lensFlare` off, corona, and high-quality presets |
| `lensflare-signoff` | release sign-off coverage for storage/outdoor flare scenes with `r_lensFlare` off/corona/high under `r_glTier auto`, `gl41`, and `gl45`; use full runs for platform evidence and `--limit` only for local smoke |
| `upload-pressure` | short upload-pressure matrix for storage, air-defense, and BSE-heavy scenes across default, persistent, reduced-ring, minimum-frame-buffer, and map-range/subdata fallback variants |
| `upload-pressure-long` | 30-second upload-pressure samples for intermittent fence, ring high-water, overflow, and frame-stall evidence, with explicit upload counter coverage in the generated report |
| `performance-comparison` | default, explicit ARB2, and executor-prep variants over storage1, airdefense1, storage2, and medlabs with uncapped wall-clock sampling for ARB2-or-better review |
| `map-transition-resource-count` | single-process SP map-transition loop over `game/storage1 -> game/storage2 -> game/storage1 -> game/storage2 -> game/storage1`; compares post-warm repeated-map graph/FBO, upload frame-buffer, fence, and shader counts while tracking static upload residency separately |
| `mode-transition-resource-count` | single-process SP-to-MP-to-SP game-module transition loop over `SP/game_sp/game/storage1 -> MP/game_mp/mp/q4dm1 -> SP/game_sp/game/storage1`; compares renderer object counts across module reloads |
| `static-residency-budget` | single-process SP map-transition loop with `r_vertexBufferBudget 1`, `r_vertexBufferMegs 8`, and `r_vertexBufferBudgetFrames 2`; requires the vertex-cache budget status to remain passing while graph/FBO, upload frame-buffer, fence, and shader counts stay stable |
| `low-overhead-submit` | GL 4.5 diagnostic modern-submit gameplay profile for storage and medlabs with modern executor and GPU timers enabled; requires submitted masked draws, graph DSA activity, texture/sampler multibind batches, restore handoff counters, and zero submit fallback or missing-buffer counters |
| `graph-invalidation-modern` | default-vs-armed render-graph invalidation gameplay profile with modern side-path cvars applied after map load, GPU timers enabled, and explicit submitted-versus-unsubmitted-pass accounting for armed candidates |

Optional deterministic image comparison uses TGA references:

```powershell
python tools\tests\renderer_gameplay_benchmark.py --profile smoke --reference-dir .tmp\renderer-references --require-references
python tools\tests\renderer_gameplay_benchmark.py --profile lensflare --reference-dir .tmp\renderer-references\lensflare --require-references
python tools\tests\renderer_gameplay_benchmark.py --profile lensflare-signoff --reference-dir .tmp\renderer-references\lensflare-signoff\windows-x64 --require-references
```

Promotion-quality visual references use a separate approval summary so binary screenshots can stay out of the repo while the review decision remains auditable:

```powershell
python tools\tests\renderer_visual_reference_summary.py --output-dir .tmp\renderer-references\<visual-reference-run> --write-template
python tools\tests\renderer_gameplay_benchmark.py --profile visual-comparison --reference-dir .tmp\renderer-references\mid-term-visual\windows-x64 --require-references --output-dir .tmp\renderer-gameplay\<visual-run>
python tools\tests\renderer_visual_reference_summary.py --output-dir .tmp\renderer-references\<visual-reference-run> --manifest .tmp\renderer-references\<visual-reference-run>\visual_reference_manifest_template.json --gameplay-report .tmp\renderer-gameplay\<visual-run>\renderer_gameplay_benchmark_report.json --require-complete
```

The manifest records platform/GL metadata, approval status, per-role reference paths, and the human-review checklist for black frames, post-process output, GUI/subview composition, lens-flare drift, shadow/stencil fallback drift, BSE visibility, and MP HUD/weapon-view composition. A blocked visual-reference summary is expected until the reference directory, filled manifest, and `visual-comparison --require-references` gameplay report all exist.

Promotion-quality performance evidence uses a separate summary so raw benchmark runs and the final ARB2-or-better review decision stay distinct:

```powershell
python tools\tests\renderer_performance_summary.py --output-dir .tmp\renderer-validation\<performance-summary-run> --write-template --gameplay-report .tmp\renderer-gameplay\<performance-run>\renderer_gameplay_benchmark_report.json
python tools\tests\renderer_performance_summary.py --output-dir .tmp\renderer-validation\<performance-summary-run> --review-manifest .tmp\renderer-validation\<performance-summary-run>\performance_review_template.json --require-complete
```

The default summary compares the `executor` launch variant against explicit `arb2`, with the current `default` run kept as secondary context. It checks P50/P95/P99/max frame times, pacing Hz, warning counts, required launch variants, and wall-clock sampling. Any required scene that misses the ARB2-or-better threshold keeps `perf=arb2-or-better` unsatisfied.

Nondeterministic BSE, cinematic, and MP scenes need human review in addition to the automated log/screenshot gates:

| Case | Focus | Checks |
|---|---|---|
| `sp-bse-heavy` | BSE-heavy effects in `game/medlabs` | effect sprites/trails animate at the expected cadence, no black quads, no missing additive passes, no warning spam |
| `sp-cinematic-subview` | cinematic/subview flow in `game/mcc_landing` | remote-camera/subview content is visible, GUI overlays composite in the right order, cinematic handoff keeps frame pacing stable |
| `mp-q4dm1-listen` | local MP listen server plus loopback client | client reaches the map, player/world lighting matches host expectations, frame pacing remains uncapped when requested |

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
- Lens-flare cross-platform claims require `lensflare-signoff` visual and pacing evidence on every claimed platform, with macOS treated as GL 4.1/fallback coverage unless a separate backend is explicitly qualified.
- `rendererDefaultSafetySelfTest` and `rendererDefaultPromotionSelfTest` pass before any default-promotion discussion.
- `renderer_promotion_evidence.py --require-complete` passes against the reviewed promotion bundle before `r_rendererPromotionEvidence` is set.
- `r_rendererModernAutoPromote 1` is used only with the complete `r_rendererPromotionEvidence` token after the default-promotion criteria pass; `r_renderer arb2`, `r_glTier legacy`, and the modern-disable cvar set remain documented rollback paths.
