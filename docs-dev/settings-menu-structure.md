# Settings Menu Structure

This document describes the current OpenQ4 Settings menu as implemented by the main menu GUI scripts. It records the visible menu organization, widget types, cvar or GUI-state targets, discrete values, and slider ranges.

Source files:

- `content/baseoq4/guis/mainmenu.gui`
- `content/baseoq4/guis/menu/settings/game.gui`
- `content/baseoq4/guis/menu/settings/game_hovers.gui`
- `src/framework/Session_menu.cpp`

## Widget Type Key

| Type | Meaning |
|---|---|
| `windowDef` | Static label, background, container, preview, action hotspot, or hover surface depending on event handlers. |
| `choiceDef` | Discrete picker. Uses explicit `values` when present. For common boolean `choiceType 0` rows, `#str_200059` maps to `No;Yes` and normally means `0;1`. |
| `sliderDef` | Numeric slider. The GUI range is defined by `low`, `high`, and `step`. |
| `editDef` | Text/numeric value field, usually paired with a slider that writes the same cvar. |
| `bindDef` | Key/button binding capture widget. Bind widgets have no numeric range. |

Several rows use `gui` instead of `cvar`. These write a GUI state variable first, then apply one or more console commands from GUI event handlers.

## Top-Level Settings Shell

The Settings panel is `p_settings` in `mainmenu.gui`. It owns the left navigation, the active content pane, popups, and shared named events.

| Entry | Widget | Opens or performs | Notes |
|---|---|---|---|
| Controls | `windowDef set_b_controls` | `p_settings_ctrls` | Defaults to the Movement binding category. |
| Game Options | `windowDef set_b_gameoptions` | `p_settings_game` | Includes gameplay, mouse, controller, crosshair, corpse, language, and console options. |
| System | `windowDef set_b_system` | `p_settings_sys` | Video, display, quality, advanced rendering, and post effects. |
| Audio | `windowDef set_b_audio` | `p_settings_audio` | Output backend/device plus volume controls. |
| Load Defaults | `windowDef set_b_loaddefaults` | Reset-confirm popup | Sets `desktop::active` and opens `anim_pop_defaultsIn`. |
| Video Restart | `windowDef set_b_vidrestart` | `vid_restart` | The active button block is currently commented out. Some warning state and label assets still exist. |
| Back | `windowDef set_b_back` | Exits Settings | If `desktop::vidwarn` is set, Back routes through the video warning flow. |

Scroll controls:

| Pane | Scroll widget | Cvar | Range | Step | Notes |
|---|---|---|---|---|---|
| Game Options | `sliderDef set_game_scroll_thumb` | `gui_set_game_scroll` | `0..27` | `1` | Vertical scrollbar. Calls `applySetGameScroll`. |
| System | `sliderDef set_sys_scroll_thumb` | `gui_set_sys_scroll` | `0..19` | `1` | Vertical scrollbar. Calls `applySetSystemScroll`. |
| Audio | `sliderDef set_audio_scroll_thumb` | `gui_set_audio_scroll` | `0..0` | `1` | Disabled in practice because Audio fits without scrolling. |

Settings popups:

| Popup | Widget/action surface | Type | Effect |
|---|---|---|---|
| Load Defaults confirmation | `pop_b_defaults_yes` | `windowDef` action | Runs `resetdefaults`, sets `desktop::loaddefaults 1`, resets crosshair preview state, and closes the popup. |
| Load Defaults confirmation | `pop_b_defaults_no` | `windowDef` action | Closes the defaults popup without applying changes. |
| Auto Detect confirmation | `set_b_system_auto` | `windowDef` action | Opens `anim_pop_autoIn`; the auto-detect popup handles machine-spec reset/application. |

## Controls

The Controls pane is `p_settings_ctrls`. It has four subpanes selected by action buttons: Movement, Weapons, Attack/Look, and Other. Every row uses `bindDef`.

Category selectors:

