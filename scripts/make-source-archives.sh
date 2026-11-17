#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# make-source-archives.sh - the complete corresponding source for a release.
#
# Usage: RA_SRC=<RetroArch git clone> SGX_SRC=<beetle-supergrafx git clone> \
#        scripts/make-source-archives.sh [dist-dir]
#
# Produces, for attaching to the GitHub release next to the binaries:
#   archgx-<version>-source.tar.gz         ArchGX itself (this repository at HEAD):
#                                          our code, the RetroArch patch, the glibc
#                                          shim and the build scripts
#   retroarch-1.7.4-02e23be-source.tar.gz  RetroArch exactly as checked out by the
#                                          build, before our patch
#   beetle-supergrafx-9c11dcd-source.tar.gz  the SuperGrafx core, built unmodified
# Together with docs/BUILDING.md these are everything needed to rebuild the
# GPL binaries in the release (GPL-3.0 section 6, GPL-2.0 section 3a).
set -eu
TOP=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "${1:-$TOP/dist}"; DIST=$(cd "${1:-$TOP/dist}" && pwd)
V=$(cat "$TOP/VERSION")
RA_COMMIT=02e23be127db29a72170d3c79bfd642288cd15ea
SGX_COMMIT=9c11dcd213a79d57d2c86313971631af801f2cdc
: "${RA_SRC:?set RA_SRC to a RetroArch git clone}" "${SGX_SRC:?set SGX_SRC to a beetle-supergrafx-libretro git clone}"

git -C "$TOP" diff --quiet HEAD || { echo "error: uncommitted changes in ArchGX" >&2; exit 1; }
git -C "$TOP" archive --format=tar --prefix="archgx-$V/" HEAD | gzip -9n > "$DIST/archgx-$V-source.tar.gz"
git -C "$RA_SRC" archive --format=tar --prefix=RetroArch-1.7.4/ "$RA_COMMIT" | gzip -9n > "$DIST/retroarch-1.7.4-02e23be-source.tar.gz"
git -C "$SGX_SRC" archive --format=tar --prefix=beetle-supergrafx-libretro/ "$SGX_COMMIT" | gzip -9n > "$DIST/beetle-supergrafx-9c11dcd-source.tar.gz"
ls -l "$DIST"/*-source.tar.gz
