#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "room.h"

#if __has_attribute(__fallthrough__)
# define fallthrough                    __attribute__((__fallthrough__))
#else
# define fallthrough                    do {} while (0)  /* fallthrough */
#endif

/* Terminal control sequences */
#define SAVE_CURSOR "\x1b" "7"
#define RESTORE_CURSOR "\x1b" "8"
#define HIDE_CURSOR "\x1b[?25l"
#define SHOW_CURSOR "\x1b[?25h"

// really this is alt buff
#define SAVE_SCREEN    "\x1b[?47h"
#define RESTORE_SCREEN "\x1b[?47l"

// really this is alt buff with cursor save and clear on exit
#define ENABLE_ALT_BUFFER  /*"\x1b[?1049h" */
#define DISABLE_ALT_BUFFER /*"\x1b[?1049l" */

#define RESET_GFX_MODE "\x1b[0m"
#define CLEAR_SCREEN   "\x1b[0J"

/* Screen dimensions */
#define MIN_HEIGHT (HEIGHT_TILES + 1)
#define MIN_WIDTH (2 * WIDTH_TILES)

/* Key sequences — raw char for ESC (used in help text / terminal sequences) */
#define ESCAPE '\033'

/* Key modifier bitmask */
typedef enum {
    MOD_NONE  = 0,
    MOD_SHIFT = 1,
    MOD_ALT   = 2,
    MOD_CTRL  = 4,
} KeyMod;

/* Logical key code */
typedef enum {
    KEY_CHAR,        /* printable; .ch holds the character */
    KEY_CTRL,        /* control byte; .ch holds 'A'–'_' (e.g. Ctrl-P → .ch='P') */
    KEY_ESC,
    KEY_BS,          /* 0x7f — backspace/delete on most terminals */
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_HOME, KEY_END,
    KEY_PAGE_UP, KEY_PAGE_DOWN,
    KEY_INSERT, KEY_DELETE,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4,  KEY_F5,  KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_MOUSE,
    KEY_UNKNOWN,
} KeyCode;

/* Mouse button codes (KEY_MOUSE): raw button byte from terminal.
 * Base buttons: 0=left, 1=middle, 2=right.
 * Scroll: 64=up, 65=down, 66=left, 67=right.
 * Modifier bits in button byte: +8=Alt, +16=Ctrl. */
#define MOUSE_BTN(b)      ((b) & 0x03)
#define MOUSE_IS_SCROLL(b) ((b) >= 64)
#define MOUSE_MOD_ALT(b)  ((b) & 0x08)
#define MOUSE_MOD_CTRL(b) ((b) & 0x10)

/* Decoded key */
typedef struct {
    KeyCode  code;
    char     ch;    /* KEY_CHAR: the char; KEY_CTRL: 'A'–'_' */
    uint8_t  mod;   /* bitmask of KeyMod */
    struct {
        int16_t  x;       /* 0-indexed terminal column */
        int16_t  y;       /* 0-indexed terminal row */
        uint8_t  button;  /* raw button byte (use MOUSE_* macros) */
        bool     release; /* true = button-up event */
    } mouse;
} Key;

typedef ARRAY(Key) KeyArray;

/* Screen buffer */
#define SCREEN_MAX_WIDTH  512
#define SCREEN_MAX_HEIGHT 512

#define ATTR_BOLD  1
#define ATTR_UNDER 2
#define ATTR_BLINK 4

typedef struct {
    char    ch;    /* character ('\0' = unset/space) */
    uint8_t fg;    /* 0 = default; 1–8 = ANSI colors 30–37 */
    uint8_t bg;    /* 0 = default; 1–8 = ANSI colors 40–47 */
    uint8_t attr;  /* bitmask of ATTR_* above */
} Cell;

/* Types */
typedef struct {
    int x;
    int y;
} v2;

