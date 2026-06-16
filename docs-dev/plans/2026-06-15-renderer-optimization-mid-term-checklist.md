# Renderer Optimization Mid-Term Checklist

Date: 2026-06-15

This document tracks the mid-term renderer optimization work after the completed immediate and near-term phases in:

- `docs-dev/plans/2026-06-15-renderer-optimization.md`
- `docs-dev/plans/2026-06-15-renderer-optimization-next-steps-checklist.md`
- `docs-dev/plans/2026-06-15-renderer-optimization-near-term-checklist.md`

## Baseline

- [x] Treat the immediate phase as complete.
- [x] Treat the next-steps audit checklist as complete.
- [x] Treat the near-term checklist as complete.
- [x] Preserve near-term renderer validation evidence:
  - [x] Focused mixed-target/upload-metric validation: `.tmp/renderer-validation/near-term-regression-round2/renderer_validation_report.md`
  - [x] Focused no-zero-unbind validation: `.tmp/renderer-validation/near-term-nozero-round3/renderer_validation_report.md`
  - [x] Debug-output restart proof: `.tmp/renderer-validation/near-term-debug-output-round4/renderer_validation_report.md`
  - [x] Broad safe renderer validation: `.tmp/renderer-validation/near-term-safe-round3/renderer_validation_report.md`
- [x] Preserve near-term gameplay evidence:
  - [x] MP-only benchmark capture: `.tmp/rgmp2/renderer_gameplay_benchmark_report.md`
  - [x] Full required SP/MP gameplay: `.tmp/renderer-gameplay/ntr-required-r2/renderer_gameplay_benchmark_report.md`
  - [x] Upload-stress A/B bundle: `.tmp/renderer-validation/near-term-upload-stress-round4/upload_stress_summary.md`
- [x] Carry forward the near-term policy decisions:
  - [x] Real-debug-context validation remains opt-in.
  - [x] GL debug callback messages remain developer-only diagnostics, not release-signoff severity counters.
  - [x] Upload stress remains opt-in A/B profiling evidence, not a default validation gate.

## Mid-Term Goal

- [x] Convert the hardened renderer paths into measured optimization decisions by profiling upload pressure, validating transient render-graph invalidation opportunities, comparing low-overhead state-change behavior, and assembling promotion-quality evidence without changing the default renderer path prematurely.
  - [x] First Windows x64 tranche has upload-pressure, low-overhead, performance, presentation, GPU-timer, visual, graph-invalidation, safe-matrix, and required gameplay artifacts linked below.
  - [x] Decision: no renderer/upload/invalidation default-policy change is justified by the sampled data; renderer promotion, API-level bind-count claims, approved-reference visual claims, and portability claims are deferred beyond this mid-term implementation tranche.

## Scope Boundaries

- [x] Keep the stock ARB2-compatible renderer path as the default unless the promotion evidence gate is intentionally completed.
- [x] Do not introduce engine-side replacement content to make validation pass.
- [x] Keep gameplay/map evidence based on installed Quake 4 assets plus openQ4 binaries and staged overrides.
- [x] Keep debug-only validation and GPU readbacks opt-in.
- [x] Keep `.install/` as staged runtime output and `.tmp/` as the evidence root.
- [x] Treat `E:\Repositories\openQ4-GameLibs` as part of the workspace only when benchmark readiness or gameplay validation changes require canonical game-library edits.
  - [x] First Windows x64 tranche made no GameLibs edits; companion repo has pre-existing local modifications documented under completion criteria.

## Upload Path Profiling

- [x] Define a repeatable upload-pressure matrix that covers:
  - [x] Default upload ring.
  - [x] Reduced ring size.
  - [x] Minimum safe frame-buffer count.
  - [x] Persistent mapping on capable GL 4.4+ drivers.
  - [x] Map-range/subdata fallback tiers where available.
- [x] Add a named upload-stress profile to `tools/tests/renderer_gameplay_benchmark.py` if launch-time cvar handling can express the matrix clearly.
- [x] Document the first-round command matrix in this checklist.
  - [x] Use `python tools\tests\renderer_gameplay_benchmark.py --profile upload-pressure --sample-msec 3000 --output-dir .tmp\renderer-gameplay\mid-term-upload-pressure-r1` for the full 15-case Windows x64 upload-pressure matrix.
  - [x] Use `--limit 1 --dry-run` with the same profile for command-generation checks.
  - [x] The profile expands launch-time variants `default`, `persistent`, `reduced-ring`, `min-frame-buffers`, and `map-range`; the last variant exercises map-range or subdata fallback behavior according to the target driver's available upload path.
