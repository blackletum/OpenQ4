# High-refresh presentation owners

> **Development status:** This document describes the current source tree and its evidence, not the contents of the latest available 0.13.2 package. The 0.14.0 release was withdrawn. See [release status](../user/release-status.md).

Contract for HF-03 through HF-08 in the [roadmap](roadmap.md), 23 October 2026,
extended 24 October 2026.
It extends the [clock ownership contract](presentation-clock.md) (HF-01/HF-02),
which still owns the clocks themselves. Nothing here changes authoritative
simulation, usercmds, save formats, network timestamps or demo timing. Every
owner below is draw-only: it moves what is drawn between two authoritative
60 Hz samples, or toward input the game has not applied yet, and hands the
authoritative pose back before gameplay can read it.

## Late look (HF-03)

`in_presentationView 1` (default `0`) turns the drawn view with input that has
not reached the game yet. Previous-to-current interpolation deliberately draws
the eye a tick behind; mouse motion then waited for the next usercmd on top of
that. Late look removes both delays for the input-driven part of the view.

**Engine sampler.** `idUsercmdGen::GetPresentationViewAngles` (game API 54
passes the generator through `gameImport_t`) returns the angles the next
usercmd would carry if it were built now: the last built usercmd, plus mouse
and gyro motion drained under the async lock and scaled exactly as `MouseMove`
scales it (CPI, sensitivity, acceleration, zoom), plus the elapsed share of the
coming tick for held look keys and stick deflection
(`openq4::PresentationRateShare`). It writes no usercmd field, never consumes
`mouseDx/mouseDy` and never shifts the build stamp, so the next usercmd still
receives all of the input. The retired `GetPresentationViewDelta` reported only
pending mouse motion and would have dropped usercmds that were built but not
yet run (`com_asyncInput 1`); the game now compares absolute angles with the
usercmd it actually applied, which counts buffered usercmds exactly once.

The sampler returns false, and the view stays on the interpolated authoritative
path, when late look is off, input is inhibited (console, menus, retained UI,
or the native input driver owning the event pump, so the draw never drains it),
a radial selector owns the look input, a command or render demo drives the
game, or a VR headset owns the view. Android touch look arrives as a
per-usercmd delta with no continuous sample and is excluded.

**Filtered versus late look.** `m_smooth` above `1` and `m_filter` above `0`
blend several usercmds of motion. Showing raw motion ahead of that blend would
turn the view one way and then settle it another, so filtered input keeps the
authoritative view. This is a deliberate policy, not a missing feature.

**Game consumer.** `idPlayer::UpdatePresentationLook` runs once per prepared
draw, inside `CalculateRenderView` and only while
`idGameLocal::IsPresentationSceneActive()`. It freezes one correction:
`(current view + unapplied input) − interpolated view` in yaw and pitch, with
pitch clamped exactly as `UpdateViewAngles` clamps it
(`openq4::ComputePresentationLook`). `GetPresentationViewPos` applies that
correction last, in gravity-relative angles, pivoting the eye about the neck
the way `GetViewPos` places it (`g_viewNodalX/Z`). Everything held in front of
the eye follows: the interpolated view model through `GetPresentationViewPos`,
the non-interpolating view model through `ApplyPresentationLookToEyeAttachment`,
and bound effects, attached models, the GUI light, the muzzle-flash light and
the flashlight through the held view-model pose. The scope compass already uses
the drawn render view.

Late look applies only to this client's own first-person eye
(`AllowsPresentationLook`). It stands down while dead, driving a vehicle, in a
cinematic or camera view (unless noclipping), under an influence that locks the
view, during centre-view, in third person or `pm_modelView 1`, during the SP
benchmark view sweep, `g_stopTime`, demo or timedemo playback, VR, a radial
selector, or an MP Arena freeze. Any mode not named there is caught by the
coupling check: each authoritative sample records whether the view's yaw step
matched its usercmds' yaw step (`PresentationLookCoupled`); a held, scripted or
teleported view fails it, and late look stays off for that sample. More than
90° of unapplied input is a discontinuity (angle reset, map change), not motion.

