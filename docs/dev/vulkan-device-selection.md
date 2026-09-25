# Vulkan device admission

Automatic selection now checks a candidate's required capabilities before
publishing it to the renderer. Previously, the first graphics/present-capable
adapter could be chosen and then rejected for its API version or descriptor
limit, sending the entire launch back to OpenGL even when a later GPU was usable.

The renderer requires Vulkan 1.3, eight bound descriptor sets, dynamic rendering,
synchronization2 and the swapchain extension. It also checks for a nonempty
graphics/present queue with a successful surface-support query, color-attachment
surface usage, a compatible SDR surface format and a depth/stencil attachment
format. Optional features such as depth clamp, BC compression and programmable
sample locations retain their existing negotiation and fallbacks.

`r_vkDevice -1` takes the first admitted candidate in driver enumeration order.
An explicit index is checked alone; an unavailable or incompatible selection
fails without silently choosing another GPU. Rejected devices are logged with
their index, name, API version, descriptor limit and the failing check. This
does not rank GPUs by speed or change the default renderer.

The production selector uses instance query function pointers and does not
create a logical device or mutate renderer globals. Its native tests use the
same compiled library with fake driver responses. Device, extension and surface
format lists retry incomplete enumeration up to four times; failed or partial
results cannot become a successful selection. Admission and swapchain creation
share the SDR format selector, including the legacy undefined-format fallback.
An sRGB attachment is never substituted for display-coded UNORM output.

## Preliminary capability probe

The startup gate and `rendererVkProbe` now use the same compiled selection
library for their physical-device inventory. They read complete device and
device-extension lists with bounded retries, allocate queue-family storage
from the reported count, and ignore empty queues. The probe shares the
renderer's API-version and descriptor-set limits. It keeps its existing
logical-device, memory-allocation and optional timeline-semaphore checks.

Quiet startup stops at the first suitable candidate. Verbose diagnostics
inspect the full inventory and rank only suitable candidates by the existing
device-type, memory and transfer-queue preferences. A forced index queries
that device alone in either mode; an unavailable or incompatible choice fails
without switching GPUs. Optional capabilities remain optional, portability
support comes from the same extension inventory, and diagnostic core feature
queries respect the adapter's advertised API version.

This probe creates no window or surface, so presentation admission remains
the responsibility of the later renderer selector. Its preference score is
diagnostic and does not change automatic renderer selection order.

## Validation

The v61 Windows build passes all 43 renderer-selection cases and 31 new probe
cases through the production library. Probe cases include failed and changing
lists, bounded incomplete-enumeration retries, entries beyond the old caps,
empty queues, explicit choices, optional capabilities, older API versions and
an unsuitable high-memory device that must never win the diagnostic score.
The four related source contracts also pass. Current-round evidence is under
`.tmp/vulkan-gap-closure/probe-admission/`.

All eight staged startup/GPU/recovery cases pass again in v61. Six additional
local controls exercise the real capability probe: full verbose inventory,
forced NVIDIA quiet and Intel verbose probes, rejection of a Vulkan 1.2
translation adapter, an unavailable explicit index, and same-process OpenGL
startup recovery for that unavailable choice. The forced verbose probes query
only the requested device. Runtime hashes stay unchanged, and no Vulkan
validation or tracked renderer warnings appear in those six controls. The
Intel control takes 101 seconds including application startup and shutdown;
this is functional evidence, not a startup-performance pass.

The v61 stock Air Defense 1 and Q4DM1 runs at 0x MSAA also pass HDR on/off,
exposure, temporal scaling, resize and full/partial video-restart checks.
All 16 engine captures have the expected dimensions and nonblank contents;
representative initial and final views are inspected. Neither run reports
tracked renderer warnings, and the complete staged package stays unchanged.
These are hidden, windowed, input-disabled functional runs. Existing stock
MP asset/navigation warnings remain, and full visible parity is still open.

The v60 Windows build passes 43 native selection cases. They cover incompatible
devices before a valid device, explicit choices, unsupported features and
formats, failed queries, changing/incomplete enumeration, queues and formats
beyond the old fixed arrays, and clearing stale results after failure. The
eight staged startup, GPU self-test and recovery cases also pass; the explicit
driver-failure drill contains its expected injected failure diagnostic.

Four stock SP/MP gameplay profiles at 0x/4x MSAA pass capture, HDR, resize and
video-restart checks, producing 32 engine screenshots with no tracked renderer
warnings. Two more stock gameplay runs successfully recover to OpenGL after an
injected mandatory-resource failure, with one capture each. Both harnesses
verify unchanged runtime files during testing. These hidden, windowed runs
have input disabled; they establish local functional evidence, not visible
AA/PBR parity or performance qualification. Representative captures are also
inspected for complete gameplay views. Existing stock MP asset/AAS warnings
remain separate from the renderer warning checks.

Evidence is retained under `.tmp/vulkan-gap-closure/device-admission/`.
The native test is registered as `openq4-vulkan-device-selection` with Meson
when Vulkan and `build_native_tests` are enabled, so normal native-test runs
include it without a GPU or Vulkan loader.

## Remaining scope

The probe's earlier instance-layer and instance-extension enumeration still
uses fixed arrays and needs an error-handling audit. The renderer's separate
optional-device-extension lookup also remains bounded. Retrying other
adapters after later device/resource-creation failures is another open item.
The public `r_vkDevice` override still has its existing maximum index of 15,
although automatic enumeration no longer has the old sixteen-device cap.
Native fake-driver tests do not establish physical GPU, Linux or MoltenVK
qualification.

This startup correction does not resolve the observed cross-adapter frame
stalls, remaining PBR/material/shadow comparisons, or the broader
[Vulkan qualification requirements](plans/2026-09-20-vulkan-gap-closure.md).