| Category | Action widget | Opens | Notes |
|---|---|---|---|
| Movement | `set_b_controls_1` | `p_set_ctrls_move` | Hides the other controls subpanes. |
| Weapons | `set_b_controls_2` | `p_set_ctrls_weap` | Hides the other controls subpanes. |
| Attack/Look | `set_b_controls_3` | `p_set_ctrls_attk` | Hides the other controls subpanes. |
| Other | `set_b_controls_4` | `p_set_ctrls_other` | Hides the other controls subpanes. |

The numbered `set_b_controls_move_1..13` windows are row hover/action surfaces for the visible binding rows.

### Movement

| Label | Widget | Bind command |
|---|---|---|
| Forward | `set_ctrls_move_forward_key` | `_forward` |
| Backpedal | `set_ctrls_move_backpedal_key` | `_back` |
| Move Left | `set_ctrls_move_moveleft_key` | `_moveleft` |
| Move Right | `set_ctrls_move_moveright_key` | `_moveright` |
| Jump | `set_ctrls_move_jump_key` | `_moveUp` |
| Crouch | `set_ctrls_move_crouch_key` | `_moveDown` |
| Turn Left | `set_ctrls_move_turnleft_key` | `_left` |
| Turn Right | `set_ctrls_move_turnright_key` | `_right` |
| Strafe | `set_ctrls_move_strafe_key` | `_strafe` |
| Walk | `set_ctrls_move_walk_key` | `_speed` |

### Weapons

| Label | Widget | Bind command |
|---|---|---|
| Blaster/Gauntlet | `set_ctrls_weap_blastergaunt_key` | `_impulse0` |
| Machinegun | `set_ctrls_weap_mgun_key` | `_impulse1` |
| Shotgun | `set_ctrls_weap_shotgun_key` | `_impulse2` |
| Hyperblaster | `set_ctrls_weap_hblaster_key` | `_impulse3` |
| Grenade Launcher | `set_ctrls_weap_grenade_key` | `_impulse4` |
| Nailgun | `set_ctrls_weap_nailgun_key` | `_impulse5` |
| Rocket Launcher | `set_ctrls_weap_rocket_key` | `_impulse6` |
| Railgun | `set_ctrls_weap_railgun_key` | `_impulse7` |
| Lightning Gun | `set_ctrls_weap_lgun_key` | `_impulse8` |
| Dark Matter Gun | `set_ctrls_weap_dmg_key` | `_impulse9` |
| Napalm Launcher | `set_ctrls_weap_napalmlauncher_key` | `_impulse10` |
| Buy Menu | `set_ctrls_weap_buymenu_key` | `buymenu` |

### Attack/Look

| Label | Widget | Bind command |
|---|---|---|
| Attack | `set_ctrls_attk_attack_key` | `_attack` |
| Previous Weapon | `set_ctrls_attk_prevweap_key` | `_impulse15` |
| Next Weapon | `set_ctrls_attk_nextweap_key` | `_impulse14` |
| Reload | `set_ctrls_attk_reload_key` | `_impulse13` |
| Look Up | `set_ctrls_attk_lookup_key` | `_lookUp` |
| Look Down | `set_ctrls_attk_lookdown_key` | `_lookDown` |
| Mouse Look | `set_ctrls_attk_mouselook_key` | `_mlook` |
| Center View | `set_ctrls_attk_centerview_key` | `centerview` |
| Zoom View | `set_ctrls_attk_zoomview_key` | `_zoom` |
| Objectives | `set_ctrls_attk_objectives_key` | `_impulse19` |
| Flashlight | `set_ctrls_attk_flashlight_key` | `_impulse50` |
| Screenshot | `set_ctrls_attk_screenshot_key` | `screenshot` |
| Weapon Wheel | `set_ctrls_attk_weaponwheel_key` | `_weaponWheel` |

### Other

