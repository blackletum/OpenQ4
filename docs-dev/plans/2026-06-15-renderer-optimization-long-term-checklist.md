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

- [ ] Define the exact long-term promotion evidence token or checklist entry that can satisfy `r_rendererPromotionEvidence`.
- [ ] Require a reviewed evidence bundle before enabling `r_rendererModernAutoPromote` in any default path.
- [ ] Include zero-warning safe renderer validation in the bundle.
- [ ] Include required SP gameplay coverage in the bundle.
- [ ] Include required MP listen/client gameplay coverage in the bundle.
- [ ] Include approved deterministic visual references in the bundle.
- [ ] Include RenderDoc/API captures in the bundle.
- [ ] Include ARB2-or-better performance comparison in the bundle.
- [ ] Include capped, uncapped, and VSync presentation coverage in the bundle.
- [ ] Include rollback command coverage in the bundle.
- [ ] Include debug-off and experimental-path-off default coverage in the bundle.
- [ ] Include Windows x64, Linux x64, and macOS GL 4.1 platform evidence before making any renderer-promotion or portability claim.
- [ ] Review the completed bundle and record the promotion/no-promotion decision in this checklist and the parent plan.

## Approved Visual References

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
- [ ] Decide whether approved references should move from `.tmp/` into a tracked lightweight reference manifest or stay external/manual.

## RenderDoc And API Capture

- [ ] Define the capture matrix before collecting traces:
  - [ ] default ARB2-compatible path.
  - [ ] explicit `r_renderer arb2` rollback path.
  - [ ] modern executor prepare-only path.
  - [ ] modern-visible opt-in path.
  - [ ] graph-invalidation armed path.
  - [ ] low-overhead GL 4.5 path.
  - [ ] GL 3.3 fallback path.
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
- [ ] Add a summary artifact under `.tmp/renderer-captures/<long-term-run>/`.

## Cross-Platform Validation

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
- [ ] Do not infer Linux or macOS readiness from Windows x64 evidence.

## Render-Graph Invalidation Backend

- [ ] Thread render-graph last-use/discard data into the pass owner or executor that can issue GL invalidation after final use.
- [ ] Keep resource-manager eligibility classification separate from GL submission.
- [ ] Emit invalidation only when:
  - [ ] the attachment has no later read/write/resolve/present use in the frame.
  - [ ] the pass owner has completed the final use.
  - [ ] the active GL tier supports the required invalidation call.
  - [ ] visual comparison and capture evidence show no regression.
- [ ] Add metrics for submitted invalidation calls by pass/resource.
- [ ] Add skip reasons for unsupported tier, later use, missing owner, missing FBO, and disabled cvar.
- [ ] Extend `rendererRenderGraphResourceSelfTest` or add a focused self-test for pass-owned invalidation submission.
- [ ] Compare default versus armed runs with GPU timers after submission exists.
- [ ] Confirm RenderDoc/API traces show expected invalidation calls.
- [ ] Decide whether `r_rendererGraphInvalidate` should remain default-off, become a validation profile, or become default-on for specific tiers.

## Upload And Synchronization Follow-Up

- [ ] Re-run upload-pressure on broader hardware, including slower GPUs and integrated GPUs.
- [ ] Add or confirm counters for:
  - [ ] fence wait count.
  - [ ] fence timeout count.
  - [ ] fallback count.
  - [ ] upload ring high-water.
  - [ ] overflow KB.
  - [ ] frame stalls.
- [ ] Evaluate coherent persistent mapping versus explicit flush on target GPUs.
- [ ] Evaluate whether `r_rendererUploadMegs` should remain unchanged after broader captures.
- [ ] Evaluate whether `r_rendererUploadFrameBuffers` should remain unchanged after broader captures.
- [ ] Add a long-run upload-pressure profile if short 3-second samples miss intermittent stalls.
- [ ] Record any upload policy change with before/after gameplay evidence and release-completion notes.

## Low-Overhead State And Bind Follow-Up

- [ ] Use API capture to compare GL call counts for GL 3.3 fallback and GL 4.5 low-overhead paths.
- [ ] Exercise draw-submission paths that produce DSA updates and multibind batches, not only prepare-only scenes.
- [ ] Confirm no-zero-unbind behavior under modern submit paths.
- [ ] Confirm texture multibind fallback remains target-aware for non-2D textures.
- [ ] Add a validation case that forces no-multibind fallback where supported by the harness.
- [ ] Decide whether any low-overhead state-cache counter becomes a release-signoff gate.
- [ ] Keep any new default state-filtering policy blocked until API traces and gameplay logs agree.

## Modern Renderer Promotion Candidate

- [ ] Define which modern path is the candidate:
  - [ ] executor prepare-only.
  - [ ] modern-visible hybrid.
  - [ ] deferred-lite.
  - [ ] forward+.
  - [ ] GPU-driven submit.
- [ ] Define candidate acceptance thresholds for:
  - [ ] frame average.
  - [ ] P50.
  - [ ] P95.
  - [ ] P99.
  - [ ] max frame time.
  - [ ] fallback counts.
  - [ ] renderer warning signatures.
  - [ ] visual diff thresholds.
  - [ ] rollback success.
