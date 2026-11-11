#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# gx-start.sh - ArchGX startup and game launcher for the RetroN GX
# Part of ArchGX (https://github.com/ArchGX/ArchGX)
#
# Started by rcS once Weston is up. Replaces Hyperkin's check_firmware,
# tgdumper-loading, menu and shell scripts. Uses ArchGX's own tools
# (pce-launcher, pce-normalize, fix_storage.sh, ArchGX's RetroArch build)
# plus the console's existing Beetle PCE Fast core.
#
#   cart in the slot  -> dump is cleaned, identified (gamedb), filed in games/, played
#   no cart           -> game picker; the chosen game is played
#   game ends         -> back to the picker
#   cart pulled       -> a running cart game saves and quits
#
# Logs (FAT partition): logs/gx-start.log, logs/retroarch.log (last game)
#
# Card layout (FAT partition, /userdata/rom):
#   games/              ROMs. A file name never changes once created.
#   saves/              battery saves (.srm) and save states, named after the ROM file
#   titles.txt          "file<TAB>title": names shown in the picker (Rename edits this)
#   archgx-settings.cfg Options menu choices
#   recent.txt          recently played games
#   screenshots/        in-game screenshots
#   gamedb-local.csv    your own game entries (same columns as the built-in gamedb.csv:
#                       crc32,header,title,region,notes - opens in any spreadsheet);
#                       naming an unknown HuCard in the picker adds it here

GAMES=/userdata/rom/games
LOGD=/userdata/rom/logs
LOG=$LOGD/gx-start.log
TMP=/tmp/gx
RA_FIFO=/tmp/gx/ra.cmd               # RetroArch stdin commands (in-game menu)
STATES_TMP=/tmp/gx/states            # RetroArch's working save state (slot 0) lives in RAM
GX_SET_CFG=/tmp/gx/settings-ra.cfg   # RetroArch settings generated from archgx-settings.cfg
FILTER_DIR=/usr/lib/retroarch/filters/video
CART_MNT=/media/usb1                 # pce-launcher watches this path
CART_FILE=$CART_MNT/Test_pcengine.pce
CORE=/usr/lib/libretro/beetlepcefast_libretro.so
RA_CFG=/userdata/retroarch/retroarch.cfg
CORE_OPTS=/userdata/retroarch/retroarch-core-options.cfg
GX_RA_CFG=/etc/gx-retroarch.cfg      # our settings on top of the stock config (saves folder, simple menu)
GX_ADV_CFG=/etc/gx-retroarch-advanced.cfg   # "RetroArch (advanced)": the full menu
GX_RESUME_CFG=/etc/gx-resume.cfg     # Resume: load <game>.state.auto at start
RA_DIR=/userdata/retroarch           # rcS copies RA_STORE here (RAM) at every boot
RA_STORE=/userdata/retroarch_org     # persistent copy on the userdata partition
ROM=/userdata/rom
SAVES=$ROM/saves
TITLES=$ROM/titles.txt
LOCAL_DB=$ROM/gamedb-local.csv
SETTINGS=$ROM/archgx-settings.cfg      # Options menu choices (aspect, filter, volume...)
RECENT=$ROM/recent.txt               # recently played games, newest first (library order)
BASE_DB=/usr/share/archgx/gamedb.csv
VID=2e25 PID=988c                    # BTE "TG Dumper" (cart slot + pad ports)
SYS=${SYS:-/sys}                     # overridable for simulation only
export SDL_AUDIODRIVER=pulseaudio SDL_ASSERT=always_ignore XDG_RUNTIME_DIR=/var/run HOME=/userdata

log() { echo "$(cut -d' ' -f1 /proc/uptime)s $*" >> "$LOG"; sync; }   # sync: survive power-off

