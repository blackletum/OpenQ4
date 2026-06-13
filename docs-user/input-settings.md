# Input Settings and Controls Guide

This guide covers the main ways to configure keyboard, mouse, and controller input in openQ4, including the in-game menu, console commands, default binds, config files, and common troubleshooting.

## Quick Start

- To rebind actions, open `Settings -> Controls`.
- To tune mouse and controller feel, open `Settings -> Game Options`.
- Start with sensitivity and dead zone before changing advanced values.
- Press `` ` `` or `~` to open the console with the stock default bindings.
- Run `listBinds` to see your current bindings.
- Run `writeConfig my-input-backup.cfg` to save a manual backup before large binding changes.
- Use `Load Defaults` if the controls get into a bad state. This also resets custom binds.

## Where Things Are

| Menu | Use it for |
|---|---|
| `Settings -> Controls -> Movement` | Forward, back, strafe, jump, crouch, turn, run/walk. |
| `Settings -> Controls -> Weapons` | Direct weapon slots and buy menu. |
| `Settings -> Controls -> Attack` | Attack, reload, zoom, next/previous weapon, flashlight, objectives, weapon wheel. |
| `Settings -> Controls -> Other` | Quick save/load, chat, vote, ready, stats, emotes, voice chat. |
| `Settings -> Game Options` | Free look, toggle zoom, mouse input tuning, controller tuning, console access. |

To rebind an action, select its row and press the key, mouse button, or controller button you want to use. Analog sticks are configured through the controller settings instead of being rebound as buttons.

## Where openQ4 Stores Input Settings

At startup, openQ4 loads input-related config in this order:

1. `default.cfg`
2. `openq4_defaults.cfg` (if present)
3. `openq4_profile_<profile>.cfg`, such as `openq4_profile_steamdeck.cfg` when a platform profile is active
4. `openQ4Config.cfg`
5. `autoexec.cfg` (if present)

What this means in practice:

- Stock defaults come from `content/baseoq4/default.cfg`.
- openQ4-specific overrides can be layered on top through `openq4_defaults.cfg`.
- Platform-specific overrides can be applied through `com_platformProfile`.
- Your saved personal changes live in `openQ4Config.cfg`.
- Advanced users can place their own final overrides in `autoexec.cfg`.

`openQ4Config.cfg` is the main user config file name, and it is written under the normal writable save/config area (`fs_savepath`, which defaults to `fs_homepath`).

## Mouse Settings

Mouse tuning lives in `Settings -> Game Options`.

| Setting | Backing cvar | Good starting point | What it changes |
|---|---|---:|---|
| Free Look | `in_freeLook` | On | Lets the mouse control view direction by default. |
| Toggle Zoom | `in_toggleZoom` | Off | Makes zoom act as a toggle instead of hold-to-zoom. |
| Mouse Pitch | `m_pitch` | Normal | Flips vertical mouse look. |
| Mouse Smooth | `m_smooth` | `1` | Higher values smooth motion but can feel less direct. |
| Mouse Sensitivity | `sensitivity` | Personal preference | Main mouse look speed. With Mouse CPI enabled, this is degrees per centimeter. |
| Mouse CPI | `m_cpi` | `0` | Set this to your mouse CPI/DPI for physical sensitivity. `0` keeps classic raw-count behavior. |
| View Filter | `m_filter` | `0` | Optional QuakeLive-style view-angle history filtering. Leave it off for the most direct raw input. |
| Mouse Accel | `cl_mouseAccel` | `0` | QuakeLive-style acceleration. Positive values add sensitivity as movement rate rises; negative values subtract it. |
| Accel Offset | `cl_mouseAccelOffset` | `0` | Movement-rate threshold before acceleration starts. |
| Accel Curve | `cl_mouseAccelPower` | `2` | Acceleration curve shape. `2` matches QuakeLive's default power. |
| Sensitivity Cap | `cl_mouseSensCap` | `0` | Caps accelerated sensitivity when set above `0`. |
| Console Access | `con_allowConsole` | Tilde | Controls whether tilde opens the console directly or requires Ctrl+Alt+Tilde. |

Useful console-only fallback:

```cfg
seta m_maxMouseDelta 0
```

`m_maxMouseDelta 0` keeps the modern high-DPI path unclamped. Set a positive value only if a faulty mouse or driver creates large camera jumps.

Debug-only console fallback:

```cfg
seta cl_mouseAccelDebug 0
```

`cl_mouseAccelDebug 1` writes acceleration samples to `logs/mouse.log` under the active save path.

For CPI-normalized tuning, set `m_cpi` to the mouse hardware value and use `360 / sensitivity` as an approximate centimeters-per-360 target.

SDL3 builds use relative mouse capture during gameplay on Windows, Linux, and macOS. On Linux, openQ4 requests unscaled relative deltas from SDL where the desktop stack supports them and preserves fractional movement before it reaches the integer engine event queue. `Mouse1` through `Mouse8` are bindable by name on SDL3 and the native X11 fallback, and wheel up/down remain available as `MWHEELUP` and `MWHEELDOWN`.

Native macOS builds also expose `Mouse1` through `Mouse8`, track high-resolution wheel/trackpad deltas before emitting wheel steps, and synthesize common edit-field control characters such as Return, Tab, Backspace, and Ctrl-letter shortcuts.

## Controller Settings

Controller tuning lives in `Settings -> Game Options -> Controller`.

| Setting | Backing cvar | Default | What it changes |
|---|---|---:|---|
| Controller | `in_joystick` | On | Enables SDL3 gamepad/joystick input and hotplug support. |
| Stick Layout | `in_joystickSouthpaw` | Default | Use `Southpaw` to swap movement and look sticks. |
| Stick Dead Zone | `in_joystickDeadZone` | `0.18` | Raises or lowers the center area ignored by analog sticks. |
| Look Sensitivity | `in_joystickLookSensitivity` | `0.75` | Scales controller turn speed after stick deflection is normalized. |
| Look Curve | `in_joystickLookCurve` | `1.35` | Higher values damp small aim movements; full-stick turn speed is controlled by Look Sensitivity. |
| Invert Look | `in_joystickInvertLook` | Off | Flips vertical controller look. |
| Trigger Press | `in_joystickTriggerThreshold` | `0.35` | Controls how far LT/RT must be pressed before they count as buttons. |
| Rumble | `in_joystickRumble` | On | Enables gameplay rumble when the device supports it. |
| Rumble Strength | `in_joystickRumbleScale` | `1.0` | Scales rumble intensity from `0.0` to `2.0`. |

Suggested adjustments:

- If the view or movement drifts, raise `Stick Dead Zone` a little.
- If aiming feels sluggish, raise `Look Sensitivity`.
- If small aim corrections are too twitchy, raise `Look Curve` or `Stick Dead Zone`.
- If you cannot reach full turn speed comfortably, lower `Look Curve` toward `1.0`.
- If triggers fire too easily, raise `Trigger Press`.
- If triggers feel unresponsive, lower `Trigger Press`.

Advanced movement tuning is available from the console:

```cfg
seta in_joystickMoveCurve 1.0
```

`1.0` is linear movement. Higher values make walking easier near the center of the stick but require more stick travel to reach full movement.

SDL gamepads use SDL's standard controller database, so Xbox, PlayStation, Steam Input, and similar pads should not need axis remapping. Generic SDL joysticks use raw axes with these auto defaults:

| Cvar | Auto default | What it changes |
|---|---:|---|
| `in_joystickUseDedicatedLookAxes` | `-1` | `-1` uses look axes only when a paired look stick is available, `0` keeps classic single-stick behavior, and `1` forces dedicated look axes when any mapped look axis exists. |
| `in_joystickMoveAxisX` / `in_joystickMoveAxisY` | `0` / `1` | Raw joystick axes used for movement or classic stick look. |
| `in_joystickLookAxisX` / `in_joystickLookAxisY` | `2` / `3` | Raw joystick axes used for dedicated look on four-axis devices. |
| `in_joystickUpAxis` / `in_joystickUpAxisNegative` | `4` / `5` | Optional vertical/throttle axes; when both exist, openQ4 uses positive minus negative. |

Set an axis cvar to `-1` for auto, or to a raw SDL axis number from `0` through `31`. These advanced joystick cvars only affect generic SDL joysticks; SDL gamepads keep the stable `JOY` button and stick layout described below.

## Stock Keyboard and Mouse Binds

The shipped defaults are defined in `content/baseoq4/default.cfg`.

### Core Movement and Combat

| Action | Default bind |
|---|---|
| Move forward / back | `W` / `S`, `Up Arrow` / `Down Arrow` |
| Move left / right | `A` / `D` |
| Turn left / right | `Left Arrow` / `Right Arrow` |
| Jump | `Space` |
| Crouch | `C` |
| Attack | `Mouse1`, `LeftCtrl`, `RightCtrl` |
| Zoom | `Mouse2`, `Mouse3` |
| Reload | `R` |
| Flashlight | `F` |
| Weapon wheel | `E` |
| Next / previous weapon | `MWHEELDOWN` / `MWHEELUP`, `[` / `]` |

### Common UI and Utility Binds

| Action | Default bind |
|---|---|
| Console | `` ` `` and `~` |
| Menu | `Escape` |
| Objectives / scores | `Tab` |
| Chat | `T` |
| Team chat | `Y` |
| Quick save / quick load | `F5` / `F9` |
| Screenshot | `F12` |