| Label | Widget | Bind command |
|---|---|---|
| Quick Save | `set_ctrls_other_quicksave_key` | `savegame quick` |
| Quick Load | `set_ctrls_other_quickload_key` | `loadgame quick` |
| Chat | `set_ctrls_other_chat_key` | `clientMessageMode` |
| Team Chat | `set_ctrls_other_teamchat_key` | `clientMessageMode 1` |
| Vote Yes | `set_ctrls_other_voteyes_key` | `voteyes` |
| Vote No | `set_ctrls_other_voteno_key` | `voteno` |
| Stats | `set_ctrls_other_stats_key` | `_ingamestats` |
| Ready | `set_ctrls_other_ready_key` | `ready` |
| Salute | `set_ctrls_other_salute_key` | `emote salute` |
| Cheer | `set_ctrls_other_cheer_key` | `emote cheer` |
| Taunt | `set_ctrls_other_taunt_key` | `emote taunt` |
| Grab | `set_ctrls_other_grab_key` | `emote grab_a` |
| Voice Chat | `set_ctrls_other_vchat_key` | `_voicechat` |

## Game Options

The Game Options pane is `p_settings_game` and is included from `content/baseoq4/guis/menu/settings/game.gui`. It uses hover/action overlays from `game_hovers.gui`.

### Gameplay And View

| Label | Widget type | Value widget | Target | Values or range | Notes |
|---|---|---|---|---|---|
| Free Look | `choiceDef` | `set_game_freelook_value` | `in_freeLook` | `No;Yes` | Boolean picker. |
| Auto Reload | `choiceDef` | `set_game_autoreload_value` | `ui_autoReload` | `No;Yes` | Boolean picker. |
| Auto Switch | `choiceDef` | `set_game_autoswitch_value` | `ui_autoSwitch` | `No;Yes` | Boolean picker. |
| Toggle Zoom | `choiceDef` | `set_game_togglezoom_value` | `in_toggleZoom` | `No;Yes` | Boolean picker. |
| Show Decals | `choiceDef` | `set_game_showdecals_value` | `g_decals` | `No;Yes` | Boolean picker. |
| Show Gun | `choiceDef` | `set_game_showgun_value` | `ui_showGun` | `No;Yes` | Boolean picker. |
| Gun Position | `choiceDef` | `set_game_gunXYZ_value` | GUI state `g_gunXYZ` | `0 Right`, `1 Centered`, `2 Lower Right` | Writes `g_gunX`, `g_gunY`, `g_gunZ`, and `g_weaponFovEffect`. |
| Simple Items | `choiceDef` | `set_game_simpleitems_value` | `g_simpleItems` | `No;Yes` | Boolean picker. |
| Force Model | `choiceDef` | `set_game_forcemodel_value` | GUI state `ui_proskins` | `0 No`, `1 Yes` | Writes `g_forceModel`, `g_forceMarineModel`, and `g_forcestroggmodel`. |

### Mouse

