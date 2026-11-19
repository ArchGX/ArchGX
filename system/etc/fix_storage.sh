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
#        fix_storage.sh --needed [disk]   exit 0 if the card needs preparing
# STATUS=<file>: progress for the "Preparing your SD card" screen (gx-start.sh).
# Every step is also logged to /userdata/fix_storage.log (ext2), which survives
# even if formatting the storage partition fails.

NEEDED=""; [ "$1" = "--needed" ] && { NEEDED=1; shift; }
DISK=${1:-/dev/mmcblk0}
PART=${2:-${DISK}p6}
PARTED=/usr/sbin/parted
LOG=/tmp/fix_storage.log

PLOG=/userdata/fix_storage.log
log(){
	echo "fix_storage: $*" >> "$LOG"
	echo "$(cut -d' ' -f1 /proc/uptime)s $*" >> "$PLOG" 2>/dev/null; sync
	[ -w /dev/console ] && echo "fix_storage: $*" > /dev/console
}
# screen progress: message, subtitle, state ("" or error)
st(){ [ -n "$STATUS" ] && printf '%s\n%s\n-\n%s\n' "$1" "$2" "$3" > "$STATUS.new" && mv "$STATUS.new" "$STATUS"; }
# run a command for at most $1 seconds (a disk tool waiting for an answer
# must never freeze the first start)
tmo(){
	t=$1; shift
	"$@" & p=$!; n=0
	while kill -0 $p 2>/dev/null; do
		[ $n -ge $((t * 10)) ] && { kill -9 $p 2>/dev/null; log "timeout after ${t}s: $*"; return 124; }
		sleep 0.1; n=$((n + 1))
	done
	wait $p
}

p6_mib(){
	# size of partition 6 in MiB (integer), empty if it does not exist
	$PARTED -s "$DISK" unit MiB print 2>/dev/null | \
		awk '$1 == "6" { s = $4; sub(/MiB/, "", s); printf "%d\n", s }'
}

if [ -n "$NEEDED" ]; then
	size=$(p6_mib)
	[ -z "$size" ] || [ "$size" -le 64 ]
	exit
fi

# one run at a time (stock resize hook + library safety net)
mkdir /tmp/fix_storage.lock 2>/dev/null || exit 0
trap 'rmdir /tmp/fix_storage.lock 2>/dev/null' EXIT

size=$(p6_mib)
log "start: partition 6 size=${size:-missing} MiB"
if [ -n "$size" ] && [ "$size" -gt 64 ]; then
	log "storage partition already large, nothing to do"
	exit 0
fi

# 1. let the GPT cover the whole card. parted may ask "Fix/Ignore?" (table
#    smaller than the card) or "OK/Cancel?" (damaged backup table): both are
#    answered, and it is stopped if it still waits.
st "Preparing your SD card (1 of 3)" "Checking the partition table"
log "step 1: partition table"
printf 'Fix\nOK\nFix\nOK\nFix\nOK\n' > /tmp/fix_storage.answers
tmo 60 sh -c "$PARTED ---pretend-input-tty '$DISK' print < /tmp/fix_storage.answers" >> "$LOG" 2>&1

# 2. grow or recreate partition 6
st "Preparing your SD card (2 of 3)" "Making room for your games"
log "step 2: grow partition 6"
umount /userdata/rom 2>/dev/null
if [ -n "$size" ]; then
	tmo 60 $PARTED -s "$DISK" resizepart 6 100% >> "$LOG" 2>&1
else
	tmo 60 $PARTED -s "$DISK" mkpart gb_rom fat32 80MiB 100% >> "$LOG" 2>&1
fi
new=$(p6_mib)
log "after resize: partition 6 size=${new:-missing} MiB"
if [ -z "$new" ] || [ "$new" -le 64 ]; then
	log "resize failed, leaving the card as it is"
	st "Couldn't prepare the SD card" "Enlarging failed: please report it on GitHub" error
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
st "Preparing your SD card (3 of 3)" "Formatting: can take minutes. Don't switch off"
log "step 3: format $(( new / 1024 )) GiB"
mkfs.vfat -F 32 -n RETRONGX "$PART" >> "$LOG" 2>&1 || { log "mkfs failed"; st "Couldn't prepare the SD card" "Formatting failed: please report it on GitHub" error; exit 1; }
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
