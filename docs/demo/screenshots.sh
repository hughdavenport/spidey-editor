#!/bin/bash
# Retake docs/screenshots/*.png: runs `./a.out gui` on Xvfb :98 against the original demo data
# (gen_demo.py's graphics, rooms.txt's rooms; none of the game's own files), drives it with
# xdotool and captures the editor's window at each step. Everything runs in a sandbox copy,
# so neither the repo's files nor a GUI you have open are touched.
# Needs Xvfb, xdotool and ImageMagick's import. Run `make` first.
#   ASAN_OPTIONS=detect_leaks=0 docs/demo/screenshots.sh
set -eu
R=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$R/docs/screenshots
T=$(mktemp -d)
trap 'kill $GP $XV $WD 2>/dev/null; rm -rf "$T"' EXIT
GP= XV= WD=

# A sandbox copy (sources too: gui.so is built inside it) and the demo data dir.
cp "$R"/*.c "$R"/*.h "$R"/a.out "$T/"
ln -s "$R/raylib-6.0_linux_amd64" "$T/raylib-6.0_linux_amd64"
python3 "$R/docs/demo/gen_demo.py" "$T/demo"
(cd "$T/demo" && ../a.out import "$R/docs/demo/rooms.txt" ROOMS.SPL >/dev/null 2>&1)
cd "$T"

Xvfb :98 -screen 0 1600x1200x24 -nolisten tcp >/dev/null 2>&1 &
XV=$!
sleep 1
export DISPLAY=:98
./a.out gui --data-dir demo --scale 3 > gui.log 2>&1 &
GP=$!
( sleep 300; kill -9 $GP 2>/dev/null ) &   # the first run builds gui.so
WD=$!
for _ in $(seq 120); do WIN=$(xdotool search --name "Spidey Editor" 2>/dev/null | head -1) && [ -n "$WIN" ] && break; sleep 1; done
[ -n "${WIN:-}" ] || { echo "the GUI never opened; see $T/gui.log" >&2; cat gui.log >&2; exit 1; }
sleep 3
mkdir -p "$OUT"
snap() { sleep 0.6; import -window "$WIN" "$OUT/$1.png"; }
key() { xdotool key --delay 120 "$@"; }

esc() { for _ in 1 2 3; do xdotool key Escape; sleep 0.3; done; }   # back to plain tile editing from any depth
key s 0; snap gui-overview                      # cursor on switch 0: its links are drawn thick
key ctrl+s c 2; snap gui-switch-toggle          # switch 0's TOGGLE_BIT chunk into Store Room...
esc; key s 0 ctrl+s c 1; snap gui-switch-block   # ...and its TOGGLE_BLOCK chunk (the gate)
esc; key s 1 ctrl+s c 1; snap gui-switch-object-block    # switch 1 sends the lift up
esc; key s 2 ctrl+s c 1; snap gui-switch-object-sprite   # switch 2 sets the dinosaur patrolling
esc; key o 0; snap gui-block-editor             # cursor on the lift block: its tile grid replaces the pixel editor
esc; key s 0; esc
# the source buttons under the picker: two columns at x 776 / 876, rows of 32 px from y 416
click() { xdotool mousemove --window "$WIN" "$1" "$2" mousedown 1; sleep 0.08; xdotool mouseup 1; }
click $((776 + 46)) $((480 + 12)); snap gui-sprites   # sprites
click $((876 + 46)) $((448 + 12)); snap gui-backs     # backs
click $((776 + 46)) $((448 + 12))                     # back to tiles
key ctrl+shift+slash; snap gui-help             # the help overlay, with the GUI's lines and the colour legend
key q
echo "wrote $(ls "$OUT"/gui-*.png | wc -l) screenshots to $OUT"
