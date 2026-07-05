# Experimental macOS Support Data

macOS support is experimental Apple Silicon/arm64 support. If openQ4 crashes on
macOS, a complete report is much more useful than a screenshot because the
maintainer may not have access to a Mac.

This page is especially useful for startup crashes like
[GitHub issue #73](https://github.com/themuffinator/openQ4/issues/73), where
the last visible lines can be in the Apple OpenGL 2.1, ARB2, and interaction
program startup path.

If your log contains `ARB2 interaction driver bypass`, openQ4 intentionally
used a degraded Apple OpenGL 2.1 compatibility fallback that skips ARB2 light
interaction drawing to avoid the known startup crash. Please still attach the
terminal output and support archive if it crashes after that line. Newer builds
also print post-bypass breadcrumbs such as `ARB2 interaction bypass state
restored`, `ARB2 interaction bypass light scale`, `ARB2 interaction bypass
ambient rescue`, and `ARB2 interaction bypass frame tail` to identify the next
classic renderer operation reached.

## What To Attach

Attach or paste these items when possible:

- The exact openQ4 version and package artifact name, such as
  `openq4-0.6.8-macos-arm64-metal.dmg`.
- Whether you used the OpenGL package, the Metal bridge package, or both.
- Whether you launched `openQ4.app`, `openQ4-client_arm64` from Terminal,
  `openQ4-ded_arm64` from Terminal, or more than one path.
- macOS version/build and hardware model. `sw_vers` plus the Apple Silicon
  generation, such as M4 Max or M5, is enough.
- Full terminal output as text, not only a screenshot.
- `~/Library/Application Support/openQ4/baseoq4/logs/openq4.log` when it
  exists.
- The matching `.ips` or `.crash` file from
  `~/Library/Logs/DiagnosticReports` when macOS writes one.
- `SYMBOLS.txt` from the package root or from `package/SYMBOLS.txt` inside the
  support archive, if present.
- `package/build-metadata.txt` from the support archive, or the package
  `VERSION.txt` lines for `openq4_commit` and `openq4_game_commit` when
  present.
- OpenAL audio lines from `openq4.log` when present: `OpenAL vendor:`,
  `OpenAL renderer:`, `OpenAL active device:`, and any `OpenAL EFX ...`
  warning/status lines.
- `system/rosetta.txt`, `logs/renderer-summary.txt`,
  `package/binary-architecture.txt`, `package/dylib-dependencies.txt`,
  `package/signing.txt`, and
  `package/quarantine.txt` from the support archive when present.
- Whether `openQ4.app`, `baseoq4/`, the loose client/dedicated binaries, and
  support files stayed together as one adjacent package root.

Do not attach retail Quake 4 `q4base/*.pk4` assets.

## Capture Terminal Output

From Terminal, run the client from the package root and redirect output to a
text file:

```sh
cd "/path/to/openQ4 package"
./openQ4-client_arm64 > ~/Desktop/openq4-terminal.txt 2>&1
```

For dedicated-server reports, run the dedicated binary from the same package
root and redirect output separately:

```sh
cd "/path/to/openQ4 package"
./openQ4-ded_arm64 +set dedicated 1 > ~/Desktop/openq4-ded-terminal.txt 2>&1
```

If the app, client, or dedicated server crashes, attach the matching terminal
text file or paste its full contents into the issue.

For issue #73 style reports, keep the lines around these markers:

- `----- R_InitOpenGL -----`
- `R_ReloadARBPrograms`
- `interaction color mode`
- `renderer startup phase`
- `last renderer startup phase`
- `first ARB2 interaction handoff`
- `ARB2 interaction driver bypass`
- `ARB2 interaction bypass state restored`
- `ARB2 interaction bypass light scale`
- `ARB2 interaction bypass ambient rescue`
- `ARB2 interaction bypass frame tail`
- `ARB2 light interaction`
- `Renderer upload manager`
- `using ARB2 renderSystem`
- `Unsupported Apple OpenGL 2.1 compatibility path`
- `SimpleInteraction.vfp`
- `fatal signal SIGSEGV`

## Run The Support Collector

macOS packages include `collect_macos_support_info.sh` in the package root. Run
it from Terminal:

```sh
cd "/path/to/openQ4 package"
./collect_macos_support_info.sh
```

If macOS refuses to execute the script directly, run it through `sh`:

```sh
sh ./collect_macos_support_info.sh
```

The script creates an archive named like:

```text
openq4-macos-support-YYYYMMDD-HHMMSSZ.tar.gz
```

Review the archive before attaching it publicly. The collector redacts
`/Users/<name>` paths and email-like strings, does not dump the environment,
does not launch openQ4, and does not copy retail `q4base` PK4 assets.
It also does not follow symlinked package, log, or crash-report inputs; skipped
symlinks are recorded in the relevant report files instead of copying or
inspecting their targets.
If a DiagnosticReports filename contains unusual characters, the collector
records a skipped-file note instead of placing that name into the support
archive.
Generated support archives are private by default and the collector refuses to
overwrite an existing archive with the same timestamp. Very large copied text
files are bounded: package/log text is limited to the final 2 MiB, and crash
reports are limited to the final 8 MiB, with a truncation note written into the
copied file.

The archive includes `package/path-resolution.txt`. That file records the
package root, app path, expected loose runtime paths, and any copied log lines
that mention `fs_basepath`, `fs_cdpath`, and `fs_savepath`. It does this
without launching openQ4.

The archive also includes `package/build-metadata.txt`. That file copies the
package/app `VERSION.txt` metadata lines, including `openq4_commit`,
`openq4_dirty`, `openq4_game_commit`, and `openq4_game_dirty` when the package
contains them, plus the `baseoq4/` game module filenames.

The archive also includes `logs/openal-summary.txt` when the collector can read
an existing log. It copies the OpenAL vendor, renderer, version, requested/default
or active device name, and `OpenAL EFX` warning/status lines without launching
openQ4.

The archive also includes `logs/renderer-summary.txt` when an existing log is
available. It copies renderer startup, driver-quirk, ARB2 interaction, and
fatal-signal breadcrumbs such as `R_InitOpenGL`, `Renderer driver quirks`,
`ARB2 interaction driver bypass`, and `fatal signal SIGSEGV`.

The archive also includes `system/rosetta.txt`. That file records `arch`,
`uname -m`, and `sysctl.proc_translated` output from the collector process so
maintainers can spot unsupported Rosetta or translated-Terminal reports.

The archive also includes `package/binary-architecture.txt`. That file records
read-only `file` and `lipo -archs` output for the app executable, loose
client/dedicated binaries, and game modules so maintainers can spot wrong-arch
or universal-binary surprises.

The archive also includes `package/dylib-dependencies.txt`. That file records
read-only `otool -L` dependency output and `otool -D` game-module install names
so maintainers can spot unexpected non-system dependencies or broken
`@loader_path` module names.

The archive also includes `package/signing.txt`. That file records read-only
`codesign`, `spctl --assess --type execute --verbose=4`, and
`xcrun stapler validate` checks for the app bundle, loose binaries, and game
modules without launching openQ4.

The archive also includes `package/quarantine.txt`. That file lists
extended-attribute names and reports whether `com.apple.quarantine` is present;
it does not copy extended-attribute values.

Current release packages remain Apple Silicon/arm64 only, but the architecture,
signing, Gatekeeper, and quarantine reports also inspect any loose `x64` or
`x86` openQ4 executable names if they are present. That makes wrong-arch or
future cross-arch package contents visible without treating those paths as
supported release targets.

## Symbolication Data

macOS packages include a small `SYMBOLS.txt` manifest at the package root. It
names the matching `openq4-<version>-macos-arm64-<bridge>-symbols.tar.xz`
dSYM archive and lists the packaged binary checksums and Mach-O UUIDs.

If you attach a `.ips` or `.crash` report, also attach `SYMBOLS.txt` or run the
support collector so it can copy the file to `package/SYMBOLS.txt`. Maintainers
use that manifest to choose the correct symbol archive; the support archive does
not include `.dSYM` debug bundles.

Maintainer details live in [macOS Symbolication Workflow](../dev/macos-symbolication.md).

## Manual Locations

If the helper is not available, collect these manually:

```text
~/Library/Application Support/openQ4/baseoq4/logs/openq4.log
~/Library/Logs/DiagnosticReports/openQ4*.ips
~/Library/Logs/DiagnosticReports/openQ4*.crash
~/Library/Logs/DiagnosticReports/openQ4-client*.ips
~/Library/Logs/DiagnosticReports/openQ4-client*.crash
~/Library/Logs/DiagnosticReports/openQ4-ded*.ips
~/Library/Logs/DiagnosticReports/openQ4-ded*.crash
```

Also include the output of:

```sh
sw_vers
uname -a
system_profiler SPHardwareDataType
system_profiler SPDisplaysDataType
```

## Package Layout Check

The current macOS package is not a self-contained `.app` bundle. Keep these
items together:

```text
openQ4.app
openQ4-client_arm64
openQ4-ded_arm64
baseoq4/
collect_macos_support_info.sh
```

Moving only `openQ4.app` to `/Applications` is not supported yet. If you tried
that, mention it in the issue because it changes the failure mode.
