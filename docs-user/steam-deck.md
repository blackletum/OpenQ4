# Steam Deck

openQ4 supports Steam Deck through the dedicated `openQ4-steamdeck` launcher shipped in Linux packages as of March 30, 2026.

## Launching

- Use `openQ4-steamdeck` instead of `openQ4-client_x64`.
- The launcher adds `+set com_platformProfile steamdeck` and preserves any extra command-line arguments you pass.
- If `SDL_VIDEODRIVER` is unset and both `WAYLAND_DISPLAY` and `DISPLAY` exist, the launcher exports `SDL_VIDEODRIVER=x11` so Steam Deck sessions prefer XWayland for now.

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

Menu behavior:

- `JOY7` and `JOY8` both open the in-game menu.
- `JOY3` selects the focused menu item, while `JOY4`, `JOY7`, and `JOY8` back out of menus.
- The D-pad is reserved for menu focus movement whenever a menu is open, even though those same buttons also have gameplay bindings.
- The left stick can also move menu focus, and holding the D-pad, left stick, or shoulder buttons repeats navigation/scrolling for longer lists.
- Rear paddles are left unbound by default for user customization.

Haptics:

- Controller rumble is driven from Quake 4 sound shake/rumble metadata during gameplay.
- Use `in_joystickRumble 0` to disable motor output, or tune strength with `in_joystickRumbleScale`.

## Asset Discovery

Linux Steam auto-discovery checks these roots and then expands any additional library folders from `libraryfolders.vdf`:

- `~/.steam/steam`
- `~/.local/share/Steam`
- `~/.var/app/com.valvesoftware.Steam/.local/share/Steam`

openQ4 then looks for `steamapps/common/Quake 4` under each Steam library root.

## Notes

- Steam Deck support is explicit launcher/profile behavior, not automatic hardware detection.
- Native Wayland-specific work is not required for this pass. XWayland is the recommended path when both Wayland and X11 are available.