Vehicles stay excluded (decision, 24 October 2026). In a vehicle, input steers
a turret, torso or body with its own turn-rate limits, and the eye rides a joint
of that vehicle. Unapplied input therefore does not tell where the next drawn
eye will point: a walker driven at 220°/s turned at 196°/s. A correction would
draw orientations the vehicle never reaches and snap back on the next tick, the
case the coupling check exists to refuse. Cockpit views stay interpolated and
exact within each tick (see the `vehicle` evidence below). Late look in vehicles
would need each vehicle's turn model evaluated on the draw, so it is not
planned.

**Displayed aim versus authoritative firing.** A shot uses the usercmd that
carries the input up to its build. Late look draws the input up to the draw,
so the drawn aim never leads the input that the next usercmd will carry.
Without late look the drawn aim trails the shot's aim by up to a tick.

**Same-fire-frame alignment.** Bound muzzle effects, attached models and the
view lights follow the drawn gun. World-space shot visuals stay where the shot
left: hitscan tracers and impacts, and `launchFromBarrel` projectiles, which
spawn at the authoritative barrel and are then drawn by the generic entity
pass. On the first draw after the firing tick, their start can differ from the
drawn barrel by the rotation since that usercmd was built, at most one draw
interval of turning. Interpolation alone shows up to a whole tick of it, in the
other direction. No cosmetic retrace is performed: a draw-frame trace would be
gameplay work, and moving a tracer would misreport where the shot went.

**Never altered.** Traces, damage, projectile spawns, network events, RNG,
`viewAngles`, `deltaViewAngles`, `firstPersonViewOrigin/Axis` and the usercmd.
[`presentation_hf_contract.py`](https://github.com/themuffinator/openQ4/blob/master/tools/tests/presentation_hf_contract.py)
rejects writes to each of them from the late-look path, confines the correction
to the presentation functions and checks the SP/MP bodies are identical;
`--mutations` proves representative regressions fail it.

## Visual ownership inventory (HF-04)

| Visual | Disposition |
| --- | --- |
| View weapon, its bound effects and attached models, the GUI light, the lightning beam's near end | Existing owners (`UpdatePresentationWeapon`, `UpdatePresentationClientEntities`, `UpdatePresentationEffects`), now also turned by late look. |
| View muzzle-flash light and flashlight | **Implemented.** Re-placed from the drawn eye and gun when the light is already live. Think still creates, re-times and frees them; the flashlight carries Think's wall back-off instead of tracing on a draw frame. A VR hand keeps its tracked placement. |
| World muzzle-flash and flashlight lights (`_WORLD`) | Suppressed in the owner's own view. Other views see them on the owner's world-model joint: an SP subview may show the authoritative tick; MP remote owners are remote actors (HF-05/MP-07). Recorded exclusion. |
| Classic muzzle-flash dynamic light | A short world light at the fire position, intentionally fixed where the shot happened. |
| Unbound client debris, brass, BSE debris and MP client gibs (`rvClientMoveable`) and their trail effects | **Implemented.** Sampled after Think's authoritative push; drawn between the two tick samples with the scale envelope; the trail is pushed with the authoritative effect clock (`rvClientEffect::PresentAt`). Physics, bounce sounds and lifetime stay in Think. |
| Projectiles, movers, actors, bound client entities and lights | Existing generic entity pass and projectile replay fixes; terminal projectile states and restore reseeding unchanged. |
| Active articulated figures (ragdolls) | **Implemented.** The animator keeps the AF pose it replaces each tick; a draw blends the previous and current authoritative model-space poses (quaternion blend) over the root's interval. The AF bodies are never re-evaluated. |
| Angular-velocity joint modifiers | Exclusion: they integrate state from their last sample and cannot be evaluated at an earlier time; the authoritative pose is copied. |
| `idBrittleFracture` shards | Exclusion: the model callback rebuilds every shard from its own physics body each tick; a per-shard history would be a new owner for a rare, short-lived effect. |
| `idMultiModelAF` (one def per body) | Exclusion: bodies own separate render defs outside the entity pass; used by few props. |
| Unbound effects an owner repositions per tick: Makron bolt and gauntlet impact, the lightning gun's world beam (third person/other players), the vehicle lock-on guide and Awakening cockpit markers | Exclusion: their endpoints are trace or target positions chosen by gameplay each tick; interpolating them would draw beams to points the game never chose. The lock-on guide can trail an interpolated target by up to a tick of its motion. |

## Alternate views (HF-05)

Cinematic and private (security) cameras, third person, third person in a
vehicle and the death camera used to put the whole scene back on the 60 Hz tick.
`idPlayer::PresentAlternateView` now samples each such view once per tick,
exactly as the authoritative path built it, and draws it between its last two
samples on the scene's clock; `IsPresentationViewInterpolated` then admits the
scene to the same clock. Camera frame commands, cuts and third-person traces
stay in the authoritative call, which is idempotent within a tick; the draw pass
never calls `GetViewParms` at another time.

A view starts again from its current sample instead of blending when it
publishes an authored cut (`renderView.forceUpdate` from `idCameraAnim`),
changes kind or camera entity (spawn identity), skips a tick, runs at an altered
tic rate, jumps further than the motion already in flight (32 units plus four
times the last step, or 90°), when `g_presentationInterpolation 0`, during demo
or timedemo playback, when third person rides a carrier that cannot be drawn
interpolated, or when third person or the death camera follows a body held on
the tick (a remote player followed in multiplayer). A static private camera samples the same pose twice, so it costs
nothing and lets the world it shows move smoothly.

Remote actors (decision, 24 October 2026). While the local client plays, every
remote body stays on the authoritative tick, outside the presentation pass.
MP-07's server rewinds a target by the age of the pose the shooter saw (the
prediction row of the [clock contract](presentation-clock.md)). Drawing remote
bodies a further fraction of a tick behind would make that pose older than the
rewind allows for. Telling the server each command's draw phase would change the
network protocol. So remote bodies stay off the presentation clock while the
local client plays; this is a decision, not a pending item. A spectating client
fires nothing, so nothing it draws is ever rewound against. While the local
player spectates, remote bodies therefore join the presentation pass: a followed
player's body, third person behind it and the death camera draw on the camera's
clock instead of stepping at 60 Hz. A free-flying spectator already used the
first-person path. The local body's admission to the entity pass (SP actor, MP
local player) keeps its contract coverage (`actor_policy`), and the spectator
rule is pinned beside it. The MP Arena tableau camera is unchanged.

