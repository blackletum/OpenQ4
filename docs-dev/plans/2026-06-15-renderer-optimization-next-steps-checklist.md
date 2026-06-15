# Renderer Optimization Next-Steps Checklist

Date: 2026-06-15

This document tracks the follow-up implementation and validation work after the immediate renderer optimization round from `docs-dev/plans/2026-06-15-renderer-optimization.md`.

## Immediate Plan Completion Audit

The `Immediate` phase from `2026-06-15-renderer-optimization.md` is complete as of this round.

| Original immediate item | Status | Evidence |
|---|---|---|
| Add debug callback/filtering | Complete | `GLDebugScope` registers core/ARB debug-output callbacks, filters notifications, and exposes `r_glDebugOutput` / `r_glDebugSynchronous`; the opt-in validation case checks for callback activation. |
| Harden SSBO draw-record indices | Complete | Generated material shaders use `uDrawRecordCount` and fall back safely when a draw-record index is out of range. |
| Harden GPU-driven bucket/indirect indices | Complete | GPU-driven compute now validates `u_bucketCount`, clamps invalid bucket use, and counts indirect output overflow instead of writing past the generated command budget. |
| Add invalid-index and fence-timeout counters | Complete | Renderer logs/metrics include `invalidBucket`, `overflow`, upload fence `timeouts`, and upload fence `fallbacks`; self-tests exercise the invalid GPU-driven path. |
| Preserve validation visibility | Complete | `renderer-debug-output-debug-context`, `renderer-gpu-driven-selftest`, `renderer-low-overhead-selftest`, `renderer-gbuffer-selftest`, and `renderer-visible-depth-selftest` are covered by focused and broad matrix reports. |

## Implementation Checklist

- [x] Preserve the completed immediate-term fixes as the current baseline:
  - [x] Debug-context OpenGL debug-output callback registration exists.
  - [x] Driver callback messages are queued and flushed outside the callback stack.
  - [x] Generated draw-record SSBO fetches are guarded by `uDrawRecordCount`.
  - [x] GPU-driven bucket IDs and indirect output capacity fail closed with counters.
  - [x] Mixed 2D/cube-map multibind fallback carries per-unit target metadata.
  - [x] Hot buffer create/update helpers avoid redundant zero-unbind churn.
  - [x] Upload fence retirement can scan alternate frame rings before bounded blocking waits.
- [x] Add a formal validation-matrix case for the explicit debug-context debug-output callback path.
- [x] Keep the explicit debug-output callback case opt-in if it requires platform support that the default cross-platform matrix cannot assume.
- [x] Ensure `--list` exposes opt-in validation cases clearly.
- [x] Re-run `git diff --check` after the harness edit.
- [x] Symbolicate the `renderer-gbuffer-selftest` crash dump from the broader safe matrix.
- [x] Fix the G-buffer self-test lifetime issue by keeping synthetic draw surfaces, geometry, caches, and view data alive for the whole self-test.
- [x] Fix the visible-depth self-test lifetime issue by keeping synthetic draw surfaces, geometry, caches, and view data alive across draw-plan, submit-plan, resource, and executor checks.
- [x] Audit the remaining modern renderer self-test frame builders that keep legacy draw-surface pointers:
  - [x] `RendererDeferredResolve_RunSelfTest` keeps all synthetic frame data in the same function scope as packet, graph, draw-plan, and executor checks.
  - [x] `RendererForwardPlus_RunSelfTest` keeps all synthetic frame data in the same function scope as packet, graph, draw-plan, and executor checks.
  - [x] `RendererModernVisible_RunSelfTest` keeps all synthetic frame data in the same function scope as the global plan validation/reset guard.
  - [x] `RendererLowOverhead_RunSelfTest` keeps all synthetic frame data in the same function scope as packet, graph, draw-plan, and executor checks.
- [x] Harden GL debug object labels so generated buffers are labelled only after materialization and generated framebuffers are materialized/restored before labelling.
- [x] Rebuild and restage after the source fixes.
- [x] Update release-completion notes for the renderer diagnostic and validation reliability changes.

## Automated Validation Checklist

- [x] Baseline focused renderer matrix passed before this follow-up:
  - [x] `renderer-foundation-selftests`
  - [x] `renderer-gpu-driven-selftest`
  - [x] `renderer-low-overhead-selftest`
- [x] Baseline explicit debug-context probe passed before this follow-up:
  - [x] `requestedDebug=1`
  - [x] `actualDebug=1`
  - [x] `OpenGL debug output callback enabled`
  - [x] Clean shutdown with no leftover `openQ4-client_x64.exe` process.
