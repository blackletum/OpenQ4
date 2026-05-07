# Ralph Context Snapshot: Shadow Mapping Roadmap, q4dm1 Dark Lighting

## Request
- Continue using the Ralph loop and subagents to fully implement the remaining "Checklist Implementation Roadmap" from `docs-dev/proposals/shadow-mapping-assessment-30-04-2026.md`.
- New blocker: user screenshots from `mp/q4dm1` show correct lighting with `r_useShadowMap 0`, but lighting is largely absent/dark after `r_useShadowMap 1`.

## Screenshot Evidence
- Map/view: `mp/q4dm1`.
- Console-reported position: `origin: (-4442.5 6063.7 388.2)`.
- Console-reported angles: `(20.8 -105.6 0.0)`.
- User toggled:
  - `r_useLightGrid 0`
  - `r_useShadowMap 0`
  - `r_useShadowMap 1`
- Visual symptom: emissive/glow surfaces still appear, but shadow-mapped interactions remove most local diffuse/specular lighting from the world and weapon view.

## Environment
- Repository: `E:\Repositories\OpenQ4`.
- Date: 2026-04-30.
- Shell: PowerShell.
- Build rules: use `tools/build/meson_setup.ps1`; do not invoke raw Meson on Windows.
- Runtime validation must enter actual map gameplay.

## Ralph Tool Availability
- `ralph` skill loaded from `C:\Users\djdac\.codex\skills\ralph\SKILL.md`.
- Optional Oh-My-Codex state tools and `$visual-verdict` were not exposed by tool discovery in this environment.
- Fallback: keep this manual context snapshot and explicit state file under `.omx/`.

## Current Hypotheses
- High priority: point-light shadow hardware depth compare may be using a linear radial receiver depth against a non-linear cube-map depth buffer, causing most point lights to evaluate as fully shadowed.
- Alternative: mapped point/projected interaction shader may be suppressing the normal ARB2 light contribution when a shadow map is allocated but invalid or over-shadowed.
- Validation should compare `mp/q4dm1` with shadow maps off/on, then isolate point compare (`r_shadowMapPointUseDepthCompare`) and projected compare (`r_shadowMapUseDepthCompare`).

## Constraints
- Preserve stock Quake 4 asset compatibility; fix engine/shader behavior rather than shipping replacement map/content assets.
- Canonical shader edits belong under `content/baseoq4/glprogs/`; `.install/baseoq4/glprogs/` must be staged copies for runtime testing.
- Do not revert unrelated user changes.
