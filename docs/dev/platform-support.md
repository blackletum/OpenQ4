# openQ4 Platform And Architecture Roadmap

This document defines the long-term platform direction for openQ4 and how SDL3 + Meson are used to get there.

## Target End State

- First-class support on modern desktop operating systems:
  - Windows
  - Linux
  - macOS, currently experimental until the first-class criteria below are met
- First-class support for modern 64-bit desktop architecture:
  - x64 (`x86_64`)
  - arm64 (`aarch64`)
- Keep original Quake 4 gameplay/module behavior compatible while modernizing platform and build layers.

## Current Baseline (0.1.010 beta line)

- Primary actively validated build targets: Windows x64, Linux x64/arm64, and experimental macOS arm64 through hosted CI/package generation.
- Windows arm64 remains package-validated during bring-up, with runtime validation still required on hardware.
- Experimental manual macOS release artifacts are Apple Silicon/arm64 only. The macOS support matrix policy is recorded in `docs/dev/macos-support-matrix-policy.md`: current user-facing releases are `arm64 only`, Intel Mac/`x86_64`, universal2, and Rosetta compatibility are not supported release targets, and the documented OS floor remains `macOS 11` until changed in Meson, package metadata, docs, workflows, and evidence together. Credentialed release runs publish signed/notarized DMGs; runs without Apple Developer ID signing and notarization credentials publish clearly labeled unsigned/unnotarized `-unsigned.tar.gz` archives. The manual release workflow defaults to `macos_support_tier=experimental`; `macos_support_tier=first-class` is a hard gate that requires signed/notarized DMGs and fails when Apple signing/notary secrets are missing. Intel Mac and universal2 packages are intentionally not claimed until the project adds a dedicated Intel validation runner or a universal packaging lane with architecture-specific launch checks.
- Build system: Meson + Ninja.
- Dependency model: Meson subprojects/wraps.
- Platform backend direction: SDL3-first (legacy Win32 backend is transitional only).
- Language baseline target: C++23 semantics (`vc++latest` on current MSVC Meson front-end).
- Toolchain baseline direction: MSVC 19.46+ (Visual Studio 2026 generation), with compatibility fallback permitted during migration.
- As of March 30, 2026, Linux and experimental macOS default to the SDL3 backend and keep `-Dplatform_backend=native` as a fallback/comparison path. On macOS, `platform_backend=native` is comparison-only diagnostic infrastructure and is not a supported release backend.
- Steam Deck and SteamOS support is delivered through the explicit `openQ4-steamdeck` launcher/profile, plus direct-client host auto-detection when `com_platformProfile` is still `default`.
- Native Wayland is supported through the SDL3 backend. The shared SDL3 path logs the selected video driver, active Wayland hints, display content scale, orientation, current/desktop display modes, exact refresh details when SDL reports them, and compositor-accepted window state after screen changes; applies Wayland-aware defaults; avoids persisting compositor-owned window positions; keeps relative mouse-look usable when pointer confinement is unavailable; synchronizes compositor-negotiated size/fullscreen changes before refreshing renderer placement; and tries SDL's unversioned OpenGL compatibility fallback first when `r_glTier` is `auto` on native Wayland. CI now exercises native Wayland SDL3 window lifecycle, relative mouse-capture, and display-diagnostics cases under Weston, including fullscreen/windowed `vid_restart` transitions and the optional blocking window-operation synchronization path.
- SDL3 Linux VRAM autodetection can enumerate DRM card/render-node sysfs before legacy `/proc/dri` fallback, and native Wayland skips optional XNVCtrl/X11 probing, so native Wayland and minimal X11-free sessions are less dependent on optional X11 helpers.
- SDL3 Linux and experimental macOS desktop-resolution queries fall back from desktop mode to current mode and display bounds, improving startup robustness on compositors or display bridges that do not report a conventional desktop mode.
- Windows, Linux, and experimental macOS SDL3 builds share the same `r_screen`, `r_multiScreen`, fullscreen, exclusive-mode, borderless, windowed-placement, high-DPI drawable, display-change, selected-display spanned-UI viewport, and diagnostic display-list code paths. Native Wayland keeps compositor-owned placement semantics and falls back from multi-display spanning to the selected display when absolute placement is unavailable. The shared SDL3 renderer path also verifies a live, current OpenGL context before screen changes, swaps, deactivation, and teardown, validates extension lookups, uses overflow-safe selected-display viewport math, clamps malformed mouse movement, wheel, controller, and rumble values before integer conversion and event queuing, normalizes unusual app-entry argument state, guards POSIX clipboard/console command/input allocation, terminal and desktop-console cursor and scroll state, pthread setup and initialization state, requires error-checking mutex attributes before enabling critical sections, checks thread/event indexing, signal reporting, process handoff, and fatal-error/print formatting paths, and makes the macOS display selector fall back from requested display to main display to first active display before returning a null display id.
- Windows, Linux, and experimental macOS keyboard, mouse, and controller input are routed through the shared SDL3 backend. Linux and experimental macOS explicitly keep SDL's HIDAPI controller stack, enhanced reports, hotplug events, rumble, battery diagnostics, gyro, touchpad, and touchscreen routing available at the same feature level as the Windows SDL3 path while preserving user/SDL environment overrides.
- XWayland remains available as an explicit fallback by setting `OPENQ4_FORCE_X11=1` or an SDL video-driver override such as `SDL_VIDEO_DRIVER=x11`.
- The Linux X11/Xvfb fallback lane also runs an SDL3 display-diagnostics smoke in CI so native Wayland and explicit X11 driver paths both prove display enumeration, scale/orientation reporting, and selected-display diagnostics.
- The project-level `OPENQ4_FORCE_X11=1` fallback is runtime-validated separately from raw SDL driver overrides; CI launches the staged client under Xvfb with that flag and checks that SDL reports the X11 driver plus normal display diagnostics.
- If a native Wayland compositor has decoration, resize, or window-control issues, `OPENQ4_WAYLAND_PREFER_LIBDECOR=1` asks SDL to prefer libdecor without changing the default path for other sessions.
- If libdecor itself causes startup or decoration issues on a compositor stack, `OPENQ4_WAYLAND_DISABLE_LIBDECOR=1` asks SDL to keep libdecor disabled for that launch.
- If a compositor applies window changes too asynchronously for diagnosis, `OPENQ4_WAYLAND_SYNC_WINDOW_OPS=1` asks SDL to synchronize every window operation. Use it only as a troubleshooting option because some compositors may block during window animations.
- Experimental macOS arm64 release packages are built and validated in both OpenGL and Metal bridge variants, with `.app` metadata, executable bits, runtime dependency roots, and architecture-matched `.dylib` game modules checked before release publication. The current supported package contract is an adjacent package root: `openQ4.app`, `baseoq4/`, loose engine binaries, and loose runtime support files stay together; moving only `openQ4.app` is unsupported until a self-contained bundle migration is implemented. Credentialed runs add final compressed DMG creation, `hdiutil` image verification, Developer ID signing, and DMG notarization/stapling. Release runs without Apple signing/notary credentials publish `-unsigned.tar.gz` archives instead, ad-hoc signed only for bundle validity and clearly marked as unsigned/unnotarized. The current release line does not publish Intel Mac or universal2 artifacts; those remain future support-policy decisions rather than implied compatibility. The SDL3 release path links Cocoa/OpenGL/ApplicationServices and keeps the legacy Carbon framework isolated to `-Dplatform_backend=native`. The Metal bridge keeps the stock-compatible OpenGL renderer path, links Metal/QuartzCore for the SDL3/Cocoa host surface, applies the bridge define consistently across C/C++/Objective-C/Objective-C++ sources, logs failed SDL Metal hint setup, requires valid SDL window IDs before splash/system-console renderer creation, lets those support windows try the Metal/default renderer before falling back to software, and reports failed splash event requeues during startup/error UI draining. The renderer/backend policy is tracked in `docs/dev/macos-renderer-backend-policy.md`, and native backend containment is tracked in `docs/dev/macos-native-backend-containment-policy.md`: current `-metal` packages are Metal bridge packages, not native Metal renderer packages.
- Experimental macOS audio still defaults to Apple's OpenAL framework for release stability through `-Dmacos_openal_provider=apple_framework`. The `-Dmacos_openal_provider=system` build option is available only for local OpenAL Soft migration testing with a system `openal` dependency and `AL/...` headers, but it is not the packaged release default and current macOS packages do not bundle OpenAL Soft. The detailed provider policy is recorded in `docs/dev/macos-openal-provider-policy.md`.
- Experimental macOS release signing uses the Hardened Runtime without custom entitlements by default. Optional entitlement files are validated as plist dictionaries and App Sandbox or `get-task-allow` entitlements are rejected until the project has a reviewed sandbox/file-access design for user-selected Quake 4 assets, saves, logs, and staged runtime overlays.
- The native macOS Cocoa/OpenGL fallback is experimental and kept crash-resistant for comparison testing: it now stores a valid CGL context pointer, validates pixel-format/context/window setup before use, releases created contexts if final make-current fails, fails cleanly when fullscreen display capture/mode/context setup cannot complete, unwinds partial display captures, treats missing or invalid-display VRAM telemetry as non-fatal, guards mouse capture, scroll-wheel overflow, nil event entry points, display/gamma-table, stack-free extension lookup, verified-current context swap/activation/pause/resume, and shutdown lifetime paths, returns deterministic unsupported status from legacy render-thread and screen-change stubs, uses the engine allocator for clipboard text, owns UTF-8 strings handed back from Cocoa key-name, clipboard, and fatal-error alert paths, keeps obsolete Carbon/Xcode-era macOS sources out of the Meson build manifest, and validates that live macOS backend sources do not reintroduce raw `strcpy`/`strcat`/`sprintf`/`alloca` string builders.
- Windows arm64 currently uses a custom OpenAL Soft package path during bring-up because the in-repo bundled Windows runtime payload is still x64-only.

