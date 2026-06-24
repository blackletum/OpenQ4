# Display Settings and Multi-Screen Guide

This guide covers openQ4 display/window settings for end users, including multi-monitor behavior and modern fullscreen/window handling.

## Quick Start

- Press `Alt+Enter` to toggle fullscreen/windowed mode (fast path uses `vid_restart partial`).
- Run `listDisplays` in the console to list monitor indices for `r_screen`.
- On SDL3 builds, run `listDisplayModes [displayIndex]` to list available exclusive fullscreen modes, including SDL-reported content scale, pixel density, and exact refresh details when available.
- The in-game `Settings -> System` menu exposes display resolution, fullscreen policy, borderless/window sizing, custom exclusive fullscreen sizing, refresh rate, UI aspect behavior, display target, multi-screen, and resolution scale controls.
- `Settings -> System` also exposes a **Performance Preset** dropdown plus an **Auto-Detect** button above the display-resolution controls.
- After changing video cvars, run `vid_restart` (or `vid_restart partial` for quick window/fullscreen transitions).

## Performance Presets

The `com_performancePreset` cvar stores the selected preset. Use the Settings menu dropdown, or run `applyPerformancePreset <name>` from the console.

| Preset | Intended use |
|---|---|
| `minimum` | Most constrained systems; uses 50% resolution scale, no AA, texture downsizing, and conservative audio mixing. |
| `lowpower` | Raspberry Pi-class and other low-power systems; keeps authored rendering features on while reducing resolution scale, AA, post effects, texture pressure, and surround/EAX audio cost. |
| `performance` | Modest desktops/handhelds aiming for smoother frame pacing, with lighter AA and audio-effect targets. |
| `balanced` | General desktop default. |
| `quality` | Strong desktop GPUs. |
| `ultra` | Explicit high-end choice; Auto-Detect does not select this automatically. |

`autoDetectPerformancePreset` selects a conservative preset from platform signals, CPU architecture, system RAM, video RAM, and renderer capability flags, then applies it. On Raspberry Pi hosts or explicit `OPENQ4_LOWPOWER=1` / `OPENQ4_RASPBERRYPI=1` signals, it chooses `lowpower`.

For package or platform validation, `performancePresetSelfTest` checks that the preset commands are registered, every preset is known to the menu-facing cvar, auto-detect returns a supported non-`ultra` preset, and the preset cvar mappings apply correctly.

## Core Display Settings

| Setting | Default | What it does |
|---|---:|---|
| `r_fullscreen` | `1` | `1` = fullscreen, `0` = windowed. |
| `r_fullscreenDesktop` | `1` | `1` = native desktop fullscreen (recommended). `0` = exclusive fullscreen using `r_mode`/`r_custom*`. |
| `r_borderless` | `1` | Borderless window mode when `r_fullscreen 0`. |
| `r_windowWidth` | `1280` | Windowed width. |
| `r_windowHeight` | `720` | Windowed height. |
| `win_xpos` | (auto) | Window X position (updated automatically when you move the window). |
| `win_ypos` | (auto) | Window Y position (updated automatically when you move the window). |
| `r_mode` | `-2` | Fullscreen sizing selector (`-2` = desktop native/current display, `-1` = custom, `0+` = legacy preset index). |
| `r_customWidth` | `1920` | Custom exclusive-fullscreen width used when `r_mode -1`. |
| `r_customHeight` | `1080` | Custom exclusive-fullscreen height used when `r_mode -1`. |
| `r_displayRefresh` | `0` | Requested fullscreen refresh rate (0 = default/driver choice). |
| `r_screen` | `-1` | SDL3 monitor target (`-1` auto/current, `0..N` explicit index). |

## Frame Cap

| Setting | Default | What it does |
|---|---:|---|
| `com_maxfps` | `240` | Presentation frame cap (`0` = uncapped). |
| `com_steamDeckAutoFrameCap` | `1` | When the Steam Deck profile is active, applies a Deck-friendly cap only while `com_maxfps` is still `240`. |
| `com_steamDeckFrameCap` | `0` | Steam Deck default cap override (`0` = use detected display refresh clamped to the Deck-oriented range). |

