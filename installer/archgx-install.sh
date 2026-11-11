#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# archgx-install.sh - install ArchGX on a RetroN GX SD card (or a card image).
# Part of ArchGX (https://github.com/ArchGX/ArchGX)
#
# ArchGX ships no Hyperkin or Rockchip files. This script takes the system
# partition ("rootfs") from YOUR card, adds the ArchGX payload, switches off
# Hyperkin's own launcher programs, and writes the result back. The bootloader,
# kernel, settings and game partitions are not touched.
#
# Usage:
#   sudo ./archgx-install.sh install  <card-device|card-image> [options]
#   sudo ./archgx-install.sh restore  <card-device|card-image> --stock <backup>
#   sudo ./archgx-install.sh check    <card-device|card-image>
# Options:
#   --stock <file>       stock rootfs backup to start from (needed when the card
#                        already runs ArchGX, e.g. to upgrade)
#   --backup-dir <dir>   where to save the stock rootfs backup (default: .)
#   --payload <dir>      payload directory (default: ../payload next to this script)
#   -y                   do not ask for confirmation
set -euo pipefail

VERSION_FILE_IN_PAYLOAD=etc/archgx-release
# SHA-256 of the stock system partition (RubyMain-20250709, 98304 sectors)
STOCK_ROOTFS_SHA256=ef6184b3968d185ce00688cbaa405555b5983327768a641e60fa41fd8ea99344
# Hyperkin programs that ArchGX replaces: made non-executable, never deleted
DISABLE=(
	etc/check_dumper.sh etc/firmware_update.sh etc/partition_resize.sh etc/reset-btn.sh
	etc/start_tgdumper.sh etc/usbmount/mount.d/01_restart_retroarch
	etc/usbmount/mount.d/01_try_launch_retroarch etc/usbmount/umount.d/01_kill_retroarch
	usr/bin/bte_fw_update_andrew usr/bin/check_firmware usr/bin/menu usr/bin/tgdumper-loading
)

HERE=$(cd "$(dirname "$0")" && pwd)
PAYLOAD=$HERE/../payload
BACKUP_DIR=.
STOCK=""
YES=""
die() { echo "error: $*" >&2; exit 1; }
say() { echo "== $*"; }

[ $# -ge 2 ] || { sed -n '9,20p' "$0"; exit 1; }
CMD=$1; TARGET=$2; shift 2
while [ $# -gt 0 ]; do
	case "$1" in
		--stock) STOCK=$2; shift 2 ;;
		--backup-dir) BACKUP_DIR=$2; shift 2 ;;
		--payload) PAYLOAD=$2; shift 2 ;;
		-y) YES=1; shift ;;
		*) die "unknown option $1" ;;
	esac
done

[ "$(id -u)" -eq 0 ] || die "run as root (sudo): file ownership inside the system partition must be kept"
for t in unsquashfs mksquashfs sha256sum dd; do command -v $t >/dev/null || die "$t is missing (install squashfs-tools and coreutils)"; done
[ -e "$TARGET" ] || die "$TARGET does not exist"

# --- locate the rootfs partition (GPT name "rootfs") -------------------------
read START SIZE < <(
	if command -v sfdisk >/dev/null; then
		sfdisk -d "$TARGET" 2>/dev/null | awk -F'[=,]' '/name="rootfs"/ { gsub(/ /, ""); print $2, $4 }' | head -1
	elif command -v sgdisk >/dev/null; then
		sgdisk -p "$TARGET" 2>/dev/null | awk '$NF == "rootfs" { print $2, $3 - $2 + 1 }' | head -1
	else
		echo ""
	fi
) || true
[ -n "${START:-}" ] && [ -n "${SIZE:-}" ] || die "no partition named \"rootfs\" on $TARGET (need sfdisk or sgdisk; is this a RetroN GX card?)"
say "rootfs partition: sector $START, $SIZE sectors ($((SIZE / 2048)) MiB)"

WORK=$(mktemp -d /tmp/archgx.XXXXXX)
trap 'rm -rf "$WORK"' EXIT
dd if="$TARGET" of="$WORK/card-rootfs.img" bs=512 skip="$START" count="$SIZE" status=none
CARD_SHA=$(sha256sum "$WORK/card-rootfs.img" | cut -d' ' -f1)
CARD_STATE=other
if [ "$CARD_SHA" = "$STOCK_ROOTFS_SHA256" ]; then
	CARD_STATE=stock
elif unsquashfs -l "$WORK/card-rootfs.img" 2>/dev/null | grep "squashfs-root/$VERSION_FILE_IN_PAYLOAD\$" >/dev/null; then
	CARD_STATE=archgx
fi

if [ "$CMD" = check ]; then
	case $CARD_STATE in
		stock)  echo "The card has the stock RetroN GX system (supported)." ;;
		archgx) echo "The card runs ArchGX: $(unsquashfs -cat "$WORK/card-rootfs.img" $VERSION_FILE_IN_PAYLOAD 2>/dev/null)" ;;
		*)      echo "Unknown system partition (sha256 $CARD_SHA): not supported." ;;
	esac
	exit 0
fi

confirm() {
	[ -n "$YES" ] && return 0
	if [ -b "$TARGET" ]; then
		echo "Target is a DEVICE: $TARGET ($(lsblk -dno SIZE,MODEL "$TARGET" 2>/dev/null || echo '?'))"
		grep -q "^$TARGET" /proc/mounts && die "$TARGET (or a partition on it) is mounted: unmount it first"
	fi
	read -r -p "$1 [type yes]: " a
	[ "$a" = yes ] || die "cancelled"
}

