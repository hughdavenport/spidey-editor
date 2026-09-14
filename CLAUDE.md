# spidey-editor: CLAUDE.md

## Project Overview
A terminal-based map editor for "The Amazing Spider-Man" (1990 DOS game). Reads/writes `ROOMS.SPL` binary format with RLE compression. Two modes: patch (CLI commands) and interactive editor (ANSI terminal UI with hot-reloading).

## Build
```sh
make         # builds ./a.out with ASAN + debug
./a.out editor   # interactive editor
./a.out help     # show all patch commands
```

Makefile compiles `main.c room.c editor.c` with `-fsanitize=address -ggdb -Werror`.

## Source Files

| File | Purpose |
|------|---------|
| `main.c` | Patch/CLI mode: `rooms`, `display`, `patch`, `delete` subcommands |
| `room.c` | ROOMS.SPL binary decode/encode with RLE compression |
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
  texture (refreshed for the room being edited). Left click = go to that room; right click =
  `transfer_item_to_room()` (editor_actions.c) moves the switch/object under the cursor there,
  or, with a TOGGLE_BIT chunk selected, retargets the chunk's room.
- Switch links: with a switch open (Ctrl-S, any EDIT_SWITCHDETAILS* state) right-click an
  object -> TOGGLE_OBJECT chunk, right-click another switch -> TOGGLE_BIT chunk, right-drag a
  run of tiles -> TOGGLE_BLOCK chunk covering it; all via `switch_add_chunk()`, which selects
  the new chunk for editing exactly as `C` / `c[n]` would.
- GUI-only: Spider-Man drawn at the cursor as a size reference; tileset palette right of the
  picture (click selects, Shift+drag / middle-drag paints; one undo entry per stroke,
  written 0.4 s after the stroke). Left/right click go to the editor's mouse handling.
- `./gui_smoke_test.sh` drives the GUI headlessly (Xvfb + xdotool) and screenshots each step.

## Raylib notes
- Raylib 6.0 prebuilt lives in `raylib-6.0_linux_amd64/` (gitignored; fetch the GitHub release)
- `../breadboard/breadboard.c` is the original reference for the raylib + dlopen hot-reload pattern
