# Authored Vulkan material programs

Status: native drawing is implemented for an initial subset of authored GLSL
ambient and per-light stages. Unknown program names now load and compile their actual source
pair. Stock-name overrides also use their actual source; an embedded stock
implementation requires a reviewed source fingerprint or a canonical name with
no source files. Vulkan remains experimental, and the
full custom-program requirement is still open.

## Compiler contract

`src/renderer/materialprogram/GLSLCompiler.cpp` accepts a vertex/fragment source
pair and explicit, case-sensitive material parameter and texture bindings. It
uses the pinned glslang 15.1.0 compiler in process, through a static library built
by the project's native Meson subproject. It does not invoke an installed SDK
executable, perform file access, or create GPU resources. Source inputs are
limited to one MiB each; empty and embedded-NUL inputs are rejected. Compilation
is serialized around the library's process lifetime and independent callers
can compile safely.

The original OpenGL GLSL pair is parsed and linked before translation. This is
necessary because the compatibility layer initializes per-invocation globals
from uniform storage; translating first could accidentally legalize an authored
write to a uniform. The glslang preprocessor expands macros and conditionals.
The translator operates on tokens, preserving diagnostic line records and
function-local variable shadowing. Generated Vulkan shaders are linked together
before emitting SPIR-V 1.3 for Vulkan 1.1. Failed compilation clears both output
modules and returns a diagnostic, with the shader filename for source failures.

The initial language subset covers GLSL 1.10/1.20 scalar/vector float uniforms,
2D/cube samplers, named varyings, the engine's explicitly bound vertex attribute
names, common legacy vertex/color/texture-coordinate inputs, model-view,
projection, normal and texture matrices, fragment color and coordinates, and
the supported 2D/cube explicit-LOD/gradient functions. Material names are not a
shader-family whitelist. Uniform and interface arrays, authored matrix/int/bool
uniforms, other sampler types, newer GLSL versions and other extensions remain
unsupported and receive diagnostics. Unrecognized legacy built-ins must pass the
Vulkan compiler; they are not silently replaced with dummy values.

The parameter capacity is the engine's 32 slots; the texture capacity is eight.
Active scalar/vector parameters must match the material's upload width. The
runtime draw interface is:

| Resource | Binding | Layout |
|---|---|---|
| Per-draw data | Set 0, binding 0 | 1,312-byte std140 block |
| Authored textures | Set 1, bindings 0–7 | Separate combined image samplers |
| Vertex inputs | Locations 0–5 | Position, color, normal, tangent, bitangent, UV |

The uniform block contains 32 parameter vectors, model-view and projection
matrices, eight texture matrices, stage color, four draw controls and two
CPU-composed model-view-projection matrices. Matrices are column-major. Authored
matrix operations retain GL clip coordinates; the generated vertex entry point
converts clip depth to Vulkan's range. Unmodified `ftransform()` or legacy MVP
positions use the same CPU-converted matrix as depth fill, preserving equal-depth
lighting coverage. Changed authored positions keep the ordinary clip conversion.
Window Y and `dFdy` select
the target's orientation through a pipeline specialization constant. In the authored GLSL
path, vertex color arrays are exposed directly, matching `draw_common.cpp`;
the wrapper does not invent stage tint or inverse modulation for those arrays.
The first native comparison covers texture coordinates, model-view/projection,
authored vertex offsets, fragment coordinates and derivatives in the ordinary
display path. Other target, color and geometry combinations remain unqualified.

## Runtime ownership

OpenGL and Vulkan share the source-pair search order in
`MaterialProgramSource.cpp`. Vulkan passes the complete file lengths to the
compiler, preserving embedded-NUL rejection. Shader identity includes the
program name, parameter names/order/semantics and texture names/order. Numeric
values and image selection remain per draw. `reloadGLSLprograms` invalidates the
authored source lookup on both backends; `reloadARBprograms` also performs this
GLSL reload. Vulkan reuses unchanged valid or invalid source pairs before
checking its version limit. Changed
versions stay alive until device shutdown so recorded draws cannot reference a
destroyed module or a reused pipeline identity.

