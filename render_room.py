#!/usr/bin/env python3
"""Render a game room to PNG: background (BACKS.SPL+SCENERY) + foreground
(ROOMS.SPL+BLOCKS) + sprites (SPIDS1/2), in true colours.

Usage:
    python3 render_room.py <roomid> <out.png>
    python3 render_room.py all <out_dir>      # all 64 rooms + montage

All EGA graphics use plane order (3,2,1,0) and the EXE-derived palette
(game_palette.py). Sprite transparency key = colour 15;
foreground-block transparency key = colour 0 (shows the background through).
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
SPRITE_RE = re.compile(r"sprite \d+: \(x,y\)=(\d+),(\d+) (\w+) dmg:(\d+)")
W, H, TILE = 32, 22, 8
SPRITE_ORDER = (3, 2, 1, 0)         # SPIDS plane -> EGA colour bit
TILE_ORDER = (3, 2, 1, 0)           # BLOCKS/SCENERY plane -> bit (B/G swapped vs sprites)
SPIDS_PLANE, SPIDS_STRIDE = 63, 256
SPIDS_W, SPIDS_H = 24, 21
PLAYER_ROOM, PLAYER_FEET = 1, (3, 20)  # Spidey only appears in room 1; feet tile
PLAYER_START_FRAME = 9                  # his resting/start pose in SPIDS1

# Each creature's animation frame range (file, first, last-inclusive). Frame
# ranges verified from the SPIDS contact sheets; SPIDS1 32-37 is the white
# mummy and 38-42 is the distinct blue-man figure (not extra mummy frames).
CREATURE_FRAMES = {
    "SPIDEY":       ("SPIDS1.EGA", 0, 31),
    "MUMMY":        ("SPIDS1.EGA", 32, 37),
    "BLUE_MAN":     ("SPIDS1.EGA", 38, 42),
    "SHARK":        ("SPIDS1.EGA", 43, 45),
    "WOLF":         ("SPIDS1.EGA", 46, 49),
    "R2D2":         ("SPIDS1.EGA", 50, 52),
    "DINOSAUR":     ("SPIDS1.EGA", 53, 57),
    "RAT":          ("SPIDS1.EGA", 58, 61),
    "MARY_JANE":    ("SPIDS1.EGA", 62, 63),
    "SHOTGUN_LADY": ("SPIDS2.EGA", 0, 5),
    "MYSTERIO":     ("SPIDS2.EGA", 6, 10),
}


def resolve_creature(type_name, dmg):
    """Map an editor sprite (type + damage) to the creature actually drawn.
    The SHARK type is overloaded by damage (see room.h): dmg 3 = Mysterio,
    dmg 4 = Mary Jane, otherwise a shark."""
    if type_name == "SHARK":
        return {3: "MYSTERIO", 4: "MARY_JANE"}.get(dmg, "SHARK")
    return type_name
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
                c |= ((data[base + p * 8 + y] >> (7 - x)) & 1) << TILE_ORDER[p]
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
                    c |= ((data[o] >> (7 - (x % 8))) & 1) << SPRITE_ORDER[slot]
            if c != 15:                       # 15 = sprite transparency key
                px[x, y] = (*PAL[c], 255)
    return img


def _s8(b): return b - 256 if b >= 128 else b
def backs_tilemap(bid, target=704):
    """Decompress BACKS.SPL block `bid` (the room's `bkgrnd` metadata value,
    NOT its room id). Many rooms share a background, selected by `bkgrnd`.
    Same LZ/RLE format as ROOMS.SPL; see spl.c."""
    d = _f("BACKS.SPL")
    offs = [(d[i * 2] << 8) | d[i * 2 + 1] for i in range(64)]
    file_size = (d[0x80] << 8) | d[0x81]
    start = offs[bid]
    end = offs[bid + 1] if bid + 1 < 64 else file_size
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
    """-> list of (x, y, creature) using the type+damage resolution."""
    return [(int(m[1]), int(m[2]), resolve_creature(m[3], int(m[4])))
            for m in SPRITE_RE.finditer(ANSI_RE.sub("", text))]


def creature_frame(creature, phase):
    """Pick a frame for a creature, cycling through its range by `phase`."""
    fname, lo, hi = CREATURE_FRAMES[creature]
    return fname, lo + (phase % (hi - lo + 1))


TILE_OFFSET_RE = re.compile(r"tiles:\s*(\d+)")
BKGRND_RE = re.compile(r"bkgrnd:\s*(\d+)")


def room_display(rid):
    return subprocess.run([AOUT, "display", str(rid)], capture_output=True, text=True, timeout=20).stdout


def render_room(rid, phase=0, out=None):
    if out is None:
        out = room_display(rid)
    grid, sprites = parse_grid(out), parse_sprites(out)
    m = TILE_OFFSET_RE.search(ANSI_RE.sub("", out))
    # tile_offset advances in steps of 4 per 64-tile set (0,4,8,..,28 = 8 sets
    # = BLOCKS' 512 tiles), so one unit = 16 tiles.
    tile_offset = int(m[1]) if m else 0
    mb = BKGRND_RE.search(ANSI_RE.sub("", out))
    bkgrnd = int(mb[1]) if mb else rid       # background block selector
    canvas = Image.new("RGBA", (W * TILE, H * TILE), (0, 0, 0, 255))
    scenery, blocks = _f("SCENERY.EGA"), _f("BLOCKS.EGA")
    # background: SCENERY, opaque (colour 0 = black night sky)
    bg = backs_tilemap(bkgrnd)
    for ty in range(H):
        for tx in range(W):
            v = bg[ty * W + tx] & 0xff
            if v:
                canvas.alpha_composite(decode_tile(scenery, v, None), (tx * TILE, ty * TILE))
    # blank the tile cell each sprite sits on (display marks it; it isn't a tile)
    for x, y, _ in sprites:
        if 0 <= y < H and 0 <= x < W:
            grid[y][x] = 0
    # foreground: BLOCKS. Tile index = (raw & 0x3f) within the 64-tile set
    # selected by tile_offset (one unit = 16 tiles); high bits of raw are
    # attributes (solid/magnetic/slippery), not part of the tile index.
    # Foreground tiles are opaque (no colour key) — the game does no alpha
    # blending; only tile index 0 ("invisible") shows the background through.
    for ty, row in enumerate(grid):
        for tx, raw in enumerate(row):
            v = raw & 0x3f
            if v:
                canvas.alpha_composite(decode_tile(blocks, v + tile_offset * 16, None),
                                       (tx * TILE, ty * TILE))
    # sprites + player; `phase` cycles each creature through its frame range.
    # blit at a *feet tile* (fx, fy): left edge at column fx, feet resting on
    # the bottom of row fy. An enemy's stored (x,y) is one tile up-left of its
    # feet tile, so its feet tile is (x+1, y+1). The player's feet tile is
    # given directly (PLAYER_FEET).
    def blit(spr, fx, fy):
        canvas.alpha_composite(spr, (fx * TILE, max(0, (fy + 1) * TILE - SPIDS_H)))
    if rid == PLAYER_ROOM:
        blit(decode_sprite(*creature_frame("SPIDEY", PLAYER_START_FRAME + phase)), *PLAYER_FEET)
    for x, y, creature in sprites:
        blit(decode_sprite(*creature_frame(creature, phase)), x + 1, y + 1)
    return canvas.convert("RGB")


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "cycle":
        # animated GIF cycling every sprite through all its frames
        rid = int(sys.argv[2])
        out = Path(sys.argv[3]) if len(sys.argv) > 3 else Path(f"room{rid:02d}.gif")
        frames = [render_room(rid, p).resize((W * TILE * 2, H * TILE * 2), Image.Resampling.NEAREST)
                  for p in range(8)]
        frames[0].save(out, save_all=True, append_images=frames[1:], duration=160, loop=0)
        print(f"wrote {out}")
        return
    if len(sys.argv) >= 3 and sys.argv[1] == "tilesets":
        # one folder per used tile_offset, one PNG per tile (00..3f) in that set
        root = Path(sys.argv[2]); root.mkdir(parents=True, exist_ok=True)
        blocks = _f("BLOCKS.EGA")
        for off in (0, 4, 8, 12, 16, 20, 24, 28):
            d = root / f"offset{off:02d}"; d.mkdir(exist_ok=True)
            for t in range(64):
                img = decode_tile(blocks, t + off * 16, None).convert("RGB")
                img.resize((64, 64), Image.Resampling.NEAREST).save(d / f"tile{t:02x}.png")
        print(f"wrote 8 offset folders x 64 tiles -> {root}")
        return
    if len(sys.argv) >= 3 and sys.argv[1] == "roomframes":
        # folder per used tile_offset; inside, a full room render per sprite
        # animation frame (walking/standing/etc). Frame count per room = the
        # longest frame range among its sprites (incl. the player in room 1).
        root = Path(sys.argv[2]); root.mkdir(parents=True, exist_ok=True)
        tw, th = W * TILE, H * TILE
        for rid in range(64):
            out = room_display(rid)
            mo = TILE_OFFSET_RE.search(ANSI_RE.sub("", out))
            off = int(mo[1]) if mo else 0
            creatures = [c for _, _, c in parse_sprites(out)]
            if rid == PLAYER_ROOM:
                creatures.append("SPIDEY")
            nframes = max((CREATURE_FRAMES[c][2] - CREATURE_FRAMES[c][1] + 1
                           for c in creatures), default=1)
            d = root / f"offset{off:02d}"; d.mkdir(exist_ok=True)
            for p in range(nframes):
                render_room(rid, p, out).resize((tw * 3, th * 3), Image.Resampling.NEAREST) \
                    .save(d / f"room{rid:02d}_f{p:02d}.png")
        print(f"wrote per-offset room frames -> {root}")
        return
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
