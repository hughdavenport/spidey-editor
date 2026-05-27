#!/usr/bin/env python3
"""Render a game room to PNG: background (BACKS.SPL+SCENERY) + foreground
(ROOMS.SPL+BLOCKS) + sprites (SPIDS1/2), in true colours.

Usage:
    python3 render_room.py <roomid> <out.png>
    python3 render_room.py all <out_dir>      # all 64 rooms + montage

All EGA graphics use plane order (3,2,0,1) and the EXE-derived palette
(game_palette.py). Sprite transparency key = colour 15; foreground-block
transparency key = colour 0 (shows the background through).
"""
import re
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).parent
SPC = ROOT / "Spider-claude"
sys.path.insert(0, str(SPC))
from game_palette import GAME_PALETTE as PAL  # noqa: E402

AOUT = str(ROOT / "a.out")
ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")
SPRITE_RE = re.compile(r"sprite \d+: \(x,y\)=(\d+),(\d+) (\w+)")
W, H, TILE = 32, 22, 8
PLANE_ORDER = (3, 2, 0, 1)          # file plane -> EGA colour bit (tiles & sprites)
SPIDS_PLANE, SPIDS_STRIDE = 63, 256
SPIDS_W, SPIDS_H = 24, 21
PLAYER_ROOM, PLAYER_POS, PLAYER_FRAME = 1, (3, 20), 0   # Spidey only in room 1

SPRITE_FRAME = {
    "SHARK": ("SPIDS1.EGA", 43), "MUMMY": ("SPIDS1.EGA", 32),
    "BLUE_MAN": ("SPIDS2.EGA", 6), "WOLF": ("SPIDS1.EGA", 46),
    "R2D2": ("SPIDS1.EGA", 50), "DINOSAUR": ("SPIDS1.EGA", 53),
    "RAT": ("SPIDS1.EGA", 58), "SHOTGUN_LADY": ("SPIDS2.EGA", 0),
}
_cache: dict[str, bytes] = {}
def _f(name): return _cache.setdefault(name, (SPC / name).read_bytes())


def decode_tile(data, idx, key):
    """8x8 sequential-planar tile -> RGBA; pixels == key are transparent."""
    img = Image.new("RGBA", (8, 8), (0, 0, 0, 0))
    px = img.load()
    base = idx * 32
    for y in range(8):
        for x in range(8):
            c = 0
            for p in range(4):
                c |= ((data[base + p * 8 + y] >> (7 - x)) & 1) << PLANE_ORDER[p]
            if c != key:
                px[x, y] = (*PAL[c], 255)
    return img


def decode_sprite(fname, frame):
    data = _f(fname)
    img = Image.new("RGBA", (SPIDS_W, SPIDS_H), (0, 0, 0, 0))
    px = img.load()
    base = frame * SPIDS_STRIDE
    for y in range(SPIDS_H):
        for x in range(SPIDS_W):
            c = 0
            for slot in range(4):
                o = base + slot * SPIDS_PLANE + y * 3 + x // 8
                if o < len(data):
                    c |= ((data[o] >> (7 - (x % 8))) & 1) << PLANE_ORDER[slot]
            if c != 15:                       # 15 = sprite transparency key
                px[x, y] = (*PAL[c], 255)
    return img


def _s8(b): return b - 256 if b >= 128 else b
def backs_tilemap(rid, target=704):
    """Decompress a BACKS.SPL room (same LZ/RLE as ROOMS.SPL; see spl.c)."""
    d = _f("BACKS.SPL")
    offs = [(d[i * 2] << 8) | d[i * 2 + 1] for i in range(64)]
    file_size = (d[0x80] << 8) | d[0x81]
    start = offs[rid]
    end = offs[rid + 1] if rid + 1 < 64 else file_size
    if end <= start:
        return [0] * target
    m, i, out = d[start:start + 4], start + 4, []
    def rd():
        nonlocal i; v = d[i]; i += 1; return v
    def back(n):                     # safe back-reference (0 if out too short)
        return out[-n] if 0 < n <= len(out) else 0
    while len(out) < target and i < end:
        val = rd()
        if val == m[1]:
            nx = rd()
            if nx < 0x80: out += [back(1)] * (nx + 2)
            elif nx != 0x80:
                for _ in range(-_s8(nx)): out.append(back(0x20))
            else: out.append(val)
        elif val == m[2]:
            nx = rd()
            if nx < 0x80: out += [m[0]] * (nx + 2)
            elif nx != 0x80:
                bk = rd()
                for _ in range(-_s8(nx) + 1): out.append(back(bk))
            else: out.append(val)
        elif val == m[3]:
            nx = rd()
            if nx < 0x80:
                bk = rd() | 0x200
                for _ in range(nx + 2): out.append(back(bk))
            elif nx != 0x80:
                bk = rd() | 0x100
                for _ in range(-_s8(nx) + 1): out.append(back(bk))
            else: out.append(val)
        else:
            out.append(val)
    return (out + [0] * target)[:target]


