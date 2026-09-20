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
// Tall enough that the panel (scrollable) and the out-of-bounds switch list
// (its own scroll, gui.c) always have somewhere to put every line the editor
// draws - a room can have up to 15 switches, each with up to 16 chunks, and
// several of those switches off-grid at once. 400 rows * 132 cols is well
// under game_state's screen_curr/screen_prev cap (SCREEN_MAX_HEIGHT 512 *
// SCREEN_MAX_WIDTH 512), so there is plenty of headroom either way.
#define GED_ROWS 400
#define GED_MAP_X 0
#define GED_MAP_Y 1

// Create the editor state around main()'s room file (taking over its data),
// or re-adopt `prev` after a hot reload. `prev` is discarded (its rooms handed
// back to `host`) when the editor's state struct has changed size.
void *ged_open(RoomFile *host, void *prev, const char *data_dir);   // data_dir: where ROOMS.SPL / MOVERTAB.DAT are (room.h data_path)
// Hand the room data back to `host` and free the editor state (window closed).
void  ged_close(void *es, RoomFile *host);

void  ged_key(GKey k);             // dispatch one key through the editor
bool  ged_replay_step(void);       // run one queued macro key; false if none
bool  ged_quit_requested(void);    // 'q' in NORMAL/TILE_EDIT asked to quit
void  ged_new_input_batch(void);   // clears the status line (per keypress, as the TUI does)

void  ged_redraw(void);            // run the editor's redraw into the cell buffer
const GCell *ged_cells(int *stride);
const uint8_t *ged_movertab(void); // state->movertab, for render_set_movertab

// Queries for the picture layer.
int   ged_room(void);
void  ged_cursor(int *x, int *y);
void  ged_set_cursor(int x, int y);
void  ged_clear_selection(void);   // forget the selected switch/chunk (as moving the cursor does)
bool  ged_show_objects(void);
bool  ged_show_switches(void);
bool  ged_show_tiles(void);       /* Ctrl-D t: tint tiles by class, hurting tiles red */
unsigned ged_save_count(const char **file);   /* bumps on every editor write; *file names it (ROOMS.SPL, MOVERTAB.DAT, BACKS.SPL) */
typedef struct RenderCtx RenderCtx;
bool  ged_hex(void);                         /* Ctrl-h: numbers shown in hex (default) or decimal */
void  ged_set_render(RenderCtx *ctx);        /* the GUI's render context: backgrounds for the editor's Ctrl-B / F2 background view */
void  ged_set_backs_view(bool on);
bool  ged_backs_view_on(void);
void  ged_backs_show(bool *tiles, bool *blocks, bool *sprites, bool *chunks);   /* Ctrl-D in background mode: which of the room's things to draw over the background */               /* the editor's own idea of it after its keys ran (a macro replay may have switched it) */           /* F2 with the backs picker: the editor draws and edits the background block's hex */
bool  ged_backs_edited(void);                /* a background tile was edited through the editor since the last call (redraw the picture) */
bool  ged_show_spidey(void);      /* Ctrl-D c: draw Spider-Man at the cursor (default on) */
bool  ged_show_switch_links(void);  /* Ctrl-D l: every switch chunk's link (default on); off = just the active switch's */
// switch_side_hidden / switch_entry_effective: declared in room.h (pure room-data logic, no
// ged_ wrapper needed - gui_bridge.h already pulls in room.h above).
RoomFile *ged_rooms(void);
bool  ged_help_shown(void);
int   ged_current_switch(void);    // the editor's selected switch (Ctrl-S / s#), or -1
int   ged_goto_kind(void);         // 1 = choosing an object (o), 2 = a switch (s), else 0
bool  ged_map_is_text(void);       // the editor has replaced the hex map with text (room list)