## Default Controller Buttons

SDL gamepads use stable `JOY` button names so binds work across common Xbox, PlayStation, and Steam Input layouts. Controller support is first-class in SDL3 builds on Windows, Linux, and macOS. The native Linux, native macOS, and legacy Win32 input fallbacks currently remain keyboard/mouse focused.

| Button | Default action |
|---|---|
| `JOY1` / LB | Weapon wheel |
| `JOY2` / RB | Last weapon |
| `JOY3` / A or Cross | Jump |
| `JOY4` / B or Circle | Crouch |
| `JOY5` / Y or Triangle | Flashlight |
| `JOY6` / X or Square | Reload |
| `JOY7` / Start | Menu |
| `JOY8` / Back or Select | Menu |
| `JOY9` / D-pad Up | Objectives or scores |
| `JOY10` / D-pad Down | Center view |
| `JOY11` / D-pad Right | Previous weapon |
| `JOY12` / D-pad Left | Next weapon |
| `JOY13` / Left stick click | Run or walk modifier |
| `JOY14` / Right stick click | Center view |
| `JOY15` / RT | Attack |
| `JOY16` / LT | Zoom |
| `JOY17` / Guide | Menu, when delivered by the OS |
| `JOY18` / Touchpad | Objectives or scores, when available |
| `JOY19` / Right paddle 1 | Jump |
| `JOY20` / Left paddle 1 | Crouch |
| `JOY21` / Right paddle 2 | Reload |
| `JOY22` / Left paddle 2 | Weapon wheel |

