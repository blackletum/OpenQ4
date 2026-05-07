# Ralph Loop State: Shadow Mapping Roadmap q4dm1 Follow-Up

## Status
- Implementation revised after follow-up: q4dm1 regression reproduced, point-cubemap orientation fixed, GLSL receiver submission fixed for VBO offset-zero geometry, and the point-light shadow-map path is now opt-in only. Stock point lights use the legacy stencil shadow path by default even when `r_useShadowMap 1`, because the experimental point cubemap path is still not parity-correct enough to own shipped Quake 4 point lights.

## Completed In Prior Pass
- Checklist roadmap items for conservative caster salvage, better area rejection diagnostics, semantic light classification diagnostics, bias floor reporting, GPU timer diagnostics, CSM diagnostics, docs, and release notes were implemented and build/runtime verified.

## New Gap From User Screenshots
- `mp/q4dm1` lighting becomes mostly absent when `r_useShadowMap 1`.
- Screenshot coordinates: `origin: (-4442.5 6063.7 388.2)`, `angles: (20.8 -105.6 0.0)`.

## Current Loop Tasks
- [x] Reproduce or gather runtime evidence on `mp/q4dm1`.
- [x] Inspect point/projected shadow interaction shaders and renderer dispatch for full-light suppression.
- [x] Implement the smallest robust fix that restores lighting while preserving mapped shadow behavior.
- [x] Sync canonical shader/content changes to `.install/`.
- [x] Build and shader-compile validate.
- [x] Runtime validate inside `mp/q4dm1`, including a compare setting isolation if needed.
- [x] Update docs/release notes if behavior, cvars, or defaults change.
- [x] Run architect verification before final response.

## q4dm1 Finding
- Runtime logs show the captured user view is point-light only: `projected=0`, `point=7-13` depending on frame.
- Point depth-compare on/off left the original dark image visually unchanged, so the initial fault was common to both compare modes.
- `RB_PointShadowMapBuildViewAxis` used a vertically flipped cubemap orientation compared with the existing envshot and lightgrid cubemap axes. This made point shadow receivers sample a different face convention from the one used during point shadow rendering.
- Point depth rendering is limited to the shadow-map caster lists so legacy multi-chain shadow-volume surfaces do not become false occluders. A temporary containment rule also required point opaque casters to have legacy `shadowTris`; q4dm1 reports showed that rejected roughly 860-900 legitimate point casters per interval, leaving most shadows missing. That requirement was removed, but the mapped point-light result still was not visually reliable enough for stock assets.
- The final q4dm1 mask-fail blocker was receiver preparation, not missing geometry. Verbose diagnostics showed valid geometry/cache counts but `prepared=0`; `vertexCache.Position()` may legally return OpenGL VBO offset `0`, and the old helper treated that value as `NULL`. The helper now returns a success flag separately from the GL pointer value and uses explicit draw-vertex byte offsets for GLSL attributes.
- Final policy: `r_shadowMapPointLights` defaults to `0`. With this default, `RB_ShadowMapLightSupportReason()` reports point lights as `point-disabled`, so they bypass mapped shadow rendering and continue through the legacy interaction/stencil path. Setting `r_shadowMapPointLights 1` keeps the experimental point cubemap path available for focused future work.

## Validation
- `.\tools\build\meson_setup.ps1 compile -C builddir` succeeded after the VBO offset-zero fix; only the existing JPEG PDB linker warnings appeared.
- `.\tools\build\meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects` succeeded and staged content/shaders under `.install/baseoq4/`.
- `mp/q4dm1` at `origin (-4442.5 6063.7 388.2)`, `angles (20.8 -105.6 0.0)`, `r_useShadowMap 1`, `r_shadowMapPointDepthCompare 0`: `q4dm1_vbozero_sm1_20260501_173617`, screenshot luma `84.91`, log reports `mapped(local=3 global=13)` and `fallback(local=0 global=0)`.
- Same map/view with `r_shadowMapPointDepthCompare 1`: `q4dm1_pointcmp_sm1_20260501_173812`, screenshot luma `84.99`, log reports mapped point passes, `pointCmp` active, and `fallback(local=0 global=0)`.
- Architect verification found no blocking issues and judged this validation sufficient for the reported q4dm1 lighting regression. Noted non-blocking follow-ups: broader map/driver coverage, future ambient-only point caster support, and wider projected/CSM/translucent regression sweeps.
- Follow-up "most shadows are missing" validation after removing the legacy-`shadowTris` point caster gate:
  - `q4dm1_casters_restored_sm1_20260501_190630`, `r_shadowMapPointDepthCompare 0`: caster totals rose to roughly `440-556`, `pointNoLegacyCaster` disappeared, log reports mapped point passes with `fallback(local=0 global=0)`, screenshot luma `84.76`.
  - `q4dm1_casters_restored_pointcmp_20260501_190757`, `r_shadowMapPointDepthCompare 1`: caster totals rose to roughly `440-674`, `pointCmp` active, mapped point passes with `fallback(local=0 global=0)`, screenshot luma `85.18`.
- Correctness-first fallback validation after adding `r_shadowMapPointLights 0` default:
  - `.\tools\build\meson_setup.ps1 compile -C builddir` succeeded after adding the opt-in cvar; only the existing JPEG PDB linker warnings appeared.
  - `.\tools\build\meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects` succeeded and staged the updated binaries/content.
  - `q4dm1_pointfallback_sm1_20260501_192636`, `r_useShadowMap 1`, `r_shadowMapPointLights 0`: log reports `supported=0`, `mapped(local=0 global=0)`, `unsupported[noShadows-flag=7, point-disabled=11]`; screenshot luma `66.21`.
  - `q4dm1_legacycontrol_sm0_20260501_192757`, `r_useShadowMap 0`, same map/view: screenshot luma `66.13`.
  - Sampled image comparison between the fixed `r_useShadowMap 1` fallback and legacy `r_useShadowMap 0` control: `rmse=7.89`, `meanAbs=1.20`, confirming the fixed default behaves like the known-good legacy point-light path instead of the broken mapped point-light path.

## Tool Notes
- Optional Ralph MCP state and visual-verdict tools unavailable; using manual files and screenshot analysis.
