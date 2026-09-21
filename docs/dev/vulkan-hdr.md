# Vulkan HDR scene and exposure

Status: implemented with local Windows stock-map and GPU correctness evidence;
visual/platform qualification remains open. Vulkan remains experimental. This describes internal scene range
and tone mapping, not HDR10/scRGB display output or full stock-material
linearization.

## Controls and ownership

| Control | Default | Vulkan behavior |
|---|---|---|
| `r_hdrToneMap` | `0` | Enables the existing tone-map/color-correction pass. |
| `r_hdrSceneTarget` | `1` | Selects an RGBA16F scene when tone mapping or an HDR debug view is explicitly enabled and post processing is allowed. Ordinary default rendering retains its LDR behavior. |
| `r_hdrAutoExposure` | `0` | Meters scene luminance before bloom/tone mapping and adapts exposure within the configured limits. |
| `r_hdrAutoExposureAsync` | `1` | Uses completed frame-slot readbacks. `0` deliberately submits and waits without presenting the partial frame, then resumes recording. |
| `r_hdrExposure` | `1` | Multiplies the automatic exposure; remains the exposure control when automatic exposure is disabled. |

Canonical game code in `openQ4-game` selects RGBA16F for the forward scene and
its MSAA resolve. Game post-AA targets remain LDR, because engine tone mapping
runs before authored post effects, SMAA and HUD composition. The Vulkan-owned
direct/scaled scene target follows the same HDR request. A live HDR request
change rebuilds game targets; backend target format changes retire old image
storage behind the existing fences.

The backend-owned MP/direct scene also honors `r_multiSamples`: multisampled
color/depth resolve into a separate sampled target before post processing.
MSAA alone retains this scene target after HDR/TAA are disabled. Temporal and
spatial effects accept resolved depth only when its current generation is valid.

The luminance pyramid uses the same four-tap logarithmic reduction as the
OpenGL HDR shader. Every level is RGBA16F, halves the dimensions with rounding
up, and clamps edge samples. The final 1x1 pixel is copied into a mapped buffer
owned by its frame slot. Normal consumption occurs only after that slot's
existing fence has completed; no additional queue/device wait is introduced
by asynchronous exposure. Invalid floating-point samples are ignored.

The exposure target is key value divided by average luminance, clamped to the
configured minimum/maximum. Exponential adaptation uses elapsed presentation
time and separate brightening/darkening speeds. Live exposure bounds apply
immediately even if adaptation is paused. Map/world identity, device restart,
scene format/sample count/extent and camera discontinuities invalidate the
sample generation. Old and duplicate completions cannot seed the new history.
Captures reuse the current exposure without queuing or adapting a new sample;
already submitted samples may still retire during a synchronous capture.

The explicitly synchronous exposure mode splits a graphics submission without
presenting, retains the acquired swapchain image and upload-ring ownership,
and excludes the split frame from whole-frame GPU timing. It is a diagnostic
mode, not a performance comparison configuration.

## Diagnostics and verification

`rendererVulkanHDRInfo` reports the actual scene format/sample count/extent,
exposure enable/initialization and asynchronous-readback state, generation, queued/completed sample
counts, average luminance, target and current exposure.

`rendererVulkanHDRSelfTest` exercises 24 real GPU fixtures:

- RGBA16F scene capture at 8x8 and 17x9, with 0x and actual 4x MSAA.
- Bright radiance above one, mixed RGB radiance, and dark radiance, each with
  independently computed expected log luminance and exposure.
- Both synchronous and asynchronous readback, resize/storage retirement,
  and rejection of stale, duplicate and disabled completions.

The mandatory `renderer-vk-hdr-selftest` runtime case runs this exercise before
and after a full `vid_restart`, requires the ordered engine-log evidence for
both passes, and rejects validation/VUID/call failures. Both Linux push and
pull-request workflow selections include it. Hosted results and physical
platform coverage remain separate requirements.

The first Windows run passed all 24 fixtures with active validation. A later
run passed all 48 executions around full device teardown/recreation. Evidence:
`.tmp/vulkan-gap-closure/hdr/restart-gpu/renderer_validation_report.json`.
`HDRExposureCoreTest.cpp` checks finite-sample handling, exposure bounds,
time discontinuities, 30/60/144 Hz adaptation equivalence and FP16 decoding.

`tools/tests/renderer_vulkan_hdr_gameplay.py` is the stock-map integration
harness. It uses isolated save paths and an independently copied runtime,
hashes the runtime before and after, disables input, and runs hidden windowed
SP `game/airdefense1` and auto-joined MP `mp/q4dm1` with 0x/4x MSAA. Its engine
captures and state checks cover HDR/LDR toggling, capture continuity, TAA at
75% scene scale, both exposure readback modes, full and partial resize/restart,
manual exposure and disabling HDR. These are correctness runs, not benchmark
or soak evidence.

The initial gameplay run exposed an existing full-restart dispatch error:
Vulkan entered `R_InitOpenGL` and failed to create a GL context. Full restart
now uses the same backend-specific initialization seam as initial startup.
The initial failed report is retained in
`.tmp/vulkan-gap-closure/hdr/gameplay-first/`.

The takeover also reproduced a crash when a screenshot immediately preceded a
partial restart: resumed, unsubmitted command recording still referenced the
old swapchain views. Resize now closes/submits that frame before retiring the
swapchain. Hidden startup consistently suppresses fullscreen/borderless modes.
The original immediate sequence passes on v63 for stock SP and MP at both 0x
and 4x, with active validation, no tracked rendering diagnostics and an
unchanged independent runtime. GPU attachment/MRT tests and 48 HDR fixtures
around full restart pass on the same module. Evidence:
`.tmp/pbr-audit/vulkan-v63-hdr-gameplay/report.json` and
`.tmp/pbr-audit/vulkan-v63-gpu/renderer_validation_report.json`.
The inspected SP captures still show excessive sky/highlight exposure; these
runs qualify lifecycle and sample ownership, not visual parity or performance.

The same four stock cases pass again on the v68 PBR cutout/emission binary,
with active validation and an unchanged runtime
(`.tmp/pbr-audit/vulkan-v68-hdr-gameplay/report.json`). Its GPU suite additionally
reads actual FP16 native emission attachments at 0x/4x MSAA: a low texture
channel retains 4,096 while the bright channels bound to 65,504 after modulation.
This proves a single emission write, not arbitrary multi-light accumulation
or complete scene linearization.
The v69 specular-filtering candidate repeats the same four stock transition
cases and both GPU self-tests successfully, with unchanged runtime files
(`.tmp/pbr-audit/vulkan-v69-hdr-gameplay/report.json` and
`.tmp/pbr-audit/vulkan-v69-gpu/renderer_validation_report.json`).

Renderer ABI 14 also reconciles the engine's renderer header with the canonical
game header (ETC2 capability field and clear alpha). The clear command now
forwards its alpha argument. Engine and both renderer modules must be updated
together. The game scene-format integration requires the rebuilt game modules.

## Remaining acceptance

- Resolve the SP automatic-exposure highlight loss with controlled references.
- Retain controlled OpenGL/Vulkan visual comparisons for the HDR path,
  including authored feedback/capture and post-effect interoperability.
- Verify map/camera discontinuities and resource-admission failures in runtime
  fixtures, beyond the numerical generation rejection tests.
- Complete the broader clean-package, platform/driver, performance and soak
  requirements in the [Vulkan gap ledger](plans/2026-09-20-vulkan-gap-closure.md).
