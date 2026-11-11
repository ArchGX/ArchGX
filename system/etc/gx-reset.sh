#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Part of ArchGX (https://github.com/ArchGX/ArchGX)
# gx-reset.sh - console reset button (via input-event-daemon)
# During a game: save and quit, which returns to the picker. Otherwise nothing.
pidof retroarch > /dev/null || exit 0
echo "$(cut -d' ' -f1 /proc/uptime)s reset button: stopping game" >> /userdata/rom/logs/gx-start.log; sync
killall -TERM retroarch
n=0
while pidof retroarch > /dev/null && [ $n -lt 30 ]; do sleep 0.1; n=$((n+1)); done
pidof retroarch > /dev/null && killall -9 retroarch
exit 0
