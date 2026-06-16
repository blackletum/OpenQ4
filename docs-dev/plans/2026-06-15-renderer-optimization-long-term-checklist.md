# Renderer Optimization Long-Term Checklist

Date: 2026-06-16

This document tracks the long-term renderer optimization work after the completed immediate, next-steps, near-term, and mid-term renderer phases in:

- `docs-dev/plans/2026-06-15-renderer-optimization.md`
- `docs-dev/plans/2026-06-15-renderer-optimization-next-steps-checklist.md`
- `docs-dev/plans/2026-06-15-renderer-optimization-near-term-checklist.md`
- `docs-dev/plans/2026-06-15-renderer-optimization-mid-term-checklist.md`

## Baseline

- [x] Treat the immediate renderer hardening phase as complete.
- [x] Treat the next-steps audit checklist as complete.
- [x] Treat the near-term renderer checklist as complete.
- [x] Treat the mid-term Windows x64 implementation tranche as closed without renderer-default promotion.
- [x] Preserve the mid-term policy decisions:
  - [x] Stock ARB2-compatible rendering remains the default.
  - [x] Modern-visible, deferred, forward+, GPU-driven, bindless, GPU-validation, and shader-reload paths remain opt-in.
  - [x] `r_rendererGraphInvalidate` remains default-off until pass-owned GL invalidation is implemented and capture-validated.
  - [x] Timer-query captures remain diagnostic evidence, not acceptance pacing data.
  - [x] No API-level bind-count, approved-reference visual parity, renderer-promotion, or portability claim is made from Windows-only mid-term evidence.
- [x] Preserve the mid-term local evidence bundle:
  - [x] Upload-pressure matrix: `.tmp/renderer-gameplay/mid-term-upload-pressure-r1/renderer_gameplay_benchmark_report.md`
  - [x] Upload path-length repair rerun: `.tmp/renderer-gameplay/mid-term-upload-pressure-r2-pathfix/renderer_gameplay_benchmark_report.md`
  - [x] Low-overhead state/bind metrics: `.tmp/renderer-gameplay/mid-term-low-overhead-state-r1/renderer_gameplay_benchmark_report.md`
  - [x] Required SP/MP gameplay: `.tmp/renderer-gameplay/mid-term-required-gameplay-r1/renderer_gameplay_benchmark_report.md`
  - [x] Broad safe renderer matrix: `.tmp/renderer-validation/mid-term-safe-matrix-r1/renderer_validation_report.md`
  - [x] Performance comparison: `.tmp/renderer-gameplay/mid-term-performance-comparison-r1/renderer_gameplay_benchmark_report.md`
  - [x] GPU timer comparison: `.tmp/renderer-gameplay/mid-term-gpu-timers-r1/renderer_gameplay_benchmark_report.md`
  - [x] Presentation comparison: `.tmp/renderer-gameplay/mid-term-presentation-comparison-r1/renderer_gameplay_benchmark_report.md`
  - [x] Visual screenshot inspection: `.tmp/renderer-gameplay/mid-term-visual-comparison-r1/renderer_gameplay_benchmark_report.md`
  - [x] Graph-invalidation GPU evidence: `.tmp/renderer-gameplay/mid-term-graph-invalidation-r1/renderer_gameplay_benchmark_report.md`
  - [x] Rollback command gameplay proof: `.tmp/renderer-gameplay/mid-term-rollback-r2/renderer_gameplay_benchmark_report.md`
  - [x] Manual/API capture notes: `.tmp/renderer-captures/mid-term-renderdoc-notes-r1/manual_capture_notes.md`
  - [x] Platform capability notes: `.tmp/renderer-validation/mid-term-platform-notes-r1/renderer_validation_report.md`

## Long-Term Goal

- [ ] Convert the deferred renderer-promotion gates and later optimization ideas into reviewed, cross-platform evidence before changing renderer defaults.
- [ ] Keep stock Quake 4 asset compatibility as the acceptance target for every renderer path.
- [ ] Keep `.install/` as staged runtime output and `.tmp/` as the evidence root.
- [ ] Keep `content/baseoq4/` changes minimal and avoid engine-side replacement content unless a compatibility fix cannot reasonably live in code.
- [ ] Treat `E:\Repositories\openQ4-GameLibs` as part of the workspace only when gameplay validation or benchmark readiness requires canonical game-library edits.

## Promotion Evidence Bundle

Round 13 evidence, 2026-06-16:
- Promotion evidence bundle summary: `.tmp/renderer-validation/long-term-promotion-evidence-r1/promotion_evidence_summary.md`.
- Added `tools/tests/renderer_promotion_evidence.py`, a conservative promotion-evidence aggregator that reads existing safe validation, gameplay, visual, capture, performance, presentation, rollback, default-safety, and platform reports and writes Markdown/JSON summaries.
- The long-term evidence token remains the existing engine contract: `phase8=complete;warnings=0;visual=pass;gameplay=pass;renderdoc=pass;perf=arb2-or-better;presentation=pass;rollback=pass;debug=off`. The checklist artifact that decides whether that token may be used is now the reviewed `promotion_evidence_summary.md` bundle.
- The first summary is intentionally blocked: zero-warning safe validation, required SP gameplay, required MP gameplay, presentation, rollback, debug-off defaults, and Windows x64 platform evidence are present; approved reference-backed visual comparison, RenderDoc/API review, ARB2-or-better performance review, Linux x64, macOS GL 4.1, and the final promotion/no-promotion decision remain open.
- Added the tool to the GitHub script-smoke `py_compile` checks. CI remains responsible for safe syntax and assetless renderer validation artifacts, while stock-asset gameplay, approved references, RenderDoc/API traces, and macOS GL evidence remain local/manual until suitable runners exist.

