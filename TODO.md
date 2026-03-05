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

[x] - Object/switch IDs above 15 hit UNREACHABLE (single hex digit display limit)
      [x] - Non-issue: hard limits of 7 objects and 15 switches are now enforced in the editor;
            IDs > 15 can never occur, UNREACHABLEs are genuinely unreachable
      [x] - goto-room grid already handles 2-digit (partial_byte); object/switch goto do not

[x] - Move object at room edge doesn't carry through to neighbour room
      [x] - move() transfers object/switch to neighbour room
      [x] - x as cut (yank + delete) for cross-room cut-paste
      [x] - Undo/redo cross-room moves navigate to the correct room (first_room not last_room)

[ ] - Chunk split when editing middle of a chunk (advanced / deferred)
      [ ] - Editing a byte in the middle of a TOGGLE_BLOCK should split into [start..n-1], [n], [n+1..end]

[x] - Multiple objects/switches at same tile not handled in status panel
      [x] - Overlapping objects shown one-per-line; first hit highlighted as active

[ ] - Selected chunk UNIMPLEMENTED in switch detail panel
      [ ] - editor_draw.c:1669

[ ] - Object/switch patch bounds not validated before write in main.c
      [ ] - main.c:154, 158: FIXME It'll probably blow the assert when writing

[x] - Overlapping block objects corrupt tile data on writeRooms→reload cycle
      [x] - writeRoom now uses local ser_tiles (copy of room->data.tiles), overlays
            objects in reverse order, never modifies room->data.tiles

[x] - Mouse support: left click moves cursor, right click / Ctrl+click moves active item
      [x] - TOGGLE_BLOCK chunk move now clears room->data.tiles at old chunk position
      [x] - Ctrl+click added as alias for right-click move

[ ] - room.c writeRoom: first byte of some chunk types may need 0x80 set
      [ ] - room.c:930: FIXME depending on type, first byte should have 0x80

---

## Backlog

[ ] - Undo history depth cap and optional persistence
      [ ] - Cap stack at N entries (e.g. 200), dropping oldest on overflow
      [ ] - Optional: serialise undo stack to disk on exit, reload on startup
            (main value: crash recovery; each snapshot ~1-3 KB, 200 entries ~300 KB)

[x] - Macro record/replay (named slots a-z)
      [x] - State: macro_meta, macro_replay_pending, macro_recording, macro_slot,
            macro_last_saved, macro_last_replayed, macro_buf, macro_slots[26], macro_replay
      [x] - Bindings (intercepted in handle_normal, active in all states):
            m (idle)       → macro_meta=true; show slot overlay ("m> a c f m p z")
            m + a-z        → start recording to slot (mm = slot 'm')
            m (recording)  → stop; save macro_buf → macro_slots[slot]; macro_last_saved=slot
            , (idle)       → replay_pending=true; show slot overlay (",=m .=f  a c f m p z")
            , + a-z        → replay slot X; macro_last_replayed=X
            , + ,          → replay macro_last_replayed slot
            , + .          → replay macro_last_saved slot
            , + anything   → cancel
      [x] - Slot overlay: drawn at bottom (status line) while waiting for slot key
      [x] - Replay: macro_replay ARRAY(Key) queue in game_state; drained one key per
            process_input() call before poll() — same dispatch path as live input
      [x] - Guard: m and , keys ignored during replay; no replay-within-replay
      [x] - Header shows [Mm] while recording to slot m, nothing when idle
      [x] - Cleanup: free macro_slots[i], macro_buf, macro_replay in end()

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
      [x] - Help text updated: all states audited and corrected
            - NORMAL/TILE_EDIT: added u/U undo/redo, mouse bindings, o[n] goto object
            - GOTO_ROOM: fixed swapped UP/Alt+k and DOWN/Alt+j directions
            - EDIT_ROOMNAME: fixed DEL/BS descriptions; added Home/End
            - EDIT_ROOMDETAILS: added h/j/k/l aliases for room link navigation
            - CHUNK_MEMORY: fixed Space description; fixed 0-9a-fA-F label
            - CHUNK_BLOCK: fixed 0-9a-fA-F label

[x] - Create new object from TILE_EDIT when no object is under cursor
      [x] - Creates 1x1 BLOCK at cursor position

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
