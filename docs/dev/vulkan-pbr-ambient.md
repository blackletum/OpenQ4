# Native Vulkan PBR ambient lights

Status: implemented; 41 lighting, 18 capacity and 44 resource controls pass at
each of 0x/4x MSAA on Windows/NVIDIA. Both 68-case direct-light regressions pass.
This is authored `ambientLight` material lighting, separate from analytic IBL,
reflection probes and baked light grids. Vulkan and PBR remain experimental.

Eligible PBR receivers now evaluate an ambient light in the same linear material
domain as their point and projected lights. The source is isotropic diffuse:
projection × falloff × light color × linear albedo × (1 − metallic) × 0.96/π.
It has no normal-direction or roughness dependence and contributes no metallic
specular lobe. Environment lighting retains that responsibility. Authored AO
continues to affect the indirect source; it does not attenuate this light stage.
Material diagnostics and emission retain their once-per-surface owner.

Previously, `VK_PBRDirectInteraction` rejected ambient lights. Opaque PBR
surfaces therefore accumulated the classic tangent-space ambient response, and
transparent PBR surfaces received classic lighting before their ordered native
alpha composite. Ambient stages were also absent from the transparent record
bound and resource transaction. They now use the same admission, preparation,
recording, rollback and ordered replay as other supported lights. Every active
ambient stage counts toward the complete 256-record bound. Classic materials
and rejected PBR materials retain their existing shader.

## Reproduction and acceptance

Prepare a fresh laboratory runtime with `renderer_pbr_laboratory.py --prepare`
after generating the current fixture. The two precached ambient materials have
white projection/falloff textures and one or two colored stages. The dedicated
capture tool uses constant radiance to make the material response independently
calculable, with a classic background retained behind source-alpha surfaces.

```text
python tools/tests/renderer_vulkan_pbr_ambient.py --runtime-root <laboratory> --basepath <retail-assets> --output-dir <captures> --backend vk --samples 0
python tools/tests/renderer_pbr_ambient_parity.py --vk <captures>/report.json --output <proof.json>
python tools/tests/renderer_vulkan_pbr_capacity.py --runtime-root <laboratory> --basepath <retail-assets> --output-dir <capacity> --samples 0 --ambient-lights
python tools/tests/renderer_vulkan_pbr_resources.py --runtime-root <laboratory> --basepath <retail-assets> --output-dir <resources> --samples 0 --ambient-lights
```

Repeat with `--samples 4`. The 41 lighting captures cover scalar/packed/separate
material data, zero AO, roughness extremes, metallic rejection, normal encodings,
rotation, cutouts, diagnostics, emission, two lights, two stages, transparent
backgrounds, resource rejection and image/partial/full restart. The proof checks
absolute pixel values and alpha composition within 1.1 display bytes, plus exact
material, rollback and restoration comparisons. Fully covered interior pixels
are identified from a separate ownership capture, including a cutout control.
It validates complete capture sets, image/log hashes and actual sample counts.

The v25 negative and v26 candidate are retained under
`.tmp/vulkan-gap-closure/pbr-direct-curvature/`. The old scalar sample reads
147/73/37 at its center; the corrected value is 24/12/6. A metallic sample changes
from 191/62/20 to black with IBL disabled. The negative fails both the numerical
checks and complete ambient-record ownership. Both candidate sample counts pass
all 55 independent checks, including exact full-frame material, rollback and
image/restart restoration comparisons. Numerical error remains below one byte.
The 36 ambient-stage capacity captures pass at, below and above the record bound,
including exact complete classic fallback. All 88 ambient resource captures pass
early/late failure, descriptor rollback, image/video recovery and unlit/skipped
view checks. Six classic/background controls match v25 byte-for-byte.

The first complete direct-light run passed 67/68 controls. Its source-alpha
predicate wrongly rejected green opaque diagnostic spheres visible behind the
transparent specimen; the same image is byte-identical on v25. The direct suite
now hides the background stations before drawing its isolated specimen. The
original failure and old-binary reproduction remain in `direct-v26-0` and
`direct-alpha-background-v25-0`. Both corrected 68-case runs pass in
`direct-v26-0b` and `direct-v26-4b`, including point/projected shadows, emission,
cutouts and restarts. Their complete alpha interior is exactly 0/112/0, matching
the authored source alpha. The 342 accepted captures, retained negatives,
runtime/source hashes and staging state are bound in `checkpoint-v26-ambient.json`.

The attempted production GL pair is **not a valid PBR reference**: its mixed
classic/ambient scene declines modern lighting ownership. That failed capture is
retained as `ambient-gl-v26-0`; no diagnostic parity override is used to turn it
into production evidence. The optional `--gl` comparison rejects that report.
Independent native radiance qualification does not claim GL scene parity.

## Remaining scope

Ambient admission removes one source of mixed numeric lighting on PBR surfaces.
It does not finish native linear HDR scene ownership. Classic accumulated light,
baked diffuse, material compositing and PBR still need a consistent scene-color
boundary before the matching filmic/output transfer can be enabled. In
particular, changing tone curves based on visible PBR counts or decoding each
classic light independently would change the wrong operation. See
[native HDR acceptance](vulkan-hdr.md#remaining-acceptance).