write_rootfs() {   # <squashfs image>
	local n=$(( ($(stat -c %s "$1") + 511) / 512 ))
	[ "$n" -le "$SIZE" ] || die "new system image ($n sectors) is larger than the partition ($SIZE)"
	dd if="$1" of="$TARGET" bs=512 seek="$START" conv=notrunc,fsync status=none
	sync
	dd if="$TARGET" of="$WORK/verify.img" bs=512 skip="$START" count="$n" status=none
	cmp -s -n "$(stat -c %s "$1")" "$WORK/verify.img" "$1" \
		|| die "verification failed after writing: restore with the stock backup"
	say "written and verified"
}

# --- restore ---------------------------------------------------------------
if [ "$CMD" = restore ]; then
	[ -n "$STOCK" ] || die "restore needs --stock <stock rootfs backup>"
	[ "$(sha256sum "$STOCK" | cut -d' ' -f1)" = "$STOCK_ROOTFS_SHA256" ] || die "$STOCK is not the stock rootfs"
	confirm "Write the stock system partition back to $TARGET?"
	write_rootfs "$STOCK"
	say "stock system restored. Your games and saves were not touched."
	exit 0
fi
[ "$CMD" = install ] || die "unknown command $CMD (install, restore or check)"

# --- install ---------------------------------------------------------------
[ -f "$PAYLOAD/$VERSION_FILE_IN_PAYLOAD" ] || die "payload not found at $PAYLOAD"
if [ -f "$PAYLOAD/../payload.sha256" ]; then
	( cd "$PAYLOAD" && sha256sum --quiet -c ../payload.sha256 ) || die "payload is damaged (checksum mismatch): download it again"
fi
NEW_VERSION=$(cat "$PAYLOAD/$VERSION_FILE_IN_PAYLOAD")

case $CARD_STATE in
	stock)
		BASE=$WORK/card-rootfs.img
		mkdir -p "$BACKUP_DIR"
		B=$BACKUP_DIR/archgx-stock-rootfs.img
		if [ -f "$B" ] && [ "$(sha256sum "$B" | cut -d' ' -f1)" = "$STOCK_ROOTFS_SHA256" ]; then
			say "stock backup already present: $B"
		else
			cp "$BASE" "$B"; sha256sum "$B" > "$B.sha256"
			say "stock system partition backed up to $B (keep it: it undoes the install)"
		fi ;;
	archgx)
		[ -n "$STOCK" ] || die "this card already runs ArchGX: give the stock backup with --stock archgx-stock-rootfs.img"
		[ "$(sha256sum "$STOCK" | cut -d' ' -f1)" = "$STOCK_ROOTFS_SHA256" ] || die "$STOCK is not the stock rootfs"
		BASE=$STOCK ;;
	*)
		die "unknown system partition on $TARGET (sha256 $CARD_SHA). ArchGX only installs over the stock RubyMain-20250709 system." ;;
esac

say "building $NEW_VERSION"
unsquashfs -q -d "$WORK/root" "$BASE" >/dev/null
R=$WORK/root

# our files (owned by root, existing directories keep their metadata)
( cd "$PAYLOAD" && tar cf - . ) | ( cd "$R" && tar xf - --no-same-owner --no-overwrite-dir )

# start ArchGX instead of Hyperkin's launcher
sed -i -e '/^[[:space:]]*source \/etc\/check_dumper\.sh[[:space:]]*$/d' \
       -e 's#/usr/bin/check_firmware > /dev/null &#/etc/gx-start.sh > /dev/null 2>\&1 \&   \# ArchGX#' "$R/etc/init.d/rcS"
grep -q '/etc/gx-start.sh' "$R/etc/init.d/rcS" || die "could not edit /etc/init.d/rcS"
grep -q 'check_dumper\|check_firmware' "$R/etc/init.d/rcS" && die "/etc/init.d/rcS still starts Hyperkin programs"
# reset button -> ArchGX; no automatic USB mounts (ArchGX mounts the reader read-only)
sed -i 's#^F3 = /etc/reset-btn.sh#F3 = /etc/gx-reset.sh#' "$R/etc/input-event-daemon.conf"
grep -q '^F3 = /etc/gx-reset.sh' "$R/etc/input-event-daemon.conf" || die "could not edit input-event-daemon.conf"
sed -i 's#^ENABLED=1#ENABLED=0  \# ArchGX mounts the HuCard reader itself#' "$R/etc/usbmount/usbmount.conf"
grep -q '^ENABLED=0' "$R/etc/usbmount/usbmount.conf" || die "could not edit usbmount.conf"
for f in "${DISABLE[@]}"; do [ -e "$R/$f" ] && chmod 0 "$R/$f"; done

mksquashfs "$R" "$WORK/new-rootfs.img" -comp gzip -b 131072 -noappend -quiet >/dev/null
say "new system partition: $(( $(stat -c %s "$WORK/new-rootfs.img") / 1048576 )) MiB of $((SIZE / 2048)) MiB"

confirm "Install $NEW_VERSION on $TARGET?"
write_rootfs "$WORK/new-rootfs.img"
say "$NEW_VERSION installed. Put the card in the console and switch it on."
