# Changelog

## GP2-beta12: Settings tab

- **Settings tab** after CD Games: Controller test, About, RetroArch (advanced) and Restart console moved here, so the game tabs list only games, your folders and the pinned HuCard / CD / USB entries. Left from Recent wraps to Settings. The Debug screen is still SELECT held on About.
- **USB pickers:** "Clear all ticks" under Everything.
- The six tabs are spaced to fit the screen; an empty tab no longer shows a cursor.

## GP2-beta11: folders of your own, Move to…, USB pickers, safer copies

- **Fix:** beta10 refused every HuCard that isn't a US card ("Could not prepare the game").
- **Your own folders inside a tab:** shown at the top of the tab ("Folder"); I opens one, II goes back. SELECT on a folder renames it or deletes it when empty. The Recent tab has no folders.
- **Move to…** (SELECT on a game): a folder of the tab, the tab itself, the other HuCard tab (PC Engine ↔ TurboGrafx-16, to fix a game filed in the wrong one) or a **New folder…** named with the keyboard. CDs move with their whole folder.
- **USB import picker:** browse the stick; I ticks a game or a CD's folder (or opens any other folder), SELECT ticks a whole folder, RUN copies. "Everything" is the first entry. Games land in the right tab.
- **USB export picker:** the same over your library (tabs and folders kept on the stick); each exported game's saves go with it.
- **Cancel with II** during import/export: the file in progress is removed, finished files stay.
- **Watchdog:** a copy that makes no progress for 30 seconds is stopped ("The USB drive stopped responding"); mounting and unmounting a stick also give up after 30 seconds. A pulled or full stick stops the copy at the first error.
- **Readable names on the card:** at start-up, games still named by code (`cart-e7f8375c.pce`, a CD's `cd-c19b138e` folder) take their library name; saves, recent list and colours follow.

## GP2-beta10: library tabs, Rename renames the file, USB import/export, card name

- **Tabs** (Left/Right): **Recent** (the 5 last played: a quick pick), **PC Engine**, **TurboGrafx-16**, **SuperGrafx**, **CD Games**, each with its game count. Each system tab is a folder in `games/`. New dumps go to the right one: SuperGrafx carts to SuperGrafx, bit-swapped (US) carts to TurboGrafx-16, other HuCards to PC Engine, copied CDs to CD Games. Games left loose in `games/` (copied from a computer or a USB stick) are moved into the folders at start-up; other folders stay where they are.
- **Pinned entries:** "Play the HuCard in the slot", "Play the CD in the drive" and "USB stick" are at the top of every tab, when there is something to play or copy.
- **Rename renames the file:** a HuCard's file and its saves get the new name (`cart-7ee8cdcd.pce` → `Neutopia II.pce`); a CD's folder is renamed (its tracks keep their names). The SD card now reads like the library. "Mark as SuperGrafx / HuCard" also moves the game to its tab's folder.
- **USB stick import and export** (FAT32): import copies `games/`, `saves/`, `bios/` and `patches/` from the stick's `ArchGX` folder (or its top level) and skips what the card already has; export copies `games/` and `saves/` to `ArchGX/` on the stick, ready for another console. Progress bar, space check, and the cart reader is never mistaken for a stick.
- **Card name:** a card formatted on a computer (no name) is named **RETRONGX** at start-up (new `fat-label` tool; a name you chose is kept).
- **CD copies:** a disc already in the library is found wherever it is (renamed or moved) and plays straight away. The log line for a named disc no longer repeats the title.

## GP2-beta9: faster, cancellable CD copies; CD names

From the first real-drive test (Bonk 3 CD, Sapphire and Nexzr copied at about 1.2 MB/s; Nexzr's last 2,489 sectors were unreadable and took about 22 minutes of retries):
- **Cancel a CD copy with II.** The partial copy is deleted.
- **Damaged areas are skipped.** After 16 bad sectors in a row, only one sector per second of audio is tried, once, until reads work again ("skipping a damaged area..."). Nexzr's damaged edge would now take seconds.
- **Bigger reads:** 48 sectors per request instead of 16 (halved automatically if a drive refuses them).
- **CD names:** a copied disc is named from `cddb.csv` by its disc id. Renaming a CD on the console adds it to `cddb-local.csv`, ready to share like `gamedb-local.csv`.

## GP2-beta8: verified library, flash-cart dumps, autosave, Debug screen

- **Unreadable carts aren't added.** If some bytes differ in all three reads, the game isn't saved: "Couldn't get a clean read of this HuCard". If the vote settles every byte, the read is used.
- **Library colours:** green = matches the game database, amber + "Modified?" = a known title with a different checksum (hack, revision or bad read), grey = not in the database (homebrew, CD games). Games copied from a computer are checked once, in the menu (`read-check.txt` now stores each HuCard's checksum).
- **Standard dumps from flash carts and odd sizes:** huconv.py's 384 KB layout is rebuilt; a known game inside a bigger read (768 KB Darius Plus read as 1 MB, flash-cart leftovers) is trimmed; a region-free patch is undone. The saved file is the standard ROM.
- **Same game, no duplicate:** a dump matches library files with a 512-byte copier header, and files one folder down.
- **Database:** Darius Plus header (it was named Darius Alpha); Dai Makaimura Improvement Hack (SuperGrafx), Darius Alpha/Plus SG Enhanced, Die Hard (English), Dinoforce, Gunhed variants.
- **Autosave every minute** (CD backup RAM) and a flush to the card while a game runs: switching off loses at most a minute.
- **Dropped frames** logged after every game in `gx-start.log` and `logs/last-session.txt`.
- **Debug screen** (hold SELECT for 3 s on About): FPS counter, checksums in the library, read every HuCard twice, pause the USB log during games (on by default), hardware status, last game's frames and how it closed.

## GP2-beta7: bad-read indicator

- A cart read is checked two ways: **unstable** (two reads differed; settled by majority vote) and **mismatch** (the header names a known game but the checksum isn't that game's, so the read is probably damaged).
- Either one shows a red message before the game starts and marks the game **Bad read?** in the library (red, with a hint to clean the HuCard and insert it again). Recorded in `read-check.txt` on the card.
- Inserting the same cart again and getting a good read replaces the bad copy and clears the mark. Saves stay, because the file name doesn't change.

## GP2-beta6: reliable cart reads, easier SuperGrafx, composite colours

From the GP2-beta4 hardware test:
- **Bad reads found:** the SuperGrafx carts that went to a white screen were damaged reads, not an emulation problem. Two reads of Aldynes gave two different checksums; the Daimakaimura read had 358 wrong bytes (bits 0x20/0x60 flipped) and still ran. The unrecognised System Cards were bad reads too (headers like `3838383822222222`).
- **Verified reads:** a cart that isn't an exact database match is read a second time. If the two reads differ, a third read settles every byte by majority vote (`pce-normalize --vote`) and the console says the cart reads unreliably (clean the contacts). Exact matches still start straight away.
- **SuperGrafx by name:** a cart identified by its header as a "(Super Grafx)" title is now filed as `.sgx` even when its checksum doesn't match.
- **New HuCard chooser:** an unknown cart now asks how to run it: Play as PC Engine / Play as SuperGrafx / Use as CD System Card / Not now. No more black screen, quit, SELECT, change.
- **Cart reader warning:** only for the cart reader, and only after it has been missing for 30 seconds in a row (it disconnects briefly at every cart change). The USB ports are shown in About only.
- **Composite colours** (Options): the composite-video palette of both cores (`pce_fast_palette` / `sgx_palette`), applied live.
- **Performance:** RetroArch's own frame-by-frame rewind (on in the stock config: a save state every 10 frames into a 20 MB buffer) is switched off. ArchGX's rewind is unaffected. This should reduce the CD skips (2.6 % dropped frames in Rondo of Blood and Lords of Thunder).

## GP2-beta5: copy CDs from a USB CD drive

- New `cd-rip`: with a USB CD drive in a front port and a disc inserted, **Play the CD in the drive** appears in the library. The first time it copies the disc to `games/cd-<id>/` (one `.bin` per track plus a `.cue`, raw 2352-byte sectors), with a progress bar showing the track, speed and time left, then plays it. A disc copied before is recognised by its table of contents and plays straight away.
- Bad reads are retried (chunk twice, then sector by sector three times); unreadable sectors are zero-filled and reported. Drives that refuse raw data reads fall back to 2048-byte data sectors. Copying happens in a hidden folder, so an interrupted copy never shows up in the library. Free space is checked first.
- Copied discs are named "Unknown CD (id)"; Rename starts with an empty name for any "Unknown ..." game.

## GP2-beta4: hardware check on screen

- 20 seconds after power-on, the library checks for the cart reader (2e25:988c) and the front USB ports (hub 1a40:0101) every 2 seconds. If one is missing, a red line under the list says so ("Cart reader not found: HuCards and the controller ports won't work", "Front USB ports not found", or "Cart reader is in update mode: restart the console"). It clears by itself when the device comes back.
- About shows "Cart reader: OK / not found" and "Front USB ports: OK / not found". The result also goes to `logs/launcher.log`.

## GP2-beta3: USB event log (hardware work)

- `logs/usb.log` lists the USB devices present at start-up, then every device that appears (+) or disappears (−), with its ID, speed and name. Read-only: nothing is sent to any device.
- Game database: header signatures for 1941 Counter Attack, Aldynes and Daimakaimura.

## GP2-beta2: game options for SuperGrafx and System Card dumps

Hardware test of GP2-beta1: SuperGrafx games and a CD game (Sylphia, .cue) ran at full speed. System Card and SuperGrafx carts were dumped as unknown HuCards, because the reader dumps 1 MB whatever the card holds.

- **Game options (SELECT):** **Mark as SuperGrafx** / **Mark as HuCard** changes only the file extension, so saves, title and recent order stay. **Use as CD System Card** saves the first 256 KB to `bios/` (named after the card when its checksum is known, else `syscard3.pce`) and removes the dump from the library.
- **Better detection:** cart dumps are also checked by the checksum of their first 256 KB and 512 KB, so oversized System Card and SuperGrafx dumps are recognised. Both checksums are logged to help grow the database.
- Footer hint now reads "SELECT  Game options".

## GP2-beta1: SuperGrafx and CD games (beta)

**New**
- **SuperGrafx:** ArchGX now ships Beetle SuperGrafx, built from upstream source. SuperGrafx games (`.sgx`, or a known SuperGrafx HuCard by checksum) run on it; everything else stays on the stock core. SuperGrafx HuCards dumped in the slot are stored as `.sgx`. "Reduce flicker" also applies to this core.
- **CD games:** `.cue`/`.bin`, `.chd`, `.ccd` and `.m3u` (multi-disc) in `games/`, loose or one folder per game. Tracks and discs named by a playlist are hidden; a folder holding one game shows the folder name.
- **System Card as BIOS:** inserting a System Card saves it to `bios/` instead of playing it. CD games pick the best System Card present (3.0 > US 3.0 > 2.x > US 2.0 > 1.0); without one a clear message says what to do.
- Library markers **SGX** and **CD**. Deleting a CD game removes its tracks and empty folder.

**Under the hood**
- glibc 2.26 shim: `fstatat64`, `arc4random*`, `__libc_single_threaded`, `__ioctl_time64` (needed by the new core).
- The startup log shows the installed build from `/etc/archgx-release`.

**Known issues / to check on hardware**
- CD speed on the Cortex-A7, especially `.chd` (decompressed on the fly).
- SuperGrafx speed (two video chips: heavier than PCE).
- Arcade Card games are not supported by the stock core.
- The System Card checksums for 1.0/2.0/2.1 come from the game database and are not yet confirmed with real cards.

## GP1: first public build

**Install**
- New installer (`installer/archgx-install.sh`) that patches the user's own card. It:
  - checks for the supported stock system (`RubyMain-20250709`);
  - backs up the original system partition;
  - verifies after writing;
  - can update and uninstall (`restore`).
- Installer **app** for Windows, macOS and Linux: finds the card by itself, one confirmation, no other tools needed. It contains its own SquashFS reader/writer and does the same as the script.
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