struct debug {
    bool hex;
    bool pos;
    bool data;
    bool neighbours;
    bool objects;
    bool switches;
    bool tiles;     /* GUI: tint every tile by its class (pass-through / solid / magnetic / slippery) */
    bool spidey;    /* GUI: draw Spider-Man at the cursor as a size reference (default on); off shows just the cursor square */
    bool switch_links;   /* GUI: draw every switch chunk's link line/arrow (default on); off shows only the active switch's (viewed or being edited) */
    bool all;
};

typedef enum {
    NORMAL,
    TILE_EDIT,

    GOTO_OBJECT,
    GOTO_SWITCH,
    GOTO_ROOM,
    GOTO_COORD,

    EDIT_ROOMNAME,
    EDIT_ROOMDETAILS,
    EDIT_ROOMDETAILS_NUM,
    EDIT_ROOMDETAILS_ROOM,

    EDIT_SWITCHDETAILS,
    EDIT_SWITCHDETAILS_XY,

    EDIT_SWITCHDETAILS_SELECT_CHUNK,
    EDIT_SWITCHDETAILS_CHUNK_BLOCK_DETAILS,
    EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS,
    EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS,
    EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS_ROOM,
    EDIT_SWITCHDETAILS_CHUNK_OBJECT_DETAILS,

    EDIT_MOVERTAB_OBJECT,

    TOGGLE_DISPLAY,
    NUM_STATES
} game_state_state;

typedef enum {
    CLIP_NONE,
    CLIP_OBJECT,
    CLIP_SWITCH,
    CLIP_CHUNK,
    CLIP_TILE,
    CLIP_ROOM,
} ClipType;

typedef struct {
    ClipType type;
    uint8_t tile;  /* background tile stored when type == CLIP_SWITCH */
    union {
        struct RoomObject object; /* tiles malloc'd, must free before overwrite */
        struct SwitchObject sw;  /* chunks.data malloc'd, must free before overwrite */
        struct SwitchChunk chunk; /* plain copy, no pointers */
        struct {                  /* a whole room: deep copy (room_snapshot_copy), free with room_snapshot_free */
            struct DecompressedRoom data;
            uint8_array rest;     /* the room's trailing bytes */
            uint8_t index;        /* where it was yanked from */
        } room;
    };
} Clipboard;

typedef struct {
    uint8_t room_idx;
    bool grouped;  /* true = pop one more entry from same stack (cross-room moves) */
    bool valid;    /* room_idx's own Room.valid at snapshot time (room_move() can flip this) */
    struct DecompressedRoom snapshot;
    uint8_t movertab[256 * 3];  /* MOVERTAB.DAT is a separate file, not part of the room - snapshotted here too so undo covers it (Hugh: "undo doesn't undo movertab entries") */
} UndoEntry;

