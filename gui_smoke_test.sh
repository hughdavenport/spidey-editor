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
cp Spider/BLOCKS.EGA $T/BLOCKS.EGA.bak; had_orig=0; [ -e Spider/BLOCKS.EGA.orig ] && had_orig=1
cp Spider/SPIDS1.EGA $T/SPIDS1.EGA.bak; had_sorig=0; [ -e Spider/SPIDS1.EGA.orig ] && had_sorig=1
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
eval $(xdotool search --name "Spidey Editor" getwindowgeometry --shell | head -5)
echo "window at $X,$Y size ${WIDTH}x${HEIGHT}"
xdotool key ctrl+shift+slash; sleep 0.5; snap s2-nohelp
xdotool key l l l j j; sleep 0.5; snap s3-moved
xdotool key r; sleep 0.4; snap s4a-gotoroom-list; xdotool type 02; sleep 0.6; snap s4-room2
xdotool key r; sleep 0.3; xdotool type 03; sleep 0.6; snap s4b-lift
xdotool mousemove $((X + 30)) $((Y + HEIGHT - 60)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.5; snap s4c-lift-oob-click
xdotool key r; sleep 0.3; xdotool type 02; sleep 0.6
xdotool key ctrl+r; sleep 0.5; snap s5-roomdetails
xdotool key Escape; sleep 0.3
xdotool key F2; sleep 0.5; snap s6-hexmap
xdotool key F2; xdotool key ctrl+d; sleep 0.5; snap s7-toggledisplay
xdotool key Escape; sleep 0.3
xdotool key s; sleep 0.5; snap s8a-gotoswitch-mode; xdotool type 0; sleep 0.5; snap s8-gotoswitch
xdotool key o; sleep 0.5; snap s8b-gotoobject-mode; xdotool key Escape; sleep 0.3
xdotool key ctrl+shift+slash; sleep 0.5; snap s8c-help-overlay; xdotool key ctrl+shift+slash; sleep 0.3
# picture scale 3: tile = 24 px, picture origin (0,24). left click on tile (10,5)
xdotool mousemove $((X + 10*24 + 12)) $((Y + 24 + 5*24 + 12)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.5; snap s9-click
# drag: room 1, pull the helicopter block (9,15) two tiles right; the cursor must stay put
xdotool key r; sleep 0.2; xdotool type 01; sleep 0.5
xdotool mousemove $((X + 9*24 + 12)) $((Y + 24 + 15*24 + 12)) mousedown 1; sleep 0.1
xdotool mousemove_relative 24 0; sleep 0.1; xdotool mousemove_relative 24 0; sleep 0.2; snap s9b-dragging
xdotool mouseup 1; sleep 0.6; snap s9c-dragged
xdotool key u; sleep 0.5
xdotool key r; sleep 0.2; xdotool type 02; sleep 0.5
# tile pixel editor (right of the palette): pick colour 2 (red), paint pixel (1,1) of tile 0x12
xdotool mousemove $((X + 776 + 192 + 16 + 2*9 + 4)) $((Y + 24 + 8*9 + 8 + 4)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.2
xdotool mousemove $((X + 776 + 192 + 16 + 1*9 + 4)) $((Y + 24 + 1*9 + 4)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 1.2; snap s12b-pixel-edit
xdotool key u; sleep 0.8; snap s12c-pixel-undo   # u undoes the stroke (most recent edit)
# sprite editing: SPIDS1 button (2nd button under the editor), pick frame 0x18 (row 3, col 0), paint a pixel, snapshot, undo
xdotool mousemove $((X + 984 + 2*116 + 30)) $((Y + 354 + 12)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.4; snap s12d0-spids1-mode
xdotool mousemove $((X + 776 + 12)) $((Y + 24 + 3*21 + 10)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.4
xdotool mousemove $((X + 776 + 192 + 16 + 5*9 + 4)) $((Y + 24 + 5*9 + 4)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 1.2; snap s12d-sprite-edit
xdotool key u; sleep 0.8
xdotool mousemove $((X + 984 + 116 + 30)) $((Y + 357 + 12)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.3  # back to tiles
# world map (right of picture, under palette labels): click room 1's thumbnail -> Midnight.
# thumbs start at y = 24 + 192 + 8 + 8*24 + 8 = 424, 8 across the remaining width
xdotool mousemove $((X + 776 + 1*97 + 40)) $((Y + 424 + 30)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.6; snap s13-worldmap-click
# wheel over the picture: background -1 (wheel down), Shift+wheel: tileset +4; then undo both
xdotool mousemove $((X + 400)) $((Y + 24 + 200)) click 5; sleep 0.4; snap s13b-wheel-bkgrnd
xdotool keydown shift; xdotool click 4; xdotool keyup shift; sleep 0.4; snap s13c-wheel-tileset
xdotool key u; sleep 0.3; xdotool key u; sleep 0.3
# wheel over the text panel scrolls it (room 1 has a short panel: no-op is fine)
xdotool mousemove $((X + 200)) $((Y + 24 + 528 + 40)) click 5 click 5; sleep 0.3; snap s14-scrolled
# switch link: open switch 0 in Midnight (goto switch, Ctrl-S), right-click object O1 (R2D2 at 26,7)
xdotool key s; sleep 0.2; xdotool type 0; sleep 0.3; xdotool key ctrl+s; sleep 0.5; snap s15-switch-open
xdotool mousemove $((X + 26*24 + 12)) $((Y + 24 + 7*24 + 12)) mousedown 3; sleep 0.08; xdotool mouseup 3; sleep 0.6; snap s16-linked-object
xdotool key Escape; sleep 0.2; xdotool key Escape; sleep 0.2; xdotool key Escape; sleep 0.2; xdotool key u; sleep 0.4
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
if [ $had_orig = 0 ] && [ -e Spider/BLOCKS.EGA.orig ]; then echo "BLOCKS.EGA was written by the pixel edit (expected)"; else echo "BLOCKS.EGA never written (pixel edit did not save!)"; fi
cp $T/BLOCKS.EGA.bak Spider/BLOCKS.EGA; [ $had_orig = 0 ] && rm -f Spider/BLOCKS.EGA.orig
if [ $had_sorig = 0 ] && [ -e Spider/SPIDS1.EGA.orig ]; then echo "SPIDS1.EGA was written by the sprite edit (expected)"; else echo "SPIDS1.EGA never written (sprite edit did not save!)"; fi
cp $T/SPIDS1.EGA.bak Spider/SPIDS1.EGA; [ $had_sorig = 0 ] && rm -f Spider/SPIDS1.EGA.orig
tail -8 $T/gui.log
