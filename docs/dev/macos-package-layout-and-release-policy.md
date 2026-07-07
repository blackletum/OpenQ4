# macOS Package Layout And Release Policy

Updated: 2026-06-30

This document records the current macOS package support contract for openQ4.
It is intentionally conservative while macOS remains experimental.

## Current Layout Decision

The supported macOS package layout is an adjacent package root, not a
self-contained drag-only app bundle.

Supported package roots contain these entries side by side:

- `openQ4.app`
- `openQ4-client_<arch>`
- `openQ4-ded_<arch>`
- `baseoq4/`
- loose runtime support files such as icons, splash assets, docs, and version metadata

`openQ4.app` is the Finder entry point, but it is not currently the full runtime
container. The app must stay beside `baseoq4/` and the loose runtime files until
the project deliberately migrates to `openQ4.app/Contents/Resources` or another
self-contained layout.

Release packages and archive validation require the bundle to be named
`openQ4.app`. Runtime app-bundle detection still recognizes renamed `.app`
bundles that keep the standard `*.app/Contents/MacOS` layout, so a hand-renamed
bundle can report the same adjacent package-root diagnostic instead of falling
through to generic base-path probing.

## Supported Launch Flows

Supported for experimental macOS signoff:

- Double-click `openQ4.app` from the mounted signed/notarized DMG payload.
- Copy the whole package payload to a user-writable folder, keeping
  `openQ4.app`, `baseoq4/`, and the loose runtime files together, then
  double-click `openQ4.app`.
- Launch from Terminal with the working directory at the package root.
- Launch the loose `openQ4-client_<arch>` or `openQ4-ded_<arch>` binaries from
  the package root for diagnostics or dedicated-server work.

Unsupported until a self-contained bundle migration is implemented:

- Moving only `openQ4.app` away from its package root.
- Copying only `openQ4.app` into `/Applications`.
- Deleting or relocating adjacent `baseoq4/`, game dylibs, or loose runtime
  support files without a documented replacement path.

If a user launches an app-only move, the expected current result is a clear,
actionable failure that identifies the missing adjacent runtime payload. If the
failure is confusing, that is a packaging UX bug to fix before first-class
macOS support.

The current startup diagnostic title is:

```text
openQ4.app adjacent package root is incomplete
```

The diagnostic must name this contract:

```text
Expected adjacent package-root contract: `openQ4.app`, loose binaries, and `baseoq4/` together
```

It must also make this user action clear:

```text
Moving only `openQ4.app` to `/Applications` is not supported yet
```

Moving only `openQ4.app` is unsupported until a self-contained bundle migration is implemented.

## Symbol Artifacts

Runtime macOS packages may include the small root `SYMBOLS.txt` manifest so
support archives can identify matching binaries and dSYM archives.

Runtime DMGs and tarballs must not include `.dSYM` bundles or other debug
payloads. dSYMs are published as separate artifacts named like
`openq4-<version>-macos-arm64-opengl-symbols.tar.xz` and
`openq4-<version>-macos-arm64-metal-symbols.tar.xz`.

See [macOS Symbolication Workflow](macos-symbolication.md) for the crash-log
pairing process.

## Support Path Reports

`collect_macos_support_info.sh` writes `package/path-resolution.txt` without
launching openQ4. The report records the package root, app path, expected loose
runtime paths, expected `baseoq4/` path, and any copied log lines that mention
`fs_basepath`, `fs_cdpath`, or `fs_savepath`.
If `HOME` is absent in a sparse launch environment, the collector keeps the
package-local log checks and records archive notes instead of aborting on
home-scoped log or DiagnosticReports paths.

The same archive also includes `package/binary-architecture.txt` and
`package/dylib-dependencies.txt` without launching openQ4. These reports record
read-only `file`/`lipo` architecture output, `otool -L` dependency output, and
`otool -D` install names for game modules so maintainers can confirm the
package shape when users report architecture, loader, or `@loader_path`
failures.

`package/signing.txt` uses the same Gatekeeper assessment shape as release
validation, including `spctl --assess --type execute --verbose=4` and
`xcrun stapler validate` where the local tools are available. Current release
packages remain arm64-only, but the support archive also inspects any loose
`x64` or `x86` executable names that happen to be present so accidental
wrong-arch package contents are visible in intake data without becoming support
claims.

`system/rosetta.txt` records the collector process architecture and
`sysctl.proc_translated` state. Rosetta remains outside the supported release
matrix, but the report helps maintainers distinguish unsupported translated
sessions from native arm64 package failures.

## Signoff Evidence Requirements

Every completed macOS signoff archive for a release candidate must record:

- Finder launch from the mounted DMG or final release image.
- Finder launch after copying the whole package payload to a user-writable
  location.
- Terminal launch from the package root.
- The app-only move result, either working or explicitly recorded as unsupported
  with a clear error.
- `fs_basepath`, `fs_cdpath`, and `fs_savepath` log lines from Finder/copied
  package and terminal launches.
- Gatekeeper behavior for the package under test, including `spctl` assessment
  for signed/notarized DMGs or the expected approval friction for unsigned
  development archives.

The signoff evidence index keeps these fields even while no accepted completed
archive has been recorded yet.

## Release Policy

First-class macOS releases require signed and notarized DMGs for every supported
macOS artifact. A first-class macOS release job must fail if Apple Developer ID
signing or notarization credentials are missing.

Unsigned `-unsigned.tar.gz` archives are allowed only for experimental or
development fallback output. They must stay clearly marked as unsigned and
unnotarized in artifact names, release notes, and user-facing docs.

Runtime and symbol archive validation must match the declared package format. A
`.tar.gz` runtime artifact is opened as gzip data, a `.tar.xz` runtime artifact
is opened as xz data, and macOS dSYM symbol archives are opened as
xz-compressed tarballs. Mislabeled tarballs fail validation instead of relying
on auto-detection.

Release archive, DMG, notarization, and symbol archive outputs must not be
written inside the package or symbol tree they are archiving.

Credentialed macOS release artifacts must keep these checks mandatory:

- Developer ID Application signing.
- Hardened Runtime.
- entitlement validation that rejects App Sandbox and `get-task-allow` unless a
  reviewed sandbox/file-access design exists.
- app and DMG notarization/stapling.
- `codesign`, `spctl`, `xcrun stapler validate`, and `hdiutil verify`.

## Migration Trigger

If the project moves to a self-contained app bundle, update all of these
together:

- package allowlists in `tools/build/package_nightly.py`
- app bundle generation and resource layout
- game dylib install names and loader expectations
- signing, notarization, and DMG validation
- signoff archive validation and evidence fields
- `BUILDING.md`, `docs/dev/platform-support.md`, user docs, release README, and
  release notes
- the macOS compatibility/support plan checklist