| Label | Widget type | Value widget | Target | Values or range | Notes |
|---|---|---|---|---|---|
| Mouse Input | `windowDef` label | `set_game_mouse` | N/A | N/A | Section label. |
| Invert Mouse | `choiceDef` | `set_game_mpitch_value` | `m_pitch` | `0.022`, `-0.022` | Displayed as `No;Yes`; selecting Yes writes the negative pitch. |
| Mouse Smoothing | `sliderDef` + `editDef` | `set_game_msmooth_slider_bar`, `set_game_msmooth_value` | `m_smooth` | `1..8`, step `1` | Edit field `maxchars 1`. |
| Mouse Sensitivity | `sliderDef` + `editDef` | `set_game_msensitivity_slider_bar`, `set_game_msensitivity_value` | `sensitivity` | `0.01..30`, step `0.01` | Edit field `maxchars 5`. With `m_cpi` enabled, this is degrees per centimeter. |
| Mouse CPI | `sliderDef` + `editDef` | `set_game_mcpi_slider_bar`, `set_game_mcpi_value` | `m_cpi` | `0..32000`, step `50` | `0` preserves classic raw-count sensitivity; nonzero enables CPI-normalized mouse input. |
| View Filter | `sliderDef` + `editDef` | `set_game_mfilter_slider_bar`, `set_game_mfilter_value` | `m_filter` | `0..31`, step `1` | Optional view-angle history filter. |
| Mouse Accel | `sliderDef` + `editDef` | `set_game_maccel_slider_bar`, `set_game_maccel_value` | `cl_mouseAccel` | `-5..5`, step `0.01` | Signed QuakeLive-style acceleration. |
| Accel Offset | `sliderDef` + `editDef` | `set_game_macceloffset_slider_bar`, `set_game_macceloffset_value` | `cl_mouseAccelOffset` | `0..50`, step `0.1` | Movement-rate threshold before acceleration starts. |
| Accel Curve | `sliderDef` + `editDef` | `set_game_maccelpower_slider_bar`, `set_game_maccelpower_value` | `cl_mouseAccelPower` | `1..8`, step `0.05` | Acceleration curve exponent. |
| Sensitivity Cap | `sliderDef` + `editDef` | `set_game_msenscap_slider_bar`, `set_game_msenscap_value` | `cl_mouseSensCap` | `0..100`, step `0.1` | `0` disables the accelerated-sensitivity cap. |

### Controller

| Label | Widget type | Value widget | Target | Values or range | Notes |
|---|---|---|---|---|---|
| Controller Input | `choiceDef` | `set_game_controller_enabled_value` | `in_joystick` | `No;Yes` | Master controller input toggle. |
| Stick Layout | `choiceDef` | `set_game_controller_layout_value` | `in_joystickSouthpaw` | `0 Default`, `1 Southpaw` | Swaps movement and look sticks. |
| Stick Dead Zone | `sliderDef` + `editDef` | `set_game_controller_deadzone_slider_bar`, `set_game_controller_deadzone_value` | `in_joystickDeadZone` | `0..0.95`, step `0.01` | Edit field `maxchars 4`. |
| Look Sensitivity | `sliderDef` + `editDef` | `set_game_controller_look_sensitivity_slider_bar`, `set_game_controller_look_sensitivity_value` | `in_joystickLookSensitivity` | `0.1..4.0`, step `0.05` | Scales analog look speed. Edit field `maxchars 4`. |
| Look Curve | `sliderDef` + `editDef` | `set_game_controller_look_curve_slider_bar`, `set_game_controller_look_curve_value` | `in_joystickLookCurve` | `1.0..3.0`, step `0.05` | `1.0` is linear; higher values soften small stick movement. |
| Invert Look | `choiceDef` | `set_game_controller_invert_value` | `in_joystickInvertLook` | `No;Yes` | Boolean picker. |
| Trigger Threshold | `sliderDef` + `editDef` | `set_game_controller_trigger_slider_bar`, `set_game_controller_trigger_value` | `in_joystickTriggerThreshold` | `0..1.0`, step `0.01` | Analog trigger press threshold. Edit field `maxchars 4`. |
| Rumble | `choiceDef` | `set_game_controller_rumble_value` | `in_joystickRumble` | `No;Yes` | Boolean picker. |
| Rumble Strength | `sliderDef` + `editDef` | `set_game_controller_rumble_strength_slider_bar`, `set_game_controller_rumble_strength_value` | `in_joystickRumbleScale` | `0..2.0`, step `0.05` | `0` effectively mutes rumble. Edit field `maxchars 4`. |

### Crosshair

