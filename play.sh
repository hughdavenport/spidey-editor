#!/bin/bash

SPIDEY_DIR=${SPIDEY_DIR:-Spider}
cp ROOMS.SPL $SPIDEY_DIR
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
