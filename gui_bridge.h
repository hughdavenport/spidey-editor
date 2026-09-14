#ifndef GUI_BRIDGE_H
#define GUI_BRIDGE_H

// Bridge between the raylib front end (gui.c) and the terminal editor's state
// machine (editor_input.c / editor_actions.c / editor_draw.c), implemented in
// gui_editor.c. It exists because raylib.h and editor_types.h both define
// KEY_UP, KEY_F1, ... so no single translation unit can include both.
//
// The GUI decodes raylib input into GKey, hands it to ged_key(), then asks
// the editor to redraw its text screen headlessly and renders those cells
// itself (with the picture of the room drawn over the hex map).

#include <stdbool.h>
#include <stdint.h>

#include "room.h"

typedef enum {
    GK_CHAR, GK_CTRL, GK_ESC, GK_BS,
    GK_UP, GK_DOWN, GK_LEFT, GK_RIGHT,
    GK_HOME, GK_END, GK_PAGE_UP, GK_PAGE_DOWN, GK_INSERT, GK_DELETE,
    GK_F1, GK_F2, GK_F3, GK_F4, GK_F5, GK_F6,
    GK_F7, GK_F8, GK_F9, GK_F10, GK_F11, GK_F12,
    GK_MOUSE,
} GKeyCode;

enum { GK_MOD_SHIFT = 1, GK_MOD_ALT = 2, GK_MOD_CTRL = 4 };

typedef struct {
    GKeyCode code;
    char     ch;        // GK_CHAR: the character; GK_CTRL: 'A'..'_'
    uint8_t  mod;       // GK_MOD_* bitmask
    int      col, row;  // GK_MOUSE: text-cell position
    int      button;    // GK_MOUSE: 0 left, 1 middle, 2 right
    bool     release;
} GKey;

// One character cell of the editor's text screen (mirrors editor_types.h Cell).
typedef struct {
    char    ch;     // '\0' = blank
    uint8_t fg;     // 0 = default; 1..8 = ANSI colours 30..37
    uint8_t bg;     // 0 = default; 1..8 = ANSI colours 40..47
    uint8_t attr;   // 1 bold, 2 underline, 4 blink
} GCell;

// Text-screen geometry the editor lays itself out on. The room's hex map is
// cells (0..63, 1..22); everything else is status panels and help.
#define GED_COLS 132
#define GED_ROWS 50
#define GED_MAP_X 0
#define GED_MAP_Y 1

// Create the editor state around main()'s room file (taking over its data),
// or re-adopt `prev` after a hot reload. `prev` is discarded (its rooms handed
// back to `host`) when the editor's state struct has changed size.
void *ged_open(RoomFile *host, void *prev);
// Hand the room data back to `host` and free the editor state (window closed).
void  ged_close(void *es, RoomFile *host);

void  ged_key(GKey k);             // dispatch one key through the editor
bool  ged_replay_step(void);       // run one queued macro key; false if none
bool  ged_quit_requested(void);    // 'q' in NORMAL/TILE_EDIT asked to quit
void  ged_new_input_batch(void);   // clears the status line (per keypress, as the TUI does)

void  ged_redraw(void);            // run the editor's redraw into the cell buffer
const GCell *ged_cells(int *stride);

// Queries for the picture layer.
int   ged_room(void);
void  ged_cursor(int *x, int *y);
void  ged_set_cursor(int x, int y);
bool  ged_show_objects(void);
bool  ged_show_switches(void);
RoomFile *ged_rooms(void);
bool  ged_help_shown(void);
int   ged_goto_kind(void);         // 1 = choosing an object (o), 2 = a switch (s), else 0
bool  ged_map_is_text(void);       // the editor has replaced the hex map with text (room list)

// World map. Left click: go to that room (in the plain editing states).
// Right click: move the switch/object under the cursor into that room, or, while
// a switch-link chunk is selected, point the chunk at that room.
void  ged_map_click(int room, bool right);
// Out-of-bounds switch list: its text rows and the switch listed on a row (-1
// if none); clicking a row goes through the editor's own click handling.
void  ged_oob_rows(int *r0, int *r1);
int   ged_oob_switch_at_row(int row);
bool  ged_is_oob_marker(int col, int row);   // text cell holding an out-of-bounds switch's map marker
// Step the current room's background (+-1, wraps 0..63) and tileset (+-4, 0..28).
void  ged_adjust_room(int dbackground, int dtileset);

// Switch links. ged_linking(): a switch is open for editing (Ctrl-S), so
// right-drag / right-click in the room create chunks on it:
int   ged_linking(void);                                   // 0 no, else the switch index + 1
void  ged_link_block(int x, int y, int w, int h);          // TOGGLE_BLOCK covering the run
void  ged_link_object(int object_idx);                     // TOGGLE_OBJECT
void  ged_link_switch(int switch_idx);                     // TOGGLE_BIT to a switch in this room
int   ged_item_at(int tx, int ty, int *idx);               // 1 switch preamble, 2 object, else 0

// GUI-side tile painting (palette + drag): one undo entry per stroke, room
// written after the stroke (ged_flush) rather than per cell.
int   ged_tile_at(int tx, int ty);         // foreground tile byte at a room cell
void  ged_paint_begin(void);
void  ged_paint(int tx, int ty, uint8_t tile);
bool  ged_flush(void);             // write pending paints; true if it wrote

#endif /* GUI_BRIDGE_H */
