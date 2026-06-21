<div align="center">

# Building openQ4 from Source

</div>

This guide covers everything required to compile openQ4 from source on Windows, Linux, and macOS.

> [!NOTE]
> **Regular players do not need to build from source.** Download the latest release from the [Releases page](https://github.com/themuffinator/openQ4/releases) and follow the [Getting Started instructions](README.md#getting-started) instead.

---

## Table of Contents

- [Prerequisites](#prerequisites)
- [GameLibs Companion Repository](#gamelibs-companion-repository)
- [Build Setup](#build-setup)
- [Build Options](#build-options)
- [Validation Scripts](#validation-scripts)
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
- **Python 3** (used by `tools/build/sync_icons.py`, `baseoq4/pak0.pk4` / `baseoq4/pak1.pk4` generation, and the wrapper scripts)

### Windows Note

On Windows, always invoke Meson through `tools/build/meson_setup.ps1` rather than calling `meson` directly from an arbitrary shell. The wrapper ensures MSVC tools (`cl.exe`, `link.exe`, etc.) are on `PATH` and performs automatic icon-set synchronisation before setup, compile, and install steps.

For Windows `arm64` builds, openQ4 also needs an ARM64 OpenAL Soft package. The release workflow prepares that automatically. For local builds, use `tools/build/prepare_windows_openal.ps1` to create one under `.tmp/`, then pass `-Dopenal_root_override=<path>` during `meson setup`.

```powershell
# Open a regular PowerShell window and use the wrapper:
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 <meson-command> [args...]
```

Alternatively, open `tools/build/openq4_devcmd.cmd` first to initialise the Visual Studio environment, then call `meson` directly.

---

## GameLibs Companion Repository

openQ4's game code (single-player and multiplayer modules) lives in a **separate companion repository** — [openQ4-game](https://github.com/themuffinator/openQ4-game). This separation clearly identifies the SDK-licensed components derived from the [Quake 4 SDK](https://www.moddb.com/games/quake-4/downloads/quake-4-sdk-v15), and makes openQ4-game the canonical source-input repository for SDK/game-library code.

> [!IMPORTANT]
> **The openQ4 build expects openQ4-game to be checked out alongside openQ4**, at `../openQ4-game` relative to this repository. If the companion repository is missing or at a different path, game-module builds will fail.

### Setting Up

```bash
# Clone both repositories side-by-side:
git clone https://github.com/themuffinator/openQ4.git
git clone https://github.com/themuffinator/openQ4-game.git

# Result:
#   ./openQ4/            ← this repository
#   ./openQ4-game/   ← game library source
```

To use a custom location, set the environment variable before configuring:

```bash
export OPENQ4_GAMELIBS_REPO=/path/to/openQ4-game   # Linux / macOS
$env:OPENQ4_GAMELIBS_REPO = "C:\path\to\openQ4-game"  # PowerShell
```

To rebuild the game libraries as part of the openQ4 build, set `OPENQ4_BUILD_GAMELIBS=1` before running compile.

During configure, openQ4 stages the required game-library source inputs from openQ4-game into `.tmp/gamelibs_stage/` and writes a source manifest with file hashes and git state. Meson requires that manifest before compiling the game modules, so Linux/macOS builds consume the companion repository directly without maintaining a local `src/game` mirror. The standalone openQ4-game build remains useful for Windows/MSVC developer outputs and compatibility checks, but the cross-platform engine build owns the staged module build.

---

## Build Setup

Third-party libraries such as SDL3, GLEW, OpenAL Soft, and stb_vorbis are managed as Meson subprojects. On Linux, the default SDL3 backend requires OpenGL plus the SDL3 runtime integration development packages, while X11/Xext are optional helpers used only when available for the SDL3 NVIDIA VRAM probe. The legacy `-Dplatform_backend=native` path still requires X11/GLX and VidMode development packages on Linux and the legacy Carbon framework on macOS. On macOS, SDL3 is the default platform path and does not link Carbon; `-Dmacos_graphics_bridge=metal` enables the Metal-ready SDL3/Cocoa bridge while keeping the stock-compatible OpenGL renderer rather than introducing a native Metal rewrite. macOS releases currently use Apple's OpenAL framework for audio; `-Dmacos_openal_provider=system` is available for local migration testing with a system/OpenAL Soft dependency and `AL/...` headers.

On current Debian/Ubuntu systems, install the SDL3/Linux package set (`binutils`, `libasound2-dev`, `libdbus-1-dev`, `libdecor-0-dev`, `libdrm-dev`, `libegl1-mesa-dev`, `libfribidi-dev`, `libgbm-dev`, `libgl1-mesa-dev`, `libibus-1.0-dev`, `libjack-dev`, `libopenal-dev`, `libpipewire-0.3-dev`, `libpulse-dev`, `libsndio-dev`, `libthai-dev`, `libudev-dev`, `libwayland-dev`, and `libxkbcommon-dev`). Add `libx11-dev`, `libxext-dev`, `libxcursor-dev`, `libxfixes-dev`, `libxi-dev`, `libxrandr-dev`, `libxss-dev`, `libxtst-dev`, `libxxf86dga-dev`, and `libxxf86vm-dev` when validating the optional SDL3 X11 helper path or the native Linux backend.

---

## Build Options

Pass any of these with `-D<option>=<value>` on the `meson setup` command line:

| Option | Default | Description |
|--------|---------|-------------|
| `build_engine` | `true` | Build `openQ4-client_<arch>` and `openQ4-ded_<arch>` |
| `build_games` | `true` | Build game modules |
| `build_game_sp` | `true` | Build single-player game module |
| `build_game_mp` | `true` | Build multiplayer game module |
| `platform_backend` | `sdl3` | `sdl3` or `legacy_win32` on Windows, `sdl3` or `native` on Linux/macOS |
| `macos_graphics_bridge` | `opengl` | macOS-only graphics bridge: `opengl` or `metal`; `metal` requires `platform_backend=sdl3` and keeps rendering on the OpenGL compatibility path |
| `macos_openal_provider` | `apple_framework` | macOS-only audio provider: `apple_framework` for release builds, or `system` for OpenAL Soft migration testing with `dependency('openal')` |
| `version_track` | `dev` | Build track label (`stable`, `dev`, `beta`, `rc`) |
| `version_iteration` | *(empty)* | Dot-separated iteration counter for pre-release builds |
| `version_base_override` | *(empty)* | Override the generated release version without editing `meson.build` |
| `enforce_msvc_2026` | `false` | Enforce MSVC 2026+ requirement (Windows only) |

---

## Validation Scripts

openQ4 includes two local validation profiles under `tools/validation/`. They share one Python runner and use the platform build wrappers so Windows validation still goes through `tools/build/meson_setup.ps1`.

GitHub Actions runs the same validation entrypoints on every pushed commit, pull request, and manual dispatch. Branch pushes run the faster push profile across Linux x64, Linux ARM64, macOS ARM64, and Windows. Pull requests/manual dispatches run the full PR profile on Windows x64, Linux ARM64, and macOS ARM64; the staged Linux ARM64 client also runs an assetless no-map renderer startup smoke under a virtual X display, and a native Wayland PR job runs the same renderer smoke under headless Weston with libdecor both enabled and disabled.

Pull requests also run Linux Sanitizer Validation on x64 with ASan+UBSan enabled. This is a compile-focused lane for engine and staged game-module code, not a gameplay/runtime sanitizer pass yet; it disables precompiled headers to keep sanitizer diagnostics attached to normal translation units and skips install packaging so failures stay centered on configure/compile output.

Current official macOS release artifacts are Apple Silicon/arm64 only. Intel Mac and universal2 packages are not published until a dedicated Intel runner or universal packaging lane is added; local Intel builds may still be used for development bring-up, but they are outside the packaged release support matrix.

### Push Validation

Use this before pushing local work. It runs lightweight Python checks, reconfigures/reuses `builddir/` as a debug build, and compiles the engine plus game modules.

```powershell
powershell -ExecutionPolicy Bypass -File tools/validation/validate_push.ps1
```

```bash
bash tools/validation/validate_push.sh
```

### PR Validation

Use this before opening or updating a pull request. It performs a clean release-style debug build in `.tmp/validation/pr-builddir`, stages `.install/`, and verifies the staged runtime payload contains the expected engine executables, SP/MP game modules, required `baseoq4` files, Windows diagnostic symbols when applicable, and no root-level build-only linker artifacts.

```powershell
powershell -ExecutionPolicy Bypass -File tools/validation/validate_pr.ps1
```

```bash
bash tools/validation/validate_pr.sh
```

Useful options:

- Add `--runtime` to run the safe renderer startup validation matrix after the staged install.
- Add `--runtime-skip-official-pak-validation` with a targeted no-map runtime case when intentionally running an assetless engine-startup smoke, such as CI coverage without retail Quake 4 PK4s.
- Add `--build-gamelibs` when you also want the Windows wrapper to build the standalone openQ4-game outputs during compile.
- Use `--game-libs-repo <path>` when the companion repository is not at `../openQ4-game`.
- Use `--build-dir <path>` plus `--no-clean` to validate a specific existing build tree.

### Linux Sanitizer Validation

On Linux, use this when touching memory-sensitive code, SDL event normalization, display/window geometry, staged source handling, or parser/launcher logic:

```bash
bash tools/validation/validate_pr.sh \
  --skip-python-tests \
  --no-install \
  --build-dir .tmp/validation/linux-sanitizer-builddir \
  --buildtype debugoptimized \
  --jobs 2 \
  --extra-setup-arg=-Duse_pch=false \
  --extra-setup-arg=-Db_sanitize=address,undefined
```

This matches the PR sanitizer job. It requires the normal Linux build dependencies and the sibling `openQ4-game` checkout so the game modules are compiled from staged source input.

---

## Building on Windows

> [!NOTE]
> Release packaging targets the static MSVC CRT so end users do not need a separate Visual C++ Redistributable install.

### VS Code Fast Build

Use **Build openQ4 (Meson Debug)** as the default local/agent build from VS Code. It runs the Meson compile path, lets Ninja skip unaffected code and PK4 targets, then incrementally copies only changed runtime files from `builddir/` into `.install/` for the existing launch configurations.

Use **Full Build and Stage openQ4 (Meson Debug)** when you intentionally need the full configure + compile + `meson install --no-rebuild --skip-subprojects` path. If you add or remove files under `content/baseoq4/pak0/` or `content/baseoq4/pak1/`, run **Configure openQ4 (Meson Debug)** once so Meson refreshes the pack dependency list; edits to existing content files are picked up by the fast build.

### Debug Build

```powershell
# 1. Configure
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 setup --wipe builddir . --backend ninja --buildtype=debug --wrap-mode=forcefallback

# 2. Compile
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 compile -C builddir

# 3. Run directly from builddir
builddir\openQ4-client_<arch>.exe
```

### Optimized Build

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
> As of March 30, 2026, Linux and macOS default to the SDL3 backend. `-Dplatform_backend=native` remains available as the fallback POSIX comparison path. Native Wayland is handled through SDL3/Wayland/EGL on Linux, while `openQ4-steamdeck` still prefers XWayland when both `WAYLAND_DISPLAY` and `DISPLAY` are present unless `SDL_VIDEO_DRIVER` or `SDL_VIDEODRIVER` is already set.

### Debug Build

```bash
# 1. Configure
bash tools/build/meson_setup.sh setup --wipe builddir . --backend ninja --buildtype=debug --wrap-mode=forcefallback

# 2. Compile
bash tools/build/meson_setup.sh compile -C builddir

# 3. Run directly from builddir
./builddir/openQ4-client_<arch>
```

Use `-Dplatform_backend=native` during setup if you need to compare against the legacy Linux X11/GLX or macOS Cocoa/OpenGL backend.

For macOS Metal bring-up without a native renderer rewrite, configure with:

```bash
bash tools/build/meson_setup.sh setup --wipe builddir . --backend ninja --buildtype=debug --wrap-mode=forcefallback -Dplatform_backend=sdl3 -Dmacos_graphics_bridge=metal
```

This mode links the Metal/QuartzCore bridge frameworks, defaults SDL's render/GPU hints to Metal, and logs the active bridge during OpenGL startup. The visible renderer remains openQ4's OpenGL compatibility path so shipped Quake 4 asset behavior stays the guiding constraint.

### Linux Packager Notes

For downstream Linux packages, treat official openQ4 Linux archives as targeting an Ubuntu 24.04-class userspace unless broader distro coverage has been validated for the release. The Meson dependency floor is SDL3 `>=3.4.4`; the bundled fallback wrap currently tracks SDL3 3.4.10.

Package the default SDL3 backend with OpenGL plus Wayland/EGL support, and keep X11/GLX available where practical for fallback and diagnostics. The launcher or package notes should document both SDL spellings for explicit driver selection because users and SDL documentation may use either form:

- Native Wayland: `SDL_VIDEODRIVER=wayland` or `SDL_VIDEO_DRIVER=wayland`.
- X11/XWayland: `SDL_VIDEODRIVER=x11`, `SDL_VIDEO_DRIVER=x11`, or the project-specific `OPENQ4_FORCE_X11=1`.
- Wayland decoration fallback: `OPENQ4_WAYLAND_DISABLE_LIBDECOR=1` when libdecor causes startup or window-decoration issues.
- Wayland decoration preference: `OPENQ4_WAYLAND_PREFER_LIBDECOR=1` when a compositor behaves better with libdecor.
- Wayland window-operation diagnostics: `OPENQ4_WAYLAND_SYNC_WINDOW_OPS=1`; use only for troubleshooting because some compositors can block during window animations.

Build from the companion `openQ4-game` checkout through `OPENQ4_GAMELIBS_REPO` or the default sibling path. The staged `.tmp/gamelibs_stage/openq4_gamelibs_stage_manifest.json` records the exact source-input hashes and git state used by the engine build; keep that manifest in CI artifacts when investigating packaging or reproducibility problems, but do not package `.tmp/gamelibs_stage/` as a runtime payload.

Release packaging publishes detached Linux debug symbols as `openQ4-<version>-linux-<arch>-debugsymbols.tar.xz`. Keep that archive paired with the matching runtime package so crash reports from optimized Linux builds can be symbolized.

Before shipping a downstream package, run at least:

```bash
bash tools/validation/validate_push.sh --install
bash tools/validation/validate_pr.sh --runtime --runtime-cases renderer-default-safety-selftest,sdl3-x11-display-diagnostics --runtime-tiers auto --runtime-basepath "" --runtime-skip-official-pak-validation
```

### Optimized Build

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
| `openQ4-client_<arch>[.exe]` | Main engine executable |
| `openQ4-ded_<arch>[.exe]` | Dedicated server |
| `baseoq4/pak0.pk4` | Compiled openQ4 core runtime content pack |
| `baseoq4/pak1.pk4` | Compiled openQ4 level content pack |
| `baseoq4/mod.json` | openQ4 game-directory manifest |
| `baseoq4/game-sp_<arch>[.dll/.so/.dylib]` | Single-player game module |
| `baseoq4/game-mp_<arch>[.dll/.so/.dylib]` | Multiplayer game module |

- BSE (Basic Set of Effects) is linked directly into `openQ4-client_<arch>`; the dedicated server keeps a disabled/stub path.
- `content/baseoq4/pak0/` and `content/baseoq4/pak1/` are compiled into deterministic `pak0.pk4` and `pak1.pk4` before the engine is built; the generated pack checksums are embedded into the executable so startup can reject missing or modified openQ4 runtime packs.
- On Windows, the wrapper stages `OpenAL32.dll` next to the executables and rejects builds that still depend on external MSVC/UCRT runtime DLLs.

### Install directory (`.install/`)

After running the install step, `.install/` is a self-contained distributable package:

```
.install/
├── openQ4-client_<arch>[.exe]  # Main executable
├── openQ4-client_<arch>.pdb    # (Windows) client diagnostic symbols
├── openQ4-ded_<arch>[.exe]     # Dedicated server
├── openQ4-ded_<arch>.pdb       # (Windows) dedicated-server diagnostic symbols
├── openQ4-steamdeck            # (Linux) Steam Deck launcher
├── OpenAL32.dll                # (Windows) runtime dependency
├── share/applications/         # (Linux) desktop entries
└── baseoq4/
    ├── pak0.pk4                         # Compiled openQ4 core runtime content
    ├── pak1.pk4                         # Compiled openQ4 level content
    ├── mod.json                         # openQ4 game-directory manifest
    ├── game-sp_<arch>[.dll/.so/.dylib]   # Single-player module
    ├── game-sp_<arch>.pdb                # (Windows) SP diagnostic symbols
    ├── game-mp_<arch>[.dll/.so/.dylib]   # Multiplayer module
    └── game-mp_<arch>.pdb                # (Windows) MP diagnostic symbols
```

> [!NOTE]
> Public release packages stay on the `stable` version track while using platform-appropriate diagnostics. Windows packages intentionally use Meson `buildtype=debug` and include matching PDB files. Linux and macOS release packages use Meson `buildtype=debugoptimized` with `b_ndebug=true`; Linux debug info is split into separate `openq4-<version>-linux-<arch>-debugsymbols.tar.xz` assets. macOS release packaging builds both `-opengl` and `-metal` variants from the SDL3 backend so the Metal bridge remains additive. MSVC import libraries (`*.lib`) are development-only artifacts and are not required in the package.

Repo-authored runtime overrides live under `content/baseoq4/pak0/` and `content/baseoq4/pak1/`. The build compiles those source-owned roots into `.install/baseoq4/pak0.pk4` and `.install/baseoq4/pak1.pk4`; `mod.json` and platform-specific game modules remain loose beside them.

On Linux desktops, you can create a user desktop shortcut for the staged runtime after the install step:

```bash
bash tools/linux/install_desktop_launcher.sh
```

If Quake 4 is not in a location that openQ4 can auto-detect, pass the retail install root that contains `q4base/`:

```bash
bash tools/linux/install_desktop_launcher.sh --basepath "/path/to/Quake 4"
```

---

## Packaging a Distributable

The `meson install` step (via the wrapper) stages all required binaries into `.install/`. This directory can be zipped and distributed as a release archive.

Release archives also generate a packaged offline HTML documentation site under `docs/`. If you run the release packager manually instead of using GitHub Actions, make sure `python -m pip install markdown` is available in the same environment.

The manually dispatched GitHub release workflow publishes architecture-qualified release assets such as `openq4-<version>-windows-x64.zip`, `openq4-<version>-windows-arm64.zip`, `openq4-<version>-linux-x64.tar.xz`, `openq4-<version>-linux-arm64.tar.xz`, `openq4-<version>-macos-arm64-opengl.dmg`, and `openq4-<version>-macos-arm64-metal.dmg`. Release workflow packages use `version_track=stable`; Windows payloads use Meson `buildtype=debug`, include PDB files, and write crash logs plus minidumps under `crashes/` beside the executable after unhandled exceptions. Linux and macOS payloads use Meson `buildtype=debugoptimized` with `b_ndebug=true`; Linux release runs also publish detached debug-symbol archives such as `openq4-<version>-linux-x64-debugsymbols.tar.xz` and `openq4-<version>-linux-arm64-debugsymbols.tar.xz`. macOS release payloads are signed/stapled, packed into compressed DMG images, submitted for final DMG notarization/stapling, and verified with `hdiutil` before upload. Windows release payloads also get native installer executables such as `openq4-<version>-windows-x64-setup.exe` and `openq4-<version>-windows-arm64-setup.exe`. Each installer is compiled from the already-packaged Windows release directory so its file set matches the archive instead of diverging from it, writes install metadata to the registry for upgrade detection, registers a normal Windows uninstaller entry, and can optionally register `openq4://` browser links.

Official macOS release jobs require Apple distribution credentials before packaging starts. Configure `MACOS_DEVELOPER_ID_APPLICATION_CERTIFICATE_BASE64`, `MACOS_DEVELOPER_ID_APPLICATION_CERTIFICATE_PASSWORD`, `MACOS_DEVELOPER_ID_APPLICATION_IDENTITY`, `MACOS_NOTARY_APPLE_ID`, `MACOS_NOTARY_TEAM_ID`, and `MACOS_NOTARY_APP_PASSWORD` as repository secrets. The workflow imports the Developer ID Application certificate into a temporary keychain, signs macOS payloads with the Hardened Runtime, submits the package payload with `notarytool`, staples the app ticket, and validates the result with `codesign`, `spctl`, and `xcrun stapler validate`.

Official macOS release jobs do not pass a custom entitlements file by default. If an entitlement plist is supplied for a future release experiment, `tools/build/package_nightly.py` validates that it is a plist dictionary and rejects App Sandbox or `get-task-allow` entitlements until the project has a reviewed sandbox/file-access design. The current direct-distribution package relies on Hardened Runtime protections without sandboxing because openQ4 must read user-selected Quake 4 assets, saves, logs, and staged runtime overlays outside an App Sandbox container.

The release workflow also posts an openQ4 release announcement to Discord after the GitHub release is published. Configure the repository secret `DISCORD_RELEASE_WEBHOOK` with the Discord webhook URL before running the workflow; the metadata job checks for it before the package matrix starts. Optional repository variables can tune the announcement without editing workflow files: `DISCORD_RELEASE_EMOJI`, `DISCORD_RELEASE_MENTIONS`, `DISCORD_FEEDBACK_CHANNEL`, and `DISCORD_RELEASE_AVATAR_URL`. If no overrides are set, the broadcaster uses `assets/img/avatar.png`, the `<:quake4:1425986174941397105>` header emote, and the `<@&1425985498693898260> <@&1390287267276525628>` role mentions. Releases created by the manual workflow announce themselves because GitHub releases created with `GITHUB_TOKEN` do not retrigger release workflows; `.github/workflows/discord-release.yml` covers releases published manually through GitHub.

If you want to build that installer manually on Windows after packaging a release directory, install [Inno Setup](https://jrsoftware.org/isinfo.php) and run:

```powershell
python tools/build/build_windows_installer.py --package-dir .tmp\openq4-0.1.010-windows-arm64 --version 0.1.010 --version-tag 0.1.010 --arch arm64 --output-dir .tmp\release-artifacts
```

To include the icon set synchronisation step before building (validated and generated by the wrapper automatically):

```powershell
# Set OPENQ4_SKIP_ICON_SYNC=1 to bypass icon sync in automated/CI workflows
$env:OPENQ4_SKIP_ICON_SYNC = "1"
```

The manually dispatched GitHub release workflow injects `version_base_override` from `tools/build/openq4_release_version.py`. That helper uses the repo version in `meson.build` as the release floor and, once stable `v*` tags exist, automatically chooses between a patch bump (`0.1.010` -> `0.1.011`) and a minor bump (`0.1.010` -> `0.2.000`) based on the scale of changes since the previous release. The workflow also accepts manual `bump_mode` choices for `auto`, `major (x..)`, `minor (.x.)`, and `patch (..x)`, plus `version_override` when a release needs an explicit version.

---

[← Back to README](README.md)
