# Temporal Presentation

Milestone E adds a default-off temporal presentation path shared by OpenGL and
Vulkan. It combines delayed GPU-time dynamic resolution with native-resolution
temporal accumulation, while preserving the existing SMAA/spatial path as the
immediate rollback.

## Ownership Contract

The renderer selects one immutable presentation state at `BeginFrame`:

- `sceneWidth` and `sceneHeight` size the game-owned 3D and authored-post chain;
- `outputWidth` and `outputHeight` size the temporal histories and final output;
- 3D rendering and authored scene effects complete before temporal resolve;
- HUD, menus, console, and other root 2D views are submitted after that resolve
  directly at native output resolution.

Renderer ABI v11 exposes this frame state and one append-only temporal-resolve
entry point to both game modules. The SP authored-post path owns two native-sized
history render targets; direct and MP paths that do not use that post chain use
an equivalent lazy backend-owned pair. Either owner swaps only after the exact
frame/generation resolve is accepted. A generation change, native extent change,
video/backend restart, map/session discontinuity, camera cut, capture, missing
current-frame depth stamp, or rejected write invalidates continuity. A dynamic
scene-size change does not: history is native-sized, so retaining it is required
for TAAU.

No loose material or shader asset is required. Backend resolve programs are
engine-owned resources, and retail Quake 4 materials remain unchanged.

## Dynamic Resolution

`r_rendererDynamicResolution 1` enables the controller and automatically enables
the backend-neutral whole-frame timing stream needed by it. OpenGL uses the
existing four-slot timestamp-query ring; Vulkan reads timestamps only after the
ordinary frame fence for that slot retires. Neither backend waits for a current
frame query.

The controller associates each delayed sample with the exact scale of its source
frame, rejects stale/out-of-order/generation-mismatched samples, drops resolution
quickly above budget, and raises it only after an under-budget streak. Dimensions
are bounded and aligned before target allocation. Unsupported timing keeps the
configured safe ceiling and reports `dynamicActive=0`.

Manual `r_screenFraction` scaling remains available when the automatic
controller is off. With temporal AA active, even mode `0` uses the scene-target
route so TAAU receives the requested low-resolution scene instead of the legacy
back-buffer crop path. Root UI-only frames never enter either scaling route.

Relevant controls:

| CVar | Default | Purpose |
|---|---:|---|
| `r_rendererDynamicResolution` | `0` | Enable automatic 3D scene scaling |
| `r_dynamicResolutionMinScale` | `50` | Minimum automatic scale, percent |
| `r_dynamicResolutionMaxScale` | `100` | Maximum automatic scale, percent |
| `r_dynamicResolutionTargetMsec` | `0` | Explicit GPU budget; `0` derives it from presentation timing |
| `r_dynamicResolutionTargetUtilization` | `90` | Share of the presentation interval available to GPU work |
| `r_dynamicResolutionDropStep` | `5` | Maximum percentage-point drop per over-budget sample |
| `r_dynamicResolutionRaiseStep` | `2` | Percentage-point increase after a recovery streak |
| `r_dynamicResolutionRaiseFrames` | `30` | Retired under-budget samples required before a raise |
| `r_dynamicResolutionAlignment` | `8` | Scene-dimension alignment |
| `r_dynamicResolutionCaptureNative` | `0` | Force known captures to native scene resolution instead of freezing scale |

`rendererTemporalPresentationStatus` prints the latched extents, delayed sample
identity and age, target budget, controller decision, image-history generation,
and capture state.

## Temporal History And Motion

`r_temporalAA 1` replaces the game SMAA tail only when the renderer accepts a
validated temporal resolve command. Rejection leaves the established SMAA or
spatial final pass in place. The resolve uses an eight-sample Halton sequence,
previous camera projection, depth reprojection, object velocity where available,
neighbourhood clamping, colour/depth disocclusion rejection, and conservative
reactive rejection.

Every visible motion class has explicit ownership:

