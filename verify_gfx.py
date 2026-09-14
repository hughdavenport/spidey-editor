#!/usr/bin/env python3
"""Check spidey_gfx decoding against emulator ground truth.

Ground truth = DOSBox-X window captures (320x200 doubled to 640x400) of the
real game: the title screen, the four story pages, and every room booted as
the start room. Each capture is reduced to palette indices (nearest game
colour) and compared pixel-for-pixel with what spidey_gfx decodes.

  python3 verify_gfx.py screens CAPTURE_DIR      # LOADSCRN + PAGE1-4, all 24 plane orders
  python3 verify_gfx.py rooms  SPL GT_DIR OUT    # every room: match %, diff images
  python3 verify_gfx.py sprite CAPTURE            # which SPIDS1 frame is the standing Spidey
  python3 verify_gfx.py sidebar CAPTURE           # SPIDFIGR layout/plane order
  python3 verify_gfx.py modes GT_DIR              # EGA-mode palette and CGA decoding
  python3 verify_gfx.py anchors SPL GT_DIR        # enemy sprite frame + pixel offset
"""

from __future__ import annotations

import itertools
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import spidey_gfx as G

ROOM_ORIGIN = (0, 24)  # room top-left on the 320x200 screen (below the header)
SIDEBAR_ORIGIN = (256, 8)  # SPIDFIGR health figure


def capture_indices(png: Path, palette=G.PALETTE) -> np.ndarray:
    """640x400 (or larger) capture -> (200, 320) palette indices."""
    im = np.asarray(Image.open(png).convert("RGB"))[:400, :640].astype(int)
    im = im[::2, ::2]  # exact 2x pixel doubling
    pal = np.array(palette, dtype=int)
    d = ((im[:, :, None, :] - pal[None, None, :, :]) ** 2).sum(-1)
    return d.argmin(-1).astype(np.uint8)


def match(a: np.ndarray, b: np.ndarray, mask: np.ndarray | None = None) -> float:
    eq = a == b
    if mask is not None:
        eq = eq[mask]
    return 100.0 * eq.mean()


def verify_screens(cap_dir: Path, data_dir: Path) -> None:
    pairs = [
        ("LOADSCRN", "probe-2.png"),
        ("PAGE1", "probe-3.png"),
        ("PAGE2", "probe-4.png"),
        ("PAGE3", "probe-5.png"),
        ("PAGE4", "probe-6.png"),
    ]
    for name, cap in pairs:
        truth = capture_indices(cap_dir / cap)
        data = (data_dir / f"{name}.EGA").read_bytes()
        scores = {}
        for order in itertools.permutations(range(4)):
            scores[("row", order)] = match(G.row_planar(data, 320, 200, order), truth)
            scores[("planar", order)] = match(G.planar(data, 320, 200, order), truth)
        best = sorted(scores.items(), key=lambda kv: -kv[1])[:3]
        print(f"{name}: " + "; ".join(f"{lay} {order} {s:.2f}%" for (lay, order), s in best))


def room_truth(png: Path) -> np.ndarray:
    idx = capture_indices(png)
    x, y = ROOM_ORIGIN
    return idx[y : y + G.ROOM_H * G.TILE, x : x + G.ROOM_W * G.TILE]


def sprite_mask(room: G.Room, player: bool) -> np.ndarray:
    """True where nothing moves: excludes a box around each sprite, the player,
    and each block object (blocks are moving platforms and the like)."""
    m = np.ones((G.ROOM_H * G.TILE, G.ROOM_W * G.TILE), dtype=bool)
    boxes = [(x + 1, y + 1) for x, y, _ in room.sprites]
    if player:
        boxes.append(G.PLAYER_START)
    for fx, fy in boxes:
        x0 = max(0, (fx - G.SPRITE_X_PAD - 1) * G.TILE)
        y0 = max(0, (fy - 3) * G.TILE)
        m[y0 : (fy + 2) * G.TILE, x0 : (fx + 3) * G.TILE] = False
    for x, y, w, h in room.blocks:
        m[max(0, (y - 1) * G.TILE) : (y + h + 1) * G.TILE, max(0, (x - 1) * G.TILE) : (x + w + 1) * G.TILE] = (
            False
        )
    return m


