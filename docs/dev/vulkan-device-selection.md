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

## Validation

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

The preliminary no-window capability probe still has its own diagnostic
scoring, fixed-size enumeration and logical-device probe. It cannot check
surface support. Its enumeration/error handling and retrying other adapters
after later device/resource-creation failures remain audit items. Native fake
driver tests are not evidence of physical multi-GPU, Linux or MoltenVK support.

This startup correction does not resolve the observed cross-adapter frame
stalls, remaining PBR/material/shadow comparisons, or the broader
[Vulkan qualification requirements](plans/2026-09-20-vulkan-gap-closure.md).
