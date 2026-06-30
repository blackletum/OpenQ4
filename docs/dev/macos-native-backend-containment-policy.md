# macOS Native Backend Containment Policy

Updated: 2026-06-30

This policy keeps the legacy macOS Cocoa/OpenGL backend useful for comparison
without allowing it to become an implied support surface.

## Current Decision

The release backend is `platform_backend=sdl3`. The legacy
`platform_backend=native` Cocoa/OpenGL backend is retained only as
comparison-only diagnostic infrastructure while SDL3 macOS coverage matures.

The native backend is not kept indefinitely by default. After SDL3 coverage is
mature enough for the current macOS floor and latest public macOS, create a
separate native-backend removal plan before deleting or changing the legacy path.
That plan must cover Meson option changes, documentation updates, rollback
strategy, release-note wording, and any temporary comparison evidence still
needed for open macOS renderer issues.

## Source Boundary

The native backend owns the remaining legacy macOS platform APIs:

- `src/sys/osx/macosx_event.mm` is the native Carbon event/input boundary.
- `src/sys/osx/macosx_glimp.mm` is the native NSOpenGL/CGL context boundary.

SDL3 macOS sources must not import Carbon, link Carbon, or use NSOpenGL/CGL
context APIs. `tools/build/meson_sources.py` must keep `macosx_event.mm` and
`macosx_glimp.mm` out of `SDL3_DARWIN_SOURCES`.

`meson.build` may link Carbon only when `platform_backend=native` is selected.
The SDL3 macOS backend links Cocoa/OpenGL/ApplicationServices, and the Metal
bridge variant adds Metal/QuartzCore only for `macos_graphics_bridge=metal`.

## Release Wording

Curated release notes may say that the native backend is comparison-only
diagnostic infrastructure and not a release backend. They must not describe the
legacy Cocoa/OpenGL backend as native macOS backend support, native Cocoa/OpenGL
support, a supported native backend, or release support evidence.

## Native Metal Boundary

No native Metal renderer is selected for the current release line. Native Metal
implementation work stays out of scope until a separate design plan covers:

- Stock Quake 4 material, shader, interaction, and lighting parity.
- BSE effect rendering and lifetime behavior.
- Shader translation or replacement strategy.
- Screenshot, readback, video, and diagnostic capture behavior.
- RenderDoc or Xcode GPU capture workflow.
- Performance counters, renderer metrics, and failure diagnostics.
- Fallback and rollback behavior.
- Package names, release notes, and signoff evidence for a new renderer path.

Until that plan exists, release packages remain `OpenGL` and `Metal bridge`,
and the `Metal bridge` label must not imply a native Metal renderer or
OpenGL-free renderer.

## Static Guard

`tools/tests/macos_native_backend_containment.py` enforces this policy by
checking the source manifests, Meson framework gating, release-note wording, and
absence of native Metal implementation tokens in active renderer/macOS sources.
It is a static guard only and does not claim macOS runtime or Finder coverage.