- [x] Capture upload metrics for at least three scenes:
  - [x] `game/storage1`
  - [x] `game/airdefense1`
  - [x] `game/medlabs`
  - [x] First full matrix capture: `.tmp/renderer-gameplay/mid-term-upload-pressure-r1/renderer_gameplay_benchmark_report.md` captured 11/15 cases and exposed four Windows path-length harness misses before launch.
  - [x] Focused path-length repair capture: `.tmp/renderer-gameplay/mid-term-upload-pressure-r2-pathfix/renderer_gameplay_benchmark_report.md` reran the missed reduced-ring/min-frame-buffer stress cases and passed 6/6.
- [x] Record per-run:
  - [x] `rendererBenchmarkCapture` upload KB.
  - [x] `rendererMetrics summary` ring usage and overflow KB.
  - [x] frame stalls.
  - [x] fence waits.
  - [x] fence timeouts.
  - [x] fence fallbacks.
  - [x] frame pacing average, P95, and P99.
- [x] Add benchmark-report extraction for upload KB, ring high-water/capacity, overflow KB, frame stalls, fence waits/timeouts/fallbacks, upload path, and state-cache counters.
- [x] Compare reduced-ring results against the near-term baseline.
  - [x] Reduced-ring runs completed for storage1, airdefense1, and medlabs with zero overflow, stalls, fence waits, fence timeouts, or fence fallbacks in the captured Windows x64 evidence.
- [x] Decide whether current bounded-wait policy needs tuning.
  - [x] Decision: no tuning this round. Captured upload-pressure runs showed no waits, timeouts, fallbacks, or ring-overflow pressure that would justify changing the bounded-wait policy.
- [x] Decide whether default `r_rendererUploadMegs` or `r_rendererUploadFrameBuffers` should change.
  - [x] Decision: no default change this round. The reduced 4 MB ring fit the sampled high-water usage, but the minimum-frame-buffer storage1 run had a slower P95/P99 pacing sample, so the safer default remains unchanged until broader evidence says otherwise.
- [x] Add validation evidence for any tuned upload policy.
  - [x] No tuned upload policy was selected in this round, so no default-policy validation artifact is required yet.

## Transient Render-Graph Invalidation

- [x] Audit render-graph resource lifetime metadata for reliable last-use information.
  - [x] `RenderGraphResources` now reuses graph `firstPass`/`lastPass` data and explicitly scans later accesses before marking an invalidate tag as a safe candidate.
- [x] Identify transient attachments that are safe invalidation candidates:
  - [x] depth-only transient targets.
  - [x] MSAA color/depth targets after resolve.
    - [x] Decision: not selected as a first-tranche safe candidate. Current graph-invalidation evidence does not expose pass-owned MSAA resolve submission/counters, so defer until the executor can submit discard calls after a proven final resolve.
  - [x] post-process scratch targets after final read.
  - [x] G-buffer attachments after deferred resolve.
    - [x] Decision: not selected as a first-tranche safe candidate. Current staged evidence reports no active modern G-buffer/deferred ownership in the graph-invalidation probe, so defer until modern-visible/deferred pass ownership is promoted far enough to prove last use.
  - [x] lens-flare accumulation after composite.
- [x] Decide whether invalidation should be emitted from the render graph executor, resource manager, or pass ownership layer.
  - [x] Decision: classify eligibility in the resource manager, but emit future GL invalidation from the pass owner/executor at pass end so discard calls happen after the final use.
- [x] Add a cvar-gated implementation path if the evidence is not yet strong enough for default-on behavior.
  - [x] `r_rendererGraphInvalidate` is default-off and only arms eligible candidates for future pass-timed submission; no visible rendering path changes yet.
- [x] Define a repeatable transient-invalidation A/B evidence profile.
  - [x] Use `python tools\tests\renderer_gameplay_benchmark.py --profile graph-invalidation --sample-msec 3000 --output-dir .tmp\renderer-gameplay\mid-term-graph-invalidation-r1` for the first 6-case default versus `r_rendererGraphInvalidate 1` Windows x64 matrix across post/lens-flare/BSE-heavy scenes.
  - [x] Use `--limit 2 --dry-run` with the same profile for command-generation checks.
- [x] Track invalidation count by pass/resource in renderer metrics.
  - [x] `rendererMetrics summary` now includes `graphInvalidate=enabled/tagged/candidates/armed/submitted/skipped`; verbose frame metrics and `rendererRenderGraphResourceDump` include skip reasons and per-pass candidate/skipped/submitted counts.
- [x] Ensure invalidation never touches attachments that are sampled later in the same frame.
  - [x] Candidate filtering rejects any invalidate access whose resource has a later read/write/resolve/present use.
