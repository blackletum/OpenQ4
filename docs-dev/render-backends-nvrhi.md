# OpenQ4 NVRHI Backend Plan

This document tracks the staged DX12 / Vulkan migration plan for OpenQ4.

## Current State

As of April 15, 2026:

- The OpenQ4 engine still renders real gameplay through the existing OpenGL renderer.
- The main client now exposes a developer-facing `r_graphicsApi` cvar plus `listGraphicsApis` / `gfxInfo` reporting, and optional `build_engine_nvrhi=true` builds add both opt-in automatic bootstrap cvars (`r_graphicsApiBootstrap*`), the blocking `gfxApiProbe` helper, and a persistent `gfxApiProbeStart` / `gfxApiProbeStop` / `gfxApiProbeStatus` session; any non-OpenGL request still resolves back to OpenGL for real gameplay rendering.
- The first NVRHI landing is `OpenQ4-rhi-probe_<arch>`, an experimental developer utility that:
  - creates a real native swap chain
  - creates a real NVRHI device
  - wraps swap-chain images through NVRHI
  - clears and presents frames repeatedly

This is intentional. The current renderer is deeply OpenGL-specific across `tr_*`, image upload, render textures, vertex cache management, and platform `GLimp_*` code. Pretending DX12/Vulkan are ready before those layers are migrated would create a misleading half-backend.

## Why NVRHI

NVRHI is the target abstraction for future non-OpenGL renderer work because it gives OpenQ4:

- one rendering API surface for DX12 and Vulkan
- explicit resource-state tracking without writing two separate barrier systems
- native escape hatches for backend-specific work when needed
- a realistic migration path informed by other idTech-derived projects

## What Landed In Phase 1

Phase 1 is bootstrap only:

- Meson options:
  - `-Dbuild_nvrhi_probe=true`
  - `-Dbuild_engine_nvrhi=true`
  - `-Dnvrhi_repo_override=<path>`
- Shared renderer-owned bootstrap sources under `src/renderer/NVRHI/` plus `src/renderer/GraphicsAPI.cpp`
- Shared SDL/native-window helpers under `src/sys/GraphicsWindow.*`, now owning reusable SDL video-subsystem, runtime-loader, and raw window creation/destruction helpers for both the OpenGL host path and NVRHI bootstrap sessions
- Source-side NVRHI compilation for the probe from a recursive NVRHI checkout
- Windows DX12 probe backend
- Windows/Linux Vulkan probe backend built through SDL/NVRHI runtime loading instead of a hard Vulkan import-library dependency at Meson configure time
- A standalone `OpenQ4-rhi-probe_<arch>` executable for backend smoke tests
- Engine-facing `r_graphicsApi` state/reporting in `gfxInfo` and `listGraphicsApis`, plus optional `gfxApiProbe` smoke testing and a persistent `gfxApiProbeStart` / `gfxApiProbeStop` / `gfxApiProbeStatus` bootstrap session in NVRHI-enabled client builds, with explicit fallback to OpenGL until the draw backend is migrated
- Archived `r_graphicsApiBootstrap`, `r_graphicsApiBootstrapFrames`, `r_graphicsApiBootstrapHidden`, and `r_graphicsApiBootstrapVsync` cvars for repeatable startup bring-up when `r_graphicsApi` requests DX12/Vulkan

The probe is developer-only and is not installed into `.install/`.

## Build Workflow

Clone NVRHI recursively first:

```powershell
git clone --depth 1 --recursive https://github.com/NVIDIA-RTX/NVRHI .tmp\nvrhi
```

