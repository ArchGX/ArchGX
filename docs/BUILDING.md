# Building ArchGX from source

Everything in a release is built from this repository:
- the launcher and menus (`pce-launcher`);
- the dump cleaner (`pce-normalize`);
- RetroArch (upstream v1.7.4 plus [our patch](../retroarch/0001-archgx-retron-gx.patch));
- the video filters.

The console runs glibc 2.26 on an ARMv7 Rockchip RK3128. ArchGX cross-compiles with a current Ubuntu toolchain and links against the console's **own** libraries, extracted from your card. Those libraries are used only at link time and are never distributed.

## Host

Ubuntu 24.04 (x86-64) or similar:

```sh
sudo apt install gcc-arm-linux-gnueabihf binutils-arm-linux-gnueabihf make git \
                 squashfs-tools util-linux python3
```

Building the release's installer app also needs [Go](https://go.dev/dl/) 1.22 or newer. It has no other dependencies and cross-compiles for every platform from Linux.

## 1. Get the console's system partition from your card

Use the installer's backup (`archgx-stock-rootfs.img`), or read it from a card image:

```sh
sudo ./installer/archgx-install.sh check card.img      # shows the rootfs start sector and size
dd if=card.img of=stock-rootfs.img bs=512 skip=38912 count=98304
sudo unsquashfs -d stock-rootfs stock-rootfs.img
```

## 2. Make the sysroot

```sh
scripts/make-sysroot.sh stock-rootfs sysroot
```

This copies the needed libraries from `stock-rootfs` and fetches the headers:
- SDL 2.0.12, SDL_image 2.0.5 and SDL_ttf 2.0.18, from their upstream git tags;
- ALSA, PulseAudio and libudev, from the Ubuntu `-dev` packages.

To use copies you've already downloaded, set `SDL_SRC`, `SDL_IMAGE_SRC`, `SDL_TTF_SRC` and `DEB_INCLUDE` (a `usr/include` tree).

## 3. Build

```sh
scripts/build.sh sysroot stock-rootfs build
```

It clones RetroArch v1.7.4 (commit `02e23be`), applies the patch, configures it from `retroarch/config.mk.in` and `retroarch/config.h`, and builds everything. Set `RA_SRC=<checkout>` to reuse an existing clone.

The result is `build/payload/`: the files the installer adds to the console. The build checks that every binary needs nothing newer than glibc 2.26.

**Compatibility details** (see `src/compat/`):
- glibc ≥ 2.38 headers rename some functions; `glibc226_shim.c` maps them back.
- Binaries are built with 32-bit `time_t` and without fortify or stack-protector symbols.
- `oldmath.h` binds `pow`/`exp`/`powf` to old symbol versions.

## 4. Test on a card image and package

```sh
sudo installer/archgx-install.sh install card.img --payload build/payload
scripts/make-release.sh build dist
```

The release step produces:
- `dist/ArchGX-Installer-<VERSION>-{Windows.exe,macOS-AppleSilicon,macOS-Intel,Linux}`;
- `dist/archgx-<VERSION>.tar.gz`;
- `dist/SHA256SUMS`.

### The installer app

`installer/app/` is a Go program with no outside libraries:
- a small SquashFS 4.0 (gzip) reader and writer (`squashfs.go`);
- the same changes as the shell installer (`patch.go`);
- GPT parsing and card detection (`disk*.go`).

The payload is embedded at build time. To test it against a stock system partition:

```sh
cd installer/app
tar -C ../../build/payload -czf payload.tar.gz . && cp ../../VERSION .
STOCK=stock-rootfs.img PAYLOAD=payload.tar.gz OUT=/tmp/gp.sqfs go test -run TestConvert -v
```

### Emulating parts of the system

The binaries run on an x86 PC with QEMU user mode, using the extracted system partition as the library root. This is handy for menu screenshots and command-channel tests:

```sh
SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software PCE_LAUNCHER_KEYS="down,ok" \
PCE_LAUNCHER_SHOT=shot.bmp qemu-arm-static -L stock-rootfs build/payload/usr/bin/pce-launcher games out
```

Test hooks (environment variables) in `pce-launcher`:

| Variable | Effect |
|---|---|
| `PCE_LAUNCHER_KEYS` | Comma-separated scripted input: `up`, `down`, `left`, `right`, `ok`, `back`, `run`, `select` |
| `PCE_LAUNCHER_SHOT` | Save the screen as BMP once the keys are processed, then exit |
| `PCE_LAUNCHER_KEYS_DELAY` | In-game mode: wait this many seconds (snapshots keep running) before the keys |
