#!/bin/bash
# dosroom.sh DISPLAYNUM GAMEDIR OUTPREFIX [VIDEOMODE]
# Boot SPIDEY.EXE from GAMEDIR headless (Xvfb :DISPLAYNUM + dosbox-x), pick
# the video mode (1=VGA 2=EGA 3=Tandy4 4=CGA 5=Tandy16), skip the four story
# pages, start a new game, and capture the window 0.5 s, 1.5 s and 3.5 s
# after the room appears (OUTPREFIX-1/2/3.png). Keys are *held* for 300 ms:
# the game polls the keyboard and misses instantaneous xdotool presses.
# Everything is hard-killed at the end; a watchdog caps the run at 60 s.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
DN=$1; GAME=$2; OUT=$3; MODE=${4:-1}
export DISPLAY=:$DN
Xvfb :$DN -screen 0 800x600x24 -nolisten tcp >/dev/null 2>&1 &
XV=$!
sleep 1
mkdir -p "$GAME/capture"
sed "s#GAMEDIR#$GAME#g" "$HERE/dosbox.conf" > "$GAME/run.conf"
dosbox-x -conf "$GAME/run.conf" -nopromptfolder -nogui >"$GAME/dosbox.log" 2>&1 &
DB=$!
( sleep 60; kill -9 $DB $XV 2>/dev/null ) &
WD=$!
hold() { xdotool keydown "$1"; sleep ${2:-0.25}; xdotool keyup "$1"; }
sleep 4
xdotool type --delay 80 "$MODE"; sleep 1.5
for i in 1 2 3 4; do xdotool key Return; sleep 1; done
sleep 1.5
hold 1 0.3; sleep 0.5
import -display :$DN -window root "$OUT-1.png"
sleep 1
import -display :$DN -window root "$OUT-2.png"
sleep 2
import -display :$DN -window root "$OUT-3.png"
kill $DB 2>/dev/null; sleep 0.3; kill -9 $DB 2>/dev/null
kill $XV 2>/dev/null; kill $WD 2>/dev/null
wait 2>/dev/null
echo "captured $OUT"