# block device (e.g. sda1) of the reader's mass-storage interface, if present
reader_part() {
	for d in "$SYS"/bus/usb/devices/*; do
		[ "$(cat "$d/idVendor" 2>/dev/null)" = $VID ] &&
		[ "$(cat "$d/idProduct" 2>/dev/null)" = $PID ] || continue
		for i in "$d"/"$(basename "$d")":*; do
			[ "$(cat "$i/bInterfaceClass" 2>/dev/null)" = 08 ] || continue
			for b in "$i"/host*/target*/*/block/*; do
				[ -d "$b" ] || continue
				b=$(basename "$b")
				[ -d "$SYS/block/$b/${b}1" ] && echo "${b}1"   # partition not up yet: try again next second
				return
			done
		done
	done
}

# Background: keep the reader mounted read-only while its disk exists.
# If the disk goes away during a cart game, the game saves and quits.
cart_watch() {
	mounted=""; seen=""; empty=0
	while :; do
		p=$(reader_part)
		if [ -n "$p" ] && [ -z "$mounted" ] && [ -b "/dev/$p" ]; then
			mkdir -p $CART_MNT
			if mount -t vfat -o ro,noexec,nodev,nosuid "/dev/$p" $CART_MNT 2>>"$LOG"; then
				mounted=$p; empty=0
				[ -z "$seen" ] && { seen=1; log "reader /dev/$p mounted read-only"; }
			fi
		elif [ -z "$p" ] && [ -n "$mounted" ]; then
			umount -l $CART_MNT 2>/dev/null; mounted=""; seen=""; log "reader removed"
			[ -f $TMP/cart_game ] && { log "cart pulled: stopping game"; stop_game; }
		elif [ -n "$mounted" ] && [ ! -f "$CART_FILE" ]; then
			empty=$((empty+1))   # mounted, no dump yet: remount every 3 s to see new files
			[ $empty -ge 3 ] && { umount $CART_MNT 2>/dev/null; mounted=""; }
		fi
		sleep 1
	done
}

# save and quit RetroArch (SIGTERM: upstream RetroArch's clean-quit signal)
stop_game() {
	killall -TERM retroarch 2>/dev/null || return
	n=0
	while pidof retroarch > /dev/null && [ $n -lt 30 ]; do sleep 0.1; n=$((n+1)); done
	pidof retroarch > /dev/null && { log "retroarch did not quit, killing"; killall -9 retroarch; }
}

# wait (up to $1 s) for a complete dump file on the reader
cart_ready() {
	t=0; last=-1
	while [ $t -le "$1" ]; do
		s=$(stat -c %s "$CART_FILE" 2>/dev/null || echo 0)
		[ "$s" -gt 0 ] && [ "$s" = "$last" ] && return 0
		last=$s; sleep 1; t=$((t+1))
	done
	return 1
}

# Default: RUN+SELECT opens RetroArch's menu, so the PC Engine soft reset
# (also RUN+SELECT) is turned off. RetroArch rewrites this file on exit,
# so it is set before every launch.
core_opt() {
	if grep -q "^$1 = " "$CORE_OPTS" 2>/dev/null; then
		sed -i "s/^$1 = .*/$1 = \"$2\"/" "$CORE_OPTS"
	else
		echo "$1 = \"$2\"" >> "$CORE_OPTS"
	fi
}
set_core_defaults() { core_opt pce_disable_softreset enabled; }

# CRC32 of a file (gzip stores it in its trailer) and the raw header bytes at 0x200
crc32_of() { gzip -c "$1" | tail -c 8 | head -c 4 | hexdump -e '1/4 "%08x"'; }
header_of() { xxd -p -s 512 -l 8 "$1" | tr -d ' \n'; }

# Title for a CRC32 (exact match) or, failing that, the raw header signature
# (used only when it points to a single title: some headers are shared).
# The local database wins over the built-in one, and within it the newest
# line wins, so naming a game again on the console corrects it.
# Both files are CSV as saved by a spreadsheet: quoted fields, CRLF, a UTF-8
# BOM and ";" separators (some Excel locales) are all accepted; codes may be
# written with or without "0x".
db_title() {
	h=$2
	case "$h" in *[!0]*) ;; *) h="" ;; esac              # blank headers (all 00 / all ff) prove nothing
	case "$h" in *[!f]*) ;; *) h="" ;; esac
	set -- "$1" "$h" "$LOCAL_DB" "$BASE_DB"
	[ -f "$LOCAL_DB" ] || set -- "$1" "$2" "$BASE_DB"      # awk stops at a missing file
	awk -v c="$1" -v h="$2" -v L="$LOCAL_DB" '
		function csv(s, f, sep,   n, i, ch, q, v) {
			n = 0; v = ""; q = 0
			for (i = 1; i <= length(s); i++) {
				ch = substr(s, i, 1)
				if (q) { if (ch == "\"") { if (substr(s, i + 1, 1) == "\"") { v = v ch; i++ } else q = 0 } else v = v ch }
				else if (ch == "\"") q = 1
				else if (ch == sep) { f[++n] = v; v = "" }
				else v = v ch
			}
			f[++n] = v; return n
		}
		function code(v, len) {
			v = tolower(v); gsub(/[ \t]/, "", v); sub(/^0x/, "", v)
			if (v !~ /^[0-9a-f]+$/) return ""
			while (length(v) < len) v = "0" v                  # a spreadsheet may drop leading zeros
			return v
		}
		{ sub(/\r$/, "") }
		FNR == 1 {
			if (substr($0, 1, 3) == "\357\273\277") $0 = substr($0, 4)
			sep = (index($0, ";") && !index($0, ",")) ? ";" : ","
			if (tolower($0) ~ /^"?crc32/) next
		}
		{
			if (csv($0, f, sep) < 3) next
			cc = code(f[1], 8); hh = code(f[2], 16); t = f[3]
			if (cc == "" || t == "") next
			if (FILENAME == L) { if (cc == c) lc = t; if (h != "" && hh == h) lh = t; next }
			if (cc == c && bc == "") bc = t
			if (h != "" && hh == h && !(t in seen)) { seen[t] = 1; nh++; bh = t }
		}
		END { if (lc != "") print lc; else if (bc != "") print bc; else if (lh != "") print lh; else if (nh == 1) print bh }
	' "$3" ${4:+"$4"} 2>/dev/null
}

