#!/bin/bash
# Headless end-to-end test of `./a.out gui`: runs it on Xvfb :97, drives it
# with xdotool through help, movement, goto room/switch/object, room details,
# the hex map, Ctrl-D display toggles, a mouse click, a Shift+drag paint
# stroke and a hot reload, screenshotting each step into $T, then quits with
# q. Everything runs on a sandbox copy, so the paint stroke changes only the
# sandbox's Spider/ROOMS.SPL. Needs Xvfb, xdotool and ImageMagick's import.
#   ASAN_OPTIONS=detect_leaks=0 ./gui_smoke_test.sh     (leaks: run without)
set -u
T=${T:-/tmp/spidey-gui-smoke}
mkdir -p "$T"
R=$(cd "$(dirname "$0")" && pwd)
# Run from a sandbox copy so the test never touches the repo's ROOMS.SPL, the
# game files, or a GUI you have open on them (the hot-reload step rebuilds
# gui.so inside the sandbox from copied sources).
SB=$T/sandbox
rm -rf "$SB"; mkdir -p "$SB"
cp $R/*.c $R/*.h $R/a.out $R/gui.so $R/play.sh $R/ROOMS.SPL "$SB/"
[ -x $R/spidey-editor ] && cp $R/spidey-editor "$SB/"
BIN=${BIN:-./a.out}   # BIN=./spidey-editor tests the release build
cp -r $R/Spider "$SB/Spider"
# --data-dir Spider below means the GUI reads and writes Spider/ROOMS.SPL (and MOVERTAB.DAT):
# give that sandbox data dir the repo's own rooms, not the game's shipped copy.
cp $R/ROOMS.SPL "$SB/Spider/ROOMS.SPL"
[ -f $R/MOVERTAB.DAT ] && cp $R/MOVERTAB.DAT "$SB/Spider/MOVERTAB.DAT"
ln -sfn $R/raylib-6.0_linux_amd64 "$SB/raylib-6.0_linux_amd64"
cd "$SB"
rm -f Spider/*.orig
pkill -f "^Xvfb :97" 2>/dev/null; sleep 0.3
Xvfb :97 -screen 0 1600x1200x24 -nolisten tcp >/dev/null 2>&1 &
XV=$!
sleep 1
export DISPLAY=:97
$BIN gui --data-dir Spider --scale 3 > $T/gui.log 2>&1 &
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
# TAKE 1's floor is a hurting tile (7C): red in the Ctrl-D t tile overlay and red borders in the picker until Ctrl-D t turns it off
# (help is up at this point; hide it for these two frames and put it back)
xdotool key r; sleep 0.3; xdotool type 07; sleep 0.6; xdotool key ctrl+shift+slash; sleep 0.4; snap s4d-take1-hurt-on
xdotool key ctrl+d; sleep 0.2; xdotool key t; sleep 0.5; snap s4e-take1-tiles-off
xdotool key ctrl+d; sleep 0.2; xdotool key t; sleep 0.3; xdotool key ctrl+shift+slash; sleep 0.3
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
# tile pixel editor (right of the palette): select tile 0x12 (tile 0 is locked), pick colour 2 (red), paint pixel (1,1)
xdotool mousemove $((X + 776 + 2*24 + 12)) $((Y + 24 + 2*24 + 12)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.3
xdotool mousemove $((X + 776 + 192 + 16 + 2*9 + 4)) $((Y + 24 + 8*9 + 8 + 4)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.2
xdotool mousemove $((X + 776 + 192 + 16 + 1*9 + 4)) $((Y + 24 + 1*9 + 4)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 1.2; snap s12b-pixel-edit
xdotool key u; sleep 0.8; snap s12c-pixel-undo   # u undoes the stroke (most recent edit)
# fill: the button right of "pick" (pick at pixed_x 984 + 192 + 8 = 1184, 72 wide; fill at 1264), then click pixel (6,6): floods its region red; undo
xdotool mousemove $((X + 1264 + 36)) $((Y + 224 + 24)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.3
xdotool mousemove $((X + 776 + 192 + 16 + 6*9 + 4)) $((Y + 24 + 6*9 + 4)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 1.2; snap s12c2-pixel-fill
xdotool key u; sleep 0.8
xdotool mousemove $((X + 1264 + 36)) $((Y + 224 + 24)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.3   # fill is a mode: turn it off again
# sprite editing: SPIDS1 button (2nd button under the editor), pick frame 0x18 (row 3, col 0), paint a pixel, snapshot, undo
xdotool mousemove $((X + 776 + 46)) $((Y + 448 + 12)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.4; snap s12d0-spids1-mode
xdotool mousemove $((X + 776 + 12)) $((Y + 24 + 3*21 + 10)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.4
xdotool mousemove $((X + 776 + 192 + 16 + 5*9 + 4)) $((Y + 24 + 5*9 + 4)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 1.2; snap s12d-sprite-edit
xdotool key u; sleep 0.8
xdotool mousemove $((X + 876 + 46)) $((Y + 416 + 12)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.3  # back to tiles
# backgrounds: "backs" button (4th), pick scenery tile 0x10 (16 across, 24 px cells like the tiles, twice the picker width: row 1, col 0; a solid tile),
# Shift+drag along room row 3, wait for the BACKS.SPL write, undo, back to tiles
xdotool mousemove $((X + 876 + 46)) $((Y + 448 + 12)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.5; snap s12e-backs-view
xdotool mousemove $((X + 776 + 0*24 + 12)) $((Y + 24 + 1*24 + 12)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.3
xdotool keydown shift; xdotool mousemove $((X + 2*24 + 12)) $((Y + 24 + 3*24 + 12)) mousedown 1; sleep 0.1
for i in 1 2 3 4; do xdotool mousemove_relative 24 0; sleep 0.1; done
xdotool mouseup 1; xdotool keyup shift; sleep 1.2; snap s12f-backs-painted
xdotool key u; sleep 0.8; snap s12g-backs-undo
xdotool mousemove $((X + 876 + 46)) $((Y + 416 + 12)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.3  # back to tiles
# font button (row 2 of the grid, y 480): the CHARSET picker (8 across, 24 px cells like the tiles); pick tile 0x41 (row 8, col 1), paint pixel (3,3) red, undo
xdotool mousemove $((X + 776 + 46)) $((Y + 480 + 12)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.5; snap s12h-font-view
xdotool mousemove $((X + 776 + 1*24 + 12)) $((Y + 24 + 8*24 + 12)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.3
xdotool mousemove $((X + 984 + 2*24 + 12)) $((Y + 224 + 12)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.2
xdotool mousemove $((X + 984 + 3*24 + 12)) $((Y + 24 + 3*24 + 12)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 1.2; snap s12i-font-edit
xdotool key u; sleep 1.5   # (the undo's debounced write lands ~0.6 s later; a click inside that long frame would be lost)
# screens: the "screens" button opens screen mode (whole window: picture at 3x from (128,192), panel at x 1216:
# list rows from y 56 every 26, swatches at y 298 (36 px), buttons pick/fill y 378, import/export y 410, room y 442)
xdotool mousemove $((X + 876 + 46)) $((Y + 480 + 12)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.8; snap s12j-screen-mode
xdotool mousemove $((X + 1224 + 40)) $((Y + 56 + 12)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.5        # LOADSCRN
xdotool mousemove $((X + 1224 + 2*36 + 18)) $((Y + 298 + 18)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.3  # colour 2
xdotool mousemove $((X + 128 + 100*3 + 1)) $((Y + 192 + 50*3 + 1)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 1.2; snap s12k-screen-edit
xdotool mousemove $((X + 1340 + 54)) $((Y + 410 + 12)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.6   # export
xdotool key u; sleep 0.8
xdotool mousemove $((X + 1224 + 54)) $((Y + 410 + 12)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 1.2; snap s12l-screen-import   # import
xdotool key u; sleep 0.6
xdotool key Escape; sleep 0.5; snap s12m-back-from-screens   # Esc leaves screen mode
xdotool mousemove $((X + 876 + 46)) $((Y + 416 + 12)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.3  # back to tiles
# world map (right of picture, under palette labels): click room 1's thumbnail -> Midnight.
# buttons under the picker in two columns (x 776 / 876, rows of 32 px from y 416, below the tallest picker (sprite sheets / font); attributes level with the swatches at 224 / 256); world map at 584; thumbs ~65x45 + 4 gap (height-limited)
xdotool mousemove $((X + 776 + 1*72 + 30)) $((Y + 584 + 20)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.6; snap s13-worldmap-click
# wheel over the picture: background -1 (wheel down), Shift+wheel: tileset +4; then undo both
xdotool mousemove $((X + 400)) $((Y + 24 + 200)) click 5; sleep 0.4; snap s13b-wheel-bkgrnd
xdotool keydown shift; xdotool click 4; xdotool keyup shift; sleep 0.4; snap s13c-wheel-tileset
# wheel over the tile picker: tileset +4 again, the background stays; then undo all three
xdotool mousemove $((X + 776 + 100)) $((Y + 24 + 100)) click 4; sleep 0.4; snap s13d-wheel-picker-tileset
xdotool key u; sleep 0.3; xdotool key u; sleep 0.3; xdotool key u; sleep 0.3
# wheel over the text panel scrolls it (room 1 has a short panel: no-op is fine)
xdotool mousemove $((X + 200)) $((Y + 24 + 528 + 40)) click 5 click 5; sleep 0.3; snap s14-scrolled
# block editor: cursor onto block O0 (helicopter) with o0; the panel shows its 6x2 tiles.
# click the "solid" attribute, then paint cell (1,1) of the block (zoom 32 px per tile) and undo
xdotool key r; sleep 0.2; xdotool type 01; sleep 0.5
xdotool key o; sleep 0.2; xdotool type 0; sleep 0.6; snap s14b-block-selected
xdotool mousemove $((X + 984 + 1*36 + 18)) $((Y + 24 + 1*36 + 18)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.8; snap s14c-block-painted
xdotool key u; sleep 0.4
# block export / import (block buttons sit under the picker, two per row from y 224: import = row 2 left, export = row 2 right): the PNG round-trips to the same tiles
xdotool mousemove $((X + 876 + 46)) $((Y + 288 + 12)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.5
xdotool mousemove $((X + 776 + 46)) $((Y + 288 + 12)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 1.0; snap s14c2-block-import
xdotool key u; sleep 0.4
# gfx (the grid's row-4 slot) hides the block panel; the same slot then reads "block" and brings it back
xdotool mousemove $((X + 776 + 46)) $((Y + 544 + 12)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.5; snap s14c3-block-hidden
xdotool mousemove $((X + 776 + 46)) $((Y + 544 + 12)) mousedown 1; sleep 0.08; xdotool mouseup 1; sleep 0.5; snap s14c4-block-back
# thick link lines: cursor on switch 4 (o0's toggling switch), snapshot
xdotool key s; sleep 0.2; xdotool type 4; sleep 0.6; snap s14d-switch-hot
# switch link: open switch 0 in Midnight (goto switch, Ctrl-S), right-click object O1 (R2D2 at 26,7)
# Space previews the open switch active (switch 1's trapdoor tiles at (15-16,4) show their on
# state), Space again idles it; Esc leaves switch editing before the switch-0 steps below
xdotool key s; sleep 0.2; xdotool type 1; sleep 0.3; xdotool key ctrl+s; sleep 0.5
xdotool key space; sleep 0.5; snap s14e-switch-preview-on; xdotool key space; sleep 0.5; snap s14f-switch-preview-off
xdotool key Escape; sleep 0.3
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
[ -e Spider/BLOCKS.EGA.orig ] && echo "BLOCKS.EGA was written by the pixel edit (expected)" || echo "BLOCKS.EGA never written (pixel edit did not save!)"
[ -e Spider/SPIDS1.EGA.orig ] && echo "SPIDS1.EGA was written by the sprite edit (expected)" || echo "SPIDS1.EGA never written (sprite edit did not save!)"
[ -e Spider/BACKS.SPL.orig ] && echo "BACKS.SPL was written by the background edit (expected)" || echo "BACKS.SPL never written (background edit did not save!)"
[ -e Spider/CHARSET.EGA.orig ] && echo "CHARSET.EGA was written by the font edit (expected)" || echo "CHARSET.EGA never written (font edit did not save!)"
[ -e Spider/LOADSCRN.EGA.orig ] && echo "LOADSCRN.EGA was written by the screen edit (expected)" || echo "LOADSCRN.EGA never written (screen edit did not save!)"
[ -e Spider/LOADSCRN.png ] && echo "LOADSCRN.png was exported (expected)" || echo "LOADSCRN.png never exported!"
[ -e Spider/BLOCK-01-0.png ] && echo "BLOCK-01-0.png was exported (expected)" || echo "BLOCK-01-0.png never exported!"
tail -8 $T/gui.log