Native stock selection uses `material_program_sources.json` and the generated
`vk_MaterialSourceSignatures.h`. Both source files must match one reviewed pair;
only CRLF line endings are normalized. The table contains hashes, not retail
shader text. `tools/build/vulkan_material_sources.py --check` is registered in
the native Meson tests and script CI. It compares the generated table with the
reviewed manifest and never automatically approves changed shader source.
Different directories with the same basename are authored programs. Modified,
empty and incomplete pairs cannot silently select a stock implementation.
Some retail guide families have no GLSL source files; their canonical names
retain the existing built-in default only while every candidate source is
absent. Source selection is cached until an explicit shader reload or device
shutdown. A complete pair retains the shared filesystem search order.

OpenGL reload invalidates parsed material stages and the engine's material GLSL
stages. Failed attempts are cached in the current GL context, so a broken file
does not compile again on every draw. Explicit reload or a new context retries
it. Neither backend needs a game restart to load a repaired supported shader.

Each frame slot owns a descriptor pool reset only after its fence completes.
The larger uniform block uses the existing frame ring with the device's uniform
alignment. All eight texture descriptors use the existing image residency and
transition path. Shader modules, layouts, pools and CPU records are destroyed
with the device, after the executor destroys its pipelines. GL handle fields
are never used to store Vulkan handles.

Limits are explicit: 256 retained program versions, 32 MiB of retained compiler
source/module data, 2,048 authored draws per frame slot, and the executor's
shared 128-entry material pipeline cache. Allocation, type and unsupported
binding failures diagnose and skip the affected stage. Exhaustion/recovery
drills are still required; these bounds are not a performance claim.

## Authored lighting

Supported `customLighting` GLSL stages now execute once per contributing light
using their actual shader pair. Per-draw bindings include local light/view
origins, projected-light and falloff planes, bump/diffuse/specular texture rows,
diffuse/specular colors, vertex-color modulation, model/projection rows and the
identity color matrix. Light projection/falloff and other engine texture
semantics resolve from the current interaction. Stage conditions, colors and
texture transforms remain material-controlled. Draws accumulate with ONE/ONE
blending and inherit the receiver's geometry, depth, cull, bias and stencil state.

Arbitrary authored shaders do not have the engine's native shadow-map sampler
contract. When mapped shadows are requested, only affected receivers run through
a separate stencil-filtered interaction walk; other surfaces retain their mapped
shadows. This matches OpenGL's retail stencil caster policy and does not add
support for expanded map-only casters. The front end conservatively retains
volumes for custom or unresolved interactions before submitting any receiver.
The cost of that conservative retention still needs performance qualification.
The two verified native lighting families keep their existing mapped path.

Explicit linear/nearest image filters now select the base mip level even when
an image already has a mip chain. This matches OpenGL's sampler behavior across
image reload. The lighting investigation also repaired OpenGL's material texture
table: unused declared array slots now receive valid material images instead of
retaining a comparison-enabled shadow texture from a previous pass.

## Validation and remaining work

The native `openq4-material-compiler` test covers valid paired programs,
parameters at the capacity boundary, 2D/cube textures, named/legacy attribute
aliases, normal transforms, macros, local variable shadowing, derivatives,
explicit LOD, deterministic recompilation and concurrent callers. Negative cases
cover syntax and link failures, uniform writes, invalid/duplicate bindings,
unsupported interfaces and source-size constraints. An independent `spirv-val`
check validates the emitted modules. These checks establish compilation and
module validity; they are not image, performance, or gameplay qualification.

