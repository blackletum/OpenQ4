# openQ4 development roadmap

Updated 21 October 2026. Stage-one implementation and tested source:
`7674acef91c2653e8c6153ae11e7b158c05e52ff`. The
[baseline evidence record](baseline-evidence.md) binds current native tests,
windowed gameplay and original-content controls to their exact source, binaries
and assets. Historical findings retain their original evidence scope; source-only
risks and remaining device/performance qualification are identified separately.

This is the single active development plan for openQ4. It consolidates the
outstanding implementation, integration, compatibility and release qualification
work across the engine, games, renderers, platforms and tools. Work should extend
the current implementation and close its remaining gaps while preserving retail
Quake 4 behavior and the supported package layout.

The [planning archive](archive/2026-10-18/README.md) preserves the previous plans,
proposals and their evidence. Historical checkboxes and instructions there do not
set current priorities or establish present support. Technical contracts, visual
specifications, requirement registers, format policies and test procedures remain
in force where this roadmap incorporates them. They provide detail and evidence;
future work and decisions are coordinated here.

## How to use this roadmap

- Keep task IDs stable. Record implementation commits, exact tested package,
  evidence and remaining limitations against an item before closing it. Split an
  item if implementation is complete but its qualification remains open.
- **P0** establishes safety, truthful scope and the evidence needed by other work.
  **P1** closes functional or qualification gaps in the intended product.
  **P2** completes broader functionality and measured quality improvements.
  **P3** is a deferred design decision or optional expansion, not a release promise.
- **Implementation** means code is missing; **integration** means relevant code
  exists outside the active source or is not connected to the production path;
  **qualification** means a scoped implementation exists but needs the stated
  evidence; **decision** means scope or compatibility policy must be settled first.
- All unchecked items remain open. A capability described as implemented below
  should not be rebuilt merely because an older plan calls it missing.
- Priorities order work within the selected delivery scope; they are not a promise
  that every P1 item ships in the next release. Select an explicit release subset
  with its dependencies, exclusions and rollback policy before implementation.
  Optional platforms/features do not block unrelated supported configurations.
- Close implementation and qualification separately. Each selected item needs an
  accountable workstream, exact source entry points, a failing/reproduction case
  where applicable, regression checks and the evidence required for its exit.
  Existing source-token checks establish contracts, not behavior or timing.
- Keep completed release material in [release completion](release-completion.md)
  and polished player notes in `releases/`. Those are release records, not a
  second development backlog. Keep the [capability matrix](engine-capability-matrix.md)
  and [platform support matrix](platform-support.md) aligned with accepted evidence.
- GitHub issues retain reports, reproductions and contributor discussion; PRs
  retain proposed changes and review. Link them to the work items here. A partial
  fix or merged PR does not close a report whose remaining symptoms are unproven,
  and a request for reporter evidence is not grounds for automatic stale closure.

## Report and contribution traceability

Reviewed 21 October 2026. These links connect the reports and contributions
to the existing workstreams; they do not create separate schedules or promise a
release. GitHub remains the source for subsequent discussion and PR/check state.