Vehicle cockpits and joint-bound eyes. The driver's eye is sampled from the
seat's eye joint each tick and drawn between its two samples. The cockpit around
it is drawn from its interpolated root and a skeleton evaluated at the draw's
time. The vehicle is the eye's carrier, so if the vehicle cannot be drawn
interpolated, the eye goes back to the tick with it. The two paths differ only by
curvature within one tick. For an eye r from the root turning θ per tick, a
lerped eye and an eye carried by a slerped root differ by at most r·θ²/8. For an
animated joint with acceleration a, the difference is a·Δt²/8. A walker eye 100
units from its root turning 90°/s (1.5° per tick) gives under 0.01 units. A
torso bob of 5 units at 2 Hz adds under 0.03 units. Both are below a pixel at
any field of view. The `vehicle` runtime case (below) drives a walker cockpit:
within each tick its drawn view is linear in the draw's phase to 0.0001°.

Nested views. Remote camera views shown on GUI surfaces (`cameraTarget`, such
as security monitors) render from the camera entity's authoritative view. Stock
monitor cameras are static, and the world they show is drawn by the same
presentation pass as the main view, so they need no owner of their own.

## Cross-system behaviour (HF-06)

| System | Behaviour |
| --- | --- |
| Game/network demo playback, timedemo | Bypass presentation (`PreparePlayerSceneForRender` returns early); late look and alternate views also refuse. Deterministic playback and benchmark meaning are preserved. |
| Render demo playback | Draws recorded views; the late-look sampler refuses while `readDemo` is open. |
| Command demos | The sampler refuses while `cmdDemoFile` drives the game. |
| AVI capture | Keeps the fixed-rate tic path (presentation-clock contract). |
| TAA history and motion vectors | Camera motion, including late look, is per-frame view reprojection. Authored cuts reach the renderer through `forceUpdate`; interpolated alternate views give continuous history. |
| BSE | Draw-only pushes repeat the authoritative game time, so particles move without respawning or ageing; the renderer de-duplicates servicing per frame. |
| Audio | Not advanced by presentation; AUD-02 owns audible lifecycle. |
| VR | Late look refuses; alternate views play flat as before. |
| Save, restore, map reset | All new state is transient and reseeded: player look/alternate-view samples, debris samples, the animator's AF history; none is serialized. |
| Repeated draws, subviews | Draw owners run no Think, physics, scripts, sound or effect creation (contract tests and the gameplay guard below). Input is consumed once: live authoritative turn rates match the configured input in both late-look modes (below). |

