# Vulkan shadow maps — OpenGL parity closure (staging plan)

Status: S1-S3 LANDED (2026-09-02) - stencil-volume elision (22eb0060),
dynamic-caster composition (742c7b91), CSM static caching (2bc4b5ff).
S4-S7 remain. S1 and S2 change runtime behaviour that token pins cannot
fully cover, and still want the user's own gameplay sign-off.

Goal doc for the remaining Vulkan shadow-mapping gaps after Phase
F2a/F2b + the 2026-07-24 shadow parity follow-up. Parent:
[2026-07-19-vulkan-phase-f.md](2026-07-19-vulkan-phase-f.md);
stencil/F3 record: [2026-07-20-vulkan-phase-g.md](2026-07-20-vulkan-phase-g.md).

Scope: bring `r_renderApi vk` shadow mapping to the behaviour, cost, and
diagnosability of the OpenGL path. Out of scope: translucent shadow
moments (`r_shadowMapTranslucentMoments`, experimental, default 0 — the
Phase F closure deliberately excludes it on Vulkan), the modern
clustered receiver path (`ModernClusteredLighting`, GL-only by design),
and the `r_useShadowMap` default flip (separate, user-gated).

## Audit ground truth (2026-09-02)

Most `r_shadowMap*` cvars that never appear under `src/renderer/Vulkan/`
are still honoured there — they reach Vulkan through the shared
`R_ShadowMapProjectedFilterSettings`,
`R_BuildShadowMapProjectedLightState`, and `ClassicInteractionDomain`
seals. A cvar-name grep overstates the gap. Vulkan already has CSM,
the 1/5/9/13 PCF tiers, rotated Poisson, PCSS-lite, receiver-plane
bias, normal-offset, hashed alpha, caster culling, subview policy,
update budgets, and both the projected-atlas and point-cube static
caches. The verified remainder is below.

| # | Gap | GL reference | Vulkan state |
| - | --- | ------------ | ------------ |
| S1 | stencil volumes never elided | `draw_arb2.cpp:2996` returns generation truth | `vk_ShadowMap.cpp:327` hard-returns `false` |
| S2 | no cached-static + dynamic compose | `SHADOWMAP_RENDER_COMPOSE_DYNAMIC`, `draw_arb2.cpp:8179` | `VK_ShadowMap_StaticCacheable` bails + invalidates on any dynamic caster, `vk_ShadowMap.cpp:1113` |
| S3 | no CSM static caching | gated on `r_shadowMapCacheCSM` | `cascadeCount != 1 \|\| atlasDiv != 1` excluded unconditionally, `vk_ShadowMap.cpp:1141` |
| S4 | no receiver debug modes / overlay | `uShadowDebugMode` (15 modes), `uShadowReceiverDebugReason`, `shadow_debug_overlay.vs/fs` | absent; `r_shadowMapDebugMode` / `r_shadowMapDebugOverlay` inert |
| S5 | no importance-ordered admission | `RB_ShadowMapBuildUpdateAdmissions`, `draw_arb2.cpp:9916` | view-light-list order |
| S6 | thin `r_shadowMapReport` | levels 1/2/3 + GPU timer queries + resident bytes | one cache line at level >= 1 |
| S7 | `r_shadowMapPointHighPrecision` inert | packed RGBA8 / fp16 colour cube | always native depth cube; cvar silently does nothing |

## Correction to the 2026-07-24 Vulkan stencil-elision revert

The Phase G "Same-frame fallback amendment" kept
`RB_ShadowMapResourcesKnownGood` conservative on Vulkan because an
Air Defense run had a light whose volume was elided before the backend
learned its admission could not be satisfied, and "the affected receiver
had to be skipped fail-closed".

That hazard no longer exists. `vk_Interactions.cpp:4081-4095` now draws
the receiver unshadowed when nothing else resolved — "a transient loss
of shadowing is preferable to dropping the light contribution for a
complete frame" — which is exactly the OpenGL contract
(`RB_ShadowMapMarkStencilFallbackSticky` then a stencil fallback over a
possibly empty volume chain). The conservative return is now
over-conservative relative to the code it guards, so S1 is unblocked.

Residual per-view Vulkan-specific miss sources after S1: atlas row-scan
exhaustion, point-cube pool exhaustion, and caster representability.
Each costs one unshadowed frame plus a sticky restore, which is the
cost OpenGL already accepts. The shared front-end mirror
(`R_ShadowMapLightWillUseShadowMaps`, `tr_light.cpp:1529`) already
refuses to elide under a non-zero `r_shadowMapMaxUpdatesPerView` or in
a subview with a non-zero `r_shadowMapSubviewPolicy`, so those two
policies cannot strand an elided light on either backend.

## Stages

S1. `VK_ShadowMap_ResourcesKnownGood` reports the per-light-class
    generation truth it already tracks, instead of `false`. Pin the
    fail-open receiver contract so a later change cannot silently
    reintroduce the 2026-07-24 fail-closed shape. Exit: shadow-mapped
    lights carry no stencil volumes on Vulkan; an admission miss draws
    the light unshadowed and marks it sticky.

S2. Cached-static + dynamic-caster composition for projected lights.
    Stop treating a dynamic caster as cache-defeating: keep the cache
    entry static-only, `vkCmdCopyImage` the resident cell into the
    view's atlas tile, then draw only `*ShadowMapDynamicCasters` over
    it. Mirrors `SHADOWMAP_RENDER_STATIC_ONLY` +
    `SHADOWMAP_RENDER_COMPOSE_DYNAMIC`. Exit: a light with a moving
    caster costs one copy plus its dynamic draws, not a full static
    re-render.

S3. CSM static caching behind `r_shadowMapCacheCSM`, reusing S2's
    cell/copy machinery for the `atlasDiv^2` block. Exit: the
    unconditional `cascadeCount != 1` exclusion becomes the GL cvar
    gate plus an exact receiver-state signature.

S4. Receiver debug modes in `interaction_shadow.frag` /
    `interaction_shadow_point.frag` driven by the existing shadow ABI,
    plus the `r_shadowMapDebugOverlay` mini-map. Exit: the 15
    `shadowMapDebugMode_t` modes read the same on both backends.

S5. Importance-ordered update admission: port the
    scissor-area / staleness / `viewInsideLight` / fairness score so a
    limited `r_shadowMapMaxUpdatesPerView` spends its budget on the
    same lights OpenGL would pick.

S6. `r_shadowMapReport` levels 2/3 with per-light decisions, plus
    resident-byte accounting and Vulkan timestamp queries for the
    `r_shadowMapGpuTimerQueries` / `r_shadowMapGpuSyncTimings` pair.

S7. `r_shadowMapPointHighPrecision` honesty on Vulkan: either implement
    the colour-cube storage path or document the cvar as OpenGL-only
    and report it as such in the shadow report. Vulkan's native depth
    cube quantises better than either colour format, so the documented
    no-op is the expected resolution.

## Validation

Build plus the token-pinning suite; no in-game runs in this track.

```
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 compile -C builddir
python tools/tests/renderer_vulkan_shadow_compatibility.py
python tools/tests/renderer_vulkan_world_interaction_compatibility.py
```

Each stage extends `renderer_vulkan_shadow_compatibility.py` with pins
for the contract it introduces. S1 and S2 change runtime behaviour that
token pins cannot fully cover; both need the user's own gameplay
sign-off before the `r_useShadowMap` default flip is reconsidered.