| Label | Widget type | Value widget | Target | Values or range | Notes |
|---|---|---|---|---|---|
| Custom Crosshair | `choiceDef` | `set_game_customxhair_value` | `g_crosshairCustom` | `Weapon Default;Custom` | Enables or disables the custom preview/action row. |
| Crosshair Preview | `windowDef` action | `set_game_previewxhair` and preview size windows | `chooseCrosshair` command | Next/previous through command args `1` and `-1` | Disabled when `g_crosshairCustom` is off. |
| Crosshair Size | `choiceDef` | `set_game_xhairsize_value` | `g_crosshairSize` | `16 Small`, `24 Medium`, `32 Default`, `40 Large`, `48 Extra Large` | Paired with preview update events. |
| Crosshair Color | `windowDef` swatches | `set_game_xhaircolor_xcolor0..7` plus hover action windows | `g_crosshairColor` | White, red, orange, yellow, green, cyan, blue, magenta | Swatches write RGBA values: `1 1 1 1`, `1 0 0 1`, `1 0.5 0 1`, `1 1 0 1`, `0 1 0 1`, `0 1 1 1`, `0 0 1 1`, `1 0 0.5 1`. |

### Corpse, Language, Console

| Label | Widget type | Value widget | Target | Values or range | Notes |
|---|---|---|---|---|---|
| Corpse Sink | `choiceDef` | `set_game_corpsesink_value` | `g_corpseSink` | `0 Off`, `1 Ragdoll`, `2 No Ragdoll` | Changes corpse sink behavior. |
| Corpse Time | `choiceDef` | `set_game_corpsetime_value` | GUI state `corpse_time_choice` | `Stock`, `Never`, `5 sec`, `10 sec`, `15 sec`, `30 sec`, `60 sec`, `Custom` | Writes `g_corpseRemoveDelaySP` or `g_corpseRemoveDelayMP` depending on current mode. Custom is selected when current cvar does not match a listed value. |
| Language | `choiceDef` | `set_game_language_value` | `sys_lang` | `english`, `spanish`, `french`, `italian` | Displayed as English, Spanish, French, Italian. |
| Console Access | `choiceDef` | `set_game_consoleaccess_value` | `con_allowConsole` | `0 Ctrl+Alt+Tilde`, `1 Tilde` | Controls shortcut required to open the console. |

## System

The System pane is `p_settings_sys` in `mainmenu.gui`. Dynamic resolution, audio-device, and display-device lists are populated by `Session_menu.cpp` when the main menu opens.

The pane has two display layouts:

- Full layout when more than one display is detected. Shows Display Device and Multi-Screen.
- Compact layout when only one display is detected. Hides Display Device and Multi-Screen and repositions the later rows.

### Video Basics

| Label | Widget type | Value widget | Target | Values or range | Notes |
|---|---|---|---|---|---|
| Auto Detect | `windowDef` action | `set_b_system_auto` | Auto-detect popup | N/A | Opens the existing auto-detect confirmation flow. |
| Video Quality / Renderer | `choiceDef` | `set_sys_vidqual_val` | `r_renderer` | `best;arb;arb2;Cg;exp;nv10;nv20;r200` | Displays `BEST;ARB;ARB2;CG;EXP;NV10;NV20;R200`. Marks `desktop::vidwarn`. |
| Screen Size | `choiceDef` | `set_sys_screensize_val_0` | `r_mode` | Runtime `gui::4_3_choices` / `gui::4_3_values` | Visible when aspect state is `Other`. Includes custom mode `-1` when `r_customWidth`/`r_customHeight` match the group. |
| Screen Size | `choiceDef` | `set_sys_screensize_val_1` | `r_mode` | Runtime `gui::16_9_choices` / `gui::16_9_values` | Visible when aspect state is `16:9`. |
| Screen Size | `choiceDef` | `set_sys_screensize_val_2` | `r_mode` | Runtime `gui::16_10_choices` / `gui::16_10_values` | Visible when aspect state is `16:10`. |
| Aspect Ratio | `choiceDef` | `set_sys_aspect_val` | GUI state `r_aspectRatio` | `0 Other`, `1 16:9`, `2 16:10` | Switches which Screen Size picker is visible. |
| Fullscreen | `choiceDef` | `set_sys_fullscreen_val` | `r_fullscreen` | `No;Yes` | Marks `desktop::vidwarn`. |
| Gamma / Brightness | `sliderDef` + `editDef` | `set_sys_gamma_slider`, `set_sys_gamma_value` | `r_brightness` | `0.5..2.0`, step `0.1` | Edit field `maxchars 1`. |