Gameplay guard. `idEvent::ServiceEvents`, `idThread::Execute` and
`idEntity::RunPhysics` first call `idGameLocal::NotePresentationGameplay`.
Outside a prepared draw it returns at once. Inside one, between
`PreparePlayerSceneForRender` and `EndPresentationSceneForRender`, it counts the
call and prints the first eight by name. `idGameLocal::PlayEffect` first calls
`NotePresentationEffect`, which treats an effect started inside the renderer's
scene work (`idPresentationRendererScope` around the game's `RenderScene`
calls) as BSE's own. Such effects are particle impacts and nested segments,
started once per particle or authoritative effect step. They are counted apart.
Any other effect started in a draw is gameplay. The counts ride each
`g_presentationClockCapture` line as `gameplay=` and `bse=`, and
`presentation_look_runtime.py` fails any case that records gameplay. The guard
only reports and never changes what runs. The contract requires the call to come
first in each entry point, the renderer scopes around scene submission and
identical SP/MP bodies, and a mutation that removes a guard must fail.

HF-06 is qualified engine-side by the runtime cases in Evidence. Every case
passes the gameplay guard, including stereo draws (`vr`), a remote-camera
subview (`subview`), demo recording and playback (`demo`) and the `combined`
cross-system session. Viewing temporal AA in motion on the reference display,
and a physical headset run, belong to HF-08's by-eye review and the VR track.

## Supported frame-rate behaviour (HF-07)

Declared targets, pending physical qualification:

| Target | Frame interval | Budgets on the Windows x64 desktop reference (visible foreground window, `r_swapInterval 0`) |
| --- | --- | --- |
| 240 Hz, supported target | 4.17 ms | p95 ≤ 4.6 ms, p99 ≤ 6.3 ms frame interval; engine input-to-draw ≤ 1 ms with late look; no missed tick at p99 |
| 360 Hz, experimental | 2.78 ms | Reported only; not a release promise |

