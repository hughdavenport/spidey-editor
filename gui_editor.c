// Editor side of the GUI bridge: owns the terminal editor's global `state`
// inside gui.so and adapts it for a windowed front end. See gui_bridge.h.
#include "gui_bridge.h"

#include <string.h>

#include "editor_types.h"

game_state *state = NULL;

static bool quit_requested = false;
static bool paint_pending = false;

/* --- the terminal editor's process-level hooks, GUI flavour --------------- */

// 'q' normally lands here via handle_normal() and must not return (the caller
// follows it with UNREACHABLE()). The GUI intercepts plain 'q' before dispatch
// (see ged_key), so this only runs for EOF-style exits; behave like the TUI.
void end(void) {
    if (state != NULL) {
        if (paint_pending) writeRooms(&state->rooms);
        free_undo_stacks();
        freeRoomFile(&state->rooms);
        free(state);
        state = NULL;
    }
    exit(0);
}

void get_screen_dimensions(void) {
    state->screen_dimensions.x = GED_COLS;
    state->screen_dimensions.y = GED_ROWS;
}

void debugalltoggle(void) {
    state->debug.data = !state->debug.all;
    state->debug.objects = !state->debug.all;
    state->debug.switches = !state->debug.all;
    state->debug.neighbours = !state->debug.all;
    state->debug.unknowns = !state->debug.all;
    state->debug.pos = !state->debug.all;
    state->debug.all = !state->debug.all;
}

bool debugany(void) {
    return state->debug.data ||
        state->debug.objects ||
        state->debug.switches ||
        state->debug.neighbours ||
        state->debug.unknowns ||
        state->debug.pos;
}

/* --- lifecycle ------------------------------------------------------------ */

// Mirrors setup() in editor.c minus the terminal handling.
static game_state *state_new(RoomFile *host) {
    game_state *s = calloc(1, sizeof *s);
    assert(s != NULL && "Not enough memory");
    s->size = sizeof *s;
    memset(s->screen_prev, 0xFF, sizeof s->screen_prev);
    s->rooms = *host;                       // take over the room data
    state = s;
    get_screen_dimensions();
    debugalltoggle();
    s->current_state = TILE_EDIT;
    for (size_t i = 0; i < C_ARRAY_LEN(s->cursors); i++) {
        s->cursors[i].x = WIDTH_TILES / 2;
        s->cursors[i].y = HEIGHT_TILES / 2;
    }
    s->current_level = 1;                   // the game's start room
    s->cursors[1].x = 3;
    s->cursors[1].y = HEIGHT_TILES - 2;
    s->help = false;                        // Ctrl-? shows it; it would hide the panels' space
    s->debug.hex = true;
    return s;
}

static void state_free_keep_rooms(game_state *s) {
    state = s;
    if (s->clipboard.type == CLIP_OBJECT && s->clipboard.object.tiles) free(s->clipboard.object.tiles);
    else if (s->clipboard.type == CLIP_SWITCH) ARRAY_FREE(s->clipboard.sw.chunks);
    free_undo_stacks();
    ARRAY_FREE(s->macro_buf);
    for (size_t mi = 0; mi < 26; mi++) ARRAY_FREE(s->macro_slots[mi]);
    ARRAY_FREE(s->macro_replay);
    free(s);
    state = NULL;
}

void *ged_open(RoomFile *host, void *prev) {
    screen_headless = true;
    game_state *s = prev;
    if (s != NULL && s->size != sizeof *s) {
        fprintf(stderr, "gui: editor state size changed, resetting\n");
        *host = s->rooms;
        state_free_keep_rooms(s);
        s = NULL;
    }
    if (s == NULL) s = state_new(host);
    state = s;
    get_screen_dimensions();
    quit_requested = false;
    return s;
}

void ged_close(void *es, RoomFile *host) {
    game_state *s = es;
    state = s;
    if (paint_pending) { writeRooms(&s->rooms); paint_pending = false; }
    *host = s->rooms;                       // main() frees the room data
    state_free_keep_rooms(s);
}

/* --- input ---------------------------------------------------------------- */