- [x] Define the exact long-term promotion evidence token or checklist entry that can satisfy `r_rendererPromotionEvidence`.
- [x] Require a reviewed evidence bundle before enabling `r_rendererModernAutoPromote` in any default path.
- [x] Include zero-warning safe renderer validation in the bundle.
- [x] Include required SP gameplay coverage in the bundle.
- [x] Include required MP listen/client gameplay coverage in the bundle.
- [ ] Include approved deterministic visual references in the bundle.
- [ ] Include RenderDoc/API captures in the bundle.
- [ ] Include ARB2-or-better performance comparison in the bundle.
- [x] Include capped, uncapped, and VSync presentation coverage in the bundle.
- [x] Include rollback command coverage in the bundle.
- [x] Include debug-off and experimental-path-off default coverage in the bundle.
- [ ] Include Windows x64, Linux x64, and macOS GL 4.1 platform evidence before making any renderer-promotion or portability claim.
- [x] Allow the promotion bundle to consume a reviewed cross-platform evidence summary when one is available.
- [ ] Review the completed bundle and record the promotion/no-promotion decision in this checklist and the parent plan.

## ARB2-Or-Better Performance Evidence

Round 17 evidence, 2026-06-16:
- Performance summary: `.tmp/renderer-validation/long-term-performance-summary-r1/performance_summary.md`.
- Performance review template: `.tmp/renderer-validation/long-term-performance-summary-r1/performance_review_template.json`.
- Promotion integration check: `.tmp/renderer-validation/long-term-promotion-evidence-r4-performance-summary/promotion_evidence_summary.md`.
- Added `tools/tests/renderer_performance_summary.py`, which defines the long-term `performance-comparison` matrix for `game/storage1`, `game/airdefense1`, `game/storage2`, and `game/medlabs`; compares the candidate variant against explicit `r_renderer arb2`; keeps the current default as a secondary baseline; checks P50/P95/P99/max frame time, pacing Hz, warning counts, required variants, and wall-clock sampling; and writes a review manifest template for platform/GL metadata and explicit approval.
- The first summary consumes the existing mid-term performance report and correctly fails the `executor` candidate as not ARB2-or-better on `game/airdefense1`: executor P95/P99/max/Hz were `30/33/34 ms/42.8 Hz` versus explicit ARB2 `14/16/18 ms/95.8 Hz`. Storage1 and medlabs were better than ARB2, storage2 stayed within tolerance, but the agreed gate requires every scene to pass.
- Feeding that failed performance summary into the promotion bundle keeps `perf=arb2-or-better` unsatisfied. This is a hard performance failure for the sampled candidate, not merely missing paperwork.

- [x] Define the repeatable performance comparison matrix before accepting `perf=arb2-or-better`.
- [x] Compare the current executor-prep candidate against explicit ARB2 and the current default on the agreed Windows x64 scene set.
- [x] Record the current sampled result as not ARB2-or-better because at least one required scene regressed.
- [ ] Re-run after candidate changes or a different candidate path is selected.
- [ ] Require a fresh passing summary before setting `perf=arb2-or-better` in promotion evidence.

## Approved Visual References

Round 16 evidence, 2026-06-16:
- Visual reference summary: `.tmp/renderer-references/long-term-visual-reference-manifest-r1/visual_reference_summary.md`.
- Visual reference manifest template: `.tmp/renderer-references/long-term-visual-reference-manifest-r1/visual_reference_manifest_template.json`.
- Added `tools/tests/renderer_visual_reference_summary.py`, which defines the long-term `visual-comparison` approval matrix, writes a lightweight manifest template, consumes reference-backed gameplay benchmark reports, checks per-role image comparison status, enforces RMS/max thresholds, requires platform/GL metadata, and records the human-review checklist for black frames, post-process, GUI/subview composition, lens flare, shadow/stencil fallback, BSE visibility, and MP HUD/weapon-view coverage.
- Standard policy decision: keep binary approved reference screenshots external/manual under `.tmp/renderer-references/...`; use manifest and summary JSON/Markdown as the auditable review evidence. Do not add binary reference images to the repo by default.
- The first summary is intentionally blocked because the approved reference directory, filled manifest approval, source `visual-comparison --require-references` gameplay report, and per-role approved references have not been collected yet.
- Integration check: `.tmp/renderer-validation/long-term-promotion-evidence-r3-visual-summary/promotion_evidence_summary.md` confirms that feeding the blocked visual-reference summary into the promotion bundle keeps renderer promotion blocked instead of satisfying `visual=pass`.

- [ ] Select the approved Windows x64 reference source for mid-term/long-term visual comparison.
- [ ] Create `.tmp/renderer-references/mid-term-visual/windows-x64` from reviewed captures.
- [ ] Re-run `visual-comparison` with `--reference-dir .tmp\renderer-references\mid-term-visual\windows-x64 --require-references`.
- [ ] Record RMS/max image thresholds and any justified per-scene exceptions.
- [ ] Inspect reference comparisons for:
  - [ ] black frames.
  - [ ] missing post-process output.
  - [ ] missing GUI/subview composition.
  - [ ] lens-flare accumulation/composite drift.
  - [ ] shadow-map/stencil fallback drift.
  - [ ] BSE effect visibility regressions.
  - [ ] MP listen/client HUD or weapon-view drift.
- [ ] Preserve contact sheets and comparison reports under `.tmp/renderer-gameplay/<long-term-run>/`.
- [x] Decide whether approved references should move from `.tmp/` into a tracked lightweight reference manifest or stay external/manual. Decision: keep binary screenshots external/manual under `.tmp/`, and use lightweight manifest/summary evidence for review.

## RenderDoc And API Capture

