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
  - [x] Required MP automated benchmark capture was the remaining sign-off gap before this round.

## Near-Term Goal

- [x] Turn the current renderer hardening work into repeatable release-signoff evidence by closing the MP gameplay capture gap, strengthening harness diagnostics, and adding focused regression/profiling coverage for the recently hardened paths. MP capture, full required gameplay, focused mixed-target/upload-metric/no-zero-unbind coverage, broad safe validation, upload-stress profiling, and debug-output policy are complete.

## Multiplayer Benchmark Capture

- [x] Identify the correct active-draw trigger point for listen-server/client benchmark capture.
- [x] Confirm the trigger fires after `mp/q4dm1` map load and after the first active draw on both server and client roles.
- [x] Avoid command execution before map readiness.
- [x] Avoid command execution after harness timeout windows.
- [x] Keep the trigger mode-safe for SP and MP.
- [x] Keep the trigger deterministic under `com_maxfps 240`, `r_swapInterval 0`, and windowed launches.
- [x] Reject approaches that require per-map authored content or staged replacement assets.
- [x] Add log lines that distinguish:
  - [x] autoexec armed.
  - [x] waiting for active draw.
  - [x] autoexec executed.
  - [x] autoexec skipped with reason.
- [x] Re-run `mp-q4dm1-listen` alone until server/client benchmark summaries are captured.
- [x] Verify MP server log contains:
  - [x] selected renderer tier.
  - [x] `gfxInfo` renderer block.
  - [x] `rendererBenchmarkCapture`.
  - [x] frame pacing summary.
  - [x] screenshot path or explicit no-screenshot failure.
- [x] Verify MP client log contains:
  - [x] selected renderer tier.
  - [x] `gfxInfo` renderer block.
  - [x] `rendererBenchmarkCapture`.
  - [x] frame pacing summary.
  - [x] screenshot path or explicit no-screenshot failure.
- [x] Verify MP screenshot output exists for both expected roles when enabled.
- [x] Verify no lingering `openQ4-client_x64.exe` process remains after the run.
- [x] Re-run the full required gameplay profile after the MP-only case passes.
- [x] Update the required gameplay artifact line from partial sign-off to complete sign-off.

## Gameplay Harness Diagnostics

- [x] Improve fast-fail reporting for no-log and empty-log process exits.
- [x] Preserve the exact launch command for each failed role in the report.
- [x] Preserve exit code, timeout state, and elapsed time for each failed role.
- [x] Add a specific failure reason when expected log files are missing.
- [x] Add a specific failure reason when logs exist but do not contain active-draw, tier, benchmark, or screenshot markers.
- [x] Record the save path, dev path, base path, game dir, port, and role in report metadata.
- [x] Keep report output compact enough for release-signoff review.
- [x] Add a targeted harness self-check or dry-run check if it can be done without launching the game.
- [x] Re-run `git diff --check` after harness edits.

## Platform-Conditional Validation Semantics

- [x] Decide whether opt-in debug-output cases should remain opt-in or become conditional default checks. Decision: keep real-debug-context probes opt-in so default validation remains portable.
- [x] If conditional default checks are chosen, add explicit `skip` semantics to the validation matrix. Decision: conditional defaults were not chosen for this tranche.
- [x] Ensure skipped cases report why they skipped, not just that they did not run. Decision: no skip path is active; unsupported debug-context platforms stay green by not running opt-in cases by default.
- [x] Keep unsupported debug-context platforms green when `actualDebug=0`. Default runs do not require a granted debug context.
- [x] Keep supported debug-context platforms strict when `actualDebug=1`. Opt-in debug-output cases still require `actualDebug=1`.
- [x] Update `--list` output to show default, opt-in, and conditional cases clearly. Existing output marks opt-in cases with `[opt-in]`; no conditional cases are active.
- [x] Add the final behavior to the near-term artifact list.

## Regression Coverage For Hardened Paths

