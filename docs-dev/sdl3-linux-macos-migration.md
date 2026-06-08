# SDL3 Migration Plan (Linux/macOS)

This document defines the implementation plan for migrating non-Windows platform backends toward SDL3 while preserving current runtime stability.

## Goals

- Reduce backend divergence between Windows and non-Windows hosts.
- Keep Linux/macOS builds and release packaging stable during migration.
- Avoid regressions in stock-asset compatibility while platform code is modernized.

## Current State

- Windows can build/run with `platform_backend=sdl3`.
- Linux defaults to `platform_backend=sdl3` and uses the SDL3 window, input, display, and OpenGL context path. The native X11/GLX backend remains available with `-Dplatform_backend=native` as a fallback.
- Windows, Linux, and macOS now share the main SDL3 implementation under `src/sys/sdl3/sdl3_backend.cpp`; `src/sys/win32/win_sdl3.cpp`, `src/sys/linux/linux_sdl3.cpp`, and `src/sys/osx/macosx_sdl3.cpp` are platform entry wrappers for build selection and host-specific glue.
- macOS `platform_backend=sdl3` now selects the SDL3 source path, including shared keyboard, mouse, and controller handling. The native Cocoa path remains available with `-Dplatform_backend=native` and keeps the validated URL opening, shell-free process handoff, millisecond sleep timing, platform-backed memory reporting, cleaner GL startup failure handling, and improved Cocoa keyboard/mouse compatibility.
- Linux SDL3 still carries small platform-specific glue for executable discovery, POSIX lifecycle, X11/XNVCtrl video-memory probing, and Steam Deck/XWayland launch behavior.
- Native Wayland now runs through the same SDL3 backend with driver-specific handling: startup logs the selected SDL video driver and relevant environment, Wayland mode/decorator/high-DPI hints are set before video init, compositor-owned window positions are not persisted, multi-display spanning falls back to the selected display, and `r_glTier auto` tries SDL's unversioned compatibility context before explicit profile/version contexts.

## Migration Phases

1. Backend Vocabulary + CI Alignment (completed)
- Use one backend selector (`platform_backend`) across all hosts.
- Allow non-Windows `platform_backend=sdl3` as a staging mode in CI and local builds.
- Keep effective non-Windows source selection native until SDL3 paths are implemented.

2. Shared SDL3 Shell Introduction (completed for Windows/Linux/macOS source selection)
- Add shared SDL3 window/input lifecycle code under `src/sys/sdl3/`, with thin platform wrappers for Windows, Linux, and macOS.
- Keep renderer context setup delegated to existing native code initially.
- Gate behind opt-in build/runtime cvars to allow side-by-side validation.

3. Linux SDL3 Runtime Bring-Up (default path)
- Replace Linux native window/event pump path with SDL3 equivalents.
- Validate fullscreen/windowed transitions, input capture, and multi-monitor behavior.
- Validate XWayland and native Wayland behavior explicitly in logs and docs.

4. macOS SDL3 Runtime Bring-Up
- Replace macOS native window/event pump path with SDL3 equivalents. Source selection now reaches the SDL3 wrapper; macOS hardware compile/runtime validation is still required.
- Validate Cocoa integration assumptions, cursor modes, focus transitions, keyboard/text input, high-resolution scrolling, controller hotplug, gamepad/joystick mapping, and rumble behavior.
- Ensure app-bundle execution path behaves correctly.

5. Promotion To First-Class
- Promote the remaining SDL3 paths to default once compile/link/runtime checks pass consistently.
- Keep native backends available for rollback until at least one release cycle is stable.

## Validation Requirements Per Phase

- Configure/build/install succeeds in CI for Windows/Linux/macOS.
- SP and MP startup smoke tests run without platform-specific content hacks.
- Log diagnostics for display/input failures remain actionable and explicit.
