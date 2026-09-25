# Vulkan authored probe sources

`vk_ProbeSource` supplies the GPU-to-CPU source bridge for the native authored
probe atlas. Residency, shared CPU selection and shader consumption are now
connected and undergoing [rendering qualification](vulkan-pbr-probes.md).
The source-reader evidence below qualifies the bridge independently of that
consumer. See the complete
[Vulkan gap-closure requirements](plans/2026-09-20-vulkan-gap-closure.md).

## Source contract

`VK_ProbeSource_Read` accepts an immutable, loaded, non-defaulted square cubemap
up to 256 texels per face. Every base face must have a successful complete
upload. Scratch/render images, incomplete images, invalid storage, unsupported
encodings and nonfinite HDR radiance fail explicitly. No partial cube or valid
stamp is returned on failure.

The current authored material loader requests uncompressed `TD_HIGH_QUALITY`
RGBA8 cubemaps. The reader also supports RGBA8 sRGB storage and generated
RGBA16F sources. Other encodings, including compressed probe sources, remain
explicitly unsupported. Ordinary compressed texture uploads are separate from
probe-source admission.

The read copies mip zero in native `+X,-X,+Y,-Y,+Z,-Z` order. Ordinary color
bytes undergo the same sRGB-to-linear conversion as the GL probe atlas. The
transfer reads encoded storage even for an sRGB image, so conversion occurs
once. HDR values retain their finite positive range; negative values clamp to
zero. Alpha does not enter environment radiance.

The stamp binds image handle, Vulkan image generation, shared storage
generation, upload generation and face dimensions. An in-place image upload
invalidates the old stamp even when allocation and handle remain unchanged.
Consumers must verify `VK_ProbeSource_Matches` before publishing filtered data.

## Transfer ownership

A cache miss appends the six-face copy after pending image uploads on the
existing graphics/upload queue. It restores the original shader-readable
layout, waits for the upload fence, invalidates mapped host memory and checks
the source stamp again before publishing the complete decoded cube. The host
readback buffer is owned by this operation; the upload batch does not release
it. A failed wait retains normal deferred destruction.

This synchronous operation must be restricted to probe cache misses during
resource preparation. It does not submit, close, reset or replace the current
scene command buffer. It is not a general framebuffer readback interface, and
does not promise recovery from device loss. The native atlas calls it only
for a newly resident or changed authored source.

The shared Vulkan image uploader now rejects invalid mip/layer indices,
negative offsets, out-of-bounds rectangles and invalid row pitches before
allocation or command recording. Compressed uploads retain block padding in
their byte payload while copying only the physical mip extent. This preserves
the BinaryImage convention for padded 2x2/1x1 cubemap mip tails. Compressed
offsets and interior extents must meet block alignment.

## Regression controls

`rendererVulkanRenderTargetsSelfTest` includes real source reads for RGBA8,
sRGB RGBA8 and RGBA16F at 1, 8 and 256 texels per face. Asymmetric face/row/column
patterns distinguish face order, flips and transposes; distinct lower mips
detect the wrong mip being read. Expected linear values are independent
constants, including 0.25, 2, 4096 and 65504 for HDR and negative-HDR clamping.
The test repeats after image reload and purge/load, verifies an in-place
overwrite, and reads while a scene command buffer remains open.

Negative controls require cleared output for null, purged, 2D, oversized,
mutable, incomplete, unsupported and failed-upload sources, plus positive
infinity, negative infinity and NaN in HDR data. Malformed upload controls
also require unchanged queue counts and content generations. On devices with
BC support, eight further GPU readbacks verify every face and mip of BC1/BC3
cubes, including padded 2x2/1x1 tails; compressed probe decoding stays rejected.

The mandatory startup case requires the source-test result. The separate
`renderer-vk-probe-source-lifecycle` case repeats it around partial and full
video restart with an MSAA change, requires ordered completion markers and
active Vulkan validation, and rejects a partial restart that silently falls
back to full. Both hosted Vulkan workflow selections include that case.
Hosted execution and other physical GPUs/platforms remain unqualified here.

## Local qualification

The v15 candidate passes `renderer-vk-clear-startup` and
`renderer-vk-probe-source-lifecycle` with active validation on Windows,
NVIDIA RTX 4060 Laptop GPU, Vulkan 1.4.325. Each source-test execution passes
29 pattern controls covering 3,543,222 texels, 32 rejection controls and
eight BC1/BC3 mip readbacks. The lifecycle case executes the source test
three times and proves the ordered partial/full restart sequence. The
surrounding attachment, MRT, MSAA, scratch reload and emission tests also pass.

Evidence root: `.tmp/vulkan-gap-closure/probe-source/`. The final candidate's
Vulkan module SHA-256 is
`5d50c7f7918aedc2246c4eca76bf54a59a3322b2c741270f3b936c42e09407aa`.
The client, GL module and other runtime inputs remain those of the retained
v12 package. These resource tests do not prove native authored-probe lighting.

Both final 27-control environment gameplay suites pass at 0x and 4x MSAA,
including image/video lifecycle and transparency. All 54 complete engine TGA
captures are byte-identical to v12. `checkpoint-v15.json` pins those reports,
reference comparisons, the GPU reports, all 50 runtime files, the current
source worktree and the unchanged companion revision. Builddir, staging and
the independent v15 runtime contain the same tested renderer binary.

The earlier v13/v14 diagnostic patterns allowed duplicate faces. V15 assigns
separate RGB face bits and mixes spatial coordinates; independent pattern
checks prove six distinct faces at all tested extents and detect nine flip,
transpose, rotation and stride mutations. `pattern-quality.json` retains both
the old aliases and the new negative-control counts. V14's production source
reader and upload implementation are unchanged in v15; the diagnostic pattern
and test allocation guard are the only native-code differences.
