#!/bin/bash
# Headless end-to-end test of `./a.out gui`: runs it on Xvfb :97, drives it
# with xdotool through help, movement, goto room/switch/object, room details,
# the hex map, Ctrl-D display toggles, a mouse click, a Shift+drag paint
# stroke and a hot reload, screenshotting each step into $T, then quits with
# q. ROOMS.SPL is backed up first and restored afterwards (the paint stroke
# must change it). Needs Xvfb, xdotool and ImageMagick's import.
#   ASAN_OPTIONS=detect_leaks=0 ./gui_smoke_test.sh     (leaks: run without)
set -u
T=${T:-/tmp/spidey-gui-smoke}
mkdir -p "$T"
R=$(cd "$(dirname "$0")" && pwd)
cd $R
cp ROOMS.SPL $T/ROOMS.SPL.bak
pkill -f "^Xvfb :97" 2>/dev/null; sleep 0.3
Xvfb :97 -screen 0 1600x1200x24 -nolisten tcp >/dev/null 2>&1 &
XV=$!
sleep 1
export DISPLAY=:97
./a.out gui --data-dir Spider --scale 3 > $T/gui.log 2>&1 &
GP=$!
( sleep 90; kill -9 $GP 2>/dev/null ) &
WD=$!
snap() { import -window root "$T/$1.png"; }
sleep 4; snap s1-start
xdotool key ctrl+shift+slash; sleep 0.5; snap s2-nohelp
xdotool key l l l j j; sleep 0.5; snap s3-moved
xdotool key r; sleep 0.4; snap s4a-gotoroom-list; xdotool type 02; sleep 0.6; snap s4-room2
xdotool key ctrl+r; sleep 0.5; snap s5-roomdetails
xdotool key Escape; sleep 0.3
xdotool key F2; sleep 0.5; snap s6-hexmap
xdotool key F2; xdotool key ctrl+d; sleep 0.5; snap s7-toggledisplay
xdotool key Escape; sleep 0.3
xdotool key s; sleep 0.5; snap s8a-gotoswitch-mode; xdotool type 0; sleep 0.5; snap s8-gotoswitch
xdotool key o; sleep 0.5; snap s8b-gotoobject-mode; xdotool key Escape; sleep 0.3
xdotool key ctrl+shift+slash; sleep 0.5; snap s8c-help-overlay; xdotool key ctrl+shift+slash; sleep 0.3
# window is at the Xvfb origin? find it
eval $(xdotool search --name "Spidey Editor" getwindowgeometry --shell | head -5)
echo "window at $X,$Y size ${WIDTH}x${HEIGHT}"
# picture scale 3: tile = 24 px, picture origin (0,24). left click on tile (10,5)
xdotool mousemove $((X + 10*24 + 12)) $((Y + 24 + 5*24 + 12)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.5; snap s9-click
# drag: room 1, pull the helicopter block (9,15) two tiles right; the cursor must stay put
xdotool key r; sleep 0.2; xdotool type 01; sleep 0.5
xdotool mousemove $((X + 9*24 + 12)) $((Y + 24 + 15*24 + 12)) mousedown 1; sleep 0.1
xdotool mousemove_relative 24 0; sleep 0.1; xdotool mousemove_relative 24 0; sleep 0.2; snap s9b-dragging
xdotool mouseup 1; sleep 0.6; snap s9c-dragged
xdotool key u; sleep 0.5
xdotool key r; sleep 0.2; xdotool type 02; sleep 0.5
# palette right of the picture: (768+8, 16); pick tile 0x12 (row 2, col 2)
xdotool mousemove $((X + 776 + 2*24 + 12)) $((Y + 24 + 2*24 + 12)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.3
xdotool keydown shift; xdotool mousemove $((X + 2*24 + 12)) $((Y + 24 + 3*24 + 12)) mousedown 1; sleep 0.1
for i in 1 2 3 4; do xdotool mousemove_relative 24 0; sleep 0.1; done
xdotool mouseup 1; xdotool keyup shift; sleep 1.0; snap s10-painted
# hot reload: touch a watched source and wait for the rebuild
touch gui_bridge.h; sleep 10; snap s11-reloaded
xdotool key h h; sleep 0.5; snap s12-after-reload-keys
xdotool key q; sleep 1.5
if kill -0 $GP 2>/dev/null; then echo "STILL RUNNING after q"; kill $GP; else echo "exited after q"; fi
wait $GP 2>/dev/null; echo "gui exit=$?"
kill $WD 2>/dev/null; kill $XV 2>/dev/null
if cmp -s ROOMS.SPL $T/ROOMS.SPL.bak; then echo "ROOMS unchanged (paint did not write!)"; else echo "ROOMS changed by paint (expected)"; ./a.out display 1 2>/dev/null | sed 's/\x1b\[[0-9;]*m//g' | sed -n 6,7p; fi
cp $T/ROOMS.SPL.bak ROOMS.SPL && echo restored
tail -8 $T/gui.log