### Display And Advanced Rendering

| Label | Widget type | Value widget | Target | Values or range | Notes |
|---|---|---|---|---|---|
| Display Device | `choiceDef` | `set_sys_display_device_val` | `r_screen` | Runtime `gui::display_names` / `gui::display_values` | Only visible when `gui::display_count > 1`. |
| Multi-Screen | `choiceDef` | `set_sys_multiscreen_val` | `r_multiScreen` | `0 Primary Display Only`, `1 Span All Displays` | Only visible when more than one display is detected. |
| Super Sample | `choiceDef` | `set_sys_supersample_val` | `r_screenFraction` | `75`, `85`, `100`, `125`, `150`, `200` | Values below `100%` keep the existing reduced-resolution choices; values above `100%` render the root scene into a larger single-sample offscreen target and resolve back to the native back buffer. |
| Anti-Aliasing | `choiceDef` | `set_sys_msaa_val` | `r_multisamples` | `0 Off`, `2 2x`, `4 4x`, `8 8x`, `16 16x` | GUI cvar spelling is lower-case `r_multisamples`; the engine cvar is `r_multiSamples` and cvar lookup is case-insensitive. |
| Post AA | `choiceDef` | `set_sys_postaa_val` | `r_postAA` | `0 Off`, `1 SMAA 1x` | Runtime post-process AA option. |
| VSync | `choiceDef` | `set_sys_vsync_val` | `r_swapInterval` | `No;Yes` | Boolean picker. |
| Borderless | `choiceDef` | `set_sys_borderless_val` | `r_borderless` | `No;Yes` | Boolean picker. |
| Fullscreen Policy | `choiceDef` | `set_sys_fullscreen_policy_val` | `r_fullscreenDesktop` | `1 Desktop`, `0 Exclusive` | Desktop/native fullscreen is first in the displayed order. |

### Quality

| Label | Widget type | Value widget | Target | Values or range | Notes |
|---|---|---|---|---|---|
| Shadows | `choiceDef` | `set_sys_shadows_val` | `r_shadows` | `No;Yes` | Boolean picker. |
| Specular | `choiceDef` | `set_sys_specular_val` | `r_skipSpecular` | Display `Yes;No`, implicit `0;1` | Because this is a skip cvar, displayed Yes means `r_skipSpecular 0`. |
| Bump Mapping | `choiceDef` | `set_sys_bump_val` | `r_skipBump` | Display `Yes;No`, implicit `0;1` | Because this is a skip cvar, displayed Yes means `r_skipBump 0`. |
| Sky | `choiceDef` | `set_sys_sky_val` | `r_skipSky` | Display `Yes;No`, implicit `0;1` | Because this is a skip cvar, displayed Yes means `r_skipSky 0`. |
| Ambient Light | `choiceDef` | `set_sys_ambient_val` | GUI state `r_forceAmbientOn` | `No;Yes` | When enabled, sets `r_forceAmbient 0.7`; when disabled, sets `r_forceAmbient 0`. |
| Ambient Brightness | `sliderDef` + `editDef` | `set_sys_ambientbr_val`, `set_sys_ambientbr_valnum` | `r_forceAmbient` | `0.025..1.0`, step `0.025` | Disabled/greyed when Ambient Light is off. Edit field `maxchars 1`. |

### Post Effects

| Label | Widget type | Value widget | Target | Values or range | Notes |
|---|---|---|---|---|---|
| Irradiance Volumes | `choiceDef` | `set_sys_irradiance_val` | `r_useLightGrid` | `No;Yes` | Enables baked light-grid indirect diffuse when available. |
| Bloom | `choiceDef` | `set_sys_bloom_val` | `r_bloom` | `No;Yes` | Boolean picker. |
| Lens Flare | `choiceDef` | `set_sys_lensflare_val` | `r_lensFlare` | `0 Off`, `1 Coronas`, `2 High Quality` | OpenQ4 post-effect quality picker. |
| SSAO | `choiceDef` | `set_sys_ssao_val` | `r_ssao` | `No;Yes` | Boolean picker. |
| Tone Mapping | `choiceDef` | `set_sys_tonemap_val` | `r_hdrToneMap` | `No;Yes` | Boolean picker. |
| CRT Filter | `choiceDef` | `set_sys_crt_val` | `r_crt` | `No;Yes` | Boolean picker. |