| Domain | History treatment |
|---|---|
| Static world | Camera/depth reprojection and disocclusion testing |
| Rigid entities | OpenGL and Vulkan write eligible object velocity from the previous model and camera transforms; a missing transform or failed draw retains conservative packet-region rejection |
| Skinned geometry | Reactive rejection unless a backend supplies an explicitly validated previous palette and velocity stream |
| Particles/BSE | Reactive rejection |
| Material/generated deforms | Reactive rejection unless a backend supplies explicitly validated previous vertices |
| Portal, mirror, and remote subviews | Stable unjittered child render; sampled parent surface is reactive and marked as separate-history ownership |
| In-world GUI | Rigid/camera motion plus reactive rejection |
| First-person view model | Depth-hack-aware rigid/camera motion plus reactive rejection |

The conservative reactive routes are deliberate ownership, not zero-vector
claims: history is suppressed when a precise prior vertex stream is unavailable.
The shared packet policy carries at most two conservative normalized regions;
when a backend cannot establish that policy, it rejects history over the full
view. Root 2D UI is never part of the temporal history or scene scaling.

Vulkan keeps TAA transform history separate from motion blur. It commits that
history only after the matching color history write, and requires the previous
backend frame, view identity, generation, model identity and scene extent to
match. Captures, cuts, resizes, missing previous geometry and failed resource
admission cannot reuse old vectors. Its single-sample RGBA16F velocity target
tests fragments against the current resolved scene depth, including MSAA scenes.
Vectors include camera motion and jitter once, use scene-pixel units, and scale
to native presentation pixels in the resolve. Unsupported geometry keeps its
explicit reactive ownership.

`rendererTemporalPresentationStatus` also reports Vulkan's eligible/drawn rigid
surfaces, whether the last vector pass was complete, and completed vector views.
The stock HDR gameplay gate checks that completed vector views increase during
each scaled/restarted interval. A single frame can correctly reject history
after a time discontinuity or newly visible entity; cumulative freshness proves
the real production draws without requiring unsafe reuse on that frame.

`r_temporalAAFeedback`, `r_temporalAAReactiveScale`, and `r_temporalAADebug`
control maximum history weight, rejection strength, and the velocity/reactive/
history-weight diagnostic views.

## Cuts And Captures

Per-view identity includes the render world/map, root or subview chain, view id,
flags, and capture surface provenance. History is rejected for identity or
generation changes, native output resizes, time discontinuities, teleports,
large rotation cuts, and projection cuts. Equal simulation times remain valid
so high-refresh presentation can accumulate between game tics.

Known screenshot and save-preview frames render without temporal jitter. A
capture that begins after the game queued its scene marks the source timing
sample ineligible, advances only the image-history generation, and makes both
backends bypass history reads and writes. The current scene is spatially
presented for readback, then ordinary history starts cleanly on a later frame.

The stock renderer remains a numeric SDR pipeline: temporal targets preserve the
same UNORM/code-value contract as their source, and neither resolve shader adds
an sRGB or gamma transfer. Native UI is composed after the scene resolve.

## Validation

Dependency-light policy coverage lives in
`openq4-temporal-presentation-core-test` and
`openq4-temporal-history-core-test`. The static cross-repository/backend contract
is checked by `tools/tests/renderer_temporal_presentation.py`. Runtime acceptance
uses windowed engine-TGA captures and mode-specific SP/MP gameplay; operating-
system capture and injected user input are not part of the workflow.

`rendererVulkanTemporalMotionSelfTest` numerically reads GPU velocity and resolve
attachments for positive/negative translation, rotation, perspective interpolation, camera plus jitter,
invalid previous clip positions, asymmetric depth occlusion and scaled scissors.
Thirty-six fixtures cover two scene extents and both stored image origins;
missing geometry, new entities and
stale view/frame/generation/extent/capture state must reject exact ownership.
The required `renderer-vk-temporal-motion-selftest` matrix case repeats them
after full restart with active validation. These tests establish numerical
behavior, not moving-scene visual parity, performance or platform promotion.