Windows example:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 setup --wipe .tmp\builddir-nvrhi-probe . --backend ninja --buildtype=debug --wrap-mode=forcefallback -Dbuild_engine=false -Dbuild_games=false -Dbuild_nvrhi_probe=true -Dnvrhi_repo_override=.tmp\nvrhi
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 compile -C .tmp\builddir-nvrhi-probe
.tmp\builddir-nvrhi-probe\OpenQ4-rhi-probe_<arch>.exe --api=d3d12 --frames=300
```

Optional Windows client example with engine-side bootstrap enabled:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 setup --wipe .tmp\builddir-engine-nvrhi . --backend ninja --buildtype=debug --wrap-mode=forcefallback -Dbuild_games=false -Dbuild_engine_nvrhi=true -Dnvrhi_repo_override=.tmp\nvrhi
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 compile -C .tmp\builddir-engine-nvrhi OpenQ4-client_<arch>
.tmp\builddir-engine-nvrhi\OpenQ4-client_<arch>.exe +gfxApiProbe d3d12 120 1 1
.tmp\builddir-engine-nvrhi\OpenQ4-client_<arch>.exe +gfxApiProbeStart d3d12 0 1
.tmp\builddir-engine-nvrhi\OpenQ4-client_<arch>.exe +set r_graphicsApi d3d12 +set r_graphicsApiBootstrap session
.tmp\builddir-engine-nvrhi\OpenQ4-client_<arch>.exe +set r_graphicsApi vulkan +set r_graphicsApiBootstrap probe +set r_graphicsApiBootstrapFrames 300 +set r_graphicsApiBootstrapHidden 1
```

Linux example:

```bash
git clone --depth 1 --recursive https://github.com/NVIDIA-RTX/NVRHI .tmp/nvrhi
bash tools/build/meson_setup.sh setup --wipe .tmp/builddir-nvrhi-probe . --backend ninja --buildtype=debug --wrap-mode=forcefallback -Dbuild_engine=false -Dbuild_games=false -Dbuild_nvrhi_probe=true -Dnvrhi_repo_override=.tmp/nvrhi
bash tools/build/meson_setup.sh compile -C .tmp/builddir-nvrhi-probe
./.tmp/builddir-nvrhi-probe/OpenQ4-rhi-probe_<arch> --api=vulkan --frames=300
```

Notes:

- Windows DX12 probe support does not require Vulkan import libraries.
- Vulkan probe support now compiles on Windows and Linux without a Vulkan loader import library at configure time; the runtime still needs a working Vulkan loader/driver installation.
- The current Vulkan path uses SDL's loader entry points plus Vulkan-Hpp dynamic dispatch so the probe and optional engine bootstrap can share one runtime-loaded Vulkan entry path.
- `build_engine_nvrhi=true` currently requires `platform_backend=sdl3`.
- The SDL3 backend now filters per-window keyboard, mouse, text, and focus traffic so a visible `gfxApiProbeStart` session can coexist with the main OpenQ4 window. Closing the bootstrap window stops that session instead of quitting the game.
- Visible engine-side bootstrap windows now position themselves beside the main SDL3/OpenGL game window when possible, instead of appearing arbitrarily on the desktop.
- Engine-side `gfxApiProbe` / `gfxApiProbeStart` now inherit the live SDL3/OpenGL client pixel size when a primary game window exists, and `gfxApiProbeStatus` reports the active bootstrap window ID plus current pixel size.
- `r_graphicsApiBootstrap=probe|session` is intentionally opt-in. It reuses the requested `r_graphicsApi` backend for startup validation, then leaves actual gameplay rendering on the existing OpenGL path.

## Planned Migration Stages

1. Bootstrap: complete.
   Land a standalone NVRHI utility that proves device + swap-chain creation for DX12/Vulkan.

2. Engine-side platform abstraction.
   Pull native-window and swap-chain management into reusable engine services instead of probe-local code.

3. Resource migration.
   Move image upload, render targets, framebuffers, and transient buffers behind backend-neutral interfaces.

4. Draw backend migration.
   Replace direct GL command emission in the core renderer with NVRHI command-list recording.

5. Feature parity and validation.
   Re-enable gameplay rendering, post-process paths, dynamic lights, shadowing, GUI rendering, screenshots, and RenderDoc/debug tooling on the new backends.

## Non-Goals For Phase 1

- No DX12/Vulkan toggle in the shipping game menus
- No promise of gameplay correctness outside the probe utility
- No partial engine path that initializes DX12/Vulkan and then silently falls back to OpenGL draw code

That constraint is deliberate. A clean migration path is more valuable than a misleading runtime toggle that cannot render real scenes yet.