Menu behavior:

- `JOY7` and `JOY8` open the in-game menu.
- `JOY3` activates the focused menu item.
- `JOY4`, `JOY7`, and `JOY8` back out of menus.
- The D-pad and movement stick move menu focus.
- Holding the D-pad, movement stick, or shoulder buttons repeats navigation for long lists.
- Steam Deck packages enable the `steamdeck` platform profile, which applies `openq4_profile_steamdeck.cfg` on top of the normal defaults.
- `JOY23` through `JOY28`, `JOY29` through `JOY32`, and generic `AUX1` through `AUX16` are bindable for extra device buttons but are intentionally unbound by default.

## Useful Console Commands

| Command | What it does |
|---|---|
| `bind <key> "<command>"` | Assigns a command to a key or button. |
| `unbind <key>` | Removes a bind from one key or button. |
| `unbindall` | Clears all binds. Use with care. |
| `listBinds` | Prints the current binds. |
| `writeConfig <filename>` | Writes a config snapshot to a `.cfg` file. |

Examples:

```cfg
bind mouse2 "_zoom"
bind JOY4 "_movedown"
bind q "_weaponWheel"
unbind JOY18
listBinds
writeConfig my-input-backup.cfg
```

Most input cvars are archived, so changes made through the menu or with `seta` persist after exit.

## Example Tweaks

### CPI-Normalized Mouse With Acceleration Disabled

```cfg
seta sensitivity 3.5
seta m_smooth 1
seta m_cpi 0
seta m_filter 0
seta cl_mouseAccel 0
seta cl_mouseAccelOffset 0
seta cl_mouseAccelPower 2
seta cl_mouseSensCap 0
```

### Invert Mouse

```cfg
seta m_pitch -0.022
```

### Higher Mouse Sensitivity With Light Smoothing

```cfg
seta sensitivity 6.50
seta m_smooth 2
```

### Toggle Crouch and Toggle Zoom

```cfg
seta in_toggleCrouch 1
seta in_toggleZoom 1
```

### Controller Tuning With a Larger Dead Zone and Weaker Rumble

```cfg
seta in_joystick 1
seta in_joystickDeadZone 0.24
seta in_joystickLookSensitivity 0.75
seta in_joystickLookCurve 1.35
seta in_joystickTriggerThreshold 0.40
seta in_joystickRumbleScale 0.50
```

### Steam Deck-Style Profile Selection

```cfg
seta com_platformProfile steamdeck
```

## Troubleshooting

| Problem | Try this |
|---|---|
| Console does not open | Verify you are using the stock console binds (`` ` `` / `~`) or rebind a key through the Controls screen. |
| Controller is not detected | Make sure `Controller` is enabled, reconnect the device, and check whether Steam Input is remapping it. |
| Movement or aim drifts | Raise `Stick Dead Zone` until the drift stops. |
| Aim feels too fast | Lower `Look Sensitivity`; if tiny stick movements are the problem, raise `Look Curve`. |
| Aim feels too slow | Raise `Look Sensitivity`. |
| Aim feels too twitchy near center | Raise `Look Curve` or `Stick Dead Zone`. |
| Triggers activate by accident | Raise `Trigger Press`. |
| Triggers feel unresponsive | Lower `Trigger Press`. |
| Rumble does not work | Enable `Rumble`, set `Rumble Strength` above `0`, confirm the device supports haptics, and reset `s_controllerRumble` to `1` if it was changed from the console. |
| Mouse look is backwards vertically | Change `Mouse Pitch` or `Invert Look`, depending on the device. |
| Binding change did not stick | Run `writeConfig my-input-backup.cfg` to confirm your commands are valid, then quit normally so `openQ4Config.cfg` is rewritten. |

If you want a final personal override layer that survives menu changes, place your preferred `seta` and `bind` commands in `autoexec.cfg`.

For Steam Deck launcher details and Deck-specific notes, see [steam-deck.md](steam-deck.md).