Round 14 evidence, 2026-06-16:
- RenderDoc/API capture matrix summary: `.tmp/renderer-captures/long-term-capture-matrix-r1/capture_summary.md`.
- Capture manifest template: `.tmp/renderer-captures/long-term-capture-matrix-r1/capture_manifest_template.json`.
- Added `tools/tests/renderer_capture_summary.py`, which defines the long-term capture matrix, per-case naming convention, required platform/GL metadata, cvar observations, and pass/resource/API checks. It writes `capture_summary.md/json` and can fail closed with `--require-complete`.
- Standard naming decision: keep capture evidence under `.tmp/renderer-captures/<run>/<case-id>/`; use `<case-id>.rdc` for RenderDoc captures, `<case-id>.api-trace.json` for API/driver trace evidence, `review.json` for per-case review metadata, and `capture_summary.md/json` as the run-level artifact.
- Capture-tool decision: use RenderDoc `.rdc` for forced core-profile modern tiers where the renderer can run under the tool; use API/driver traces for the ARB2 compatibility/default rollback paths and for low-overhead bind-count or texture-target claims. The first summary is intentionally blocked with all eight rows missing.
- Integration check: `.tmp/renderer-validation/long-term-promotion-evidence-r2-capture-summary/promotion_evidence_summary.md` confirms that feeding the blocked capture summary into the promotion bundle keeps renderer promotion blocked.

- [x] Define the capture matrix before collecting traces:
  - [x] default ARB2-compatible path.
  - [x] explicit `r_renderer arb2` rollback path.
  - [x] modern executor prepare-only path.
  - [x] modern-visible opt-in path.
  - [x] graph-invalidation armed path.
  - [x] low-overhead GL 4.5 path.
  - [x] GL 3.3 fallback path.
- [ ] Capture representative scenes:
  - [ ] `game/storage1`
  - [ ] `game/airdefense1`
  - [ ] `game/storage2`
  - [ ] `game/medlabs`
  - [ ] `game/mcc_landing`
  - [ ] `mp/q4dm1` listen/client roles where capture tooling supports it.
- [ ] Confirm named resources and expected pass contents for:
  - [ ] scene color.
  - [ ] depth.
  - [ ] post-process scratch.
  - [ ] lens-flare accumulation/composite resources.
  - [ ] light-grid resources.
  - [ ] G-buffer resources when modern-visible/deferred paths are enabled.
  - [ ] shadow-map resources.
- [ ] Use API traces to confirm state/bind-count reductions before making bind-count claims.
- [ ] Compare texture bind targets in no-multibind fallback paths, including non-2D texture targets.
- [ ] Confirm graph-invalidation calls are emitted only after final use once pass-owned invalidation is implemented.
- [ ] Record capture file locations, driver version, GPU, OS, GL version, selected tier, and cvar set.
- [x] Add a summary artifact under `.tmp/renderer-captures/<long-term-run>/`.

## Cross-Platform Validation

Round 18 evidence, 2026-06-16:
- Cross-platform evidence summary: `.tmp/renderer-validation/long-term-platform-summary-r1/platform_summary.md`.
- Platform evidence manifest template: `.tmp/renderer-validation/long-term-platform-summary-r1/platform_evidence_manifest_template.json`.
- Promotion integration check: `.tmp/renderer-validation/long-term-promotion-evidence-r5-platform-summary/promotion_evidence_summary.md`.
- Added `tools/tests/renderer_platform_summary.py`, which defines the Windows x64, Linux x64, and macOS GL 4.1 evidence matrix; consumes safe validation, required SP/MP gameplay, reference-backed visual comparison, presentation pacing, and optional platform-notes reports; extracts available executable/context/tier/upload/timer/DSA/multibind/GPU-driven capability metadata; and requires explicit reviewed OS/GPU/driver/GL metadata before any platform row can pass.
- The first summary is intentionally blocked. Windows x64 has existing safe validation, required gameplay, and presentation evidence, but still needs approved reference-backed visual comparison plus reviewed GPU/driver/vendor/renderer metadata. Linux x64 and macOS GL 4.1 remain missing rather than inferred from Windows evidence.
- The promotion aggregator now accepts `--platform-summary`, so the richer platform ledger can participate in the `phase8=complete` gate. The first integrated promotion bundle remains failed because `perf=arb2-or-better` is still a hard failure, while the cross-platform ledger adds a needs-review portability blocker.

- [x] Define a reviewed platform evidence manifest before making renderer portability claims.
- [ ] Add Linux x64 validation once a Linux test machine or CI runner with GL coverage is available.
- [ ] Add macOS GL 4.1 validation once a macOS GL test machine is available.
- [ ] For each completed platform, record:
  - [ ] executable path.
  - [ ] OS version.
  - [ ] GPU and driver.
  - [ ] GL vendor/renderer/version.
  - [ ] selected GL tier.
  - [ ] requested GL tier.
  - [ ] context profile.
  - [ ] debug-context availability.
  - [ ] timer-query availability.
  - [ ] upload manager mode.
  - [ ] persistent-map support.
  - [ ] map-range/subdata fallback support.
  - [ ] DSA availability.
  - [ ] multibind availability.
  - [ ] GPU-driven capability availability.
- [ ] Run the broad safe renderer matrix on Windows x64, Linux x64, and macOS GL 4.1.
- [ ] Run required SP/MP gameplay on Windows x64, Linux x64, and macOS GL 4.1.
- [ ] Run visual comparison on Windows x64, Linux x64, and macOS GL 4.1 after references exist.
- [ ] Run presentation pacing checks on Windows x64, Linux x64, and macOS GL 4.1.
- [ ] Treat platform-specific driver differences as validation notes unless they require code changes.
- [x] Do not infer Linux or macOS readiness from Windows x64 evidence.

## Render-Graph Invalidation Backend

Round 1 evidence, 2026-06-16:
- Self-test and startup validation: `.tmp/renderer-validation/long-term-graph-invalidation-r1/renderer_validation_report.md`.
- SP gameplay probe: `.tmp/renderer-gameplay/long-term-graph-invalidation-r1/renderer_gameplay_benchmark_report.md`.
- The self-test exercised disabled-cvar, missing-owner, and supported-tier/unsupported-tier submit paths. The gameplay probe entered `game/storage1` with `r_rendererGraphInvalidate 1` and armed candidates, but submitted zero invalidations because that run stayed on legacy-fallback ownership for the sampled passes.
- Visual references, API capture evidence, positive gameplay submit evidence, and any default-policy decision remain open.

