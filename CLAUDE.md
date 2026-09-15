# spidey-editor: CLAUDE.md

## Project Overview
A terminal-based map editor for "The Amazing Spider-Man" (1990 DOS game). Reads/writes `ROOMS.SPL` binary format with RLE compression. Two modes: patch (CLI commands) and interactive editor (ANSI terminal UI with hot-reloading).

## Build
```sh
make             # builds ./a.out with ASAN + debug; editor.so / gui.so are built on first use and hot-reload
make release     # builds ./spidey-editor: one binary, editor + GUI linked in, static raylib, -O2, no hot reload
make check       # spl_check: .SPL compressor round trips + background store save/reload (scratch copy of Spider/)
./a.out editor   # interactive editor
./a.out help     # show all patch commands
```
`SPIDEY_RELEASE` (set by `make release`) selects the linked-in paths in editor.c (`editor_main`)
and gui_host.c (`gui_main`); editor.c's `end`/`get_screen_dimensions` then serve the GUI too,
checking `screen_headless`. The release binary sets `SPIDEY_NO_HOTRELOAD` itself; setting it by hand
turns off source watching in the debug build.

Makefile compiles `main.c room.c editor.c` with `-fsanitize=address -ggdb -Werror`.

## Source Files

| File | Purpose |
|------|---------|
| `main.c` | Patch/CLI mode: `rooms`, `display`, `patch`, `delete` subcommands |
| `room.c` | ROOMS.SPL binary decode/encode with RLE compression |
| `spl.c` | The .SPL LZ/RLE codec shared by ROOMS.SPL and BACKS.SPL: `spl_decompress`, `spl_compress` (writeRoom's compressor, extracted) |
| `render.c` | Room rendering (PNG + GUI), the game graphics as editable sources (`GfxSource`), the BACKS.SPL background store |
| `spl_check.c` | `make check`: round-trip tests for the codec and the background store |
| `room.h` | Room structs: `DecompresssedRoom`, `RoomObject`, `SwitchObject`, `SwitchChunk`, `RoomFile` |
| `editor.c` | Interactive TUI editor with hot-reload via dlopen. Global `state` pointer. All drawing via ANSI escape codes to stdout. |
| `array.h` | Generic dynamic array macros (`ARRAY`, `ARRAY_ADD`, `ARRAY_FREE`, `ARRAY_ENSURE`) |
| `vt100.c` | VT100 state machine (Paul Williams DEC model). Virtual screen buffer. |
| `vt100.h` | VT100 public API |
| `vt100_main.c` | VT100 visualiser: raw/pipe mode and PTY mode. Separate binary: `vt100` |

## Key Data Structures

### Room format (room.h)
- `DecompresssedRoom`: 742 bytes + dynamic. Tiles (32×22 = 704 bytes), metadata (gravity, background, damage, name, room links), then dynamic objects/switches.
- `RoomObject`: block (x,y,w,h,tiles) or sprite (x,y,type,damage)
- `SwitchObject`: array of `SwitchChunk`s (PREAMBLE, TOGGLE_BLOCK, TOGGLE_BIT, TOGGLE_OBJECT)
- `RoomFile`: 64 rooms

### Editor state (editor.c)
- `game_state` struct: global `state` pointer (hot-reloaded)
- `screen_dimensions`: v2 (x=cols, y=rows) — queried via `TIOCGWINSZ`
- `MIN_WIDTH = 2 * WIDTH_TILES = 64`, `MIN_HEIGHT = HEIGHT_TILES + 1 = 23`
- Largest needed height: 36 rows (GOTO_ROOM state)

## File size limits
- ROOMS.SPL must stay within `MAX_ROOM_FILE_SIZE` (0x3100, room.c) and each room within writeRoom's
  960-byte buffers. `writeFile` fails (no partial file) when a room will not fit; `writeRooms` writes
  ROOMS.SPL.tmp and renames it over ROOMS.SPL only on success. Every editor write goes through
  `editor_save()` (editor_actions.c): on refusal it undoes back to `saved_undo_len` (the undo depth
  at the last successful write) and puts "NOT SAVED: ..." in the status line, so the editor always
  shows what is on disk. `room_create` un-creates its room on refusal.
- BACKS.SPL: the game's limit is unknown, so `render_backs_save` refuses to grow past the shipped
  file's size (`BACKS_FILE_LIMIT` 0x2b93; our compressor packs about twice as tight, so there is headroom) and
  also writes via .tmp + rename. The GUI then undoes background strokes until it fits and shows the
  refusal in red under the picker (`gui_error`).
- `make check` covers both refusals (oversized file refused, file on disk untouched).

## Terminal / Screen Handling
- Editor uses ANSI escape codes (`\033[row;colH` cursor movement, color codes)
- `get_screen_dimensions()`: calls `ioctl(STDOUT_FILENO, TIOCGWINSZ, &w)` — **fails when piped (returns 0)**
- `GOTO(x, y)` macro asserts `x < screen_dimensions.x && y < screen_dimensions.y`
- SIGWINCH handler sets `state->resized = true`; main loop calls `get_screen_dimensions()` on next iteration

## Hot Reload (editor.c)
- Editor compiles itself as a shared library (`editor.so`)
- `hotreload_loop()` is the entry point in the .so
- When source files change, recompiles and returns from the loop so main can reload
- State is preserved across reloads via global `state` pointer
- Watched files: `editor.c`, `room.c`, `room.h`, `array.h`

## VT100 Tool
- `./vt100 [opts] [command]`: pipe mode or PTY mode
- Pipe mode (`./a.out editor | ./vt100`): reads editor stdout, renders virtual screen to stderr
- PTY mode (`./vt100 ./a.out editor`): wraps program in PTY, correct terminal size propagated
- Constants: `VT_DUMP_GUTTER=5`, `VT_DUMP_SIDEBAR=22` (column overhead for display borders)
- **Bug**: pipe mode fails because editor gets 0 width from TIOCGWINSZ on non-tty stdout

## Game Format Notes
- Spider/*.EGA: game graphics in EGA format
- MOVERTAB.DAT: movement table data
- Tiles are 8×8 pixels, room is 32×22 tiles
- Objects: blocks have custom tile data; sprites use predefined enemy types (SHARK, MUMMY, BLUE_MAN, WOLF, R2D2, DINOSAUR, RAT, SHOTGUN_LADY)
- Switches: complex multi-chunk system for toggle mechanics (TOGGLE_BLOCK, TOGGLE_BIT, TOGGLE_OBJECT)

## Conventions / Style
- C99/C11, `-Wpedantic -Werror -Wall -Wextra`
- `assert()` used extensively for invariant checking
- `asprintf` for string formatting
- Macros: `C_ARRAY_LEN`, `TILE_IDX`, `ARRAY_*`
- `UNREACHABLE()` macro for impossible branches
- No external dependencies beyond libc (for editor); raylib for the GUI

## GUI (`./a.out gui [--data-dir DIR] [--scale N]`)
- `gui_host.c` (in a.out) dlopens `gui.so`, built by `GUI_BUILD_CMD` in `gui.h` from
  `render.c room.c spl.c gui.c gui_editor.c editor_input.c editor_actions.c editor_draw.c
  editor_screen.c`. A new shared `.c` must be added there AND to `WATCHED[]` in gui.c.
- **The GUI runs the terminal editor's state machine.** `gui_editor.c` owns the editor's
  global `state` inside gui.so and defines the process hooks the editor expects (`end`,
  `get_screen_dimensions`, `debugalltoggle`, `debugany`). Keys are decoded from raylib into
  the editor's `Key` and dispatched via `editor_handle_key()` (editor_input.c); the editor's
  `redraw()` runs headless (`screen_headless`, editor_screen.c) and gui.c draws its cell
  buffer as text, with the rendered room picture over the hex map (F2 swaps the hex map back).
- `gui_bridge.h` is the only header shared by gui.c and gui_editor.c: raylib.h and
  editor_types.h both define `KEY_UP`, `KEY_F1`, ... so no file may include both.
- Keys come from raylib's event queues (`GetKeyPressed`/`GetCharPressed`), never
  `IsKeyPressed`: a room save takes ~50 ms and polled state drops taps. Modifier presses are
  latched for a few frames (`mod_latch`) because a fast Ctrl-X can straddle a frame boundary.
- Text cells are 4*scale x 8*scale px so the 64x22 hex map is exactly the picture; the
  scale follows the window size unless `--scale` pins it. Help is drawn as an overlay. The
  text under the picture scrolls with the wheel (scrollbar on the picture's right edge); the
  editor's last row (macro overlay) is pinned to the window bottom.
- Plain left press is held until release: a click moves the cursor, a drag moves the item
  and leaves the cursor (and Spider-Man) where it was.
- World map right of the picture under the palette: every room rendered into one 8x8 atlas
  texture (refreshed for the room being edited). Left click = go to that room, creating it if the
  file has no such room (`room_create()` in editor_actions.c: blank tiles and all settings 0).
  Only the world map creates rooms: the `r` prompt and exit-walking never do, since exits of 0
  mean "none" and walking to room 0 must not conjure one; right click =
  `transfer_item_to_room()` (editor_actions.c) moves the switch/object under the cursor there,
  or, with a TOGGLE_BIT chunk selected, retargets the chunk's room.
- Switch links: with a switch open (Ctrl-S, any EDIT_SWITCHDETAILS* state) right-click an
  object -> TOGGLE_OBJECT chunk, right-click another switch -> TOGGLE_BIT chunk, right-drag a
  run of tiles -> TOGGLE_BLOCK chunk covering it; all via `switch_add_chunk()`, which selects
  the new chunk for editing exactly as `C` / `c[n]` would.
- Clicks on the info text below the map are handled in the editor itself (`panel_click()` in
  editor_input.c, so the terminal mouse gets them too): the row's text is parsed back from the
  drawn screen — "switch NN"/"switch id NN" opens that switch (`s`+id, Ctrl-S; a click on its
  (entry)= / (once)= / (side)= token also flips that field, the side clockwise), a "(NN) type"
  chunk line selects that chunk, "sprite N"/"block N" goes to the object, a room field label
  (bkgrnd, dmg, left, ...) opens Ctrl-R on that field. Any click first Escapes out of the
  current mode (`editor_cancel_to_plain()`); in room-choosing states a click on the room list
  or the world map types that room number instead.
- The out-of-bounds switch list is pinned at the bottom of the panel with S# badges (redraw()
  records its rows in `panel_oob_*`); the editor's own out-of-bounds map markers are hidden.
- Wheel over the picture or the palette: background +-1; horizontal or Shift+wheel: tileset +-4
  (`ged_adjust_room`). Middle-click on a room tile picks it into the palette.
- Pixel editor right of the picker, for a "graphics source" (`GfxSource` in render.h): the
  BLOCKS tiles, or the sprite frames (24x21, colour 15 transparent, drawn as a checker; the
  "sprites" view stacks SPIDS1 over SPIDS2, `sel_sprite` 0..127). Paints into the RenderCtx
  data (`render_gfx_set_pixel`) and writes the file in the data dir 0.6 s after a stroke,
  keeping a one-time <file>.orig backup. This edits the game's graphics for every room. `u`
  undoes the last stroke while a pixel stroke is the most recent edit (GUI-side stack, no
  redo); "reset" restores the image from the .orig; "pick" is an eyedropper for the next click
  anywhere on screen; "fill" is a mode: while lit, each click on the image flood-fills that region
  (4-connected, one undo entry each); click it again, or press `f` with the pixel editor focused,
  to go back to painting.
  Right-click in the grid picks a colour without moving the keyboard cursor. play.sh copies newer ./BLOCKS/SPIDS1/SPIDS2/SCENERY .EGA and .CGA and
  BACKS.SPL into Spider/ so the game shows edits.
- Backgrounds ("backs" button): the picker shows the 256 SCENERY tiles 16 across; Shift+drag in
  the room paints them into the room's background block (`render_backs_set_tile`, BACKS.SPL
  block = the room's `bkgrnd`, so every room with that number changes: the label lists them),
  middle-click picks the background tile under the mouse, Shift shows a green ghost. Strokes go on
  the same undo stack as pixel strokes (`UNDO_BACKS` entries hold the whole 704-byte block) and
  BACKS.SPL is recompressed and written 0.6 s later (`render_backs_save`: 64 offsets + size header,
  `spl_compress` per present block, one-time BACKS.SPL.orig). The game's size limit for BACKS.SPL
  is unknown; growing past the original only warns on stderr. Scenery tile pixels are edited in
  the same pixel editor (`GFX_SCENERY`; tile 0 is locked, it is never drawn).
- F3 switches the graphics files being shown and edited between .EGA and .CGA (`set_gfx_mode`:
  pending edits written, `render_open(dir, cga)`, palette / sheets / Spider-Man / atlas rebuilt,
  pixel undo cleared). The .CGA files are the same pictures packed 2 bpp (16-byte tiles, 128-byte
  sprite frames, key colour 3) with the 4-colour CGA palette; render.c's `img_pixel` /
  `img_set_pixel` handle both layouts, `render_palette(ctx)` / `render_colours(ctx)` give the
  mode's palette, and the swatches show only its colours. BACKS.SPL is shared by both modes.
  CGA sprites carry their transparency in MASKS1/2.CGA (AND masks, 3 = keep background); the
  editor reports a masked pixel as colour 4, shows it as a 5th "transparent" swatch, and an
  undo record / reset / save of a CGA sprite covers its mask frame too (`has_mask`, `mask_of`).
  The two sets are separate files: an EGA edit is not mirrored into the CGA file or vice versa.
- Two rows of buttons under the pixel editor: reset / tiles / sprites / backs, then font / screens /
  import / export (/ block). "font" is CHARSET (128 tiles, 16 across). "screens" enters screen mode
  (`g->screens_view`): the whole window becomes the chosen full-screen picture (LOADSCRN, WINSCRN,
  PAGE1-4 320x200, HISCORE 320x38, SPIDFIGR / SKELFIGR 64x192; row-planar in EGA,
  `render_gfx_is_screen`) at the largest integer zoom left of a side panel with the screens list,
  swatches, pick / fill / import / export / room buttons and notes (`draw_screen_mode`,
  `screen_mode_mouse`, texture `scr_tex`). Painting is click / drag on the picture, right-click
  picks; keyboard: the list has focus on entry, Tab cycles list / colours / buttons / picture
  (`FOCUS_PICKER` / `FOCUS_COLOURS` / `FOCUS_BUTTONS` / `FOCUS_EDITOR`), arrows and hjkl move within
  the focused one, Enter / Space on the list goes to the picture, on a button presses it, on the
  picture paints; f fill, u undo; other keys are swallowed (the room is not on screen); Esc or
  "room" returns to the normal layout. Undo entries hold a whole image (GFX_MAX_BYTES).
  "export" writes the current image as <data dir>/<FILE>.png (screens) or <FILE>-<idx>.png (sheet
  images); "import" reads that file back, or any PNG dropped on the window: scaled to the image with
  nearest neighbour and snapped to the mode's palette, transparent pixels -> the sprite key colour.
- Sprite frames map to creatures (`CREATURES[]` in gui.c, ranges verified against the game);
  with a sprite selected, Shift+click in the room places that creature (`ged_place_sprite`:
  retypes the object under the cursor or creates one).
- Block object under the cursor: its tile grid replaces the pixel editor (`ged_block_*`);
  click paints the picked tile (with the attribute buttons' bits), w+/w-/h+/h- call the
  editor's stretch/shrink, "gfx" hides the panel until the cursor leaves the block.
- Keyboard focus (`g->focus`): Tab cycles room / picker / editor / colours / buttons / world map,
  or click one; arrows and hjkl move that panel's selection (colours: moving selects the paint
  colour; buttons: left/right along the row, up/down to the pass/solid/magnet/slip row in the tiles
  view, Enter/Space presses; both skipped while the block panel is up),
  Enter/Space applies it (world map: go there;
  picker: put tile / creature at the room cursor; editor: paint). Esc, or any other key that
  is forwarded to the editor (`r`, `s`, digits, Ctrl-X, ...), returns focus to the room, so
  `r` then `h`/`l` types into the room prompt rather than moving the world-map selection. `u` after a mouse-driven room edit goes to the room undo.
- The hint text lives in the Ctrl-? overlay (GUI lines appended under the editor's box).
- `gui_smoke_test.sh` runs in a sandbox copy (sources, binaries, Spider/, ROOMS.SPL) so it
  never touches the live files or a GUI you have open.
- Ctrl-P / play.sh: `PLAY_ROOM` (set by fork_play) makes play.sh swap the current room into
  slot 1 of the copy the game runs from, so you start in the room being edited.
- GUI-only: Spider-Man drawn at the cursor as a size reference; tileset palette right of the
  picture (click selects, Shift+drag / middle-drag paints; one undo entry per stroke,
  written 0.4 s after the stroke). Left/right click go to the editor's mouse handling.
- `./gui_smoke_test.sh` drives the GUI headlessly (Xvfb + xdotool) and screenshots each step.

## Raylib notes
- Raylib 6.0 prebuilt (headers, `libraylib.a`, `libraylib.so`, zlib licence) is committed in
  `raylib-6.0_linux_amd64/` so a fresh clone builds with `make` / `make release` and nothing else
- `../breadboard/breadboard.c` is the original reference for the raylib + dlopen hot-reload pattern
