<div align="center">

# Building OpenQ4 from Source

</div>

This guide covers everything required to compile OpenQ4 from source on Windows, Linux, and macOS.

> [!NOTE]
> **Regular players do not need to build from source.** Download the latest release from the [Releases page](https://github.com/themuffinator/OpenQ4/releases) and follow the [Quick Start instructions](README.md#quick-start) instead.

---

## Table of Contents

- [Prerequisites](#prerequisites)
- [GameLibs Companion Repository](#gamelibs-companion-repository)
- [Build Setup](#build-setup)
- [Build Options](#build-options)
- [Building on Windows](#building-on-windows)
- [Building on Linux / macOS](#building-on-linux--macos)
- [Output Files](#output-files)
- [Packaging a Distributable](#packaging-a-distributable)

---

## Prerequisites

### Compiler

| Platform | Minimum | Notes |
|----------|---------|-------|
| **Windows** | MSVC 19.46+ (Visual Studio 2026+) | Use the Developer PowerShell or run `tools/build/openq4_devcmd.cmd` to initialise the environment |
| **Linux** | GCC 13+ or Clang 17+ | Distro packages are fine |
| **macOS** | Xcode 16+ / Clang 17+ | Install Command Line Tools via `xcode-select --install` |

### Build Tools

- **[Meson](https://mesonbuild.com/)** 1.2.0 or newer
- **[Ninja](https://ninja-build.org/)** (recommended backend)
- **Python 3** (used by `tools/build/sync_icons.py` and the wrapper scripts)

### Windows Note

On Windows, always invoke Meson through `tools/build/meson_setup.ps1` rather than calling `meson` directly from an arbitrary shell. The wrapper ensures MSVC tools (`cl.exe`, `link.exe`, etc.) are on `PATH` and performs automatic icon-set synchronisation before setup, compile, and install steps.

For Windows `arm64` builds, OpenQ4 also needs an ARM64 OpenAL Soft package. The release workflow prepares that automatically. For local builds, use `tools/build/prepare_windows_openal.ps1` to create one under `.tmp/`, then pass `-Dopenal_root_override=<path>` during `meson setup`.

```powershell
# Open a regular PowerShell window and use the wrapper:
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 <meson-command> [args...]
```

Alternatively, open `tools/build/openq4_devcmd.cmd` first to initialise the Visual Studio environment, then call `meson` directly.

---

## GameLibs Companion Repository

OpenQ4's game code (single-player and multiplayer modules) lives in a **separate companion repository** — [OpenQ4-GameLibs](https://github.com/themuffinator/OpenQ4-GameLibs). This separation clearly identifies the SDK-licensed components derived from the [Quake 4 SDK](https://www.moddb.com/games/quake-4/downloads/quake-4-sdk-v15).

> [!IMPORTANT]
> **The OpenQ4 build expects OpenQ4-GameLibs to be checked out alongside OpenQ4**, at `../OpenQ4-GameLibs` relative to this repository. If the companion repository is missing or at a different path, game-module builds will fail.

### Setting Up

```bash
# Clone both repositories side-by-side:
git clone https://github.com/themuffinator/OpenQ4.git
git clone https://github.com/themuffinator/OpenQ4-GameLibs.git

# Result:
#   ./OpenQ4/            ← this repository
#   ./OpenQ4-GameLibs/   ← game library source
```

To use a custom location, set the environment variable before configuring:

```bash
export OPENQ4_GAMELIBS_REPO=/path/to/OpenQ4-GameLibs   # Linux / macOS
$env:OPENQ4_GAMELIBS_REPO = "C:\path\to\OpenQ4-GameLibs"  # PowerShell
```

To rebuild the game libraries as part of the OpenQ4 build, set `OPENQ4_BUILD_GAMELIBS=1` before running compile.

---

## Build Setup

Third-party libraries such as SDL3, GLEW, OpenAL Soft, and stb_vorbis are managed as Meson subprojects. Linux still requires the usual native development packages for X11/OpenGL plus SDL3 runtime integrations before the first configure step. On current Debian/Ubuntu systems, install the CI-aligned SDL3/Linux package set (`libasound2-dev`, `libdbus-1-dev`, `libdecor-0-dev`, `libdrm-dev`, `libegl1-mesa-dev`, `libfribidi-dev`, `libgbm-dev`, `libgl1-mesa-dev`, `libibus-1.0-dev`, `libjack-dev`, `libopenal-dev`, `libpipewire-0.3-dev`, `libpulse-dev`, `libsndio-dev`, `libthai-dev`, `libudev-dev`, `libwayland-dev`, `libx11-dev`, `libxcursor-dev`, `libxext-dev`, `libxfixes-dev`, `libxi-dev`, `libxkbcommon-dev`, `libxrandr-dev`, `libxss-dev`, `libxtst-dev`, `libxxf86dga-dev`, and `libxxf86vm-dev`).

The experimental `OpenQ4-rhi-probe_<arch>` target is the current exception: it compiles NVRHI directly from a recursive NVRHI checkout instead of an in-repo Meson subproject while backend migration work is still in bootstrap.

---

## Build Options

Pass any of these with `-D<option>=<value>` on the `meson setup` command line:

| Option | Default | Description |
|--------|---------|-------------|
| `build_engine` | `true` | Build `OpenQ4-client_<arch>` and `OpenQ4-ded_<arch>` |
| `build_games` | `true` | Build game modules |
| `build_game_sp` | `true` | Build single-player game module |
| `build_game_mp` | `true` | Build multiplayer game module |
| `platform_backend` | platform-dependent | `sdl3` or `legacy_win32` on Windows, `sdl3` or `native` on Linux, `native` on macOS |
| `build_nvrhi_probe` | `false` | Build the experimental `OpenQ4-rhi-probe_<arch>` DX12/Vulkan bootstrap utility through NVRHI |
| `build_engine_nvrhi` | `false` | Compile the experimental NVRHI bootstrap path into `OpenQ4-client_<arch>` and expose developer smoke-test commands such as `gfxApiProbe` and `gfxApiProbeStart` |
| `nvrhi_repo_override` | *(empty)* | Optional path to a recursive NVRHI checkout for the probe; if empty, Meson looks in `.tmp/nvrhi`, `.tmp/nvrhi-inspect`, and `subprojects/nvrhi` |
| `version_track` | `dev` | Build track label (`stable`, `dev`, `beta`, `rc`) |
| `version_iteration` | *(empty)* | Dot-separated iteration counter for pre-release builds |
| `version_base_override` | *(empty)* | Override the generated release version without editing `meson.build` |
| `enforce_msvc_2026` | `false` | Enforce MSVC 2026+ requirement (Windows only) |

---

## Building on Windows

> [!NOTE]
> Release packaging targets the static MSVC CRT so end users do not need a separate Visual C++ Redistributable install.

### Debug Build

```powershell
# 1. Configure
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 setup --wipe builddir . --backend ninja --buildtype=debug --wrap-mode=forcefallback

# 2. Compile
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 compile -C builddir

# 3. Run directly from builddir
builddir\OpenQ4-client_<arch>.exe
```

### Release Build

```powershell
# 1. Configure
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 setup builddir . --backend ninja --buildtype=release --wrap-mode=forcefallback

# 2. Compile
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 compile -C builddir

# 3. Stage distributable package into .install/
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects
```

### From a Visual Studio Developer Command Prompt

Once the MSVC environment is initialised you can call `meson` directly:

```batch
meson setup builddir . --backend ninja --buildtype=release
meson compile -C builddir
```

---

## Building on Linux / macOS

> [!NOTE]
> As of March 30, 2026, Linux defaults to the SDL3 backend. `-Dplatform_backend=native` remains available as the fallback Linux path. On Steam Deck or other mixed Wayland/X11 sessions, `OpenQ4-steamdeck` prefers XWayland when both `WAYLAND_DISPLAY` and `DISPLAY` are present.

### Debug Build

```bash
# 1. Configure
bash tools/build/meson_setup.sh setup --wipe builddir . --backend ninja --buildtype=debug --wrap-mode=forcefallback

# 2. Compile
bash tools/build/meson_setup.sh compile -C builddir

# 3. Run directly from builddir
./builddir/OpenQ4-client_<arch>
```

Use `-Dplatform_backend=native` during setup if you need to compare against the legacy Linux X11/GLX backend.

### Release Build

```bash
# 1. Configure
bash tools/build/meson_setup.sh setup builddir . --backend ninja --buildtype=release --wrap-mode=forcefallback

# 2. Compile
bash tools/build/meson_setup.sh compile -C builddir

# 3. Stage distributable package into .install/
bash tools/build/meson_setup.sh install -C builddir --no-rebuild --skip-subprojects
```

---

## Output Files

### Build directory (`builddir/`)

| File | Description |
|------|-------------|
| `OpenQ4-client_<arch>[.exe]` | Main engine executable |
| `OpenQ4-ded_<arch>[.exe]` | Dedicated server |
| `OpenQ4-rhi-probe_<arch>[.exe]` | Experimental NVRHI DX12/Vulkan bootstrap utility when `-Dbuild_nvrhi_probe=true` |
| `baseoq4/game-sp_<arch>[.dll/.so/.dylib]` | Single-player game module |
| `baseoq4/game-mp_<arch>[.dll/.so/.dylib]` | Multiplayer game module |

- BSE (Basic Set of Effects) is linked directly into `OpenQ4-client_<arch>`; the dedicated server keeps a disabled/stub path.
- On Windows, the wrapper stages `OpenAL32.dll` next to the executables and rejects builds that still depend on external MSVC/UCRT runtime DLLs.

### Install directory (`.install/`)

After running the install step, `.install/` is a self-contained distributable package:

```
.install/
├── OpenQ4-client_<arch>[.exe]  # Main executable
├── OpenQ4-ded_<arch>[.exe]     # Dedicated server
├── OpenQ4-steamdeck            # (Linux) Steam Deck launcher
├── OpenAL32.dll                # (Windows) runtime dependency
├── share/applications/         # (Linux) desktop entries
└── baseoq4/
    ├── game-sp_<arch>[.dll/.so/.dylib]   # Single-player module
    ├── game-mp_<arch>[.dll/.so/.dylib]   # Multiplayer module
    ├── openq4_defaults.cfg            # OpenQ4-owned default binds
    └── openq4_profile_steamdeck.cfg   # Steam Deck profile overrides
```

> [!NOTE]
> Do not distribute raw `buildtype=debug` artifacts in public packages. MSVC import libraries (`*.lib`) are development-only artifacts and are not required in the package.

Repo-authored runtime overrides live under `content/baseoq4/`. The install step stages that source-owned content into the runtime `baseoq4/` directory inside `.install/`.

---

## Experimental NVRHI Probe

`OpenQ4-rhi-probe_<arch>` is a developer bring-up harness, not a gameplay-ready renderer. It creates a native DX12 or Vulkan swap chain, wraps that swap chain through NVRHI, and repeatedly clears/presents frames so backend initialization problems can be isolated before the engine renderer is migrated.

The optional `-Dbuild_engine_nvrhi=true` path extends that bootstrap into the main client executable. When enabled on an SDL3 build, `OpenQ4-client_<arch>` exposes:

- `r_graphicsApi` / `listGraphicsApis` / `gfxInfo` reporting for requested vs runtime API state
- `r_graphicsApiBootstrap`, `r_graphicsApiBootstrapFrames`, `r_graphicsApiBootstrapHidden`, and `r_graphicsApiBootstrapVsync` for opt-in automatic engine-side bootstrap when `r_graphicsApi` requests `d3d12` or `vulkan`
- `gfxApiProbe [auto|d3d12|vulkan] [frames] [hidden 0|1] [vsync 0|1]` for an engine-side NVRHI smoke test
- `gfxApiProbeStart [auto|d3d12|vulkan] [hidden 0|1] [vsync 0|1]`, `gfxApiProbeStop`, and `gfxApiProbeStatus` for a persistent NVRHI bootstrap session that ticks once per engine frame

This still does not make gameplay rendering run on DX12/Vulkan. The real renderer continues to fall back to OpenGL.

Clone NVRHI recursively into `.tmp/nvrhi` or another path before enabling the target:

```powershell
git clone --depth 1 --recursive https://github.com/NVIDIA-RTX/NVRHI .tmp\nvrhi
```

Windows DX12 probe-only configure/build:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 setup --wipe .tmp\builddir-nvrhi-probe . --backend ninja --buildtype=debug --wrap-mode=forcefallback -Dbuild_engine=false -Dbuild_games=false -Dbuild_nvrhi_probe=true -Dnvrhi_repo_override=.tmp\nvrhi
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 compile -C .tmp\builddir-nvrhi-probe
.tmp\builddir-nvrhi-probe\OpenQ4-rhi-probe_<arch>.exe --api=d3d12 --frames=300
```

Windows client build with engine-side NVRHI bootstrap enabled:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 setup --wipe .tmp\builddir-engine-nvrhi . --backend ninja --buildtype=debug --wrap-mode=forcefallback -Dbuild_games=false -Dbuild_engine_nvrhi=true -Dnvrhi_repo_override=.tmp\nvrhi
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 compile -C .tmp\builddir-engine-nvrhi OpenQ4-client_<arch>
.tmp\builddir-engine-nvrhi\OpenQ4-client_<arch>.exe +gfxApiProbe d3d12 120 1 1
.tmp\builddir-engine-nvrhi\OpenQ4-client_<arch>.exe +gfxApiProbeStart d3d12 0 1
.tmp\builddir-engine-nvrhi\OpenQ4-client_<arch>.exe +set r_graphicsApi d3d12 +set r_graphicsApiBootstrap session
.tmp\builddir-engine-nvrhi\OpenQ4-client_<arch>.exe +set r_graphicsApi vulkan +set r_graphicsApiBootstrap probe +set r_graphicsApiBootstrapFrames 300 +set r_graphicsApiBootstrapHidden 1
```

Linux Vulkan probe-only configure/build:

```bash
git clone --depth 1 --recursive https://github.com/NVIDIA-RTX/NVRHI .tmp/nvrhi
bash tools/build/meson_setup.sh setup --wipe .tmp/builddir-nvrhi-probe . --backend ninja --buildtype=debug --wrap-mode=forcefallback -Dbuild_engine=false -Dbuild_games=false -Dbuild_nvrhi_probe=true -Dnvrhi_repo_override=.tmp/nvrhi
bash tools/build/meson_setup.sh compile -C .tmp/builddir-nvrhi-probe
./.tmp/builddir-nvrhi-probe/OpenQ4-rhi-probe_<arch> --api=vulkan --frames=300
```

Notes:

- Windows can build the DX12 and Vulkan NVRHI probe paths without `vulkan-1.lib`; Vulkan is loaded at runtime through SDL/NVRHI instead of a configure-time import-library dependency.
- Linux can also compile the Vulkan NVRHI probe path without `libvulkan` development files, but running it still requires a working Vulkan loader/driver installation on the target machine.
- `build_engine_nvrhi=true` currently requires `-Dplatform_backend=sdl3` so the client and bootstrap path share SDL3 window management.
- The SDL3 backend now filters keyboard, mouse, text, and window events by window ID, so a visible `gfxApiProbeStart` session no longer drives the main game window. Closing the bootstrap window stops that session without quitting the game.
- Visible `gfxApiProbeStart` windows now try to open beside the main SDL3/OpenGL client window, which makes side-by-side DX12/Vulkan bring-up easier during live engine runs.
- Engine-side `gfxApiProbe` and `gfxApiProbeStart` now default their bootstrap window size to the live SDL3/OpenGL client pixel size when available, and `gfxApiProbeStatus` reports the active bootstrap window ID plus current size.
- `r_graphicsApiBootstrap=probe|session` is opt-in and only runs when `r_graphicsApi` explicitly requests `d3d12` or `vulkan`; the gameplay renderer still falls back to OpenGL after the bootstrap work completes or starts.
- The main OpenQ4 client still renders through the existing OpenGL renderer. The probe and engine-side bootstrap command are phase 1/2 bring-up work for a later renderer migration.

---

## Packaging a Distributable

The `meson install` step (via the wrapper) stages all required binaries into `.install/`. This directory can be zipped and distributed as a release archive.

Release archives also generate a packaged offline HTML documentation site under `docs/`. If you run the release packager manually instead of using GitHub Actions, make sure `python -m pip install markdown` is available in the same environment.

The manually dispatched GitHub release workflow publishes architecture-qualified release assets such as `openq4-<version>-windows-x64.zip`, `openq4-<version>-windows-arm64.zip`, `openq4-<version>-linux-x64.tar.xz`, `openq4-<version>-linux-arm64.tar.xz`, and `openq4-<version>-macos-arm64.tar.gz`. Windows release payloads also get native installer executables such as `openq4-<version>-windows-x64-setup.exe` and `openq4-<version>-windows-arm64-setup.exe`. Each installer is compiled from the already-packaged Windows release directory so its file set matches the archive instead of diverging from it, writes install metadata to the registry for upgrade detection, registers a normal Windows uninstaller entry, and can optionally register `openq4://` browser links.

If you want to build that installer manually on Windows after packaging a release directory, install [Inno Setup](https://jrsoftware.org/isinfo.php) and run:

```powershell
python tools/build/build_windows_installer.py --package-dir .tmp\openq4-0.1.010-windows-arm64 --version 0.1.010 --version-tag 0.1.010 --arch arm64 --output-dir .tmp\release-artifacts
```

To include the icon set synchronisation step before building (validated and generated by the wrapper automatically):

```powershell
# Set OPENQ4_SKIP_ICON_SYNC=1 to bypass icon sync in automated/CI workflows
$env:OPENQ4_SKIP_ICON_SYNC = "1"
```

The manually dispatched GitHub release workflow builds with `version_track=stable` and injects `version_base_override` from `tools/build/openq4_release_version.py`. That helper uses the repo version in `meson.build` as the release floor and, once stable `v*` tags exist, automatically chooses between a serial bump (`0.1.010` -> `0.1.011`) and a milestone bump (`0.1.010` -> `0.2.000`) based on the scale of changes since the previous release. The workflow also accepts manual `bump_mode` and `version_override` inputs when a release needs an explicit decision.

---

[← Back to README](README.md)
