// Editor side of the GUI bridge: owns the terminal editor's global `state`
// inside gui.so and adapts it for a windowed front end. See gui_bridge.h.
#include "gui_bridge.h"

#include <string.h>

#include "editor_types.h"
#include "render.h"

static bool quit_requested = false;
static bool paint_pending = false;

/* --- the terminal editor's process-level hooks, GUI flavour ---------------
 * gui.so has no editor.c, so it needs its own; the release build links
 * editor.c in and its versions check screen_headless instead. */
#ifndef SPIDEY_RELEASE
game_state *state = NULL;


// 'q' normally lands here via handle_normal() and must not return (the caller
// follows it with UNREACHABLE()). The GUI intercepts plain 'q' before dispatch
// (see ged_key), so this only runs for EOF-style exits; behave like the TUI.
void end(void) {
    if (state != NULL) {
        if (paint_pending) editor_save();
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
    state->debug.pos = !state->debug.all;
    state->debug.all = !state->debug.all;
}

bool debugany(void) {
    return state->debug.data ||
        state->debug.objects ||
        state->debug.switches ||
        state->debug.neighbours ||
        state->debug.pos;
}
#endif /* !SPIDEY_RELEASE */

/* --- lifecycle ------------------------------------------------------------ */

// Mirrors setup() in editor.c minus the terminal handling.
static game_state *state_new(RoomFile *host, const char *data_dir) {
    game_state *s = calloc(1, sizeof *s);
    assert(s != NULL && "Not enough memory");
    s->size = sizeof *s;
    snprintf(s->data_dir, sizeof s->data_dir, "%s", data_dir);
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
    s->debug.tiles = true;
    s->debug.spidey = true;
    s->debug.switch_links = true;
    movertab_load();
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

void *ged_open(RoomFile *host, void *prev, const char *data_dir) {
    screen_headless = true;
    game_state *s = prev;
    if (s != NULL && s->size != sizeof *s) {
        fprintf(stderr, "gui: editor state size changed, resetting\n");
        *host = s->rooms;
        state_free_keep_rooms(s);
        s = NULL;
    }
    if (s == NULL) s = state_new(host, data_dir);
    state = s;
    get_screen_dimensions();
    quit_requested = false;
    return s;
}

void ged_close(void *es, RoomFile *host) {
    game_state *s = es;
    state = s;
    if (paint_pending) { editor_save(); paint_pending = false; }
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

// For render_set_movertab, so the room picture mirrors a sprite per its facing/upside-down
// bits (movertab_behavior_desc in room.c). Loaded lazily by redraw() itself, same as the
// object-panel display, so this is never stale/unloaded once any frame has drawn.
const uint8_t *ged_movertab(void) { return state->movertab; }

int ged_room(void) { return (int)state->current_level; }
void ged_cursor(int *x, int *y) {
    *x = state->cursors[state->current_level].x;
    *y = state->cursors[state->current_level].y;
}
bool ged_show_objects(void) { return state->debug.objects; }
bool ged_show_switches(void) { return state->debug.switches; }
bool ged_show_tiles(void)    { return state->debug.tiles; }
unsigned ged_save_count(const char **file) { *file = state->saved_file; return state->save_count; }
void ged_set_render(RenderCtx *ctx) { state->rctx = ctx; }
bool ged_hex(void) { return state->debug.hex; }
bool ged_backs_view_on(void) { return state->backs_view; }
void ged_backs_show(bool *tiles, bool *blocks, bool *sprites, bool *chunks) {
    *tiles = state->backs_show.tiles; *blocks = state->backs_show.blocks;
    *sprites = state->backs_show.sprites; *chunks = state->backs_show.chunks;
}
void ged_set_backs_view(bool on) {
    if (on && state->rctx == NULL) on = false;
    if (state->backs_view != on) state->partial_byte = 0;
    state->backs_view = on;
}
bool ged_backs_edited(void) { bool e = state->backs_edited; state->backs_edited = false; return e; }
bool ged_show_spidey(void)   { return state->debug.spidey; }
bool ged_show_switch_links(void) { return state->debug.switch_links; }
RoomFile *ged_rooms(void) { return &state->rooms; }
void ged_set_cursor(int x, int y) {
    if (x < 0 || x >= WIDTH_TILES || y < 0 || y >= HEIGHT_TILES) return;
    state->cursors[state->current_level].x = x;
    state->cursors[state->current_level].y = y;
}
void ged_clear_selection(void) {
    state->current_switch = 0;
    state->switch_on = false;
    state->current_chunk = 0;
}
bool ged_help_shown(void) { return state->help; }
bool ged_map_is_text(void) {
    return state->current_state == GOTO_ROOM || state->current_state == EDIT_ROOMDETAILS_ROOM ||
           state->current_state == EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS_ROOM;
}
int ged_current_switch(void) { return state->current_switch ? (int)state->current_switch - 1 : -1; }
int ged_goto_kind(void) {
    return state->current_state == GOTO_OBJECT ? 1 : state->current_state == GOTO_SWITCH ? 2 : 0;
}

/* --- world map & switch links ------------------------------------------ */

static bool plain_state(void) {
    return state->current_state == NORMAL || state->current_state == TILE_EDIT;
}

static struct DecompressedRoom *cur_room(void) { return &state->rooms.rooms[state->current_level].data; }

int ged_item_at(int tx, int ty, int *idx) {
    struct DecompressedRoom *room = cur_room();
    for (size_t i = 0; i < room->num_switches; i++) {
        struct SwitchObject *sw = room->switches + i;
        if (sw->chunks.length > 0 && sw->chunks.data[0].type == PREAMBLE &&
                sw->chunks.data[0].x == tx && sw->chunks.data[0].y == ty) { *idx = (int)i; return 1; }
    }
    for (size_t i = 0; i < room->num_objects; i++) {
        struct RoomObject *o = room->objects + i;
        bool hit = o->type == SPRITE ? (tx == o->x && ty == o->y)
                 : (tx >= o->x && tx < o->x + o->block.width && ty >= o->y && ty < o->y + o->block.height);
        if (hit) { *idx = (int)i; return 2; }
    }
    return 0;
}

// A sprite's x/y MOVERTAB offset can draw it well clear of its logical tile (px, py: room pixels,
// 0..ROOM_PX_W/H) - this is what lets a click there still select it (ged_goto_object), same as a
// click on its logical tile already does via ged_item_at. Checked after ged_item_at's plain tile
// hit, not instead of it: a sprite drawn with no offset is still found by that faster path.
int ged_sprite_hit(int px, int py) {
    struct DecompressedRoom *room = cur_room();
    for (size_t i = 0; i < room->num_objects; i++) {
        struct RoomObject *o = room->objects + i;
        if (o->type != SPRITE) continue;
        int dx, dy, w, h; bool fh, fv;
        if (!render_sprite_box(o, state->movertab, (unsigned)room->UNKNOWN_c + (unsigned)i, &dx, &dy, &w, &h, &fh, &fv)) continue;
        if (px >= dx && px < dx + w && py >= dy && py < dy + h) return (int)i;
    }
    return -1;
}

bool ged_map_click(int room, bool right, int tx, int ty) {
    if (room < 0 || room >= 64) return false;
    if (!state->rooms.rooms[room].valid) {
        if (right) return false;                    // nothing to move into a room that does not exist
        if (!room_create((size_t)room)) return false;   // going to an empty slot on the map creates the room (unless the file is full)
    }
    if (right) {
        if (state->current_state == EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS && state->current_switch) {
            struct SwitchObject *sw = cur_room()->switches + state->current_switch - 1;
            if (state->current_chunk < sw->chunks.length) {
                push_undo((uint8_t)state->current_level, false);
                sw->chunks.data[state->current_chunk].room_idx = (uint16_t)room;
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                snprintf(state->status_message, sizeof(state->status_message), "Chunk now targets room %d", room);
                editor_save();
            }
        } else if (plain_state()) {
            if (!transfer_item_to_room((size_t)room))
                snprintf(state->status_message, sizeof(state->status_message), "Nothing under the cursor to move");
        }
        return false;
    }
    // choosing a room (goto room, a room link, a chunk's room): this is the choice;
    // otherwise drop whatever was in progress and go there
    if (state->current_state == GOTO_ROOM || state->current_state == EDIT_ROOMDETAILS_ROOM ||
            state->current_state == EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS_ROOM) {
        editor_feed_room_number(room);
        return true;
    }
    editor_cancel_to_plain();
    state->current_level = (size_t)room;
    state->partial_byte = 0;
    state->current_switch = 0;
    state->switch_on = false;
    state->current_chunk = 0;
    // A Ctrl-click on the thumbnail carries where in it the click fell: land the cursor there.
    // A plain click, keyboard Enter and a switch-link jump pass -1 and leave the cursor where
    // that room last had it.
    if (tx >= 0 && tx < WIDTH_TILES && ty >= 0 && ty < HEIGHT_TILES) {
        state->cursors[room].x = tx;
        state->cursors[room].y = ty;
    }
    return false;
}

bool ged_room_move(int src, int dst) {
    if (src < 0 || src >= 64 || dst < 0 || dst >= 64) return false;
    return room_move((size_t)src, (size_t)dst);
}

int ged_oob_switch_at_row(int row) {
    for (size_t i = 0; i < panel_oob_switch_n; i++)
        if (panel_oob_switch_row[i] == row) return panel_oob_switch_id[i];
    return -1;
}
void ged_adjust_room(int dbackground, int dtileset) {
    if (!plain_state() || (dbackground == 0 && dtileset == 0)) return;
    struct DecompressedRoom *room = cur_room();
    push_undo((uint8_t)state->current_level, false);
    if (dbackground) room->background = (uint8_t)((room->background + 64 + dbackground) % 64);
    if (dtileset) {
        int t = (int)room->tile_offset / 4 * 4 + 4 * dtileset;    // the game uses multiples of 4, 0..28
        while (t < 0) t += 32;
        room->tile_offset = (uint8_t)(t % 32);
    }
    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
    snprintf(state->status_message, sizeof(state->status_message), "bkgrnd %d tileset %d",
             room->background, room->tile_offset);
    editor_save();
}

void ged_select_chunk(int sw, int chunk) {
    if (sw < 0 || sw > 15) return;
    static const char hex[] = "0123456789abcdef";
    editor_cancel_to_plain();
    editor_handle_key((Key){ .code = KEY_CHAR, .ch = 's', .mod = MOD_NONE });
    editor_handle_key((Key){ .code = KEY_CHAR, .ch = hex[sw], .mod = MOD_NONE });
    if (plain_state()) editor_handle_key((Key){ .code = KEY_CTRL, .ch = 'S', .mod = MOD_NONE });   // (goto-switch opens out-of-bounds ones itself)
    if (state->current_state == EDIT_SWITCHDETAILS && chunk > 0 && chunk <= 15) {
        editor_handle_key((Key){ .code = KEY_CHAR, .ch = 'c', .mod = MOD_NONE });
        editor_handle_key((Key){ .code = KEY_CHAR, .ch = hex[chunk], .mod = MOD_NONE });
    }
}

int ged_current_chunk(void) { return (int)state->current_chunk; }

// Whether a switch or one of its TOGGLE_BLOCK/memory chunks is open for editing, and if so
// which and whether the "switch on" preview ('o'/'O') is active - the picture then shows the
// on tile(s) there instead of the idle off tile(s) (Hugh: "when editing a block chunk, change
// the displayed tiles for the on/off value depending on which being edited"; later extended to
// the whole switch, not just one open chunk - see editor_hot_block_preview).
bool ged_hot_block_preview(int *switch_idx, int *chunk_idx, bool *on) {
    return editor_hot_block_preview(switch_idx, chunk_idx, on);
}

void ged_goto_object(int idx) {
    if (idx < 0 || idx > 15) return;
    editor_cancel_to_plain();
    editor_handle_key((Key){ .code = KEY_CHAR, .ch = 'o', .mod = MOD_NONE });
    editor_handle_key((Key){ .code = KEY_CHAR, .ch = "0123456789abcdef"[idx], .mod = MOD_NONE });
}
// Open object `idx`'s MOVERTAB editor without moving the cursor there first (unlike
// ged_goto_object) - for the click-on-a-sprite's-rendered-position hit test in gui.c, where
// Hugh didn't want a click warping the cursor away to the object's logical tile: "that also
// warps which i wouldn't want". The cursor is left wherever the click itself already put it.
void ged_edit_movertab(int idx) {
    struct DecompressedRoom *room = cur_room();
    if (idx < 0 || (size_t)idx >= room->num_objects || !plain_state()) return;
    if (!state->movertab_loaded) movertab_load();
    state->previous_state = state->current_state;
    state->current_state = EDIT_MOVERTAB_OBJECT;
    state->current_object = (size_t)idx;
    state->movertab_raw = false;
    state->movertab_block_focus = 0;
}
void ged_cancel(void) { editor_cancel_to_plain(); }
void ged_left_room(int prev) { if (prev >= 0 && prev < 64) room_prune_blank((size_t)prev); }

int ged_linking(void) {
    switch (state->current_state) {
        case EDIT_SWITCHDETAILS:
        case EDIT_SWITCHDETAILS_SELECT_CHUNK:
        case EDIT_SWITCHDETAILS_CHUNK_BLOCK_DETAILS:
        case EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS:
        case EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS:
        case EDIT_SWITCHDETAILS_CHUNK_OBJECT_DETAILS:
            return (int)state->current_switch;
        default:
            return 0;
    }
}

void ged_link_block(int x, int y, int w, int h) {
    if (!ged_linking()) return;
    struct SwitchChunk c = { .type = TOGGLE_BLOCK, .x = (uint8_t)x, .y = (uint8_t)y,
                             .dir = w >= h ? HORIZONTAL : VERTICAL, .size = (uint8_t)(w >= h ? w : h),
                             .on = cur_room()->tiles[TILE_IDX(x, y)], .off = 0 };
    switch_add_chunk(state->current_switch - 1, c);
}

void ged_link_object(int object_idx) {
    if (!ged_linking()) return;
    struct SwitchChunk c = { .type = TOGGLE_OBJECT, .index = (uint8_t)object_idx, .test = 0x20, .value = 0 };   // test lives in the high nibble
    switch_add_chunk(state->current_switch - 1, c);
}

void ged_link_switch(int switch_idx) {
    if (!ged_linking()) return;
    struct SwitchChunk c = { .type = TOGGLE_BIT, .room_idx = (uint16_t)state->current_level,
                             .switch_idx = (uint16_t)switch_idx, .on = 1, .off = 0 };
    switch_add_chunk(state->current_switch - 1, c);
}

/* --- painting ------------------------------------------------------------- */

static struct RoomObject *block_covering(int tx, int ty) {
    struct DecompressedRoom *room = cur_room();
    for (size_t i = 0; i < room->num_objects; i++) {
        struct RoomObject *o = room->objects + i;
        if (o->type == BLOCK && o->tiles && tx >= o->x && tx < o->x + o->block.width &&
                ty >= o->y && ty < o->y + o->block.height) return o;
    }
    return NULL;
}

int ged_tile_at(int tx, int ty) {
    if (tx < 0 || tx >= WIDTH_TILES || ty < 0 || ty >= HEIGHT_TILES) return 0;
    struct RoomObject *o = block_covering(tx, ty);
    if (o) return o->tiles[(ty - o->y) * o->block.width + (tx - o->x)];
    return cur_room()->tiles[TILE_IDX(tx, ty)];
}

bool ged_place_sprite(int sprite_type, int damage) {
    if (!plain_state()) return false;
    struct DecompressedRoom *room = cur_room();
    int x = state->cursors[state->current_level].x, y = state->cursors[state->current_level].y;
    struct RoomObject *o = NULL;
    for (size_t i = 0; i < room->num_objects && !o; i++) {
        struct RoomObject *c = room->objects + i;
        bool hit = c->type == SPRITE ? (x == c->x && y == c->y)
                 : (x >= c->x && x < c->x + c->block.width && y >= c->y && y < c->y + c->block.height);
        if (hit) o = c;
    }
    push_undo((uint8_t)state->current_level, false);
    if (o == NULL) {
        if (room->num_objects >= 7) {
            snprintf(state->status_message, sizeof(state->status_message), "Max 7 objects");
            return false;
        }
        room->objects = realloc(room->objects, (room->num_objects + 1) * sizeof(struct RoomObject));
        assert(room->objects != NULL);
        o = room->objects + room->num_objects++;
        memset(o, 0, sizeof *o);
        o->x = (uint8_t)x; o->y = (uint8_t)y;
    } else if (o->type == BLOCK) {
        free(o->tiles);
        o->tiles = NULL;
    }
    o->type = SPRITE;
    o->sprite.type = (SpriteType)sprite_type;
    o->sprite.damage = (uint8_t)damage;
    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
    snprintf(state->status_message, sizeof(state->status_message), "Writing room");
    editor_save();
    return true;
}

bool ged_block_at_cursor(int *idx, int *x, int *y, int *w, int *h) {
    struct DecompressedRoom *room = cur_room();
    int cx = state->cursors[state->current_level].x, cy = state->cursors[state->current_level].y;
    struct RoomObject *o = block_covering(cx, cy);
    if (o == NULL) return false;
    *idx = (int)(o - room->objects);
    *x = o->x; *y = o->y; *w = o->block.width; *h = o->block.height;
    return true;
}

int ged_block_tile(int idx, int bx, int by) {
    struct DecompressedRoom *room = cur_room();
    if (idx < 0 || idx >= room->num_objects) return 0;
    struct RoomObject *o = room->objects + idx;
    if (o->type != BLOCK || !o->tiles || bx < 0 || by < 0 || bx >= o->block.width || by >= o->block.height) return 0;
    return o->tiles[by * o->block.width + bx];
}

void ged_block_set_tile(int idx, int bx, int by, uint8_t tile) {
    struct DecompressedRoom *room = cur_room();
    if (idx < 0 || idx >= room->num_objects) return;
    struct RoomObject *o = room->objects + idx;
    if (o->type != BLOCK || !o->tiles || bx < 0 || by < 0 || bx >= o->block.width || by >= o->block.height) return;
    o->tiles[by * o->block.width + bx] = tile;
    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
    paint_pending = true;
}

void ged_block_resize(int dx, int dy, bool grow) {
    if (!plain_state()) return;
    /* stretch()/shrink() only touch a block OBJECT when state->debug.objects is set (it
     * gates the same code path the 'o' debug overlay uses) - the GUI's block panel shows
     * regardless of that flag (ged_block_at_cursor doesn't check it), so w+/w-/h+/h- did
     * nothing whenever the overlay happened to be off. We already know a block is here. */
    bool had_objects = state->debug.objects;
    state->debug.objects = true;
    if (grow) stretch(dx, dy); else shrink(dx, dy);
    state->debug.objects = had_objects;
}

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
    editor_save();
    return true;
}

bool ged_room_export_text(const char *path) {
    return room_export_text(&state->rooms.rooms[state->current_level], &state->rooms, path);
}

bool ged_room_import_text(const char *path) {
    if (!plain_state()) return false;
    push_undo((uint8_t)state->current_level, false);
    bool ok = room_import_text(&state->rooms.rooms[state->current_level], &state->rooms, path);
    if (!ok) return false;
    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
    snprintf(state->status_message, sizeof(state->status_message), "Writing room");
    editor_save();
    return true;
}