# title shown for a library file: titles.txt (last entry wins), else the file name
title_of() {
	f=$(basename "$1")
	t=$(awk -F '\t' -v f="$f" '$1 == f { t = $2 } END { print t }' "$TITLES" 2>/dev/null)
	[ -n "$t" ] && echo "$t" || echo "${f%.*}"
}

set_title() { printf '%s\t%s\n' "$(basename "$1")" "$2" >> "$TITLES"; }

# FAT-safe file name from a title
safe_name() { echo "$1" | sed 's#[\\/:*?"<>|]#-#g; s/[. ]*$//'; }

# ------------------------------------------------------- progress screen
# pce-launcher --status shows $TMP/status: message, subtitle, progress
# ("0".."100" measured, "-" activity only) and state ("error" / "done").
status() { printf '%s\n%s\n%s\n%s\n' "$1" "$2" "$3" "$4" > $TMP/status.new && mv $TMP/status.new $TMP/status; }
status_show() {
	status "$@"
	if [ -z "$STATUS_PID" ] || ! kill -0 "$STATUS_PID" 2>/dev/null; then
		/usr/bin/pce-launcher --status $TMP/status >> $LOGD/launcher.log 2>&1 &
		STATUS_PID=$!
	fi
}
status_close() {
	[ -n "$STATUS_PID" ] || return
	status "" "" "" done
	n=0; while kill -0 "$STATUS_PID" 2>/dev/null && [ $n -lt 20 ]; do sleep 0.1; n=$((n+1)); done
	kill "$STATUS_PID" 2>/dev/null; STATUS_PID=""; rm -f $TMP/status
}
show_error() { log "error: $1"; status_show "$1" "$2" "" error; sleep 3; status_close; }

# Keep RetroArch settings changed in its menu: copy them back to the
# persistent store after every session (a stock copy is kept once).
# Recovery from a PC: create an empty file "reset-retroarch-settings" on the card.
persist_ra() {
	for f in retroarch.cfg retroarch-core-options.cfg; do
		[ -f "$RA_DIR/$f" ] || continue
		[ -f "$RA_STORE/$f.stock" ] || cp "$RA_STORE/$f" "$RA_STORE/$f.stock" 2>/dev/null
		if ! cmp -s "$RA_DIR/$f" "$RA_STORE/$f"; then
			cp "$RA_DIR/$f" "$RA_STORE/$f.new" && mv "$RA_STORE/$f.new" "$RA_STORE/$f"
		fi
	done
	[ -d "$RA_DIR/config" ] && cp -r "$RA_DIR/config" "$RA_STORE/" 2>/dev/null   # overrides and remaps
	sync
}