| Report or contribution | Owning work and remaining acceptance |
| --- | --- |
| [#168: disappearing menu text after preset changes](https://github.com/themuffinator/openQ4/issues/168) | UI-01/UI-07, REN-04: the earlier glyph reload fix is partial evidence; qualify current GL/Vulkan preset changes and seek confirmation of the intermittent symptom. |
| [#169: resolution scaling affects menus](https://github.com/themuffinator/openQ4/issues/169) | REN-04/UI-07: reproduce the recorded softened/cropped UI in current stock and retained routes; require native-resolution 2D output through scale changes and restart. |
| [#170: weapon effects, sound and reload animation](https://github.com/themuffinator/openQ4/issues/170) | Q4-03/AUD-01/HF-04: preserve the merged BSE sound-diversity fix; identify and qualify each remaining weapon/effect/animation symptom separately. |
| [#181: rebuilt hub2 cannot load](https://github.com/themuffinator/openQ4/issues/181) | CORE-05: the compiler allocation crash was fixed by #182; oversized runtime surfaces and silhouette warnings remain. |
| [#183: machinegun transparency](https://github.com/themuffinator/openQ4/issues/183) | Q4-01/VK-04/REN-08: reporter supplied a save and reproduced on GL/Vulkan with 0.13.2/0.14.000 and stock assets. Reproduce that save on current source and qualify matching backend views; it is no longer awaiting reporter evidence. |
| [#184: Tech Li at the hangar1 lift](https://github.com/themuffinator/openQ4/issues/184) | Q4-02: contributor traced the supplied save to Turbo shortening the lift's scripted timing. #198 prevents the fresh-sequence race; existing stuck saves need separate recovery qualification. #189 addresses a different unreachable-patient stall. |
| [#185: GOG offline installation on macOS](https://github.com/themuffinator/openQ4/issues/185) | PLAT-04/REL-01: closed after the reporter confirmed extraction with Mac Source Ports Extractor and successful startup. The guide records that scoped result; the language-persistence symptom is preserved in #199. |
| [#186: subtitles](https://github.com/themuffinator/openQ4/issues/186) | AUD-07/UI-02/UI-04: define supported dialogue text, timing, localization and accessible display coverage. |
| [#187: dialogue volume](https://github.com/themuffinator/openQ4/issues/187) | AUD-06/UI-01: independent spoken-dialogue gain with real mixer classification and persistent settings. |
| [#192: saves stop loading](https://github.com/themuffinator/openQ4/issues/192) | Q4-05/Q4-07: #193 addresses the weapon GUI event backlog with a reproduced synthetic save. Integrate the fix and qualify the reporter's failure; the report remains open. |
| [#194: shadow-mapping request](https://github.com/themuffinator/openQ4/issues/194) | REN-08/VK-04: closed as already implemented; the specific door-frame example uses authored `noSelfShadow`. Existing shadow-map qualification and documented stencil fallback remain open. |
| [#195: question marks after language switching](https://github.com/themuffinator/openQ4/issues/195) | UI-01: closed after reporter confirmation of the cached-label reload fix in `3b8534d3`; separate from language persistence across launches. |
| [#196: Vulkan video-restart crash](https://github.com/themuffinator/openQ4/issues/196) | VK-01: closed after reporter confirmation on 0.14 and a supplied log; backend-aware restart is in `b87b703b`. This does not change release availability. |
| [#199: macOS language resets on launch](https://github.com/themuffinator/openQ4/issues/199) | UI-01/PLAT-04: preserve the installation reporter's separate language-persistence defect; establish the build, startup overrides and writable configuration before choosing a fix. |
| [#173: high-DPI menu cursor PR](https://github.com/themuffinator/openQ4/pull/173) | UI-02/PLAT-05: preserve controller-to-mouse handoff and console ownership; requalify against current routing. |
| [#180: GUI save migration PR](https://github.com/themuffinator/openQ4/pull/180) | Q4-05/Q4-07/UI-09: preserve scripted values and evaluation flags through subsequent GUI updates; validate both legacy and retained presentation. |
| [#189: medic approach watchdog PR](https://github.com/themuffinator/openQ4/pull/189) | Q4-02: bounded stall recovery, SP/MP parity, restore behavior and normal heal/progression regression; does not by itself prove #184 resolved. |
| [#190: numeric clipboard harness PR](https://github.com/themuffinator/openQ4/pull/190) | CORE-04/GOV-02: mock-interface fix merged; the subsequent atlas math-include fix passes locally. Complete fresh CI without suppressing checks; neither fixture fix alone qualifies the full product. |
| [#191: parallel validation PR](https://github.com/themuffinator/openQ4/pull/191) | CORE-04/GOV-02: preserve the complete check list and failure aggregation while reducing serial CI time. Qualify together with #197, retain native regression diagnostics and allow sanitizer tests to finish. |
| [#193: weapon GUI save backlog PR](https://github.com/themuffinator/openQ4/pull/193) | Q4-05/Q4-07: keep GUI-less weapons from accumulating undeliverable events and restore existing bounded backlogs without changing the save writer. |
| [#197: queued command copy PR](https://github.com/themuffinator/openQ4/pull/197) | CORE-04/GOV-02: local native negative control reproduces lost commands 17/33 on `cbe967f2`; `4acf4f1d` preserves them. Complete integrated CI with #191; timed-out or skipped jobs remain unqualified. |
| [#198: Turbo scripted movers PR](https://github.com/themuffinator/openQ4/pull/198) | Q4-02: retain authored plain `func_mover` timing while preserving Turbo behavior for other mover classes; qualify the lift sequence and preserve the already-stuck-save limitation. |

## Current baseline to preserve

| Area | Current implementation and boundary |
| --- | --- |
| High refresh | Rendering is decoupled from exact 60 Hz scheduling. Camera, view weapon, eligible world transforms and skeletons interpolate; local multiplayer body presentation, ordinary joint modifiers, clock-phase correction and weapon GUI-light alignment are implemented. Remote actors and several bespoke visual owners remain separate work. |
| Game time | Ordinary game code advances in 16 ms steps. Network game timestamps deliberately use that same clock while scheduling uses the exact 60 Hz ladder. Do not apply a global 16-to-16.667 ms conversion as a presentation fix. |
| Vulkan | Stable supported opt-in on Windows x64 following the October NVIDIA/Intel qualification; experimental on Linux and macOS. OpenGL remains the desktop default and `best` selection. Historical failed runs remain evidence for their specific builds and conditions. |
| Modern rendering | Native GL/Vulkan PBR is default-on for authored PBR materials; stock materials stay classic. Shared classic rendering domains exist but remain default-off. Aggregate modern-visible lighting still has `MODERN_LIGHTING_PARITY_PROVEN_DOMAINS=0`; these are different promotion decisions. |
| Temporal rendering | Automatic TAAU supports subnative bilinear/bicubic scaling and dynamic resolution on eligible desktop GL/Vulkan paths; crop/nearest modes and VR are excluded. Rigid, skinned and first-person weapon motion vectors and reactive effect coverage exist. Dynamic resolution and GPU skinning remain opt-in. GLES has no temporal resolve. |
| VR | The active source implements OpenGL PC OpenXR, stereo, tracked input, room scale, crouch, two-handed aiming, haptics, comfort controls and vehicle views. Vulkan/EGL/Quest work is recoverable from archived history, not active implementation. Real-headset qualification remains open. |
| Android | Experimental ARM64/API 24+ GLES3 client, SDLActivity host, in-tree game modules, signed APK/release verification and dependency licensing are implemented. Device qualification and integration of archived onboarding/touch/lifecycle work remain open. |
| UI | Many retained screens and HUDs now run by default. The register preserves 347 rows, 310 active requirements and 37 superseded rows. UI-10 reconciled retail/base/Awakening mounts to 358 source/include-bound variants; all remain unaccepted for migration. Historical hashes and invalidated inventory acceptance are preserved. |
| Retail and Awakening | Canonical SP/MP sources and BSE are in-tree. Awakening is additive SP content under `q4xbase`, not a separate multiplayer module. Retail decl and BSE audits have already resolved extensive earlier gaps. |
| Multiplayer | Dual-stack networking, pure asset checks and bounded rcon2 exist. Actual MP defaults are `net_mpLagCompensation 0` and `net_mpPredictMode 0`; the player networking guide now agrees. `RemoteExtrapolationMS()` is still a zero placeholder because the server cannot observe the client's prediction choice. Retail 2.85 interoperability is outside the current openQ4 2.41 protocol contract. |
| Platforms | Windows x64 and Linux x64/Steam Deck are first-class platform targets; macOS arm64 and Linux ARM64 are previews; Windows ARM64 and Android are experimental. Renderer tiers are separate from platform tiers. |
| Foundations and tools | The bounded job service, worker DDS prefetch, generated-cache experiments, native dmap/AAS commands, save-format compatibility readers and experimental map workspace already exist. Their remaining scope is defined below. |

## Delivery sequence and dependencies

| Stage | Work to conduct | Exit condition |
| --- | --- | --- |
| 1. Establish the current baseline — complete | GOV-01 through GOV-03, HF-01/HF-02, Q4-01, UI-10; scope/recovery review for VR-01/VR-06 and AND-01 | Current-source ownership, clocks, compatibility inventory, tests and evidence identities agree. [Closure evidence](baseline-evidence.md) retains reproduced failures and later-stage qualification limits. |
| 2. Close product gaps | UI-01/UI-02 plus the first UI-06 editor round trip, UI-07 measurements and UI-09 toolkit work, then UI-03/UI-04 at accepted scope; Q4-02 through Q4-05/Q4-07, MP-01 through MP-08, AUD-01; selected VR-01/VR-06 and AND-01/AND-03 scope | Required player flows work through the intended UI and runtime; remaining unsupported cases are explicit and fail safely. |
| 3. Improve motion and rendering | HF-03 through HF-07, VK-01 through VK-07, REN-01 through REN-08 | Visible gameplay meets declared correctness, latency and frame-time budgets with rollback paths intact. |
| 4. Complete tools and broader quality | Remaining UI-06 scope, CORE-01 through CORE-06, remaining P2 items; TOOL-01 through TOOL-04 only when the paused map-editor work resumes | Authoring and runtime workloads meet their stated completion gates; optimizations are justified by measurements. |
| 5. Qualify packages and promote scope | PLAT-01 through PLAT-05, REL-01 through REL-04, UI-07/UI-08/UI-11, VR-02, AND-02 and selected AND-05 scope | Exact release packages pass the selected platform, device, gameplay, visual and human acceptance matrix. |

Independent workstreams may proceed together. GPU/renderer optimization depends on
the current timing and workload baseline; UI cutover depends on its complete
requirements and migration evidence; support promotion depends on physical device
and final-package evidence. Optional P3 work never silently becomes a prerequisite
for shipping an already supported configuration.

These stages apply to each delivery increment, rather than deferring all testing
to a final project-wide phase. Important dependencies are:

| Work | Required predecessor or boundary |
| --- | --- |
| Full UI migration/cutover | UI-10 refreshed inventory and resolved scope; a real SYSTEM editor round trip and RmlUi decision at `GATE-003`; shared toolkit/editor qualification at `GATE-004`; UI-11 evidence-controlled acceptance. Working screens may continue to improve before full-product acceptance. |
| Remote-player smoothing or lag-compensation promotion | HF-01 and MP-07 must define the displayed remote pose, extrapolation and server rewind together. Do not enable both corrections independently. |
| Vulkan/Quest VR integration | VR-06 fault-safe lifecycle and current renderer/module contracts; Android variants also need AND-01 packaging/verifier work. Existing GL headset qualification can proceed independently. |
| Android sustained operation | AND-02 includes AUD-03/CORE-03 aggregate memory and cancellation behavior, not only renderer FPS. |
| Modern-renderer promotion | Each REN-01 shared-domain gate is separate from REN-02 aggregate visible-lighting ownership. Keep their defaults and rollback decisions independent. |

## Governance and baseline evidence

- [x] **GOV-01 — P0, qualification: reconcile current scope and defaults.** Audit
  current source, capability/support claims and remaining historical findings.
  Classify each as reproduced, resolved, intentionally unsupported, qualification
  pending or optional. Preserve old failure records without presenting them as
  current failures. Correct stale reference/player documentation against the
  actual shipped defaults, beginning with multiplayer prediction/lag compensation
  and audio distance units; a documentation claim must not drive an unqualified
  default change. Close when every supported configuration and promoted default
  has an identifiable current evidence owner. Completed: the
  [configuration/ownership table](baseline-evidence.md#current-defaults-and-accountable-workstreams),
  capability/platform matrices, multiplayer guide and audio contract now agree
  with source; no unqualified default or support tier was promoted.
- [x] **GOV-02 — P0, implementation and qualification: recover missing regression
  coverage.** Review the imported game-history tests, beginning with
  `presentation_interpolation_contract.py`, now restored in this checkout.
  Restore/adapt relevant contracts to in-tree SP/MP and add executable clock/state
  tests where source-token checks are insufficient. Preserve licensing/provenance.
  Map the missing historical tests to current coverage first; restore behaviors
  that are untested, not obsolete filenames or duplicate token assertions. Verify
  test discovery, native suite registration and CI actually execute the checks.
  Close when validation runs those checks without either archived companion repo.
  Completed: [83 historical entries mapped](game-test-coverage.md), 74 relevant
  Python contracts/compiled fixtures recovered, production native clock and
  platform probes registered with Meson and push/PR CI. The current Windows
  Meson run passes all 184 tests; Linux portability/checksum probes pass in WSL.
- [x] **GOV-03 — P0, qualification: bind results to the actual workload.** Record
  source revision, staged binary hashes, backend/device/driver, map/entity filter,
  camera, settings, capture mode and sample interval. Keep hidden offscreen,
  visible-window, software-rendered and physical-device results distinct. Retain
  failed reports alongside reruns. Use current benchmark and support-evidence
  machinery instead of creating incompatible parallel acceptance formats.
  For timing comparisons declare warmup, sample size, run ordering, measurement
  overhead and acceptable noise/tolerance before testing; retain distributions
  and failures rather than promoting a single favorable average. Completed:
  [baseline workload/timing policy and report hashes](baseline-evidence.md)
  retain clean-source runtime identities, original media, overlays, cameras,
  capture modes and failures using the existing harness formats. The current
  visual controls make no before/after performance claim.

## High refresh presentation and input

Sources: [historical high-refresh scope](archive/2026-10-18/high-framerate-rendering-plan.md),
[temporal presentation](temporal-presentation.md), and current
`src/game/Game_local.cpp`, `src/framework/async/AsyncServer.cpp` and
`src/framework/UsercmdGen.cpp`.

- [x] **HF-01 — P0, decision and qualification: specify clock ownership.** Preserve
  exact scheduling versus the current 16 ms game/snapshot clock unless a separate
  gameplay-timing project is accepted. Specify wall, usercmd, game, prediction,
  pose, material/effect, UI, audio and capture time; cover slow motion, competitive
  pause, ordinary SP pause and Arena freeze. Test long-run timestamp agreement,
  event ordering and unchanged gameplay across presentation caps. Completed:
  [clock ownership contract](presentation-clock.md), one-million-tick production
  scheduler/SP/MP/network agreement and deterministic event/state fixtures across
  presentation caps. Full game replay and physical pacing remain HF-06/HF-07.
- [x] **HF-02 — P1, implementation: improve fractional presentation precision.**
  Replace or justify millisecond-quantized draw-phase sampling using one
  high-resolution monotonic tick-phase source and one frozen scene sample. Keep
  established integer timestamp APIs at their boundaries. Test 16/17 ms scheduling,
  non-divisor refresh rates, jitter, catch-up, missed ticks, resets and clock wrap;
  verify no accumulated phase error or authoritative state mutation.
  Include VR smooth-turn/frame deltas and distinguish the host clock from XR
  predicted display time. Completed: shared monotonic fractional SP/MP policy,
  one frozen phase per scene, unchanged integer API boundaries and high-resolution
  XR host deltas. Native/pause tests and 256 live samples each in SP and MP pass;
  see the [clock contract](presentation-clock.md) and [evidence](baseline-evidence.md).
- [ ] **HF-03 — P1, implementation: finish low-latency look presentation.** The
  optional `GetPresentationViewDelta()` sampler has no game-side consumer.
  Integrate a defined presentation-only path for mouse and applicable controller/
  gyro input, respecting sensitivity, zoom, filtering, recoil and UI ownership.
  Define displayed aim versus authoritative firing, prevent double consumption and
  keep collision/network commands authoritative. Measure input-to-display latency
  as well as smoothness; previous-to-current interpolation has a deliberate delay.
  The existing helper deliberately omits filtering; choose and test a filtered-
  versus-late-look policy. Define same-fire-frame muzzle/tracer/impact alignment
  for stock and `launchFromBarrel` weapons before enablement. Cosmetic correction
  must not alter authoritative traces, damage, projectile spawns, network events
  or RNG. Test rapid turns, zoom, movers and prediction recovery with identical
  authoritative command/event replay.
- [ ] **HF-04 — P1, implementation and qualification: close remaining visual
  ownership gaps.** Inventory muzzle-flash lights, independent weapon effects,
  unbound client models, world BSE, trails, debris, fractures, active articulated
  figures and stateful joint modifiers. Reuse completed GUI-light, bound-effect and
  projectile fixes. Implement bounded draw-only owners with spawn/age/sound events
  executed once, or record a precise release exclusion. Test moving/rotating
  carriers, attachments, terminal projectile states and restore/reconciliation.
- [ ] **HF-05 — P2, decision and implementation: alternate views and remote
  actors.** Define cinematic, private/security, third-person, spectator and nested
  camera smoothing with cut/discontinuity rules. Keep remote-player interpolation
  separate from the generic entity pass until snapshot smoothing, server rewind
  and visible aim agree. Local-body admission is already implemented and needs
  regression coverage, not reintroduction. Qualify vehicles and joint-bound cameras.
  Depend on MP-07's prediction/rewind contract before changing remote poses.
- [ ] **HF-06 — P1, qualification: preserve cross-system presentation.** Exercise
  TAA history/motion vectors, shadow caches, BSE multi-view servicing, retained UI,
  demo/AVI clocks, audio, VR and save/map resets together. Verify repeated draws and
  stereo/subviews do not rerun Think, prediction, scripts or effect events and that
  presentation buffers remain valid for their actual renderer lifetime.
  Account for intentional current bypasses: `PreparePlayerSceneForRender` skips
  demo/timedemo playback, altered tic rates prevent fractional animation, and
  noninterpolated cameras return the scene to authoritative time. Define game/
  network-demo, render-demo and timedemo behavior separately; preserve deterministic
  playback/benchmark meaning before extending smooth presentation to those paths.
- [ ] **HF-07 — P1, qualification: establish supported frame-rate behavior.** Declare
  target hardware/scenes/settings and latency/p95/p99 budgets for 240 Hz (4.17 ms
  frame interval); retain 360 Hz (2.78 ms) as experimental until qualified. Include
  30/60/120/144/165/240/360 and uncapped, VSync/VRR where available, low-FPS catch-up,
  menu/loading/background policy and preset/Steam Deck overrides. Verify physical
  motion and pacing; a cap, average FPS or hidden capture alone cannot close it.

## Vulkan rendering

Sources: [Vulkan gap history](archive/2026-10-18/plans/2026-09-20-vulkan-gap-closure.md),
[frame pacing](vulkan-frame-pacing.md), [device selection](vulkan-device-selection.md),
[authored programs](vulkan-material-programs.md), [HDR](vulkan-hdr.md), and
[point-shadow evidence](vulkan-point-shadow-parity.md).

- [ ] **VK-01 — P1, qualification: extend the supported driver/platform matrix.**
  Preserve Windows x64 NVIDIA/Intel support and qualify AMD, physical Linux GPUs
  and MoltenVK on real Apple hardware separately. Cover stock SP/MP, restarts,
  device/surface loss, resize, captures, long sessions and final-package budgets.
  Software Vulkan CI remains useful regression evidence but cannot promote a
  physical platform or change `best` by itself.
- [ ] **VK-02 — P1, investigation and qualification: visible presentation pacing.**
  Reproduce the hybrid-adapter/power-state conditions using current foreground
  windowed workloads and unchanged budgets. Separate host scheduling, fence waits,
  GPU execution and display presentation. Hidden Vulkan now renders offscreen;
  use actual presentation evidence for queue/latency claims and retain the older
  failure conditions. Close with repeated direct/hybrid-adapter results meeting
  the declared budget, or a reviewed support-scope limitation tied to retained
  reproduction evidence. Merely recording an exception does not qualify a failing
  configuration as supported.
- [ ] **VK-03 — P1, implementation: finish bounded device admission and recovery.**
  Audit fixed-size instance/extension enumeration, optional extension truncation,
  the explicit-device index limit and retry after later resource/device creation
  failure. Preserve explicit adapter selection. Exercise incomplete/error/capacity
  driver responses, automatic fallback, repeated restart and resource cleanup.
- [ ] **VK-04 — P1, qualification and targeted implementation: authored material
  parity.** Expand executable GLSL/ARB and VFS-override coverage across mixed and
  custom lighting, alpha/translucent receivers, animated models, texgen, decals,
  `_currentRender`/`_currentDepth`, offscreen views and reloads. Implement reproduced
  missing semantics; do not recreate the existing program translator/executor.
  Require visible paired GL/Vulkan results, bounded resources and safe rejection.
  Make rejection visible and intentional: current failed authored GLSL/ARB stages
  are skipped, while failed diagnostic translation of a stock pair retains its
  native path. Specify the supported subset and missing-stage/material/frame
  fallback policy; test appearance, diagnostics and recovery after reload. Existing
  per-program compile/allocation diagnostics should be retained, not recreated.
- [ ] **VK-05 — P1, implementation and qualification: internal HDR and complex
  view composition.** Extend complete-scene admission for remaining baked/material,
  portal, capture, nested and authored-post combinations. Verify radiance, alpha,
  image orientation, exposure, reset and whole-view fallback. Current production
  PBR is display-referred; internal linear HDR and HDR monitor output are separate
  capabilities. Share temporal/shadow acceptance with REN-03/REN-04 and HF-06.
- [ ] **VK-06 — P2, measured optimization: submission and resource lifetime.**
  Profile descriptor/pipeline churn, barriers, command recording, uploads and
  attachment reuse before changing them. Preserve GUI/transparency/post order,
  bounded caches and restart generations. Require matching output, resource
  plateaus and improved frame-time tails on a declared workload before promotion.
  Record decisions for indirect drawing/Hi-Z culling, parallel command recording,
  transfer/upload overlap and pipeline warm-up. Production currently uses one
  graphics queue; the timeline-semaphore bring-up test does not prove asynchronous
  production overlap. A measured decision to defer a mechanism is valid; adding
  it without a workload and correctness/benefit gate is not required modernization.
- [ ] **VK-07 — P2, qualification: capture and developer diagnostics.** Compare
  Vulkan debug views and engine captures with their GL contracts for color/depth/
  stencil interpretation, cubemap faces, nested targets, clipping/depth hacks,
  MSAA resolves and resize/restart. Preserve capability-limited line/point sizes
  and intentional asynchronous diagnostic readbacks. Require labelled frame
  identity, bounded deferred resources and no accidental current-frame wait or
  stale result presented as current. Use engine-generated captures throughout.

## Modern rendering engine

Sources: [modernization history](archive/2026-10-18/idtech5-modernization-roadmap.md),
[visible-light ownership history](archive/2026-10-18/plans/2026-08-16-modern-visible-lighting-ownership.md),
[capability matrix](engine-capability-matrix.md), [GPU skinning](gpu-skinning-modernization.md),
[PBR production evidence](archive/2026-10-18/plans/2026-10-04-pbr-production-readiness.md) and
[shadow production audit](shadowmapping-final-audit-2026-09-05.md).

- [ ] **REN-01 — P1, qualification: promote shared classic domains individually.**
  Finish fresh-package GL/Vulkan acceptance for root GUI, in-world GUI, ambient,
  interaction, fog/blend, deform, subview, cinematic/post and special-frame owners.
  Cover stock fixed cameras, mapped/stencil/hybrid lights, dynamic/cutout casters,
  direct/dynamic mirrors, remote/x-ray/refraction views, nested color/depth/cubemap
  targets, `_currentDepth`, root post and render-demo/Raven effects. Require
  nonzero executed ownership, same-backend classic parity, visible effect deltas,
  complete zero-commit rollback and human/platform evidence for each domain.
- [ ] **REN-02 — P2, implementation and qualification: finish aggregate modern
  lit-frame ownership.** Inventory the remaining material/light semantics against
  implemented shared corridors; finish coherent whole-frame lighting and
  composition before changing parity masks, auto-promotion or core-profile defaults.
  Require meaningful visible lighting, complete stock fallback and measured CPU/GPU
  benefit. Diagnostic readiness is not proof that the new executor owns gameplay.
- [ ] **REN-03 — P1, qualification and implementation: shadow production scope.**
  Qualify point/projected/CSM, stencil/map hybrids, moments, movers, near emitters,
  dynamic/cutout/custom receivers and constrained budgets through full scenes and
  continuous motion. Audit point static/live composition and mapped dynamic-image/
  non-explicit-texcoord alpha cases; retain stencil fallback when coverage cannot
  be reproduced. Translucent moments already exist on Vulkan. Prove cache freshness,
  bounded memory, no missing/doubled receivers and retained light on fallback.
- [ ] **REN-04 — P1, qualification: temporal AA and dynamic resolution.** Exercise
  existing rigid/skinned/weapon exact vectors and reactive surfaces through
  interpolation, disocclusion, pause, cuts, failed presents, capture, multiple
  views and retained UI. Test delayed/stale GPU timing and resolution recovery.
  Require stable quality, separate native UI, valid previous-frame identity and
  immediate current-frame/SMAA fallback. Per-eye VR temporal work remains VR-05.
- [ ] **REN-05 — P1, qualification: remaining PBR quality and portability.**
  Reproduce strict residual Intel SSAO and lavapipe scalar/texture differences and
  recorded cutout/normal diagnostic mismatches; fix or explicitly bound exceptions.
  Qualify authored mixed-material mods, skin substitutions on packed MD5R, physical
  Linux and Apple fallback. Preserve stock appearance. Box probes, probe capture,
  animated/MD5R PBR and indirect-only SSAO are implemented and need regression
  evidence rather than another implementation phase.
- [ ] **REN-06 — P2, qualification: probes, baked data and advanced effects.**
  Qualify probe capture orientation, generated-grid provenance and whether existing
  baked grids need regeneration after capture fixes. Broaden authored probe/decal
  scenes and resource captures. Validate default-off froxel, SSR and SSGI leaves
  individually and together with exact master-off rollback and bounded work.
  Keep their screen-space approximation limits explicit; classic decal routing
  does not require an invented Vulkan clustered-decal port.
  Qualify actual probe-source admission: the Vulkan loader requires immutable,
  nondefault cubemaps within the current 256-face-size ceiling, with supported
  authored RGBA8/generated RGBA16F storage. Test incomplete sources, storage/upload
  generations, reload and atlas exhaustion. Measure the synchronous upload-flush/
  readback path on source-cache misses and set cold/reload latency and memory
  budgets. Decide explicit support or exclusion for compressed, authored-HDR and
  oversized inputs; an HDR generator does not establish a general HDR loader.
- [ ] **REN-07 — P2, measured optimization: remove demonstrated CPU/GPU stalls.**
  Audit hot copy-helper GL state queries, cluster allocation/binning, per-frame
  pose work, shadow invalidation, render-target generations and upload lifetimes.
  Reuse existing job/measurement infrastructure. Shadow uniform caching already
  exists. Require at least five matched performance runs, unchanged quality/order,
  no p99 regression beyond the predeclared measurement tolerance in GOV-03 and
  bounded resources through at least 100 resize/scale cycles.
- [ ] **REN-08 — P1, qualification and targeted implementation: complete post-
  processing combinations.** Qualify motion blur independently of TAA: it has its
  own history, limited rigid-opaque object vectors and subview/time-gap resets;
  existing gameplay benchmarks disable it. Cover skinned/weapon motion, mirrors/
  portals/remote views, frame caps, pause/slow frames and TAA/HDR/bloom/SSAO/scaling
  combinations on GL/Vulkan. Require no unrelated-subview history flicker, coherent
  effect strength and accurate requested/effective settings. Fix reproduced gaps
  or publish precise supported combinations, including Apple GL 2.1 offscreen/MSAA
  restrictions and Vulkan's effective 8x MSAA ceiling. Preserve unchanged gameplay.

## Virtual reality

Sources: [OpenXR implementation history](archive/2026-10-18/plans/2026-10-02-openxr-vr.md),
[player guide](../user/vr.md) and current
`src/sys/openxr/OpenXRSystem.cpp`.

- [ ] **VR-01 — P1, integration: review recoverable Vulkan/EGL/Quest changes.**
  Review `archive/worktree-wtq-quest-vr`, especially `0d83c14c4` (Vulkan),
  `f98a1d247` (headset rendering without desktop presentation), `4b5e0c4f9`
  (Wayland/EGL), `65aa9bd88` (Quest) and `9dcd121da` (world-GUI pointing).
  Reconcile selected changes with current renderer APIs, in-tree game code, UI and
  Android packaging rather than replaying the branch wholesale. Gate integration
  on GL/Vulkan test-runtime SP/MP/menu coverage, runtime GPU/device ownership,
  restart/session-loss recovery, validation-clean Vulkan and VR-off parity. Require
  continued headset submission when desktop presentation is unavailable; qualify
  native-Wayland Vulkan/EGL and the appropriate GLX fallback independently.
  Carry VR-06's lifecycle guarantees into each recovered backend/host change.
  Stage-one scope/recovery review is complete in the
  [XR/Android recovery review](platform-recovery-review.md); integration remains open.
- [ ] **VR-02 — P1, qualification: real headset gameplay and comfort.** Qualify
  available PC runtimes/headsets for refresh pacing, tracking latency, aim/grip,
  handedness, two-hand weapons, haptics, room-scale collision/MP prediction, crouch,
  seated vehicles, zoom, menus/HUD/world GUI and loss/reconnect. Exercise full combat
  and camera transitions. Synthetic stereo/test-runtime success cannot establish
  physical tracking, readable UI or comfort.
- [ ] **VR-03 — P2, implementation: standing space and floor calibration.** Define
  LOCAL/STAGE/floor behavior, recentering, seated/standing transitions and safe
  fallback when a runtime lacks stage space. Validate crouch/body alignment and
  room-scale bounds without double-applying physical movement.
- [ ] **VR-04 — P2, measured optimization: stereo frame cost.** Profile two-eye
  front-end work, shadows, particles, UI and loading on actual hardware. Share
  view-independent work while preserving eye-specific culling/pose/output and
  exactly-once effect servicing. Establish headset-specific CPU/GPU tail budgets.
- [ ] **VR-05 — P3, decision and implementation: per-eye temporal features and
  further interaction.** TAA/dynamic resolution require independent eye histories,
  motion vectors, resets and scale control before enablement. Hand presence and
  physical-object interaction require separate gameplay/comfort designs. Quest
  distribution and device qualification also depend on AND-01 through AND-05.
- [ ] **VR-06 — P1, implementation and qualification: fault-safe XR ownership.**
  Reproduce the current `Check` → `DestroySession` → `ReleaseImages` teardown
  re-entry risk in `OpenXRSystem.cpp`: session loss can be reported before
  `frameOpen`/acquired flags are cleared. Make destruction idempotent and stop
  frame/input work immediately when its owning session is invalidated. Extend the
  existing test runtime with injected session/instance loss, partial acquisition,
  wait/release failures, malformed image counts/indices, failed swapchain
  creation/resize and tracking invalidation. Require bounded cleanup, no recursion
  or stale-handle calls, balanced image/frame ownership and a recoverable desktop
  path. Ordinary call-order tests and headset happy paths do not close this item.
  Stage-one review confirmed the source-derived risk and required fault matrix
  in the [recovery review](platform-recovery-review.md); runtime reproduction and
  lifecycle repair remain open.

## Android and mobile rendering

Sources: [Android build and packaging](android-build.md),
[port integration and credits](android-gles-integration.md),
[standalone host](../../mobile/android/README.md).

- [ ] **AND-01 — P1, integration: recover onboarding, controls and lifecycle work.**
  Review archived Android changes for touch controls (`486ecf735`), diagnostics/
  lifecycle (`3ce244cda`), background tic sleep (`819846f5d`), cutouts (`20766e8d1`),
  launcher/localized data preflight (`ba6d3af2e`, `c5845ca2d`, `2f1d2d681`) and
  emulator support (`023f0eeee`, `1484195c4`). Preserve newer release signing,
  verification, notices, existing usable assets during failed extraction and the
  retained UI. Accept each integrated feature independently with recovery tests.
  Select an explicit ABI/API/device/flavor matrix before integration. Current
  Meson/Gradle admit ARM64 and `verify_android_apk.py` expects an exact library and
  asset set; emulator/Quest additions need coordinated metadata, staging and
  variant-specific allowlists. Preserve matching SDL/native versions, 16 KiB
  alignment, certificate identity and licensing. Reject test-runtime/debug payloads
  from published APKs; toolchain/API bumps require their own qualification.
  Stage-one archived-change review and explicit phone/emulator/Quest scope matrix
  are complete in the [recovery review](platform-recovery-review.md). Feature
  integration, flavor verification and physical-device qualification remain open.
- [ ] **AND-02 — P1, qualification: real Adreno/Mali gameplay.** Cover the API floor
  and current Android, ES3 capability differences, stock SP/MP, saves, map/campaign
  transitions, materials/shadows/ETC2/EAC, cold/warm caches, audio and touch/gamepad/
  mouse/keyboard. Exercise focus, suspend, low memory, process death, rotation,
  density and cutouts. Retain thermal, memory, storage and sustained frame budgets.
  Include compressed input, pending decode, retained CPU PCM and uploaded OpenAL
  residency together. Qualify Android's own OpenAL provider/capabilities rather
  than inheriting desktop provider assumptions; depend on AUD-03/CORE-03 limits.
- [ ] **AND-03 — P1, implementation and qualification: player setup and upgrades.**
  Finish scoped-storage onboarding, retail-pak validation, localized insufficient
  space/extraction/retry messages, accessible logs and save backup/export. Validate
  interrupted onboarding, APK upgrades and signing-certificate continuity. Keep
  proprietary retail data outside the APK. Optional SigmaTouch/host dependencies
  require a compatible distribution licence, not merely an available licence.
  Repair missing/truncated/mismatched extracted files behind an existing `.ready`
  marker: `OpenQ4Activity.extractPackage()` currently trusts that marker and has
  no repair flow. Use a validated readiness record or an engine-validation failure
  handoff, with bounded repair/retry and low-space/interruption coverage; retain
  full engine PK4 checks and usable saves/configuration. Make diagnostics and
  save export reachable in a signed, non-debuggable release with private storage.
- [ ] **AND-04 — P2, implementation and qualification: mobile UI and defaults.**
  Qualify touch targets, keyboard entry, safe areas, HUD/menu scaling, bindings and
  overlay configuration. Select mobile presets from device results; explicitly
  handle GLES feature limits rather than assuming desktop temporal/post parity.
- [ ] **AND-05 — P2, integration and qualification: standalone Quest.** After
  VR-01/AND-01, qualify real installation, assets, stereo gameplay, controller/UI
  interaction, comfort, session recovery and sustained refresh/thermal budgets.
  Decide sideload/store packaging policy. Keep emulator/test-runtime results
  separate from headset support and x86_64 emulator builds separate from publication.
  Require AND-01's flavor-aware package verification and VR-06 lifecycle tests.

## New user interface

The [visual specification](ui-visual-design.md),
[product register](ui/product-requirements.md),
[machine-readable requirements](ui/product-requirements.json) and
[migration manifest](ui/migration-manifest.json) retain the full product contract.
This roadmap adopts all **310 active requirements, 358 current source/include-bound
migration variants and seven final gates**. UI-10 reconciled retail/base/Awakening
mounts while preserving the historical 271-entry manifest. The denominator follows
effective source changes; it is not a fixed completion target. Scope contradictions
remain explicitly paired with UI-05 and cannot grant migration acceptance.
Archiving their original plans does not remove requirements,
renumber IDs or accept a GUI. Stable `product:`/`original:` references retain their
archived provenance; new scheduling belongs here.

- [ ] **UI-01 — P1, implementation: complete production settings and essential
  pages.** Finish GAME, CONTROLS, AUDIO, Load Defaults and Player Setup; complete
  preset and mixed-effect transactions before SYSTEM leaves its current opt-in
  scope. Demonstrate real effect/readback, persistence, cancel/revert, failed Apply,
  interrupted writes, restart, device/capability changes and stale-owner rejection
  in SP/MP. Localize every display string and preserve current working pages.
- [ ] **UI-02 — P1, implementation and qualification: text, input and accessibility.**
  Finish in-field IME/composition, shaping/kerning/fallback/styles, grapheme editing,
  caret/candidate placement, binding capture, accessibility exposure and contextual
  prompts. Qualify shipped languages, long labels/breadcrumbs, portrait/ultrawide,
  text scale/density, keyboard/controller/touch and hotplug. Keep physical input
  acceptance distinct from handler/ownership unit tests.
  Connect `NativeInputDriver`/`WindowsTextSession` to production translators,
  bootstrap, activation and disposal; they currently have tests but no normal
  construction path. Require native-message/SDL routing, focus/loss/recreation,
  held-source handling and a working fallback before claiming live IME support.
- [ ] **UI-03 — P1, implementation: complete player and multiplayer workflows.**
  Finish tourney queue/arena/watch controls, model previews, full Match Control,
  lists beyond sixteen players/48 maps, stable refresh focus, contextual disabled
  reasons and role-specific vote operations. Bind mute/friend state to the right
  occupant across slot reuse. Complete save sorting/filter/refresh, chapter and
  objective/play-time details, Strogg confirmations, loading handoffs, remote-load
  arsenal and MVD rosters. Qualify real task completion and failure/cancellation.
- [ ] **UI-04 — P1, implementation and qualification: full GUI corpus migration.**
  Finish all HUD, wrist/comms, scope, vehicle, cinematic, terminal/world and scripted
  surfaces with source-bound retained lowering. Preserve legacy grouping/remainder
  and established conditional-alpha behavior; finish include, script and timeline
  provenance instead of reopening resolved native alpha semantics. Preserve
  per-entity instances, projected density
  and input, saves/checkpoints and exactly-once commands. Require accepted behavior
  and artwork for every resource; conversion counts do not close migration.
- [ ] **UI-05 — P1, decision and qualification: resolve visual policy conflicts.**
  Reconcile SP/MP hit-marker defaults with visual specification section 14.7 and
  other recorded deviations before acceptance. Preserve editable vector framing,
  controls, symbols, 45-degree geometry and continuous motion; use bitmaps only for
  approved complex imagery. Keep stock references without adding proprietary art.
  Resolve `FLOW-054`/`WID-034` clauses still requiring Awakening multiplayer through
  a register revision preserving their history and applicable SP/ordinary-MP
  obligations. Do not build expansion MP to satisfy stale acceptance clauses.
- [ ] **UI-06 — P1, implementation: finish the visual authoring editor.** Build on
  DocumentEdit/history/prepared-view foundations: shell, hierarchy, property/layout/
  anchor tools, reusable components, pen/path/gradient/mask editing, timelines/state
  graphs/bindings, localization/assets, undo, conflicts and recovery. Use the same
  runtime renderer. Gate on editing a production screen and component/transition,
  undo/redo, interrupted save/recovery, reopen/package and SP/MP execution across
  multiple documents/viewports.
  Deliver the first real SYSTEM source round trip alongside UI-01, as required by
  `GATE-003`; judge the RmlUi architecture with that result and optimized cost.
  Complete the shared editor/toolkit exit at `GATE-004` before bulk corpus cutover.
- [ ] **UI-07 — P1, measured optimization and qualification: full UI performance.**
  Close retained title/pause parity and remaining fractional motion, cold compile,
  long-document, vertex-copy, mask/composition and GPU/memory costs. Ratify per-UI
  CPU/GPU budgets against HF-07 and VR-04; the inherited proposal is p95 at most
  10% and p99 at most 20% of the target frame interval per budget. Use optimized
  builds, at least 1,000 warm frames, cold/restart/resize results and 1,000-cycle
  bounded-resource checks. Keep whole-frame and isolated UI measurements separate.
  Begin with the SYSTEM/editor vertical slice; do not defer architecture/cost
  decisions until all screens have been converted.
- [ ] **UI-08 — P1, qualification and decision: final cutover.** Complete all seven
  final gates with current source-bound behavior, visual/editor/performance and
  Windows/Linux/macOS/Android-GLES evidence, including dedicated packaging. Decide
  external-mod legacy loading policy before removing temporary stock-product
  routes. Update acceptance registers only for the exact scope actually proven.
  UI-10 and UI-11 are prerequisites; currently passing checkpoint validators cannot
  record product completion.
- [ ] **UI-09 — P1, implementation and qualification: production toolkit and
  engine integration.** Close the adopted RUN/DOC/WID/RES/SUR/SAVE requirements,
  not just individual pages: general controls/components, public GUI adapter
  methods, material/movie/model/generated-image operations, world-surface output
  and input, gameplay timers, independent instances and save/restart continuity.
  Current code has six canonical control roles, four node kinds, an unimplemented
  generated-texture callback, partial material-image operations, empty binding-name
  updates/text-wrap queries and an explicit world-output rejection. Map each
  limitation to required semantics and extend existing working facilities rather
  than rebuilding them. Test actual SP/MP engine callers, simultaneous HUD/menu/
  world/editor contexts, failed recreation, timer/presentation-clock separation,
  restored state and exactly-once actions before bulk UI-03/UI-04 acceptance.
- [x] **UI-10 — P0, investigation and implementation: refresh effective scope and
  source inventory.** Completed [retail/base/Awakening inventory](ui/inventory-report.md):
  264/276/324 effective paths, 358 distinct source/include-bound variants, 18
  changed loose overlays and five added base paths. Historical hashes/evidence
  remain; stale inventory acceptance was invalidated and no GUI was accepted.
  The governed inventory-revision path checks added/removed/changed sources,
  include closure and exact denominator while retaining existing validator
  restrictions. Positive and negative controls pass. UI-05 owns explicit artwork/
  exception contradictions; UI-11 still owns product acceptance tooling.
- [ ] **UI-11 — P1, implementation: evidence-controlled acceptance tooling.**
  `validate_product_requirements.py` deliberately freezes migration records and
  forbids new verified requirements/gates in checkpoint, revision and archive
  modes. Add a separately reviewed acceptance transition for exact requirements
  and resources, with current source/evidence bindings, complete dependencies and
  per-resource behavior/artwork proof. Retain existing modes' restrictions. Test
  stale evidence, partial conjuncts, changed sources, missing dependencies and
  unqualified resources as failures; a broad success flag must not accept a corpus.
  Milestone gates may accept their explicitly bounded exit scope while broader
  shared requirements remain partial, as specified by `GATE-003`/`GATE-004`.
  Final product acceptance still requires complete applicable requirements; check
  dependency evidence against the particular milestone being accepted.

Detailed current evidence: [menu audit](ui/menu-audit.md),
[retained screens](ui/retained-screens.md), [multiplayer menus](ui/multiplayer-menus.md),
[runtime performance](ui/runtime-performance.md), [document editing](ui/document-editing.md),
and [settings transactions](ui/settings-effect-execution.md).

## Retail Quake 4 compatibility and campaigns

Sources: [retail decl audit](retail-q4-decl-parity-audit.md),
[BSE retail audit](archive/2026-10-18/quake4-bse-research-and-implementation-plan.md#retail-parity-audit-2026-10-02),
[Awakening history](archive/2026-10-18/plans/q4x-awakening.md), [save reliability](savegame-reliability.md),
[save compatibility policy](savegame-compatibility-policy.md),
[retail asset baseline](stock-asset-baseline.md).

- [x] **Q4-01 — P0, investigation and qualification: current retail gap ledger.**
  Inventory and reproduce compatibility reports against current binaries and
  installed base PK4s. Distinguish engine defects, absent media, authored-content
  issues and deliberate supported differences. Recheck historical Network2/Recomp/
  Storage1/Q4DM9 lighting findings before calling them current. Keep resolved decl
  and BSE reconstruction work closed. Audit remaining overrides file-by-file and
  replace bootstrap workarounds with engine/game fixes where possible. Separate
  compatibility workarounds from intentional new UI/campaign content; retain the
  original-asset-only baseline and record each overlay used by a qualification.
  Completed: [current ledger and 165-path overlay audit](retail-baseline-ledger.md),
  both-API four-map controls plus lit Recomp, original-content SP/MP gameplay,
  and retained complete-baseline failures with source/package identities. The
  connecting pure-client timeout belongs to MP-08/Q4-02; unsynchronized lighting
  differences and reports awaiting precise reproductions remain explicitly open.
- [ ] **Q4-02 — P1, qualification: complete stock campaign and mode transitions.**
  Cover progression, scripted triggers, set pieces, AI, vehicles, bosses,
  cinematics/lip-sync, world terminals, checkpoints and saves through full campaigns
  on supported render paths. Test SP/MP and campaign switches with original assets;
  classify expected warnings individually. Map initialization or a single capture
  is insufficient evidence of playable progression.
- [ ] **Q4-03 — P1, investigation and qualification: remaining BSE/effect parity.**
  Resolve initial-light current-versus-spawn transform and rotated model-particle
  normal/tangent questions from the October audit. Qualify light/sound/decal,
  trails, child effects, collision, stops/deletes, owner changes, subviews,
  high-refresh and demo/save lifecycle. Preserve intentional safety and
  presentation differences; BSE stays in-tree and linked into the client.
- [ ] **Q4-04 — P1, qualification: Awakening campaign closure.** Play all thirteen
  maps and restore saves during unique weapon/status/vehicle/boss states; qualify
  freeze persistence, damage gates, animation speed, and turret effects. Recheck
  transient corpse pin loss on restore as a bounded limitation. Verify base/Arena/
  MP isolation after restart. Keep the completed classes, gameplay and turret work;
  no expansion MP module or resurrection of obsolete companion-layer architecture.
- [ ] **Q4-05 — P1, decision and implementation: save failure recovery.** Preserve
  approved v2/v3/v4 readers and current corruption checks. Design safe recovery for
  object-restore failure after map teardown; evaluate staged-world rollback versus
  explicit recovery behavior. For unrecognized unstamped/build-1 payloads, assess
  restart at saved level with header loadout rather than guessing the payload;
  recognized build-1 releases already have compatibility readers. Require
  interruption/corruption fixtures and no destruction of valid existing saves.
- [ ] **Q4-06 — P3, decision: portable saves.** `raw1` native aggregates are not
  cross-platform saves. Inventory and normalize game plus GUI/interpolator raw
  blocks before introducing a versioned portable format and ABI/architecture
  fixture matrix. Keep existing decoders and explicit unsupported-format handling.
  Retail proprietary game-DLL/save interchangeability is not a project target.
- [ ] **Q4-07 — P1, qualification: genuine historical save corpus.** Complete the
  historic-binary corpus for 0.6.95, 0.8, 0.10, 0.11 and 0.13 where current evidence
  uses equivalent/restamped fixtures. Qualify the exact HiDef #123 fixture and
  other target ABIs, identifying source executable, architecture and save layout.
  Keep synthetic corruption/layout tests separately labelled; they cannot prove
  compatibility with saves generated by the original binaries.
- [ ] **Q4-08 — P2, investigation and qualification: save cost and durability.**
  Measure large-save CRC/sync time and fault behavior across target filesystems.
  Directory metadata is not synced and full power-loss atomicity is not established.
  Define any stronger durability guarantee before implementing it; qualify partial
  writes, replace failures, process interruption and preservation of the last valid
  save independently from object-restore recovery in Q4-05.

## Multiplayer bots Arena and demos

Sources: [competitive closure audit](competitive-match-reference-audit.md),
[bot implementation](mp-bots.md), [Arena ceremony](arena-campaign-ceremony.md),
[demo architecture](multiview-demos.md), [MVD qualification](mvd-qualification-2026-09-07.md),
[liquid implementation history](archive/2026-10-18/plans/2026-08-07-liquid-support.md).

- [ ] **MP-01 — P1, qualification: competitive framework release closure.** Exercise
  roster invitations/substitution/bench/disconnect, replacement identity and slot
  reuse, ballots/stale operations, pool/veto/load-failure rollback, countdown/ready/
  round edges and duplicate timeout charging. Qualify role-filtered item timers,
  invalidated follow targets, journal/MVD interruption/capacity and all live roles'
  localized keyboard/controller workflows. Keep captain spectator invitations
  fail-closed and record remaining exact runtime scenarios from the current audit.
- [ ] **MP-02 — P1, qualification and targeted fixes: bot traversal safety.** Rerun
  staged q4dm8/q4ctf3/q4ctf5/q4dz3 after the exact-state jump/unroutable-pad fixes,
  then the complete 32-map sweep including warmup. Separate enemy impulses from
  navigation/control failures and retain strict world-death gates. Qualify route
  generation/think budgets, objectives and real combat; source tests do not close
  the previously failing runtime safety sweep.
- [ ] **MP-03 — P2, implementation and qualification: broader bot navigation.**
  Audit custom-map ladders/lifts, shallow trenches, route-cost approximations,
  pad/player collisions, graph limits, optional caching and debug growth. Extend
  only demonstrated gaps; bots and personality/chat/roster systems already exist.
- [ ] **MP-04 — P1, implementation and qualification: Arena completion.** Resolve
  world-pause versus continuing sound, restart/map-navigation cost and physical
  pause navigation. Capture victory/defeat/draw ledgers, progression and ceremony
  through save/reload and failure paths; an aborted-launch ledger is not completion
  evidence. Coordinate with UI-03, HF-01 and AUD-02.
- [ ] **MP-05 — P1, investigation and implementation: demo reliability and
  operator workflow.** Reproduce the recorded Red Rover seek/debris distortion
  using the original recording. Define retention/quota/download/partial recovery
  and structured markers; complete named/previous follow, autotracking, independent
  keyframes and desired reverse-scrub/timedemo/AVI/thumbnail scope. Preserve approved
  formats and explicit retail-stream rejection. Test partial writes, repeated
  seeking, pause/restart/round changes and transient geometry; reuse competition's
  existing auto-record lifecycle rather than duplicating it.
- [ ] **MP-06 — P2, implementation and qualification: liquid extensions.** Verify
  splash/bubble/wade effects, underwater view/muffle, projectiles, buoyancy and bot
  behavior. The current AAS compiler already emits water/swim/waterjump data;
  validate its end-to-end consumers before proposing new generation. AI swimming,
  authored swim animation, moving volumes and air-HUD replication need explicit
  content/gameplay scope. Preserve mapper guidance against duplicate hurt damage.
- [ ] **MP-07 — P1, implementation and qualification: coupled prediction and
  rewind.** In `src/mpgame/gamesys/SysCvar.cpp` both enhanced prediction and lag
  compensation default off; `src/mpgame/Game_local.cpp` bounds rewind using RTT
  and reported prediction, but `RemoteExtrapolationMS()` still returns zero. Define
  server-visible/negotiated extrapolation, displayed pose age and rewind together
  with HF-01/HF-05; bound untrusted client claims and prevent double correction.
  Keep promotion separate from implementation. Qualify actual remote peers under
  delay, jitter, loss, reordering and duplicate commands, including teleport,
  respawn, slot reuse, pause and spectator changes. Cover hitscan, lightning and
  melee plus same-fire-frame presentation; restore present-world state before
  damage/effects and preserve authoritative replay. Extend existing lag-compensation
  contracts with executable state/peer tests and correct the networking guide.
- [ ] **MP-08 — P1, qualification and decision: network and content interoperability.**
  Qualify real cross-platform/architecture peers over IPv4 and IPv6, fragmentation/
  MTU/rate limits, congestion, high player counts, discovery and reconnect. Address
  or explicitly document single-address DNS selection: the current resolvers
  prefer an IPv4 result and do not provide a bounded multi-address/family failover
  sequence. Exercise ordered pure assets, content-only mods and explicit trusted
  local code-module admission. Keep the same-platform pure compatibility token
  distinct from cryptographic module verification or anti-cheat; standard builds
  do not enable direct asset downloading. Document clear rejection of unsupported
  protocols: retail 2.85 interoperability is a separate future scope decision,
  not a version-number change to openQ4 2.41 or a prerequisite for retail assets.

## Audio and voice

Sources: [audio implementation and remaining limits](audio-modernization.md),
[retail reverb](retail-audio-reverb.md), [networking guide](../user/multiplayer-networking.md).

- [ ] **AUD-01 — P1, investigation and qualification: retail distance semantics.**
  Audit listener/source radius and authored range units against retail audible
  fixtures and distance curves before changing attenuation defaults. Both
  `DOOM_TO_METERS` and `METERS_TO_DOOM` in `src/sound/sound.h` are currently 1.0;
  the older audio report's unit-mismatch explanation is not an established current
  defect. Reconcile that report with measured source/listener behavior rather than
  applying a speculative 39.37 conversion. Preserve the authored mix and already
  implemented spatial/HRTF/EFX/reverb behavior.
- [ ] **AUD-02 — P1, qualification: physical device and timing lifecycle.** Test
  default/selected endpoint loss and return, hotplug, pause/resume/slow motion,
  cinematics, map/restart and long sessions on target OSes. Verify audible output,
  not just backend initialization; include Arena's unpaused sound-world gap.
- [ ] **AUD-03 — P2, implementation and qualification: bounded decode and compressed
  streaming.** Current decode admission bounds job count, while each job can
  allocate a complete PCM sample. Measure compressed input, pending decoded PCM,
  retained CPU data and uploaded OpenAL buffers together; set byte budgets and
  backpressure where needed, including Android low-memory/map-cancel cases.
  Add bounded streaming for music/VO with correct leadin/loop, seek/save restore,
  pause/time scale and underrun behavior. Existing queued PCM and decode jobs do
  not by themselves establish compressed streaming. Android's required memory
  bound can ship before optional streaming completion; keep those exits separate.
- [ ] **AUD-04 — P2, implementation and qualification: surround and mod reverb.**
  Resolve `SSF_CENTER`/VO placement on 5.1 with stereo/HRTF fallback and nonzero mod
  room-rolloff unit handling. Keep stock zero-rolloff behavior intact.
- [ ] **AUD-05 — P1, implementation: functional multiplayer voice.** Capture,
  codec, transport and playback remain disabled/incomplete despite UI controls.
  Implement the full bounded path, consent/activation controls, mute identity,
  moderation/rate limits, reconnect and device recovery, then qualify real peers.
  Preserve existing recipient policy; decide bounded latency, late-packet dropping
  and backpressure before connecting voice to the current reliable client-message
  path, whose head-of-line blocking needs measurement under loss/congestion.
  Until then expose honest unavailable controls and do not advertise working voice.
- [ ] **AUD-06 — P2, implementation and qualification: independent dialogue volume.**
  Address [#187](https://github.com/themuffinator/openQ4/issues/187) with a localized,
  persistent spoken-dialogue control in UI-01. Inventory actual dialogue routing
  across NPC speech, radio/comms, cinematics, scripted/BSE sources and Awakening;
  do not assume one channel or the multiplayer voice-chat CVar covers them all.
  Keep master/effects/music controls and authored distance/reverb behavior coherent.
  Qualify overlapping speech, mute, pause, save/restore, locale fallback and device
  changes, including proving that the control does not mute unrelated weapon audio.
- [ ] **AUD-07 — P1, decision, implementation and qualification: dialogue subtitles.**
  Address [#186](https://github.com/themuffinator/openQ4/issues/186) with UI-02/UI-04.
  Inventory dialogue-to-text/localization data available from installed assets and
  define licensed authored mappings for gaps; do not add extracted proprietary
  transcripts to the repository. Drive cues from actual voice playback, covering
  interruption, overlapping/radio speech, pause/time scale, save/restore and locale
  fallback. Provide readable scaling, contrast/background, speaker identification
  where available and persistent controls. Qualify SP/Awakening scenes and stereo
  HUD placement; distinguish subtitles from optional full sound-effect captions.
  Missing transcript coverage must remain explicit rather than counted as complete.

## Desktop platforms and packaging

Sources: [platform support](platform-support.md), [Deck QA](steam-deck-qa.md),
[Linux ARM64 evidence](linux-arm64-signoff-evidence.md),
[macOS policy](macos-support-matrix-policy.md), [macOS evidence](macos-signoff-evidence.md).

- [ ] **PLAT-01 — P1, qualification: Linux x64 and Steam Deck hardware breadth.**
  Broaden physical GNOME/KWin/wlroots/SteamOS, Wayland/XWayland, audio/input hotplug,
  suspend/resume, HiDPI/display and package coverage. Execute the Deck hardware
  checklist across available LCD/OLED/game/desktop modes and Steam Input routes.
  Its engineering implementation is already present; hosted compositor/WSLg tests
  cannot substitute for its remaining physical-device checks. Retain the specific
  CachyOS/RTX 3070 pacing report until matching evidence closes it.
- [ ] **PLAT-02 — P1, qualification: Linux ARM64 promotion.** Use physical
  desktop-GL-capable AArch64 hardware for native-Wayland SP/save-load, MP peers,
  packaged dedicated/client, audible audio, input and display recovery. Bind the
  accepted reports to exact candidate archive/runtime hashes under the existing
  evidence policy before changing the preview release tier.
- [ ] **PLAT-03 — P1, implementation and qualification: Windows ARM64 client.**
  Add automated client startup/renderer/audio evidence, then real-hardware SP/MP,
  save/load, controllers and package checks using available GL/Vulkan drivers.
  Native build/stage and assetless dedicated startup already exist and do not
  establish client support.
- [ ] **PLAT-04 — P1, qualification: macOS arm64 release evidence.** Use real Apple
  hardware/compliant Apple-hosted workflow at the documented macOS 11 floor and
  latest supported public macOS. Qualify both published package variants, SP/MP,
  dedicated scope, Retina/displays, input/audio, Finder/terminal/DMG/copied-app
  discovery, signing/notarization/Gatekeeper and matching symbols. Run the existing
  full Signoff/CollectResults validators before first-class claims. Legacy evidence
  schema companion-SHA fields must describe the in-tree source identity.
- [ ] **PLAT-05 — P2, investigation and qualification: Apple renderer and
  packaging decisions.** Recheck historical points/pixels/letterbox, failed
  context/MSAA preferences, VBO workaround cost and module transitions on current
  hardware. Qualify MoltenVK separately; retain its translation-layer description.
  Intel/universal2/Rosetta publication, removing the Metal-bridge package, native
  Metal and retiring comparison backends require explicit policy and evidence
  decisions; an old plan is not authorization to change the current release matrix.

## Engine loading safety and developer tools

Sources: [job service](parallel-job-system.md), [loading caches](loading-cache-modernization.md),
[runtime refinements](core-runtime-refinements.md), [map workspace](level-editor-modernization.md),
[AAS compiler](aas-compiler.md), [dmap](dmap-compilation.md).

The broader map-editor implementation remains **paused** as recorded in its
reference. TOOL-01 through TOOL-04 preserve the outstanding scope; this planning
consolidation does not resume it. Compiler/runtime compatibility fixes and the UI
visual editor have their own active items.

- [ ] **CORE-01 — P1, qualification: foundation final-package evidence.** Repeat
  job on/off, renderer-budget, retail baseline, map-cycle/cancellation and
  deterministic dedicated shutdown checks on clean committed final packages and
  required platforms. Keep installed content read-only and dedicated BSE disabled.
- [ ] **CORE-02 — P2, optimization: cache and loading promotion.** Investigate cold
  load regressions, per-file Sync, cache identity and rewrite churn before enabling
  learned/generated-cache experiments. Default worker DDS reading already exists.
  Require cold/warm baselines, corrupt-cache fallback, bounded memory and cancel/
  restart/disconnect coverage; generated data remains disposable under savepath.
- [ ] **CORE-03 — P2, implementation: justified background asset work.** Extend
  detached decode/transcode/upload scheduling and portal-aware prioritization only
  after profiling, with immutable ownership handoffs, backpressure, cancellation
  and main-thread adoption. Do not move game callbacks, GL calls or live parsers
  onto workers without a proven lifetime and thread-safety contract.
- [ ] **CORE-04 — P2, implementation and qualification: compiler and safety debt.**
  Replace `/Zc:strictStrings-` with strict-string compliance; broaden sanitizer
  gameplay and focused fuzzing for remaining parsers, event normalization and
  geometry. Preserve existing bounds, malformed-network rejection, archive/path
  safety and source provenance. Track reproduced issues individually rather than
  treating prior hardening as proof of every parser or full restore transaction.
  Reproduce historical RmlUi wrap/revision and four native input fixtures' SDL
  subproject-header warnings on a fresh current configure; repair remaining
  dependency/header mismatches without updating dependencies solely to satisfy
  an obsolete warning. Recheck lazy pre-cache/missing-retail-image diagnostics
  separately.
- [ ] **CORE-05 — P1, implementation and qualification: safe compiled geometry.**
  Resolve `hub2`'s generated 36,635,340-index runtime surface against the current
  16,777,216-index limit through safe partitioning or representation changes; do
  not simply raise safety bounds. Address `MAX_SIL_EDGES` and remaining compiler
  diagnostics individually. Gate on successful compile, runtime load, rendering,
  collision and AI behavior with bounded memory. Reproduce the historical `hub2`
  failure with the current compiler before selecting a representation change.
- [ ] **CORE-06 — P2, implementation and qualification: AAS tactical generation.**
  Complete cover/lean/corner/pinch/vantage generation or retain an explicit
  rebuilt-map limitation; path/swim compilation already exists. Test stock/custom
  rebuilt data and actual AI consumers, including malformed or absent tactical
  data. These compiler gaps remain independent of the map-editor pause.
- [ ] **TOOL-01 — P2, implementation: complete the map workspace.** Build on the
  existing transactional document service and source preview. Add docking,
  orthographic views/camera persistence, richer entity/model previews, typed
  properties and linked diagnostics. Qualify localized responsive layouts, native
  input/high DPI and desktop backends. This is not restored MFC Radiant.
- [ ] **TOOL-02 — P2, implementation: geometry and asset authoring.** Add lossless
  brush/patch adapters, grid/snapping/transforms, texture locking, one undo action
  per gesture, layers/groups/prefabs and safe rename/reference repair. Make asset
  indexing asynchronous and bounded. Test cancellation, mixed selections, large
  maps and preservation of untouched source.
- [ ] **TOOL-03 — P2, implementation: build and play workflow.** Integrate dmap/AAS
  progress, cancellation and source-linked diagnostics with an isolated play
  sandbox and editor-state restoration. Preserve previous playable output on
  failure. Consume CORE-05/CORE-06 compiler capabilities and qualify stock/custom map
  round trips and AI use of rebuilt data.
- [ ] **TOOL-04 — P2, qualification: authoring reliability.** Cover forced I/O
  failure, external edits, atomic saves, recovery, repeated undo/redo, renderer
  switches and Windows/Linux/macOS workflows. Declare startup/edit/selection/view
  latency budgets. Keep the known unbuilt legacy Radiant defects distinct from
  the supported experimental workspace and the separate UI visual editor.
  Reproduce and fix workspace labels disappearing after full video restart on
  GL/Vulkan; Vulkan reactivation helping one run does not close both backends.

## Deferred design decisions

These preserve uncompleted ambitions from old plans without presenting them as
promised features or retail compatibility defects. Promote a decision into a
workstream with scope, dependencies and acceptance before implementation.

| ID | Deferred scope and prerequisites |
| --- | --- |
| OPT-01 | Higher authoritative simulation rates: separate physics/script/prediction/demo/content design after HF-01 through HF-07; do not change tick rate to claim higher FPS. |
| OPT-02 | GPU skinning promotion for animation-heavy content: batch GL dispatches/persist source buffers and prove benefit. Current stock workloads did not justify default promotion. |
| OPT-03 | True HDR10/scRGB output, sparse/virtual textures, broad GPU-driven/async-compute work, background shader compilation, shadowed light-injected volumetrics and world-space GI: require measured need, target content/hardware and classic rollback. |
| OPT-04 | Live relay/delayed broadcast, multi-POV/multi-window audio, compression/export, persistent competitive identity/web/brackets: separate security, capacity, backpressure and reconnect designs. Forced client screenshots remain rejected. |
| OPT-05 | Overload/Harvester/Domination/Attack and Defend: keep unavailable until authoritative objectives/scoring, networking, bots and UI exist. Race/factories/rulesets remain outside accepted current scope. |
| OPT-06 | Awakening's unreachable grapple/concussion/tether/gravgun/compass content: explicit content-completion project, not a blocker for the integrated SP campaign. No expansion multiplayer module. |
| OPT-07 | HDR audio, ducking, geometric obstruction and reverb editing: optional authored-mix changes after retail audio qualification. |
| OPT-08 | Additional idTech titles, wholesale SP/MP source unification, broad third-party feature ports and extra cosmetic color syntax: require a product/maintenance decision, source/license review and concrete benefit; the old TODO is not an implementation mandate. |

## Release qualification and completion rules

- [ ] **REL-01 — P1, qualification: exact final-package matrix.** Build with
  project Meson wrappers/presets and restage current engine/renderer/game modules
  into `.install/`. Run SP, MP listen/client and dedicated roles against installed
  original assets, including campaign/mode transitions and save/demo cases.
  Record both passed and failed configurations, source/build identities and logs;
  no stale staged package or menu-only run qualifies gameplay.
  Use complete Git history for release version/save-layout generation; the current
  shallow-history rejection protects against erroneous legacy build-1 stamps.
  Test both clean installs and upgrades with existing archived CVars/configs,
  bindings, saves and mod selections. Qualify any default/config migration without
  erasing player choices merely to make upgraded installs match fresh defaults.
- [ ] **REL-02 — P1, qualification: visual and human evidence.** Use engine
  `screenshot` render-target captures and matched viewpoints/time. Perform actual
  motion, input latency, audible sound, accessibility and VR comfort checks on
  appropriate devices. Default to windowed tests. Fullscreen and input control
  require the project's specific prior user permission; never substitute OS
  screen capture or silently infer permission from a historical test plan.
- [ ] **REL-03 — P1, qualification: lifecycle and failure matrix.** Cover cold/warm
  starts, assets/cache failures, long sessions, map/campaign/mode changes, network
  loss/reconnect, saves/demos, device/surface/audio loss, resize/high DPI, pause/
  slow motion, background/suspend, shutdown and renderer/module recovery. Verify
  bounded resources, fallback and user-visible error behavior without lowering
  gates to turn historical failures green.
- [ ] **REL-04 — P1, documentation and promotion: close only proven scope.** Update
  this roadmap, UI acceptance registers, support/capability matrices and curated
  release notes together. State hardware/backend exclusions, upgrade/reinstall
  requirements and opt-in defaults. Use the existing renderer promotion token,
  platform evidence validators, signing/symbol/licensing/package checks. A scoped
  local implementation pass, synthetic device or archived branch is not product
  acceptance. Record a deliberate exclusion when a feature will remain deferred.

## Plan archive and traceability

The [archive index](archive/2026-10-18/README.md) and
[relocation manifest](archive/2026-10-18/manifest.json) map every superseded planning
file to its retained location and the owning roadmap sections. The manifest binds
original Git content and migrated archive bytes. Historical design constraints
used by tests and UI requirement sources remain traceable; their paths and hashes
are updated without accepting or deleting requirements.

Keep new work in this roadmap. Put implementation contracts, investigations and
validation evidence in the relevant technical reference, linked from the task.
Add newly discovered gaps here with a stable ID and acceptance condition instead
of starting another competing plan.