- [x] Add self-test coverage for invalidation eligibility and last-use safety.
  - [x] `rendererRenderGraphResourceSelfTest` validates safe world-graph candidates and rejects a synthetic invalidate-before-later-read graph.
- [x] Capture before/after GPU evidence on at least one bandwidth-sensitive post/HDR/lens-flare scene.
  - [x] `python tools\tests\renderer_gameplay_benchmark.py --profile graph-invalidation --sample-msec 3000 --gpu-timers --output-dir .tmp\renderer-gameplay\mid-term-graph-invalidation-r1` passed 6/6 across default versus `r_rendererGraphInvalidate 1` variants for lens-flare storage1, lens-flare airdefense1, and BSE-heavy medlabs.
  - [x] GPU-timer summary samples were captured for each pair: storage1 default/armed 1/3 ms, airdefense1 default/armed 6/4 ms, medlabs default/armed 1/1 ms. The data is diagnostic only and does not justify a default-policy change.
  - [x] A short `--modern-executor` probe passed 2/2 on lensflare-storage1 and exposed aggregate render-graph resource activity (`res=7/2/5`, `invalidate=27`, `graphGL=1`) for both default and armed variants, but the currently staged runtime does not expose separate `graphInvalidate=` candidate/armed/submitted counters in reports. Keep pass-owned invalidate submission evidence open for a later rebuilt/staged runtime path.
- [x] Keep the feature default-off until captures show no visual regressions.
  - [x] Round 1 evidence/deferral note: `.tmp/renderer-validation/mid-term-transient-invalidation-r1/transient_invalidation_note.md`
  - [x] First graph-invalidation GPU evidence run found no renderer failure signatures; feature remains default-off.

## Low-Overhead State And Bind Profiling

- [x] Define a repeatable low-overhead state/bind profiling profile.
  - [x] Use `python tools\tests\renderer_gameplay_benchmark.py --profile low-overhead-state --sample-msec 3000 --output-dir .tmp\renderer-gameplay\mid-term-low-overhead-state-r1` for the first 6-case Windows x64 GL 3.3 versus GL 4.5 state/bind matrix.
  - [x] The profile enables `r_rendererModernExecutor` and uses `r_rendererMetrics 2` during sampling so `rendererMetrics lowOverhead(...)` lines are emitted.
- [x] Establish baseline GL call-count evidence for representative scenes.
  - [x] First GL 3.3 versus GL 4.5 gameplay matrix passed 6/6: `.tmp/renderer-gameplay/mid-term-low-overhead-state-r1/renderer_gameplay_benchmark_report.md`.
  - [x] Captured storage1, airdefense1, and medlabs with `r_rendererModernExecutor 1` and `r_rendererMetrics 2`.
- [x] Compare low-overhead-capable and fallback paths:
  - [x] DSA buffer updates.
    - [x] No DSA buffer-update activity was observed in this prepare-only sample; keep this as a future submit-path comparison point.
  - [x] bind-based buffer updates.
    - [x] GL 3.3 runs stayed on the classic graph allocation path and map-range upload fallback; GL 4.5 runs used persistent upload defaults.
  - [x] texture multibind.
    - [x] No texture multibind batches were observed in the sampled prepare-only scenes.
  - [x] sampler multibind.
    - [x] No sampler multibind batches were observed in the sampled prepare-only scenes.
  - [x] state-cache hit/miss counts.
    - [x] All six runs reported stable state-cache counters of `0/1 invalid=3 legacyReset=1`.
  - [x] Graph resource allocation split: GL 3.3 reported `graphClassic=5/5`, while GL 4.5 reported `graphDSA=5/20/5` and `graphClassic=0/0` for each sampled scene.
- [x] Verify no-zero-unbind behavior remains stable under gameplay, not only self-tests.
  - [x] The six-case gameplay logs had no `GL_INVALID` or `zero-unbind` signature matches in the low-overhead-state capture; stock-content warning lines remain unrelated to this renderer-state check.
- [x] Add benchmark report extraction for state-cache counters if the current reports are too manual to compare.
- [x] Add benchmark report extraction for low-overhead DSA updates, buffer/texture/sampler multibind batches, classic texture binds, compacted batches, graph DSA/classic allocation counts, and upload fence counters.
- [x] Resolve API-capture or driver-tooling status for bind-count reductions.
  - [x] Deferred beyond this mid-term implementation tranche. The current evidence is renderer-metric based, not RenderDoc/API-trace based, so no bind-count reduction claim or default-policy change is made from this checklist.
