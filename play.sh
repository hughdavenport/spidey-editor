#!/bin/bash

SPIDEY_DIR=${SPIDEY_DIR:-Spider}
# The editor's data dir (its --data-dir, passed on by Ctrl-P as SPIDEY_DATA_DIR): where
# ROOMS.SPL, MOVERTAB.DAT and any graphics edited in the GUI live. Default: here.
DATA_DIR=${SPIDEY_DATA_DIR:-.}
# graphics and backgrounds edited in the GUI
for f in "$DATA_DIR"/*.EGA "$DATA_DIR"/*.CGA "$DATA_DIR"/BACKS.SPL; do
    if [ -f "$f" ] && [ "$f" -nt "$SPIDEY_DIR/$(basename "$f")" ]; then cp "$f" "$SPIDEY_DIR/"; fi
done
# The mover table (MOVERTAB.DAT, one entry per object across the whole set) must match
# ROOMS.SPL: a levelset's own table lives beside it in the data dir; without one the game's
# original table is used.
[ -f "$SPIDEY_DIR/MOVERTAB.DAT.original" ] || cp "$SPIDEY_DIR/MOVERTAB.DAT" "$SPIDEY_DIR/MOVERTAB.DAT.original"
MOVERTAB="$DATA_DIR/MOVERTAB.DAT"; [ -f "$MOVERTAB" ] || MOVERTAB="$SPIDEY_DIR/MOVERTAB.DAT.original"
# PLAY_ROOM (set by the editor's Ctrl-P) = start in that room: `swap` exchanges it with slot 1,
# the game's start room, in the copy the game runs from, with exits, toggle targets, index bases
# and the mover table following (importRoom would leave the room's old bases and lose the name
# other rooms' switches refer to it by).
if [ -n "${PLAY_ROOM:-}" ] && [ "$PLAY_ROOM" != 1 ]; then
    ./a.out --data-dir "$DATA_DIR" swap 1 "$PLAY_ROOM" --movertab "$MOVERTAB" "$SPIDEY_DIR/ROOMS.SPL" || exit 1
else
    cp "$DATA_DIR/ROOMS.SPL" "$SPIDEY_DIR/"
    cp "$MOVERTAB" "$SPIDEY_DIR/MOVERTAB.DAT"
fi
cd $SPIDEY_DIR
dosbox-x -nopromptfolder SPIDEY.EXE 2>/dev/null &
db=$!
# Wait for window to open
i3-msg -m -t subscribe '[ "window" ]' | grep -q DOSBOX
#sleep 5
# xdotool key Alt+Return
sleep .2
xdotool type 1
sleep .1
xdotool type Enter
sleep .1
xdotool type Enter
sleep .1
xdotool type Enter
sleep .1
xdotool type Enter
sleep .1
xdotool type Enter
sleep .1
for i in {1..100}; do xdotool key 1; done # why does it not pick it up :(
xdotool key F12+F
wait $db
