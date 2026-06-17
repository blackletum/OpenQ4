# openQ4 Platform And Architecture Roadmap

This document defines the long-term platform direction for openQ4 and how SDL3 + Meson are used to get there.

## Target End State

- First-class support on modern desktop operating systems:
  - Windows
  - Linux
  - macOS
- First-class support for modern 64-bit desktop architecture:
  - x64 (`x86_64`)
  - arm64 (`aarch64`)
- Keep original Quake 4 gameplay/module behavior compatible while modernizing platform and build layers.

## Current Baseline (0.1.010 beta line)

- Primary actively validated build targets: Windows x64, Linux x64/arm64, and macOS arm64 through hosted CI/package generation.
- Windows arm64 remains package-validated during bring-up, with runtime validation still required on hardware.
- Build system: Meson + Ninja.
- Dependency model: Meson subprojects/wraps.
- Platform backend direction: SDL3-first (legacy Win32 backend is transitional only).
- Language baseline target: C++23 semantics (`vc++latest` on current MSVC Meson front-end).
- Toolchain baseline direction: MSVC 19.46+ (Visual Studio 2026 generation), with compatibility fallback permitted during migration.
- As of March 30, 2026, Linux and macOS default to the SDL3 backend and keep `-Dplatform_backend=native` as a fallback/comparison path.
- Steam Deck and SteamOS support is delivered through the explicit `openQ4-steamdeck` launcher/profile, plus direct-client host auto-detection when `com_platformProfile` is still `default`.
- Native Wayland is supported through the SDL3 backend. The shared SDL3 path logs the selected video driver, active Wayland hints, display content scale, orientation, current/desktop display modes, exact refresh details when SDL reports them, and compositor-accepted window state after screen changes; applies Wayland-aware defaults; avoids persisting compositor-owned window positions; keeps relative mouse-look usable when pointer confinement is unavailable; synchronizes compositor-negotiated size/fullscreen changes before refreshing renderer placement; and tries SDL's unversioned OpenGL compatibility fallback first when `r_glTier` is `auto` on native Wayland.
- SDL3 Linux VRAM autodetection can enumerate DRM card/render-node sysfs before legacy `/proc/dri` fallback, so native Wayland and minimal X11-free sessions are less dependent on optional XNVCtrl helpers.
- SDL3 Linux and macOS desktop-resolution queries fall back from desktop mode to current mode and display bounds, improving startup robustness on compositors or display bridges that do not report a conventional desktop mode.
- Windows, Linux, and macOS SDL3 builds share the same `r_screen`, `r_multiScreen`, fullscreen, exclusive-mode, borderless, windowed-placement, high-DPI drawable, display-change, selected-display spanned-UI viewport, and diagnostic display-list code paths. Native Wayland keeps compositor-owned placement semantics and falls back from multi-display spanning to the selected display when absolute placement is unavailable.
- Windows, Linux, and macOS keyboard, mouse, and controller input are routed through the shared SDL3 backend. Linux and macOS explicitly keep SDL's HIDAPI controller stack, enhanced reports, hotplug events, rumble, battery diagnostics, gyro, touchpad, and touchscreen routing available at the same feature level as the Windows SDL3 path while preserving user/SDL environment overrides.
- XWayland remains available as an explicit fallback by setting `OPENQ4_FORCE_X11=1` or an SDL video-driver override such as `SDL_VIDEO_DRIVER=x11`.
- If a native Wayland compositor has decoration, resize, or window-control issues, `OPENQ4_WAYLAND_PREFER_LIBDECOR=1` asks SDL to prefer libdecor without changing the default path for other sessions.
- If a compositor applies window changes too asynchronously for diagnosis, `OPENQ4_WAYLAND_SYNC_WINDOW_OPS=1` asks SDL to synchronize every window operation. Use it only as a troubleshooting option because some compositors may block during window animations.
- macOS arm64 release packages are built and validated in both OpenGL and Metal bridge variants, with `.app` metadata, executable bits, archive contents, runtime dependency roots, and architecture-matched `.dylib` game modules checked before release publication.
- Windows arm64 currently uses a custom OpenAL Soft package path during bring-up because the in-repo bundled Windows runtime payload is still x64-only.