- [ ] Compare candidate against current default and explicit ARB2 on the target scene set.
- [ ] Require candidate to be ARB2-or-better across the agreed scenes before any default-policy change.
- [ ] Preserve explicit `r_renderer arb2` escape behavior.
- [ ] Preserve `r_glTier legacy` behavior as a fail-closed compatibility route.
- [ ] Keep debug, validation, bindless, shader-reload, and experimental side paths off by default.
- [ ] Record the final promotion/no-promotion decision in the parent plan.

## Shader And Test Asset Maintenance

- [ ] Decide whether generated shader sources should remain C++ string-built, move to generated templates, or become externalized test/build inputs.
- [ ] Keep runtime behavior independent of loose shader-content files unless an explicit project decision changes the packaging model.
- [ ] Add shader-library tests for any externalized/generated source flow.
- [ ] Preserve GLSL 330, 410, 430, and 450 variant coverage.
- [ ] Keep shader reload/debug paths opt-in.
- [ ] Document any generated shader artifact flow in developer docs.

## Memory, Restart, And Residency

- [ ] Add or run leak-detection coverage for renderer shutdown and `vid_restart`.
- [ ] Track GPU-memory residency or live object counts for:
  - [ ] render-graph textures.
  - [ ] framebuffer objects.
  - [ ] upload buffers.
  - [ ] static vertex/index buffers.
  - [ ] sync objects.
  - [ ] shader programs.
- [ ] Validate repeated map loads without growth in renderer object counts.
- [ ] Validate SP to MP and MP to SP mode switches without renderer object leaks.
- [ ] Validate `vid_restart` after toggling renderer-tier and modern-path cvars.
- [ ] Preserve dedicated-server disabled-BSE-manager behavior unless a renderer change proves it needs adjustment.

## Automation And CI

- [ ] Decide which long-term renderer checks belong in CI versus local/manual validation.
- [ ] Keep broad safe validation runnable without stock PK4 assets where possible.
- [ ] Keep gameplay benchmark profiles asset-dependent and explicit.
- [ ] Add CI artifacts for renderer validation reports where practical.
- [ ] Add optional/manual CI jobs for Linux GL validation if runner access exists.
- [ ] Keep macOS GL validation manual until reliable runner coverage exists.
- [ ] Ensure new benchmark profiles appear in `tools/tests/renderer_gameplay_benchmark.py --list`.
- [ ] Ensure new validation cases appear in `tools/tests/renderer_validation_matrix.py --list`.

## Documentation And Release Notes

- [ ] Update this checklist as each long-term item is completed.
- [ ] Update the parent renderer optimization plan when long-term decisions change the roadmap.
- [ ] Update `docs-dev/release-completion.md` for user-visible renderer reliability, validation, performance, packaging, compatibility, input, or platform changes.
- [ ] Keep release-note language benefit-first.
- [ ] Avoid internal-only implementation detail in release notes unless it materially helps users.
- [ ] Document any new renderer rollback commands, promotion gates, or validation commands in user-facing docs when they affect testers.

## Artifacts To Fill

- [ ] Approved visual reference comparison report: `.tmp/renderer-gameplay/<long-term-visual-run>/renderer_gameplay_benchmark_report.md`
- [ ] RenderDoc/API capture summary: `.tmp/renderer-captures/<long-term-capture-run>/capture_summary.md`
- [ ] Linux x64 validation report: `.tmp/renderer-validation/<long-term-linux-run>/renderer_validation_report.md`
- [ ] macOS GL 4.1 validation report: `.tmp/renderer-validation/<long-term-macos-run>/renderer_validation_report.md`
- [ ] Cross-platform gameplay report: `.tmp/renderer-gameplay/<long-term-cross-platform-run>/renderer_gameplay_benchmark_report.md`
- [ ] Pass-owned graph-invalidation report: `.tmp/renderer-gameplay/<long-term-graph-invalidation-run>/renderer_gameplay_benchmark_report.md`
- [ ] Long-run upload-pressure report: `.tmp/renderer-gameplay/<long-term-upload-run>/renderer_gameplay_benchmark_report.md`
- [ ] Promotion evidence bundle summary: `.tmp/renderer-validation/<long-term-promotion-run>/promotion_evidence_summary.md`

## Open Decisions

- [ ] Should approved visual references stay in `.tmp/` as manual evidence, or should a small tracked manifest describe the reference bundle?
- [ ] Which capture tool and file naming convention should be standard for RenderDoc/API evidence?
- [ ] What minimum Linux GPU/driver set is required before making a Linux renderer portability claim?
- [ ] What minimum macOS hardware/OS set is required before making a macOS GL 4.1 renderer portability claim?
- [ ] Should pass-owned framebuffer invalidation ever become default-on, or stay opt-in for bandwidth-sensitive validation profiles?
- [ ] Should upload-ring defaults change after broader hardware evidence, or remain conservative?
- [ ] Which modern renderer path, if any, should become the next promotion candidate?
- [ ] Should shader sources be externalized/generated for tests while preserving executable-only runtime defaults?

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