The Steam Deck auto cap preserves custom `com_maxfps` values. Set `com_steamDeckAutoFrameCap 0` to opt out, or set `com_maxfps` directly for a fixed cap.

## Anti-Aliasing Settings (New)

| Setting | Default | What it does |
|---|---:|---|
| `r_multiSamples` | `0` | MSAA sample count for the main scene render target (`0`, `2`, `4`, `8`, `16`; `0` = off). |
| `r_postAA` | `0` | Post AA mode (`0` = off, `1` = SMAA medium, `2` = SMAA high, `3` = SMAA ultra, `4` = color-edge prototype). |
| `r_msaaAlphaToCoverage` | `1` | Enables alpha-to-coverage for perforated/alpha-tested materials when MSAA is active. Helps foliage/fences look cleaner. |
| `r_msaaResolveDepth` | `0` | Also resolves depth during MSAA resolve. Usually leave this off unless debugging a depth-dependent edge case. |

`r_multiSamples` value guide:
- `0`: disabled (fastest, most aliasing).
- `2`: low-cost MSAA uplift for modest GPUs.
- `4`: recommended default quality/performance balance.
- `8`: high quality, noticeably higher GPU cost.
- `16`: enthusiast/high-end setting where supported.
- `1` usually provides no meaningful benefit and is not recommended.

`r_postAA` value guide:
- `0`: disabled.
- `1`: SMAA medium; luma-edge detection with a `0.10` threshold and 8-step search, recommended as the post-AA default.
- `2`: SMAA high; luma-edge detection with the same `0.10` threshold and a 16-step search.
- `3`: SMAA ultra; luma-edge detection with a lower `0.05` threshold and a 32-step search.
- `4`: color-edge prototype; color-edge detection with a `0.10` threshold and a 16-step search for comparison captures.

Notes:
- `r_multiSamples` is hardware-limited and may be clamped by the driver/GPU.
- Unsupported `r_multiSamples` values are normalized to the supported ladder before video startup (`1` becomes off; odd/intermediate values step up to `2`, `4`, `8`, or `16`).
- On SDL3 builds, video startup retries lower MSAA requests if the window or GL context rejects the requested sample count (`16 -> 8 -> 4 -> 2 -> off`) and logs the requested, selected, and driver-reported multisample attributes.
- `gfxInfo` reports the active AA summary, including requested/effective MSAA, `GL_MAX_SAMPLES`, alpha-to-coverage, post AA mode, screen fraction, and supersampling state.
- The Post AA startup/runtime log records the active SMAA edge mode, threshold, search steps, and local contrast scale so quality captures can be compared without guessing which shader contract was active.
- Changing `r_multiSamples` should be followed by `vid_restart`.
- `r_postAA`, `r_msaaAlphaToCoverage`, and `r_msaaResolveDepth` can be changed at runtime, but a `vid_restart` is still safe if behavior looks stale.

## Resolution Scale

| Setting | Default | What it does |
|---|---:|---|
| `r_screenFraction` | `100` | Main-scene resolution scale percentage (`10..200`). Values below `100` reduce scene resolution for performance; values above `100` supersample the scene before resolving it back to the native back buffer. |

The Display menu exposes curated presets: `10%`, `25%`, `50%`, `75%`, `85%`, `100%`, `125%`, `150%`, and `200%`.

## Fullscreen Policy (Desktop vs Exclusive)

