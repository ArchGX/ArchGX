# Roadmap

ArchGX aims to be an **easy console**:
- insert and play;
- save and resume;
- clear status messages;
- simple settings;
- RetroArch's full power still there for those who want it.

## Now: GP1

The first public build: library, HuCards, the in-game menu with save slots, rewind and options, an installer that patches your own card. See the [Changelog](CHANGELOG.md).

## Next: GP2 (candidates)

| Item | Why |
|---|---|
| **Bad-read detection** | Recognise garbage or nearly-correct HuCard reads (one bit off a known game's header) and ask for a reinsert instead of adding an unknown cart |
| **Four players** | Up to 4 pads (2 controller ports + 2 USB) for games like Dungeon Explorer; per-game input settings where a game needs them |
| **Screenshot viewer** | Browse `screenshots/` from the library |
| **Fine rewind** | After jumping to a snapshot, step back frame by frame using RetroArch's rewind buffer |
| **Installer for Windows** | A simple tool that works on the card directly, without WSL |
| **Faster start** | Our own init script without the stock bad-block scan at every boot |

## Later

| Item | Notes |
|---|---|
| **Beetle PCE Fast from upstream source** | Replace the manufacturer's modified core with one built from public source |
| **Newer RetroArch** | Move beyond 1.7.4 if it stays fast on the Mali driver |
| **More cart and game data** | Grow the game database from community submissions ([how to contribute](docs/GAMEDB.md)) |
| **Fully open system image** | Kernel and U-Boot from Rockchip's public sources. Research only: the DDR init blob would still be Rockchip's |

## Not planned

These won't happen:
- shipping games, BIOS files or any manufacturer firmware;
- online features.

Suggestions are welcome as GitHub issues. Please explain what you'd like to do on the console, not only how.
