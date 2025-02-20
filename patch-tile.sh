#!/bin/bash

set -o errexit
tile=${1:-1}
offset=${2:-0}
y=21
room=0

if ! ./a.out display $room |& grep 'Test room'; then
    for idx in {0..704}; do ./a.out patch $room tile[$idx] 0; done
    ./a.out patch $room name 'Test room'
fi
for x in {0..8}; do
    ./a.out patch $room tile[$x][$((y-1))] $((tile+128))
    ./a.out patch $room tile[$x][$y] $((tile+64))
done
for x in {8..16}; do
    ./a.out patch $room tile[$x][$((y-1))] $((tile+192))
    ./a.out patch $room tile[$x][$y] $((tile+64))
done
for x in {16..24}; do
    ./a.out patch $room tile[$x][$((y-2))] $((tile+128))
    ./a.out patch $room tile[$x][$y] $((tile+64))
done
for x in {24..30}; do
    ./a.out patch $room tile[$x][$((y-1))] $tile
    ./a.out patch $room tile[$x][$y] $((tile+64))
done
./a.out patch $room tile_offset $offset
./a.out display $room
./a.out find_tile $tile $offset
./play.sh
