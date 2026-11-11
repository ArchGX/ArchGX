#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# make-release.sh - package a built payload into the release archive.
#
# Usage: scripts/make-release.sh [build-dir] [dist-dir]
#   build-dir: output of scripts/build.sh (default: build/)
#   dist-dir:  where the archive goes   (default: dist/)
#
# The archive contains only ArchGX files: the installer, the payload (our
# binaries and scripts), checksums, the licence and the third-party notices.
# No Hyperkin or Rockchip files: the installer works on the user's own card.
set -eu
TOP=$(cd "$(dirname "$0")/.." && pwd)
BUILD=$(cd "${1:-$TOP/build}" && pwd)
mkdir -p "${2:-$TOP/dist}"; DIST=$(cd "${2:-$TOP/dist}" && pwd)
V=$(cat "$TOP/VERSION")
NAME=archgx-$V
STAGE=$DIST/$NAME

[ -f "$BUILD/payload/etc/archgx-release" ] || { echo "error: no payload in $BUILD (run scripts/build.sh)" >&2; exit 1; }
rm -rf "$STAGE"; mkdir -p "$STAGE/installer"
cp -a "$BUILD/payload" "$STAGE/payload"
cp "$BUILD/payload.sha256" "$STAGE/payload.sha256"
install -m 755 "$TOP/installer/archgx-install.sh" "$STAGE/installer/"
cp "$TOP/LICENSE" "$TOP/THIRD_PARTY.md" "$TOP/CHANGELOG.md" "$STAGE/"
cp "$TOP/docs/INSTALL.md" "$STAGE/INSTALL.md"
cp "$TOP/retroarch/0001-archgx-retron-gx.patch" "$STAGE/"
cat > "$STAGE/SOURCE.txt" <<EOF
ArchGX $V - corresponding source

All ArchGX source code, build scripts and the RetroArch patch are published at
  https://github.com/ArchGX/ArchGX   (tag $V)

The RetroArch binary in payload/usr/bin/retroarch and the video filters in
payload/usr/lib/retroarch/filters/video/ are built from:
  RetroArch v1.7.4 (commit 02e23be127db29a72170d3c79bfd642288cd15ea)
  https://github.com/libretro/RetroArch
plus 0001-archgx-retron-gx.patch (included here), with the build recipe in
retroarch/ and scripts/build.sh of the ArchGX repository. RetroArch is GPL-3.0;
see THIRD_PARTY.md.
EOF
( cd "$DIST" && tar --owner=0 --group=0 -czf "$NAME.tar.gz" "$NAME" && sha256sum "$NAME.tar.gz" > "$NAME.tar.gz.sha256" )
echo "release: $DIST/$NAME.tar.gz"
cat "$DIST/$NAME.tar.gz.sha256"