- [x] New formal debug-output validation case passes from `.install`.
- [x] Focused renderer matrix still passes after the harness edit.
- [x] Failed broader default matrix was investigated and fixed:
  - [x] `renderer-gbuffer-selftest` access violation reproduced and symbolicated to `RB_DrawSurfHasSoftParticleStage` using stale self-test `drawSurf_t` pointers.
  - [x] `tier-gl33-debug-context` GL error signatures traced to labelling generated-but-unmaterialized buffer/FBO names.
  - [x] Focused rerun passed `renderer-gbuffer-selftest`, `renderer-debug-output-debug-context`, and `tier-gl33-debug-context`.
- [x] Broader safe renderer coverage passes:
  - [x] `renderer-visible-depth-selftest`
  - [x] `renderer-gbuffer-selftest`
  - [x] `renderer-cluster-grid-selftest`
  - [x] `renderer-shadow-planner-selftest`
  - [x] `renderer-shadow-projected-diagnostic`
  - [x] `renderer-deferred-resolve-selftest`
  - [x] `renderer-forward-plus-selftest`
  - [x] `renderer-modern-visible-selftest`
  - [x] `renderer-modern-compatibility-selftest`
  - [x] `renderer-compatibility-gates-selftest`
  - [x] `renderer-default-promotion-selftest`
  - [x] `renderer-default-safety-selftest`
  - [x] `renderer-benchmark-selftest`
  - [x] `renderer-gpu-driven-selftest`
  - [x] `renderer-low-overhead-selftest`
- [x] Shader-tier safe cases pass:
  - [x] `shader-lensflare-gl33`
  - [x] `shader-lensflare-gl41`
  - [x] `shader-lensflare-gl43`
  - [x] `shader-lensflare-gl45`
  - [x] `shader-lensflare-gl46`
- [x] Startup/tier probes pass:
  - [x] `tier-auto`
  - [x] `tier-legacy`
  - [x] `tier-gl33`
  - [x] `tier-gl41`
  - [x] `tier-gl43`
  - [x] `tier-gl45`
  - [x] `tier-gl46`
  - [x] `tier-gl33-debug-context`
  - [x] `present-vsync0-fps0`
  - [x] `present-vsync1-fps240`
  - [x] `present-vsync1-fps120`
- [x] Focused lifetime-hardening rerun passes after the visible-depth self-test refactor:
  - [x] `renderer-visible-depth-selftest`
  - [x] `renderer-gbuffer-selftest`
  - [x] `renderer-debug-output-debug-context`
  - [x] `tier-gl33-debug-context`
- [x] Broader safe renderer matrix still passes after the visible-depth self-test refactor:
  - [x] `32 passed, 0 failed`
- [x] Focused immediate-plan audit rerun passes after documenting completion:
  - [x] `renderer-visible-depth-selftest`
  - [x] `renderer-gbuffer-selftest`
  - [x] `renderer-gpu-driven-selftest`
  - [x] `renderer-low-overhead-selftest`
  - [x] `renderer-debug-output-debug-context`
  - [x] `tier-gl33-debug-context`
  - [x] `6 passed, 0 failed`

## Gameplay And Manual Evidence Checklist

- [x] Decide this round should include one bounded gameplay smoke after safe startup/self-tests went green.
- [x] Run `tools/tests/renderer_gameplay_benchmark.py --profile smoke` after safe startup coverage is green.
- [x] Gameplay smoke passed:
  - [x] `sp-storage1_auto_fps240_vsync0_windowed_default_best`
  - [x] Map: `game/storage1`
  - [x] Tier: `auto`
  - [x] Pacing: `124.8 Hz / p95 11 ms`
  - [x] Screenshot captured under `.tmp/renderer-gameplay/renderer-optimization-next-smoke/`
  - [x] Screenshot is not black: 1280x720 RGB mean approximately `82/68/61`, channel extrema `0..255`.
- [x] Decide to run the required SP/MP gameplay coverage in this round now that smoke and default safe renderer validation passed.
- [x] Run the required SP/MP gameplay coverage and record the split result.
- [x] Required single-player gameplay coverage passes:
  - [x] `sp-airdefense1`: 135.4 Hz / p95 12 ms
  - [x] `sp-airdefense2`: 200.1 Hz / p95 8 ms
  - [x] `sp-storage2`: 214.4 Hz / p95 8 ms
  - [x] `sp-medlabs`: 223.1 Hz / p95 7 ms
  - [x] `sp-mcc-landing`: 239.9 Hz / p95 6 ms