- [x] Decide whether any additional redundant state filtering should move from diagnostics to default behavior.
  - [x] Decision: no default-behavior change this round. GL 4.5 graph DSA readiness is confirmed, but buffer-update and multibind counters did not exercise enough draw-submission work to justify moving more filtering out of diagnostics.

## Modern Executor Promotion Readiness

- [x] Re-run default-promotion and default-safety self-tests before any promotion-policy change.
  - [x] Validation matrix now keeps `renderer-default-promotion-selftest` and `renderer-default-safety-selftest` visible alongside focused promotion-readiness startup probes.
  - [x] Both self-tests passed in the broad mid-term safe matrix: `.tmp/renderer-validation/mid-term-safe-matrix-r1/renderer_validation_report.md`.
- [x] Confirm `r_rendererModernAutoPromote 1` remains blocked without a complete `r_rendererPromotionEvidence` token.
  - [x] Added and passed `promotion-missing-evidence-block` and `promotion-incomplete-evidence-block` assetless validation cases; report: `.tmp/renderer-validation/mid-term-promotion-readiness-r1/renderer_validation_report.md`.
- [x] Resolve candidate Phase 8 evidence-bundle status.
  - [x] No candidate promotion bundle is assembled in this mid-term implementation tranche; promotion remains blocked until the deferred external/manual gates below are completed and reviewed.
  - [x] zero-warning safe renderer matrix.
    - [x] Broad default safe matrix passed 36/36 startup/self-test/tier/presentation cases with no warning-signature failures: `.tmp/renderer-validation/mid-term-safe-matrix-r1/renderer_validation_report.md`.
  - [x] required SP/MP gameplay capture.
    - [x] Required Windows x64 SP/MP gameplay passed after mid-term edits: `.tmp/renderer-gameplay/mid-term-required-gameplay-r1/renderer_gameplay_benchmark_report.md`.
  - [x] Resolve deterministic visual-capture status for this tranche.
    - [x] First Windows x64 screenshot capture/inspection bundle exists: `.tmp/renderer-gameplay/mid-term-visual-comparison-r1/renderer_gameplay_benchmark_report.md`.
    - [x] Approved reference bundle + `--require-references` comparison is deferred beyond this mid-term implementation tranche; no approved-reference visual parity claim is made.
  - [x] Resolve RenderDoc tier-capture status.
    - [x] Deferred beyond this mid-term implementation tranche. RenderDoc/API captures remain required before promotion, pass-content, bind-count, or rollback-review claims.
  - [x] ARB2-or-better performance comparison.
    - [x] First Windows x64 comparison passed 12/12 across default, explicit ARB2, and executor-prep variants: `.tmp/renderer-gameplay/mid-term-performance-comparison-r1/renderer_gameplay_benchmark_report.md`.
  - [x] presentation/vsync/fps coverage.
    - [x] First Windows x64 capped/uncapped/VSync presentation matrix passed 30/30 cases across storage1, airdefense1, storage2, medlabs, and q4dm1 listen/client roles: `.tmp/renderer-gameplay/mid-term-presentation-comparison-r1/renderer_gameplay_benchmark_report.md`.
  - [x] rollback command coverage.
    - [x] Added and passed `promotion-explicit-arb2-rollback` as a validation-matrix startup probe for the explicit ARB2 escape path.
    - [x] Gameplay rollback proof entered `game/storage1`, requested modern-visible side paths long enough to emit `modernVisible req=1 ... blocked=1 ... reason=modern-visible-fail-closed`, then issued `r_renderer arb2`, `r_glTier legacy`, and modern-disable commands; post-rollback `gfxInfo` reported `rendererAllows=0`, `reason=explicit-renderer-escape`, `rollback=available`, and visible/deferred/forward+/GPU validation/bindless/shader-reload cvars off: `.tmp/renderer-gameplay/mid-term-rollback-r2/renderer_gameplay_benchmark_report.md`.
    - [x] Rollback proof pacing is diagnostic only because the command sequence intentionally waits and changes renderer cvars mid-run.
  - [x] debug-off coverage.
    - [x] Added and passed `promotion-debug-off-defaults` as a validation-matrix startup probe for default-off debug/validation/experimental side paths.
- [x] Keep modern-visible, deferred, forward+, GPU-driven, and bindless paths opt-in until the evidence bundle is reviewed.
  - [x] Validation and gameplay evidence kept these side paths opt-in; no promotion/default-policy change was made.
- [x] Add any missing promotion evidence to the validation matrix report, not just ad hoc notes.
  - [x] `renderer_validation_matrix.py` now emits a `Promotion Readiness Probes` report section and JSON `promotionReadinessMatrix` entries for missing evidence, incomplete evidence, explicit ARB2 rollback, and debug-off defaults.