- Default behavior is **desktop-native fullscreen** (`r_fullscreenDesktop 1`): fullscreen matches your current desktop resolution and does not change Windows display mode.
- For **exclusive fullscreen** (explicit mode switch), set `r_fullscreenDesktop 0`. In this mode, `r_mode`/`r_customWidth`/`r_customHeight` control the requested fullscreen resolution.
- In `Settings -> System`, **Display Resolution** lists Desktop Native first, then only the fullscreen preset resolutions SDL3 reports for the selected display, followed by Custom. Preset entries include compact aspect labels such as `16:9` or `21:9`. Choosing an unusual reported mode writes exact custom width/height when no legacy `r_mode` index exists.
- The console `listModes` command shows the expanded legacy `r_mode` preset catalog for configs and command-line use, covering common desktop, laptop, ultrawide, HiDPI, 4K, 5K, 6K, and 8K resolutions. The Settings dropdown still hides static presets that the selected display does not report.
- **Refresh Rate** lists Auto plus SDL3-reported refresh rates for the currently selected fullscreen resolution. Leave it on Auto unless you specifically need an exclusive-mode refresh request.
- On Windows, fullscreen windows minimize on focus loss so system UI such as Alt+Tab and the Snipping Tool overlay can take foreground cleanly.
- On Windows, `PrintScreen` yields to the system snipping UI by default (`win_printScreenToSystemTool 1`). Use `F12` for the built-in openQ4 screenshot command, or set that cvar to `0` if you explicitly want `PrintScreen` available for in-engine binds again.

Notes:
- When `r_fullscreenDesktop 1`, `r_mode` and `r_custom*` are ignored for fullscreen sizing (they still exist for legacy configs and exclusive mode). Use `r_screenFraction` for below-native scaling or supersampling while staying in desktop-native fullscreen.
- Use `listDisplayModes` to see what your monitor actually supports in exclusive mode. On SDL3/Wayland, display diagnostics also report scale, orientation, pixel density, and exact refresh details that help diagnose compositor scaling behavior.

## Windowed Sizing and Placement

- `Settings -> System -> Display Sizing` exposes `r_windowWidth`, `r_windowHeight`, `r_customWidth`, `r_customHeight`, and `r_displayRefresh`. Leaving Refresh Rate on `Auto` writes `r_displayRefresh 0`.
- New Windows installs, and legacy Windows configs migrated from the old default, use borderless windowed presentation when `r_fullscreen 0` to avoid OpenGL bordered-window frame pacing stalls. Set `r_borderless 0` and run `vid_restart` if you specifically want a resizable bordered window.
- When bordered windowed mode is active (`r_fullscreen 0`, `r_borderless 0`), resizing updates `r_windowWidth`/`r_windowHeight` automatically.
- Moving the window updates `win_xpos`/`win_ypos` automatically.
- When switching fullscreen -> windowed, openQ4 restores the last remembered windowed size/position (it should not come back as a fullscreen-sized window).
- If you unplug/rearrange monitors and the saved window position becomes off-screen, openQ4 will recover by clamping/recentering the window back onto a valid display.
- If you set `r_screen` to an explicit display index (`0..N`), window placement is constrained to that display's usable area. With `r_screen -1`, placement is respected unless it becomes invalid/off-screen.
- SDL3 tip: hold `Shift` while resizing to snap the window aspect ratio to common targets (4:3, 16:9, 16:10, 21:9, etc.).

## Aspect Ratio and FOV

- `r_aspectRatio` is **deprecated/ignored**. Aspect ratio and FOV behavior are derived automatically from the current render size, so the game follows any aspect ratio without manual selection.
- The Display menu no longer exposes a manual Aspect Ratio selector; Display Resolution and the live window size drive aspect behavior automatically.
- Weapon gameplay zoom uses the same gameplay FOV conversion path as normal view FOV, so authored weapon zoom values keep consistent framing/magnification across aspect ratios.
- In multiplayer, zoomed first-person view suppresses view bob while scoped so reticle tracking stays stable during movement.
- Scope GUI yaw tracking for zoom overlays follows the weapon/player view axis path, improving scope alignment while turning.

## View Weapon FOV and Placement (New)

These settings control first-person viewmodel rendering (the weapon on screen). They are client-side tuning controls and are not gameplay/network authority cvars.

The in-game menu exposes these under `Settings -> Game Options -> View Weapon`.

| Setting | Default | What it does |
|---|---:|---|
| `cl_gunfov` | `0` | View-weapon FOV override (`0` = follow current view FOV). |
| `cl_gunfov_adjust` | `1` | Aspect policy for `cl_gunfov`: `1` keeps classic 4:3-style weapon framing across screen ratios, `0` uses direct viewport-based FOV conversion. |
| `cl_gun_x` | `0` | Client weapon X/right offset. |
| `cl_gun_y` | `0` | Client weapon Y/forward offset. |
| `cl_gun_z` | `0` | Client weapon Z/up offset. |

