# RetroArch for ArchGX

ArchGX ships **RetroArch v1.7.4** (upstream commit `02e23be127db29a72170d3c79bfd642288cd15ea`, https://github.com/libretro/RetroArch) built from source with one patch. RetroArch is GPL-3.0-or-later.

| File | What it is |
|---|---|
| `0001-archgx-retron-gx.patch` | The ArchGX changes (`git apply` on the v1.7.4 tree) |
| `config.mk.in` | RetroArch's generated `config.mk`, with the sysroot paths as `@SYSROOT@`, `@ROOTFS@` and `@BUILD@`; `scripts/build.sh` fills them in |
| `config.h` | RetroArch's generated `config.h` |

## What the patch changes

See [docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md#the-retroarch-patch). In short:
- the front aspect switch, applied live;
- live settings from the ArchGX menu, read from `/tmp/gx/ra-set`;
- turbo for I and II;
- a fix so the stdin command interface works without networking;
- a scanline video filter.

## Configuration

**`configure` options used to produce `config.mk` and `config.h`:**

```
--enable-sdl2 --disable-sdl --enable-udev --enable-pulse --enable-alsa --enable-threads
--enable-zlib --enable-builtinzlib --enable-rgui --enable-neon --enable-floathard
```

These drivers and menus were disabled: x11, wayland, kms, egl, opengl, vulkan, qt, ffmpeg, freetype, xmb, materialui, networking, cheevos and the other optional drivers.

**Changes made afterwards:**
- `HAVE_GETOPT_LONG` off (RetroArch's built-in getopt is used);
- `HAVE_CXX=0`;
- `CFLAGS`: `-O2 -mcpu=cortex-a7 -mfpu=neon -mfloat-abi=hard -marm -D_TIME_BITS=32 -U_FORTIFY_SOURCE -fno-stack-protector -fno-pie -fcommon`;
- link: `-no-pie -static-libgcc` and the glibc 2.26 shim (`src/compat/`).