- [x] Add or extend validation coverage for target-aware texture multibind fallback.
- [x] Ensure the fallback test includes mixed `GL_TEXTURE_2D` and `GL_TEXTURE_CUBE_MAP` slots.
- [x] Ensure the fallback test does not require real driver support for `glBindTextures`.
- [x] Verify `RendererGLStateCacheSelfTest` or an equivalent matrix case reports the mixed-target fallback path.
- [x] Add or extend validation coverage for no zero-unbind buffer update/create helpers.
- [x] Verify state-cache buffer binding expectations remain stable after modern executor buffer updates.
- [x] Add or extend validation coverage for upload fence fallback counters.
- [x] Verify `waits`, `timeouts`, and `fallbacks` remain visible in low-overhead metrics.
- [x] Confirm gameplay smoke logs report `invalidBucket=0` and `overflow=0` outside synthetic invalid-index tests.

## Debug Output Metrics

- [x] Decide whether GL debug callback messages should become renderer metrics counters. Decision: keep callback messages as developer-only diagnostics for the near term, not release-signoff metric counters.
- [x] If yes, define counters for severity buckets. Decision: not applicable for the near-term policy; no severity counters were added.
  - [x] high.
  - [x] medium.
  - [x] low.
  - [x] notification, if enabled.
- [x] Keep callback handling outside unsafe callback-stack work. The callback queues messages and safe renderer boundaries flush them.
- [x] Flush queued callback messages on a safe renderer boundary.
- [x] Ensure metrics reset on renderer restart. No new metrics were added; the debug-output queue is drained during safe-boundary flush/shutdown.
- [x] Ensure `vid_restart` unregisters and re-registers callbacks cleanly.
- [x] Add validation evidence for callback registration after `vid_restart`, if feasible in this tranche.

## Upload Stall Profiling

- [x] Define a bounded upload-stress profile or command sequence.
- [x] Capture baseline upload metrics with default ring settings.
- [x] Capture stress upload metrics with reduced ring pressure, if existing cvars allow it safely.
- [x] Record:
  - [x] upload KB.
  - [x] ring usage.
  - [x] frame stalls.
  - [x] fence waits.
  - [x] fence timeouts.
  - [x] fence fallbacks.
- [x] Confirm bounded fallback policy avoids indefinite waits.
- [x] Decide whether the current upload policy is sufficient or needs another tuning pass. Decision: sufficient for near-term release-signoff evidence; deeper pressure tuning remains opt-in.
- [x] Add the chosen stress report to artifacts.

## Broader Renderer Gameplay Evidence

- [x] Re-run SP smoke after any harness or renderer-path edit.
- [x] Re-run focused renderer validation after any renderer-path edit.
- [x] Re-run broad safe renderer validation before marking near-term complete.
- [x] Re-run required gameplay profile before marking near-term complete.
- [x] Inspect gameplay screenshots for black frames or obvious present-path failures.
- [x] Inspect gameplay logs for:
  - [x] renderer warnings.
  - [x] GL debug errors.
  - [x] GPU-driven invalid buckets.
  - [x] GPU-driven indirect overflow.
  - [x] upload fence timeouts/fallbacks.
- [x] Keep all new artifacts under `.tmp/`.

## Documentation And Release Notes

- [x] Update this checklist as each near-term item is completed.
- [x] Update `docs-dev/release-completion.md` if user-visible reliability, validation, or packaging claims change.
- [x] Keep release-note language benefit-first and avoid internal-only noise.
- [x] Link final validation artifacts from this checklist.
- [x] If MP benchmark sign-off is completed, update the next-steps checklist to remove the partial-signoff wording.
- [x] If a new validation mode or harness behavior is added, document the command in this checklist.

## Completion Criteria

- [x] `mp-q4dm1-listen` automated benchmark capture passes for both server and client roles.
- [x] Required gameplay profile passes all SP and MP cases.
- [x] Focused renderer validation passes after near-term edits.
- [x] Broad safe renderer matrix passes after near-term edits.
- [x] Harness reports explain failures without requiring manual log archaeology.
- [x] Mixed-target texture fallback and upload-fence metrics have explicit validation evidence.
- [x] Release-completion notes are current.
- [x] `git diff --check` passes with no whitespace errors.
- [x] Companion `openQ4-GameLibs` status is clean unless the near-term work intentionally lands canonical game-library changes there.

