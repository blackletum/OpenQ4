# Authoring PBR materials

PBR is an opt-in material extension for new openQ4 content. It does not convert
retail Quake 4 materials automatically. The OpenGL implementation is experimental;
unsupported scenes retain the authored classic rendering path. Vulkan currently
supports a smaller direct-light subset. See the [qualification ledger](../dev/plans/2026-09-20-pbr-rendering-audit.md)
for current evidence and limitations.

## Material declaration

Keep a usable classic material beside the `pbr` block. The following example
assumes that the named textures have been authored; it does not add assets to
the engine package.

```text
textures/my_mod/panel
{
    bumpmap textures/my_mod/panel_local
    diffusemap textures/my_mod/panel_d
    specularmap textures/my_mod/panel_s

    pbr
    {
        workflow metallicRoughness
        albedoMap textures/my_mod/panel_albedo
        normalMap textures/my_mod/panel_normal_xyz
        normalFormat tangentXYZ
        normalScale 1
        ormMap textures/my_mod/panel_orm
        metallic 1
        roughness 1
        ao 1
    }
}
```

The classic bump texture retains the engine's classic normal-map convention.
The PBR normal texture declares its own convention explicitly; do not assume
that a raw RGB normal and an A/G/B-swizzled normal contain the same channels.

| Input | Interpretation |
|---|---|
| `albedoMap` | sRGB color; alpha remains linear coverage. Do not bake lighting into it. |
| `emissiveMap` | sRGB color, multiplied by linear `emissiveColor r, g, b`. Values above one retain HDR energy. |
| `ormMap` | Linear data: red = occlusion, green = perceptual roughness, blue = metallic. |
| `metallicMap`, `roughnessMap`, `aoMap` | Separate linear data maps, used instead of packed ORM. |
| `normalFormat tangentXYZ` | Signed tangent-space XYZ reconstructed from RGB. |
| `normalFormat tangentRG` | Signed tangent-space XY from red/green; positive Z is reconstructed. |
| `normalFormat quake4AGB` | The Quake 4 alpha/green/blue channel convention. |

`metallic`, `roughness` and `ao` multiply their maps. Without maps they are
scalar material values. For example, omit `ormMap` and use `metallic 0`,
`roughness 0.6`, `ao 1` for a uniform dielectric. Do not declare packed ORM and
separate material-data maps together. Roughness is bounded to `[0.045, 1]` for
shading, with normal-variance filtering on OpenGL and native Vulkan PBR. AO
attenuates indirect lighting, not direct lights.

For a cutout, author the classic diffuse stage's `alphaTest` threshold. A
conventional `translucent` material with a single `blend blend` stage can use
ordered PBR source-alpha transparency on OpenGL and on native Vulkan. Keep its
alpha in the albedo texture: both backends read the coverage from the albedo
image and scale it by the stage's own alpha register, so the blend stage must
name that same image with untransformed coordinates and no vertex tint.
Unusual blend expressions and custom material programs keep classic ownership.

## Preview and fallback

The PBR preview requires `r_rendererModernQuality 1`, `r_pbrMaterials 1` and
`r_rendererModernVisible 1`. Use `r_hdrToneMap 1` for linear HDR composition.
`r_pbrIBL 1` supplies filtered environment lighting; `r_rendererReflectionProbes 1`
allows explicitly authored probe lights. `r_pbrIBLIntensity` changes indirect
lighting only. `r_rendererModernQuality 0` restores classic ownership.

`r_pbrDebug` exposes albedo (1), normals (2), metallic (3), roughness (4), AO (5),
emissive (6) and ownership (7). In ownership mode, green identifies PBR draws;
magenta identifies classic draws in the modern path. Always check `gfxInfo` as
well: a rejected frame must not be mistaken for successful PBR just because its
classic fallback is visible.

The qualified OpenGL HDR path includes point/projected shadows, cutouts,
ordered source alpha, authored fog/blend lights, and existing baked area
irradiance. Baked diffuse replaces environment diffuse while preserving
environment specular, and respects metalness and AO. The bake remains LDR.

Fixed classic bump/diffuse/specular materials can coexist in a full-viewport
HDR PBR scene with shadows and MSAA disabled. Their original light projections,
normal convention and specular colors are preserved. More complex classic
materials, unsupported receiver semantics, cropped/multiple-root views,
portal/view-weapon grid blending and exhausted resource budgets retain the
complete classic frame. This is not general classic-renderer parity.

Authored probes use the existing `openQ4SpecularProbe` light-material block.
The current atlas holds eight authored cubemaps plus the analytic environment,
with at most 32 frame records and two selected probes per cluster. It filters
rough reflections and diffuse irradiance; it does not capture the scene at
runtime or apply box-projected parallax correction.

