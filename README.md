<a id="top"></a>

<div align="center">

<img src="assets/docs/img/banner.png" alt="openQ4 banner">

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Status](https://img.shields.io/badge/status-Beta%20Development-d97a1f.svg)](https://github.com/themuffinator/openQ4/releases)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)](https://github.com/themuffinator/openQ4)
[![Architecture](https://img.shields.io/badge/arch-x64%20%7C%20ARM64-orange.svg)](https://github.com/themuffinator/openQ4)

**Play Quake 4 on modern systems with an open-source engine and game-code replacement built around the original retail assets.**

<a href="https://github.com/themuffinator/openQ4/releases">
  <img src="https://img.shields.io/badge/Download-Latest%20Release-2d8f4e?style=for-the-badge&logo=github" alt="Download the latest openQ4 release">
</a>
<a href="https://discord.gg/T32mFejwR4">
  <img src="https://img.shields.io/badge/Join%20the-Discord-5865F2?style=for-the-badge&logo=discord&logoColor=white" alt="Join the openQ4 Discord server">
</a>
<a href="https://github.com/themuffinator/openQ4/stargazers">
  <img src="https://img.shields.io/github/stars/themuffinator/openQ4?style=for-the-badge&logo=github&label=Star%20on%20GitHub" alt="Star openQ4 on GitHub">
</a>
<a href="https://github.com/themuffinator/openQ4/fork">
  <img src="https://img.shields.io/github/forks/themuffinator/openQ4?style=for-the-badge&logo=github&label=Fork%20on%20GitHub" alt="Fork openQ4 on GitHub">
</a>

[Get Started](docs-user/getting-started.md) | [Features](#why-players-use-openq4) | [Player Docs](#player-guides) | [Build from Source](BUILDING.md) | [Technical Reference](TECHNICAL.md)

</div>

---

## What is openQ4?

**openQ4** is an open-source replacement for the Quake 4 engine and game binaries, built to keep the original game playable on modern PCs while improving presentation, controls, packaging, and day-to-day usability.

It is designed for players who want the original Quake 4 experience with a cleaner path to running it on today's hardware.

> [!NOTE]
> openQ4 does **not** include Quake 4 assets. You still need a legitimate Quake 4 copy from Steam or GOG.

<p align="center">
  <img src="assets/docs/img/shot1.png" alt="openQ4 gameplay screenshot showing stock Quake 4 assets running in openQ4" width="92%">
</p>
<p align="center"><sub>Original Quake 4 content, modernized for current systems.</sub></p>

---

## Why players use openQ4

- **Modern display support** for widescreen, ultrawide, multi-monitor, borderless, and fullscreen setups.
- **Optional visual upgrades** such as bloom, HDR, anti-aliasing, soft particles, and enhanced shadow options.
- **Improved input and quality-of-life features** including controller support, better console UX, and modern settings behavior.
- **Single-player and multiplayer in one install** with active compatibility work aimed at the stock game.
- **Cross-platform releases** for Windows, Linux, and macOS, with Steam Deck support in the Linux package line.
- **Open development** with releases, issue tracking, and community feedback all happening in public.

<p align="center">
  <img src="assets/docs/img/shot2.png" alt="openQ4 gameplay screenshot showing combat and bloom" width="49%">
  <img src="assets/docs/img/shot3.png" alt="openQ4 gameplay screenshot showing environment lighting and shadow detail" width="49%">
</p>
<p align="center"><sub>Visual improvements are optional, so you can tune the experience to your taste.</sub></p>

---

## Quick start

1. Install **Quake 4** from [Steam](https://store.steampowered.com/app/2210/Quake_4/) or [GOG](https://www.gog.com/en/game/quake_4).
2. Download the latest openQ4 build from the [Releases page](https://github.com/themuffinator/openQ4/releases).
3. Launch `openQ4-client_<arch>` (or `openQ4-steamdeck` on Steam Deck).
4. If openQ4 does not find your Quake 4 install automatically, follow the path setup notes in the [Getting Started guide](docs-user/getting-started.md).

**Need the step-by-step version?** Start with [docs-user/getting-started.md](docs-user/getting-started.md).

---

## Player guides

### Start here

- [Getting Started](docs-user/getting-started.md) - installation, first launch, and common setup questions
- [Client Settings Guide](docs-user/client-settings.md) - where to find the most useful in-game settings
- [Server Setup Guide](docs-user/server-setup.md) - basic dedicated server setup and common server variables

### Play and tune

- [Display Settings](docs-user/display-settings.md) - fullscreen, windowed mode, resolution scale, and multi-monitor behavior
- [Input Settings](docs-user/input-settings.md) - keyboard, mouse, controller, and binding help
- [Gameplay Settings](docs-user/gameplay-settings.md) - gameplay and audio toggles for everyday play
- [Steam Deck](docs-user/steam-deck.md) - launcher, controls, and Linux handheld notes
- [Multiplayer Networking](docs-user/multiplayer-networking.md) - multiplayer tuning and lag-comp behavior
- [Shadow Mapping](docs-user/shadow-mapping.md) - optional shadow-map settings and troubleshooting
- [Light Grids](docs-user/light-grids.md) - advanced lighting guide for players and testers

### Build and technical docs

- [BUILDING.md](BUILDING.md) - compile openQ4 from source
- [TECHNICAL.md](TECHNICAL.md) - advanced configuration, file layout, compatibility notes, and mod details

---

## Compatibility at a glance

- openQ4 targets the **official Quake 4 retail assets**.
- It ships its **own engine and game modules**.
- It is **not** a drop-in runtime for the original proprietary Quake 4 DLL mods.
- The project is still in **beta development**, so compatibility work is ongoing.

If you run into problems, please use the [issue tracker](https://github.com/themuffinator/openQ4/issues) and include crash logs or setup details when possible.

---

## Contributing

Bug reports, compatibility reports, testing feedback, and code contributions are all welcome. If you want to help build the project itself, start with [BUILDING.md](BUILDING.md).

---

## Credits

- **themuffinator** - openQ4 development and maintenance
- **DarkMatter Productions** - project stewardship and website
- **Justin Marshall** - Quake4Doom and early BSE reverse engineering reference work
- **Robert Beckebans** - renderer modernization reference work, including RBDOOM-3-BFG inspiration
- **id Software** and **Raven Software** - Quake 4 and the underlying technology

---

## License and disclaimer

openQ4 engine code is licensed under the [GNU General Public License v3.0](https://www.gnu.org/licenses/gpl-3.0). See [LICENSE](LICENSE) for details.

The game-library code in [openQ4-GameLibs](https://github.com/themuffinator/openQ4-GameLibs) is derived from the Quake 4 SDK and remains subject to id Software's SDK EULA. Quake 4 assets remain the property of id Software and ZeniMax Media.

openQ4 is an independent project and is not affiliated with, endorsed by, or sponsored by id Software, Raven Software, Bethesda, or ZeniMax Media.

---

[Website](https://www.darkmatter-quake.com) | [Repository](https://github.com/themuffinator/openQ4) | [Game Library](https://github.com/themuffinator/openQ4-GameLibs) | [Issues](https://github.com/themuffinator/openQ4/issues) | [Releases](https://github.com/themuffinator/openQ4/releases)

[Back to Top](#top)