## Artifacts To Fill

- [x] MP-only benchmark rerun: `.tmp/rgmp2/renderer_gameplay_benchmark_report.md`
- [x] Full required gameplay rerun: `.tmp/renderer-gameplay/ntr-required-r2/renderer_gameplay_benchmark_report.md`
- [x] Focused renderer validation rerun:
  - `.tmp/renderer-validation/near-term-regression-round2/renderer_validation_report.md`
  - `.tmp/renderer-validation/near-term-nozero-round3/renderer_validation_report.md`
- [x] Debug-output opt-in registration/restart proof:
  - `.tmp/renderer-validation/near-term-debug-output-round4/renderer_validation_report.md`
- [x] Broad safe renderer validation rerun:
  - `.tmp/renderer-validation/near-term-safe-round2/renderer_validation_report.md`
  - `.tmp/renderer-validation/near-term-safe-round3/renderer_validation_report.md`
- [x] Upload-stress profiling report or log bundle:
  - `.tmp/renderer-validation/near-term-upload-stress-round4/upload_stress_summary.md`
  - `.tmp/renderer-validation/near-term-upload-stress-round4/baseline/renderer_gameplay_benchmark_report.md`
  - `.tmp/renderer-validation/near-term-upload-stress-round4/stress-r1m-f3/renderer_gameplay_benchmark_report.md`
- [x] Harness diagnostics proof report:
  - `.tmp/renderer-gameplay/renderer-optimization-near-term-harness-dryrun/renderer_gameplay_benchmark_report.md`
  - `.tmp/renderer-gameplay/renderer-optimization-near-term-mp-rerun/renderer_gameplay_benchmark_report.md`
  - `.tmp/renderer-gameplay/renderer-optimization-near-term-mp-frame-trigger/renderer_gameplay_benchmark_report.md`

## First-Round Notes

- MP autoexec now arms at map activation but waits for an active frame before executing. SP keeps the draw-path trigger; MP also checks the live server frame and local client prediction frame.
- The MP harness keeps the listen server alive for `--mp-server-quit-delay-frames` after server capture so the loopback client can finish its own capture before server shutdown.
- Windows output directories should stay compact for benchmark evidence. A diagnostic run at `.tmp/renderer-gameplay/renderer-optimization-near-term-mp-frame-trigger/` produced expected log paths around 264 characters and no logs/screenshots; the harness now reports path-length warnings for missing expected files when paths are at or above 260 characters.
- Passing MP-only command:

```powershell
python tools\tests\renderer_gameplay_benchmark.py --profile required --cases mp-q4dm1-listen --output-dir .tmp\rgmp2
```

## Second-Round Notes

- `RendererGLStateCacheSelfTest` now forces the texture multibind fallback path during the self-test by temporarily disabling the `glBindTextures` entry point, then binds mixed `GL_TEXTURE_2D` and `GL_TEXTURE_CUBE_MAP` slots through the fallback helper. The pass line now reports `textureMultiBindFallback=` and `mixedTargetFallback=1`.
- The validation matrix now asserts the mixed-target fallback markers and keeps upload fence metric fields visible in both foundation and low-overhead coverage.
- Focused validation command:

```powershell
python tools\tests\renderer_validation_matrix.py --cases renderer-foundation-selftests,renderer-low-overhead-selftest --output-dir .tmp\renderer-validation\near-term-regression-round2
```

- Full required gameplay command:

```powershell
python tools\tests\renderer_gameplay_benchmark.py --profile required --output-dir .tmp\renderer-gameplay\ntr-required-r2
```

- Focused validation passed `2 passed, 0 failed`; broad safe validation passed `32 passed, 0 failed`; required gameplay passed `7 passed, 0 failed`.
- Required gameplay logs reduce to `invalidBucket=0`, GPU overflow `0`, cluster overflow `0/0`, and upload fence `waits=0 timeouts=0 fallbacks=0` for all SP logs and both MP server/client logs.
- Gameplay screenshot pixel checks found 16 generated/captured TGA files with nonzero dynamic range; no black-frame evidence was found.

