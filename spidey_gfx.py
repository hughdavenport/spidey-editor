#!/usr/bin/env python3
"""Decoders for the graphics files of The Amazing Spider-Man (Paragon, 1990),
plus a room compositor. See GRAPHICS.md for the format write-up.

Every EGA file is 4 bit-planes of 1 bit per pixel, MSB = leftmost pixel.
Three layouts occur (each verified pixel-for-pixel against DOSBox-X captures
of the running game by verify_gfx.py):

  tiles   BLOCKS/SCENERY/CHARSET.EGA: 8x8 tiles, 32 bytes each, plane-major
          (8 bytes of plane 0, then plane 1, 2, 3).
  screen  LOADSCRN/WINSCRN/PAGE1-4 (320x200), HISCORE (320x38),
          SPIDFIGR/SKELFIGR (64x192): row-planar: for each row, the four
          plane rows back to back (width/8 bytes each).
  sprite  SPIDS1/SPIDS2.EGA: 64 frames of 24x21, plane-major within the
          frame (4 planes x 63 bytes), 256-byte frame stride (4 pad bytes).
          Colour 15 is the transparency key.

In every layout the first plane stored is the intensity bit and the last is
blue, i.e. planes are stored I,R,G,B (PLANE_ORDER maps stored plane -> bit).

The CGA files are the same pictures packed 2 bits per pixel (native CGA
framebuffer bytes), same structure, half the bytes; colour 3 is the sprite
key. The 16-colour palette is the DAC table the game programs on VGA, read
out of the EXEPACK-unpacked SPIDEY.EXE; EGA_PALETTE is what the EGA video
mode shows instead.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent
AOUT = ROOT / "a.out"

# Stored plane k contributes colour bit PLANE_ORDER[k]. Verified by verify_gfx.py
# against emulator captures (the natural B,G,R,I order swaps blue<->red).
PLANE_ORDER = (3, 2, 1, 0)
# CGA files are packed 2 bits per pixel (native CGA framebuffer bytes, MSB =
# leftmost pixel), same tile/frame/row structure as the EGA files, half the
# bytes. A 2-entry "order" selects that decoding in planar()/row_planar().
CGA_PLANE_ORDER = (1, 0)

# Game palette. SPIDEY.EXE programs the VGA DAC (INT 10h AH=10h) from a table
# of 16 six-bit RGB triplets at DS:0x97AE (DS=0x0FB9 in the unpacked image):
#   00 00 00  00 18 30  38 10 00  38 38 38  18 18 18  00 10 28  28 08 00  28 28 28
#   18 38 38  08 30 10  30 10 38  38 20 00  00 28 38  00 18 08  18 08 20  38 30 00
VGA_DAC_6BIT = [
    (0, 0, 0),
    (0, 24, 48),
    (56, 16, 0),
    (56, 56, 56),
    (24, 24, 24),
    (0, 16, 40),
    (40, 8, 0),
    (40, 40, 40),
    (24, 56, 56),
    (8, 48, 16),
    (48, 16, 56),
    (56, 32, 0),
    (0, 40, 56),
    (0, 24, 8),
    (24, 8, 32),
    (56, 48, 0),
]
PALETTE = [tuple(round(v * 255 / 63) for v in rgb) for rgb in VGA_DAC_6BIT]

# On a real EGA (video mode 2) the game instead loads the 16 attribute
# registers from the 17-byte table at DS:0x978C (last byte = overscan):
#   00 f9 fc ff f8 01 04 07 fb fa fd 06 03 02 05 fe 00
# Only the low six bits matter (rgbRGB: bits 5..3 = secondary/low-intensity
# r,g,b; bits 2..0 = primary R,G,B), giving the EGA 64-colour palette entry.
EGA_ATTR = [0x00, 0xF9, 0xFC, 0xFF, 0xF8, 0x01, 0x04, 0x07, 0xFB, 0xFA, 0xFD, 0x06, 0x03, 0x02, 0x05, 0xFE]


def ega64(attr: int) -> tuple[int, int, int]:
    """EGA 6-bit rgbRGB attribute value -> RGB (0x55 per secondary, 0xAA per primary).
    Value 6 is the classic brown, as VGA's default DAC (and DOSBox) shows it."""
    if attr == 0x06:
        return (0xAA, 0x55, 0x00)
    r = 0xAA * ((attr >> 2) & 1) + 0x55 * ((attr >> 5) & 1)
    g = 0xAA * ((attr >> 1) & 1) + 0x55 * ((attr >> 4) & 1)
    b = 0xAA * (attr & 1) + 0x55 * ((attr >> 3) & 1)
    return (r, g, b)


