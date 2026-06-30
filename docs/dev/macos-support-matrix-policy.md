# macOS Support Matrix Policy

Updated: 2026-06-30

This document defines the current macOS architecture and OS-version matrix for
openQ4 releases. It records what is supported now, what is deliberately not
claimed, and what evidence is required before the matrix can expand.

## Current Release Matrix

Current macOS release artifacts are experimental Apple Silicon/arm64 only:

- `openq4-<version>-macos-arm64-opengl.dmg`
- `openq4-<version>-macos-arm64-metal.dmg`
- `openq4-<version>-macos-arm64-opengl-unsigned.tar.gz` for experimental fallback output
- `openq4-<version>-macos-arm64-metal-unsigned.tar.gz` for experimental fallback output

The current hosted CI and manual release lanes use GitHub-hosted `macos-15`
runners for configure, build, staging, package, signing, notarization, and
static validation. Hosted runner success is not a replacement for real
Apple-hardware runtime signoff.

## Architecture Policy

The current policy is `arm64 only`.

Not supported by current user-facing macOS releases:

- Intel Mac / `x86_64` packages.
- universal2 packages.
- Rosetta as a supported compatibility layer.

Local x86_64 or Rosetta experiments may be useful for development, but they do
not change the published support matrix. User-facing docs and release notes
must continue to say Apple Silicon/arm64 only until the requirements below are
met.

Before claiming Intel Mac or universal2 support, openQ4 must have:

- An explicit release-lane decision: separate `macos-x64` artifacts or
  universal2 artifacts.
- Matching openQ4 and `openQ4-game` CI coverage for every claimed architecture.
- `lipo -archs` validation for the app executable, loose client, dedicated
  server, and both SP/MP game dylibs.
- `otool -L` and install-name validation after any `lipo` combine step.
- Developer ID signing, notarization, stapling, `spctl`, and `hdiutil`
  validation after final artifact creation.
- Real Apple-hardware runtime signoff on every claimed architecture.
- Artifact names and docs that distinguish `macos-arm64`, `macos-x64`, and
  `macos-universal2` when more than one architecture is published.

## OS-Version Policy

The current packaged compatibility floor is `macOS 11` for the experimental
Apple Silicon/arm64 release line. Meson sets `-mmacosx-version-min=11.0`, app
metadata sets `LSMinimumSystemVersion` to `11.0`, and user-facing docs say
macOS 11 or later.

The validation policy is:

- Treat `macOS 11` as a documented floor, not as proven first-class support,
  until floor-version signoff exists on Apple Silicon hardware or a compliant
  Apple-hardware VM.
- Treat the latest public macOS release as the rolling current-version signoff
  target for every release that changes platform, packaging, input, audio,
  renderer, loader, or game-module behavior.
- Record both floor-version and latest-version results before promoting macOS
  beyond experimental.
- Keep the published OS range no broader than the evidence in
  `docs/dev/macos-signoff-evidence.md`.

Before changing the floor, update all of these together:

- Meson deployment target.
- `tools/build/package_nightly.py` app metadata.
- `.github/workflows/manual-release.yml` package validation.
- `BUILDING.md`.
- `docs/dev/platform-support.md`.
- `docs/user/getting-started.md`.
- `assets/release/README.html`.
- `docs/dev/macos-signoff-evidence.md`.
- macOS matrix and package policy tests.

## Evidence Requirements

Every completed macOS signoff record must include:

- Architecture policy and actual CPU architecture.
- OS matrix role: floor candidate, latest public macOS, or current hosted CI
  runner.
- macOS version and build.
- Kernel version.
- Xcode and macOS SDK version.
- Hardware model and CPU.
- Graphics bridge variant.
- OpenAL provider.
- Package artifact names and signing/notarization status.

First-class macOS support requires at least:

- OpenGL and Metal bridge signoff on the oldest supported macOS floor.
- OpenGL and Metal bridge signoff on the latest public macOS release.
- Real Apple-hardware or compliant Apple-VM evidence for every claimed
  architecture.
- Matching release notes that identify any untested, unsupported, or
  experimental macOS paths.
