# Engine / format quirks — observed during playtest

These are things the editor should warn about (or at least document) because
they bite during level design.

## 2026-05-28 — Room 1 of `LEVELSET.SPL` ("Neon Boulevard")

### "Glitching" — corrupted columns at screen edges
**Symptoms.** When playing the room in DOSBox:

- The title bar at the top continually flashes with noisy yellow.
- The top quarter and bottom quarter of the large stylised Spidey on the side
  panel both flash with yellow.
- Occasionally the bottom two tiles of the actual playfield flash.
- Movement still works; it's not a hard crash.

Repro state: room had **one sprite (R2D2) and one block (helicopter
decoration)**; both `entry+once` switches were defined.

### The R2D2 sprite never appears in-game
The room defines `sprite 0: (x,y)=24,15 R2D2 dmg:1`. In DOSBox the sprite is
**not rendered** at all. It still consumes the slot — that's likely connected
to the glitch symptom above.

### Hypothesis (worth verifying)
The original-game rooms (e.g. Midnight) have **object 0 = a block** and the
**sprite slots come later**. Our room had object 0 as a sprite directly. The
hunch: **the game engine may require the first object to be a block, not a
sprite** (perhaps the renderer scans the first slot for a block pointer and
mis-interprets a sprite record, corrupting nearby graphics memory and skipping
the sprite render).

**Action**: try moving the helicopter block into object slot 0 and the R2D2
into slot 1 — see if the glitch and the missing sprite both resolve. If so,
add a hard editor warning when slot 0 is a sprite.

## Horizontal wrap softlock
The screen **wraps**: crawling off the left edge places you on the right edge.
If the room geometry has a small unreachable "pocket" on the right edge near
the floor, the player can land in it and have **no way out** unless a switch
has already cleared the wall.

**Action**: editor should detect right/left edge pockets reachable only via
wrap and warn — or designers should always make sure the bottom rows are
mirrored across the wrap.

## Damage = 0 silences hazard tiles
`dmg: 0` on a room metadata byte makes the electric / fire / water HURT tiles
non-damaging — they still *look* hazardous but don't actually subtract health.
The hurt sound effect still plays, which is misleading.

**Action**: editor should warn when a room contains any of the known HURT
tiles (`1C` `1E` `1F` electric, `5C..5F` solid-electric, `1C..1F` off-4 fire,
`3C..3F` off-4 vertical-water, `3C..3F` off-0 healing-but-hurts) **and**
`dmg == 0`. Set `dmg` to at least 1 in those rooms by default.

## Undefined room link (link to room 0 when room 0 is blank)
Edges (`left`/`down`/`up`/`right`) that point at room id 0 may cause the
engine to wrap back to the current room, or render glitchily. We saw the
**top-left and bottom edges seem to loop back to room 1** when room 0 was
blank.