EGA_PALETTE = [ega64(a & 0x3F) for a in EGA_ATTR]

SPRITE_KEY = 15
CGA_SPRITE_KEY = 3  # CGA sprites: colour 3 (white) is the transparency key
SPRITE_W, SPRITE_H, SPRITE_PLANE, SPRITE_STRIDE = 24, 21, 63, 256
TILE_BYTES = 32
ROOM_W, ROOM_H, TILE = 32, 22, 8

SCREEN_SIZES = {
    "LOADSCRN": (320, 200),
    "WINSCRN": (320, 200),
    "PAGE1": (320, 200),
    "PAGE2": (320, 200),
    "PAGE3": (320, 200),
    "PAGE4": (320, 200),
    "HISCORE": (320, 38),
    "SPIDFIGR": (64, 192),
    "SKELFIGR": (64, 192),
}
TILE_FILES = ("BLOCKS", "SCENERY", "CHARSET")
SPRITE_FILES = ("SPIDS1", "SPIDS2")

# ------------------------------------------------------------------ bit-plane decoding


def _bits(data: np.ndarray) -> np.ndarray:
    """uint8 array (..., nbytes) -> (..., nbytes*8) of 0/1, MSB first."""
    return np.unpackbits(data, axis=-1)


def packed2(data: bytes, w: int, h: int) -> np.ndarray:
    """Packed 2bpp rows (CGA memory layout): 4 pixels per byte, MSB pair first -> (h, w)."""
    a = np.frombuffer(data, dtype=np.uint8, count=w * h // 4).reshape(h, w // 4)
    bits = _bits(a).reshape(h, w, 2)
    return ((bits[..., 0] << 1) | bits[..., 1]).astype(np.uint8)


def planar(data: bytes, w: int, h: int, order=PLANE_ORDER, plane_bytes: int | None = None) -> np.ndarray:
    """Plane-major block: all rows of plane 0, then plane 1, ... -> (h, w) colour indices."""
    if len(order) == 2:
        return packed2(data, w, h)
    bpr = w // 8
    pb = plane_bytes or bpr * h
    a = np.frombuffer(data, dtype=np.uint8, count=pb * len(order))
    a = a.reshape(len(order), pb)[:, : bpr * h].reshape(len(order), h, bpr)
    bits = _bits(a)  # (planes, h, w)
    out = np.zeros((h, w), dtype=np.uint8)
    for k, bit in enumerate(order):
        out |= (bits[k] << bit).astype(np.uint8)
    return out


def row_planar(data: bytes, w: int, h: int, order=PLANE_ORDER) -> np.ndarray:
    """Row-planar image: for each row, one row of every plane -> (h, w) colour indices."""
    if len(order) == 2:
        return packed2(data, w, h)
    bpr = w // 8
    a = np.frombuffer(data, dtype=np.uint8, count=bpr * h * len(order))
    a = a.reshape(h, len(order), bpr)
    bits = _bits(a)  # (h, planes, w)
    out = np.zeros((h, w), dtype=np.uint8)
    for k, bit in enumerate(order):
        out |= (bits[:, k, :] << bit).astype(np.uint8)
    return out


def decode_tiles(data: bytes, order=PLANE_ORDER) -> np.ndarray:
    """Whole tile file -> (ntiles, 8, 8) colour indices."""
    n = len(data) // (8 * len(order))
    step = 8 * len(order)
    return np.stack([planar(data[i * step : (i + 1) * step], 8, 8, order) for i in range(n)])


def decode_sprites(data: bytes, order=PLANE_ORDER) -> np.ndarray:
    """Whole SPIDS file -> (64, 21, 24) colour indices (15 = transparent)."""
    stride = SPRITE_STRIDE if len(order) == 4 else SPRITE_STRIDE // 2
    n = len(data) // stride
    return np.stack(
        [
            planar(data[i * stride : (i + 1) * stride], SPRITE_W, SPRITE_H, order, plane_bytes=SPRITE_PLANE)
            for i in range(n)
        ]
    )


def decode_screen(data: bytes, w: int, h: int, order=PLANE_ORDER) -> np.ndarray:
    return row_planar(data, w, h, order)


# ------------------------------------------------------------------ images


def to_rgb(idx: np.ndarray, palette=PALETTE, key: int | None = None) -> Image.Image:
    """Index array -> PIL image (RGBA with `key` transparent, else RGB)."""
    pal = np.array(palette, dtype=np.uint8)
    rgb = pal[idx]
    if key is None:
        return Image.fromarray(rgb, "RGB")
    alpha = np.where(idx == key, 0, 255).astype(np.uint8)[..., None]
    return Image.fromarray(np.concatenate([rgb, alpha], axis=-1), "RGBA")


def sheet(cells: np.ndarray, per_row: int, pad: int = 1, bg: int = 0) -> np.ndarray:
    """Tile (n,h,w) index arrays into one index image, `pad` pixels of `bg` between."""
    n, h, w = cells.shape
    rows = (n + per_row - 1) // per_row
    out = np.full((rows * (h + pad) + pad, per_row * (w + pad) + pad), bg, dtype=np.uint8)
    for i, c in enumerate(cells):
        y, x = divmod(i, per_row)
        out[pad + y * (h + pad) : pad + y * (h + pad) + h, pad + x * (w + pad) : pad + x * (w + pad) + w] = c
    return out


# ------------------------------------------------------------------ BACKS.SPL


def _s8(b: int) -> int:
    return b - 256 if b >= 128 else b


def backs_tilemap(backs: bytes, bid: int, target: int = ROOM_W * ROOM_H) -> list[int]:
    """Decompress background block `bid` of BACKS.SPL (the room's `bkgrnd`
    value) -> 704 SCENERY tile indices. Same LZ/RLE scheme as ROOMS.SPL
    (mirrors spl.c): the block's first 4 bytes are the marker bytes."""
    offs = [(backs[i * 2] << 8) | backs[i * 2 + 1] for i in range(64)]
    size = (backs[0x80] << 8) | backs[0x81]
    start = offs[bid]
    end = offs[bid + 1] if bid + 1 < 64 else size
    if end <= start:
        return [0] * target
    m, i, out = backs[start : start + 4], start + 4, []

    def rd():
        nonlocal i
        v = backs[i]
        i += 1
        return v

    def back(n):
        return out[-n] if 0 < n <= len(out) else 0

    while len(out) < target and i < end:
        val = rd()
        if val == m[1]:
            nx = rd()
            if nx < 0x80:
                out += [back(1)] * (nx + 2)
            elif nx != 0x80:
                out += [back(0x20) for _ in range(-_s8(nx))]
            else:
                out.append(val)
        elif val == m[2]:
            nx = rd()
            if nx < 0x80:
                out += [m[0]] * (nx + 2)
            elif nx != 0x80:
                bk = rd()
                for _ in range(-_s8(nx) + 1):
                    out.append(back(bk))
            else:
                out.append(val)
        elif val == m[3]:
            nx = rd()
            if nx < 0x80:
                bk = rd() | 0x200
                for _ in range(nx + 2):
                    out.append(back(bk))
            elif nx != 0x80:
                bk = rd() | 0x100
                for _ in range(-_s8(nx) + 1):
                    out.append(back(bk))
            else:
                out.append(val)
        else:
            out.append(val)
    return (out + [0] * target)[:target]


# ------------------------------------------------------------------ rooms (via the editor)

ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")
SPRITE_RE = re.compile(r"sprite \d+: \(x,y\)=(\d+),(\d+) (\w+) dmg:(\d+)")
BLOCK_RE = re.compile(r"block \d+: \(x,y\)=(\d+),(\d+) \(w,h\)=(\d+),(\d+)")
META_RE = re.compile(r"bkgrnd:\s*(\d+), tiles:\s*(\d+)")
NAME_RE = re.compile(r'Room \d+: "([^"]*)"')

# Which SPIDS frame range draws each creature (file, first frame, last frame),
# and the frame the game shows when the room is entered (matched 100% in the
# 0.5 s captures; Mysterio is not drawn at all until he materialises).
# The SHARK editor type is overloaded by its damage byte: 3 = Mysterio,
# 4 = Mary Jane, otherwise a shark.
CREATURE_FRAMES = {
    "SPIDEY": ("SPIDS1", 0, 31),
    "MUMMY": ("SPIDS1", 32, 37),
    "BLUE_MAN": ("SPIDS1", 38, 42),
    "SHARK": ("SPIDS1", 43, 45),
    "WOLF": ("SPIDS1", 46, 49),
    "R2D2": ("SPIDS1", 50, 52),
    "DINOSAUR": ("SPIDS1", 53, 57),
    "RAT": ("SPIDS1", 58, 61),
    "MARY_JANE": ("SPIDS1", 62, 63),
    "SHOTGUN_LADY": ("SPIDS2", 0, 5),
    "MYSTERIO": ("SPIDS2", 6, 10),
}
ENTRY_FRAME = {
    "SPIDEY": 24,
    "MUMMY": 35,
    "BLUE_MAN": 38,
    "SHARK": 44,
    "WOLF": 46,
    "R2D2": 50,
    "DINOSAUR": 53,
    "RAT": 58,
    "MARY_JANE": 62,
    "SHOTGUN_LADY": 5,
    "MYSTERIO": 6,
}
PLAYER_START = (3, 20)  # feet tile of Spider-Man in the start room
PLAYER_FRAME = ENTRY_FRAME["SPIDEY"]
SPRITE_X_PAD = 2  # the 24px figure sits 2 tiles in from its box's left edge
SPRITE_X_NUDGE = 3  # ...plus 3 px: measured on the player and on Mary Jane (static NPC)


class Room:
    """One room as the editor's `display` reports it."""

    def __init__(self, text: str):
        text = ANSI_RE.sub("", text)
        lines = text.splitlines()
        m = NAME_RE.search(text)
        self.name = m[1].strip() if m else ""
        start = next(i for i, line in enumerate(lines) if line.strip() == "Tiles:")
        self.grid = np.zeros((ROOM_H, ROOM_W), dtype=np.uint8)
        for y, line in enumerate(lines[start + 1 : start + 1 + ROOM_H]):
            line = line.ljust(ROOM_W * 2)
            for x in range(ROOM_W):
                cell = line[x * 2 : x * 2 + 2]
                if cell.strip():
                    self.grid[y, x] = int(cell, 16)
        mm = META_RE.search(text)
        assert mm, "display output without bkgrnd/tiles metadata"
        self.bkgrnd, self.tile_offset = int(mm[1]), int(mm[2])
        self.blocks = [tuple(int(v) for v in bm.groups()) for bm in BLOCK_RE.finditer(text)]
        self.sprites = []
        for sm in SPRITE_RE.finditer(text):
            x, y, typ, dmg = int(sm[1]), int(sm[2]), sm[3], int(sm[4])
            if typ == "SHARK":
                typ = {3: "MYSTERIO", 4: "MARY_JANE"}.get(dmg, "SHARK")
            self.sprites.append((x, y, typ))
            if 0 <= y < ROOM_H and 0 <= x < ROOM_W:
                self.grid[y, x] = 0  # the grid cell only marks the sprite


def display_room(spl: Path, room: int) -> str:
    """Run the editor's `display` on a ROOMS.SPL-format file (it reads ROOMS.SPL from cwd)."""
    spl = Path(spl).resolve()
    cwd = spl.parent
    if spl.name != "ROOMS.SPL":
        cwd = ROOT / ".spl-view" / spl.stem
        cwd.mkdir(parents=True, exist_ok=True)
        link = cwd / "ROOMS.SPL"
        if not link.exists() or link.resolve() != spl:
            if link.is_symlink() or link.exists():
                link.unlink()
            link.symlink_to(spl)
    r = subprocess.run([str(AOUT), "display", str(room)], cwd=cwd, capture_output=True, text=True, timeout=20)
    return r.stdout


def room_ids(spl: Path) -> list[int]:
    """Rooms with any compressed data (empty slots share the next slot's offset)."""
    d = Path(spl).read_bytes()
    offs = [(d[i * 2] << 8) | d[i * 2 + 1] for i in range(64)]
    size = (d[0x80] << 8) | d[0x81]
    return [i for i in range(64) if (offs[i + 1] if i < 63 else size) > offs[i]]


class Assets:
    """Decoded tile sheets, sprite sheets and BACKS.SPL; `cga` selects the .CGA files."""

    def __init__(self, data_dir: Path, cga: bool = False):
        self.dir = Path(data_dir)
        ext, order = (".CGA", CGA_PLANE_ORDER) if cga else (".EGA", PLANE_ORDER)
        raw = {n: (self.dir / f"{n}{ext}").read_bytes() for n in TILE_FILES + SPRITE_FILES}
        self.blocks = decode_tiles(raw["BLOCKS"], order)
        self.scenery = decode_tiles(raw["SCENERY"], order)
        self.charset = decode_tiles(raw["CHARSET"], order)
        self.sprites = {n: decode_sprites(raw[n], order) for n in SPRITE_FILES}
        self.backs = (self.dir / "BACKS.SPL").read_bytes()
        self.key = CGA_SPRITE_KEY if cga else SPRITE_KEY

    def sprite(self, creature: str, phase: int = 0) -> np.ndarray:
        """Creature's entry frame, advanced `phase` steps through its range."""
        fname, lo, hi = CREATURE_FRAMES[creature]
        return self.sprites[fname][lo + (ENTRY_FRAME[creature] - lo + phase) % (hi - lo + 1)]


def compose_room(room: Room, assets: Assets, player: bool = False, phase: int = 0) -> np.ndarray:
    """Room -> (176, 256) colour-index image: background, foreground, sprites."""
    cv = np.zeros((ROOM_H * TILE, ROOM_W * TILE), dtype=np.uint8)
    bg = backs_tilemap(assets.backs, room.bkgrnd)
    for ty in range(ROOM_H):
        for tx in range(ROOM_W):
            v = bg[ty * ROOM_W + tx]
            if v:
                cv[ty * TILE : (ty + 1) * TILE, tx * TILE : (tx + 1) * TILE] = assets.scenery[v]
    # Foreground tile index = (byte & 0x3f) + tile_offset*16; the high two bits
    # are the tile's attributes (solid / magnetic / slippery). Tiles are opaque;
    # only index 0 lets the background show through.
    for ty in range(ROOM_H):
        for tx in range(ROOM_W):
            v = room.grid[ty, tx] & 0x3F
            if v:
                cv[ty * TILE : (ty + 1) * TILE, tx * TILE : (tx + 1) * TILE] = assets.blocks[
                    v + room.tile_offset * 16
                ]

    def blit(spr: np.ndarray, feet_x: int, feet_y: int) -> None:
        x0 = (feet_x - SPRITE_X_PAD) * TILE + SPRITE_X_NUDGE
        y0 = max(0, (feet_y + 1) * TILE - SPRITE_H)
        for sy in range(SPRITE_H):
            for sx in range(SPRITE_W):
                c = spr[sy, sx]
                x, y = x0 + sx, y0 + sy
                if c != assets.key and 0 <= x < cv.shape[1] and 0 <= y < cv.shape[0]:
                    cv[y, x] = c

    if player:
        blit(assets.sprite("SPIDEY"), *PLAYER_START)
    for x, y, creature in room.sprites:
        blit(assets.sprite(creature, phase), x + 1, y + 1)
    return cv


# ------------------------------------------------------------------ CLI


def dump_all(data_dir: Path, out_dir: Path, palette=PALETTE, scale: int = 1) -> None:
    """Decode every graphics file in data_dir to PNGs in out_dir."""
    out_dir.mkdir(parents=True, exist_ok=True)

    def save(img: Image.Image, name: str) -> None:
        if scale > 1:
            img = img.resize((img.width * scale, img.height * scale), Image.Resampling.NEAREST)
        img.save(out_dir / name)

    for n in TILE_FILES:
        tiles = decode_tiles((data_dir / f"{n}.EGA").read_bytes())
        save(to_rgb(sheet(tiles, 16), palette), f"{n}.png")
    for n in SPRITE_FILES:
        frames = decode_sprites((data_dir / f"{n}.EGA").read_bytes())
        save(to_rgb(sheet(frames, 8, bg=SPRITE_KEY), palette, key=SPRITE_KEY), f"{n}.png")
    for n, (w, h) in SCREEN_SIZES.items():
        p = data_dir / f"{n}.EGA"
        if p.exists():
            save(to_rgb(decode_screen(p.read_bytes(), w, h), palette), f"{n}.png")
    # CGA variants: 2 planes, half the bytes, 4-colour palette (mode 4, palette 1).
    cga_pal = CGA_PALETTE
    for n in TILE_FILES:
        p = data_dir / f"{n}.CGA"
        if p.exists():
            save(to_rgb(sheet(decode_tiles(p.read_bytes(), CGA_PLANE_ORDER), 16), cga_pal), f"{n}.CGA.png")
    for n in SPRITE_FILES:
        p = data_dir / f"{n}.CGA"
        if p.exists():
            frames = decode_sprites(p.read_bytes(), CGA_PLANE_ORDER)
            save(to_rgb(sheet(frames, 8, bg=3), cga_pal), f"{n}.CGA.png")
    for n, (w, h) in SCREEN_SIZES.items():
        p = data_dir / f"{n}.CGA"
        if p.exists():
            save(to_rgb(decode_screen(p.read_bytes(), w, h, CGA_PLANE_ORDER), cga_pal), f"{n}.CGA.png")


# CGA mode 4 palette 1 with high intensity (black, light cyan, light magenta, white).
CGA_PALETTE = [(0, 0, 0), (85, 255, 255), (255, 85, 255), (255, 255, 255)]


def render_rooms(spl: Path, data_dir: Path, out_dir: Path, scale: int = 3, palette=PALETTE) -> None:
    """Render every non-empty room of an SPL file to out_dir/roomNN.png plus a montage."""
    out_dir.mkdir(parents=True, exist_ok=True)
    assets = Assets(data_dir)
    ids = room_ids(spl)
    thumbs = []
    for rid in ids:
        room = Room(display_room(spl, rid))
        cv = compose_room(room, assets, player=(rid == 1))
        img = to_rgb(cv, palette)
        img.resize((img.width * scale, img.height * scale), Image.Resampling.NEAREST).save(
            out_dir / f"room{rid:02d}.png"
        )
        thumbs.append((rid, room.name, img))
        print(f"room {rid:2d} {room.name}")
    cols = 4 if len(ids) <= 16 else 8
    tw, th, gap, label = ROOM_W * TILE, ROOM_H * TILE, 6, 12
    rows = (len(thumbs) + cols - 1) // cols
    from PIL import ImageDraw

    montage = Image.new("RGB", (cols * (tw + gap) + gap, rows * (th + gap + label) + gap), (24, 24, 30))  # type: ignore[arg-type]
    draw = ImageDraw.Draw(montage)
    for i, (rid, name, img) in enumerate(thumbs):
        x = gap + (i % cols) * (tw + gap)
        y = gap + (i // cols) * (th + gap + label)
        draw.text((x, y), f"{rid:02d} {name}", fill=(230, 230, 230))
        montage.paste(img, (x, y + label))
    montage.save(out_dir / "montage.png")
    print(f"wrote {len(ids)} rooms + montage -> {out_dir}")


def main(argv: list[str]) -> int:
    import argparse

    ap = argparse.ArgumentParser(description=(__doc__ or "").splitlines()[0])
    ap.add_argument("--data-dir", default=str(ROOT / "Spider"), help="directory with the game files")
    ap.add_argument("--ega", action="store_true", help="use the EGA-monitor palette instead of VGA")
    sub = ap.add_subparsers(dest="cmd", required=True)
    d = sub.add_parser("dump", help="decode every graphics file to PNG")
    d.add_argument("out_dir")
    d.add_argument("--scale", type=int, default=1)
    r = sub.add_parser("rooms", help="render every room of an SPL file")
    r.add_argument("spl")
    r.add_argument("out_dir")
    r.add_argument("--scale", type=int, default=3)
    a = ap.parse_args(argv)
    pal = EGA_PALETTE if a.ega else PALETTE
    if a.cmd == "dump":
        dump_all(Path(a.data_dir), Path(a.out_dir), pal, a.scale)
    else:
        render_rooms(Path(a.spl), Path(a.data_dir), Path(a.out_dir), a.scale, pal)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
