# How ArchGX works

## The console

**Hardware and firmware**

| Part | What it is |
|---|---|
| SoC | Rockchip RK3128 (4× Cortex-A7, Mali-400 GPU) |
| HDMI output | 1280×720 |
| Firmware | Buildroot 2018.02, Linux 4.4, glibc 2.26 |
| Graphics stack | Weston 3 (Wayland), SDL2 2.0.12 |
| Audio | PulseAudio |
| Emulator | RetroArch with the Beetle PCE Fast core |

**HuCard slot and pads**
- The slot is a USB device (`2E25:988C`, "BTE TG Dumper"). It reads the inserted cart by itself and presents the dump as a file on a small USB mass-storage drive.
- The same device provides the two controller ports as USB HID pads.

**SD card partitions (GPT)**

| Partition | Contents | ArchGX |
|---|---|---|
| `uboot`, `trust`, `boot` | Bootloader, OP-TEE, kernel | untouched |
| `rootfs` | Read-only SquashFS system | **the only partition the installer changes** |
| `userdata` | ext2: RetroArch settings | untouched by the installer; written to while running (see below) |
| `gb_rom` | FAT: games and saves | untouched by the installer; enlarged on first start if still stock-sized; named RETRONGX if it has no name |

## What ArchGX changes in the system partition

Both installers do the same:
- the app (`installer/app/`, its own SquashFS reader/writer, for Windows, macOS and Linux);
- the script (`installer/archgx-install.sh`, using squashfs-tools).

Each reads the user's own stock SquashFS from the card, then:

