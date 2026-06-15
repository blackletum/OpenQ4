# Renderer Optimization Near-Term Checklist

Date: 2026-06-15

This document tracks the near-term renderer optimization work after the completed immediate phase in `docs-dev/plans/2026-06-15-renderer-optimization.md` and the audit checklist in `docs-dev/plans/2026-06-15-renderer-optimization-next-steps-checklist.md`.

## Baseline

- [x] Treat the original `Immediate` phase as complete.
- [x] Preserve the focused audit report: `.tmp/renderer-validation/renderer-immediate-plan-audit/renderer_validation_report.md`.
- [x] Preserve the broad safe-matrix report: `.tmp/renderer-validation/renderer-optimization-next-lifetime-default/renderer_validation_report.md`.
- [x] Preserve the required gameplay report as partial sign-off: `.tmp/renderer-gameplay/renderer-optimization-next-required/renderer_gameplay_benchmark_report.md`.
- [x] Carry forward the known split result:
  - [x] Renderer validation: `32 passed, 0 failed`.
  - [x] Focused immediate audit: `6 passed, 0 failed`.
  - [x] Required SP gameplay: six SP scenes passed.
  - [ ] Required MP automated benchmark capture is not signed off.

## Near-Term Goal

- [ ] Turn the current renderer hardening work into repeatable release-signoff evidence by closing the MP gameplay capture gap, strengthening harness diagnostics, and adding focused regression/profiling coverage for the recently hardened paths.

## Multiplayer Benchmark Capture

- [ ] Identify the correct active-draw trigger point for listen-server/client benchmark capture.
- [ ] Confirm the trigger fires after `mp/q4dm1` map load and after the first active draw on both server and client roles.
- [ ] Avoid command execution before map readiness.
- [ ] Avoid command execution after harness timeout windows.
- [ ] Keep the trigger mode-safe for SP and MP.
- [ ] Keep the trigger deterministic under `com_maxfps 240`, `r_swapInterval 0`, and windowed launches.
- [ ] Reject approaches that require per-map authored content or staged replacement assets.
- [ ] Add log lines that distinguish:
  - [ ] autoexec armed.
  - [ ] waiting for active draw.
  - [ ] autoexec executed.
  - [ ] autoexec skipped with reason.
- [ ] Re-run `mp-q4dm1-listen` alone until server/client benchmark summaries are captured.
- [ ] Verify MP server log contains:
  - [ ] selected renderer tier.
  - [ ] `gfxInfo` renderer block.
  - [ ] `rendererBenchmarkCapture`.
  - [ ] frame pacing summary.
  - [ ] screenshot path or explicit no-screenshot failure.
- [ ] Verify MP client log contains:
  - [ ] selected renderer tier.
  - [ ] `gfxInfo` renderer block.
  - [ ] `rendererBenchmarkCapture`.
  - [ ] frame pacing summary.
  - [ ] screenshot path or explicit no-screenshot failure.
- [ ] Verify MP screenshot output exists for both expected roles when enabled.
- [ ] Verify no lingering `openQ4-client_x64.exe` process remains after the run.
- [ ] Re-run the full required gameplay profile after the MP-only case passes.
- [ ] Update the required gameplay artifact line from partial sign-off to complete sign-off.

## Gameplay Harness Diagnostics

- [ ] Improve fast-fail reporting for no-log and empty-log process exits.
- [ ] Preserve the exact launch command for each failed role in the report.
- [ ] Preserve exit code, timeout state, and elapsed time for each failed role.
- [ ] Add a specific failure reason when expected log files are missing.
- [ ] Add a specific failure reason when logs exist but do not contain active-draw, tier, benchmark, or screenshot markers.
- [ ] Record the save path, dev path, base path, game dir, port, and role in report metadata.
- [ ] Keep report output compact enough for release-signoff review.
- [ ] Add a targeted harness self-check or dry-run check if it can be done without launching the game.
- [ ] Re-run `git diff --check` after harness edits.

## Platform-Conditional Validation Semantics

- [ ] Decide whether opt-in debug-output cases should remain opt-in or become conditional default checks.
- [ ] If conditional default checks are chosen, add explicit `skip` semantics to the validation matrix.
- [ ] Ensure skipped cases report why they skipped, not just that they did not run.
- [ ] Keep unsupported debug-context platforms green when `actualDebug=0`.
- [ ] Keep supported debug-context platforms strict when `actualDebug=1`.
- [ ] Update `--list` output to show default, opt-in, and conditional cases clearly.
- [ ] Add the final behavior to the near-term artifact list.

## Regression Coverage For Hardened Paths