def parse_grid(text):
    lines = ANSI_RE.sub("", text).splitlines()
    start = next((i for i, l in enumerate(lines) if l.strip() == "Tiles:"), None)
    if start is None:
        return [[0] * W for _ in range(H)]
    grid = []
    for line in lines[start + 1: start + 1 + H]:
        line = line.ljust(W * 2)[: W * 2]
        grid.append([int(line[c * 2:c * 2 + 2], 16) if line[c * 2:c * 2 + 2].strip() else 0
                     for c in range(W)])
    return grid


def parse_sprites(text):
    return [(int(m[1]), int(m[2]), m[3]) for m in SPRITE_RE.finditer(ANSI_RE.sub("", text))]


def render_room(rid):
    out = subprocess.run([AOUT, "display", str(rid)], capture_output=True, text=True, timeout=20).stdout
    grid, sprites = parse_grid(out), parse_sprites(out)
    canvas = Image.new("RGBA", (W * TILE, H * TILE), (0, 0, 0, 255))
    scenery, blocks = _f("SCENERY.EGA"), _f("BLOCKS.EGA")
    # background: SCENERY, opaque (colour 0 = black night sky)
    bg = backs_tilemap(rid)
    for ty in range(H):
        for tx in range(W):
            v = bg[ty * W + tx] & 0xff
            if v:
                canvas.alpha_composite(decode_tile(scenery, v, None), (tx * TILE, ty * TILE))
    # foreground: BLOCKS, colour 0 transparent (background shows through)
    for ty, row in enumerate(grid):
        for tx, raw in enumerate(row):
            v = raw & 0x3f
            if v:
                canvas.alpha_composite(decode_tile(blocks, v, 0), (tx * TILE, ty * TILE))
    # sprites + player
    drawn = list(sprites)
    if rid == PLAYER_ROOM:
        canvas.alpha_composite(decode_sprite("SPIDS1.EGA", PLAYER_FRAME),
                               (PLAYER_POS[0] * TILE, max(0, (PLAYER_POS[1] + 1) * TILE - SPIDS_H)))
    for x, y, name in drawn:
        if name in SPRITE_FRAME:
            spr = decode_sprite(*SPRITE_FRAME[name])
            canvas.alpha_composite(spr, (x * TILE, max(0, (y + 1) * TILE - SPIDS_H)))
    return canvas.convert("RGB")


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "all":
        out_dir = Path(sys.argv[2]); out_dir.mkdir(parents=True, exist_ok=True)
        tw, th = W * TILE, H * TILE
        montage = Image.new("RGB", (8 * (tw // 2 + 4) + 4, 8 * (th // 2 + 4) + 4), (40, 40, 48))
        for rid in range(64):
            img = render_room(rid)
            img.resize((tw * 3, th * 3), Image.Resampling.NEAREST).save(out_dir / f"room{rid:02d}.png")
            montage.paste(img.resize((tw // 2, th // 2)), (4 + (rid % 8) * (tw // 2 + 4), 4 + (rid // 8) * (th // 2 + 4)))
        montage.save(out_dir / "montage.png")
        print(f"wrote 64 rooms + montage -> {out_dir}")
    else:
        rid = int(sys.argv[1]) if len(sys.argv) > 1 else 1
        out = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("room.png")
        render_room(rid).resize((W * TILE * 3, H * TILE * 3), Image.Resampling.NEAREST).save(out)
        print(f"wrote {out}")


if __name__ == "__main__":
    main()