The v50 Windows native-Meson run passed all 31 cases and the registered Meson
test. The v51 suite adds two invalid parameter-width cases and passes 33 cases.
Independent validation passes all 24 emitted modules and verifies the
uniform member offsets, matching vertex/fragment interfaces, boundary texture
slots and deterministic recompilation. The latest compiler evidence is retained
in `.tmp/vulkan-gap-closure/authored-glsl/compiler-qualification.json`; the
prior foundation evidence remains in the `material-programs` directory.
All 2,978 extracted upstream files and both overlay files were compared with
their pinned inputs. Other operating systems have not yet run this compiler
suite. The v51 runtime links the compiler into the Vulkan module; it requires
the matching staged engine and renderer modules. The prior PBR cutout fix is
retained, and its separate filtering discrepancy remains open.

The expanded v51 scene uses two independently compiled material programs and
engine `screenshot` captures in hidden, windowed, input-disabled gameplay. The
ten-control v51a run passes GL/Vulkan comparisons within one display level;
image reload, partial restart and full restart reproduce each backend's base
image exactly. Its unchanged-source shader control did not detect that Vulkan
had not registered the reload command; v52 adds actual live-edit coverage.
The reusable harness is
`tools/tests/renderer_vulkan_material_programs.py`; reports remain under
`.tmp/vulkan-gap-closure/authored-glsl/`. This is local image evidence, not
stock-map, performance, long-session or platform promotion evidence.

The capacity scene exposed a separate GL reference bug: ambient GLSL stages
linked the named tangent/bitangent/normal attributes to lanes 9/10/11 without
enabling and supplying those arrays. Those stages now bind the surface basis
and legacy normal array around the draw. The capacity fixture uses all three
named basis vectors to choose cube faces; a constant-color cube is a separate
control. This preserves a meaningful reference instead of reproducing stale
attribute values in Vulkan.

After that repair, all twenty v51d and final v51e controls pass. The 32-parameter/eight-texture
program, the changed last parameter and each independently changed texture slot
match GL pixel for pixel. The remaining controls differ by at most one 8-bit
display level, within the unchanged limit of two. Both backends show more than
29,000 changed pixels for each binding control and exact recovery images. The
tests cover named basis attributes, scalar/vec2/vec3/vec4 parameters and 2D/cube
samplers. These results do not establish untested language, blend or target
combinations. Failed intermediate reports remain retained with their diagnostics.
The final staged build also passes the render-target/startup, HDR and temporal
motion GPU regression groups with zero tracked warnings. Runtime, sources,
compiler outputs and reports are bound by
`.tmp/vulkan-gap-closure/authored-glsl/checkpoint-v51-authored-glsl.json`.

The v52 live-edit suite, `tools/tests/renderer_vulkan_material_reload.py`, adds
sixteen controls per backend. It exercises a stock basename under a different
directory, a canonical stock source replacement, changed fragment and vertex
source, syntax failure and repair, a fragment-only canonical source pair and
repair, both reload commands, and partial/full video restart. All sixteen
GL/Vulkan pairs differ by at most one display level; each backend's recovery
and deliberate failure images match their references exactly. The captures
also require the three unchanged SMAA sources to select verified native code.
The two deliberate failures each produce exactly one primary diagnostic before
repair, including a subsequent still-invalid capture. Expected compiler
messages are accepted only inside explicit bounds; unrelated warnings, API
errors and unknown commands still fail the run. Raw logs remain retained.

This suite found that the earlier Vulkan reload functions had no registered
commands: registration was inside OpenGL initialization. Both commands now
register in shared renderer initialization. The first failed Vulkan run is
retained, and the common laboratory now rejects unknown commands. The initial
GL diagnostic parser also needed to strip engine color codes, and the Vulkan
parser needed the specific follow-on compiler messages for the deliberately
undefined identifier. These parser failures remain failed reports. Accepted
source-edit evidence is `gl-v52b`, `vk-v52c` and `compare-v52b` under
`.tmp/vulkan-gap-closure/material-reload/`.

The final staged runtime also passes 35 OpenGL HDR/bloom controls, including
exact full-frame restoration after both GLSL reload commands. The Vulkan
render-target/startup, HDR and temporal-motion GPU groups pass without tracked
warnings. Both registered native tests pass; the 33-case compiler output is
byte-identical to all 24 independently validated v51 modules. The implementation
and retained evidence are bound by
`.tmp/vulkan-gap-closure/material-reload/checkpoint-v52-material-reload.json`.