Round 10 evidence, 2026-06-16:
- Safe render-graph/resource self-test validation: `.tmp/renderer-validation/long-term-graph-invalidation-accounting-r1/renderer_validation_report.md`.
- Modern-side invalidation accounting gameplay report: `.tmp/renderer-gameplay/long-term-graph-invalidation-modern-r1/renderer_gameplay_benchmark_report.md`.
- The new `graph-invalidation-modern` gameplay profile writes modern side-path cvars after map load, enables the modern executor and GPU timers by default, compares default and `r_rendererGraphInvalidate`-armed launch variants, and fails if the expected modern-side cvars or graph-invalidation counters are missing.
- The first Windows x64 `game/storage1` run passed both variants. Default reported `enabled=0`, `tagged=36`, `candidates=3`, `armed=0`, `submitted=0`, `unsubmittedPass=0`; the armed run reported `enabled=1`, `tagged=36`, `candidates=3`, `armed=3`, `submitted=0`, `unsubmittedPass=3`.
- This closes the previous accounting blind spot for armed candidates that never reach an owning pass submit hook. Capture-positive GL discard evidence remains open because no gameplay pass submitted `glInvalidateFramebuffer` in this run.

- [x] Thread render-graph last-use/discard data into the pass owner or executor that can issue GL invalidation after final use.
- [x] Keep resource-manager eligibility classification separate from GL submission.
- [ ] Emit invalidation only when:
  - [x] the attachment has no later read/write/resolve/present use in the frame.
  - [x] the pass owner has completed the final use.
  - [x] the active GL tier supports the required invalidation call.
  - [ ] visual comparison and capture evidence show no regression.
- [x] Add metrics for submitted invalidation calls by pass/resource.
- [x] Add skip reasons for unsupported tier, later use, missing owner, missing FBO, disabled cvar, and armed candidates whose owning pass did not submit.
- [x] Extend `rendererRenderGraphResourceSelfTest` or add a focused self-test for pass-owned invalidation submission.
- [x] Add a gated gameplay profile that verifies modern side-path cvars are applied after map load and that default-vs-armed invalidation candidates are accounted.
- [ ] Compare default versus armed runs with GPU timers after submission exists.
- [ ] Confirm RenderDoc/API traces show expected invalidation calls.
- [ ] Decide whether `r_rendererGraphInvalidate` should remain default-off, become a validation profile, or become default-on for specific tiers.

## Upload And Synchronization Follow-Up

Round 2 evidence, 2026-06-16:
- Long-run upload-pressure probe: `.tmp/renderer-gameplay/long-term-upload-pressure-r1/renderer_gameplay_benchmark_report.md`.
- The `upload-pressure-long` profile now carries 30-second sample, 600-frame settle, and 420-second timeout defaults so intermittent upload stalls can be sampled without repeating timing flags on every command.
- The first bounded Windows x64 probe ran `game/storage1` with the reduced-ring upload variant and parsed every required upload counter: waits, timeouts, fallbacks, ring high-water, overflow KB, and frame stalls. The sampled run reported `wait=0 timeout=0 fallback=0`, ring high-water `977/4096KB`, overflow `0KB`, and stalls `0`.
- Broader hardware, longer full-matrix coverage, coherent persistent mapping comparison, and upload-default decisions remain open.

- [ ] Re-run upload-pressure on broader hardware, including slower GPUs and integrated GPUs.
- [x] Add or confirm counters for:
  - [x] fence wait count.
  - [x] fence timeout count.
  - [x] fallback count.
  - [x] upload ring high-water.
  - [x] overflow KB.
  - [x] frame stalls.
- [ ] Evaluate coherent persistent mapping versus explicit flush on target GPUs.
- [ ] Evaluate whether `r_rendererUploadMegs` should remain unchanged after broader captures.
- [ ] Evaluate whether `r_rendererUploadFrameBuffers` should remain unchanged after broader captures.
- [x] Add a long-run upload-pressure profile if short 3-second samples miss intermittent stalls.
- [ ] Record any upload policy change with before/after gameplay evidence and release-completion notes.

## Low-Overhead State And Bind Follow-Up

Round 3 evidence, 2026-06-16:
- Focused no-multibind fallback validation: `.tmp/renderer-validation/long-term-low-overhead-no-multibind-r1/renderer_validation_report.md`.
- Added `renderer-no-multibind-fallback-selftest` to the safe validation matrix and documented it in `docs-dev/renderer-validation-matrix.md`.
- The case runs the GL state-cache self-test under forced `r_glTier gl45`; the self-test temporarily routes texture-group binding through the no-multibind fallback and verifies mixed 2D/cube-map targets. The log recorded `textureMultiBindFallback=2`, `mixedTargetFallback=1`, selected `LowOverheadGL45`, and zero warning signatures.

Round 11 evidence, 2026-06-16:
- Modern-submit low-overhead gameplay report: `.tmp/renderer-gameplay/long-term-low-overhead-submit-r1/renderer_gameplay_benchmark_report.md`.
- Added the `low-overhead-submit` gameplay profile, which forces `r_glTier gl45`, enables the modern executor, turns on `r_rendererModernSubmit 1` after map load, enables GPU timers, and fails if submitted diagnostic draws, low-overhead readiness, graph DSA activity, texture/sampler multibind batches, restore counters, or zero submit-fallback/missing-buffer counters are absent.
- The first Windows x64 run passed `game/storage1` and `game/medlabs`. Storage reported `submitted=1/12`, `program=2`, `vbo=9`, `missing=0/0`, `graphDSA=6/26/6`, `tex/samp multibind=1/1`, and `legacyReset=1`; medlabs reported `submitted=1/192`, `program=3`, `vbo=136`, `missing=0/0`, `graphDSA=6/26/6`, `tex/samp multibind=2/1`, and `legacyReset=1`.
- Release-signoff policy decision: keep low-overhead submit counters as an opt-in validation gate for now; do not make them release sign-off or default-promotion gates until API captures corroborate reduced GL call counts.
- API capture and any new default state-filtering policy remain open.

