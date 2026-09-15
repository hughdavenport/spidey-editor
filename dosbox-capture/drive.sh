#!/bin/bash
# drive.sh DISPLAYNUM GAMEDIR OUTPREFIX STEP...
# Like dosroom.sh (boot SPIDEY.EXE headless from GAMEDIR on Xvfb :DISPLAYNUM, VGA mode,
# skip the story pages, start a game) but then runs a scripted list of STEPs:
#   hold:KEY:SECS        hold one X key (xdotool name: Right, Up, space, ...) for SECS
#   holdN:K1+K2:SECS     hold several keys together (e.g. Up+Right = jump right)
#   wait:SECS            sleep
#   shot                 capture the window as OUTPREFIX-<n>.png (n counts from 1)
# The game polls the keyboard, so keys must be held, not tapped.  Hard-killed after 110 s.
# Cursor keys move, Up jumps, space fires the web (from the game's own key help).
# Example (walk right for 4 s, jump right, then three shots two seconds apart):
#   drive.sh 91 /tmp/game /tmp/out hold:Right:4 holdN:Up+Right:0.5 shot wait:2 shot wait:2 shot
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
DN=$1; GAME=$2; OUT=$3; shift 3
export DISPLAY=:$DN
Xvfb :$DN -screen 0 800x600x24 -nolisten tcp >/dev/null 2>&1 &
XV=$!
sleep 1
mkdir -p "$GAME/capture"
sed "s#GAMEDIR#$GAME#g" "$HERE/dosbox.conf" > "$GAME/run.conf"
dosbox-x -conf "$GAME/run.conf" -nopromptfolder -nogui >"$GAME/dosbox.log" 2>&1 &
DB=$!
( sleep 110; kill -9 $DB $XV 2>/dev/null ) &
WD=$!
hold() { xdotool keydown "$1"; sleep ${2:-0.25}; xdotool keyup "$1"; }
sleep 4
xdotool type --delay 80 "1"; sleep 1.5
for i in 1 2 3 4; do xdotool key Return; sleep 1; done
sleep 1.5
hold 1 0.3; sleep 0.5
n=0
for step in "$@"; do
  IFS=: read -r op a b <<< "$step"
  case $op in
    hold) hold "$a" "$b" ;;
    holdN) IFS=+ read -ra ks <<< "$a"; for k in "${ks[@]}"; do xdotool keydown "$k"; done; sleep "$b"; for k in "${ks[@]}"; do xdotool keyup "$k"; done ;;
    wait) sleep "$a" ;;
    shot) n=$((n+1)); import -display :$DN -window root "$OUT-$n.png" ;;
    *) echo "drive.sh: unknown step '$step'" >&2 ;;
  esac
done
kill $DB 2>/dev/null; sleep 0.3; kill -9 $DB 2>/dev/null
kill $XV 2>/dev/null; kill $WD 2>/dev/null
wait 2>/dev/null
echo "done $OUT ($n shots)"