## Third-Round Notes

- `RendererModernGLExecutorSelfTest` now runs a buffer-helper regression check that forces the bind-based `R_ModernGLExecutor_CreateBuffer` / `R_ModernGLExecutor_UpdateBuffer` path even on GL45-capable systems.
- The check verifies that create/update helpers leave the touched `GL_ARRAY_BUFFER` binding cached instead of issuing a redundant zero-unbind; the log reports `RendererModernGLExecutor buffer-helper self-test passed (noZeroUnbind=1 createHit=1 updateHit=1 updateCachedHit=1)`.
- Focused validation command:

```powershell
python tools\tests\renderer_validation_matrix.py --cases renderer-foundation-selftests --output-dir .tmp\renderer-validation\near-term-nozero-round3
```

- Broad safe validation command:

```powershell
python tools\tests\renderer_validation_matrix.py --output-dir .tmp\renderer-validation\near-term-safe-round3
```

- Focused validation passed `1 passed, 0 failed`; broad safe validation passed `32 passed, 0 failed`.

## Fourth-Round Notes

- Debug-output validation remains opt-in rather than a default conditional case. This keeps unsupported debug-context platforms green in the default matrix while supported opt-in runs stay strict on `actualDebug=1`.
- The validation matrix now supports opt-in occurrence-count checks and adds `renderer-debug-output-vid-restart`, which requires debug-output callback registration before and after `vid_restart`.
- Debug callback messages remain developer-only diagnostics in the near term. No release-signoff severity counters were added; the existing queue/flush path keeps callback work outside unsafe callback stack execution.
- Debug-output validation command:

```powershell
python tools\tests\renderer_validation_matrix.py --cases renderer-debug-output-debug-context,renderer-debug-output-vid-restart --output-dir .tmp\renderer-validation\near-term-debug-output-round4 --timeout 120
```

- Upload-stress baseline command:

```powershell
python tools\tests\renderer_gameplay_benchmark.py --profile smoke --output-dir .tmp\renderer-validation\near-term-upload-stress-round4\baseline --settle-frames 240 --sample-frames 360 --timeout 180
```

- Upload-stress reduced-ring command:

```powershell
python tools\tests\renderer_gameplay_benchmark.py --profile smoke --output-dir .tmp\renderer-validation\near-term-upload-stress-round4\stress-r1m-f3 --settle-frames 240 --sample-frames 360 --timeout 180 --set-launch-cvar r_rendererUploadMegs=1 --set-launch-cvar r_rendererUploadFrameBuffers=3
```

- Debug-output validation passed `2 passed, 0 failed`.
- Upload-stress baseline and reduced-ring smoke runs each passed `1 passed, 0 failed`.
- Baseline upload summary: `work(upload=1712KB)`, `ring=1072/16384KB overflow=0KB`, `stalls=0`, `waits=0 timeouts=0 fallbacks=0`.
- Reduced-ring upload summary: `work(upload=1056KB)`, `ring=1023/1024KB overflow=53KB`, `stalls=0`, `waits=0 timeouts=0 fallbacks=0`.
- `docs-dev/release-completion.md` now mentions the debug-output `vid_restart` proof and MP q4dm1 listen/client benchmark sign-off.

## Open Decisions

- [x] Should MP benchmark autoexec live in engine timing code, game draw timing, or the Python harness orchestration layer? Decision: game-library active-frame timing owns readiness; the Python harness only orchestrates role launch and delayed server quit.
- [x] Should validation matrix platform-conditional cases use `skip`, `xfail`, or opt-in-only behavior? Decision: opt-in-only for real-debug-context cases.
- [x] Should GL debug callback severity counters be release-signoff gates or developer-only diagnostics? Decision: developer-only diagnostics for the near term.
- [x] Should upload stress be part of default renderer validation or remain an opt-in profile? Decision: opt-in A/B profiling evidence, not a default validation gate.