- [ ] Use API capture to compare GL call counts for GL 3.3 fallback and GL 4.5 low-overhead paths.
- [x] Exercise draw-submission paths that produce DSA updates and multibind batches, not only prepare-only scenes.
- [x] Confirm no-zero-unbind behavior under modern submit paths.
- [x] Confirm texture multibind fallback remains target-aware for non-2D textures.
- [x] Add a validation case that forces no-multibind fallback where supported by the harness.
- [x] Decide whether any low-overhead state-cache counter becomes a release-signoff gate.
- [ ] Keep any new default state-filtering policy blocked until API traces and gameplay logs agree.

## Modern Renderer Promotion Candidate

Round 19 evidence, 2026-06-16:
- Candidate summary: `.tmp/renderer-validation/long-term-candidate-summary-r1/candidate_summary.md`.
- Candidate manifest template: `.tmp/renderer-validation/long-term-candidate-summary-r1/candidate_manifest_template.json`.
- Promotion integration check: `.tmp/renderer-validation/long-term-promotion-evidence-r6-candidate-summary/promotion_evidence_summary.md`.
- Added `tools/tests/renderer_candidate_summary.py`, which defines the modern candidate matrix, acceptance thresholds, required promotion evidence gates, and candidate review manifest before any default-promotion path can be selected.
- The first summary evaluates the currently measured `executor-prepare` candidate and marks it failed. It passes storage1, storage2, and medlabs against explicit ARB2 under the candidate thresholds, but `game/airdefense1` fails frame average, P50, P95, P99, max frame time, and pacing-Hz checks. The recommendation is `executor-prepare-disqualified`.
- The other candidate rows, `modern-visible-hybrid`, `deferred-lite`, `forward-plus`, and `gpu-driven-submit`, are documented but not selected because they do not yet have performance-comparison variants or the required visual/capture/platform evidence. No renderer default changes from this round.

- [x] Define which modern path is the currently measured candidate:
  - [x] executor prepare-only.
  - [x] modern-visible hybrid.
  - [x] deferred-lite.
  - [x] forward+.
  - [x] GPU-driven submit.
- [x] Define candidate acceptance thresholds for:
  - [x] frame average.
  - [x] P50.
  - [x] P95.
  - [x] P99.
  - [x] max frame time.
  - [x] fallback counts.
  - [x] renderer warning signatures.
  - [x] visual diff thresholds.
  - [x] rollback success.
- [x] Compare the current measured candidate against current default and explicit ARB2 on the target scene set.
- [x] Require candidate to be ARB2-or-better across the agreed scenes before any default-policy change.
- [x] Preserve explicit `r_renderer arb2` escape behavior as a required candidate gate.
- [x] Preserve `r_glTier legacy` behavior as a fail-closed compatibility route through the existing rollback/default-safety gates.
- [x] Keep debug, validation, bindless, shader-reload, and experimental side paths off by default as a required candidate gate.
- [ ] Select a replacement candidate or re-run the measured candidate after performance changes.
- [ ] Record the final promotion/no-promotion decision in the parent plan.

## Shader And Test Asset Maintenance

Round 15 evidence, 2026-06-16:
- Shader source inventory report: `.tmp/renderer-validation/long-term-shader-source-inventory-r1/shader_source_inventory.md`.
- Added `tools/tests/renderer_shader_inventory.py`, a static audit that cross-checks the shader-kind enum, descriptor table, `ModernGLShaderProgramKind_Name`, GLSL 330/410/430/450 source generation, 64-slot program capacity, draw-record bounds guards, lens-flare tier self-test tokens, forced shader-tier validation cases, and the opt-in shader reload gate.
- The first report passed with 16 shader kinds, four GLSL tiers, and 64 expected program slots exactly matching `MODERN_GL_SHADER_MAX_PROGRAMS`. The explicit policy decision is to keep runtime shader sources internal C++ string builders; generated artifacts are validation-only Markdown/JSON reports under `.tmp/`, not loose runtime shader assets or packaged `.install/` content.

- [x] Decide whether generated shader sources should remain C++ string-built, move to generated templates, or become externalized test/build inputs.
- [x] Keep runtime behavior independent of loose shader-content files unless an explicit project decision changes the packaging model.
- [x] Add shader-library tests for any externalized/generated source flow.
- [x] Preserve GLSL 330, 410, 430, and 450 variant coverage.
- [x] Keep shader reload/debug paths opt-in.
- [x] Document any generated shader artifact flow in developer docs.

## Memory, Restart, And Residency

Round 4 evidence, 2026-06-16:
- Focused modern-cvar restart validation: `.tmp/renderer-validation/long-term-modern-cvar-vid-restart-r1/renderer_validation_report.md`.
- Added `renderer-modern-cvar-vid-restart` to the safe validation matrix and documented it in `docs-dev/renderer-validation-matrix.md`.
- The assetless Windows x64 case records `gfxInfo`, performs `vid_restart` after forcing `r_glTier gl45` with opt-in modern executor, visible, visible-depth, opaque G-buffer, deferred, and forward+ cvars enabled, then records `gfxInfo` again. The sampled run selected `LowOverheadGL45`, preserved the post-restart cvar state (`executor=1`, `visible=1`, `visibleDepth=1`, `opaque=1`, `deferred=1`, `forwardPlus=1`), and reported zero validation warning signatures.
- Leak-detection coverage, repeated map loads, SP/MP mode switches, live object-count tracking, and the manual ten-restart long-run loop remain open.

