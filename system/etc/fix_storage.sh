#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Part of ArchGX (https://github.com/ArchGX/ArchGX)
#
# Make the SD card's storage partition (6, "gb_rom", FAT32) fill the card.
# Replaces the stock resize.
#
# The stock image's GPT says the disk is 128 MB. The stock resize ran
# "parted rm 6" then "parted mkpart ... 100%"; parted refused the mkpart
# (outside the 128 MB table) and the storage partition was simply gone,
# so games/saves ended up in a 9 MB folder on the system partition.
#
# This script: 1) moves the backup GPT to the real end of the card,
# 2) grows partition 6 (or recreates it if missing) - never deletes it,
# 3) formats it FAT32 and mounts it, like the stock tool. Files already on a
#    small storage partition are copied to RAM first and restored afterwards.
# It does nothing if partition 6 already exists and is bigger than 64 MiB.
#
# Usage: fix_storage.sh [disk] [partition] [nofs]

DISK=${1:-/dev/mmcblk0}
PART=${2:-${DISK}p6}
PARTED=/usr/sbin/parted
LOG=/tmp/fix_storage.log

log(){
	echo "fix_storage: $*" >> "$LOG"
	[ -w /dev/console ] && echo "fix_storage: $*" > /dev/console
}

p6_mib(){
	# size of partition 6 in MiB (integer), empty if it does not exist
	$PARTED -s "$DISK" unit MiB print 2>/dev/null | \
		awk '$1 == "6" { s = $4; sub(/MiB/, "", s); printf "%d\n", s }'
}

# one run at a time (stock resize hook + library safety net)
mkdir /tmp/fix_storage.lock 2>/dev/null || exit 0
trap 'rmdir /tmp/fix_storage.lock 2>/dev/null' EXIT

size=$(p6_mib)
log "start: partition 6 size=${size:-missing} MiB"
if [ -n "$size" ] && [ "$size" -gt 64 ]; then
	log "storage partition already large, nothing to do"
	exit 0
fi

# 1. let the GPT cover the whole card (answers parted's Fix/Ignore prompt)
printf 'Fix\nFix\n' | $PARTED ---pretend-input-tty "$DISK" print >> "$LOG" 2>&1

# 2. grow or recreate partition 6
umount /userdata/rom 2>/dev/null
if [ -n "$size" ]; then
	$PARTED -s "$DISK" resizepart 6 100% >> "$LOG" 2>&1
else
	$PARTED -s "$DISK" mkpart gb_rom fat32 80MiB 100% >> "$LOG" 2>&1
fi
new=$(p6_mib)
log "after resize: partition 6 size=${new:-missing} MiB"
if [ -z "$new" ] || [ "$new" -le 64 ]; then
	log "resize failed, leaving the card as it is"
	exit 1
fi

[ "$3" = "nofs" ] && exit 0

# 3. format and mount (same options as the stock tool)
partprobe "$DISK" 2>/dev/null
n=0
while [ ! -b "$PART" ] && [ $n -lt 50 ]; do sleep .1; n=$((n+1)); done
if [ ! -b "$PART" ]; then
	log "$PART did not appear"
	exit 1
fi
# keep anything already on the old (small) filesystem
BAK=/tmp/fix_storage.bak
rm -rf "$BAK"; mkdir -p /tmp/fix_storage.old
if mount -t vfat -o ro "$PART" /tmp/fix_storage.old 2>/dev/null; then
	if [ -n "$(ls -A /tmp/fix_storage.old 2>/dev/null)" ]; then
		if cp -a /tmp/fix_storage.old "$BAK" 2>>"$LOG"; then
			log "kept $(du -sk "$BAK" | cut -f1) KiB of existing files in RAM"
		else
			umount /tmp/fix_storage.old
			log "could not copy the existing files: not formatting"
			exit 1
		fi
	fi
	umount /tmp/fix_storage.old
fi
mkfs.vfat -F 32 -n RETRONGX "$PART" >> "$LOG" 2>&1 || { log "mkfs failed"; exit 1; }
mount -t vfat -o rw,noatime,nodiratime,norelatime,shortname=mixed,errors=remount-ro "$PART" /userdata/rom
if [ -d "$BAK" ]; then
	cp -a "$BAK"/. /userdata/rom/ 2>>"$LOG" && log "restored the existing files" || log "WARNING: restoring files failed (copy kept in $BAK)"
	sync
fi
mkdir -p /userdata/rom/data /userdata/rom/logs
cp "$LOG" /userdata/rom/logs/fix_storage.log 2>/dev/null
sync
log "done"
exit 0