Notes:
- `cl_gunfov` values above `0` are clamped to a safe range internally for weapon projection.
- Weapon projection is handled in renderer weapon-depth path, so narrow/wide aspect changes are handled consistently.
- `cl_gun_x/y/z` are additive with legacy `g_gunX/Y/Z` offsets. Prefer `cl_gun_*` for user config.
- openQ4's legacy baseline keeps `g_gunX` at `1` and `g_gunZ` at `-1` so the default widescreen viewmodel framing stays out of the viewport edge.

## UI Aspect Correction (New)

This controls 2D UI layout behavior (menu, HUD, console, loading/initializing screens):

The in-game menu exposes this as `Settings -> System -> Display Sizing -> UI Aspect`.

| Setting | Default | What it does |
|---|---:|---|
| `ui_aspectCorrection` | `1` | `1` keeps classic 4:3-style correction for all 2D UI. `0` stretches 2D UI to the full 2D draw region. |

## Multi-Monitor Behavior (New)

When the render surface spans multiple monitors:

- 2D elements (console, HUD, menus, loading/initializing UI) are constrained to the selected display region. With `r_screen -1`, this defaults to the primary display.
- 2D aspect behavior inside that region is controlled by `ui_aspectCorrection`.
- Menu cursor mapping follows the same 2D region so mouse interaction stays aligned.

3D world rendering is unchanged by these UI cvars.

## Useful Console Examples

### Recommended Modern Defaults

```cfg
seta r_screen -1
seta r_fullscreenDesktop 1
seta r_fullscreen 1
seta r_multiSamples 4
seta r_postAA 1
seta r_msaaAlphaToCoverage 1
seta ui_aspectCorrection 1
vid_restart
```

### Borderless Window on a Specific Monitor

```cfg
seta r_fullscreen 0
seta r_borderless 1
seta r_screen 1
vid_restart
```

### Custom Fullscreen Resolution

```cfg
seta r_fullscreen 1
seta r_fullscreenDesktop 0
seta r_mode -1
seta r_customWidth 2560
seta r_customHeight 1440
vid_restart
```

### Stretch Menu + HUD (No 4:3 Correction)

```cfg
seta ui_aspectCorrection 0
```

### View Weapon: Classic Framing + Slight Lowering

```cfg
seta cl_gunfov 90
seta cl_gunfov_adjust 1
seta cl_gun_z -1.15
```

### View Weapon: Match World FOV, Personal Position Offset

```cfg
seta cl_gunfov 0
seta cl_gun_x 0.5
seta cl_gun_y -0.5
seta cl_gun_z -0.5
```

### Performance-Focused AA Preset

```cfg
seta r_multiSamples 2
seta r_postAA 1
seta r_msaaAlphaToCoverage 1
vid_restart
```

### Maximum Clarity (Higher GPU Cost)

```cfg
seta r_multiSamples 8
seta r_postAA 1
seta r_msaaAlphaToCoverage 1
vid_restart
```

## Troubleshooting

- If a display change does not apply, run `vid_restart`.
- If monitor targeting looks wrong, run `listDisplays`, then set `r_screen` to the correct index and restart video.
- If UI appears too centered/boxed on wide displays, set `ui_aspectCorrection 0`.
- If the window opens off-screen after a monitor change, set `r_screen` explicitly to the target monitor and restart video; openQ4 will also attempt to recover automatically.
- If AA settings seem unchanged, check values with `r_multiSamples`, `r_postAA`, and `r_msaaAlphaToCoverage`, then run `vid_restart`.
- If enabling `r_postAA 1` turns the 3D viewport black on an older build, set `r_postAA 0`, run `vid_restart`, and attach `openq4.log` plus the output of `gfxInfo`. Current builds use a three-pass GLSL SMAA path and should no longer hit the old feedback-loop failure. RenderDoc capture is not yet supported on the current openQ4 renderer.
