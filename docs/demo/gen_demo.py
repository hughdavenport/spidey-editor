#!/usr/bin/env python3
"""Write a complete, original data set for the editor: the docs' screenshots use it.

The game's own graphics and rooms are copyrighted (Paragon Software, 1990) and
never go in this repository, so the screenshots in docs/screenshots/ are taken
against these files instead: every tile, background and sprite here is drawn
below, and the rooms come from docs/demo/rooms.txt (`a.out import`).

    python3 docs/demo/gen_demo.py OUT_DIR

writes OUT_DIR/BLOCKS.EGA, SCENERY.EGA, SPIDS1.EGA, SPIDS2.EGA, BACKS.SPL and
MOVERTAB.DAT, in the layouts render.c reads (see GRAPHICS.md).
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# EGA plane -> colour bit, as render.c's PLANE_ORDER.
PLANE_ORDER = (3, 2, 1, 0)
SPRITE_KEY = 15  # transparent in a sprite frame
SPIDS_W, SPIDS_H, SPIDS_STRIDE = 24, 21, 256

# One character per palette index, for the pictures below. The palette is the
# game's: 0 black, 1 blue, 2 red, 3 white, 4 dark grey, 5 dark blue, 6 dark red,
# 7 light grey, 8 light cyan, 9 green, 10 magenta, 11 orange, 12 sky blue,
# 13 dark green, 14 purple, 15 yellow (the sprites' transparent colour).
COLOURS = {
    ".": 0, "b": 1, "r": 2, "W": 3, "g": 4, "B": 5, "R": 6, "w": 7,
    "c": 8, "G": 9, "m": 10, "o": 11, "s": 12, "d": 13, "p": 14, "y": 15,
    " ": SPRITE_KEY,
}


def picture(rows, w, h):
    """A w x h grid of palette indices from rows of COLOURS characters (short rows / missing rows pad
    with the key colour, bottom-aligned so sprites stand on the frame's last row)."""
    rows = [r.ljust(w)[:w] for r in rows]
    rows = [" " * w] * (h - len(rows)) + rows
    return [[COLOURS[ch] for ch in row] for row in rows]


def ega_planar(grid, w, h):
    """Plane-major EGA bytes (all rows of plane 0, then plane 1, ...), as tiles and sprites use."""
    bpr = w // 8
    out = bytearray(4 * bpr * h)
    for p in range(4):
        for y in range(h):
            for x in range(w):
                if (grid[y][x] >> PLANE_ORDER[p]) & 1:
                    out[p * bpr * h + y * bpr + x // 8] |= 0x80 >> (x % 8)
    return bytes(out)


# --------------------------------------------------------------------- tiles

def tile(*rows):
    assert len(rows) == 8 and all(len(r) == 8 for r in rows), rows
    return picture(rows, 8, 8)


def fill(c):
    return [[c] * 8 for _ in range(8)]


def recolour(grid, mapping):
    return [[mapping.get(c, c) for c in row] for row in grid]


# Tileset 0. Indices 0x01, 0x1c-0x21 and 0x3c-0x3f hurt (room.c's tile_hurts), so they are drawn as hazards.
FOREGROUND = {
    0x01: tile("........", "........", "........", "...W...W", "..WwW.Ww", ".wwwwwww", "wwwwwwww", "gggggggg"),
    0x02: tile("oooRoooo", "oooRoooo", "RRRRRRRR", "RoooooRo", "RoooooRo", "RRRRRRRR", "oooRoooo", "RRRRRRRR"),
    0x03: tile("wwwwwwwg", "wWwwwwwg", "wwwwwwwg", "wwwwwwwg", "wwwwwwwg", "wwwwwwwg", "wwwwwwwg", "gggggggg"),
    0x04: tile("oooooooo", "oRooooRo", "RRRRRRRR", "........", "........", "........", "........", "........"),
    0x05: tile("bbbbbbbb", "bs....sb", "b.s..s.b", "b..ss..b", "b..ss..b", "b.s..s.b", "bs....sb", "bbbbbbbb"),
    0x06: tile("G.G.G.G.", "GGGGGGGG", "GdGGdGGd", "dddddddd", "RdRRRdRR", "RRRRRRRR", "RRgRRRRR", "RRRRRRgR"),
    0x07: tile("RRRRRRRR", "RRgRRRRR", "RRRRRRgR", "RRRRRRRR", "RgRRRRRR", "RRRRgRRR", "RRRRRRRR", "RRRRRRRg"),
    0x08: tile("oooooooo", "oR....Ro", "o.R..R.o", "o..RR..o", "o..RR..o", "o.R..R.o", "oR....Ro", "oooooooo"),
    0x09: tile("wwwwwwww", "wg....gw", "w......w", "w......w", "w......w", "w......w", "wg....gw", "wwwwwwww"),
    0x0a: tile("........", "gggggggg", "wWwwwwww", "wwwwwwww", "wwwwwwww", "gggggggg", "........", "........"),
    0x0b: tile(".gwWwwg.", ".gwwwwg.", ".gwwwwg.", ".gwwwwg.", ".gwwwwg.", ".gwwwwg.", ".gwwwwg.", ".gwwwwg."),
    0x0c: tile("...gg...", "...gg...", "..gggg..", ".gyyyyg.", ".yyWWyy.", ".yyyyyy.", "..yyyy..", "........"),
    0x0d: tile("wwwwwwww", "wBBBwBBw", "wBsBwBBw", "wBBBwBBw", "wwwwwwww", "wBBBwBBw", "wBBBwBsw", "wwwwwwww"),
    0x0e: tile("........", "..W.....", "...W....", "....W...", ".....W..", "..gggg..", ".gwwwwg.", "gggggggg"),
    0x0f: tile("........", ".....W..", "....W...", "...W....", "..W.....", "..gggg..", ".gwwwwg.", "gggggggg"),
    0x10: tile("w.w.w.w.", "w.w.w.w.", "wwwwwwww", "w.w.w.w.", "w.w.w.w.", "wwwwwwww", "w.w.w.w.", "w.w.w.w."),
    0x11: tile("........", "........", "..rrrr..", ".rrWrrr.", ".rrrrrr.", "gggggggg", "gwwwwwwg", "gggggggg"),
    0x12: tile("gggggggg", "wGwGwGwG", "gggggggg", ".g.g.g.g", "........", "........", "........", "........"),
    0x13: tile("cccccccc", "sssssssc", "bbbbbbbs", "BBBBBBBb", "........", "........", "........", "........"),
    0x14: tile(".ccccccc", "csssssss", "cbbbbbbb", ".BBBBBBB", "........", "........", "........", "........"),
    0x15: tile("ccccccc.", "sssssssc", "bbbbbbbc", "BBBBBBB.", "........", "........", "........", "........"),
    0x16: tile("...ww...", "..w..w..", "...ww...", "..w..w..", "...ww...", "..w..w..", "...ww...", "..w..w.."),
    0x17: tile("...G....", "..GdG.G.", ".GdGGGd.", "..GdGd..", "...dd...", ".oooooo.", "..oRRo..", "..oooo.."),
    0x18: tile("wwwwwwww", "w.W.W.Ww", "wwwwwwww", "w.W.W..w", "wwwwwwww", "...gg...", "...gg...", "...gg..."),
    0x19: tile(".RRRRRR.", "RoooooRR", "RRRRRRRR", "RoooooRR", "RoooooRR", "RRRRRRRR", "RoooooRR", ".RRRRRR."),
    0x1a: tile("gggggggg", "g.g.g.gg", "gggggggg", "g.g.g.gg", "gggggggg", "g.g.g.gg", "gggggggg", "g.g.g.gg"),
    0x1b: tile("wwwwwwww", "wgwwwwgw", "wwwwwwww", "wwwwwwww", "wwwwwwww", "wwwwwwww", "wgwwwwgw", "wwwwwwww"),
    0x1c: tile("..o...o.", ".ooo.ooo", "oorooroo", "rrrrrrrr", "rrRrrrrR", "RrrrRrrr", "rrRrrrRr", "RRRRRRRR"),
    0x1d: tile("rrRrrrrR", "RrrrRrrr", "rrRrrrRr", "RRRRRRRR", "rrrRrrrr", "RrrrrRrr", "rrRrrrrR", "RRRRRRRR"),
    0x1e: tile("...c....", "....c...", "...c....", "..c.....", "...c....", "....c...", "...c....", "..c....."),
    0x1f: tile("...w....", "..www...", ".wwgww..", "wwg.gwww", ".wwgww..", "..www...", "...w....", "........"),
    0x20: tile("........", "........", "..G..G..", ".GGGGGGG", "GdGGGdGG", "GGdGGGGd", "dGGGdGGG", "dddddddd"),
    0x21: tile("....o...", "...oo...", "..oyo...", "..oyyo..", ".oyWyo..", ".oyyyyo.", "oyyWyyyo", "rrrrrrrr"),
    0x3c: tile("........", "........", "........", "........", "...W...W", "..WwW.Ww", ".wwwwwww", "gggggggg"),
    0x3d: tile("gggggggg", "wwwwwww.", "wW.wwW..", "W...W...", "........", "........", "........", "........"),
    0x3e: tile("gg......", "wwwW....", "wwwwW...", "wwW.....", "wwwwW...", "wwwW....", "wwwwW...", "gg......"),
    0x3f: tile("......gg", "....Wwww", "...Wwwww", ".....Www", "...Wwwww", "....Wwww", "...Wwwww", "......gg"),
}


def foreground_tiles():
    """The 512 BLOCKS tiles: tileset 0 drawn above (fillers recoloured from it), sets 1-7 recoloured
    copies so every tileset in the picker has something in it."""
    drawn = sorted(FOREGROUND)
    base = [fill(0)] * 64
    for i in range(1, 64):
        if i in FOREGROUND:
            base[i] = FOREGROUND[i]
        else:   # 0x22-0x3b: the solid tiles in other colours
            src = FOREGROUND[drawn[(i * 7) % len(drawn)]]
            base[i] = recolour(src, {11: 12, 6: 5, 2: 1, 7: 8, 4: 5, 9: 10, 13: 14})
    sets = []
    swaps = [{}, {11: 9, 6: 13}, {11: 12, 6: 5, 2: 1}, {7: 11, 3: 15}, {11: 10, 6: 14},
             {4: 6, 7: 2}, {9: 11, 13: 6}, {1: 13, 12: 9, 5: 13}]
    for s in range(8):
        sets += [recolour(t, swaps[s]) for t in base]
    return sets


# ------------------------------------------------------------------ scenery

SCENERY = {
    0x01: fill(5),
    0x02: tile("BBBBBBBB", "BBBBBBBB", "BBBBBBBB", "BBBsBBBB", "BBBBBBBB", "BBBBBBBB", "BBBBBBBB", "BBBBBBBB"),
    0x03: tile("BBBBBBBB", "BBBBsBBB", "BBBBsBBB", "BBssWssB", "BBBBsBBB", "BBBBsBBB", "BBBBBBBB", "BBBBBBBB"),
    0x04: tile("BBBBBBww", "BBBBwwww", "BBBwwwww", "BBwwwwww", "BwwwwgWw", "Bwwwwwww", "wwwwwwww", "wwwwwwww"),
    0x05: tile("wwBBBBBB", "wwwwBBBB", "wwwwwBBB", "wwwwwwBB", "wwgwwwwB", "wwwwwwwB", "wwwwwwww", "wwwwwwww"),
    0x06: tile("wwwwwwww", "wwwwwwww", "Bwwwwwww", "Bwwwgwww", "BBwwwwww", "BBBwwwww", "BBBBwwww", "BBBBBBww"),
    0x07: tile("wwwwwwww", "wwwwwwww", "wwwwwwwB", "wwwwwwwB", "wwwwwwBB", "wwwwwBBB", "wwwwBBBB", "wwBBBBBB"),
    0x08: tile("BBBBBBBB", "BBBBBBBB", "BBBBBBdd", "BBBBdddd", "BBdddddd", "Bddddddd", "dddddddd", "dddddddd"),
    0x09: fill(13),
    0x0a: tile("BBBBBBBB", "BBBBBBBB", "BBdddddd", "Bddddddd", "dddddddd", "dddddddd", "dddddddd", "dddddddd"),
    0x0b: tile("BBBBBBBB", "BBBBBBBB", "dddBBBBB", "ddddddBB", "dddddddd", "dddddddd", "dddddddd", "dddddddd"),
    0x0c: tile("BBBBBBBB", "B......B", "B.....B.", "........", "........", "........", "........", "........"),
    0x0d: tile("........", ".oo..oo.", ".oo..oo.", "........", "........", ".oo.....", ".oo.....", "........"),
    0x0e: tile("........", "........", "........", "........", "........", "........", "........", "........"),
    0x0f: tile("........", ".....oo.", ".....oo.", "........", "........", "........", "........", "........"),
    0x10: tile("gggRgggg", "gggRgggg", "RRRRRRRR", "gRgggggg", "gRgggggg", "RRRRRRRR", "gggRgggg", "RRRRRRRR"),
    0x11: tile("gggggggg", "gggggggg", "gggggggg", "gggggggg", "gggggggg", "gggggggg", "gggggggg", "gggggggg"),
    0x12: tile("..BbbB..", "..BbbB..", "..BbbB..", "..BbbB..", "..BbbB..", "..BbbB..", "..BbbB..", "..BbbB.."),
    0x13: tile("........", "........", "BBBBBBBB", "bbbbbbbb", "bbbbbbbb", "BBBBBBBB", "........", "........"),
    0x14: tile("gggggggg", "gBBBBBBg", "gBsBBBBg", "gBBBBBBg", "gBBBBBBg", "gBBBBsBg", "gBBBBBBg", "gggggggg"),
}


def scenery_tiles():
    tiles = [fill(0)] * 256
    for i, t in SCENERY.items():
        tiles[i] = t
    for i in range(0x15, 256):   # the rest: the drawn ones in other colours, for the backs picker
        src = SCENERY[1 + (i * 5) % len(SCENERY)]
        tiles[i] = recolour(src, {5: (i % 15) + 1, 13: ((i * 3) % 15) + 1})
    return tiles


# ----------------------------------------------------------------- sprites

# The player: a round-headed robot explorer (SPIDS1 0-31; frame 24 is the one the editor draws).
HERO_STAND = [
    "          W",
    "          w",
    "        gggggg",
    "       gWWWWWWg",
    "       gWccccWg",
    "       gWcBBcWg",
    "       gWccccWg",
    "        gggggg",
    "         wgw",
    "      ooooooooo",
    "     oRooroorRoo",
    "     oRooooooRoo",
    "     W.oooooo.W",
    "       oooooo",
    "       bbbbbb",
    "       bb  bb",
    "       bb  bb",
    "       bb  bb",
    "       bb  bb",
    "      ggg  ggg",
    "      ggg  ggg",
]
HERO_WALK = HERO_STAND[:14] + [
    "       bbbbbb",
    "      bb    bb",
    "      bb    bb",
    "     bb      bb",
    "     bb      bb",
    "    ggg      ggg",
    "    ggg      ggg",
]
GHOST = [
    "        wwwwww",
    "      wwwwwwwwww",
    "     wwwwwwwwwwww",
    "     ww..ww..wwww",
    "     ww.Rww.Rwwww",
    "     wwwwwwwwwwww",
    "     wwwgwwwwgwww",
    "     wwwwwwwwwwww",
    "     wgwwwwwgwwww",
    "     wwwwwwwwwwww",
    "     wwwwwgwwwwww",
    "     wwwwwwwwwwww",
    "     ww ww ww ww",
    "     w  w  w  w",
]
DROID = [
    "         bb",
    "          b",
    "      bbbbbbbbbb",
    "      bccccccccb",
    "      bcr cc rcb",
    "      bccccccccb",
    "      bbbbbbbbbb",
    "        BbbbB",
    "     bbbbbbbbbbbb",
    "     bBwwwwwwwwBb",
    "     bBwrwwGwwwBb",
    "     bBwwwwwwwwBb",
    "     bbbbbbbbbbbb",
    "       gg    gg",
    "      gggg  gggg",
]
FISH = [
    "           ss",
    "          sss",
    "   s    ssssssss",
    "   ss  sssssssW.s",
    "   sssssssssssssss",
    "   ss  sbsbsbsss",
    "   s    sssssss",
    "          ss",
]
DOG = [
    "               oo",
    "              oooo",
    "              oo.oR",
    "   o          ooooo",
    "    oooooooooooo",
    "    oooooooooooo",
    "    ooRoooooRoo",
    "    oo oo  oo oo",
    "    oo oo  oo oo",
    "    RR RR  RR RR",
]
DINO = [
    "              GGGG",
    "             GGGG.G",
    "             GGGGGGG",
    "             GGG",
    "    G       GGGG",
    "    GG    GGGGGG",
    "     GGGGGGGGGdG",
    "      GGGGGGGGGG",
    "       GGdGGGGG",
    "        GG  GG",
    "        GG  GG",
    "        dd  dd",
]
RAT = [
    "             gg",
    "            gggg",
    "     ggggggggg.gR",
    "   gggggggggggggg",
    "  g gggggggggggg",
    " g   gg  gg  gg",
]
PERSON = [
    "         oooo",
    "        oooooo",
    "        oWWWWo",
    "        oW.W.o",
    "        oWWWWo",
    "         WWWW",
    "        mmmmmm",
    "       mmmmmmmm",
    "       W mmmm W",
    "         mmmm",
    "        mmmmmm",
    "       mmmmmmmm",
    "         W  W",
    "         W  W",
    "        RR  RR",
]
HAT = [
    "       RRRRRRRR",
    "     RRRRRRRRRRRR",
] + PERSON[2:]
CLOAK = [
    "         ssss",
    "        sW.Wss",
    "        ssssss",
    "      pppppppppp",
    "     pppppppppppp",
    "     ppp pppp ppp",
    "     pp  pppp  pp",
    "         pppp",
    "        pppppp",
    "        pppppp",
    "       pppppppp",
    "       pp    pp",
]


def flip(rows):
    return [r.ljust(SPIDS_W)[::-1] for r in rows]


def rotate(rows):
    g = picture(rows, SPIDS_W, SPIDS_H)
    return [[g[SPIDS_H - 1 - y][SPIDS_W - 1 - x] for x in range(SPIDS_W)] for y in range(SPIDS_H)]


def sheet(frames):
    out = bytearray()
    for f in frames:
        g = f if isinstance(f[0], list) else picture(f, SPIDS_W, SPIDS_H)
        rec = ega_planar(g, SPIDS_W, SPIDS_H)
        out += rec + bytes(SPIDS_STRIDE - len(rec))
    return bytes(out)


def sprite_sheets():
    hero = [HERO_STAND, HERO_WALK, flip(HERO_STAND), flip(HERO_WALK)]
    s1 = [hero[i % 4] for i in range(32)]
    s1[24] = HERO_STAND
    s1 += [GHOST, flip(GHOST)] * 3                     # 32-37 mummy
    s1 += [DROID, flip(DROID), DROID, flip(DROID), DROID]   # 38-42 blue man
    s1 += [FISH, flip(FISH), FISH]                     # 43-45 shark
    s1 += [DOG, flip(DOG), DOG, flip(DOG)]             # 46-49 wolf
    s1 += [DROID, flip(DROID), DROID]                  # 50-52 R2D2
    s1 += [DINO, flip(DINO), DINO, flip(DINO), DINO]   # 53-57 dinosaur
    s1 += [RAT, flip(RAT), RAT]                        # 58-60 rat
    s1 += [DOG, PERSON, flip(PERSON)]                  # 61 wolf, 62-63 Mary Jane
    s2 = [HAT, flip(HAT)] * 3                          # 0-5 shotgun lady
    s2 += [CLOAK, flip(CLOAK), CLOAK, flip(CLOAK), CLOAK]   # 6-10 Mysterio
    s2 += [HERO_STAND] * 21                            # 11-31
    s2 += [rotate(hero[i % 4]) for i in range(32)]     # 32-63 the player upside down
    assert len(s1) == 64 and len(s2) == 64
    return sheet(s1), sheet(s2)


# --------------------------------------------------------------- BACKS.SPL

def night_sky():
    """Background 1: stars and a moon over hills."""
    b = [[1] * 32 for _ in range(22)]
    for i, (x, y) in enumerate([(3, 2), (11, 1), (19, 4), (27, 2), (7, 7), (23, 9), (14, 6), (30, 7), (1, 11)]):
        b[y][x] = 3 if i % 3 == 0 else 2
    b[2][24], b[2][25], b[3][24], b[3][25] = 4, 5, 6, 7
    for x in range(32):
        top = 15 + (2 if 8 <= x < 20 else 0) + (x % 5 == 0)
        b[top][x] = 8 if x % 3 else 0x0a + x % 2
        for y in range(top + 1, 22):
            b[y][x] = 9
    for x in (5, 6, 7, 26, 27):   # lit windows on two distant towers
        for y in range(10, 16):
            b[y][x] = 0x0d if (x + y) % 3 else 0x0f
    return [t for row in b for t in row]


def workshop():
    """Background 2: an indoor wall with a pipe and a window."""
    b = [[0x10] * 32 for _ in range(22)]
    for x in range(32):
        b[4][x] = 0x13
    for y in range(22):
        b[y][21] = 0x12
    for y in range(8, 12):
        for x in range(6, 12):
            b[y][x] = 0x14
    return [t for row in b for t in row]


def spl_literal(data):
    """One .SPL block that any decoder reads back as `data`: marker 0, then three marker bytes no
    data byte uses (so every data byte is a literal), then the bytes, with runs folded by marker 1."""
    used = set(data)
    free = [m for m in range(1, 255) if m not in used][:3]
    out = bytearray([0] + free)
    i = 0
    while i < len(data):
        run = 1
        while i + run < len(data) and data[i + run] == data[i] and run < 0x81:
            run += 1
        out.append(data[i])
        if run >= 3:
            out += bytes([free[0], run - 3])   # repeat the last byte (n + 2) more times
        else:
            out += bytes(data[i + 1:i + run])
        i += run
    return bytes(out)


def backs_spl():
    blocks = {1: night_sky(), 2: workshop()}
    header = bytearray(0x82)
    body = bytearray()
    for bid in range(64):
        off = 0x82 + len(body)
        header[bid * 2:bid * 2 + 2] = off.to_bytes(2, "big")
        if bid in blocks:
            body += spl_literal(bytes(blocks[bid]))
    header[0x80:0x82] = (0x82 + len(body)).to_bytes(2, "big")
    return bytes(header + body)


# ------------------------------------------------------------- MOVERTAB.DAT

def movertab(rooms_txt):
    """Each object's default entry (what the editor's Ctrl-O gives a new one), in global object
    order: rooms by number, objects in order."""
    rooms = {}
    room = None
    for line in open(rooms_txt):
        m = re.match(r"Room (\d+):", line)
        if m:
            room = int(m.group(1))
            rooms[room] = []
        m = re.match(r"    (block|sprite) \d+:", line)
        if m and room is not None:
            rooms[room].append(m.group(1))
    table = bytearray(256 * 3)
    n = 0
    for r in sorted(rooms):
        for kind in rooms[r]:
            table[n * 3:n * 3 + 3] = b"\x7f\xff\xb8" if kind == "sprite" else b"\x00\x00\x88"
            n += 1
    return bytes(table)


def main(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    files = {
        "BLOCKS.EGA": b"".join(ega_planar(t, 8, 8) for t in foreground_tiles()),
        "SCENERY.EGA": b"".join(ega_planar(t, 8, 8) for t in scenery_tiles()),
        "BACKS.SPL": backs_spl(),
        "MOVERTAB.DAT": movertab(os.path.join(HERE, "rooms.txt")),
    }
    files["SPIDS1.EGA"], files["SPIDS2.EGA"] = sprite_sheets()
    for name, data in files.items():
        with open(os.path.join(out_dir, name), "wb") as f:
            f.write(data)
    assert len(files["BLOCKS.EGA"]) == 512 * 32 and len(files["SCENERY.EGA"]) == 256 * 32
    assert len(files["SPIDS1.EGA"]) == len(files["SPIDS2.EGA"]) == 64 * SPIDS_STRIDE


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
