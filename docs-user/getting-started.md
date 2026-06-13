# Getting Started with openQ4

This guide is for players who want to install openQ4 and start playing as quickly as possible.

## What You Need

- A legitimate copy of **Quake 4** from [Steam](https://store.steampowered.com/app/2210/Quake_4/) or [GOG](https://www.gog.com/en/game/quake_4)
- The latest openQ4 release from the [Releases page](https://github.com/themuffinator/openQ4/releases)

> [!NOTE]
> openQ4 does not include Quake 4 assets. It uses the original retail game files from your existing installation.

## Recommended Install Flow

1. Install **Quake 4** through Steam or GOG.
2. Download the openQ4 release that matches your CPU architecture.
3. Install or extract openQ4 to its **own folder**.
4. Launch `openQ4-client_<arch>`.
5. On Steam Deck, launch `openQ4-steamdeck` instead.

openQ4 will try to find your Quake 4 install automatically.

## Folder Layout Options

### Recommended

- Leave retail Quake 4 in its original Steam or GOG folder.
- Keep openQ4 in a separate folder.
- Let openQ4 detect the retail `q4base/` assets automatically.

### Portable / Manual

If you prefer a self-contained setup, keep these side by side in the same root folder:

- `baseoq4/` from openQ4
- `q4base/` copied from your Quake 4 install

> [!IMPORTANT]
> Do **not** copy retail PK4 files into `baseoq4/`. That folder is for openQ4 runtime files.

## Platform Notes

### Windows

- You can use the matching installer or the `.zip` release.
- Windows release packages are meant to be self-contained.
- If openQ4 crashes, check the `crashes/` folder beside the executable for log and dump files.

### Linux and macOS

- Extract the release archive to a folder of your choice.
- Linux packages default to the SDL3 runtime path.

### Steam Deck

- Use `openQ4-steamdeck` instead of the normal client launcher.
- For more detail, see the [Steam Deck guide](steam-deck.md).

## If Auto-Detection Fails

Point openQ4 at the **Quake 4 root folder** that contains `q4base/`.

Example:

```text
openQ4-client_x64 +set fs_basepath "C:\path\to\Quake 4"
```

Do not point it directly at `q4base/` or `baseoq4/`.

For advanced path behavior and file layout details, see [TECHNICAL.md](../TECHNICAL.md).

## Next Steps

- Want better video options? See [Display Settings](display-settings.md).
- Want to tune controls? See [Input Settings](input-settings.md).
- Want a quick overview of player-facing options? See [Client Settings Guide](client-settings.md).
- Want to host games? See [Server Setup Guide](server-setup.md).