typedef struct {
    game_state_state current_state;
    game_state_state previous_state;
    size_t size;
    v2 cursors[64];
    size_t current_level;
    v2 screen_dimensions;
    struct termios original_termios;
    /* mtime ROOMS.SPL had right after our own last successful write, so the
     * "did something else change ROOMS.SPL on disk" poll in editor.c doesn't
     * mistake our own save for an external edit and free+reread state out
     * from under whatever the UI is mid-way through doing with it. */
    struct timespec last_own_write_mtim;
    /* Where ROOMS.SPL and MOVERTAB.DAT are read and written: main()'s --data-dir, or "."
     * (room.h data_path). Lives here so it survives hot reloads and reaches editor.so /
     * gui.so, which have their own copy of room.c and no view of main()'s variables. */
    char data_dir[1024];
    RoomFile rooms;
    bool resized;
    bool help;
    struct debug debug;
    // FIXME have a pos for each room now that there is a goto

    uint16_t partial_byte;

    /* goto-coord state (g / G) */
    int  goto_coord_x;    /* -1 = not yet entered */
    bool goto_coord_move; /* false = move cursor (g), true = warp item (G) */

    char room_detail;
    char room_name[24];
    size_t roomname_length;
    size_t roomname_cursor;
    bool room_delete_mode;   /* GOTO_ROOM: Ctrl-D toggles; next room picked is blanked, not entered */

    size_t current_switch;
    size_t current_chunk;
    bool switch_on;
    size_t hot_preview_switch;   /* NORMAL/TILE_EDIT: 1-based switch index 'O' is previewing while just standing on it (0 = none), independent of current_switch (Hugh: "allowed when just viewing a switch") */
    size_t current_object;   /* EDIT_MOVERTAB_OBJECT: which of the current room's objects */
    bool movertab_raw;       /* EDIT_MOVERTAB_OBJECT: raw hex entry (byte 2 only) instead of the attribute picker */
    char movertab_block_focus;   /* EDIT_MOVERTAB_OBJECT, block only: 0 (hjkl/arrows adjust the x/y offset), or 's'/'d'/'w' (hjkl/arrows and |/- adjust speed/direction/walls instead) */
    char chunk_value_focus;      /* EDIT_SWITCHDETAILS_CHUNK_OBJECT_DETAILS: 0, or 's'/'d'/'w' (same speed/direction/walls scheme as a block's MOVERTAB entry, see block_speed_ratchet/block_direction_set) */
    bool chunk_value_raw;        /* EDIT_SWITCHDETAILS_CHUNK_OBJECT_DETAILS: raw hex entry for chunk->value instead of the attribute picker (toggled by 'r', same convention as movertab_raw) */

    Clipboard clipboard;

    /* MOVERTAB.DAT: 256 entries x 3 bytes, indexed by global object number (see
     * recomputeBases). Loaded once (movertab_load) and edited object-by-object
     * (EDIT_MOVERTAB_OBJECT, entered with Ctrl-M over an object). */
    uint8_t movertab[256 * 3];
    bool movertab_loaded;
    bool movertab_load_attempted;   /* redraw() tries movertab_load() once, not every frame */

    ARRAY(UndoEntry) undo_stack;
    ARRAY(UndoEntry) redo_stack;
    size_t saved_undo_len;      // undo_stack.length when ROOMS.SPL was last written (editor_save reverts to here on refusal)
    char status_message[80];

    /* Mouse drag state */
    bool    mouse_dragging;     /* left button currently held */
    int16_t mouse_drag_tile_x;  /* tile where press occurred */
    int16_t mouse_drag_tile_y;
    bool    mouse_drag_has_obj; /* draggable (object/switch/chunk) was at press tile */

    /* Macro record/replay */
    bool       macro_meta;           /* 'm' pressed, awaiting slot key */
    bool       macro_replay_pending; /* ',' pressed, awaiting slot key */
    bool       macro_recording;      /* currently recording */
    char       macro_slot;           /* slot being recorded to ('a'–'z') */
    char       macro_last_saved;     /* '\0' = none */
    char       macro_last_replayed;  /* '\0' = none */
    KeyArray   macro_buf;            /* keys being recorded */
    KeyArray   macro_slots[26];      /* named slots a-z */
    KeyArray   macro_replay;         /* injection queue */

    Cell screen_prev[SCREEN_MAX_HEIGHT * SCREEN_MAX_WIDTH];
    Cell screen_curr[SCREEN_MAX_HEIGHT * SCREEN_MAX_WIDTH];
} game_state;

extern game_state *state;

/* Forward declarations for screen buffer functions (defined in editor_screen.c) */
int __attribute__((format(printf, 1, 2))) scr_printf(const char *fmt, ...);
void screen_flush(void);

/* GOTO must come after game_state definition since it references state */
#define GOTO(_x, _y)     do { \
    assert((_x) < state->screen_dimensions.x && "width out of bounds"); \
    assert((_y) < state->screen_dimensions.y && "height out of bounds"); \
    scr_printf("\033[%d;%dH", (_y) + 1, (_x) + 1); \
} while (false)

#define UNREACHABLE() do { \
    fprintf(stderr, "%s:%d: UNREACHABLE\n", __FILE__, __LINE__); \
    if (state != NULL) { \
        assert(tcsetattr(STDIN_FILENO, TCSANOW, &state->original_termios) == 0); \
        freeRoomFile(&state->rooms); \
        free(state); \
    } \
    exit(1); \
} while (false)