- [ ] Required multiplayer benchmark capture passes:
  - [x] `mp-q4dm1-listen` server/client launch reaches `mp/q4dm1` and arms the map-load autoexec path.
  - [x] A clean staged direct MP listen-server sanity launch reaches `mp/q4dm1`, renders, and shuts down normally from scripted `quit`.
  - [ ] The MP benchmark autoexec executes after the listen-server/client reach active draw.
  - [ ] Server and client benchmark summaries, pacing data, screenshots, `gfxInfo`, and selected renderer tier are captured.
- [x] Reject the naive MP draw-hook experiment and restore GameLibs after it caused fast no-log harness exits.
- [x] Inspect safe-matrix logs for renderer warning signatures:
  - [x] Full default fixed report shows `32 passed, 0 failed`.
  - [x] `tier-gl33-debug-context` reports warning signatures `0`.
  - [x] GPU-driven/low-overhead/default matrix logs report warning signatures `0`.
- [x] Inspect gameplay smoke log for detailed renderer counters before required-profile sign-off:
  - [x] `invalidBucket=0` outside synthetic invalid-index tests.
  - [x] `overflow=0` outside synthetic overflow tests.
  - [x] Upload fence `fallbacks=0`, `timeouts=0`, and `waits=0`.
  - [x] Multibind counters are consistent with the smoke baseline: modern executor off, `textureMultiBind=0`, `samplerMultiBind=0`.

## Artifacts

- [x] Previous focused report: `.tmp/renderer-validation/renderer-optimization-next-final/renderer_validation_report.md`
- [x] New debug-output report: `.tmp/renderer-validation/renderer-debug-output-formal/renderer_validation_report.md`
- [x] New focused follow-up report: `.tmp/renderer-validation/renderer-optimization-next-followup-focused/renderer_validation_report.md`
- [x] Failed broader safe report: `.tmp/renderer-validation/renderer-optimization-next-followup-default/renderer_validation_report.md`
- [x] Crash report used for root-cause analysis: `.install/crashes/openq4_crash_20260615_183237_40272_59144.log`
- [x] Focused fix rerun report: `.tmp/renderer-validation/renderer-optimization-next-fix-rerun/renderer_validation_report.md`
- [x] New broader safe fixed report: `.tmp/renderer-validation/renderer-optimization-next-followup-default-fixed/renderer_validation_report.md`
- [x] Gameplay smoke report: `.tmp/renderer-gameplay/renderer-optimization-next-smoke/renderer_gameplay_benchmark_report.md`
- [x] Focused lifetime-hardening rerun report: `.tmp/renderer-validation/renderer-optimization-next-lifetime-rerun/renderer_validation_report.md`
- [x] Broader lifetime-hardening safe report: `.tmp/renderer-validation/renderer-optimization-next-lifetime-default/renderer_validation_report.md`
- [x] Focused immediate-plan audit report: `.tmp/renderer-validation/renderer-immediate-plan-audit/renderer_validation_report.md`
- [x] Required gameplay profile report: `.tmp/renderer-gameplay/renderer-optimization-next-required/renderer_gameplay_benchmark_report.md` (partial sign-off: six SP cases passed; `mp-q4dm1-listen` capture blocked)
- [x] Post-MP cleanup SP smoke report: `.tmp/renderer-gameplay/renderer-optimization-next-postmp-sp-smoke/renderer_gameplay_benchmark_report.md`
- [x] Clean staged MP sanity log: `.tmp/renderer-gameplay/manual-mp-noauto-clean/q4base/logs/openq4_manual_mp_noauto_clean.log`

## Open Questions

- [ ] Should the explicit debug-output callback probe become default on platforms where `actualDebug=1` is available, or remain an opt-in developer diagnostic?
- [ ] Should the validation harness gain explicit skip semantics for platform-conditional cases instead of opt-in-only cases?
- [ ] Should GL debug callback messages be promoted into renderer metrics counters in addition to log output?
- [x] Should the required gameplay profile be run in this round, or saved for the next renderer-validation round after reviewing the smoke screenshot/log counters? Answer: run it in this round.
- [x] Should other self-test frame builders that keep legacy draw-surface pointers be refactored into explicit lifetime structs proactively, even if their current pass mix does not dereference those pointers after helper return? Answer: refactor helper-return cases now; leave same-scope self-tests unchanged after auditing their lifetimes.
- [ ] What is the right MP benchmark trigger point for listen-server/client runs so `rendererBenchmarkCapture`, screenshots, `gfxInfo`, and tier logging happen after active draw instead of never firing or firing before the map is ready?
- [ ] Why did the naive MP game draw-hook autoexec experiment cause fast no-log exits in the benchmark harness while direct staged MP listen-server startup remained healthy?
- [ ] Should the gameplay benchmark harness surface fast no-log exits with captured launch metadata and an explicit failure reason?
