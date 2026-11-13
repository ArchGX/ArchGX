# Roadmap

ArchGX aims to be an **easy console**:
- insert and play;
- save and resume;
- clear status messages;
- simple settings;
- RetroArch's full power still there for those who want it.

## Done: GP1

The first public build: library, HuCards, the in-game menu with save slots, rewind and options, an installer that patches your own card. See the [Changelog](CHANGELOG.md).

## Now: GP2 (in beta, GP2-beta9)

Done in the betas: SuperGrafx and CD games, System Cards as BIOS, copying CDs from a USB drive, verified HuCard reads and bad-read handling, standard dumps from flash carts, library colours (verified / modified / unknown), composite colours, autosave, dropped-frame logging, a hidden Debug screen. Details in the [Changelog](CHANGELOG.md).

Still to do before GP2 is released:

| Item | Why |
|---|---|
| **Hardware test of beta11** | Folders and Move to…, USB pickers, cancel and watchdog, readable names |
| **Patcher** | Apply IPS/BPS patches (hacks, translations) to a game; the result is a new game, the original stays |
| **Game database submissions** | Guide and GitHub issue form for sharing `gamedb-local.csv` and `cddb-local.csv` entries |

## Next (candidates)

| Item | Why |
|---|---|
| **Website** | Downloads, guide, compatibility list, database submissions (static, GitHub Pages) |
| **Shadow mask and sharp scaling** | Requested by players; off by default, measured for speed first |
| **Four players** | Up to 4 pads (2 controller ports + 2 USB) for games like Dungeon Explorer |
| **Screenshot viewer** | Browse `screenshots/` from the library |
| **Fine rewind** | After jumping to a snapshot, step back frame by frame |
| **Faster start** | Our own init script without the stock bad-block scan at every boot |
| **RetroArch exit crash** | RetroArch crashes (exit 139) after saving when a game closes: harmless, but worth finding |

## Later

| Item | Notes |
|---|---|
| **RetroArch 1.8 or newer: investigate** | Physical CD support arrived in 1.8. Check speed on the Mali driver, the size of porting our patch, and whether **playing straight from the USB CD drive** is practical (seek times, drive noise) |
| **Beetle PCE Fast from upstream source** | Replace the manufacturer's modified core with one built from public source |
| **More cart and game data** | Grow the game and CD databases from community submissions ([how to contribute](docs/GAMEDB.md)) |
| **Play from removable media** | A USB SSD or stick plugged in becomes the library (games played from it, not copied) |
| **Fully open system image** | Kernel and U-Boot from Rockchip's public sources. Research only: the DDR init blob would still be Rockchip's |

## Parked (hardware research)

| Item | Notes |
|---|---|
| **HuCard flasher** | Write AM29F080 flash HuCards. Best route: a small RP2350-based USB flasher, not the stock reader |
| **Cart reader firmware** | Probe the reader's microcontroller (SWD pads) to learn its protocol; the prototype's reader needs recovery |
| **Upgrade PCB** | Feasibility done: not needed for SuperGrafx or CD |
| **Our own HuCard reader** | A reader/dumper (and flasher) of our own could also serve RetroPie and MiSTer setups |
| **Our own emulation machine** | A console of our own design around that reader. Idea only |

## Not planned

These won't happen:
- shipping games, BIOS files or any manufacturer firmware;
- online features.

Suggestions are welcome as GitHub issues. Please explain what you'd like to do on the console, not only how.