Round 5 evidence, 2026-06-16:
- Repeated-restart resource-count validation: `.tmp/renderer-validation/long-term-resource-count-vid-restart-r1/renderer_validation_report.md`.
- Added generic `valueChecks` support to `tools/tests/renderer_validation_matrix.py` so restart/residency probes can compare named values extracted from repeated log summaries and publish the stable baseline in Markdown/JSON reports.
- Added `renderer-resource-count-vid-restart`, an assetless Windows x64 case that forces `r_glTier gl45` with modern executor, visible-depth, G-buffer, deferred, and forward+ side paths enabled, runs three `vid_restart` cycles, and compares the last three post-restart `gfxInfo` samples.
- The sampled run selected `LowOverheadGL45`, reported zero validation warning signatures, and kept the tracked post-restart values stable: render-graph handles/textures/buffers/physical/FBO counts `0/0/0/0/0`, upload buffers `4`, static upload buffers `2/4096KB`, fences `0/0`, waits/timeouts/fallbacks `0/0/0`, and shader library programs `64` with `failed=0` and `highestGLSL=450`.
- External leak-detector coverage, repeated map-load growth checks, SP/MP mode switches, and the manual ten-restart long-run loop remain open.

Round 6 evidence, 2026-06-16:
- Repeated-map gameplay resource-count validation: `.tmp/renderer-gameplay/long-term-map-transition-r4/renderer_gameplay_benchmark_report.md`.
- Added the asset-dependent `map-transition-resource-count` gameplay profile, which runs one SP process through `game/storage1 -> game/storage2 -> game/storage1 -> game/storage2 -> game/storage1`, then compares post-warm repeated `storage1` samples.
- The sampled Windows x64 run passed with render-graph object counts stable across the post-warm `storage1` comparison: graph handles/textures/FBO-ready/FBO-total stayed `9/8/5/5`, upload frame buffers stayed `4`, fences stayed `1/1` with `wait=0 timeout=0 fallback=0`, and shader programs stayed `64` with `failed=0`.
- Static upload residency is kept visible in the report but tracked separately rather than used as the hard object-count leak gate: the same run recorded `uploadStaticLiveBuffers 103->166` and `uploadStaticLiveKB 6128->7427` as content-cache residency that may keep warming until a separate vertex-cache budget policy is implemented.
- External leak-detector coverage, SP/MP mode-switch object lifetime checks, and a static-residency budget decision remain open.

Round 7 evidence, 2026-06-16:
- SP/MP/SP mode-switch gameplay resource-count validation: `.tmp/renderer-gameplay/long-term-mode-transition-r4/renderer_gameplay_benchmark_report.md`.
- Added the asset-dependent `mode-transition-resource-count` gameplay profile, which runs one process through `SP/game_sp/game/storage1 -> MP/game_mp/mp/q4dm1 -> SP/game_sp/game/storage1` and compares the repeated initial SP sample after both game-module reloads.
- Preserved `logFile` across `reloadEngine` so a single report can retain pre-reload, MP, and post-reload SP markers instead of truncating the earlier samples when the file system reopens the log.
- The sampled Windows x64 run passed with render-graph object counts stable across the SP/MP/SP comparison: graph handles/textures/FBO-ready/FBO-total stayed `9/8/5/5`, upload frame buffers stayed `4`, fences stayed `1/1` with `wait=0 timeout=0 fallback=0`, and shader programs stayed `64` with `failed=0`.
- Static upload residency is visible and tracked separately in the mode-transition report. In the passing run the repeated SP sample returned to `uploadStaticLiveBuffers 38` and `uploadStaticLiveKB 4805` after the MP hop, while the intermediate MP sample reported `219/4806KB`.
- External leak-detector coverage and the static-residency budget/LRU decision remain open.

Round 8 evidence, 2026-06-16:
- Renderer shutdown/`vid_restart` live-object leak-audit validation: `.tmp/renderer-validation/long-term-shutdown-leak-audit-r1/renderer_validation_report.md`.
- Added default-off `r_rendererShutdownAudit` markers around full `vid_restart` teardown and final renderer shutdown, plus direct live-object counters for render-graph physical allocations/textures/FBOs, upload frame/mapped/fence/static/pool objects, modern executor VAO/buffer/FBO/sampler/helper-program/shader-library/sync objects, and clustered-lighting GL objects.
- Added `renderer-shutdown-leak-audit-vid-restart`, an assetless safe validation case that runs three full `vid_restart` cycles and final `+quit` under forced `LowOverheadGL45` with modern side-path cvars enabled.
- The first Windows x64 run passed with zero warning signatures. All three `vid_restart` post-teardown rows and the final `shutdown` post-teardown row reported `live=0 status=pass`; the value table kept every tracked post-teardown field at zero.
- External process/driver leak tools are still useful for platform sign-off, but renderer-owned GL object teardown now has an automated internal gate.

Round 9 evidence, 2026-06-16:
- Vertex-cache budget safe validation: `.tmp/renderer-validation/long-term-vertex-cache-budget-r1/renderer_validation_report.md`.
- Static-residency budget gameplay validation: `.tmp/renderer-gameplay/long-term-static-residency-budget-r1/renderer_gameplay_benchmark_report.md`.
- Added default-off `r_vertexBufferBudget`, using the existing `r_vertexBufferMegs` working-set value as an opt-in LRU budget for static vertex/index cache blocks. `r_vertexBufferBudgetFrames` protects recently touched blocks from purge so queued GPU work is not invalidated.
- Added `Renderer vertex cache:` diagnostics to `gfxInfo`, reporting mode, budget enable/status, limit, fixed/purgable/protected bytes, the last purge, and remaining over-budget KB.
- Added `rendererVertexCacheBudgetSelfTest` plus the safe `renderer-vertex-cache-budget-selftest` case. The first run passed under `TopGL46` with zero warning signatures and recorded `purged=1/2048KB` while leaving the touched block protected.
- Added the asset-dependent `static-residency-budget` gameplay profile, which runs the SP map-transition loop with `r_vertexBufferBudget 1`, `r_vertexBufferMegs 8`, and `r_vertexBufferBudgetFrames 2`.
- The first gameplay run passed. All five map-transition samples reported `budget=pass/0KB`; the final sample stayed under the 8MB budget at `166/7425KB` while graph/FBO, upload frame-buffer, fence, and shader counts remained stable under the existing gates.
- Decision: keep static upload residency tracked-only by default, but use the opt-in vertex-cache LRU budget gate for targeted residency validation and low-memory experiments. No default policy change is made from this Windows-only run.

