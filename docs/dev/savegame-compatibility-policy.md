# Savegame Compatibility Policy

This policy defines which openQ4 single-player saves the current runtime may
load. Reliability mechanisms and implementation detail are documented in
[Savegame Reliability](savegame-reliability.md).

## Current Policy at a Glance

| Save payload | Current decision |
| --- | --- |
| Version 3, exact wire-ABI stamp, build at or above the verified floor, valid integrity/footer | Supported format path; build/source drift is diagnostic only |
| Version 3, build below the verified floor | Rejected before map teardown |
| Version 3, different wire-ABI stamp | Rejected before map teardown |
| Version 2, any tuple | Rejected before map teardown |
| Unstamped legacy payload | Accepted only when its marker equals the current build and the runtime stamp is Windows/MSVC x64 little-endian `raw1` |
| Empty, negative-length, or over-512-MiB `.save` | Rejected before header or CRC preflight |
| Payload version newer than 3 | Rejected; no forward-compatibility guessing |
| Multiplayer/dedicated save | Not a supported player-facing feature |

The outer session-header reader accepts outer versions `1834`, `0`, and `1` so
that the payload can be examined. Passing that outer check does not override the
payload policy in this document.

## Version 3

Version 3 is the only format written by current builds. The complete file must be
no larger than 512 MiB, including its integrity trailer, and the payload wire-ABI
stamp must exactly equal the running build's stamp:

```text
<os>-<compiler-abi>-<architecture>-<endian>-raw1
```

Examples include `windows-msvcabi-x64-le-raw1`,
`linux-itaniumabi-x64-le-raw1`, and `macos-itaniumabi-arm64-le-raw1`.

Within v3, build number, generated source hash, and source-file count are recorded
for diagnosis but do not reject a save. The policy assumes every wire-incompatible
change bumps the gameplay compatibility version. A v3 save from another build is
therefore eligible only when the exact ABI stamp matches and both builds correctly
honored the v3 schema.

Eligibility is not runtime certification. For example, a Linux arm64 v3 save may
be format-eligible on another Linux arm64 `raw1` build, but it is supported for a
release only when that platform has matching candidate runtime evidence.

One released v0.10 snapshot used version 3 before two player liquid-state fields
were added:

```text
build 1
source 19351be39d2d4077a74294c0442707ef9565fc7a2fa9af9b81e05fc9aca8b220
404 files
windows-msvcabi-x64-le-raw1
```

The SP and MP readers recognize only that complete tuple when deciding that
`idPhysics_Player::swimSpeed` and `idPlayer::nextLiquidSurfaceSoundTime` are
absent. Both fields receive safe defaults, and every unrelated v3 source snapshot
continues to use the current layout. A second v0.10 release-build defect came
from MSVC identical-code folding: the former pointer-address comparison could
omit the empty `idPhysics` class frame. Current readers use the surrounding sync
sequence to accept that known omission, while current writers derive class-frame
ownership from source declarations so optimization can no longer alter the wire
format.

## Version 2 Is No Longer Claimed

Version 2 did not carry its own wire-ABI field, and support for it was expressed
as an allowlist of `(build, source SHA-256, source-file count)` tuples. On
2026-08-31 every one of those tuples that a real save existed for was tested:
builds 544 (three distinct hashes), 556, and 614. All five desynced part way
through the restore, and did so only after the running map had been torn down --
the outcome this policy exists to prevent.

The allowlist was therefore removed rather than corrected. It asserted support
that had never been demonstrated against a real save, and the tuples that were
demonstrable were all wrong. Version 2 payloads are now refused during preflight
with a message naming their provenance. Current writers never create v2 saves.

Restoring a v2 claim requires the same evidence any other claim does: a reviewed
byte-layout comparison, a successful real SP save/load of a save in that exact
format, corruption-contract coverage, and a release-note entry.

## The Verified Build Floor

Within version 3, support is expressed as the oldest build whose layout a
verified decoder covers (`SESSION_OPENQ4_SAVEGAME_MINIMUM_SUPPORTED_BUILD`,
currently 661). Payloads below it are refused during preflight.

661 is where the first of the two player liquid fields entered the save. Builds
from 661 upward restore cleanly, including the range that previously failed;
below it the only sample available, the released v0.10 payload (build 1),
desynced part way through the restore even with its bounded field decoder, so
that lineage is no longer claimed either.

Lowering the floor requires a real save in the older format that restores
cleanly, not a layout argument alone.

## Fields Added Within A Version

A field added to a saved class without a version bump splits the format in two
even though both halves report the same version. Each such field therefore
carries the build that introduced it, and the reader consults that build rather
than a snapshot identity:

| Field | Added by | Build |
| --- | --- | ---: |
| `idPhysics_Player::swimSpeed` | openQ4-game `2cc5a61`, 2026-08-13 | 661 |
| `idPlayer::nextLiquidSurfaceSoundTime` | openQ4-game `d06a09d`, 2026-08-19 | 721 |

