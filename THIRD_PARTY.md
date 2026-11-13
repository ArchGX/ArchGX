# Third-party components and licences

ArchGX's own code is licensed under the **GNU General Public License v3 or later** ([LICENSE](LICENSE)). This page lists everything ArchGX builds on, what is distributed in a release, and where its source is.

## Distributed in a release (`archgx-<version>.tar.gz`)

| Component | Files in the release | Licence | Source |
|---|---|---|---|
| ArchGX | `pce-launcher`, `pce-normalize`, `gx-start.sh`, `gx-reset.sh`, `fix_storage.sh`, configs, installer script, installer app | GPL-3.0-or-later | This repository |
| Go standard library and runtime | built into the installer app | BSD-3-Clause | https://go.dev |
| **RetroArch** v1.7.4 with the ArchGX patch | `usr/bin/retroarch` | GPL-3.0-or-later | Upstream https://github.com/libretro/RetroArch tag `v1.7.4` (commit `02e23be`), plus [`retroarch/0001-archgx-retron-gx.patch`](retroarch/0001-archgx-retron-gx.patch) and the build recipe ([`retroarch/`](retroarch/), [`scripts/build.sh`](scripts/build.sh)) |
| **Beetle SuperGrafx** libretro core | `usr/lib/libretro/mednafen_supergrafx_libretro.so` (from GP2) | GPL-2.0-only (Mednafen; libretro port) | Upstream https://github.com/libretro/beetle-supergrafx-libretro commit `9c11dcd`, built unmodified by [`scripts/build.sh`](scripts/build.sh) with `HAVE_CDROM=0` and the [glibc 2.26 shim](src/compat/glibc226_shim.c) |
| libretro-common (inside RetroArch) | built into `retroarch` and the filters | MIT-style (per-file headers) | RetroArch tree, `libretro-common/` |
| RetroArch video filter "Blargg NTSC SNES" | `blargg_ntsc_snes.so` | GPL-3.0 wrapper (RetroArch); `snes_ntsc` 0.2.2 by Shay Green is LGPL-2.1-or-later | RetroArch tree, `gfx/video_filters/` |
| ArchGX scanline filter | `archgx_scanlines.so` | GPL-3.0-or-later | In the RetroArch patch |
| ArchGX artwork | `usr/share/archgx/*.png`, `assets/` | Part of ArchGX (GPL-3.0-or-later); the ArchGX name and logos identify this project, so please don't use them for unrelated projects | This repository |
| Game database | `usr/share/archgx/gamedb.csv` | Factual data: CRC32 checksums, header bytes and game titles. Titles are trademarks of their owners | This repository |

**Corresponding source:** every release has a matching git tag in this repository. The release archive includes the RetroArch patch and a `SOURCE.txt` pointing to the exact upstream commit, so the GPL obligations for the RetroArch binary are met by this repository plus the upstream RetroArch repository. The same applies to the Beetle SuperGrafx core and its upstream commit. If an upstream repository were ever unavailable, open an issue and we will provide the complete source.

## Used at build time only (not distributed)

| Component | Use | Licence |
|---|---|---|
| SDL 2.0.12, SDL_image 2.0.5, SDL_ttf 2.0.18 headers | Compiling against the console's SDL libraries | zlib |
| ALSA, PulseAudio, libudev headers (Ubuntu `-dev` packages) | Compiling RetroArch's audio/input drivers | LGPL-2.1-or-later |
| GNU toolchain (`gcc-arm-linux-gnueabihf`), `libgcc` (statically linked) | Compiler; libgcc under the GCC Runtime Library Exception | GPL-3.0 with exception |
| The console's own libraries (glibc 2.26, SDL2, PulseAudio, ...) | Link time only, taken from the user's own card by `scripts/make-sysroot.sh` | Various (LGPL, zlib, ...); never redistributed |

## On the console, not part of ArchGX

ArchGX runs on top of software already on the user's SD card, and neither changes nor redistributes it:
- the Rockchip boot chain (DDR init, miniloader, OP-TEE), U-Boot and the Linux kernel;
- the Mali-400 GPU driver, Weston, SDL2, PulseAudio, BusyBox and glibc;
- the Beetle PCE Fast libretro core;
- the Inter fonts.

These belong to their respective authors and are covered by their own licences, as shipped with the console by its manufacturer.

## Trademarks

"Hyperkin" and "RetroN" are trademarks of Hyperkin Inc. "TurboGrafx-16", "PC Engine" and "HuCard" are trademarks of their owners. ArchGX is an independent project, not affiliated with or endorsed by any of them. Trademarks are used only to describe compatibility.