Round 12 evidence, 2026-06-16:
- Dedicated-server BSE/renderer startup validation: `.tmp/renderer-validation/long-term-dedicated-bse-r1/renderer_validation_report.md`.
- Added `dedicated-disabled-bse-manager-startup` to the safe validation matrix. The case runs `.install/openQ4-ded_x64.exe`, requires the dedicated startup banner, requires `Attaching integrated BSE decl allocator with disabled runtime manager.`, verifies the MP game module loads, and rejects `ERROR:`, `idVertexCache Free: NULL pointer`, BSE-unavailable markers, or the full client BSE attach marker.
- Fixed dedicated renderer shutdown by tracking whether `idVertexCache::Init()` actually ran before `idVertexCache::Shutdown()` walks GL-backed cache lists. Dedicated servers initialize renderer scaffolding but never open GL, so the shutdown path now no-ops instead of reporting a null vertex-cache free.
- The first Windows x64 run passed with warning-signature count `0`, loaded `game-mp_x64.dll`, preserved the disabled-BSE runtime manager path, and shut down renderer scaffolding without the previous vertex-cache error.

- [x] Add or run leak-detection coverage for renderer shutdown and `vid_restart`.
- [x] Track GPU-memory residency or live object counts for:
  - [x] render-graph textures.
  - [x] framebuffer objects.
  - [x] upload buffers.
  - [x] static vertex/index buffers.
  - [x] sync objects.
  - [x] shader programs.
- [x] Validate repeated map loads without growth in renderer object counts.
- [x] Decide whether static vertex/index buffer residency needs an explicit LRU/budget gate beyond tracked benchmark reporting.
- [x] Validate SP to MP and MP to SP mode switches without renderer object leaks.
- [x] Validate `vid_restart` after toggling renderer-tier and modern-path cvars.
- [x] Preserve dedicated-server disabled-BSE-manager behavior unless a renderer change proves it needs adjustment.

## Automation And CI

- [x] Decide which long-term renderer checks belong in CI versus local/manual validation.
- [ ] Keep broad safe validation runnable without stock PK4 assets where possible.
- [x] Keep gameplay benchmark profiles asset-dependent and explicit.
- [x] Add CI artifacts for renderer validation reports where practical.
- [ ] Add optional/manual CI jobs for Linux GL validation if runner access exists.
- [ ] Keep macOS GL validation manual until reliable runner coverage exists.
- [x] Ensure new benchmark profiles appear in `tools/tests/renderer_gameplay_benchmark.py --list`.
- [x] Ensure new validation cases appear in `tools/tests/renderer_validation_matrix.py --list`.

## Documentation And Release Notes

- [x] Update this checklist as each long-term item is completed.
- [x] Update the parent renderer optimization plan when long-term decisions change the roadmap.
- [x] Update `docs-dev/release-completion.md` for user-visible renderer reliability, validation, performance, packaging, compatibility, input, or platform changes.
- [x] Keep release-note language benefit-first.
- [x] Avoid internal-only implementation detail in release notes unless it materially helps users.
- [ ] Document any new renderer rollback commands, promotion gates, or validation commands in user-facing docs when they affect testers.

## Artifacts To Fill