## Gameplay And Visual Evidence

- [x] Re-run required gameplay after any renderer-path edit:
  - [x] `sp-storage1`
  - [x] `sp-airdefense1`
  - [x] `sp-airdefense2`
  - [x] `sp-storage2`
  - [x] `sp-medlabs`
  - [x] `sp-mcc-landing`
  - [x] `mp-q4dm1-listen`
  - [x] Report: `.tmp/renderer-gameplay/mid-term-required-gameplay-r1/renderer_gameplay_benchmark_report.md` passed 7/7 cases, including MP server/client roles.
  - [x] Log scan found no renderer failure signatures; the only broad `fatal` text hit was stock `damage_fatalfall` decl text, not a fatal error.
- [x] Add a mid-term visual comparison set for:
  - [x] post/HDR-heavy scene: `lensflare-airdefense1`
  - [x] BSE-heavy scene: `sp-medlabs`
  - [x] GUI/subview scene: `sp-mcc-landing`
  - [x] dense local-light scene: `sp-storage2`
  - [x] MP listen/client scene: `mp-q4dm1-listen`
  - [x] Use `python tools\tests\renderer_gameplay_benchmark.py --profile visual-comparison --sample-msec 3000 --output-dir .tmp\renderer-gameplay\mid-term-visual-comparison-r1` for the first five-case screenshot set.
  - [x] Use `--reference-dir .tmp\renderer-references\mid-term-visual\windows-x64` when approved references exist; add `--require-references` only after that reference bundle is reviewed.
- [x] Check screenshots for:
  - [x] black frames.
  - [x] missing post-process output.
  - [x] missing GUI/subview composition.
  - [x] incorrect lens-flare accumulation/composite.
  - [x] shadow-map/stencil fallback drift.
  - [x] Manual inspection of `.tmp/renderer-gameplay/mid-term-visual-comparison-r1/visual-thumbnails/contact_sheet.png` found all six captured roles nonblank with expected scene/HUD content; the dark medlabs capture is scene lighting/geometry, not a failed clear.
- [x] Preserve representative screenshots under `.tmp/renderer-gameplay/<mid-term-run>/`.
  - [x] First bundle preserved under `.tmp/renderer-gameplay/mid-term-visual-comparison-r1/`; contact sheet and PNG thumbnails are under `visual-thumbnails/`.
- [x] Record manual observations in this checklist or a linked report.
  - [x] Visual comparison run passed 5/5 cases; log scan found no `GL_INVALID`, zero-unbind, overflow, shader-link, fatal-error, or `ERROR:` renderer failure signatures.

## Performance Evidence

- [x] Define target-machine comparison rules before collecting numbers.
  - [x] First Windows x64 comparison uses uncapped `com_maxfps 0`, `r_swapInterval 0`, a 3000 ms sample window, matched windowed display settings, and identical scene/cvar setup across variants.
  - [x] First ARB2-or-better target scenes are `game/storage1`, `game/airdefense1`, `game/storage2`, and `game/medlabs`.
  - [x] Compare current default, explicit `r_renderer arb2`, and `r_rendererModernExecutor 1` prepare-only variants before considering any default-policy change.
- [x] Define a repeatable performance comparison profile.
  - [x] Use `python tools\tests\renderer_gameplay_benchmark.py --profile performance-comparison --sample-msec 3000 --output-dir .tmp\renderer-gameplay\mid-term-performance-comparison-r1` for the first 12-case Windows x64 ARB2-or-better comparison matrix.
  - [x] Use `--limit 3 --dry-run` with the same profile for command-generation checks.
- [x] Define a repeatable presentation pacing profile.
  - [x] Use `python tools\tests\renderer_gameplay_benchmark.py --profile presentation-comparison --sample-msec 3000 --pacing-only --output-dir .tmp\renderer-gameplay\mid-term-presentation-comparison-r1` for the first Windows x64 capped/uncapped/VSync matrix.
  - [x] The profile covers `game/storage1`, `game/airdefense1`, `game/storage2`, `game/medlabs`, and MP `mp/q4dm1` listen/client roles across `com_maxfps 0/120/240` and `r_swapInterval 0/1` in windowed mode.
- [x] Capture ARB2/default baseline pacing for target scenes.
  - [x] Default and explicit ARB2 baselines captured for storage1, airdefense1, storage2, and medlabs in `.tmp/renderer-gameplay/mid-term-performance-comparison-r1/renderer_gameplay_benchmark_report.md`.
- [x] Capture candidate renderer pacing with matching settings.
  - [x] Executor-prep variant captured with matching windowed, uncapped, VSync-off settings for all four target scenes.