static Key convert(GKey g) {
    Key k = {0};
    k.mod = g.mod;                          // GK_MOD_* == MOD_* bit for bit
    switch (g.code) {
        case GK_CHAR:      k.code = KEY_CHAR; k.ch = g.ch; break;
        case GK_CTRL:      k.code = KEY_CTRL; k.ch = g.ch; break;
        case GK_ESC:       k.code = KEY_ESC; break;
        case GK_BS:        k.code = KEY_BS; break;
        case GK_UP:        k.code = KEY_UP; break;
        case GK_DOWN:      k.code = KEY_DOWN; break;
        case GK_LEFT:      k.code = KEY_LEFT; break;
        case GK_RIGHT:     k.code = KEY_RIGHT; break;
        case GK_HOME:      k.code = KEY_HOME; break;
        case GK_END:       k.code = KEY_END; break;
        case GK_PAGE_UP:   k.code = KEY_PAGE_UP; break;
        case GK_PAGE_DOWN: k.code = KEY_PAGE_DOWN; break;
        case GK_INSERT:    k.code = KEY_INSERT; break;
        case GK_DELETE:    k.code = KEY_DELETE; break;
        case GK_F1:  k.code = KEY_F1;  break; case GK_F2:  k.code = KEY_F2;  break;
        case GK_F3:  k.code = KEY_F3;  break; case GK_F4:  k.code = KEY_F4;  break;
        case GK_F5:  k.code = KEY_F5;  break; case GK_F6:  k.code = KEY_F6;  break;
        case GK_F7:  k.code = KEY_F7;  break; case GK_F8:  k.code = KEY_F8;  break;
        case GK_F9:  k.code = KEY_F9;  break; case GK_F10: k.code = KEY_F10; break;
        case GK_F11: k.code = KEY_F11; break; case GK_F12: k.code = KEY_F12; break;
        case GK_MOUSE:
            k.code = KEY_MOUSE;
            k.mouse.x = (int16_t)g.col;
            k.mouse.y = (int16_t)g.row;
            k.mouse.button = (uint8_t)g.button;
            k.mouse.release = g.release;
            break;
    }
    return k;
}

void ged_key(GKey g) {
    Key k = convert(g);
    // Plain 'q' with nothing to cancel would call end() and exit the process;
    // in a window that is "close the window" instead.
    if (k.code == KEY_CHAR && k.ch == 'q' && k.mod == MOD_NONE &&
            (state->current_state == NORMAL || state->current_state == TILE_EDIT) &&
            !state->help && !state->partial_byte &&
            !state->macro_meta && !state->macro_replay_pending) {
        quit_requested = true;
        return;
    }
    editor_handle_key(k);
}

bool ged_replay_step(void) { return editor_replay_step(); }
bool ged_quit_requested(void) { return quit_requested; }
void ged_new_input_batch(void) { state->status_message[0] = '\0'; }

/* --- frame ---------------------------------------------------------------- */

void ged_redraw(void) { redraw(); }

const GCell *ged_cells(int *stride) {
    _Static_assert(sizeof(GCell) == sizeof(Cell), "GCell must mirror Cell");
    *stride = SCREEN_MAX_WIDTH;
    return (const GCell *)state->screen_prev;   // the frame redraw() just flushed
}

int ged_room(void) { return (int)state->current_level; }
void ged_cursor(int *x, int *y) {
    *x = state->cursors[state->current_level].x;
    *y = state->cursors[state->current_level].y;
}
bool ged_show_objects(void) { return state->debug.objects; }
bool ged_show_switches(void) { return state->debug.switches; }
RoomFile *ged_rooms(void) { return &state->rooms; }
bool ged_help_shown(void) { return state->help; }
int ged_goto_kind(void) {
    return state->current_state == GOTO_OBJECT ? 1 : state->current_state == GOTO_SWITCH ? 2 : 0;
}

/* --- painting ------------------------------------------------------------- */

void ged_paint_begin(void) { push_undo((uint8_t)state->current_level, false); }

void ged_paint(int tx, int ty, uint8_t tile) {
    if (tx < 0 || tx >= WIDTH_TILES || ty < 0 || ty >= HEIGHT_TILES) return;
    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
    room->tiles[TILE_IDX(tx, ty)] = tile;
    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
    paint_pending = true;
}

bool ged_flush(void) {
    if (!paint_pending) return false;
    paint_pending = false;
    assert(writeRooms(&state->rooms));
    return true;
}