- [ ] Approved visual reference comparison report: `.tmp/renderer-gameplay/<long-term-visual-run>/renderer_gameplay_benchmark_report.md`
- [x] Visual reference manifest summary: `.tmp/renderer-references/long-term-visual-reference-manifest-r1/visual_reference_summary.md`
- [x] RenderDoc/API capture summary: `.tmp/renderer-captures/long-term-capture-matrix-r1/capture_summary.md`
- [ ] Linux x64 validation report: `.tmp/renderer-validation/<long-term-linux-run>/renderer_validation_report.md`
- [ ] macOS GL 4.1 validation report: `.tmp/renderer-validation/<long-term-macos-run>/renderer_validation_report.md`
- [ ] Cross-platform gameplay report: `.tmp/renderer-gameplay/<long-term-cross-platform-run>/renderer_gameplay_benchmark_report.md`
- [x] Cross-platform evidence summary: `.tmp/renderer-validation/long-term-platform-summary-r1/platform_summary.md`
- [x] Platform evidence manifest template: `.tmp/renderer-validation/long-term-platform-summary-r1/platform_evidence_manifest_template.json`
- [x] First-round graph-invalidation self-test report: `.tmp/renderer-validation/long-term-graph-invalidation-r1/renderer_validation_report.md`
- [x] First-round graph-invalidation gameplay probe: `.tmp/renderer-gameplay/long-term-graph-invalidation-r1/renderer_gameplay_benchmark_report.md`
- [x] Graph-invalidation accounting self-test report: `.tmp/renderer-validation/long-term-graph-invalidation-accounting-r1/renderer_validation_report.md`
- [x] First modern-side graph-invalidation accounting report: `.tmp/renderer-gameplay/long-term-graph-invalidation-modern-r1/renderer_gameplay_benchmark_report.md`
- [ ] Capture-positive pass-owned graph-invalidation report: `.tmp/renderer-gameplay/<long-term-graph-invalidation-run>/renderer_gameplay_benchmark_report.md`
- [x] First long-run upload-pressure probe: `.tmp/renderer-gameplay/long-term-upload-pressure-r1/renderer_gameplay_benchmark_report.md`
- [ ] Full long-run upload-pressure matrix: `.tmp/renderer-gameplay/<long-term-upload-run>/renderer_gameplay_benchmark_report.md`
- [x] First no-multibind fallback validation report: `.tmp/renderer-validation/long-term-low-overhead-no-multibind-r1/renderer_validation_report.md`
- [x] First low-overhead modern-submit gameplay report: `.tmp/renderer-gameplay/long-term-low-overhead-submit-r1/renderer_gameplay_benchmark_report.md`
- [x] First modern-cvar `vid_restart` validation report: `.tmp/renderer-validation/long-term-modern-cvar-vid-restart-r1/renderer_validation_report.md`
- [x] First repeated-restart resource-count validation report: `.tmp/renderer-validation/long-term-resource-count-vid-restart-r1/renderer_validation_report.md`
- [x] First repeated-map transition resource-count gameplay report: `.tmp/renderer-gameplay/long-term-map-transition-r4/renderer_gameplay_benchmark_report.md`
- [x] First SP/MP/SP mode-transition resource-count gameplay report: `.tmp/renderer-gameplay/long-term-mode-transition-r4/renderer_gameplay_benchmark_report.md`
- [x] First renderer shutdown leak-audit validation report: `.tmp/renderer-validation/long-term-shutdown-leak-audit-r1/renderer_validation_report.md`
- [x] First vertex-cache budget validation report: `.tmp/renderer-validation/long-term-vertex-cache-budget-r1/renderer_validation_report.md`
- [x] First static-residency budget gameplay report: `.tmp/renderer-gameplay/long-term-static-residency-budget-r1/renderer_gameplay_benchmark_report.md`
- [x] First dedicated-server disabled-BSE validation report: `.tmp/renderer-validation/long-term-dedicated-bse-r1/renderer_validation_report.md`
- [x] Promotion evidence bundle summary: `.tmp/renderer-validation/long-term-promotion-evidence-r1/promotion_evidence_summary.md`
- [x] Promotion evidence bundle with blocked visual-reference summary: `.tmp/renderer-validation/long-term-promotion-evidence-r3-visual-summary/promotion_evidence_summary.md`
- [x] Performance summary with current executor candidate failure: `.tmp/renderer-validation/long-term-performance-summary-r1/performance_summary.md`
- [x] Promotion evidence bundle with failed performance summary: `.tmp/renderer-validation/long-term-promotion-evidence-r4-performance-summary/promotion_evidence_summary.md`
- [x] Promotion evidence bundle with blocked platform summary: `.tmp/renderer-validation/long-term-promotion-evidence-r5-platform-summary/promotion_evidence_summary.md`
- [x] Candidate summary with current executor-prepare failure: `.tmp/renderer-validation/long-term-candidate-summary-r1/candidate_summary.md`
- [x] Promotion evidence bundle with failed candidate summary: `.tmp/renderer-validation/long-term-promotion-evidence-r6-candidate-summary/promotion_evidence_summary.md`
- [x] Shader source inventory report: `.tmp/renderer-validation/long-term-shader-source-inventory-r1/shader_source_inventory.md`

## Open Decisions

- [x] Should approved visual references stay in `.tmp/` as manual evidence, or should a small tracked manifest describe the reference bundle? Decision: keep binary reference screenshots external/manual under `.tmp/renderer-references/...`; use lightweight manifest and summary JSON/Markdown as the auditable review evidence, and do not track binary reference images by default.
- [x] Which capture tool and file naming convention should be standard for RenderDoc/API evidence? Decision: use RenderDoc `.rdc` files for forced core-profile modern-tier captures, API/driver trace JSON/text for ARB2 compatibility and bind-count evidence, and store each case under `.tmp/renderer-captures/<run>/<case-id>/` with `review.json` plus a run-level `capture_summary.md/json`.
- [ ] What minimum Linux GPU/driver set is required before making a Linux renderer portability claim?
- [ ] What minimum macOS hardware/OS set is required before making a macOS GL 4.1 renderer portability claim?
- [ ] Should pass-owned framebuffer invalidation ever become default-on, or stay opt-in for bandwidth-sensitive validation profiles?
- [ ] Should upload-ring defaults change after broader hardware evidence, or remain conservative?
- [x] Which modern renderer path, if any, should become the next promotion candidate? Decision: no promotable candidate is selected yet; the current measured `executor-prepare` path is disqualified by `game/airdefense1` performance evidence, and replacement candidates need dedicated performance, visual, capture, and platform evidence before selection.
- [x] Should shader sources be externalized/generated for tests while preserving executable-only runtime defaults? Decision: runtime shader sources stay internal C++ string-built; generated shader-source inventory artifacts are validation-only files under `.tmp/` and are not staged into `.install/`.
- [x] Should static upload residency remain tracked-only in map-transition and mode-transition profiles, or should vertex-cache LRU/budget purging become a validation gate? Decision: defaults remain tracked-only; `r_vertexBufferBudget 1` enables the LRU budget gate for focused residency validation.

## Completion Criteria

- [ ] Approved visual references are captured, reviewed, and compared with `--require-references`.
- [ ] RenderDoc/API evidence confirms pass contents, bind-count claims, rollback behavior, and invalidation behavior where relevant.
- [ ] Linux x64 validation artifacts exist and are reviewed.
- [ ] macOS GL 4.1 validation artifacts exist and are reviewed.
- [ ] Pass-owned graph invalidation is either implemented with capture evidence or explicitly deferred again with reasoning.
- [ ] Upload and synchronization policy is either kept unchanged with broader evidence or changed with before/after validation.
- [ ] Low-overhead state/bind claims are backed by API traces, not only renderer metrics.
- [ ] Any modern renderer promotion candidate is ARB2-or-better across the agreed scene set.
- [ ] Rollback remains available and documented after any promotion experiment.
- [ ] Documentation and release-completion notes are current.
- [ ] `git diff --check` reports no whitespace errors beyond known line-ending normalization warnings.
- [ ] Companion `openQ4-GameLibs` changes are either absent or intentionally documented.