- [x] Capture uncapped `r_swapInterval 0`, `com_maxfps 0` pacing where appropriate.
  - [x] The first comparison matrix used `com_maxfps 0` and `r_swapInterval 0` for all 12 runs.
- [x] Capture capped `com_maxfps 120` and `com_maxfps 240` presentation behavior.
  - [x] Presentation matrix passed 30/30 cases with `com_maxfps 0/120/240` crossed with `r_swapInterval 0/1`; log scan found no `GL_INVALID`, zero-unbind, overflow, shader-link, fatal-error, or `ERROR:` renderer failure signatures.
  - [x] Aggregate worst P99 by setting: fps0/vsync0 36 ms, fps0/vsync1 31 ms, fps120/vsync0 33 ms, fps120/vsync1 21 ms, fps240/vsync0 34 ms, fps240/vsync1 23 ms. Worst single max was 79 ms on the MP q4dm1 listen-server role at fps240/vsync0.
- [x] Record frame average, P50, P95, P99, max, and pass/fail threshold.
  - [x] Gameplay benchmark reports now include a `Presentation Pacing` table with avg/Hz/P50/P95/P99/max for completed presentation runs.
  - [x] First performance-comparison run recorded frame avg/Hz/P50/P95/P99/max for all 12 runs; no acceptance threshold was applied because this was a comparison capture, not a promotion gate.
  - [x] First presentation-comparison run recorded frame avg/Hz/P50/P95/P99/max for all 36 SP/MP roles represented by the 30-case matrix.
- [x] Record CPU phase metrics:
  - [x] front-end.
  - [x] visibility.
  - [x] packet.
  - [x] graph.
  - [x] submit.
  - [x] backend.
  - [x] present.
- [x] Record GPU timer metrics when `r_rendererGpuTimers 1` is safe for the run.
  - [x] Windows x64 platform notes show timer queries available on auto/GL 3.3/GL 4.5 tiers (`timer=available tq=1`), so GPU timers were enabled for a diagnostic performance run.
  - [x] `python tools\tests\renderer_gameplay_benchmark.py --profile performance-comparison --sample-msec 3000 --gpu-timers --output-dir .tmp\renderer-gameplay\mid-term-gpu-timers-r1` passed 12/12 across default, explicit ARB2, and executor-prep variants.
  - [x] Reports captured resolved GPU totals in the summary table plus benchmark `gpu(total/3d/2d/rt/copy/special/lens/setbuf/swap/deferred/forward/composite/indirect)` buckets. `rendererMetrics summary gpu` ranged from 1 ms to 11 ms; the highest summary sample was storage1 executor-prep. Log scan found no renderer failure signatures.
  - [x] Decision: keep this as diagnostic evidence only; timer queries can perturb pacing, so no default renderer or performance-policy change is made from this capture.
- [x] Decide whether any candidate path is ARB2-or-better for the relevant scenes.
  - [x] Decision: no candidate path is ARB2-or-better across the first target set. Executor-prep improved storage1, but regressed airdefense1, storage2, and medlabs versus current default; explicit ARB2 improved airdefense1 but trailed current default in storage2 and medlabs.

## Cross-Platform Validation

- [x] Keep Windows x64 as the first validation target for this tranche.
  - [x] First Windows platform capability notes captured from `tier-auto`, `tier-gl33`, and `tier-gl45`: `.tmp/renderer-validation/mid-term-platform-notes-r1/renderer_validation_report.md`.
- [x] Resolve Linux x64 validation status.
  - [x] Deferred beyond this mid-term implementation tranche; no Linux x64 renderer promotion or portability claim is made from Windows-only evidence.
- [x] Resolve macOS GL 4.1 validation status.
  - [x] Deferred beyond this mid-term implementation tranche; no macOS GL 4.1 renderer promotion or portability claim is made from Windows-only evidence.
- [x] Resolve per-platform capability recording status.
  - [x] For completed platform runs, record selected GL tier.
  - [x] For completed platform runs, record context profile.
  - [x] For completed platform runs, record debug-context availability.
  - [x] For completed platform runs, record upload manager mode.
  - [x] For completed platform runs, record timer-query availability.
  - [x] For completed platform runs, record multibind/DSA availability.
  - [x] Validation reports now extract these fields for completed startup cases into `Captured Platform Capability Summary` and JSON `capturedPlatformSummaries` entries.
- [x] Treat platform-specific driver differences as validation notes unless they require code changes.
  - [x] Windows x64 is the only completed platform in this tranche; Linux/macOS entries are deferred and must not be inferred from Windows.
- [x] Do not promote debug-output opt-in cases into default cross-platform validation without a fresh policy decision.
  - [x] Real-debug-context/debug-output probes remain opt-in diagnostics.