## macOS Support Matrix

- Current architecture policy: `arm64 only` for experimental Apple Silicon/arm64 release packages.
- Unsupported current macOS release targets: Intel Mac/`x86_64`, universal2 packages, and Rosetta as a supported compatibility layer.
- Current OS-version policy: `macOS 11` is the documented Apple Silicon/arm64 package floor, while the latest public macOS release is the rolling current-version signoff target.
- Hosted `macos-15` CI builds prove configure, build, package, signing, notarization, and static validation paths; they do not replace real Apple-hardware runtime signoff for the macOS 11 floor or the latest public macOS release.
- First-class macOS support requires completed OpenGL and Metal bridge evidence for both the documented floor and latest public macOS, with architecture policy, CPU architecture, OS matrix role, Xcode version, and macOS SDK version recorded in `docs/dev/macos-signoff-evidence.md`.
- Matrix expansion requirements are tracked in `docs/dev/macos-support-matrix-policy.md` and `docs/dev/plan/2026-06-30-macos-compatibility-support.md`.

## macOS Renderer And Backend Policy

- Current release renderer policy: both macOS package variants use the stock-compatible OpenGL renderer.
- Current package variants: `OpenGL` and `Metal bridge`. The Metal bridge is not a native Metal renderer and must not be described as OpenGL-free.
- Current backend policy: release packages use `platform_backend=sdl3`; `platform_backend=native` on macOS is retained for comparison-only diagnostics.
- Native Metal is not selected for the current release line. If native Metal becomes a target, create a separate design plan covering stock asset parity, shader translation, BSE effects, screenshot/readback behavior, diagnostics, performance counters, and rollback/fallback behavior before implementation.
- Native Cocoa/OpenGL backend results are useful for comparing SDL3 regressions but do not count as release support evidence unless a future support decision adds dedicated CI, packaging, and real Apple-hardware signoff.
- The detailed policy is recorded in `docs/dev/macos-renderer-backend-policy.md` and `docs/dev/macos-native-backend-containment-policy.md`.