def cell_static(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    """Pixel mask that is False over every 8x8 cell where the two captures differ."""
    m = np.ones_like(a, dtype=bool)
    for ty in range(G.ROOM_H):
        for tx in range(G.ROOM_W):
            sl = (slice(ty * G.TILE, (ty + 1) * G.TILE), slice(tx * G.TILE, (tx + 1) * G.TILE))
            if (a[sl] != b[sl]).any():
                m[sl] = False
    return m


def mismatch_tiles(room: G.Room, ours: np.ndarray, truth: np.ndarray, mask: np.ndarray) -> dict[int, int]:
    """{foreground tile byte: number of 8x8 cells with that byte that mismatch} (unmasked cells only)."""
    out: dict[int, int] = {}
    for ty in range(G.ROOM_H):
        for tx in range(G.ROOM_W):
            sl = (slice(ty * G.TILE, (ty + 1) * G.TILE), slice(tx * G.TILE, (tx + 1) * G.TILE))
            if mask[sl].all() and (ours[sl] != truth[sl]).any():
                v = int(room.grid[ty, tx])
                out[v] = out.get(v, 0) + 1
    return out


def verify_rooms(spl: Path, gt_dir: Path, out_dir: Path, data_dir: Path) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    assets = G.Assets(data_dir)
    results = []
    for rid in G.room_ids(spl):
        shot = gt_dir / f"room{rid:02d}" / "shot-1.png"
        if not shot.exists():
            print(f"room {rid:2d}: no capture")
            continue
        room = G.Room(G.display_room(spl, rid))
        truth = room_truth(shot)
        ours = G.compose_room(room, assets, player=False)
        mask = sprite_mask(room, player=True)
        # anything that changes between the game's own successive captures is
        # animated (water, fire, force fields) or moving: not a decoding question
        for k in (2, 3):
            other = gt_dir / f"room{rid:02d}" / f"shot-{k}.png"
            if other.exists():
                mask &= cell_static(truth, room_truth(other))
        static = match(ours, truth, mask)
        full = match(ours, truth)
        results.append((rid, room.name, static, full))
        # side-by-side + diff image for eyeballing
        diff = ours != truth
        vis = np.zeros((*ours.shape, 3), dtype=np.uint8)
        vis[..., 0] = diff * 255
        vis[..., 1] = (~mask) * 90
        row = np.concatenate([np.asarray(G.to_rgb(truth)), np.asarray(G.to_rgb(ours)), vis], axis=1)
        Image.fromarray(row).resize((row.shape[1] * 2, row.shape[0] * 2), Image.Resampling.NEAREST).save(
            out_dir / f"room{rid:02d}.png"
        )
        bad = mismatch_tiles(room, ours, truth, mask)
        flag = "" if static > 99.9 else "  cells: " + ", ".join(f"{v:02x}x{n}" for v, n in sorted(bad.items()))
        print(f"room {rid:2d} {room.name:24s} static {static:6.2f}%  all {full:6.2f}%{flag}")
    worst = sorted(results, key=lambda r: r[2])[:5]
    print("worst static:", [(r[0], round(r[2], 2)) for r in worst])


def verify_sprite(capture: Path, data_dir: Path) -> None:
    """Find which SPIDS1 frame, at what offset, is Spider-Man standing at the start."""
    truth = room_truth(capture)
    frames = G.decode_sprites((data_dir / "SPIDS1.EGA").read_bytes())
    fx, fy = G.PLAYER_START
    best = []
    for f in range(32):
        spr = frames[f]
        opaque = spr != G.SPRITE_KEY
        for dx in range(-4, 5):
            for dy in range(-4, 5):
                x0 = (fx - G.SPRITE_X_PAD) * G.TILE + dx
                y0 = (fy + 1) * G.TILE - G.SPRITE_H + dy
                if x0 < 0 or y0 < 0:
                    continue
                win = truth[y0 : y0 + G.SPRITE_H, x0 : x0 + G.SPRITE_W]
                if win.shape != spr.shape:
                    continue
                score = (win[opaque] == spr[opaque]).mean()
                best.append((score, f, dx, dy))
    best.sort(reverse=True)
    for s, f, dx, dy in best[:5]:
        print(f"frame {f:2d} offset ({dx:+d},{dy:+d}) opaque-pixel match {100*s:.1f}%")


def verify_anchors(spl: Path, gt_dir: Path, data_dir: Path) -> None:
    """For every enemy sprite, find the best-matching frame of its creature and
    its pixel offset from our anchor in the earliest capture (0.5 s after the
    room appears). Reports offset (dx,dy) relative to compose_room's placement."""
    assets = G.Assets(data_dir)
    for rid in G.room_ids(spl):
        shot = gt_dir / f"room{rid:02d}" / "shot-1.png"
        if not shot.exists():
            continue
        room = G.Room(G.display_room(spl, rid))
        if not room.sprites:
            continue
        truth = room_truth(shot)
        for x, y, creature in room.sprites:
            fname, lo, hi = G.CREATURE_FRAMES[creature]
            frames = assets.sprites[fname]
            fx, fy = x + 1, y + 1
            bx = (fx - G.SPRITE_X_PAD) * G.TILE
            by = (fy + 1) * G.TILE - G.SPRITE_H
            best = (0.0, -1, 0, 0)
            for f in range(lo, hi + 1):
                spr = frames[f]
                opaque = spr != G.SPRITE_KEY
                if opaque.sum() < 20:
                    continue
                for dx in range(-24, 25):
                    for dy in range(-16, 17):
                        x0, y0 = bx + dx, by + dy
                        if (
                            x0 < 0
                            or y0 < 0
                            or x0 + G.SPRITE_W > truth.shape[1]
                            or y0 + G.SPRITE_H > truth.shape[0]
                        ):
                            continue
                        win = truth[y0 : y0 + G.SPRITE_H, x0 : x0 + G.SPRITE_W]
                        score = float((win[opaque] == spr[opaque]).mean())
                        if score > best[0]:
                            best = (score, f, dx, dy)
            s, f, dx, dy = best
            print(
                f"room {rid:2d} {creature:12s} at ({x:2d},{y:2d}): frame {f:2d} offset ({dx:+3d},{dy:+3d}) match {100*s:5.1f}%"
            )


def verify_sidebar(capture: Path, data_dir: Path) -> None:
    idx = capture_indices(capture)
    x, y = SIDEBAR_ORIGIN
    truth = idx[y : y + 192, x : x + 64]
    data = (data_dir / "SPIDFIGR.EGA").read_bytes()
    scores = {}
    for order in itertools.permutations(range(4)):
        scores[("row", order)] = match(G.row_planar(data, 64, 192, order), truth)
        scores[("planar", order)] = match(G.planar(data, 64, 192, order), truth)
    best = sorted(scores.items(), key=lambda kv: -kv[1])[:3]
    print("SPIDFIGR: " + "; ".join(f"{lay} {order} {s:.2f}%" for (lay, order), s in best))


def verify_modes(gt_dir: Path, data_dir: Path) -> None:
    """Midnight captured in EGA (mode 2) and CGA (mode 4): check the palettes
    and, for CGA, the .CGA tile decoding."""
    ega = np.asarray(Image.open(gt_dir / "room01-mode2" / "shot-1.png").convert("RGB"))[:400:2, :640:2]
    cols = sorted({tuple(int(c) for c in px) for px in ega.reshape(-1, 3)})
    print("EGA-mode colours seen:", cols)
    print("EGA palette predicted :", sorted(set(G.EGA_PALETTE)))
    truth = capture_indices(gt_dir / "room01-mode2" / "shot-1.png", G.EGA_PALETTE)
    room = G.Room(G.display_room(G.ROOT / "ROOMS.SPL", 1))
    ours = G.compose_room(room, G.Assets(data_dir))
    x, y = ROOM_ORIGIN
    print(
        f"EGA-mode Midnight static match {match(ours, truth[y:y + 176, x:x + 256], sprite_mask(room, True)):.2f}%"
    )
    cga = np.asarray(Image.open(gt_dir / "room01-mode4" / "shot-1.png").convert("RGB"))[:400:2, :640:2]
    cols = sorted({tuple(int(c) for c in px) for px in cga.reshape(-1, 3)})
    print("CGA-mode colours seen:", cols)
    truth = capture_indices(gt_dir / "room01-mode4" / "shot-1.png", G.CGA_PALETTE)[y : y + 176, x : x + 256]
    assets = G.Assets(data_dir, cga=True)
    ours = G.compose_room(room, assets)
    print(f"CGA packed-2bpp: Midnight static match {match(ours, truth, sprite_mask(room, True)):.2f}%")
    # and the CGA title screen against a mode-4 capture if present
    cap = gt_dir / "room01-mode4" / "title.png"
    if cap.exists():
        t = capture_indices(cap, G.CGA_PALETTE)
        print(
            f"CGA LOADSCRN packed-2bpp: {match(G.packed2((data_dir / 'LOADSCRN.CGA').read_bytes(), 320, 200), t):.2f}%"
        )


def main(argv: list[str]) -> int:
    data_dir = G.ROOT / "Spider"
    cmd, args = argv[0], argv[1:]
    if cmd == "screens":
        verify_screens(Path(args[0]), data_dir)
    elif cmd == "rooms":
        verify_rooms(Path(args[0]), Path(args[1]), Path(args[2]), data_dir)
    elif cmd == "sprite":
        verify_sprite(Path(args[0]), data_dir)
    elif cmd == "sidebar":
        verify_sidebar(Path(args[0]), data_dir)
    elif cmd == "modes":
        verify_modes(Path(args[0]), data_dir)
    elif cmd == "anchors":
        verify_anchors(Path(args[0]), Path(args[1]), data_dir)
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