1. **Adds the ArchGX payload** (all ArchGX's own files or built from public source; nothing taken from the stock image):
   - scripts: `/etc/gx-start.sh` (supervisor), `/etc/gx-reset.sh` (RESET button), `/etc/fix_storage.sh` (storage partition repair);
   - RetroArch config fragments `/etc/gx-retroarch.cfg`, `/etc/gx-retroarch-advanced.cfg`, `/etc/gx-resume.cfg` (applied with `--appendconfig`; the stock config files are not changed by the installer);
   - `/etc/archgx-release` (version);
   - ArchGX programs in `/usr/bin/`: `pce-launcher` (library, progress screens, in-game menu, USB pickers), `pce-normalize` (cart dump cleanup and identification helpers), `cd-rip` (copies a CD from a USB drive), `fat-label` (names the card), `zipit` (bug-report zips);
   - `/usr/bin/retroarch`: RetroArch 1.7.4 built from source with our patch, **replacing Hyperkin's RetroArch binary** (the stock one is overwritten in the new system partition; the backup keeps it);
   - `/usr/lib/libretro/mednafen_supergrafx_libretro.so`: the Beetle SuperGrafx core, built unmodified from upstream;
   - video filters in `/usr/lib/retroarch/filters/video/` (Scanlines, Blargg NTSC composite);
   - in `/usr/share/archgx/`: artwork, the HuCard database `gamedb.csv` and the CD database `cddb.csv` (see [Game database](GAMEDB.md) for where the entries come from).
2. **Edits three lines of stock configuration**:
   - `/etc/init.d/rcS` starts `/etc/gx-start.sh` instead of Hyperkin's `check_firmware` and no longer sources `check_dumper.sh`;
   - `/etc/input-event-daemon.conf` sends the RESET button (F3) to `gx-reset.sh`;
   - `/etc/usbmount/usbmount.conf` turns automatic USB mounting off, because ArchGX mounts the reader itself, read-only.
3. **Switches off Hyperkin's launcher programs** (mode 000, not deleted): `check_firmware`, `tgdumper-loading`, `menu`, `bte_fw_update_andrew`, and the scripts `check_dumper.sh`, `firmware_update.sh`, `partition_resize.sh`, `reset-btn.sh`, `start_tgdumper.sh` and the three `usbmount` RetroArch hooks.

**Updating:** with the stock backup, the new system is built from it as on a first install. Without it, the card's current ArchGX system is the starting point: every step above can be repeated, and files listed in `/etc/archgx-files` (written by every install from GP2.1 on) that the new version no longer ships are removed. Both give the same system.

**What the installer does not do:** no binary patches (no stock program is edited byte by byte), no changes to the boot chain, kernel or other partitions, and no Hyperkin files are copied into the release. The stock system partition is backed up first (`archgx-stock-rootfs.img`) and `restore` puts it back byte for byte.

Everything else is the console's own: boot chain, kernel, Mali driver, Weston, SDL2, PulseAudio, the Beetle PCE Fast core and the fonts.

## Runtime

```
rcS ─► Weston ─► gx-start.sh ─┬─► pce-launcher           (Game Library)
                              ├─► pce-launcher --status  (progress screens)
                              ├─► reader: mount ro, copy dump, pce-normalize, identify
                              └─► retroarch ◄── stdin FIFO ── pce-launcher --ingame (menu)
```

**`gx-start.sh`** is the supervisor:
- repairs storage on first start (`fix_storage.sh`) and runs one-time imports;
- shows the library;
- handles HuCards (read-only mount → copy with progress → `pce-normalize` → identify by CRC32/header → file into `games/`);
- turns `archgx-settings.cfg` into RetroArch settings for each launch;
- launches RetroArch, then returns to the library;
- restarts Weston if it dies.

**`pce-launcher`** is one SDL2 program with three modes: the library, `--status` (progress screens) and `--ingame` (the RUN+SELECT menu).

**The in-game menu** runs beside RetroArch for the whole game:
- It watches the pads for RUN+SELECT; RetroArch's own menu combo is switched off.
- It opens a translucent window above RetroArch, so the paused game stays visible.
- It controls RetroArch through RetroArch's stdin command interface: `PAUSE_TOGGLE`, `SAVE_STATE`, `LOAD_STATE`, `RESET`, `MENU_TOGGLE`, `QUIT`. The interface is a FIFO kept open by `gx-start.sh`.
- RetroArch ignores save/load/menu commands while paused, so the menu unpauses before sending them.

**Save states**
- RetroArch always saves to and loads from its slot 0, kept in RAM (`/tmp/gx/states`), with a thumbnail of the core's frame.
- The menu copies slot 0 to `saves/<game>.state1..4` (+ `.png`) on the card, and copies a slot back before loading. RetroArch's own slot counter never matters, and only an explicit Save writes to the SD card.

**Rewind**
- Every 10 s the menu process asks for a slot-0 save and moves it into a ring of 12 snapshots in `/tmp/gx/rewind`.
- Going back is a load of one of them.

**Options**
- Options are stored in `archgx-settings.cfg` on the card.
- When you leave the Options screen, the menu writes `/tmp/gx/ra-set`, which the RetroArch patch applies live. Applying once per visit matters: several video re-inits in quick succession crashed RetroArch on this driver.
- At launch, `gx-start.sh` writes the same settings as an appended RetroArch config.

## What ArchGX changes while it runs

Besides `games/`, `saves/` and its own files on the card, ArchGX writes to a few stock places at run time:

| Where | What | Why |
|---|---|---|
| `gb_rom` partition | Enlarged or recreated by `fix_storage.sh` on first start if still stock-sized (files are kept), behind a "Preparing your SD card" screen; named `RETRONGX` by `fat-label` if it has no name | The stock resize deleted this partition on large cards |
| `/userdata/fix_storage.log` | Each first-start step (copied to `logs/fix_storage-steps.log`) | Survives a failed format |
| `/userdata/retroarch/retroarch-core-options.cfg` | Before each game: `pce_disable_softreset`, `sgx_disable_softreset`, `sgx_forcesgx`, the CD BIOS choice, sprite limit and palette options | RUN+SELECT is the menu combo, not a soft reset; Options-menu choices |
| `/userdata/retroarch_org/` (stock persistent copy of the RetroArch settings) | After each game, settings changed in RetroArch's own menu are copied back; a `.stock` copy is kept once. An empty `reset-retroarch-settings` file on the card restores the stock settings | So RetroArch menu changes survive a restart, as stock |
| `/userdata/*.pce` | System Card files from `bios/` are copied where the cores look for them | CD games |
| `/tmp/SelectGame.txt` | The game's title | Read by the console's own Beetle PCE Fast core, as with the stock launcher |

ArchGX never writes to the HuCard reader (mounted read-only, no commands sent) or to a CD.

## The RetroArch patch

RetroArch v1.7.4 plus [`retroarch/0001-archgx-retron-gx.patch`](../retroarch/0001-archgx-retron-gx.patch):

| File | Change |
|---|---|
| `retroarch.c` | Every 30 frames: reads the front switch (`/sys/devices/platform/adc-vol/ratio`, `0` = 4:3, `1` = 16:9) and applies changes live; re-reads `/tmp/gx/ra-set` when it changes (aspect, video filter, smoothing, volume, the `pce_nospritelimit` core option, turbo) |
| `input/input_driver.c` | Turbo for I and II (2 frames pressed, 2 released) |
| `command.c` | Makes the stdin command interface work in builds without networking. Upstream 1.7.4 left stdin blocking and parsed commands only with `HAVE_NETWORKING` |
| `gfx/video_filters/archgx_scanlines.c` | Scanline filter: doubles the height, darkens every second line |

The build uses SDL2 video (Mali GLES through SDL), PulseAudio, udev input and RGUI. Networking, Qt, Vulkan and the other menus are left out. Our SDL2 video driver can't run shaders, so screen effects use RetroArch's CPU "softfilters".

## Logs

All logs are written to `logs/` on the card's FAT partition and synced:
- `gx-start.log`: supervisor;
- `retroarch.log` and `.1`: the last two games;
- `ingame.log`: menu commands;
- `launcher.log`: library;
- `fix_storage.log`: first-start partition work.
