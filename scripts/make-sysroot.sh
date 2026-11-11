#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# make-sysroot.sh - build the cross-compilation sysroot for ArchGX.
#
# Usage: scripts/make-sysroot.sh <stock-rootfs-dir> <sysroot-out>
#
# <stock-rootfs-dir> is the console's system partition, extracted from YOUR
# OWN card (see docs/BUILDING.md). Its libraries are used only for linking;
# nothing from it ends up in the ArchGX payload.
#
# Headers come from upstream sources:
#   SDL 2.0.12, SDL_image 2.0.5, SDL_ttf 2.0.18   (git tags, libsdl-org)
#   ALSA, PulseAudio and libudev headers          (Ubuntu -dev packages)
# Already downloaded copies can be used instead:
#   SDL_SRC=dir SDL_IMAGE_SRC=dir SDL_TTF_SRC=dir  DEB_INCLUDE=dir (a usr/include tree)
set -eu

[ $# -eq 2 ] || { echo "usage: $0 <stock-rootfs-dir> <sysroot-out>" >&2; exit 1; }
ROOTFS=$(cd "$1" && pwd)
mkdir -p "$2"; OUT=$(cd "$2" && pwd)
WORK=$OUT/src
mkdir -p "$OUT/lib" "$OUT/inc/SDL2" "$OUT/inc/sys" "$WORK"

# --- libraries from the console (link time only) ---------------------------
cp_lib() {   # <glob in rootfs> <name in sysroot>
	f=$(ls $ROOTFS/$1 2>/dev/null | head -1)
	[ -n "$f" ] || { echo "error: $1 not found in $ROOTFS" >&2; exit 1; }
	cp -L "$f" "$OUT/lib/$2"
}
cp_lib "lib/ld-linux-armhf.so.3"        ld-linux-armhf.so.3
cp_lib "lib/libc.so.6"                  libc.so.6
cp_lib "lib/libdl.so.2"                 libdl.so.2
cp_lib "lib/libm.so.6"                  libm.so.6
cp_lib "lib/libpthread.so.0"            libpthread.so.0
cp_lib "lib/librt.so.1"                 librt.so.1
cp_lib "lib/libudev.so.1*"              libudev.so
cp_lib "usr/lib/libSDL2-2.0.so.0*"      libSDL2.so
cp_lib "usr/lib/libSDL2_image-2.0.so.0*" libSDL2_image.so
cp_lib "usr/lib/libSDL2_ttf-2.0.so.0*"  libSDL2_ttf.so
cp_lib "usr/lib/libasound.so.2*"        libasound.so
cp_lib "usr/lib/libpulse.so.0*"         libpulse.so
for l in dl:2 m:6 pthread:0 rt:1; do ln -sf "lib${l%%:*}.so.${l#*:}" "$OUT/lib/lib${l%%:*}.so"; done
NONSHARED=$(arm-linux-gnueabihf-gcc -print-file-name=libc_nonshared.a)
cat > "$OUT/lib/libc.so" <<EOF
OUTPUT_FORMAT(elf32-littlearm)
GROUP ( $OUT/lib/libc.so.6 $NONSHARED AS_NEEDED ( $OUT/lib/ld-linux-armhf.so.3 ) )
EOF

# --- headers -----------------------------------------------------------------
fetch() {   # <var-with-dir> <repo> <tag>
	eval d=\${$1:-}
	if [ -z "$d" ]; then
		d=$WORK/$(basename "$2")
		[ -d "$d" ] || git clone -q --depth 1 --branch "$3" "https://github.com/libsdl-org/$2" "$d"
	fi
	echo "$d"
}
SDL=$(fetch SDL_SRC SDL release-2.0.12)
IMG=$(fetch SDL_IMAGE_SRC SDL_image release-2.0.5)
TTF=$(fetch SDL_TTF_SRC SDL_ttf release-2.0.18)
cp "$SDL"/include/*.h "$OUT/inc/SDL2/"
cp "$IMG/SDL_image.h" "$TTF/SDL_ttf.h" "$OUT/inc/SDL2/"

if [ -z "${DEB_INCLUDE:-}" ]; then
	( cd "$WORK" && apt-get download libasound2-dev libpulse-dev libudev-dev )
	for d in "$WORK"/*.deb; do dpkg-deb -x "$d" "$WORK/deb"; done
	DEB_INCLUDE=$WORK/deb/usr/include
fi
cp -r "$DEB_INCLUDE/alsa" "$DEB_INCLUDE/pulse" "$OUT/inc/"
cp "$DEB_INCLUDE/libudev.h" "$OUT/inc/"
printf '#include <alsa/asoundlib.h>\n' > "$OUT/inc/sys/asoundlib.h"

echo "sysroot ready: $OUT"