/* Key parser — decodes raw bytes into a Key struct, advancing *i */
Key parse_key(const char *buf, int *i, int n);

/* Key dispatch (editor_input.c): macro layer + current state's handler */
void editor_handle_key(Key key);
bool editor_replay_step(void);
void editor_cancel_to_plain(void);        /* Escape out to NORMAL/TILE_EDIT */
void editor_feed_room_number(int room);   /* type a room id into a room-choosing state */

/* Screen buffer output switch (editor_screen.c): true = GUI renders the cells */
extern bool screen_headless;
/* Out-of-bounds switch list placement recorded by redraw() (editor_draw.c) */
extern int panel_oob_switch_row[16], panel_oob_switch_id[16];
extern size_t panel_oob_switch_n;

/* GUI helpers (editor_actions.c) */
bool transfer_item_to_room(size_t room_idx);
bool room_move(size_t src, size_t dst);   // world map drag: src takes dst's id, discarding dst's own room
bool room_create(size_t room_idx);   // make an absent room exist, entirely blank (tiles and settings all 0); false if it already does
bool room_prune_blank(size_t room_idx);   // remove a room again if it is still entirely blank (not the current one); true if it was
// A switch's index shifts when it's removed from a room (deleted, or moved to another room) -
// fix up any TOGGLE_BIT chunk elsewhere that named a switch after it in that room, so it still
// points at the same switch (see editor_actions.c for why this exists).
void fixup_toggle_bit_after_switch_removed(uint8_t room_idx, size_t removed_switch_idx);
void movertab_load(void);            // read ./MOVERTAB.DAT, else Spider/MOVERTAB.DAT(.original), else zero; sets movertab_loaded
bool movertab_save(void);            // write state->movertab to ./MOVERTAB.DAT
int movertab_offset_x(const uint8_t *entry);                     // decode a MOVERTAB entry's byte 1 (x offset, whole tiles)
int movertab_offset_y(const uint8_t *entry, bool is_sprite);     // decode a MOVERTAB entry's byte 0 (y offset, whole tiles) - sprite/block encodings differ
void movertab_clamp_offset(uint8_t *entry, bool is_sprite, int x, int y);   // pull the x/y offset back in if it would draw outside the room at this tile position
// Whether a switch or one of its TOGGLE_BLOCK/memory chunks is open for editing, and if so
// which switch/chunk and whether the "switch on" preview ('o'/'O') is active - the map (and,
// via ged_hot_block_preview, the GUI picture) then shows the "on" tile there instead of the
// idle "off" one. *chunk_idx is -1 when the whole switch is open (EDIT_SWITCHDETAILS, not one
// specific chunk) - every TOGGLE_BLOCK chunk it owns previews together, matching the real
// game's single state bit per switch.
bool editor_hot_block_preview(int *switch_idx, int *chunk_idx, bool *on);
// switch_side_hidden / switch_entry_effective: declared in room.h (pure room-data logic).
bool chunk_target_is_sprite(struct SwitchChunk *chunk);   // whether chunk->index currently names a sprite in this room (false, incl. out of bounds, means treat as a block)
bool editor_save(void);              // writeRooms(); a refused write (room/file size limits) undoes the change and names which in the status line
void clipboard_clear(void);          // free whatever the clipboard holds; type becomes CLIP_NONE
void room_snapshot_copy(struct DecompressedRoom *dst, const struct DecompressedRoom *src);   // deep copy (objects, switches)
void room_snapshot_free(struct DecompressedRoom *snap);
int  switch_add_chunk(size_t sw_idx, struct SwitchChunk chunk);

/* Forward declarations for cross-module calls */
void end(void);
void debugalltoggle(void);
bool debugany(void);
void get_screen_dimensions(void);
void move(int dx, int dy);
void warp_to(int tx, int ty);
void stretch(int dx, int dy);
void shrink(int dx, int dy);
bool update(void);
bool process_input(void);
void show_help(void);
void redraw(void);
void push_undo(uint8_t room_idx, bool grouped);
uint8_t do_undo(void);
uint8_t do_redo(void);
void free_undo_stacks(void);