## Runtime Baselines

- Windows packaged compatibility floor: `Windows 7` or later.
- Windows validation focus: current `Windows 11` releases first, with `Windows 10` retained as a practical compatibility target even though Microsoft's general Windows 10 servicing ended on `October 14, 2025`.
- Windows 7/8/8.1 are no longer hard-blocked by the current x64 binaries, but they are legacy and outside the actively validated support matrix.
- Experimental macOS packaged compatibility floor for the Apple Silicon/arm64 release line: `macOS 11` or later. Meson now pins the deployment target to `11.0` so the binary floor matches the documented floor. Intel Mac and universal2 package floors are not published because those packages are not part of the current release matrix.
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
- macOS startup validates both advertised OpenGL extensions and the callable entry points behind the multitexture, ARB2, VBO upload, PBO readback, and GLSL paths. If Apple's OpenGL 2.1 compatibility fallback reports an incomplete loader state, openQ4 now downgrades optional upload/readback paths to CPU-backed fallbacks or fails through the normal missing-feature path instead of continuing into a SIGSEGV.
- Apple OpenGL 2.1 compatibility contexts now disable the legacy VBO vertex cache and use CPU-backed vertex data, keeping the renderer upload bridge and ARB2 startup away from the crash-prone VBO path reported on Apple Silicon. Apple OpenGL 2.1 compatibility also skips the full `interaction.vfp` upload and prefers `SimpleInteraction.vfp`, avoiding the startup crash observed immediately after interaction color-mode detection on the Metal bridge path.
- Apple OpenGL 2.1 compatibility now bypasses the ARB2 light-interaction pass on the fragile M4/M5 compatibility path reported in issue #73. This is a degraded-lighting fallback: affected launches log `ARB2 interaction driver bypass`, restore the classic post-interaction GL state, texture-unit, and ARB program-binding baseline, force a neutral light-scale state, skip the post-interaction light-scale pass, keep an ambient rescue floor, and avoid the first-frame ARB2 light interaction crash while normal GL paths retain full ARB2 interaction rendering. Post-bypass breadcrumbs record state restoration, light-scale skip, ambient rescue, and frame tail so follow-up reports can identify the next operation reached.
- Optional buffer-object users share the same capability contract: renderer upload cleanup tolerates missing delete/bind entry points, HDR exposure readback and light-grid baking use `glConfig.pixelBufferObjectAvailable`, and disabled upload bridges report zero ring buffers in diagnostics.
- Classic ARB2 interaction draws use explicit `idDrawVert` VBO byte offsets instead of deriving member addresses from the vertex-cache offset token, which keeps Apple's OpenGL 2.1 compatibility path away from undefined C++ pointer formation during the first in-game interaction pass.

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
4. Continue experimental macOS Apple Silicon hardware signoff for input devices, audio devices, and in-game renderer coverage on real Macs before promoting the CI build/package path to first-class.
5. Add an explicit Intel Mac or universal2 release lane only after the project has architecture-specific CI, `lipo`/install-name validation, and launch evidence for both packaged architectures.

## SDL3 Migration Staging (Linux/Experimental macOS)

- Linux and experimental macOS now use the shared SDL3 runtime path when `-Dplatform_backend=sdl3` is selected, and that is the default configuration as of March 30, 2026.
- Experimental macOS SDL3 builds select `src/sys/osx/macosx_sdl3.cpp` and `src/sys/osx/macosx_sdl3_main.cpp` with the shared SDL3 window, input, controller, and OpenGL context path.
- The native Linux X11/GLX backend and experimental macOS Cocoa/OpenGL diagnostic backend remain available through `-Dplatform_backend=native` for comparison and rollback while SDL3 remains the release path. On macOS, native backend results are comparison-only unless a future support decision adds dedicated validation. On SteamOS, the SDL3 path also watches application lifecycle events so suspend/resume and foreground/background transitions flow through the same input release, rumble stop, config write, and controller reacquire behavior used by the normal event pump.

## Definition Of Done For First-Class Platform Support

- Clean configure + build in Meson.
- Engine initializes and reaches map/session startup with stock Quake 4 assets.
- Core input, rendering, audio, and networking paths work without platform-specific content hacks.
- Regressions are tracked in docs and fixed in engine/platform code, not with asset overrides.
