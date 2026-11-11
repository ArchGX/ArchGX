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
| `userdata` | ext2: RetroArch settings | untouched |
| `gb_rom` | FAT: games and saves | untouched by the installer; enlarged on first start if still stock-sized |

## What ArchGX changes in the system partition

The installer (`installer/archgx-install.sh`) unpacks the user's own stock SquashFS, then:

1. **Adds the ArchGX payload**:
   - `/etc/gx-start.sh`, `/etc/gx-reset.sh` and `/etc/fix_storage.sh`;
   - the RetroArch config fragments `/etc/gx-*.cfg`;
   - `/etc/archgx-release`;
   - `/usr/bin/pce-launcher` and `/usr/bin/pce-normalize`;
   - `/usr/bin/retroarch` (our build, replacing Hyperkin's);
   - video filters in `/usr/lib/retroarch/filters/video/`;
   - the game database and artwork in `/usr/share/archgx/`.
2. **Edits three lines of stock configuration**:
   - `/etc/init.d/rcS` starts `/etc/gx-start.sh` instead of Hyperkin's `check_firmware` and no longer sources `check_dumper.sh`;
   - `/etc/input-event-daemon.conf` sends the RESET button (F3) to `gx-reset.sh`;
   - `/etc/usbmount/usbmount.conf` turns automatic USB mounting off, because ArchGX mounts the reader itself, read-only.
3. **Switches off Hyperkin's launcher programs** (mode 000, not deleted): `check_firmware`, `tgdumper-loading`, `menu`, `bte_fw_update_andrew` and their helper scripts.

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
