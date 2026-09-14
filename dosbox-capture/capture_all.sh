#!/bin/bash
# capture_all.sh GAME_FILES_DIR OUT_DIR SPL [SPL...]
# Ground-truth screenshots of every non-empty room of each SPL file: the room
# is swapped into slot 1 (the start room) with importRoom, its four exits are
# pointed back at itself (so falling out of the room re-enters it instead of
# showing the neighbour), and the game is booted headless by dosroom.sh.
# Four emulators run in parallel on Xvfb displays :91-:94. Output:
#   OUT_DIR/<spl stem>/roomNN/shot-{1,2,3}.png
# GAME_FILES_DIR must hold SPIDEY.EXE, *.EGA, *.CGA, BACKS.SPL, MOVERTAB.DAT.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
A=$REPO/a.out
FILES=$1; OUT=$2; shift 2
mkdir -p "$OUT"
jobs=$OUT/jobs.txt; : > "$jobs"

make_job() {  # spl room
    local spl=$1 room=$2 set
    set=$(basename "$spl"); set=${set%.*}
    local d="$OUT/$set/room$(printf %02d "$room")"
    mkdir -p "$d/src" "$d/game"
    cp "$spl" "$d/src/ROOMS.SPL"
    ( cd "$d/src" && $A display "$room" > room.txt 2>/dev/null && $A importRoom 1 room.txt swapped.spl >/dev/null 2>&1 )
    if [ ! -s "$d/src/swapped.spl" ]; then echo "skip $set $room (import failed)"; return; fi
    ( cd "$d/src" && cp swapped.spl ROOMS.SPL && $A patch 1 room_north 1 room_south 1 room_west 1 room_east 1 >/dev/null 2>&1 )
    cp "$FILES"/*.EGA "$FILES"/*.CGA "$FILES"/BACKS.SPL "$FILES"/MOVERTAB.DAT "$FILES"/SPIDEY.EXE "$d/game/"
    cp "$d/src/ROOMS.SPL" "$d/game/ROOMS.SPL"
    echo "$d/game $d/shot 1" >> "$jobs"
}

for spl in "$@"; do
    for r in $(python3 -c "import sys; sys.path.insert(0, '$REPO'); import spidey_gfx; print(*spidey_gfx.room_ids('$spl'))"); do
        make_job "$spl" "$r"
    done
done
echo "$(wc -l < "$jobs") jobs"

run_lane() {  # lane display
    local lane=$1 dn=$2 i=0
    while read -r game out mode; do
        if [ $((i % 4)) -eq "$lane" ]; then "$HERE/dosroom.sh" "$dn" "$game" "$out" "$mode"; fi
        i=$((i+1))
    done < "$jobs"
}
for lane in 0 1 2 3; do run_lane $lane $((91+lane)) > "$OUT/lane$lane.log" 2>&1 & done
wait
echo ALL DONE
