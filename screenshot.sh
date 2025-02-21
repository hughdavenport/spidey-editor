#!/bin/bash

SPIDEY_DIR=${SPIDEY_DIR:-Spider}
cp ROOMS.SPL $SPIDEY_DIR
cd $SPIDEY_DIR
ls
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
for i in {1..20}; do sudo ydotool key 1; done # why does it not pick it up :(
sleep .1
notify-send "should be on midnight, sleeping 1"
sleep 1;
dunstctl close-all
flameshot screen -p screenshot.png
mv $SPIDEY_DIR/screenshot.png screenshot-midnight.png
for i in {1..10}; do sudo ydotool key Left+Up; done
notify-send "stopped automoving, sleeping 2"
sleep 2;
dunstctl close-all
flameshot screen -p screenshot.png
sleep .1
xdotool key Ctrl+F9
wait $db
cd -
mv $SPIDEY_DIR/screenshot.png screenshot-testroom.png
echo "Saved to screenshot-{midnight,testroom}.png"
