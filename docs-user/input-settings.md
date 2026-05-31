# Input Settings and Controls Guide

This guide covers the main ways to configure keyboard, mouse, and controller input in OpenQ4, including the in-game menu, console commands, default binds, and the files that store your changes.

## Quick Start

- Open the in-game menu and go to `Settings -> Game Options` for the built-in mouse and gameplay input toggles.
- Use the Controls screen for interactive rebinding, or use the console for exact `bind` commands.
- Press `` ` `` or `~` to open the console with the stock default bindings.
- Run `listBinds` to see your current bindings.
- Run `writeConfig my-input-backup.cfg` to save a manual backup before large binding changes.

## Where OpenQ4 Stores Input Settings

At startup, OpenQ4 loads input-related config in this order:

1. `default.cfg`
2. `openq4_defaults.cfg` (if present)
3. `openq4_profile_<profile>.cfg` such as `openq4_profile_steamdeck.cfg` when a platform profile is active
4. `OpenQ4Config.cfg`
5. `autoexec.cfg` (if present)

What this means in practice:

- Stock defaults come from `content/baseoq4/default.cfg`.
- OpenQ4-specific overrides can be layered on top through `openq4_defaults.cfg`.
- Platform-specific overrides can be applied through `com_platformProfile`.
- Your saved personal changes live in `OpenQ4Config.cfg`.
- Advanced users can place their own final overrides in `autoexec.cfg`.

`OpenQ4Config.cfg` is the main user config file name, and it is written under the normal writable save/config area (`fs_savepath`, which defaults to `fs_homepath`).

## In-Game Menu Settings

The built-in `Settings -> Game Options` screen exposes these input-related controls:

| Menu item | Backing cvar | Notes |
|---|---|---|
| Free Look | `in_freeLook` | Standard mouse-look behavior toggle. |
| Toggle Zoom | `in_toggleZoom` | Makes the zoom key/button act as a toggle instead of hold-to-zoom. |
| Invert Mouse | `m_pitch` | Switches between normal `0.022` and inverted `-0.022`. |
| Mouse Smooth | `m_smooth` | Blends mouse samples from `1` to `8`. |
| Mouse Sensitivity | `sensitivity` | Main mouse look sensitivity slider. |

These menu entries cover the most common mouse and zoom preferences, but OpenQ4 also exposes extra controller and input tuning cvars through the console.

## Stock Keyboard and Mouse Binds

The shipped defaults are defined in `content/baseoq4/default.cfg`.

### Core movement and combat

| Action | Default bind |
|---|---|
| Move forward / back | `W` / `S` |
| Move left / right | `A` / `D` |
| Jump | `Space` |
| Crouch | `C` |
| Attack | `Mouse1`, `LeftCtrl`, `RightCtrl` |
| Zoom | `Mouse2`, `Mouse3` |
| Reload | `R` |
| Flashlight | `F` |
| Weapon wheel | `E` |
| Next / previous weapon | `MWHEELDOWN` / `MWHEELUP`, `[` / `]` |

### Common UI and utility binds

| Action | Default bind |
|---|---|
| Console | `` ` `` and `~` |
| Menu | `Escape` |
| Objectives / scores | `Tab` |
| Chat | `T` |
| Team chat | `Y` |
| Quick save / quick load | `F5` / `F9` |
| Screenshot | `F12` |

## Stock Controller Layout

SDL gamepads use the default `JOY*` bindings from `default.cfg`.

| Input | Default action |
|---|---|
| `JOY1` | Weapon wheel |
| `JOY2` | Last weapon |
| `JOY3` | Jump |
| `JOY4` | Crouch |
| `JOY5` | Flashlight |
| `JOY6` | Reload |
| `JOY9` | Objectives / scores |
| `JOY10` | Center view |
| `JOY11` | Previous weapon |
| `JOY12` | Next weapon |
| `JOY13` | Run / walk |
| `JOY14` | Center view |
| `JOY15` | Attack |
| `JOY16` | Zoom |
| `JOY18` | Objectives / scores |

Additional controller behavior:

- `JOY7` and `JOY8` both open the in-game menu.
- Controller hotplug is supported, so you can connect or disconnect a pad while the game is running.
- Steam Deck packages enable the `steamdeck` platform profile, which applies `openq4_profile_steamdeck.cfg` on top of the normal defaults.

## Important Input Cvars

| Setting | Default | What it does |
|---|---:|---|
| `sensitivity` | `5` | Main mouse look sensitivity. |
| `m_pitch` | `0.022` | Vertical mouse scale. Use a negative value to invert look. |
| `m_yaw` | `0.022` | Horizontal mouse scale. |
| `m_smooth` | `1` | Mouse smoothing sample count (`1` to `8`). |
| `in_toggleRun` | `0` | Makes the run key toggle on/off instead of acting as a hold key in multiplayer. |
| `in_toggleCrouch` | `0` | Makes crouch toggle on/off instead of requiring hold. |
| `in_toggleZoom` | `0` | Makes zoom toggle on/off instead of requiring hold. |
| `in_joystick` | `1` | Enables controller input. |
| `in_joystickDeadZone` | `0.18` | Analog stick dead zone. |
| `in_joystickTriggerThreshold` | `0.35` | Trigger press threshold. |
| `in_joystickRumble` | `1` | Enables controller rumble. |
| `in_joystickRumbleScale` | `1.0` | Scales rumble strength from `0.0` to `2.0`. |
| `com_platformProfile` | `default` | Selects a platform profile such as `steamdeck`. |

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

## Example Tweaks

### Invert mouse

```cfg
seta m_pitch -0.022
```

### Higher mouse sensitivity with light smoothing

```cfg
seta sensitivity 6.50
seta m_smooth 2
```

### Toggle crouch and toggle zoom

```cfg
seta in_toggleCrouch 1
seta in_toggleZoom 1
```

### Controller tuning with a larger dead zone and weaker rumble

```cfg
seta in_joystick 1
seta in_joystickDeadZone 0.24
seta in_joystickTriggerThreshold 0.40
seta in_joystickRumbleScale 0.50
```

### Steam Deck-style profile selection

```cfg
seta com_platformProfile steamdeck
```

## Troubleshooting

- If the console does not open, verify you are using the stock console binds (`` ` `` / `~`) or rebind a key through the Controls screen.
- If a controller does not respond, check `in_joystick 1`, then reconnect the pad or restart the game after changing profile-level settings.
- If analog sticks feel too twitchy or drift when centered, raise `in_joystickDeadZone` slightly.
- If triggers fire too early or too late, adjust `in_joystickTriggerThreshold`.
- If a binding change did not stick, run `writeConfig my-input-backup.cfg` to confirm your commands are valid, then quit normally so `OpenQ4Config.cfg` is rewritten.
- If you want a final personal override layer that survives menu changes, place your preferred `seta` and `bind` commands in `autoexec.cfg`.