# Options-menu settings -> RetroArch config for this launch (+ /tmp/gx/ra-set for
# the turbo buttons). The front switch wins if it moved since the last change.
setting() { v=$(sed -n "s/^$1=//p" "$SETTINGS" 2>/dev/null | tr -d '\r' | tail -1); echo "${v:-$2}"; }
apply_settings() {
	aspect=$(setting aspect ""); sw=$(setting switch -1)
	now=$(cat /sys/devices/platform/adc-vol/ratio 2>/dev/null | cut -c1)
	if [ -n "$now" ] && [ "$now" != "$sw" ]; then
		aspect=$now
		if [ -f "$SETTINGS" ]; then
			sed -i -e "s/^aspect=.*/aspect=$now/" -e "s/^switch=.*/switch=$now/" "$SETTINGS"
			grep -q '^switch=' "$SETTINGS" || echo "switch=$now" >> "$SETTINGS"
		fi
	fi
	[ -z "$aspect" ] && aspect=${now:-0}
	case $(setting filter off) in
		scanlines) filt=$FILTER_DIR/ArchGX_Scanlines.filt ;;
		composite) filt=$FILTER_DIR/Blargg_NTSC_SNES_Composite.filt ;;
		*)         filt="" ;;
	esac
	smooth=$(setting smooth 0); vol=$(setting volume 10)
	if [ "$vol" -le 0 ] 2>/dev/null; then db=-80; else db=$(( (vol - 10) * 3 )); fi
	[ "$smooth" = 1 ] && sm=true || sm=false
	cat > $GX_SET_CFG <<-EOC
		aspect_ratio_index = "$aspect"
		video_filter_dir = "$FILTER_DIR"
		video_filter = "$filt"
		video_smooth = "$sm"
		audio_volume = "$db"
	EOC
	[ "$(setting flicker 0)" = 1 ] && spr=enabled || spr=disabled
	core_opt pce_nospritelimit "$spr"
	printf 'turbo1=%s\nturbo2=%s\n' "$(setting turbo1 0)" "$(setting turbo2 0)" > $TMP/ra-set
	log "settings: aspect=$aspect filter=${filt##*/} smooth=$smooth volume=$vol flicker=$spr turbo=$(setting turbo1 0)/$(setting turbo2 0)"
}

# move a game to the top of the recently played list (file names, newest first)
mark_recent() {
	b=$(basename "$1")
	{ echo "$b"; grep -v -x -F "$b" "$RECENT" 2>/dev/null | head -19; } > "$RECENT.new" && mv "$RECENT.new" "$RECENT"
}

# The display server (Weston) can die together with RetroArch (seen after many
# video-filter changes). Restart it the way rcS does; reboot if it won't come back.
ensure_display() {
	pidof weston >/dev/null && return 0
	log "display server (weston) not running: restarting it"
	rd=${XDG_RUNTIME_DIR:-/var/run}; export XDG_RUNTIME_DIR=$rd
	rm -f $rd/wayland-0 $rd/wayland-0.lock
	weston --tty=2 --idle-time=0 --log=$LOGD/weston.log &
	n=0; while [ ! -e $rd/wayland-0 ] && [ $n -lt 150 ]; do sleep 0.1; n=$((n+1)); done
	if [ -e $rd/wayland-0 ]; then log "display server restarted"; sleep 1; return 0; fi
	log "display server did not come back: restarting the console"; sync; reboot; exit 0
}

