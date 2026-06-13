# Server Setup Guide

This guide covers a simple way to host an openQ4 dedicated server.

## What You Need

- A working openQ4 install
- Access to the original Quake 4 retail assets
- The `openQ4-ded_<arch>` executable from your openQ4 release or local build

## Quick Start

1. Make sure openQ4 can see your Quake 4 assets.
2. Launch the dedicated server executable.
3. Set a server name, map, and game type.
4. Start the server with `spawnServer`.

Example startup flow:

```text
openQ4-ded_x64 +set si_name "My openQ4 Server" +set si_map mp/q4dm1 +set si_gameType dm +spawnServer
```

## Common Server Variables

| Variable | What it controls |
|---|---|
| `si_name` | Server name shown to players |
| `si_map` | Starting map |
| `si_gameType` | Multiplayer game type |
| `si_fragLimit` | Frag limit |
| `si_timeLimit` | Time limit |
| `si_warmup` | Whether warmup is used |
| `g_mapCycle` | Map cycle script |

Default multiplayer values are seeded from `content/baseoq4/default.cfg`.

## Useful Console Commands

| Command | What it does |
|---|---|
| `spawnServer` | Starts the server |
| `disconnect` | Shuts the server down |
| `serverMapRestart` | Restarts the current map |
| `serverNextMap` | Advances to the next map |
| `kick` | Kicks a client by slot number |
| `gameKick` | Kicks a client by player name |

## Multiplayer Tuning

If you want to tune prediction or lag compensation behavior, see [Multiplayer Networking](multiplayer-networking.md).

## Notes

- openQ4 uses its own engine and game modules.
- openQ4 is not a drop-in runtime for the original proprietary Quake 4 DLL mods.
- For advanced configuration, file layout, and path behavior, see [TECHNICAL.md](../TECHNICAL.md).