- [ ] Add or extend validation coverage for target-aware texture multibind fallback.
- [ ] Ensure the fallback test includes mixed `GL_TEXTURE_2D` and `GL_TEXTURE_CUBE_MAP` slots.
- [ ] Ensure the fallback test does not require real driver support for `glBindTextures`.
- [ ] Verify `RendererGLStateCacheSelfTest` or an equivalent matrix case reports the mixed-target fallback path.
- [ ] Add or extend validation coverage for no zero-unbind buffer update/create helpers.
- [ ] Verify state-cache buffer binding expectations remain stable after modern executor buffer updates.
- [ ] Add or extend validation coverage for upload fence fallback counters.
- [ ] Verify `waits`, `timeouts`, and `fallbacks` remain visible in low-overhead metrics.
- [ ] Confirm gameplay smoke logs report `invalidBucket=0` and `overflow=0` outside synthetic invalid-index tests.

## Debug Output Metrics

- [ ] Decide whether GL debug callback messages should become renderer metrics counters.
- [ ] If yes, define counters for severity buckets:
  - [ ] high.
  - [ ] medium.
  - [ ] low.
  - [ ] notification, if enabled.
- [ ] Keep callback handling outside unsafe callback-stack work.
- [ ] Flush queued callback messages on a safe renderer boundary.
- [ ] Ensure metrics reset on renderer restart.
- [ ] Ensure `vid_restart` unregisters and re-registers callbacks cleanly.
- [ ] Add validation evidence for callback registration after `vid_restart`, if feasible in this tranche.

## Upload Stall Profiling

- [ ] Define a bounded upload-stress profile or command sequence.
- [ ] Capture baseline upload metrics with default ring settings.
- [ ] Capture stress upload metrics with reduced ring pressure, if existing cvars allow it safely.
- [ ] Record:
  - [ ] upload KB.
  - [ ] ring usage.
  - [ ] frame stalls.
  - [ ] fence waits.
  - [ ] fence timeouts.
  - [ ] fence fallbacks.
- [ ] Confirm bounded fallback policy avoids indefinite waits.
- [ ] Decide whether the current upload policy is sufficient or needs another tuning pass.
- [ ] Add the chosen stress report to artifacts.

## Broader Renderer Gameplay Evidence

- [ ] Re-run SP smoke after any harness or renderer-path edit.
- [ ] Re-run focused renderer validation after any renderer-path edit.
- [ ] Re-run broad safe renderer validation before marking near-term complete.
- [ ] Re-run required gameplay profile before marking near-term complete.
- [ ] Inspect gameplay screenshots for black frames or obvious present-path failures.
- [ ] Inspect gameplay logs for:
  - [ ] renderer warnings.
  - [ ] GL debug errors.
  - [ ] GPU-driven invalid buckets.
  - [ ] GPU-driven indirect overflow.
  - [ ] upload fence timeouts/fallbacks.
- [ ] Keep all new artifacts under `.tmp/`.

## Documentation And Release Notes

- [ ] Update this checklist as each near-term item is completed.
- [ ] Update `docs-dev/release-completion.md` if user-visible reliability, validation, or packaging claims change.
- [ ] Keep release-note language benefit-first and avoid internal-only noise.
- [ ] Link final validation artifacts from this checklist.
- [ ] If MP benchmark sign-off is completed, update the next-steps checklist to remove the partial-signoff wording.
- [ ] If a new validation mode or harness behavior is added, document the command in this checklist.

## Completion Criteria

- [ ] `mp-q4dm1-listen` automated benchmark capture passes for both server and client roles.
- [ ] Required gameplay profile passes all SP and MP cases.
- [ ] Focused renderer validation passes after near-term edits.
- [ ] Broad safe renderer matrix passes after near-term edits.
- [ ] Harness reports explain failures without requiring manual log archaeology.
- [ ] Mixed-target texture fallback and upload-fence metrics have explicit validation evidence.
- [ ] Release-completion notes are current.
- [ ] `git diff --check` passes with no whitespace errors.
- [ ] Companion `openQ4-GameLibs` status is clean unless the near-term work intentionally lands canonical game-library changes there.

## Artifacts To Fill

- [ ] MP-only benchmark rerun: `.tmp/renderer-gameplay/<near-term-mp-rerun>/renderer_gameplay_benchmark_report.md`
- [ ] Full required gameplay rerun: `.tmp/renderer-gameplay/<near-term-required-rerun>/renderer_gameplay_benchmark_report.md`
- [ ] Focused renderer validation rerun: `.tmp/renderer-validation/<near-term-focused-rerun>/renderer_validation_report.md`
- [ ] Broad safe renderer validation rerun: `.tmp/renderer-validation/<near-term-safe-rerun>/renderer_validation_report.md`
- [ ] Upload-stress profiling report or log bundle: `.tmp/renderer-validation/<near-term-upload-stress>/`
- [ ] Harness diagnostics proof report: `.tmp/renderer-gameplay/<near-term-harness-diagnostics>/`

## Open Decisions

- [ ] Should MP benchmark autoexec live in engine timing code, game draw timing, or the Python harness orchestration layer?
- [ ] Should validation matrix platform-conditional cases use `skip`, `xfail`, or opt-in-only behavior?
- [ ] Should GL debug callback severity counters be release-signoff gates or developer-only diagnostics?
- [ ] Should upload stress be part of default renderer validation or remain an opt-in profile?
