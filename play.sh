#!/bin/bash

SPIDEY_DIR=${SPIDEY_DIR:-Spider}
# graphics and backgrounds edited in the GUI (in its data dir, here if that was .)
for f in BLOCKS.EGA SPIDS1.EGA SPIDS2.EGA SCENERY.EGA BLOCKS.CGA SPIDS1.CGA SPIDS2.CGA SCENERY.CGA BACKS.SPL; do
    if [ -f "$f" ] && [ "$f" -nt "$SPIDEY_DIR/$f" ]; then cp "$f" "$SPIDEY_DIR/"; fi
done
# PLAY_ROOM (set by the editor's Ctrl-P) = start in that room: it is swapped
# into slot 1, the game's start room, in the copy the game runs from.
if [ -n "${PLAY_ROOM:-}" ] && [ "$PLAY_ROOM" != 1 ]; then
    tmp=$(mktemp)
    ./a.out display "$PLAY_ROOM" > "$tmp" && ./a.out importRoom 1 "$tmp" "$SPIDEY_DIR/ROOMS.SPL"
    rm -f "$tmp"
else
    cp ROOMS.SPL $SPIDEY_DIR
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
