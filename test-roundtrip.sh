#!/bin/bash
# Roundtrip test: read ROOMS.SPL, recompress all rooms, verify binary-identical output.
# Tests that our compress() produces bit-for-bit identical output to the original game data.

orig=$(mktemp /tmp/rooms_roundtrip_XXXXXX.spl)
cp ROOMS.SPL "$orig"
trap 'cp "$orig" ROOMS.SPL; rm -f "$orig"' EXIT

./a.out recompress 2>/dev/null

if diff "$orig" ROOMS.SPL >/dev/null; then
    echo "PASS: roundtrip identical"
    exit 0
else
    echo "FAIL: roundtrip differs"
    diff <(xxd "$orig") <(xxd ROOMS.SPL) | head -80
    exit 1
fi
