# Vulkan Phase F — interaction lighting + shadow maps (staging plan)

Status: recon complete (docs/dev/plans/phase-f-recon/ — READ THOSE FIRST;
they carry the file:line ground truth this plan compresses); F1 staged.
Parent: [2026-07-16-vulkan-renderer.md](2026-07-16-vulkan-renderer.md) Phase F;
Phase E record: [2026-07-18-vulkan-phase-e.md](2026-07-18-vulkan-phase-e.md).
Milestone: q4dm2 lit by real per-light bump/diffuse/specular interactions on
Vulkan (validation-clean), then shadow maps; stencil shadows stay Phase G.

## Decisions locked by recon

- F1 = unshadowed interactions for ALL lights. This is exactly the GL
  behavior for lights without shadow surfs (stencil ALWAYS + interactions),
  and vk pipelines have stencil off entirely. r_useShadowMap defaults 0, so
  the shadow-map branch is opt-in later anyway.
- Loop skeleton = RB_ARB2_DrawInteractions (draw_arb2.cpp:11458-11666):
  per viewLight; skip IsFogLight/IsBlendLight/empty; opaque interactions
  (localInteractions then globalInteractions) at additive ONE/ONE +
  GLS_DEPTHMASK off + depth EQUAL; translucentInteractions at depth LESS.
  Runs between the depth fill and the ambient walks in Draw3DView
  (RB_STD_DrawView order: fill 9717 → interactions 9732 → ambient 9774).
- Must PORT from excluded TUs (GL-free rewrite in a new vk TU):
  RB_DetermineLightScale (tr_render.cpp:675-726),
  RB_CreateSingleDrawInteractionsFiltered walk (tr_render.cpp:875-1033),
  R_SetDrawInteraction (:782-829), RB_SubmittInteraction (:836-861).
  drawInteraction_t itself is in tr_local.h (shared).
- Walk vLight chains directly; ModernShadowPlanner is dormant under vk —
  do not depend on it. lightingCache is NEVER allocated under vk
  (backEndRendererHasVertexPrograms=true) — never reference it.
- Shaders: new interaction.vert/.frag SPIR-V pair. Normalize in-shader
  (no normalization cube map); SAMPLE the real specularTableImage for
  parity with the ARB2 lookup. Reference semantics: interaction.vfp param
  list at draw_arb2.cpp:11098-11169 and glprogs/material_interaction.*.
- Uniforms: per-draw payload ≈ 296B > 128B push floor. Keep the 128B push
  block for MVP + params; add a host-visible UNIFORM_BUFFER_DYNAMIC ring
  (vkRing_t pattern, 256B-aligned slices) for the interaction block
  (origins, lightProjection[4], bump/diffuse/specular matrices, colors).
- Descriptors: reuse the existing per-image single-sampler set cache —
  pipeline layout = 6 identical image-set slots (1=bump, 2=falloff,
  3=lightProjection, 4=diffuse, 5=specular, 0=specularTable) + set 6 =
  dynamic UBO. One vkCmdBindDescriptorSets with 6 cached sets + the UBO.
- Pipeline: one interaction pipeline (blend fixed ONE/ONE) + full
  idDrawVert vertex input (xyz@0, color u8x4@12, normal@16, tangent0@32,
  tangent1@44, st@56); depth func/write via the existing dynamic state
  (EQUAL opaque / LESS translucent), cull per material as in the ambient
  walk; scissor per surface (vLight->scissorRect ∩ surf scissor matches GL
  per-surface behavior — GL sets light scissor only in the stencil-clear
  path, per-surface scissors rule).

## Stages

F1. vk_Interactions.cpp: light loop + decomposition walk + lightScale
    port; interaction SPIR-V pair; UBO ring + 7-slot pipeline layout;
    Draw3DView insertion. Exit: q4dm2 visibly lit per-light,
    validation-clean, menu/2D and GL default unregressed, full gate green.
F2. Shadow maps, scratch-first: module-owned atlas VkImage (reuse
    vkCtx.depthFormat), tile allocator (row scan), depth-only caster
    pipeline (+ perforated alpha-test variant), point-light cube pass,
    comparison sampler in vk_Image, frame-scope suspend/resume with
    loadOp LOAD (split VK_Exec_Begin/EndMainRendering), receiver shader
    variants (projected + point), per-light shadow UBO block. Defer:
    static caches/compose, CSM (default 0), translucent moments (off),
    update budgets. Failure → draw unshadowed (no stencil until G).
F3. RB_ShadowMapResourcesKnownGood honesty (per light class) + the sticky
    fallback contract (lightDef->shadowMapStencilFallbackSticky) so the
    front-end can shed stencil volumes; keep the r_shadowMapMaxUpdatesPerView
    coupling. Only after F2 soaks.

Exit: interactions + shadow maps render correctly on Vulkan for q4dm2 SP
and MP smokes, validation-clean; GL default untouched; r_useShadowMap
default flip remains a separate user-gated decision.