## Harness And Reporting

- [x] Make any new benchmark profile show up in `tools/tests/renderer_gameplay_benchmark.py --list`.
- [x] Make any new validation case show up in `tools/tests/renderer_validation_matrix.py --list`.
  - [x] `graph-invalidation` is listed as the mid-term render-graph invalidation A/B profile.
  - [x] `performance-comparison` is listed as the mid-term ARB2-or-better comparison profile.
  - [x] `presentation-comparison` is listed as the mid-term capped/uncapped/VSync presentation pacing profile.
  - [x] `visual-comparison` is listed as the mid-term screenshot comparison profile.
- [x] Keep report metadata sufficient for reproduction:
  - [x] executable.
  - [x] base path.
  - [x] save path.
  - [x] dev path.
  - [x] game dir.
  - [x] compact filesystem id.
  - [x] launch command.
  - [x] selected launch variants.
  - [x] cvar overrides.
  - [x] elapsed time.
  - [x] timeout state.
  - [x] Reporting update: gameplay benchmark reports now include global executable/base/dev/game/cvar metadata, per-role save/dev/game/autoexec/cvar metadata, full per-role launch commands in JSON, and full launch-command blocks for failed/planned roles.
  - [x] Path-length update: gameplay benchmark save/log/stdout/stderr paths now use a compact stable filesystem id so long descriptive case ids do not trip Windows path-length limits before gameplay starts.
- [x] Keep failure summaries readable without manual log archaeology.
  - [x] Failed and planned role details now show working/save paths, autoexec paths, expected logs/screenshots, cvar overrides, exec commands, missing checks, and the exact launch command.
- [x] Add report fields only when they reduce repeated manual parsing.
- [x] Keep opt-in stress profiles separate from default safe validation.

## Documentation And Release Notes

- [x] Update this checklist as each mid-term item is completed.
- [x] Update `docs-dev/release-completion.md` for user-visible renderer reliability, validation, performance, packaging, or compatibility changes.
- [x] Keep release-note language benefit-first.
  - [x] First-tranche release-completion entries lead with user-facing validation/reliability benefit and call out that defaults remain unchanged where applicable.
- [x] Avoid internal-only implementation detail in release notes unless it explains user impact.
  - [x] Detailed evidence remains in this checklist and `.tmp` reports; release-completion entries summarize outcome and compatibility/default-policy impact.
- [x] Update the renderer optimization plan if mid-term decisions change the roadmap.
  - [x] Parent plan records first-tranche status and keeps later/API/RenderDoc/cross-platform gates explicit.
- [x] Link final validation artifacts from this checklist.
  - [x] Filled artifact list below now links the first Windows x64 tranche reports and manual capture notes.

## Completion Criteria

- [x] Upload-pressure matrix has baseline and stress artifacts for representative scenes.
  - [x] Combined evidence: `.tmp/renderer-gameplay/mid-term-upload-pressure-r1/renderer_gameplay_benchmark_report.md` plus `.tmp/renderer-gameplay/mid-term-upload-pressure-r2-pathfix/renderer_gameplay_benchmark_report.md`.
- [x] Transient invalidation is either implemented with evidence or explicitly deferred with captured reasoning.
  - [x] Round 1 implemented eligibility/cvar/metrics/self-test plumbing and deferred GL submission until pass-timed capture evidence exists.
- [x] State-cache/low-overhead call-count or metric evidence is captured.
  - [x] `.tmp/renderer-gameplay/mid-term-low-overhead-state-r1/renderer_gameplay_benchmark_report.md` passed 6/6 and includes low-overhead capability, graph DSA/classic, upload/fence, and state-cache metric tables.
- [x] Required SP/MP gameplay passes after mid-term edits.
  - [x] `.tmp/renderer-gameplay/mid-term-required-gameplay-r1/renderer_gameplay_benchmark_report.md` passed 7/7 required cases with SP and MP loopback coverage.
- [x] Broad safe renderer matrix passes after mid-term edits.
  - [x] `.tmp/renderer-validation/mid-term-safe-matrix-r1/renderer_validation_report.md` passed 36/36 default safe cases; opt-in real-debug-context restart probes remain separate by policy.
- [x] Candidate performance data is compared against ARB2/default baselines.
  - [x] `.tmp/renderer-gameplay/mid-term-performance-comparison-r1/renderer_gameplay_benchmark_report.md` captured 12/12 matched default, ARB2, and executor-prep runs; result does not support a renderer default change.
