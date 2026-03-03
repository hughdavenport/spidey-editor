# TODO

Priority order: bugs → editor features → room/main quality → tests/research → GUI

---

## In progress

---

## Bugs / Quick fixes

[x] - Fix piping to vt100 (non-tty terminal size)
[x] - Only redraw when something changes (dirty flag + blocking poll)
[x] - Fix `writeRoom` progress message goes to stdout
[x] - Fix poll() assertion on SIGWINCH (EINTR)
[x] - Virtual screen buffer with cell-level diffing (no full-screen flicker)
[x] - Force full redraw on startup and resize

[x] - Fix `shrink()` object case is UNIMPLEMENTED (aborts)
      [x] - Block shrink: remove tiles from trailing edge, shrink w/h
      [ ] - Sprite shrink: no-op or remove (currently unreachable: detection skips SPRITE type)

[ ] - Object/switch IDs above 15 hit UNREACHABLE (single hex digit display limit)
      [ ] - editor_draw.c: the 2-digit display case is FIXME'd out
      [ ] - Need to handle printing 2-digit hex room IDs in the goto-room grid

---

## Editor features (from help text TODOs)

[ ] - Implement object create/edit (Ctrl-o)
      [ ] - Currently: "TODO create/edit object" in help
      [ ] - Show object creation dialog: type (BLOCK/SPRITE), position, dimensions
      [ ] - Edit existing object fields inline

[ ] - Implement copy/yank for tile-edit mode (y key)
      [ ] - Copy the thing under cursor (object, switch, or tile region)
      [ ] - Paste with a separate key

[ ] - Implement copy/yank for switch (y in EDIT_SWITCHDETAILS)
[ ] - Implement copy/yank for chunks (y in chunk edit states x4)

[ ] - Implement dir keys to change values in switch chunk detail editing
      [ ] - " TODO dir keys ?" entry in EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS help
      [ ] - Up/down should increment/decrement the field under cursor

[ ] - Create new object from TILE_EDIT when no object is under cursor
      [ ] - Currently shows "No object here, TODO create new?" in status

---

## editor.c / editor quality

[x] - Create shared header `editor_types.h`
[x] - Extract `editor_actions.c`, `editor_draw.c`, `editor_input.c`
[x] - Slim down `editor.c` to lifecycle only
[x] - Virtual screen buffer (editor_screen.c)

[ ] - Cursor position not per-room (FIXME in editor_types.h)
      [ ] - cursors[] array is indexed but only current_level is used meaningfully
      [ ] - Should remember position when switching rooms

---

## room.c quality

[x] - Remove dead #if 0 compressed-cache block in writeRoom
[x] - Rename second read_next macro → read_decompressed

[ ] - Rename struct fields `room_north/east/south/west` -> `room_up/right/down/left`
      [ ] - Remove DEPRECATED aliases in main.c once renamed
      [ ] - Update dumpRoom output labels

[ ] - Make dumpRoom take a FILE* parameter instead of hardcoding stdout

[ ] - Replace assert() with proper error returns in object bounds checks in readRoom
      [ ] - Lines ~600-612: the warn-then-assert pattern is contradictory

---

## main.c quality

[x] - Extract usage_patch() helper (was duplicated 30+ times)
[x] - Fix `last` memory leaks (Invalid object id / Invalid switch id paths)

[ ] - Extract usage_delete() helper (similar duplication)

[ ] - Clarify `addr` encoding in PatchInstruction
      [ ] - Object patches encode `(object_idx << 8) | field_offset`
      [ ] - Switch patches use a different scheme
      [ ] - Add explicit fields instead of overloading address

[ ] - DRY up object[N] and switch[N] arg parsing between main_patch/main_delete

---

## Makefile improvements

[ ] - Add `vt100` build target
[ ] - Add `release` target (no ASAN, -O2)
[ ] - Add `clean` target
[ ] - Add `editor.so` as explicit target

---

## Tests

[ ] - Roundtrip test: read ROOMS.SPL → write to temp file → binary diff original
      [ ] - Should be identical for all 64 rooms
      [ ] - Run as: `./a.out recompress && diff ROOMS.SPL ROOMS.SPL.bak`
      [ ] - Automate with a shell script that fails on any diff

[ ] - Field read test: for known rooms, assert specific field values
      [ ] - Room 1 "Midnight": tileset=3, background=2, damage=3
      [ ] - Room 2 "Foyer": neighbour links known from dumpRoom output
      [ ] - Catches regressions in readRoom field offsets

[ ] - Patch roundtrip: apply a patch, read it back, assert value matches
      [ ] - `./a.out patch 1 tileset 5 && ./a.out display 1 | grep tileset` == 5
      [ ] - Then restore: `./a.out patch 1 tileset 3`

[ ] - Compression fuzz: generate random valid room data, compress, decompress,
      assert output matches input
      [ ] - Good stress test for writeRoom/readRoom symmetry

---

## Binary analysis (SPIDEY.EXE with radare2)

[ ] - Identify room loading routine
      [ ] - `r2 Spider/SPIDEY.EXE`, then `aaa` (autoanalyse), `afl` (list functions)
      [ ] - Search for 0x8F marker read (used to detect room start in readRoom)
      [ ] - Cross-ref to understand what UNKNOWN_b/c/e/f fields control in-game

[ ] - Verify compression/decompression algorithm
      [ ] - Find the decompression loop in the binary, compare to readRoom()
      [ ] - Confirm marker[1]/[2]/[3] RLE/LZ semantics match

[ ] - Understand MOVERTAB.DAT usage
      [ ] - UNKNOWN_b is "base offset / 3 into movertab.dat" per existing comment
      [ ] - Find movertab read in binary to confirm indexing and entry size

[ ] - Document TOGGLE_BIT semantics fully
      [ ] - Find the switch-trigger code in binary
      [ ] - Clarify what "bitmask on global_switches[index]" actually controls
      [ ] - The on/off/test fields are partially understood; binary confirms

[ ] - Understand global_switches[] size and layout
      [ ] - How many global switches exist? Where are they in memory?

[ ] - Re-do MOVERTAB.DAT / UNKNOWN field research via DOS file-open tracing
      [ ] - Approach that worked previously: search for DOS INT 21h AH=3Dh (open file)
            in r2, find the call that opens "MOVERTAB.DAT", then follow the read
            buffer forward to see how the game indexes into it
      [ ] - This produced the "UNKNOWN_b = base offset / 3 into movertab.dat" comment
      [ ] - Same approach for other UNKNOWN fields: find where room data is parsed,
            cross-ref each field offset against game logic
      [ ] - Notes from the original pass are lost; this is a redo

---

## GUI layer using raylib

[ ] - Design approach: TUI editor stays as-is; GUI is a separate mode/binary
[ ] - Set up raylib integration (see ../breadboard/breadboard.c for hotreload pattern)
[ ] - Implement basic room tile renderer in raylib
[ ] - Add mouse-based tile editing
[ ] - Port object display to GUI
[ ] - Port switch display and editing to GUI

---

## Investigation / Research

[ ] - Understand EGA sprite format in Spider/*.EGA for GUI rendering
[ ] - Document MOVERTAB.DAT format fully
[ ] - Understand TOGGLE_BIT chunk semantics fully