The v53 fixture adds 26 authored-lighting controls with 27 semantic bindings,
point/projected lights, changed projection/falloff textures, material transforms,
vertex colors, conditions, multiple stages/lights, rotation and shadow/recovery
controls. Both v53e backend runs pass independently with exact image/shader/video
recovery and exact mapped-to-stencil equivalence on the custom receiver. Every
receiver comparison stays within one display level, including exact shadowed
receiver output. Projected-shadow full frames differ by at most one level. The strict
full-frame comparison still fails for three point-stencil background pixels
(maximum 30 levels) and nineteen point-map pixels (maximum five). The receiver
analysis only locates these failures; the original full-frame gate remains
failed. Reports and engine captures are retained under
`.tmp/vulkan-gap-closure/authored-lighting/`.

Development runs exposed fixture projection/removal errors, canonical transform
rounding holes, an overly restrictive stencil-admission check, the OpenGL
sampler-table conflict and a one-pixel Vulkan image-reload change. Those reports
remain failed evidence. The v53e sampler correction closes the last exact-recovery
failure without changing the threshold. Compiler qualification passes all 33
native cases and independently validates 24 newly generated modules with the
expanded uniform block; see `authored-lighting/compiler-qualification.json`.

The final v53e runtime also passes twenty ordinary authored-material comparisons
within one display level, with exact recovery, all 35 OpenGL HDR/bloom controls
and the three Vulkan render-target/startup, HDR and temporal-motion GPU groups.
These runs report no tracked API/engine diagnostics. Source/runtime identity,
the passing scoped checks and the failed full-frame comparison are retained in
`.tmp/vulkan-gap-closure/authored-lighting/checkpoint-v53-authored-lighting.json`.

The v56 [point-shadow correction](vulkan-point-shadow-parity.md#general-radial-depth-correction)
retains all 26 independent lighting controls and twenty ordinary material
comparisons. Its complete mapped-shadow image now stays within two display
levels of the unchanged GL reference. Three point-stencil background pixels
still fail, so the full lighting comparison remains failed overall. This
follow-up does not extend authored-shader admission or weaken the image gate.

Remaining work includes:

- Extend source-selection qualification to additional retail/mod source pairs,
  filesystem overlays, and long repeated-edit sessions.
- Qualify additional custom-lighting combinations, including mixed stages,
  translucent receivers, alpha coverage, animated geometry and resource failure.
  The generic evaluator now handles the per-light semantics above in addition
  to numeric registers, view origin, current render viewport/scale and supported
  post-process size/color-space bindings. Full-scene shadow parity remains open.
- Implement sampling from flipped render textures; current generic draws reject
  those images explicitly. Qualify offscreen/HDR, texture generation, alpha and
  blend state, vertex colors, decals and animated geometry.
- Extend the GLSL language subset and qualify descriptor/pipeline/uniform/cache
  exhaustion, repeated source edits and device-failure recovery.
- Implement and qualify authored ARB assembly programs. The GLSL compatibility
  compiler is not an ARB assembly translator.

The broader HDR/PBR/shadow, filtering, performance, CI, hardware and release
requirements in the [gap-closure plan](plans/2026-09-20-vulkan-gap-closure.md)
remain in force.

## Dependency provenance

[glslang 15.1.0](https://github.com/KhronosGroup/glslang/tree/15.1.0) is pinned by
archive SHA-256 in `subprojects/glslang.wrap`. Its BSD, MIT, Apache-2.0, GPLv3 with
Bison exception and permissive preprocessor notices were reviewed for this
GPLv3 repository before incorporation. The native Meson overlay selects the
upstream compiler/SPIR-V/resource-limit sources, without changing their source
or notices. Optional HLSL, SPIRV-Tools, remapper and upstream test dependencies
are excluded. The complete upstream license is retained in
`docs/licenses/glslang-LICENSE.txt` and included in the staging manifest under
`licenses/`; the README credits the original project.