Cases to qualify: caps 30/60/120/144/165/240/360 and `0`, vsync and VRR where
available, low-FPS catch-up (fewer draws than ticks), menus and loading, hidden
windows, and the preset caps. The declared policy, pinned by
`presentation_hf_contract.py`, is as follows. Loading screens redraw at the
lower of `com_maxfps` and 60 Hz, never faster, and back off when a redraw is
slow. Uncapped, they redraw at most once per tick. Hidden windows are capped at
60 (the `hidden` runtime case: a hidden window capped at 240 presented 60.0 Hz,
p95 17.4 ms) and a headset paces itself. The presets set `minimum`/`lowpower` 30,
`performance` 60, `balanced` 120, `quality` 144 and `ultra` 240, and a detected
Steam Deck starts on `performance`. Use
[`presentation_look_runtime.py`](https://github.com/themuffinator/openQ4/blob/master/tools/tests/presentation_look_runtime.py)
for engine-side latency and smoothness, `com_showFramePacing 2` for p50/p95/p99
intervals and the gameplay benchmark for frame times, all in a visible
foreground window. Physical display latency (scan-out, compositor, panel)
needs external measurement and is not claimed by these tools.

Physical latency (HF-08). Set `com_latencyFlash 1` and point a click-to-photon
sensor (NVIDIA LDAT, OSLTT or a photodiode) at the left edge of the screen.
Each press of the attack button arms the flash as the event arrives. The first
frame drawn afterwards paints a white bar over the left tenth of the screen, and
the sensor reads the time from click to light. That frame samples input as late
as late look does, so the reading is the engine, swap chain, compositor and
panel latency a late-look view has. The flash does not depend on
`in_presentationView`. A late-look view reaches the screen on the flash's frame.
The classic view trails it by the engine-side delay measured above, about one
tick (16 ms). Measure full screen on the reference machine at the HF-07 caps,
with vsync off and on. Shots still land on the next tick, so muzzle-flash
latency is longer by up to a tick. `latencyFlashTest` (cheat) arms the flash without a device, and the
`flash` runtime case checks that each arming draws exactly one frame.

The cap matrix runs as one command:

```
presentation_look_runtime.py --basepath <Quake 4> --output-dir <new dir>
  --scenario pacing --fps 30 60 120 144 165 240 360 0 [--renderer vulkan]
  [--swap-interval 1] [--fullscreen] [--reference]
```

Each cap turns the view at 220°/s with late look on. Each case reports the
prepared-draw interval p50/p95/p99 at sub-millisecond resolution, the presented
rate, ticks no draw showed, whether the rate fell below 90% of the cap, the
late-look sample-to-draw time and the engine's `framePacingSnapshot` summary.
Every case also reports `prepareMs`, the cost of preparing each draw (camera,
late look, entity poses, client debris). The pacing case adds the renderer's
front-end, back-end and GPU summary, so a slow run shows where the frame went.
The 240 cap is judged against the budget above. Other caps are reported only.
`--fullscreen` uses the native desktop mode instead of a 960×540 window.
`--reference` is the operator's statement that the window was visible, in the
foreground, on the reference machine. Without it every verdict reads "not
qualifying", whatever the numbers. With it, a 240 case over budget fails the
run.

An occluded window on Windows does not qualify: on 23 October 2026 the same
client capped at 240 presented at 57 Hz on OpenGL (p99 232 ms) and 15 Hz on
Vulkan (p99 725 ms) behind other windows on a loaded machine. HF-07 remains open
until physical runs on the reference hardware meet the budgets above.

## Evidence

Native: `openq4-presentation-look` executes the production policy header in a
model of the usercmd pipeline at 30–997 Hz with jitter, buffered and direct
usercmds, steady and flick-speed turns through the wrap and both pitch clamps:
the drawn view equals the input sampled at the draw within 0.02°, interpolation
alone always trails by a tick, no draw turns further than the input did, and
held rates draw without a staircase. Static: `presentation_hf_contract.py`
(engine sampler, SP/MP owners, alternate views, AF history, mutations) and the
updated `presentation_interpolation_contract.py`.

Live single player, `game/airdefense1`, OpenGL, synthetic probe at 2000
counts/s (220°/s), 512 draws per case, `com_maxfps 240` on an occluded window
that presented at about 62 Hz (draw interval p50 16.2 ms, p95 25.6 ms). Retained
under `.tmp/hf/look-run3` and `.tmp/hf/scen-run1`:

| Measure | Late look off | Late look on |
| --- | --- | --- |
| Authoritative turn rate (expected −220.00°/s) | −220.00°/s | −219.95°/s |
| Drawn yaw against the sampled input | — | p95 0.003°, max 0.007° (command quantization) |
| Delay behind the input (line fitted at usercmd build, 3.9 ms build-to-draw bias removed) | about 16 ms | about 0 ms |
| Frame-to-frame residual of the drawn yaw, p95 | 2.15° | 0.09° |

Third person with `g_presentationInterpolation 1` recorded 512 interpolated
draws (residual p95 2.06°, the same as interpolated first person); with `0` the
scene correctly stayed on the authoritative tick and recorded none.

Owner activity, from the capture's per-draw counters (`.tmp/hf/scen-run4`), on
airdefense1's posed `env_ragdoll_marine` figures plus a spawned Strogg marine
killed by `damage`, the player's own death (`testDeath`, death camera) and a
`launching_barrel_small` thrown by `damage`:

| Scenario | AF poses requested on draws | Blended | Client debris drawn |
| --- | --- | --- | --- |
| Ragdoll | 2,764 | 2,747 | — |
| Player death | 2,589 | 2,571 | — |
| Exploding barrel | 1,869 | 1,860 | up to 2 at once |

The unblended remainder are draws at the end of a tick, which show the
authoritative pose. A first build blended nothing: the AF calls `ForceUpdate()`
every tick, clearing `lastTransformTime`, so the history now keeps the time of
the pose held in `joints[]` itself, and the contract rejects the old condition.
No run logged an error. These runs measure engine-side behaviour on one Windows
machine; they are not physical input or display qualification, nor a visual
review of the drawn motion, and the occluded window did not reach the requested
rate.

### Second round, 24 October 2026

The same harness ran on the guard build with OpenGL, 512 draws per case and a
240 cap unless stated. Output is retained under `.tmp/hf/r2-run1` and
`.tmp/hf/r2-pacing1`. After the BSE split below, the spectator, walker,
late-look and 240 pacing cases were run again (`.tmp/hf/r2-run2`). All seven
passed, with no gameplay and no BSE starts in any draw and no errors.

Gameplay inside draws. No SP case (late look, third person, death camera,
ragdoll, barrel debris, walker cockpit) and no MP case (listen host, remote
client) counted any. One spectator run counted three `PlayEffect` calls in the
draw where a bot was gibbed. The MP gib effect's splat particles carry an impact
effect, and BSE starts it when a particle hits geometry while the renderer
services effects inside `RenderScene`, once per particle. That is BSE's retail
design, not game work repeating per draw. The guard now counts effects started
inside the renderer's scene work apart (`bse=`); any other `PlayEffect` inside a
draw is still gameplay.

Late look in multiplayer (loopback, 127.0.0.1):

| Case | Late look | Turn rate (expected −220.00°/s) | Delay behind the input | Drawn vs sampled input, p95 | Residual p95 | Draw interval p50/p95 |
| --- | --- | --- | --- | --- | --- | --- |
| Listen host | off | −220.01°/s | about 16 ms | — | 0.26° | 4.3/5.6 ms |
| Listen host | on | −220.00°/s | about 0 ms | 0.0001° | 0.08° | 4.4/5.7 ms |
| Remote client | off | −220.00°/s | about 16 ms | — | 1.17° | 4.5/19.3 ms |
| Remote client | on | −220.01°/s | about 0 ms | 0.0001° | 0.08° | 4.6/18.0 ms |

Input reached the game exactly once through client prediction as well as on the
host. The client's slower p95 interval is two clients sharing one machine. Single
player on the same build gave −220.01/−220.00°/s and a residual p95 of 1.37°
without late look against 0.08° with it.

Spectating. A listen host with two bots listed up to 68 entities in the
presentation pass while playing, 71 while spectating free and 70 while
following a bot. That fits the bots joining the pass only while the host
spectates. In the free case,
a killed bot's ragdoll was blended on 183 of 184 AF draws, which happens only to
bodies on the presentation clock. Following a bot gave 512 interpolated draws of
its view, with a step deviation p95 of 0.04°, linear within each tick to
0.0001° in the rerun.

Walker cockpit. The `vehicle` case spawns a walker and enters it by script.
In a rerun (`.tmp/hf/r2-run2`, draws about every 5.5 ms, walker turning at
196°/s), all 512 cockpit draws were interpolated, with no errors and no gameplay
in a draw. Within each tick the drawn cockpit yaw is linear in the draw's phase
to 0.0001° (p95, `tickLinearityDeg`), so the joint-bound eye adds no
presentation error. The view's
step deviation (`drawnStepJitterDeg` p95 1.7°, against 0.5° for first person
without late look and 0.15° with it) comes entirely from per-tick changes in the
walker's simulated turn (p95 2.2° per tick). That is input reaching the view
once per tick plus the walker's own turning. Late look removes the input part
for first person and is excluded in vehicles.

Cap matrix (`--scenario pacing`, late look on). This is engine-side and not
qualifying: the run was not declared `--reference`.

| Cap | Presented | Draw interval p50/p95/p99 | Ticks no draw showed | Engine snapshot: bound, p95/p99 |
| --- | --- | --- | --- | --- |
| 30 | 30.0 Hz | 33.3/34.5/35.5 ms | 483 of 511 (expected below 60) | presentation cap, 34/35 ms |
| 60 | 60.0 Hz | 16.7/17.2/17.6 ms | 1 of 510 | simulation, 18/18 ms |
| 120 | 118.7 Hz | 8.4/10.7/11.1 ms | 0 | uncapped, 10/10 ms |
| 144 | 143.8 Hz | 7.0/9.6/13.3 ms | 0 | uncapped, 9/10 ms |
| 165 | 164.6 Hz | 6.1/8.8/10.9 ms | 0 | uncapped, 8/11 ms |
| 240 | 188.6 Hz, below the cap | 5.3/8.5/12.3 ms | 0 | uncapped, 8/11 ms |
| 360 | 178.1 Hz, below the cap | 5.6/8.0/10.2 ms, one 921 ms stall | 1 | uncapped, 11/13 ms |
| Uncapped | 186.8 Hz | 5.4/8.1/9.7 ms | 0 | uncapped, 8/10 ms |

At every cap the late-look view matched the input sampled for its draw (p95
0.0001°, 0.002° at 30). The 240 case misses the budget (p95 8.48 ms against
4.6, p99 12.27 ms against 6.3) because this machine did not reach the cap: the
engine classified those frames as not cap-bound. The rerun at 240 reached
204 Hz (p95 7.2 ms, p99 8.4 ms) and is still over budget. The 921 ms stall was a
single frame the engine classified as simulation-bound. Physical qualification
on the reference machine remains HF-07.

### Zoom, lifts and packet loss, 24 October 2026

Two cheat-gated development cvars drive these cases without device input.
`in_presentationViewProbe` feeds synthetic mouse counts through the ordinary
mouse path. `in_presentationViewProbeZoom` holds the zoom button in the usercmd,
exactly as a held key would. Output is retained under `.tmp/hf/r3-run1` and
`.tmp/hf/r3-run2`.

| Case | Late look | Turn rate (expected) | Delay behind the input | Drawn vs sampled input, p95 | Residual p95 |
| --- | --- | --- | --- | --- | --- |
| Railgun zoomed (`pm_zoomedSlow 50`) | off | −110.02°/s (−110.00) | about 15 ms | — | 0.46° |
| Railgun zoomed | on | −110.01°/s (−110.00) | about 0 ms | 0.0001° | 0.04° |
| Riding airdefense2's `func_elevator_3` up 193 units | off | −220.01°/s (−220.00) | about 16 ms | — | 0.22° |
| Riding the lift | on | −220.01°/s (−220.00) | about 0 ms | 0.0007° | 0.09° |
| Remote client, 5% of packets dropped at the client | off | −220.00°/s (−220.00) | about 16 ms | — | 1.07° |
| Remote client, 5% dropped | on | −220.00°/s (−220.00) | about 0 ms | 0.0001° | 0.08° |

Zoomed look follows the scaled sensitivity in both modes, so the late-look
sampler applies the same zoom scale as the usercmd. On the lift, the drawn eye
height was linear within each tick to 0.0004 units (`tickLinearityHeight`) while
the late-look correction turned it. Under loss, input still reached the game
exactly once and the drawn view still equalled the input sampled for the draw.
No case counted gameplay or BSE starts inside a draw, and none logged an error.

Delay, jitter and loss together. Once the network impairment rework landed
(the old latency queue dropped datagrams over 1400 bytes, so a delayed client
could not connect), `mploss` set `net_forceLatency 40`, `net_forceJitter 10`
and `net_forceDrop 5` on the client. That is at least 80 ms round trip, with
reordering, and `netImpairment` confirmed the conditions held after the join:
4–5% of datagrams dropped and about 50 reordered each run
(`.tmp/hf/r6-loss2`). Input was still consumed once (−219.99 and −220.00°/s).
With late look the drawn view matched the input sampled for each draw (p95
0.0001°), about 16 ms of delay disappeared, and the frame-to-frame residual fell
from 0.84° to 0.08°. Late look is a local view correction, so network delay
does not reach it; prediction recovery keeps its MP-07 behaviour.

Demo recording and playback (HF-06). The `demo` case records a render demo
(`recordDemo`) while late look turns the view and the capture runs, then plays
it back with `timeDemoQuit` in the same session. All 512 draws during recording
were interpolated with late look active. The view matched the input sampled for
each draw (p95 0.002°) and no draw counted gameplay. Playback then took the demo
bypass and rendered the recorded 569 frames in 2.9 s before the client exited
cleanly (`.tmp/hf/r3-run3`).

### Rapid turns, VR, subviews, Vulkan and vsync, 24 October 2026

Output is retained under `.tmp/hf/r4-run2`, `.tmp/hf/r4-vk` and
`.tmp/hf/r4-vsync`. No case counted gameplay or BSE starts inside a draw, and
none logged an error.

- **Rapid turns** (`flick`, 2200°/s). Input was consumed once in both modes
  (−2200.29 and −2200.06°/s against −2200.00). With late look the drawn view
  matched the input sampled for each draw (p95 0.0001°), the delay of about
  15 ms disappeared and the frame-to-frame residual fell from 4.67° to 0.09°.
- **VR** (`vr`). The in-tree OpenXR test runtime at 90 Hz received 2,340 stereo
  frames. All 512 captured draws were interpolated, late look stood down on
  every one, and no gameplay ran in either eye's draw.
- **Remote-camera subview** (`subview-monitor`, tram1's `bigScreen`). The
  renderer drew a remote-camera view in every metrics frame (18 views per frame,
  against 9 at the airdefense1 position) and nothing ran gameplay.
- **Where a 240-capped frame goes** (OpenGL, 196 Hz). Preparing each draw
  (camera, late look, entity poses, debris) took 0.35 ms at p50 and 0.52 ms at
  p95. The renderer reported about 1 ms front end, 1 ms back end and 2 ms GPU.
  Presentation work is about 7% of the frame and is not what keeps this machine
  under the cap.
- **Vsync** (`--swap-interval 1`). This machine's 2560×1440 panel runs at
  240 Hz through hybrid graphics. With vsync, the 240 cap presented at 138 Hz
  (p50 7.3 ms) and uncapped at 130 Hz, with no tick left undrawn: frames that
  miss a 4.17 ms refresh wait for the next.
- **Vulkan cap matrix**, on a window behind others. 60 held (59.9 Hz). 144
  presented 80 Hz (p95 54 ms), 240 presented 120 Hz (p95 24 ms, 29 ticks
  undrawn), and uncapped 164 Hz with one 754 ms stall. Draw preparation spiked
  to 52 ms, so the process was being descheduled. This matches the earlier
  finding that an occluded Vulkan window is throttled; it is not qualifying
  evidence either way.

Cross-system session (HF-06, `combined`, `.tmp/hf/r7-combined`). One session ran
`r_temporalAA 1`, `r_useShadowMap 1`, late look, the retained UI and audio
together while the probe turned the view. It captured 256 draws live, then the
same after `saveGame`/`loadGame` (a map reset), after `vid_restart` (the game
re-created its render targets) and after an `aviGame` capture had run the
fixed-tic AVI path for a second. All four captures were interpolated with late
look active on every draw, no gameplay ran inside any draw and nothing logged an
error. The inherited AVI path has its frame writes commented out, so this
exercises its clock, not its output. Screenshots taken mid-turn at 220°/s under
TAA and late look show no ghosting or double edges on the weapon or the
geometry.
