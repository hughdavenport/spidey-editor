# The Amazing Spider-Man (1990) graphics formats

Everything below was checked pixel-for-pixel against the real game running in
DOSBox-X (`verify_gfx.py`), not by eye. Decoder: `spidey_gfx.py`. Capture
tooling: `dosbox-capture/`.

## Bit-plane layouts (EGA files)

All `.EGA` files are 16-colour, 4 bit-planes, 1 bit per pixel per plane, MSB =
leftmost pixel. Three layouts occur:

| Layout | Files | Structure |
|---|---|---|
| tile, plane-major | `BLOCKS.EGA` (512 tiles), `SCENERY.EGA` (256), `CHARSET.EGA` (128) | 8×8 tiles, 32 bytes each: 8 bytes of plane 0 (rows 0–7), then plane 1, 2, 3 |
| row-planar | `LOADSCRN`, `WINSCRN`, `PAGE1`–`PAGE4` (320×200), `HISCORE` (320×38), `SPIDFIGR`, `SKELFIGR` (64×192) | for each row: plane 0 row, plane 1 row, plane 2 row, plane 3 row (width/8 bytes each) |
| sprite, plane-major | `SPIDS1.EGA`, `SPIDS2.EGA` | 64 frames of 24×21; 4 planes × 63 bytes (21 rows × 3 bytes) = 252 bytes + 4 pad = 256-byte stride |

**Plane order is I, R, G, B in every file**: the first plane stored is the
intensity bit (colour bit 3), the last is blue (bit 0). This is
`PLANE_ORDER = (3, 2, 1, 0)`. The "natural" B,G,R,I order the modding wiki
suggests decodes with blue and red swapped. Verified by trying all 24
permutations of both layouts against captures: only row-planar (3,2,1,0)
scores 100.00% on `LOADSCRN` and `PAGE1`–`PAGE4`; the runner-up is 92.8%.

Sprite transparency key is colour 15 (the palette's yellow). Foreground
tiles are opaque; only tile index 0 shows the background.

## CGA files

Same pictures, same tile/frame/row structure, **packed 2 bits per pixel**
(native CGA framebuffer bytes, 4 pixels per byte, MSB pair = leftmost), half
the bytes of the EGA file. Not two bit-planes: the planar guess scored 68% on
Midnight in CGA mode, packed scores 100%. Palette is CGA mode 4, palette 1,
high intensity: black, light cyan, light magenta, white. Sprite key = 3.

## Palette

The game programs the VGA DAC (INT 10h AH=10h) from a table of 16 six-bit
RGB triplets at DS:0x97AE (DS=0x0FB9) in the EXEPACK-unpacked `SPIDEY.EXE`.
Scaled to 8-bit:

```
 0 (0,0,0)        4 (97,97,97)      8 (97,227,227)   12 (0,162,227)
 1 (0,97,194)     5 (0,65,162)      9 (32,194,65)    13 (0,97,32)
 2 (227,65,0)     6 (162,32,0)     10 (194,65,227)   14 (97,32,130)
 3 (227,227,227)  7 (162,162,162)  11 (227,130,0)    15 (227,194,0) sprite key
```

In the game's EGA video mode (menu option 2) it instead loads the attribute
registers from the 17-byte table at DS:0x978C
(`00 f9 fc ff f8 01 04 07 fb fa fd 06 03 02 05 fe` + overscan); the low six
bits are standard EGA rgbRGB values, with value 6 shown as brown. That
palette matches a DOSBox EGA-mode capture 100% (`spidey_gfx.EGA_PALETTE`).

## Screen layout (320×200)

- Header row: score and room name, y 0–23 (CHARSET).
- Room: 32×22 tiles = 256×176 px at (0, 24).
- Health figure `SPIDFIGR`/`SKELFIGR`: 64×192 at (256, 6). Row-planar
  (3,2,1,0) matches 100% there; nowhere else scores above 84%.

## Rooms

- A room's background is `BACKS.SPL` block `bkgrnd` (its metadata byte, not
  its room id) decompressed with the ROOMS.SPL scheme into 704 `SCENERY`
  tile indices, drawn opaque.
- Foreground tile index = `(byte & 0x3f) + tile_offset * 16`; the high two
  bits are attributes (solid / magnetic / slippery).
- Block objects' tiles are stored in the tile grid at the object's start
  position (the editor's `display` shows them there), so drawing the grid
  as-is shows the room as it looks on entry.
- Sprites: a creature stored at (x, y) has its feet on tile (x+1, y+1). The
  24×21 frame is drawn with its left edge at `(x+1-2)*8 + 3` pixels and its
  bottom on the bottom of row y+1. The +3 was measured on the two things that
  never move (Spider-Man at start and Mary Jane); everything else has already
  moved a few pixels by the first capture 0.5 s in.
- Frames shown on entry: Spider-Man 24, mummy 35, blue man 38, shark 44,
  wolf 46, R2D2 50, dinosaur 53, rat 58, Mary Jane 62 (all `SPIDS1`);
  shotgun lady 5, Mysterio 6 (`SPIDS2`). Mysterio is not drawn until he
  materialises. (`SHARK` with damage 3 = Mysterio, 4 = Mary Jane.)

## Verification results

Ground truth: each room swapped into slot 1 (`importRoom`), exits pointed at
itself so a start position over thin air re-enters the same room, game booted
headless (Xvfb + dosbox-x + xdotool), window captured 0.5 s, 1.5 s and 3.5 s
after the room appears. A cell is compared only if it is away from every
sprite / block object / the player and identical in all three captures
(the game animates some tiles: 0x1c–0x1f force fields and fire, 0x38–0x3f
water / lava / TV static, plus a few per-tileset).

- `LEVELSET.SPL` (12 rooms): **100.00 %** of static pixels in every room.
- `ROOMS.SPL` (56 rooms): 100.00 % in 48 rooms. The other eight differ only
  in cells the game rewrites from switch state on entry, which the level
  file cannot express:
  - Room 3 Lift: entry switch 7's three TOGGLE_BLOCK chunks show their
    `off` tiles (9 cells).
  - Rooms 9, 10, 12, 20, 27: a switch's own indicator tile is flipped to the
    other tile of its pair (red/green button 0x10/0x11, yellow/grey button
    0x12/0x13, green/black light 0x16/0x17, 0x36/0x37): one cell per switch,
    8 cells total.
  - Rooms 32, 34: one 0x1c/0x1d force-field cell each that did not animate
    within the 3.5 s window.
- Title, four story pages, health figure: 100.00 %. EGA-mode palette and
  CGA decoding: 100.00 % on Midnight.

## Reproducing

```
make
python3 spidey_gfx.py dump gfx-out/decoded --scale 3          # every image as PNG
python3 spidey_gfx.py rooms LEVELSET.SPL gfx-out/levelset      # room renders + montage
python3 spidey_gfx.py rooms ROOMS.SPL gfx-out/original
dosbox-capture/capture_all.sh Spider gt LEVELSET.SPL ROOMS.SPL # ground truth (~6 min)
python3 verify_gfx.py rooms LEVELSET.SPL gt/LEVELSET gfx-out/verify-levelset
```

`gfx-out/` is ignored by git (the decoded art is Paragon's copyright).