## Runtime Baselines

- Windows packaged compatibility floor: `Windows 7` or later.
- Windows validation focus: current `Windows 11` releases first, with `Windows 10` retained as a practical compatibility target even though Microsoft's general Windows 10 servicing ended on `October 14, 2025`.
- Windows 7/8/8.1 are no longer hard-blocked by the current x64 binaries, but they are legacy and outside the actively validated support matrix.
- macOS packaged compatibility floor for the arm64 release line: `macOS 11` or later. Meson now pins the deployment target to `11.0` so the binary floor matches the documented floor.
- Linux packaged compatibility floor: release archives are built on pinned `Ubuntu 24.04` runners and should be treated as targeting a comparable modern 64-bit desktop userspace with OpenGL plus SDL3 Wayland/EGL or X11/GLX available.
- Steam Deck support assumes a SteamOS 3.x style environment. The explicit `openQ4-steamdeck` launcher remains the preferred shipping path, while raw `openQ4-client_<arch>` launches can auto-select the `steamdeck` profile from Deck/SteamOS host signals unless disabled by environment.

## SDL3 Direction

- SDL3 is the default backend path and the portability layer for:
  - window lifecycle
  - input event handling
  - context/window interop glue
- New platform-facing work should prefer SDL3 abstractions first.
- Platform-specific code should be isolated under `src/sys/<platform>/` when SDL3 cannot cover a requirement directly.
- OpenGL context selection now uses the shared renderer ladder across SDL3, native GLX, and native NSOpenGL paths. macOS remains capped at OpenGL 4.1 core by the platform OpenGL stack, while Linux and Windows can continue down through core and compatibility-profile fallbacks according to `r_glTier`.
- macOS startup validates both advertised OpenGL extensions and the callable entry points behind the ARB2, VBO, and GLSL paths. If Apple's OpenGL 2.1 compatibility fallback reports an incomplete loader state, openQ4 now downgrades optional VBO uploads to the virtual-memory vertex cache or fails through the normal missing-feature path instead of continuing into a SIGSEGV.

## Meson Direction

- Meson is the canonical build system going forward.
- External dependencies should be consumed via Meson dependency resolution and subprojects/wraps.
- New build logic should be host-aware and architecture-aware, with x64 as the active compatibility baseline.
- Meson configuration defaults to `cpp_std=vc++latest` (C++23-targeting mode on MSVC).
- Meson currently adds `/Zc:strictStrings-` on MSVC to preserve compatibility with legacy string-literal usage while the codebase is modernized.
- `tools/build/meson_setup.ps1` prefers VS 2026+ (major 18) when present; strict minimum enforcement can be enabled with `-Denforce_msvc_2026=true`.

## Bring-Up Staging

1. Keep Windows x64 stable with SDL3 default backend.
2. Keep Linux on the SDL3 backend by default and validate both x64 and arm64 release paths.
3. Validate Windows arm64 beyond compile/package bring-up, especially runtime audio and in-game coverage.
4. Continue macOS hardware signoff for input devices, audio devices, and in-game renderer coverage on real Macs while keeping the CI build/package path first-class.

## SDL3 Migration Staging (Linux/macOS)

- Linux and macOS now use the shared SDL3 runtime path when `-Dplatform_backend=sdl3` is selected, and that is the default configuration as of March 30, 2026.
- macOS SDL3 builds select `src/sys/osx/macosx_sdl3.cpp` and `src/sys/osx/macosx_sdl3_main.cpp` with the shared SDL3 window, input, controller, and OpenGL context path.
- The native Linux X11/GLX and macOS Cocoa/OpenGL backends remain available through `-Dplatform_backend=native` for comparison and rollback while SDL3 remains the release path. On SteamOS, the SDL3 path also watches application lifecycle events so suspend/resume and foreground/background transitions flow through the same input release, rumble stop, config write, and controller reacquire behavior used by the normal event pump.

## Definition Of Done For First-Class Platform Support

- Clean configure + build in Meson.
- Engine initializes and reaches map/session startup with stock Quake 4 assets.
- Core input, rendering, audio, and networking paths work without platform-specific content hacks.
- Regressions are tracked in docs and fixed in engine/platform code, not with asset overrides.