## Reproduce the laboratory

After building and staging, run the complete native Vulkan material suite in
an independent laboratory runtime (replace the retail asset path as needed):

```powershell
python tools/tests/renderer_pbr_laboratory.py --prepare --runtime-root .tmp/pbr-native --output-dir .tmp/pbr-native-proof0 --basepath "E:/SteamLibrary/steamapps/common/Quake 4" --backend vk --batch --suite vulkan-direct --samples 0 --timeout 480
python tools/tests/renderer_pbr_laboratory.py --runtime-root .tmp/pbr-native --output-dir .tmp/pbr-native-proof4 --basepath "E:/SteamLibrary/steamapps/common/Quake 4" --backend vk --batch --suite vulkan-direct --samples 4 --timeout 480
```

The suite currently has 68 controls per sample count. Run the separate
`--cases lit,ownership,emissive,master-off` full-map check to expose
unsupported ownership. The emission control is required: transparency is
proven by the difference between the two debug captures, which is the authored
coverage alone, because a single capture also carries the background behind
the surface.

Vulkan currently implements opaque, matching alpha-tested and source-alpha
direct lighting with scalar, packed ORM or separate metallic/roughness maps and
all three documented normal encodings (or no normal map). It requires one
active classic bump/diffuse/specular sequence. A cutout must use the same
image, sampling and untransformed UVs for classic diffuse coverage and PBR
albedo. Emission requires one matching classic additive glow stage; its native
replacement emits once, including without any direct lights.

Source-alpha transparency is ordered: a translucent surface is absent from the
depth fill, so the light pass records its admitted draws instead of adding
them and the material walk composites them where the authored stage would have
drawn. The first recorded light composites through the alpha and the rest add
through it, which is the classic composite of the summed radiance. The whole
view returns to classic ownership when a shadowing light reaches a translucent
receiver, because stencil shadow coverage is reset with its light and cannot
be replayed afterwards.

Mismatched masks/glows and unsupported stage combinations keep classic
ownership. Vulkan does not yet consume PBR environment/probe data. Native
diagnostics implement emission (6) and ownership (7); material-channel modes
1–5 require OpenGL. Both markers keep a transparent surface's coverage, so the
ownership view composites the marker through the authored alpha rather than
replacing the pixel. The native laboratory separately checks equivalent
layouts, mapped shadows, emission, cutout and source-alpha coverage, rollback
and restart. Its green marker proves the admitted native surface path, not
complete environment lighting.
Vulkan filters both surface curvature and normal-map variation with the same
bounded roughness kernel. `r_vkPBRSpecularAA 0` disables this filtering for
diagnostic comparisons; it defaults to 1 and is not archived.
Full scene linearization and display-color parity also remain unqualified on
Vulkan; the direct-material controls do not establish a matching OpenGL image.

Build and stage the engine using the project's normal Meson wrapper first.
From the repository root, the following creates an independent runtime,
generates original test assets, compiles the map, and captures it through the
engine's registered screenshot command:

```text
python tools/tests/renderer_pbr_laboratory.py --prepare --runtime-root .tmp/stock-runtime/pbr-lab --basepath "<retail Quake 4 directory>" --output-dir .tmp/pbr-proof --batch --linear --gl-debug --tier gl45 --cases lit,ownership,no-probes,direct,legacy,master-off
```

The retail directory must contain `q4base`. Choose new runtime/output paths;
the harness refuses to replace an existing runtime. Subsequent runs reuse that
runtime without `--prepare` and use a new output directory. All game runs are
windowed, use isolated save paths and disable mouse capture. The harness does
not inject input or capture the desktop.

The 24-station map covers curved dielectric/metal roughness series, packed and
separate channels, normal encodings, emissive, cutout/source alpha, authored
probes, colored point lights and a projector. Additional scripted controls
exercise moving shadows, skinning, baked lighting, fallback, resize and reloads.
Reports retain binary/fixture/map/harness hashes, ownership diagnostics, display
TGA images and pre-tone-map PFM radiance for completed linear HDR scenes.
The engine rejects linear capture of the encoded non-HDR preview. Add `hdr`
to the example's case list to capture linear radiance. Scene-target storage is
bounded to finite, nonnegative half-float values (maximum 65,504) on the OpenGL
path; the Vulkan emission test bounds one texture-modulated emission write,
not arbitrary sums of many lights. Ordinary
overbright energy is retained. Numerical and negative-control
tests accompany the rendered map; a screenshot alone is not a passing result.