- [x] Cross-platform validation status is recorded, even if only Windows is complete.
  - [x] Reported current status in `.tmp/renderer-validation/mid-term-platform-notes-r1/renderer_validation_report.md`; Linux x64 and macOS GL 4.1 are deferred beyond this mid-term implementation tranche.
- [x] Release-completion notes are current.
- [x] `git diff --check` passes with no whitespace errors.
  - [x] Current check reports only CRLF conversion warnings from the working tree, not whitespace errors.
- [x] Companion `openQ4-GameLibs` changes are either absent or intentionally documented.
  - [x] Companion repo has pre-existing local modifications under `src/game` and `src/mpgame`; this round made no GameLibs edits.

## Artifacts To Fill

- [x] Upload-pressure matrix report: `.tmp/renderer-gameplay/mid-term-upload-pressure-r1/renderer_gameplay_benchmark_report.md` and `.tmp/renderer-gameplay/mid-term-upload-pressure-r2-pathfix/renderer_gameplay_benchmark_report.md`
- [x] Transient invalidation report or deferral note: `.tmp/renderer-validation/mid-term-transient-invalidation-r1/transient_invalidation_note.md`
- [x] Graph invalidation GPU evidence bundle: `.tmp/renderer-gameplay/mid-term-graph-invalidation-r1/renderer_gameplay_benchmark_report.md`
- [x] Low-overhead/state-cache comparison report: `.tmp/renderer-gameplay/mid-term-low-overhead-state-r1/renderer_gameplay_benchmark_report.md`
- [x] Required gameplay rerun: `.tmp/renderer-gameplay/mid-term-required-gameplay-r1/renderer_gameplay_benchmark_report.md`
- [x] Broad safe renderer validation: `.tmp/renderer-validation/mid-term-safe-matrix-r1/renderer_validation_report.md`
- [x] Performance comparison bundle: `.tmp/renderer-gameplay/mid-term-performance-comparison-r1/renderer_gameplay_benchmark_report.md`
- [x] GPU timer comparison bundle: `.tmp/renderer-gameplay/mid-term-gpu-timers-r1/renderer_gameplay_benchmark_report.md`
- [x] Presentation comparison bundle: `.tmp/renderer-gameplay/mid-term-presentation-comparison-r1/renderer_gameplay_benchmark_report.md`
- [x] Visual comparison bundle: `.tmp/renderer-gameplay/mid-term-visual-comparison-r1/renderer_gameplay_benchmark_report.md`
- [x] Rollback command gameplay proof: `.tmp/renderer-gameplay/mid-term-rollback-r2/renderer_gameplay_benchmark_report.md`
- [x] RenderDoc/manual capture notes: `.tmp/renderer-captures/mid-term-renderdoc-notes-r1/manual_capture_notes.md`
- [x] Cross-platform notes: `.tmp/renderer-validation/mid-term-platform-notes-r1/renderer_validation_report.md`

## Open Decisions

- [x] Should transient framebuffer invalidation be implemented in this mid-term tranche or deferred until capture tooling proves a bandwidth win? Decision: implement resource-manager eligibility, default-off arming, metrics, benchmark report extraction, and self-test coverage now; defer actual GL submission until a pass owner/executor can emit it at end-of-pass and capture tooling proves the bandwidth/visual result.
- [x] Should upload-stress become a first-class benchmark profile with launch-cvar variants, or remain a documented command matrix? Decision: first-class `upload-pressure` profile with launch-cvar variants, plus the reproduction command documented above.
- [x] Should any low-overhead state-cache counter become a release-signoff gate? Decision: no new release-signoff gate yet; keep the GL 3.3 versus GL 4.5 state/bind profile as opt-in evidence until captured metrics and API traces show a stable threshold.
- [x] Which target scenes define "ARB2-or-better" performance for this tranche? Decision: first Windows x64 tranche uses `game/storage1`, `game/airdefense1`, `game/storage2`, and `game/medlabs`, with MP/listen evidence kept in the required gameplay profile rather than the first performance matrix.
- [x] Which platforms are required before making any mid-term renderer promotion claim? Decision: Windows x64, Linux x64, and macOS GL 4.1 all need completed validation artifacts before any mid-term renderer promotion or portability claim; Windows x64 is current, Linux x64 and macOS are deferred beyond this mid-term implementation tranche.

## Closure

- [x] All local Windows x64 mid-term implementation and evidence tasks are resolved.
- [x] External/manual promotion gates are resolved for this plan by deferral, not by evidence completion: API/driver capture, approved visual references, RenderDoc tier captures, Linux x64 validation, and macOS GL 4.1 validation remain required before promotion or portability claims.
- [x] No renderer default, upload policy, transient-invalidation policy, low-overhead state policy, or promotion policy changes are made from this mid-term tranche.
