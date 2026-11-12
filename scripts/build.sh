#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# build.sh - build every ArchGX binary and assemble the install payload.
#
# Usage: scripts/build.sh <sysroot> <stock-rootfs-dir> [out-dir]
#   <sysroot>          made by scripts/make-sysroot.sh
#   <stock-rootfs-dir> the console's system partition, extracted from your card
#   [out-dir]          default: build/
# RA_SRC=dir uses an existing RetroArch v1.7.4 checkout instead of cloning it.
# SGX_SRC=dir does the same for Beetle SuperGrafx (commit $SGX_COMMIT).
#
# Result: <out-dir>/payload/ - the files the installer adds to the console's
# system partition (see installer/archgx-install.sh).
set -eu

[ $# -ge 2 ] || { echo "usage: $0 <sysroot> <stock-rootfs-dir> [out-dir]" >&2; exit 1; }
TOP=$(cd "$(dirname "$0")/.." && pwd)
SYSROOT=$(cd "$1" && pwd)
ROOTFS=$(cd "$2" && pwd)
mkdir -p "${3:-$TOP/build}"; OUT=$(cd "${3:-$TOP/build}" && pwd)
RA_TAG=v1.7.4
RA_COMMIT=02e23be127db29a72170d3c79bfd642288cd15ea
SGX_COMMIT=9c11dcd213a79d57d2c86313971631af801f2cdc   # libretro/beetle-supergrafx-libretro

CC=arm-linux-gnueabihf-gcc
STRIP=arm-linux-gnueabihf-strip
ARCH="-mcpu=cortex-a7 -mfpu=neon -mfloat-abi=hard"
# The console has glibc 2.26: 32-bit time_t, no fortify/stack-protector symbols,
# 64-bit file offsets through the glibc226 shim.
CFLAGS="-O2 -Wall -Wno-unused-function $ARCH -D_FILE_OFFSET_BITS=64 -U_TIME_BITS -D_TIME_BITS=32 -U_FORTIFY_SOURCE -fno-stack-protector"
LINK="-static-libgcc -no-pie -Wl,-rpath-link,$ROOTFS/usr/lib:$ROOTFS/lib"
PAY=$OUT/payload
rm -rf "$PAY"; mkdir -p "$OUT/obj" "$PAY/etc" "$PAY/usr/bin" "$PAY/usr/lib/retroarch/filters/video" "$PAY/usr/lib/libretro" "$PAY/usr/share/archgx"

echo "== glibc 2.26 shim"
$CC -O2 $ARCH -U_TIME_BITS -D_TIME_BITS=32 -c "$TOP/src/compat/glibc226_shim.c" -o "$OUT/obj/glibc226_shim.o"
$CC -O2 $ARCH -fPIC -U_TIME_BITS -D_TIME_BITS=32 -c "$TOP/src/compat/glibc226_shim.c" -o "$OUT/obj/glibc226_shim_pic.o"

echo "== pce-launcher"
$CC $CFLAGS -I"$SYSROOT/inc" -c "$TOP/src/pce-launcher/pce-launcher.c" -o "$OUT/obj/pce-launcher.o"
$CC -o "$OUT/obj/pce-launcher" "$OUT/obj/pce-launcher.o" "$OUT/obj/glibc226_shim.o" -L"$SYSROOT/lib" $LINK \
	-lSDL2_ttf -lSDL2_image -lSDL2
$STRIP -o "$PAY/usr/bin/pce-launcher" "$OUT/obj/pce-launcher"

echo "== pce-normalize"
$CC $CFLAGS -o "$OUT/obj/pce-normalize" "$TOP/src/pce-normalize/pce-normalize.c" "$OUT/obj/glibc226_shim.o" -L"$SYSROOT/lib" $LINK
$STRIP -o "$PAY/usr/bin/pce-normalize" "$OUT/obj/pce-normalize"

echo "== cd-rip"
$CC $CFLAGS -o "$OUT/obj/cd-rip" "$TOP/src/cd-rip/cd-rip.c" "$OUT/obj/glibc226_shim.o" -L"$SYSROOT/lib" $LINK
$STRIP -o "$PAY/usr/bin/cd-rip" "$OUT/obj/cd-rip"

echo "== RetroArch $RA_TAG + ArchGX patch"
RA=${RA_SRC:-$OUT/RetroArch}
if [ -z "${RA_SRC:-}" ]; then
	rm -rf "$RA"
	git clone -q --depth 1 --branch "$RA_TAG" https://github.com/libretro/RetroArch "$RA"
fi
[ "$(git -C "$RA" rev-parse HEAD)" = "$RA_COMMIT" ] || { echo "error: $RA is not RetroArch $RA_TAG" >&2; exit 1; }
git -C "$RA" checkout -q . && git -C "$RA" clean -fdq
git -C "$RA" apply "$TOP/retroarch/0001-archgx-retron-gx.patch"
sed -e "s#@SYSROOT@#$SYSROOT#g" -e "s#@BUILD@#$OUT/obj#g" -e "s#@ROOTFS@#$ROOTFS#g" \
	"$TOP/retroarch/config.mk.in" > "$RA/config.mk"
cp "$TOP/retroarch/config.h" "$RA/config.h"
make -C "$RA" -j"$(nproc)" > "$OUT/retroarch-build.log" 2>&1 || { tail -20 "$OUT/retroarch-build.log"; exit 1; }
$STRIP -o "$PAY/usr/bin/retroarch" "$RA/retroarch"

echo "== Beetle SuperGrafx core"
SGX=${SGX_SRC:-$OUT/beetle-supergrafx}
if [ -z "${SGX_SRC:-}" ]; then
	rm -rf "$SGX"
	git clone -q https://github.com/libretro/beetle-supergrafx-libretro "$SGX"
	git -C "$SGX" checkout -q "$SGX_COMMIT"
fi
[ "$(git -C "$SGX" rev-parse HEAD)" = "$SGX_COMMIT" ] || { echo "error: $SGX is not Beetle SuperGrafx $SGX_COMMIT" >&2; exit 1; }
F="-O2 $ARCH -D_FILE_OFFSET_BITS=64 -U_TIME_BITS -D_TIME_BITS=32 -U_FORTIFY_SOURCE -fno-stack-protector -include $TOP/src/compat/oldmath.h"
make -C "$SGX" clean > /dev/null 2>&1 || true
CFLAGS="$F" CXXFLAGS="$F" \
LDFLAGS="-L$SYSROOT/lib -Wl,-rpath-link,$ROOTFS/lib:$ROOTFS/usr/lib -static-libgcc -static-libstdc++ $OUT/obj/glibc226_shim_pic.o" \
	make -C "$SGX" -j"$(nproc)" platform=unix HAVE_CDROM=0 CC=$CC CXX=arm-linux-gnueabihf-g++ \
	> "$OUT/supergrafx-build.log" 2>&1 || { tail -20 "$OUT/supergrafx-build.log"; exit 1; }
$STRIP -o "$PAY/usr/lib/libretro/mednafen_supergrafx_libretro.so" "$SGX/mednafen_supergrafx_libretro.so"

echo "== video filters"
for f in archgx_scanlines blargg_ntsc_snes; do
	$CC -O2 $ARCH -fPIC -std=c99 -I"$RA/libretro-common/include" -U_FORTIFY_SOURCE -fno-stack-protector \
		-include "$TOP/src/compat/oldmath.h" -shared -Wl,--version-script="$RA/gfx/video_filters/link.T" \
		-static-libgcc -o "$PAY/usr/lib/retroarch/filters/video/$f.so" "$RA/gfx/video_filters/$f.c" -lm
	$STRIP "$PAY/usr/lib/retroarch/filters/video/$f.so"
done
cp "$RA/gfx/video_filters/ArchGX_Scanlines.filt" "$RA/gfx/video_filters/Blargg_NTSC_SNES_Composite.filt" \
	"$PAY/usr/lib/retroarch/filters/video/"

echo "== system files"
for f in gx-start.sh gx-reset.sh fix_storage.sh; do install -m 755 "$TOP/system/etc/$f" "$PAY/etc/$f"; done
for f in gx-retroarch.cfg gx-retroarch-advanced.cfg gx-resume.cfg; do install -m 644 "$TOP/system/etc/$f" "$PAY/etc/$f"; done
install -m 644 "$TOP/system/share/gamedb.csv" "$TOP/assets/system/background.png" "$TOP/assets/system/logo.png" \
	"$TOP/assets/system/emblem.png" "$PAY/usr/share/archgx/"
echo "ArchGX $(cat "$TOP/VERSION")" > "$PAY/etc/archgx-release"

echo "== checks"
bad=0
for b in "$PAY"/usr/bin/* "$PAY"/usr/lib/retroarch/filters/video/*.so "$PAY"/usr/lib/libretro/*.so; do
	v=$(arm-linux-gnueabihf-objdump -T "$b" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)
	[ -z "$v" ] && continue
	if [ "$(printf '%s\nGLIBC_2.26\n' "$v" | sort -V | tail -1)" != "GLIBC_2.26" ]; then
		echo "error: $b needs $v but the console has glibc 2.26"; bad=1
	fi
done
[ $bad -eq 0 ]
( cd "$PAY" && find . -type f | sort | xargs sha256sum ) > "$OUT/payload.sha256"
echo "payload ready: $PAY"
