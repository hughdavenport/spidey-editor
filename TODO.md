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
      [x] - Sprite shrink: no-op or remove (shrink() checks object->type == BLOCK; SPRITE silently skipped)

[ ] - Object/switch IDs above 15 hit UNREACHABLE (single hex digit display limit)
      [ ] - editor_draw.c: the 2-digit display case is FIXME'd out
      [x] - goto-room grid already handles 2-digit (partial_byte); object/switch goto do not
      [ ] - editor_input.c handle_edit_switchdetails_select_chunk: same issue for chunk index > 15

[ ] - Move object at room edge doesn't carry through to neighbour room
      [ ] - move() rejects moves that would go off-edge; should transfer object/switch to neighbour room
      [ ] - Add x as cut (yank + delete) to enable cut-paste between rooms

[ ] - Chunk split when editing middle of a chunk (advanced / deferred)
      [ ] - Editing a byte in the middle of a TOGGLE_BLOCK should split into [start..n-1], [n], [n+1..end]

[ ] - Multiple objects/switches at same tile not handled in status panel
      [ ] - editor_draw.c:1223: UNIMPLEMENTED: multiple objects underneath
      [ ] - editor_draw.c:1286: UNIMPLEMENTED: multiple switches underneath

[ ] - Selected chunk UNIMPLEMENTED in switch detail panel
      [ ] - editor_draw.c:1669

[ ] - Object/switch patch bounds not validated before write in main.c
      [ ] - main.c:154, 158: FIXME It'll probably blow the assert when writing

[ ] - room.c writeRoom: first byte of some chunk types may need 0x80 set
      [ ] - room.c:930: FIXME depending on type, first byte should have 0x80

---

## Backlog

[ ] - Macro record/replay
      [ ] - `m` starts recording; subsequent keys are buffered; `m` stops recording
      [ ] - `,` replays the recorded key sequence as if each key were typed live
      [ ] - Replaying should trigger all the same mutations/state changes as real input
      [ ] - Consider: replay inside a recording should play previous macro, not recurse

[ ] - Jump to arbitrary position (cursor and/or object/chunk/switch)
      [ ] - Move cursor directly to a typed (x,y) coordinate in the current room
      [ ] - Move the active object/chunk/switch to an arbitrary position in the current room
            (alternative to nudging one step at a time with hjkl)
      [ ] - Keybinding TBD (e.g. `g` for goto-coord, or a `G` prefix in move mode)

---

## Editor features (from help text TODOs)

[x] - Implement object create (Ctrl-o)
      [x] - Creates 1x1 BLOCK at cursor if no object present
      [ ] - Show object creation dialog: type (BLOCK/SPRITE), position, dimensions
      [ ] - Edit existing object fields inline (SPRITE fields not editable yet)

[x] - Implement copy/yank for tile-edit mode (y key)
      [x] - y: copy object or switch under cursor to clipboard
      [x] - P: paste copied object/switch at cursor

[x] - Implement copy/yank for switch (y in EDIT_SWITCHDETAILS)
      [x] - P: paste clipboard chunk into current switch
[x] - Implement copy/yank for chunks (y in chunk edit states x4)

[x] - Dir keys in EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS move the memory pointer (h/j/k/l + arrows)
      [ ] - Help text missing: arrow/hjkl entries absent from CHUNK_MEMORY_DETAILS help panel
      [ ] - EDIT_SWITCHDETAILS_CHUNK_OBJECT_DETAILS still has " TODO dir keys ?" placeholder

[ ] - Create new object from TILE_EDIT when no object is under cursor
      [ ] - Currently shows "No object here, TODO create new?" in status

---

## editor.c / editor quality

[x] - Create shared header `editor_types.h`
[x] - Extract `editor_actions.c`, `editor_draw.c`, `editor_input.c`
[x] - Slim down `editor.c` to lifecycle only
[x] - Virtual screen buffer (editor_screen.c)
[x] - Key input: replace strncmp KEY_MATCHES macro with parse_key() CSI state machine → Key struct dispatch

[x] - Cursor position remembered per-room (cursors[] array is indexed by level)

---

## room.c quality

[x] - Remove dead #if 0 compressed-cache block in writeRoom
[x] - Rename second read_next macro → read_decompressed

[ ] - Rename struct fields `room_north/east/south/west` -> `room_up/right/down/left`
      [ ] - Remove DEPRECATED aliases in main.c once renamed
      [ ] - Update dumpRoom output labels

[ ] - Make dumpRoom take a FILE* parameter instead of hardcoding stdout

[ ] - Remove redundant assert() after warn+continue in readRoom
      [ ] - room.c:606: assert(y < HEIGHT_TILES) is unreachable (line 604 continues if y is bad)
      [ ] - The assert gives false confidence; should just be removed

---

## main.c quality

[x] - Extract usage_patch() helper (was duplicated 30+ times)
[x] - Fix `last` memory leaks (Invalid object id / Invalid switch id paths)
[x] - Extract usage_delete() helper (similar duplication)

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

[x] - Roundtrip test: read ROOMS.SPL → recompress → binary diff original
      [x] - test-roundtrip.sh: backs up, recompresses, diffs, always restores
      [x] - PASSES: compression is bit-for-bit identical to original game data

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