// World map. Left click: go to that room (in the plain editing states), or,
// while the editor asks for a room (goto room, a neighbour in room details, a
// chunk's room), answer with it: then true is returned so the GUI can hand
// keyboard focus back to the room for the next question.
// Right click: move the switch/object under the cursor into that room, or, while
// a switch-link chunk is selected, point the chunk at that room.
// tx, ty: the tile position within the destination room a thumbnail click landed on (-1, -1
// if there isn't one, e.g. keyboard Enter or a switch-link jump) - the cursor goes there
// instead of wherever it was left on a previous visit.
bool  ged_map_click(int room, bool right, int tx, int ty);
// Drag-drop a thumbnail onto another: `src` takes over `dst`'s id, discarding whatever room
// was at `dst` (room.c's swapRooms() plus dropping src's post-swap leftovers - see room_move()).
// false if src has no room, dst == src, or either is room 0.
bool  ged_room_move(int src, int dst);
// Out-of-bounds switch list (part of the ordinary scrolled panel, not pinned): the switch
// listed on a given text row (-1 if none); clicking a row goes through the editor's own click
// handling.
int   ged_oob_switch_at_row(int row);
// Step the current room's background (+-1, wraps 0..63) and tileset (+-4, 0..28).
void  ged_adjust_room(int dbackground, int dtileset);

// Switch links. ged_linking(): a switch is open for editing (Ctrl-S), so
// right-drag / right-click in the room create chunks on it:
int   ged_linking(void);                                   // 0 no, else the switch index + 1
void  ged_link_block(int x, int y, int w, int h);          // TOGGLE_BLOCK covering the run
void  ged_link_object(int object_idx);                     // TOGGLE_OBJECT
void  ged_link_switch(int switch_idx);                     // TOGGLE_BIT to a switch in this room
int   ged_item_at(int tx, int ty, int *idx);               // 1 switch preamble, 2 object, else 0
int   ged_sprite_hit(int px, int py);                      // room-pixel (0..ROOM_PX_W/H) hit test against a sprite's rendered box (its MOVERTAB x/y offset may put this well outside its logical tile) - object index, or -1
// Open switch `sw` of the current room for editing and select its chunk
// `chunk` (0 = the switch itself), as s<id>, Ctrl-S, c<n> would.
void  ged_select_chunk(int sw, int chunk);
int   ged_current_chunk(void);     // the chunk selected for editing (0 = none / the switch itself)
bool  ged_hot_block_preview(int *switch_idx, int *chunk_idx, bool *on);  // a TOGGLE_BLOCK/memory chunk open for editing: which one, and whether the "switch on" preview is active
void  ged_goto_object(int idx);    // leave editing and put the cursor on object idx (as o<n> does)
void  ged_edit_movertab(int idx);  // open object idx's MOVERTAB editor without moving the cursor there (unlike ged_goto_object)
void  ged_cancel(void);            // leave any switch / chunk / room editing mode (Escape until plain)
void  ged_left_room(int prev);     // the GUI moved from room `prev`: a still-blank room is removed again

// GUI-side tile painting (palette + drag): one undo entry per stroke, room
// written after the stroke (ged_flush) rather than per cell.
int   ged_tile_at(int tx, int ty);         // foreground tile byte at a room cell (block objects included)
void  ged_paint_begin(void);
void  ged_paint(int tx, int ty, uint8_t tile);   // into the room grid, or the block object covering the cell

// Put a sprite of this type/damage at the cursor: retype the object there, or
// create one (max 7 objects). false if it could not.
bool  ged_place_sprite(int sprite_type, int damage);

// Block object under the cursor: its bounds and tiles, painting into it, and
// growing / shrinking it (the editor's stretch / shrink: dx for width, dy for height).
bool  ged_block_at_cursor(int *idx, int *x, int *y, int *w, int *h);
int   ged_block_tile(int idx, int bx, int by);
void  ged_block_set_tile(int idx, int bx, int by, uint8_t tile);
void  ged_block_resize(int dx, int dy, bool grow);
bool  ged_flush(void);             // write pending paints; true if it wrote

// Whole current room, as `display`-format text (room_export_text/room_import_text, room.h).
// Import leaves every other room's own data untouched, though its object/switch count changing
// does shift every later room's index bases (recomputed, but not individually undo-tracked).
bool  ged_room_export_text(const char *path);
bool  ged_room_import_text(const char *path);

#endif /* GUI_BRIDGE_H */
