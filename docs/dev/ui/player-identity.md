# Multiplayer player identity, 23 October 2026

This bounded UI-03/UI-09 increment keeps Players selections and local
friend/mute marks attached to the connection that was selected. A replacement
occupant of the same client slot starts unmarked, and the statistics return to
the local player when the selected connection disappears or is replaced.
Death/respawn, score updates and name changes within the same connection retain
the marks. Friends are session marks, not persistent account identities.

## Contract

The game publishes the entity's packed spawn ID as unsigned decimal string
state beside each row's client number and beside the selected statistics.
Strings preserve all 32 bits, including IDs with the high bit set. The
multiplayer menu cover handshake is now `mp.protocol 2`; engines and game
modules must match. An older module keeps the classic menu.

`mpSelectPlayer`, `mpMute` and `mpFriend` use the existing `session.menu`
operation with three typed arguments: `command`, numeric `client`, and string
`identity`. Event evaluation freezes these operands. Later changes to
`card.client` or `card.identity` cannot redirect queued requests. The adapter
requires an integer client from 0 to 31 and a canonical unsigned 32-bit decimal
identity. The session forwards bounded operands; the game checks membership
and exact connection identity immediately before selection or mutation.

Interactive rows and Mute/Friend buttons declare optional string `context`
expressions. A changed context cancels an armed press and invalidates queued
control actions. Returning to the old context does not revive them. Unchanged
context survives a score refresh; modal/document identity still guards restored
or replaced views. Context tokens are transient and are never saved. Buttons
without `context` retain their existing activation behavior.

The player object's friend/mute accessors also record the target's packed spawn
ID. They refuse stale generations, empty slots and non-player entities, including
replacement at the same address before any disconnect notification. Clearing a
mark remains valid after its target has disappeared. This adds no network or
save-format fields, display strings or artwork.

## Validation

The native schema/interaction and production roster tests cover typed contexts,
atomic failed updates, held keyboard/pointer input, queued activation, unchanged
score refresh, identity changes back to a prior value, restore and the actual
Escape and Summary documents. Adapter/session tests verify captured operands and
reject missing, malformed, fractional, out-of-range and injected arguments.
`tools/tests/ui_player_identity.py` compiles the actual player accessors and game
dispatch branch against bounded entity doubles for all 32 slots. Removing each
of the friend, mute and dispatch identity checks must fail an assertion.

`tools/ui/capture_player_identity.py` runs a hidden, windowed loopback match on
`mp/q4dm1`. Semantic engine commands select and mark a bot, replace it in the
same slot during a held press and after a queued row event, and then explicitly
select/mark its replacement. Engine render-target screenshots include gameplay,
each menu checkpoint and the return to gameplay. No OS input or capture is used.
The full Windows build/stage and 12 targeted native suites pass. The adapter
passes its production case and rejects 20 compiled mutations; player-state
checks reject three guard-removal mutations. Both live matches pass: OpenGL at
1280×720 and Vulkan at 1920×1080, with 14 reviewed engine screenshots, no
retained-UI warnings and no errors. Stock/bot asset warnings remain. Current
execution results and source/binary bindings are recorded in
`.tmp/ui-player-identity/validation-evidence.json`.

## Remaining scope

UI-03 remains open. Kick/vote/admin targets and Match Control rows need their
own identity treatment. Remote voice-mute packets still carry a destination
slot: a packet delayed until after replacement can target that new connection
on the server. This increment qualifies local marks and the retained menu
dispatch boundary, not that transport race. It does not qualify persistent
friends, remote reconnect timing, all input devices, other platforms or the
whole UI corpus. No product requirement, migration entry or final gate is
accepted by this evidence.