These two landed six days apart, so a save can legitimately carry the first and
not the second. A single boolean gated both against one hard-coded v0.10 tuple,
which answered "present" for every other v3 payload: every save written between
those builds read a field its file does not contain and desynced the rest of the
restore. Adding a field this way is still discouraged -- a version bump is
clearer -- but when it happens the threshold must be recorded here and in both
game trees.

## Unstamped Legacy Payloads

Unstamped payloads have no source snapshot, sync sequence, footer, or integrity
trailer. They are eligible only when:

- the first payload integer equals the running `BUILD_NUMBER`; and
- the running wire ABI is exactly `windows-msvcabi-x64-le-raw1`.

This is a narrow recovery path, not a general promise to load retail Quake 4,
arbitrary old openQ4, another operating system, or another architecture. Legacy
payloads receive bounded restore checks where available but cannot gain the v3
whole-file CRC/footer retroactively.

## Backward Compatibility

Backward compatibility means a newer runtime reading an older save. It is
supported only through an explicit decoder or allowlist:

- current v3 on the exact ABI path at or above the verified build floor;
- fields added within v3, through their recorded per-field build thresholds
  and legacy empty-physics-frame recognition;
- the six exact v2 snapshots above on Windows x64 `raw1`; and
- the narrow same-build unstamped Windows x64 legacy path.

Unsupported saves fail closed before map teardown when total size, header,
version, ABI, allowlist, integrity, footer, or map preflight fails. The 512 MiB
limit applies equally to v3, approved v2, and unstamped legacy input. No in-place
conversion or repair tool is currently provided. Players should retain a copy of
important slots before upgrading, especially when moving from an unlisted
development build.

Removing an existing approved decoder is a release-policy change and requires an
upgrade note and, where practical, a migration path.

## Forward Compatibility

Forward compatibility means an older runtime reading a save written by a newer
format. It is not promised. Unknown payload, footer, integrity, or raw-layout
versions are rejected instead of being parsed as the nearest known layout.

When version 4 is introduced, its writer must not claim v3 compatibility unless
the emitted v3 bytes and semantics genuinely remain valid for an existing v3
reader. A new reader may retain a bounded v3 decoder, but the old reader is not
required to understand v4.

## Platform and Architecture Policy

The `raw1` stamp is deliberately restrictive because native-layout fields remain.
Current policy is:

- same OS ABI, compiler ABI, architecture, endian, and raw revision: format may
  be eligible;
- different operating system, compiler ABI, architecture, endian, or raw
  revision: reject;
- x64 and arm64 saves are not interchangeable;
- Windows, Linux, and macOS saves are not interchangeable;
- compilation on a platform does not prove save/load runtime support there;
- same-platform eligibility does not supersede the project's platform support
  and release evidence gates.

Cross-platform transfer can be considered only after the remaining raw inventory
is normalized, a portable wire revision is defined, fixtures are verified on each
supported ABI, and the policy is deliberately revised. It must not be inferred
from the typed fields already migrated.

## Schema Governance

The payload version is the compatibility boundary. Any change that alters the
ordered byte stream or the interpretation of a valid value must be classified
before merge.

### A payload-version bump is required when

- a field is added, removed, reordered, resized, or changes encoding;
- a list gains or loses an on-wire count or sentinel;
- an object/reference representation changes in a way an existing reader cannot
  preserve;
- native structure layout or raw-write meaning changes;
- SP and MP would otherwise interpret the same version differently; or
- a valid old payload would be consumed at a different offset.

### A payload-version bump is not normally required when

- a write is replaced by typed calls that emit byte-for-byte identical v3 data;
- a reader adds a bound that only rejects values impossible in a valid save;
- diagnostics, source hashing, lookup performance, or transaction handling change
  without changing payload bytes; or
- sidecar validation changes without changing the `.save` payload schema.

Every versioned change must update, in one change set:

1. the engine preflight constants and decoder;
2. both SP and MP GameLib constants, writers, and readers;
3. the wire-ABI/raw revision when native layout changes;
4. footer/integrity versions when those structures change;
5. corruption models, raw-write inventory, and ABI contracts;
6. this policy, reliability documentation, and player-facing upgrade notes.

The generated source hash is evidence and diagnosis for v3, not a substitute for
this versioning decision. The v0.10 decoder above is a narrowly reviewed repair
for a format that had already shipped without a required version bump; it is not
precedent for adding fields within v3.

## Failure and User-Message Policy

Compatibility failures must identify the rejected layer—total size, outer
version, payload version, ABI, v2 snapshot, legacy build, CRC, footer, or
map—without attempting a partial restore. The running map remains active for
failures caught by engine preflight. Deeper object-specific semantic failures can
occur after teardown; they abort that restore and use the existing fresh-map
initialization fallback. There is no staged-world rollback that preserves the
former map after this point, and the failure must report the closest class/field
boundary available.

Save slots may remain visible when their bounded outer header is readable; menu
discovery intentionally does not checksum all payloads. Selecting an incompatible
slot must produce the precise failure and leave the original file untouched.

Release notes must state any compatibility break, supported older decoder, lack
of cross-platform transfer, and any required player action. They must not describe
build success or static ABI checks as runtime save/load validation.
