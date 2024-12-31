#!/bin/bash

SPIDEY_DIR=${SPIDEY_DIR:-Spider}
cp ROOMS.SPL $SPIDEY_DIR
cd $SPIDEY_DIR
ls
dosbox SPIDEY.EXE &
db=$!
sleep 2
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
for i in {1..50}; do xdotool key 1; done # why does it not pick it up :(
wait $db
cd -
