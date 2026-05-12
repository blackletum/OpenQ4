# GL Renderer Modernization

OpenQ4 now has the first GL-only foundation for the high-performance renderer redesign. This milestone does not replace the ARB2 renderer yet; it adds the capability, selection, telemetry, compatibility bridge, and opt-in modern-executor preparation path that later GL 3.3/4.x draw execution will use.

## Runtime Tiers

`r_glTier` selects the requested OpenGL tier:

- `auto` keeps the compatibility bridge safe by default while probing the full driver capability set.
- `legacy` forces the GL2-style compatibility survival path.
- `gl33`, `gl41`, `gl43`, `gl45`, and `gl46` request modern tier selection. On the SDL3 backend these forced modern values use the core-profile context ladder first.

The internal tier names are:

- `LegacyGL2Compat`
- `ModernGL33`
- `ModernGL41`
- `GpuDrivenGL43`
- `LowOverheadGL45`
- `TopGL46`
- `NullRenderer`

Metal and Vulkan are intentionally out of scope for this track.

## SDL3 Context Ladder

The SDL3 platform backend now creates GL contexts through an explicit ladder. Forced modern tiers try:

1. `4.6 core`
2. `4.5 core`
3. `4.3 core`
4. `4.1 core`
5. `3.3 core`
6. compatibility fallback

The default `auto` path uses the same version ladder with compatibility profiles, because the current shipping executor is still the ARB2 compatibility bridge. This avoids regressing stock rendering while the modern executor is being built.

`r_glDebugContext 1` requests a debug context on platforms/drivers that support it.

## Capability Probe

The new capability probe records exact version/profile data and feature flags for:

- UBOs, VAOs, instancing, texture arrays, MRT/FBO support
- timer queries, sync objects, map-buffer-range
- buffer storage, direct state access, multi-bind
- compute shaders, SSBOs, draw indirect, multi-draw indirect
- texture views, GL SPIR-V, bindless texture availability

Extension parsing is token-safe and uses `glGetStringi` when available, with legacy string parsing only as fallback.

## Upload Bridge

Frame-temporary vertex-cache uploads now route through the renderer upload stream before falling back to the original legacy temp buffers. This keeps the public `idVertexCache` API unchanged while moving dynamic GUI, deform, packed-surface, and interaction-prep uploads onto the new renderer-owned path.

The stream is feature-gated:

- GL 4.4+/`ARB_buffer_storage` uses persistent mapped buffers when `r_rendererUploadPersistent 1`.
- GL 3.x+/`ARB_map_buffer_range` uses map-range streaming.
- Older VBO-capable paths use orphaned `glBufferSubData` streaming.
- If the upload stream is unavailable or overflows, the old vertex-cache fallback remains active for compatibility.

`r_rendererUploadMegs` controls the per-frame upload-stream size. The stream keeps multiple frame buffers and uses GL sync objects when available before reusing one. `gfxInfo` reports whether the dynamic upload bridge is enabled, which path it selected, the ring size, and whether persistent mapping is active.

Use `rendererUploadSelfTest` to run the pure ring/allocation tests in a live build.

## Metrics

`r_rendererMetrics` controls renderer telemetry:

- `0`: off
- `1`: periodic frame summary
- `2`: per-frame command, packet, and graph detail

The metrics layer records front-end time, submit time, back-end time, view/entity/light counts, draw/surface/vertex/index counts, upload bytes, buffer stalls, upload-stream high-water/overflow data, scene-packet counts, packet material/resource/geometry coverage, packet-driven render-graph counts, modern-executor preparation coverage, modern shader-library readiness, modern draw-plan coverage, modern submit-plan readiness, and selected renderer tier.

`r_rendererGpuTimers 1` samples GL timer queries when `r_rendererMetrics` is enabled and the driver exposes timer-query support. Samples are resolved on a delayed, nonblocking path; unavailable results are reported as `not-sampled` or dropped instead of stalling the CPU. Detail mode reports resolved GPU timing for the current compatibility backend command categories:

- 3D views
- 2D/GUI views
- render-target operations
- copy-render operations
- special effects
- buffer switches

Use `rendererGpuTimerSelfTest` to verify live timer-query support. `gfxInfo` reports whether renderer GPU timers are available and whether the cvar is enabled.

Metrics now also include the legacy scene-packet bridge, packet-driven render-graph shell, and modern GL executor preparation path. The current ARB2 command stream is translated into `ScenePacket`, `PassPacket`, and `DrawPacket` records before execution; draw packets carry legacy sort keys, material records, first bump/diffuse/specular stage images where available, geometry counts, scissor data, shader-register availability, and cache availability. The render graph then coalesces those packet passes into ordered legacy graph nodes and reports graph pass/pass-packet/draw coverage alongside frame timing. When `r_rendererModernExecutor 1` is enabled on a GL 3.3+ capable tier, the modern executor consumes that packet/graph data, keeps a starter VAO plus frame-constants UBO alive, validates the internal shader-library variants, builds a prepare-only draw plan with program selections and state-batch counts, derives a non-rendering submit-readiness plan from vertex/index cache state, updates the UBO once per backend frame through its first owned state binding cache, and reports prepared pass/draw/plan/submit coverage before handing execution back to the legacy path.

## Modern GL Executor Bring-Up

`r_rendererModernExecutor 1` enables the first modern-executor entry point. It is deliberately prepare-only:

- Requires the selected renderer tier to expose the GL 3.3 baseline feature set, including VAOs, UBOs, and buffer objects.
- Allocates a starter VAO and frame-constants UBO during OpenGL initialization.
- Compiles and reflects internal depth and flat-material shader variants for the supported GLSL tier matrix: 330, 410, 430, and 450 where the current context supports them.
- Consumes `ScenePacket` and `RenderGraph` data every backend frame.
- Builds a prepare-only modern draw plan for depth and flat-material packet categories, including shader-program selections, fallback counts, and state-batch transition counts.
- Builds a non-rendering modern submit plan from draw-plan entries that already have VBO-backed ambient and index caches, including program/VAO-adjacent state batches, scissor/material changes, uniform updates, and explicit fallback reasons for missing buffers.
- Reports prepared pass/draw coverage, shader-library readiness, draw-plan coverage, and submit-plan readiness through `r_rendererMetrics`.
- Always falls back to ARB2 for actual rendering in this milestone.

This gives the GL 3.3/4.1 executor a live object lifetime, frame-constant upload, packet-consumption, shader-library reflection, draw-plan generation, submit-readiness accounting, and metrics contract without changing stock Quake 4 rendering behavior. Use `rendererModernGLExecutorSelfTest` to verify the analysis path and live GL object state. Use `rendererModernGLShaderLibrarySelfTest` to verify the internal GLSL variants and reflected frame-constant bindings. Use `rendererModernGLDrawPlanSelfTest` to verify packet-to-plan classification against the current graph shell. Use `rendererModernGLSubmitPlanSelfTest` to verify submit-readiness classification and cache-fallback accounting.

## Modern Shader Library

The first `ShaderLibrary` milestone is internal-only. It does not replace ARB assembly or Raven-authored GLSL material programs. Instead, capable modern tiers compile a tiny executor-owned shader family from built-in source strings:

- depth-only vertex/fragment variants
- flat-material vertex/fragment variants
- shared `ModernFrameConstants` UBO reflection
- GLSL 330 baseline, plus 410/430/450 variants when the selected GL context supports them

The library records program count, failed variant count, highest validated GLSL version, and required binding reflection. `gfxInfo` prints those values under `Modern GL shader library`. Later milestones can use these validated programs for the first depth and material executor paths without introducing external shader assets or changing stock Quake 4 material behavior.

## Modern Draw Plan

The first modern draw-plan milestone is also internal-only. It consumes the packet frame and render graph after the shader library is ready, then emits executor-owned plan entries for packet categories that can be represented by the starter shader set:

- depth, stencil-shadow, and shadow-map packets select the internal depth pipeline
- ARB2 interaction, light-grid, ambient, and fog/blend packets select the internal flat-material pipeline
- GUI, special effects, authored post-process, render-target, copy, and present packets remain explicit fallback categories

Each plan entry records the source draw packet, pass category, shader kind, program handle, material record index, geometry counts, index/vertex-only mode, and GLSL variant. Metrics report planned draws, depth draws, flat-material draws, fallback draws, state batches, program switches, material switches, and overflow status. The plan is deliberately prepare-only: it validates the modern submission shape while ARB2 continues to render the frame.

## Modern Submit Plan

The first modern submit-plan milestone stays internal-only and does not call `glDraw*`. It consumes the draw plan and derives the subset that is ready for a future GL 3.3-style VAO/VBO indexed submission path:

- planned draws with VBO-backed ambient vertex caches and index buffers become submit-ready commands
- draws missing modern-safe vertex/index buffers remain explicit fallback draws
- the plan records program, vertex-buffer, index-buffer, scissor, and material batch transitions
- the plan records per-draw uniform-update pressure and the single frame-UBO bind that a real executor would need

This gives the renderer a measured answer to “how much of this frame could a modern GL submitter draw today?” before any visible pass replacement. ARB2 still renders the frame.

## Compatibility Bridge

The active renderer remains ARB2. The modern executor is currently an opt-in preparation path and does not submit draw calls. The new scaffolding deliberately wraps existing behavior:

- `RendererBootstrap` owns selected tier/features and marks the ARB2 bridge.
- `RenderGraph` currently builds packet-driven legacy graph nodes from the scene-packet bridge.
- `ScenePacket`, `DrawPacket`, `PassPacket`, and `MaterialResourceRecord` define the backend-neutral packet contract. The bridge translates legacy draw-view, special-effect, render-target, copy, and present commands into packet/pass records, records material/resource and sort metadata from legacy drawSurfs, and does not change ARB2 execution.
- `ModernGLExecutor` owns the opt-in GL 3.3+ prepare-only executor shell, validates packet/graph coverage, keeps the modern shader library alive, updates a frame-constants UBO, and then leaves execution on the ARB2 bridge.
- `ModernGLDrawPlan` translates eligible packet/graph categories into executor-owned depth and flat-material draw-plan entries with shader selections and state-batch metrics, while explicit fallback categories stay on the legacy path.
- `ModernGLSubmitPlan` translates draw-plan entries into non-rendering submit commands when the legacy vertex cache already exposes VBO-backed ambient/index buffers, and records missing-buffer fallback reasons for the future real GL3 executor.
- `RendererUpload` owns the feature-gated dynamic frame-temp stream and keeps the old vertex-cache path as a fallback.

Use `gfxInfo` to inspect the selected tier, context profile, feature flags, capability summary, GPU-timer availability, scene-packet bridge limits, packet-driven render-graph bridge limits, modern-executor availability, shader-library status, draw-plan coverage, submit-plan readiness, and upload stream. Use `rendererTierSelfTest`, `rendererUploadSelfTest`, `rendererGpuTimerSelfTest`, `rendererScenePacketSelfTest`, `rendererRenderGraphSelfTest`, `rendererModernGLExecutorSelfTest`, `rendererModernGLShaderLibrarySelfTest`, `rendererModernGLDrawPlanSelfTest`, and `rendererModernGLSubmitPlanSelfTest` to run the live renderer foundation tests.