**Action**:
- Either always define **room 0** (the original game's "Test Lab") as a
  catch-all valid destination.
- Or self-link unused edges (e.g. `left = 1` for room 1) so the wrap behaviour
  is intentional rather than implicit.
- Editor should warn on any edge that points at a room with 0 tile bytes.

## Entry walls
A room's entrance edge **must have a gap** in the wall tiles where the player
arrives — otherwise the player tile-collides on entry and either gets stuck
or pushed out. Easy to miss when both sides are built independently.

**Action**: editor should compute the "entry row band" implied by the partner
room and red-line any solid tiles in those rows when the rooms are linked.

## Switch `(side)=LEFT` at column 0 is unreachable
A switch at column 0 with `side=LEFT` means "approach from the column to its
left" — but there *is* no column -1, you can only get there by wrap. Even
then the geometry rarely lines up. `side=RIGHT` or `BOTTOM` is the safer
default for an edge switch.

**Action**: editor should warn when a switch's required approach side points
off-grid (col 0 LEFT, col 31 RIGHT, row 0 TOP, row 21 BOTTOM).

Room 1: "Neon Boulevard          "
```
Tiles:
                                                                
                      4242424242424242424242                    
                      4316161616161616161643                    
                      4316162116161621161643                    
                      4316161616161616161643                    
                                                                
  4342424242                                                    
  4368686868                              2B2C                  
  4368166868                            2D2E2F                  
  4368176868                            6E6F69                  
  4368216868                                                    
  4368166868                                                    
  4368686868                                                    
  4368686868                                                    
  4342424242                                                    
  50                                            41              
                                        444444444444444444444444
                          44            444444444444444444444444
                          44                                    
                          445C5C5C5C5C5C                        
4544444444444444444444444444444444444444444444444444444444444446
4444444444444444444444444444444444444444444444444444444444444444

bkgrnd: 11, tiles: 0, dmg: 0, gravity (|): 4, gravity (-): 0
left: 0 - ""
down: 0 - ""
up: 0 - ""
right: 2 - "Heliport"
UNKNOWN_b: 1, UNKNOWN_c: 0, UNKNOWN_e: 0, UNKNOWN_f: 0
  objects (1):
    sprite 0: (x,y)=24,15 R2D2 dmg:1
  switches (1):
    switch 0: (x,y)=1,15 (entry)=false (once)=true (side)=BOTTOM
      chunks:
        (1) type block: (x,y)=14,19 (width)=6 (on/off)=5c/00
  Left over data (length=1): [0x00]
```

## 2026-05-29 — More Neon Boulevard / Heliport playtest

### Spidey "feet in row 20" actually means floor must be at row 21
Playtest of v4 (single-row floor at row 20) had Spidey **falling through the
floor immediately at start**, wrapping via the down-edge back to the top of
the room. The render_room PLAYER_FEET=(3,20) draws his sprite with its bottom
pixel at the top of row 21 — so the engine's "feet at (3,20)" actually means
his feet rest on the **row-21 tile**. Floor must be at row 21 (row 20 is the
empty row above his feet).

### Stage walls need ~4 tile clearance for standing entry
Heliport's left wall ending at row 17 (rows 18-20 open, row 21 floor) STILL
required Spidey to crawl into Room 2 from Neon Boulevard. Either the game
treats Spidey as taller than 3 tiles for transition purposes, or the entry-Y
from the previous room is offset. Empirically: leave 4+ rows of clearance
above the floor at any entry edge.

### Sprite spawns are non-deterministic across plays
Same room, same data, plays differently each time. Mummy in Heliport sometimes
"dies on entry" (death sound but no sprite). Sprites appear at different XY
positions across replays. Some render outside the playfield. Likely a re-
encoder bug or a misread field (UNKNOWN_b/c/e/f could be sprite-state).
**Action**: log per-room sprite positions across plays, compare against
binary bytes.

### Helicopter mis-placed in Neon Boulevard
Static decorative helicopter at the rooftop reads as floating metal salad.
Better fit: put it in **Heliport** as a moving platform (Black Thunder
pattern — switch raises it up to a roof exit) and use Neon Boulevard for a
different rooftop motif.

## 2026-05-30 — Cross-room edge tiles bleed into the neighbour

When Spidey is at the very top of Room A and Room A's row 0 is empty, the
engine appears to use Room B's row 21 (the room linked via `up`) for
collision/damage. So a HURT tile at the bottom of the room above can hurt
you from "inside" the room you're actually in.

Same logic likely applies to left/right/down edges.

**Practical implication for level design**: when designing a room, also
audit the bottom row of any room linked via `up`, the top row of any room
linked via `down`, etc. A hurt tile at an edge will affect both rooms.

**Editor action**: when rendering a room, optionally overlay the partner
row from each linked neighbour to visualise the seam.

This explains the "section of top wall hurts with no obvious reason"
Heliport observation — must be hurt tiles in the bottom row of Sky Highway
(or whichever room sits above Heliport) bleeding through.

## 2026-09-15 — Root causes found while building a levelset generator

Verified in headless DOSBox-X with throwaway test rooms:

- **The "object 0 must be a block" hypothesis and the non-deterministic sprite spawns have
  the same cause**: the per-room fields the editor calls `UNKNOWN_c` and `UNKNOWN_e/f` are
  the room's global object-index base and switch-index base (cumulative counts over
  lower-numbered rooms; exact for all 56 original rooms). `LEVELSET.SPL` has them all 0, so
  every room shares object slots 0..n and switch fields 0..m, and `MOVERTAB.DAT` (3 bytes per
  global object, byte 2 = initial behaviour: b8 walking sprite, 00 static block) is read
  through the same wrong indices. A levelset needs correct bases and its own MOVERTAB.DAT;
  since 2026-09-19 the editor recomputes the bases on every save (`recomputeBases` in
  `writeFile`), as `import` and `swap` already did.
- `entry=true` switches are contact sensors (player or object overlapping the tile), not
  room-entry triggers.
- A toggle-block chunk is at most 8 tiles wide/high (3-bit size); width 9 wraps to 1.
- A repeatable switch's object commands run only on activation; block chunks revert on the
  second trigger. Re-arm repeatable triggers with a final toggle of 2 into themselves.
- The only sprite kill reproduced is object value 0x0b on the sprite. Fire tiles, a dropped
  block and a pillar-into-wall squeeze did not kill a mummy.