run_game() {
	title=$(title_of "$1")
	base=$(basename "$1" .pce)
	mkdir -p $STATES_TMP; rm -f $STATES_TMP/*
	apply_settings
	cfgs="$GX_RA_CFG|$GX_SET_CFG"
	if [ "$2" = resume ]; then                # newest save state becomes the one loaded at start
		st=$(ls -t "$SAVES/$base".state "$SAVES/$base".state[0-9] 2>/dev/null | head -1)
		if [ -n "$st" ] && cp "$st" "$STATES_TMP/$base.state.auto"; then
			cfgs="$cfgs|$GX_RESUME_CFG"; log "resume from $(basename "$st")"
		fi
	fi
	log "play: $1 ($title)"
	mark_recent "$1"
	set_core_defaults
	echo "$title" > /tmp/SelectGame.txt      # read by the console's Beetle core (per-game input code)
	status_show "Launching $title..." "" "-"
	# command channel for the in-game menu: RetroArch reads its stdin from a FIFO,
	# kept open here (fd 3) so RetroArch never sees end-of-file
	[ -f $LOGD/retroarch.log ] && mv $LOGD/retroarch.log $LOGD/retroarch.log.1 2>/dev/null
	rm -f $RA_FIFO; mkfifo $RA_FIFO; exec 3<>$RA_FIFO
	/usr/bin/retroarch -v -c $RA_CFG --appendconfig "$cfgs" -L $CORE "$1" < $RA_FIFO > $LOGD/retroarch.log 2>&1 &
	ra=$!
	# no measurable load progress: keep the spinner until RetroArch shows the game
	n=0; started=""
	while [ $n -lt 200 ] && kill -0 $ra 2>/dev/null; do
		grep -a -q "SET_GEOMETRY" $LOGD/retroarch.log 2>/dev/null && { started=1; break; }
		sleep 0.1; n=$((n+1))
	done
	status_close
	ig=""
	if [ -n "$started" ]; then                # in-game menu: RUN + SELECT
		/usr/bin/pce-launcher --ingame $RA_FIFO "$SAVES/$base" "$title" >> $LOGD/ingame.log 2>&1 &
		ig=$!
	fi
	wait $ra; rc=$?
	[ -n "$ig" ] && kill $ig 2>/dev/null
	exec 3>&-; rm -f $RA_FIFO
	rm -f $STATES_TMP/*; persist_ra
	log "game ended (retroarch exit $rc)"
	ensure_display
	[ -z "$started" ] && show_error "Could not start $title" "See logs/retroarch.log"
}

# "RetroArch (advanced)" in the picker: the full RetroArch menu, no game loaded
run_retroarch_menu() {
	log "RetroArch (advanced) opened"
	/usr/bin/retroarch -v -c $RA_CFG --appendconfig "$GX_RA_CFG|$GX_ADV_CFG" --menu > $LOGD/retroarch.log 2>&1
	log "RetroArch (advanced) closed (exit $?)"; persist_ra
}

# copy the reader's dump in 32 KB pieces, reporting the real percentage
copy_dump() {
	size=$(stat -c %s "$CART_FILE" 2>/dev/null) || return 1
	[ "$size" -gt 0 ] || return 1
	rm -f "$2"; n=$(( (size + 32767) / 32768 )); i=0
	while [ $i -lt $n ]; do
		dd if="$1" of="$2" bs=32768 skip=$i seek=$i count=1 conv=notrunc 2>/dev/null || return 1
		i=$((i+1)); status "Reading HuCard..." "" $((i * 100 / n)) ""
	done
	[ "$(stat -c %s "$2")" = "$size" ]
}

play_cart() {
	status_show "Reading HuCard..." "" "-"          # the reader's own read: no progress visible
	cart_ready 30 || { show_error "Could not read the HuCard" "Check the card and try again"; return; }
	copy_dump "$CART_FILE" $TMP/raw.pce || { rm -f $TMP/raw.pce; show_error "Could not read the HuCard" "Check the card and try again"; return; }
	cp "${CART_FILE%.pce}.crc" $LOGD/last-cart.crc 2>/dev/null   # reader's own .crc file, kept for study
	status "Identifying game..." "" "-" ""
	pce-normalize $TMP/raw.pce $TMP/cart.pce >> "$LOG" 2>&1 || cp $TMP/raw.pce $TMP/cart.pce || { show_error "Could not prepare the game"; return; }
	crc=$(crc32_of $TMP/cart.pce); hdr=$(header_of $TMP/raw.pce)
	title=$(db_title "$crc" "$hdr")
	if [ -n "$title" ]; then log "cart: identified as \"$title\" (crc32 $crc, header $hdr)"
	else log "cart: not in the game database (crc32 $crc, header $hdr)"; fi
	status "Preparing game..." "${title:-Unknown HuCard ($crc)}" "-" ""
	s=$(stat -c %s $TMP/cart.pce); game=""
	for f in "$GAMES"/*.pce; do             # same game already in the library?
		[ -f "$f" ] && [ "$(stat -c %s "$f")" = "$s" ] && cmp -s "$f" $TMP/cart.pce && { game=$f; break; }
	done
	if [ -z "$game" ]; then
		if [ -n "$title" ]; then
			game="$GAMES/$(safe_name "$title").pce"
			[ -e "$game" ] && game="$GAMES/$(safe_name "$title") [$crc].pce"
		else
			game=$GAMES/cart-$crc.pce        # the picker offers to name it (Rename)
		fi
		cp $TMP/cart.pce "$game.part" && mv "$game.part" "$game" && sync ||
			{ rm -f "$game.part"; show_error "Could not save the game" "Is the SD card full?"; return; }
		log "cart: new dump saved as $(basename "$game")"
	fi
	rm -f $TMP/raw.pce
	# show the database title unless the user already named this file
	if [ -n "$title" ] && ! awk -F '\t' -v f="$(basename "$game")" '$1 == f { x = 1 } END { exit !x }' "$TITLES" 2>/dev/null; then
		set_title "$game" "$title"
	fi
	touch $TMP/cart_game; run_game "$game"; rm -f $TMP/cart_game
}

# ---------------------------------------------------------------- main
mkdir -p $TMP
[ -x /etc/fix_storage.sh ] && /etc/fix_storage.sh      # our storage-partition fix
mkdir -p "$GAMES" "$LOGD" "$SAVES"
[ -f "$LOG" ] && [ "$(stat -c %s "$LOG")" -gt 262144 ] && mv "$LOG" "$LOG.old"
log "==== ArchGX GP1 ($(date))"

# Recovery: "reset-retroarch-settings" on the card restores the stock RetroArch settings
if [ -f "$ROM/reset-retroarch-settings" ]; then
	r=""
	for f in retroarch.cfg retroarch-core-options.cfg; do
		[ -f "$RA_STORE/$f.stock" ] && cp "$RA_STORE/$f.stock" "$RA_STORE/$f" && cp "$RA_STORE/$f.stock" "$RA_DIR/$f" && r=1
	done
	rm -f "$ROM/reset-retroarch-settings"; sync
	[ -n "$r" ] && log "RetroArch settings restored to stock" || log "reset requested: settings were never changed"
fi

# One-time imports (nothing is deleted; existing files are never overwritten):
#  1. saves kept next to the ROMs in games/ -> saves/
#  2. the data/ folder of a card used with the stock firmware or older patches
#     (HuCard dumps + their saves) -> games/ + saves/
if [ ! -f "$SAVES/.imported" ]; then
	n=0
	for f in "$GAMES"/*.srm "$GAMES"/*.state*; do
		[ -f "$f" ] && [ ! -e "$SAVES/$(basename "$f")" ] && cp "$f" "$SAVES/" && n=$((n+1))
	done
	for f in "$ROM"/data/*.pce; do
		[ -f "$f" ] || continue
		b=$(basename "$f" .pce)
		case "$b" in *-fixed) continue ;; esac                    # handled with its raw dump
		src=$f; [ -f "$ROM/data/$b-fixed.pce" ] && src=$ROM/data/$b-fixed.pce
		dst=""
		for g in "$GAMES"/*.pce; do [ -f "$g" ] && cmp -s "$g" "$src" && { dst=$g; break; }; done
		if [ -z "$dst" ]; then
			dst=$GAMES/$b.pce; [ -e "$dst" ] && dst="$GAMES/$b (imported).pce"
			cp "$src" "$dst" && n=$((n+1))
		fi
		for sv in "$ROM"/data/"$b".srm "$ROM"/data/"$b".state*; do
			[ -f "$sv" ] || continue
			t=$SAVES/$(basename "$dst" .pce)${sv#$ROM/data/$b}
			[ -e "$t" ] || { cp "$sv" "$t" && n=$((n+1)); }
		done
	done
	sync; touch "$SAVES/.imported"; log "one-time import: $n files copied into games/ and saves/"
fi

cart_watch &

# cart in at power-on: play it straight away (reader ~2-5 s, dump file up to ~15 s)
t=0
while [ ! -f "$CART_FILE" ] && { [ $t -lt 8 ] || { [ $t -lt 20 ] && [ -n "$(reader_part)" ]; }; }; do
	sleep 1; t=$((t+1))
done
[ -f "$CART_FILE" ] && play_cart
for p in check_firmware tgdumper-loading menu bte_fw_update_andrew; do
	pidof $p > /dev/null && log "WARNING: Hyperkin program $p is running"
done

fails=0
while :; do
	if [ -f "$CART_FILE" ]; then export PCE_LAUNCHER_CART=1; else unset PCE_LAUNCHER_CART; fi
	rm -f $TMP/choice
	/usr/bin/pce-launcher "$GAMES" $TMP/choice >> $LOGD/launcher.log 2>&1
	rc=$?; sync; choice=$(head -1 $TMP/choice 2>/dev/null); how=$(sed -n 2p $TMP/choice 2>/dev/null)
	case "$rc:$choice" in
	0:__CART__|2:*)   play_cart; fails=0 ;;
	0:__RESTART__)    log "restart requested"; sync; reboot; exit 0 ;;
	0:__RETROARCH__)  run_retroarch_menu; fails=0 ;;
	0:?*)             run_game "$choice" "$how"; fails=0 ;;
	*)              fails=$((fails+1)); log "picker exited ($rc)"; ensure_display
	                [ $fails -ge 5 ] && { log "picker keeps failing, waiting 10 s"; sleep 10; fails=0; } ;;
	esac
	sleep 1
done
