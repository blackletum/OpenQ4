# Steam Deck

openQ4 supports Steam Deck through the dedicated `openQ4-steamdeck` launcher shipped in Linux packages as of March 30, 2026.

## Launching

- Use `openQ4-steamdeck` instead of `openQ4-client_x64`.
- The launcher adds `+set com_platformProfile steamdeck` and preserves any extra command-line arguments you pass.
- If `SDL_VIDEO_DRIVER` and `SDL_VIDEODRIVER` are both unset and both `WAYLAND_DISPLAY` and `DISPLAY` exist, the launcher exports `SDL_VIDEO_DRIVER=x11` and `SDL_VIDEODRIVER=x11` so Steam Deck sessions prefer XWayland for now.
- To test native Wayland explicitly, launch with `SDL_VIDEO_DRIVER=wayland` or `SDL_VIDEODRIVER=wayland`.

## Controls

Default gameplay bindings shipped by the stock openQ4 config:

- `JOY15` = attack
- `JOY16` = zoom
- `JOY3` = jump
- `JOY4` = crouch
- `JOY6` = reload
- `JOY5` = flashlight
- `JOY1` = weapon wheel
- `JOY2` = last weapon
- `JOY13` = run toggle / walk modifier
- `JOY18` = show objectives / scores
- `JOY19` = jump if exposed as right rear paddle 1
- `JOY20` = crouch if exposed as left rear paddle 1
- `JOY21` = reload if exposed as right rear paddle 2
- `JOY22` = weapon wheel if exposed as left rear paddle 2

Menu behavior:

- `JOY7` and `JOY8` both open the in-game menu; `JOY17` also opens the menu when the OS delivers the Guide button to the game.
- `JOY3` selects the focused menu item, while `JOY4`, `JOY7`, and `JOY8` back out of menus.
- The D-pad is reserved for menu focus movement whenever a menu is open, even though those same buttons also have gameplay bindings.
- The movement stick can also move menu focus, and holding the D-pad, movement stick, or shoulder buttons repeats navigation/scrolling for longer lists.
- Extra `JOY23` through `JOY32` and `AUX1` through `AUX16` buttons are bindable but left unbound by default for user customization.

Tuning and haptics:

- Controller rumble is driven from Quake 4 sound shake/rumble metadata during gameplay.
- Use `in_joystickRumble 0` to disable motor output, or tune strength with `in_joystickRumbleScale`.
- The in-game menu exposes these controls under `Settings -> Game Options -> Controller`, alongside stick layout, radial dead-zone, look sensitivity, look curve, invert-look, and trigger-threshold tuning.
- For the full input settings guide, see [input-settings.md](input-settings.md).

## Asset Discovery

Linux Steam auto-discovery checks these roots and then expands any additional library folders from `libraryfolders.vdf`:

- `~/.steam/steam`
- `~/.local/share/Steam`
- `~/.var/app/com.valvesoftware.Steam/.local/share/Steam`

openQ4 then looks for `steamapps/common/Quake 4` under each Steam library root.

## Notes

- Steam Deck support is explicit launcher/profile behavior, not automatic hardware detection.
- Native Wayland is supported through the SDL3 backend, but the Steam Deck launcher keeps XWayland as its default mixed-session path until Deck-specific Wayland validation is broader.
