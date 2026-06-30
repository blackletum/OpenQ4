# macOS Renderer And Backend Policy

Updated: 2026-06-30

This document defines the current macOS renderer and platform-backend support
policy. It exists so the OpenGL package, Metal bridge package, and legacy native
Cocoa/OpenGL backend do not imply support promises beyond what openQ4 actually
builds, validates, and releases.

The legacy backend source-containment and removal-decision policy is tracked in
`docs/dev/macos-native-backend-containment-policy.md`.

## Current Release Path

The current macOS release path is:

- `platform_backend=sdl3`.
- `macos_graphics_bridge=opengl` for the OpenGL package.
- `macos_graphics_bridge=metal` for the Metal bridge package.
- The stock-compatible openQ4 OpenGL renderer in both package variants.

The Metal bridge package is not a native Metal renderer. It keeps the renderer
on the OpenGL compatibility path while using the SDL3/Cocoa host integration and
Metal/QuartzCore bridge plumbing needed for translation-layer bring-up and
diagnostics.

Release-facing package names and docs must use `OpenGL` and `Metal bridge`.
They must not describe the current `-metal` package as native Metal, a Metal
renderer, or an OpenGL-free renderer.

## Validation Contract

Every accepted macOS renderer signoff must keep the two bridge variants
separate:

- OpenGL package signoff for `macos_graphics_bridge=opengl`.
- Metal bridge package signoff for `macos_graphics_bridge=metal`.
- `renderer-smoke`, `renderer-mp-smoke`, and `renderer-matrix` evidence for
  both variants.
- SP and MP in-game checks on real Apple hardware or a compliant Apple-hardware
  VM before any first-class support claim.

Hosted CI package/build success is useful, but it is not a replacement for
runtime evidence on the documented macOS floor and latest public macOS release.

## Apple OpenGL Risk

Apple OpenGL remains the limiting macOS rendering dependency. The current policy
accepts this for the experimental release line because stock Quake 4 asset
compatibility and renderer parity are higher priorities than changing graphics
APIs prematurely.

Known policy consequences:

- macOS remains capped by Apple OpenGL behavior, including the OpenGL 4.1 core
  ceiling and fragile compatibility-context behavior.
- Renderer compatibility work should continue to harden the OpenGL path, not
  hide OpenGL dependency behind Metal wording.
- Release notes must call out any renderer limitation found during signoff.

## Native Metal Decision Gate

No native Metal renderer is selected for the current release line.

If native Metal becomes a target, create a separate renderer design plan before
implementation. That plan must cover:

- Stock Quake 4 material, shader, interaction, and lighting parity.
- BSE effect rendering and lifetime behavior.
- Shader translation or replacement strategy.
- Screenshot, readback, video, and diagnostic capture behavior.
- RenderDoc or Xcode GPU capture workflow.
- Performance counters, renderer metrics, and failure diagnostics.
- Fallback and rollback behavior when native Metal cannot initialize.
- Package naming, release notes, and signoff evidence for native Metal as a new
  renderer path.

Until that plan exists and has matching tests, docs must continue to call the
current package `Metal bridge`.

## Native Cocoa/OpenGL Backend Policy

The legacy macOS native backend selected by `-Dplatform_backend=native` is
comparison-only diagnostic infrastructure.

It is retained to:

- Compare SDL3 regressions against the older Cocoa/OpenGL path.
- Preserve isolated fallback code while SDL3 macOS support matures.
- Keep crash-resistant guards around legacy Cocoa, NSOpenGL, and Carbon
  boundaries.

It is not a supported macOS release backend. Release packages, release
workflows, first-class support claims, and user-facing install docs must use the
SDL3 backend unless the project makes a new explicit support decision.

Native backend maintenance rules:

- Carbon and NSOpenGL remain isolated to the native fallback boundary.
- `macos_graphics_bridge=metal` must require `platform_backend=sdl3`.
- CI policy tests must continue to block Carbon/NSOpenGL leakage into the SDL3
  release path.
- Native backend results are diagnostic unless a future plan adds dedicated
  build, package, and runtime signoff coverage.
- The native backend is not kept indefinitely by default. Once SDL3 OpenGL and
  Metal bridge coverage is mature across the documented macOS floor and latest
  public macOS, create a separate native-backend removal plan before deleting or
  expanding the legacy path.

## Release-Gate Summary

Before macOS support can move beyond the current experimental wording:

- OpenGL and Metal bridge package evidence must be current for the supported OS
  matrix.
- Release notes must state that the Metal package is a bridge, not native Metal.
- Release notes must not imply that `platform_backend=native` is supported for
  release packages.
- Any future native Metal or native backend support claim must have a separate
  design plan, CI/build lane, package validation, and real Apple-hardware
  runtime evidence.
