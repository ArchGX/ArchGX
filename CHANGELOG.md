# Changelog

## GP1: first public build

**Install**
- New installer (`installer/archgx-install.sh`) that patches the user's own card. It:
  - checks for the supported stock system (`RubyMain-20250709`);
  - backs up the original system partition;
  - verifies after writing;
  - can update and uninstall (`restore`).
- Only the system partition changes. Nothing from Hyperkin or Rockchip is distributed.

**Library**
- Game Library with names, the five most recently played first, rename and delete.
- Controller test and About screens.
- New ArchGX look: logo, emblem, background and yellow accents.

**HuCards**
- A cart is read with a progress percentage, then cleaned (bit order, copier headers, mirrored dumps) and identified: CRC32 first, header bytes as fallback.
- Each cart is stored once; unknown carts can be named on the console.
- Pulling a cart mid-game saves and returns to the library.

**In-game menu** (hold RUN + SELECT)
- Resume, Save/Load State (4 slots with pictures), Rewind, Screenshot, Options, Restart, RetroArch Menu, Quit.
- Rewind: a snapshot every 10 s, the last 2 minutes, chosen by picture.
- Options:
  - aspect ratio (and the front switch, live);
  - screen filter: Scanlines or Composite TV;
  - smoothing, volume, reduce flicker, turbo I/II.
- "Resume last save / Start new game" when starting a game that has a save.

**Reliability**
- Save states are kept in RAM until you save, so the SD card is written less.
- Options are applied once, on leaving the Options screen; rapid changes crashed RetroArch.
- If the display system dies, it is restarted automatically; if that fails, the console reboots.
- First start enlarges the storage partition to fill the card and keeps any files already on it.
- Logs are kept on the card in `logs/`.

**RetroArch**
- RetroArch 1.7.4 built from source, replacing the manufacturer's build. The patch adds:
  - the live aspect switch;
  - live settings and turbo;
  - a fix for the stdin command interface in builds without networking;
  - a scanline filter.

**Known issues**
- A dirty or badly seated HuCard can read wrongly and appear as an unknown cart. Delete it, clean the contacts and reinsert.
- RetroArch exits with a crash code after saving during SDL/Mali shutdown. This is harmless; the manufacturer's build does the same.
- The console has no clock, so saves and recently played games have no dates.

### Before GP1

ArchGX was developed in private test builds (S1 to S2g) on one console from November 2026:
- an independent launcher;
- our own RetroArch build;
- game identification and the library;
- the in-game menu, Options and Rewind;
- crash fixes.

The project was called GPGX during that time.