## Audio

The Audio pane is `p_settings_audio` in `mainmenu.gui`.

| Label | Widget type | Value widget | Target | Values or range | Notes |
|---|---|---|---|---|---|
| Sound Volume | `sliderDef` + `editDef` | `set_audio_vol_slider`, `set_audio_vol_value` | `s_volume` | `0..2`, step `0.1` | GUI slider range. The current engine cvar declares `0..1`, so runtime clamping may be stricter than the widget. Edit field `maxchars 1`. |
| Sound System | `choiceDef` | `set_audio_backend_val` | `s_useOpenAL` | `0 Default`, `1 OpenAL` | Runs `sound drivar` on release. |
| Sound Device | `choiceDef` | `set_audio_device_val` | `s_deviceName` | Runtime `gui::device_name` / `gui::device_value` | Disabled when OpenAL is off or failed to load. Runs `sound drivar` on release. |
| EAX Reverb | `choiceDef` | `set_audio_eax_val` | `s_useEAXReverb` | `No;Yes` | Disabled when OpenAL is off or failed to load. Runs `sound eax` on release. |
| Surround Speakers | `choiceDef` | `set_audio_speakers_val` | `s_numberOfSpeakers` | `2`, `6` | Displayed through `No;Yes`, so this is effectively stereo vs surround. Runs `sound speakers` on release. |
| Music Volume | `sliderDef` | `set_audio_music_slider` | `s_musicVolume` | `0..1`, step `0.05` | No paired edit field in the active Audio pane. |

## Dynamic Lists

`Session_menu.cpp` fills several GUI state lists at menu-open time:

| GUI state | Used by | Source behavior |
|---|---|---|
| `aspect_choices`, `aspect_values` | System Aspect Ratio | Static `Other;16:9;16:10` with values `0;1;2`. |
| `4_3_choices`, `4_3_values` | System Screen Size | Built from supported modes in the Other aspect group, plus custom mode `-1` when applicable. |
| `16_9_choices`, `16_9_values` | System Screen Size | Built from supported 16:9 modes, plus custom mode `-1` when applicable. |
| `16_10_choices`, `16_10_values` | System Screen Size | Built from supported 16:10 modes, plus custom mode `-1` when applicable. |
| `display_names`, `display_values`, `display_count` | System Display Device / Multi-Screen | Built from SDL display enumeration. Multi-display rows are hidden when `display_count <= 1`. |
| `device_name`, `device_value` | Audio Sound Device | Built from the available audio device list. |

## Hidden Or Legacy Definitions

The GUI file still contains several dormant settings definitions that are not reachable from the current top-level Settings flow:

| Area | Widgets | Status |
|---|---|---|
| System Advanced popup | `pop_setAdv_*` widgets | Popup and rows exist, but the current `showSetSystem` event keeps `set_b_system_adv` hidden. Most options are now represented directly in the System scroll pane. |
| Sound Advanced popup | `pop_set_sndAdv_*` widgets | Popup exists and duplicates Sound System, Sound Device, EAX, and Surround Speakers. The current Audio pane exposes those rows directly. |
| System-embedded audio sliders | `set_sys_vol_slider`, `set_sys_vol2_slider`, `set_sys_vol3_slider` | Hidden at `-100,-100` with `visible 0`; not part of the active System pane. |
| Video Restart button | `set_b_vidrestart` | Clickable action block is commented out. Video changes still set `desktop::vidwarn`; Back handles the warning path. |
