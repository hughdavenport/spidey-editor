#include "editor_types.h"
#include "render.h"
#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

typedef bool (*state_handler_fn)(Key key);

static bool handle_goto_object(Key key);
static bool handle_goto_switch(Key key);
static bool handle_goto_room(Key key);
static bool handle_goto_coord(Key key);
static bool handle_edit_roomname(Key key);
static bool handle_toggle_display(Key key);
static bool handle_edit_roomdetails(Key key);
static bool handle_edit_roomdetails_num(Key key);
static bool handle_edit_roomdetails_room(Key key);
static bool handle_edit_switchdetails(Key key);
static bool handle_edit_switchdetails_xy(Key key);
static bool handle_edit_switchdetails_select_chunk(Key key);
static bool handle_edit_switchdetails_chunk_switch(Key key);
static bool handle_edit_switchdetails_chunk_switch_room(Key key);
static bool handle_edit_switchdetails_chunk_memory(Key key);
static bool handle_edit_switchdetails_chunk_block(Key key);
static bool handle_edit_switchdetails_chunk_object(Key key);
static bool handle_normal(Key key);
static bool handle_tile_edit(Key key);
static void yank_at_cursor(void);
static void yank_current_switch(void);
static void delete_current_switch(void);
static void clear_switch_block_tiles(struct DecompressedRoom *room, struct SwitchObject *sw);
static void block_speed_ratchet(uint8_t *entry, int shift, uint8_t dir_bit, bool pressed_dir);
static void block_direction_set(uint8_t *entry, uint8_t dir_bit, bool set);
static void sprite_toggle_dead(uint8_t *v);
static void sprite_cycle_flip(uint8_t *v, int dir);
static void sprite_toggle_stopped(uint8_t *v);
static void sprite_toggle_wall(uint8_t *v);
static void sprite_toggle_edge(uint8_t *v);
static void sprite_cycle_mode(uint8_t *v, int dir);
static bool sprite_cycle_forward(struct RoomObject *obj);
static const char *sprite_display_name(SpriteType type, uint8_t damage);
static uint8_t movertab_offset_set_std(int v);
static uint8_t movertab_offset_set_sprite_y(uint8_t b, int v);
static void movertab_reset_entry(unsigned global, bool is_sprite);
static void cycle_chunk_type(struct SwitchChunk *chunk, size_t level, int dir);

/* Run play.sh detached. PLAY_ROOM tells it which room to start in (it swaps
 * that room into slot 1, the game's start room, in the copy the game runs);
 * SPIDEY_DATA_DIR tells it where this editor's ROOMS.SPL and MOVERTAB.DAT are. */
static void fork_play(void) {
    signal(SIGCHLD, SIG_IGN);
    pid_t child = fork();
    if (child == 0) {
        if (state != NULL) {
            char room[8];
            snprintf(room, sizeof room, "%zu", state->current_level);
            setenv("PLAY_ROOM", room, 1);
            setenv("SPIDEY_DATA_DIR", state->data_dir, 1);
            freeRoomFile(&state->rooms);
            free(state);
        }
        pid_t sid = fork();
        if (sid == -1) exit(EXIT_FAILURE);
        if (sid > 0) exit(EXIT_SUCCESS);
        if (setsid() == -1) exit(EXIT_FAILURE);
        char *play_argv[] = { "./play.sh", NULL };
        execv("./play.sh", play_argv);
        exit(EXIT_FAILURE);
    }
}

Key parse_key(const char *buf, int *i, int n) {
    Key k = {0};
    uint8_t b = (uint8_t)buf[*i];
    if (b != 0x1b) {
        (*i)++;
        if (b == 0x7f)       { k.code = KEY_BS; }
        else if (iscntrl(b)) { k.code = KEY_CTRL; k.ch = (char)(b + 'A' - 1); }
        else                 { k.code = KEY_CHAR; k.ch = (char)b; }
        return k;
    }
    if (*i + 1 >= n) { (*i)++; k.code = KEY_ESC; return k; }
    uint8_t b1 = (uint8_t)buf[*i + 1];
    if (b1 == '[') {
        if (*i + 2 >= n) { (*i)++; k.code = KEY_ESC; return k; }
        *i += 2;
        /* X10 mouse: ESC [ M <btn+32> <col+33> <row+33> (6 raw bytes) */
        if (*i < n && (uint8_t)buf[*i] == 'M' && *i + 3 < n) {
            (*i)++;
            uint8_t btn = (uint8_t)buf[(*i)++] - 32;
            int16_t cx  = (int16_t)((uint8_t)buf[(*i)++] - 33);
            int16_t cy  = (int16_t)((uint8_t)buf[(*i)++] - 33);
            k.code          = KEY_MOUSE;
            k.mouse.release = (btn == 3);
            k.mouse.button  = k.mouse.release ? 0 : btn;
            k.mouse.x       = cx;
            k.mouse.y       = cy;
            if (btn & 0x08) k.mod |= MOD_ALT;
            if (btn & 0x10) k.mod |= MOD_CTRL;
            return k;
        }
        /* SGR mouse: ESC [ ? <btn> ; <col> ; <row> M/m  (some terminals use < instead of ?) */
        if (*i < n && (buf[*i] == '?' || buf[*i] == '<')) {
            (*i)++;
            int p[3] = {0, 0, 0}; int pi = 0;
            while (*i < n && pi < 3) {
                if (isdigit((unsigned char)buf[*i])) { p[pi] = p[pi]*10 + buf[*i]-'0'; }
                else if (buf[*i] == ';')             { pi++; }
                else break;
                (*i)++;
            }
            if (*i < n && (buf[*i] == 'M' || buf[*i] == 'm')) {
                char f = buf[(*i)++];
                k.code          = KEY_MOUSE;
                k.mouse.release = (f == 'm');
                k.mouse.button  = (uint8_t)p[0];
                k.mouse.x       = (int16_t)(p[1] - 1);
                k.mouse.y       = (int16_t)(p[2] - 1);
                if (MOUSE_MOD_ALT(p[0]))  k.mod |= MOD_ALT;
                if (MOUSE_MOD_CTRL(p[0])) k.mod |= MOD_CTRL;
            } else {
                k.code = KEY_UNKNOWN;
            }
            return k;
        }
        int p[2] = {0, 0}; int pi = 0;
        while (*i < n && pi < 2) {
            if (isdigit((unsigned char)buf[*i])) { p[pi] = p[pi]*10 + buf[*i]-'0'; }
            else if (buf[*i] == ';')             { pi++; }
            else break;
            (*i)++;
        }
        if (*i >= n) { k.code = KEY_UNKNOWN; return k; }
        if (p[1] > 0) k.mod = (uint8_t)(p[1] - 1);
        char f = buf[(*i)++];
        switch (f) {
            case 'A': k.code = KEY_UP;    break;
            case 'B': k.code = KEY_DOWN;  break;
            case 'C': k.code = KEY_RIGHT; break;
            case 'D': k.code = KEY_LEFT;  break;
            case 'H': k.code = KEY_HOME;  break;
            case 'F': k.code = KEY_END;   break;
            case '~':
                switch (p[0]) {
                    case 1:  k.code = KEY_HOME;      break;
                    case 2:  k.code = KEY_INSERT;    break;
                    case 3:  k.code = KEY_DELETE;    break;
                    case 4:  k.code = KEY_END;       break;
                    case 5:  k.code = KEY_PAGE_UP;   break;
                    case 6:  k.code = KEY_PAGE_DOWN; break;
                    case 15: k.code = KEY_F5;        break;
                    case 17: k.code = KEY_F6;        break;
                    case 18: k.code = KEY_F7;        break;
                    case 19: k.code = KEY_F8;        break;
                    case 20: k.code = KEY_F9;        break;
                    case 21: k.code = KEY_F10;       break;
                    case 23: k.code = KEY_F11;       break;
                    case 24: k.code = KEY_F12;       break;
                    default: k.code = KEY_UNKNOWN;   break;
                }
                break;
            default: k.code = KEY_UNKNOWN; break;
        }
        return k;
    }
    if (b1 == 'O') {
        if (*i + 2 >= n) { (*i)++; k.code = KEY_ESC; return k; }
        char c = buf[*i + 2]; *i += 3;
        switch (c) {
            case 'P': k.code = KEY_F1; break; case 'Q': k.code = KEY_F2; break;
            case 'R': k.code = KEY_F3; break; case 'S': k.code = KEY_F4; break;
            default:  k.code = KEY_UNKNOWN; break;
        }
        return k;
    }
    *i += 2;
    if (b1 == 0x1b)       { k.code = KEY_ESC; }
    else if (iscntrl(b1)) { k.code = KEY_CTRL; k.ch = (char)(b1+'A'-1); k.mod = MOD_ALT; }
    else                  { k.code = KEY_CHAR; k.ch = (char)b1; k.mod = MOD_ALT; }
    return k;
}

/* Helper: delete current chunk from switch, pop state */
static void delete_current_chunk(void) {
    if (!state->current_switch) UNREACHABLE();
    if (!state->current_chunk) UNREACHABLE();
    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
    struct SwitchObject *sw = room->switches + state->current_switch - 1;
    size_t ch_i = state->current_chunk;
    if (sw->chunks.length <= 1) UNREACHABLE();
    push_undo((uint8_t)state->current_level, false);
    while (ch_i < sw->chunks.length - 1) {
        sw->chunks.data[ch_i] = sw->chunks.data[ch_i+1];
        ch_i++;
    }
    memset(sw->chunks.data + ch_i, 0, sizeof(struct SwitchChunk));
    sw->chunks.length --;
    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
    editor_save();
    if (state->current_chunk > 2) state->current_chunk --;
    switch (sw->chunks.length) {
        case 1: state->current_state = EDIT_SWITCHDETAILS; break;
        case 2:
        {
            switch (sw->chunks.data[1].type) {
                case PREAMBLE: UNREACHABLE();
                case TOGGLE_BIT:
                    state->current_state = EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS;
                    break;
                case TOGGLE_OBJECT:
                    state->current_state = EDIT_SWITCHDETAILS_CHUNK_OBJECT_DETAILS;
                    break;
                case TOGGLE_BLOCK:
                {
                    size_t overflow = WIDTH_TILES * HEIGHT_TILES;
                    size_t point = sw->chunks.data[1].y * WIDTH_TILES + sw->chunks.data[1].x;
                    if (point >= overflow) {
                        state->current_state = EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS;
                    } else {
                        state->current_state = EDIT_SWITCHDETAILS_CHUNK_BLOCK_DETAILS;
                    }
                }; break;
                default: UNREACHABLE();
            }
        }; break;
        default: state->current_state = EDIT_SWITCHDETAILS_SELECT_CHUNK;
    }
}

static void pop_chunk_state(void) {
    size_t *cursorlevel = &state->current_level;
    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
    if (sw->chunks.length <= 2) {
        state->current_state = EDIT_SWITCHDETAILS;
    } else {
        state->current_state = EDIT_SWITCHDETAILS_SELECT_CHUNK;
    }
    state->current_chunk = 0;
}

static void chunk_swap_next(void) {
    size_t *cursorlevel = &state->current_level;
    if (state->current_switch) {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk ch = sw->chunks.data[state->current_chunk];
        if (state->current_chunk < sw->chunks.length - 1) {
            push_undo((uint8_t)state->current_level, false);
            sw->chunks.data[state->current_chunk] = sw->chunks.data[state->current_chunk+1];
            sw->chunks.data[state->current_chunk+1] = ch;
            state->current_chunk ++;
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            editor_save();
        }
    }
}

static void chunk_swap_prev(void) {
    size_t *cursorlevel = &state->current_level;
    if (state->current_switch) {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk ch = sw->chunks.data[state->current_chunk];
        if (state->current_chunk > 1) {
            push_undo((uint8_t)state->current_level, false);
            sw->chunks.data[state->current_chunk] = sw->chunks.data[state->current_chunk-1];
            sw->chunks.data[state->current_chunk-1] = ch;
            state->current_chunk --;
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            editor_save();
        }
    }
}

static void delete_item_at_cursor(void) {
    struct RoomObject *object_underneath = NULL;
    struct SwitchObject *switch_underneath = NULL;
    struct SwitchChunk *chunk_underneath = NULL;
    struct SwitchObject *chunk_switch_underneath = NULL;

    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
    int x = state->cursors[state->current_level].x;
    int y = state->cursors[state->current_level].y;
    int i;
    for (i = 0; i < room->num_objects; i ++) {
        struct RoomObject *object = room->objects + i;
        if (object->type == BLOCK &&
                x >= object->x && x < object->x + object->block.width &&
                y >= object->y && y < object->y + object->block.height) {
            object_underneath = object;
            break;
        } else if (object->type == SPRITE && x == object->x && y == object->y) {
            object_underneath = object;
            break;
        }
    }
    int obj_i = i;
    for (i = 0; i < room->num_switches; i ++) {
        struct SwitchObject *switcch = room->switches + i;
        if (switcch->chunks.length > 0 && switcch->chunks.data[0].type == PREAMBLE &&
                x == switcch->chunks.data[0].x && y == switcch->chunks.data[0].y) {
            switch_underneath = switcch;
            break;
        }
    }
    int sw_i = i;
    int c;
    for (i = 0; i < room->num_switches; i ++) {
        struct SwitchObject *switcch = room->switches + i;
        for (c = 1; c < (signed) switcch->chunks.length; c ++) {
            struct SwitchChunk *chunk = switcch->chunks.data + c;
            if (chunk->type == TOGGLE_BLOCK) {
                if (chunk->dir == HORIZONTAL) {
                    if (y == chunk->y && x >= chunk->x && x < chunk->x + chunk->size) {
                        chunk_switch_underneath = switcch;
                        chunk_underneath = chunk;
                        break;
                    }
                } else if (x == chunk->x && y >= chunk->y && y < chunk->y + chunk->size) {
                    chunk_switch_underneath = switcch;
                    chunk_underneath = chunk;
                    break;
                }
            }
        }
        if (chunk_underneath) break;
    }

    push_undo((uint8_t)state->current_level, false);
    if (object_underneath) {
        while (obj_i < room->num_objects - 1) {
            if (room->objects[obj_i].tiles) {
                free(room->objects[obj_i].tiles);
            }
            room->objects[obj_i] = room->objects[obj_i+1];
            obj_i++;
        }
        if (room->objects[obj_i].tiles) {
            free(room->objects[obj_i].tiles);
        }
        memset(room->objects + obj_i, 0, sizeof(struct RoomObject));
        room->num_objects --;
    } else if (switch_underneath) {
        // Same tile cleanup as delete_current_switch: the marker itself, plus every tile any
        // of its TOGGLE_BLOCK chunks covers, so nothing is left behind on the map (Hugh: "just
        // an empty tile left" - this is the plain-Backspace-in-TILE_EDIT path, a different
        // function to the one used while the switch is open for editing).
        clear_switch_block_tiles(room, switch_underneath);
        room->tiles[TILE_IDX(x, y)] = 0;
        state->hot_preview_switch = 0;   // indices shift below - don't leave a hover preview pointing at the wrong switch
        fixup_toggle_bit_after_switch_removed((uint8_t)state->current_level, (size_t)sw_i);
        while (sw_i < room->num_switches - 1) {
            room->switches[sw_i] = room->switches[sw_i+1];
            sw_i++;
        }
        memset(room->switches + sw_i, 0, sizeof(struct SwitchObject));
        room->num_switches --;
    } else if (chunk_underneath) {
        while (c < (signed) chunk_switch_underneath->chunks.length - 1) {
            chunk_switch_underneath->chunks.data[c] = chunk_switch_underneath->chunks.data[c+1];
            c++;
        }
        memset(chunk_switch_underneath->chunks.data + c, 0, sizeof(struct SwitchChunk));
        chunk_switch_underneath->chunks.length --;
    } else if (room->tiles[TILE_IDX(x, y)] != BLANK_TILE) {
        room->tiles[TILE_IDX(x, y)] = 0;
    } else {
        // Nothing at all under the cursor: matches yank_at_cursor's "empty cell -> whole
        // room" rule, so this completes the cut half of a room move (y/x here, navigate
        // elsewhere, p there). Unlike room_blank_and_stay (Ctrl-D in the room picker,
        // which keeps exits so other rooms' links to it still make sense), this clears
        // exits too: the room is now entirely blank, so leaving it prunes the slot away
        // (room_prune_blank) rather than leaving a linked-but-empty room behind.
        Room *r = &state->rooms.rooms[state->current_level];
        room_snapshot_free(room);
        memset(room, 0, sizeof(*room));
        memset(room->name, ' ', sizeof(room->name));
        ARRAY_FREE(r->rest);
        snprintf(state->status_message, sizeof(state->status_message),
                 "Cleared room %02x (exits cleared too); leave it to remove the slot, u to undo",
                 (unsigned)state->current_level);
    }
    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
    editor_save();
}

static bool handle_goto_object(Key key) {
    v2 *cursor = &state->cursors[state->current_level];
    size_t *cursorlevel = &state->current_level;
    switch (key.code) {
        case KEY_CHAR:
            if (isxdigit(key.ch)) {
                struct DecompressedRoom *room = &state->rooms.rooms[*cursorlevel].data;
                if (room->num_objects > 15) {
                    UNREACHABLE();
                }
                size_t id;
                if (key.ch > '9') {
                    id = tolower(key.ch) - 'a' + 10;
                } else {
                    id = key.ch - '0';
                }
                if (id < room->num_objects) {
                    struct RoomObject *obj = room->objects + id;
                    if (obj->x < WIDTH_TILES && obj->y < HEIGHT_TILES) {
                        cursor->x = obj->x;
                        cursor->y = obj->y;
                    }
                }
                state->current_state = state->previous_state;
                state->previous_state = NORMAL;
            } else switch (key.ch) {
                case 'q':
                    if (state->help) {
                        state->help = !state->help;
                    } else {
                        if (state->partial_byte) {
                            state->partial_byte = 0;
                        } else {
                            state->current_state = state->previous_state;
                            state->current_switch = 0;
                            state->switch_on = false;
                            state->current_chunk = 0;
                            state->previous_state = NORMAL;
                        }
                    }
                    break;
                case '?': state->help = !state->help; break;
                default:
                    state->current_state = state->previous_state;
                    state->previous_state = NORMAL;
                    break;
            }
            break;
        case KEY_ESC:
            if (state->help) {
                state->help = false;
            } else {
                state->current_state = state->previous_state;
                state->current_switch = 0;
                state->switch_on = false;
                state->current_chunk = 0;
                state->previous_state = NORMAL;
            }
            break;
        case KEY_CTRL:
            switch (key.ch) {
                case '_': state->help = !state->help; break;
                case 'H': state->debug.hex = !state->debug.hex; break;
            }
            break;
        default: break;
    }
    return true;
}

static bool handle_goto_switch(Key key) {
    v2 *cursor = &state->cursors[state->current_level];
    size_t *cursorlevel = &state->current_level;
    switch (key.code) {
        case KEY_CHAR:
            if (isxdigit(key.ch)) {
                struct DecompressedRoom *room = &state->rooms.rooms[*cursorlevel].data;
                if (room->num_switches > 15) {
                    UNREACHABLE();
                }
                size_t id;
                if (key.ch > '9') {
                    id = tolower(key.ch) - 'a' + 10;
                } else {
                    id = key.ch - '0';
                }
                if (id < room->num_switches) {
                    state->current_switch = id + 1;
                    struct SwitchObject *sw = room->switches + id;
                    if (sw->chunks.length > 0 && sw->chunks.data[0].type == PREAMBLE && sw->chunks.data[0].x < WIDTH_TILES && sw->chunks.data[0].y < HEIGHT_TILES) {
                        cursor->x = sw->chunks.data[0].x;
                        cursor->y = sw->chunks.data[0].y;
                        state->current_state = state->previous_state;
                        state->previous_state = NORMAL;
                    } else {
                        state->current_state = EDIT_SWITCHDETAILS;
                    }
                } else {
                    state->current_state = state->previous_state;
                    state->previous_state = NORMAL;
                }
            } else switch (key.ch) {
                case 'q':
                    if (state->help) {
                        state->help = !state->help;
                    } else {
                        if (state->partial_byte) {
                            state->partial_byte = 0;
                        } else {
                            state->current_state = state->previous_state;
                            state->current_switch = 0;
                            state->switch_on = false;
                            state->current_chunk = 0;
                            state->previous_state = NORMAL;
                        }
                    }
                    break;
                case '?': state->help = !state->help; break;
                default:
                    state->current_state = state->previous_state;
                    state->previous_state = NORMAL;
                    break;
            }
            break;
        case KEY_ESC:
            if (state->help) {
                state->help = false;
            } else {
                state->current_state = state->previous_state;
                state->current_switch = 0;
                state->switch_on = false;
                state->current_chunk = 0;
                state->previous_state = NORMAL;
            }
            break;
        case KEY_CTRL:
            switch (key.ch) {
                case '_': state->help = !state->help; break;
                case 'H': state->debug.hex = !state->debug.hex; break;
            }
            break;
        default: break;
    }
    return true;
}

/* Ctrl-D delete mode in GOTO_ROOM: blank the picked room in place (tiles,
 * objects, switches, name all cleared) instead of entering it. Exits are
 * kept as-is, so other rooms' links to it are undisturbed and the room
 * doesn't move on the map; it just becomes empty. Stays in GOTO_ROOM so
 * several rooms can be cleared in a row. */
static void room_blank_and_stay(size_t dest) {
    if (!state->rooms.rooms[dest].valid) {
        snprintf(state->status_message, sizeof(state->status_message), "Room %02zx is already empty", dest);
    } else {
        push_undo((uint8_t)dest, false);
        Room *r = &state->rooms.rooms[dest];
        struct DecompressedRoom *room = &r->data;
        uint8_t n = room->room_north, e = room->room_east, s = room->room_south, w = room->room_west;
        room_snapshot_free(room);
        memset(room, 0, sizeof(*room));
        memset(room->name, ' ', sizeof(room->name));
        room->room_north = n; room->room_east = e; room->room_south = s; room->room_west = w;
        ARRAY_FREE(r->rest);
        ARRAY_FREE(r->compressed);
        editor_save();
        snprintf(state->status_message, sizeof(state->status_message),
                 "Deleted room %02zx (exits kept); u to undo", dest);
    }
    state->partial_byte = 0;
    if (state->roomname_cursor) {
        memset(state->room_name, 0, state->roomname_cursor);
        state->roomname_cursor = 0;
    }
}

/* Helper macro for goto_room navigation. In GOTO_ROOM with delete mode
 * armed, a picked room is blanked instead of entered (see room_blank_and_stay);
 * the other two states that reuse this macro (room-detail neighbour editing,
 * a switch chunk's target room) always navigate normally. */
#define GOTO_ROOM_NAVIGATE(dest_level) do { \
    if (state->current_state == GOTO_ROOM && state->room_delete_mode) { \
        room_blank_and_stay((dest_level)); \
    } else { \
        size_t prev_level = *cursorlevel; \
        *cursorlevel = (dest_level); \
        if (state->current_state == GOTO_ROOM) room_prune_blank(prev_level); \
        state->current_state = state->previous_state; \
        state->current_switch = 0; \
        state->switch_on = false; \
        state->current_chunk = 0; \
        state->previous_state = NORMAL; \
        state->partial_byte = 0; \
        if (state->roomname_cursor) { \
            memset(state->room_name, 0, state->roomname_cursor); \
            state->roomname_cursor = 0; \
        } \
    } \
} while (0)

static bool handle_goto_room(Key key) {
    size_t *cursorlevel = &state->current_level;
    switch (key.code) {
        case KEY_CHAR:
            if (isxdigit(key.ch) &&
                    state->partial_byte && ((state->debug.hex && isxdigit(key.ch)) || (!state->debug.hex && isdigit(key.ch)))) {
                int digit;
                if (key.ch > '9') {
                    digit = tolower(key.ch) - 'a' + 10;
                } else {
                    digit = key.ch - '0';
                }
                size_t room = digit;
                if (state->debug.hex) {
                    room += 16 * (state->partial_byte & 0xFF);
                } else {
                    room += 10 * (state->partial_byte & 0xFF);
                }
                if (room < C_ARRAY_LEN(state->rooms.rooms)) {
                    GOTO_ROOM_NAVIGATE(room);
                }
            } else if (state->roomname_cursor == 0 && state->partial_byte == 0 &&
                       key.ch >= '0' &&
                       key.ch <= (state->debug.hex ? '3' : '6')) {
                state->partial_byte = 0xFF00 | (key.ch - '0');
            } else if (key.mod == MOD_ALT && key.ch == 'h') {
                GOTO_ROOM_NAVIGATE(state->rooms.rooms[*cursorlevel].data.room_west);
            } else if (key.mod == MOD_ALT && key.ch == 'j') {
                GOTO_ROOM_NAVIGATE(state->rooms.rooms[*cursorlevel].data.room_south);
            } else if (key.mod == MOD_ALT && key.ch == 'k') {
                GOTO_ROOM_NAVIGATE(state->rooms.rooms[*cursorlevel].data.room_north);
            } else if (key.mod == MOD_ALT && key.ch == 'l') {
                GOTO_ROOM_NAVIGATE(state->rooms.rooms[*cursorlevel].data.room_east);
            } else if (state->partial_byte == 0) {
                if (state->roomname_cursor < C_ARRAY_LEN(state->room_name) - 1) {
                    state->room_name[state->roomname_cursor++] = key.ch;

                    bool possible = false;
                    bool duplicate = false;
                    size_t matching_room = 0;
                    if (state->roomname_cursor) {
                        for (size_t _room = 0; _room < C_ARRAY_LEN(state->rooms.rooms); _room ++) {
                            struct DecompressedRoom *room = &state->rooms.rooms[_room].data;
                            if (strncasecmp(room->name, state->room_name, state->roomname_cursor) == 0) {
                                if (possible) {
                                    duplicate = true;
                                    break;
                                } else {
                                    duplicate = false;
                                }
                                possible = true;
                                matching_room = _room;
                            }
                        }
                    } else {
                        possible = true;
                    }
                    if (!possible) {
                        state->room_name[--state->roomname_cursor] = 0;
                    } else if (!duplicate) {
                        GOTO_ROOM_NAVIGATE(matching_room);
                    }
                }
            }
            break;
        case KEY_BS:
        case KEY_DELETE:
            if (state->roomname_cursor) {
                state->room_name[--state->roomname_cursor] = 0;
            } else {
                state->partial_byte = 0;
            }
            break;
        case KEY_UP:
            GOTO_ROOM_NAVIGATE(state->rooms.rooms[*cursorlevel].data.room_north);
            break;
        case KEY_DOWN:
            GOTO_ROOM_NAVIGATE(state->rooms.rooms[*cursorlevel].data.room_south);
            break;
        case KEY_RIGHT:
            GOTO_ROOM_NAVIGATE(state->rooms.rooms[*cursorlevel].data.room_east);
            break;
        case KEY_LEFT:
            GOTO_ROOM_NAVIGATE(state->rooms.rooms[*cursorlevel].data.room_west);
            break;
        case KEY_ESC:
            if (state->help) {
                state->help = false;
            } else {
                state->current_state = state->previous_state;
                state->current_switch = 0;
                state->switch_on = false;
                state->current_chunk = 0;
                state->previous_state = NORMAL;
                state->partial_byte = 0;
                state->room_delete_mode = false;
                if (state->roomname_cursor) {
                    memset(state->room_name, 0, state->roomname_cursor);
                    state->roomname_cursor = 0;
                }
            }
            break;
        case KEY_CTRL:
            switch (key.ch) {
                case '_': state->help = !state->help; break;
                // Ctrl-X, not Ctrl-D: Ctrl-D is the well-known, documented toggle-display key
                // everywhere else in the editor (TOGGLE_DISPLAY, handle_normal) - reusing it
                // here for something destructive would mean the exact same muscle-memory
                // keystroke quietly arms room deletion instead of showing debug info.
                case 'X':
                    state->room_delete_mode = !state->room_delete_mode;
                    snprintf(state->status_message, sizeof(state->status_message),
                             state->room_delete_mode ? "Delete mode: next room picked is blanked, not entered"
                                                      : "Delete mode off");
                    break;
                case 'H':
                    state->debug.hex = !state->debug.hex;
                    if (state->partial_byte) {
                        if (state->debug.hex) {
                            state->partial_byte = ((state->partial_byte & 0xFF) * 10 / 16) % 16 + 1;
                        } else {
                            state->partial_byte = ((state->partial_byte & 0xFF) * 16 / 10) % 10 + 1;
                        }
                    }
                    break;
                case 'Q':
                    if (state->help) {
                        state->help = !state->help;
                    } else {
                        state->current_state = state->previous_state;
                        state->current_switch = 0;
                        state->switch_on = false;
                        state->current_chunk = 0;
                        state->previous_state = NORMAL;
                        state->partial_byte = 0;
                        state->room_delete_mode = false;
                        if (state->roomname_cursor) {
                            memset(state->room_name, 0, state->roomname_cursor);
                            state->roomname_cursor = 0;
                        }
                    }
                    break;
            }
            break;
        default: break;
    }
    return true;
}

#undef GOTO_ROOM_NAVIGATE

static bool handle_edit_roomname(Key key) {
    size_t *cursorlevel = &state->current_level;

    switch (key.code) {
        case KEY_CTRL:
            if (key.ch == 'J' || key.ch == 'M') {
                /* ENTER: Ctrl-J (\n) from a terminal, Ctrl-M (\r) from the GUI's KEY_ENTER */
                struct DecompressedRoom *room = &state->rooms.rooms[*cursorlevel].data;
                assert(C_ARRAY_LEN(state->room_name) <= C_ARRAY_LEN(room->name));
                push_undo((uint8_t)state->current_level, false);
                /* Every room name shipped with the game is space-padded to the full field
                 * width (e.g. "Foyer                   "), not NUL-padded: strncpy's normal
                 * NUL-fill left leftover game data behind a short name, since the game reads
                 * the whole field rather than stopping at a terminator - the next thing that
                 * wrote near it in memory would show through, and saving again could as well. */
                size_t len = strnlen(state->room_name, C_ARRAY_LEN(room->name));
                memcpy(room->name, state->room_name, len);
                memset(room->name + len, ' ', C_ARRAY_LEN(room->name) - len);
                state->current_state = state->previous_state;
                state->current_switch = 0;
                state->switch_on = false;
                state->current_chunk = 0;
                state->previous_state = NORMAL;
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                editor_save();
            } else {
                switch (key.ch) {
                    case 'H': state->debug.hex = !state->debug.hex; break;
                    case '_': state->help = !state->help; break;
                }
            }
            break;
        case KEY_CHAR:
            if (state->roomname_cursor < C_ARRAY_LEN(state->room_name) - 1) {
                state->room_name[state->roomname_cursor++] = key.ch;
            }
            break;
        case KEY_HOME:
            state->roomname_cursor = 0;
            break;
        case KEY_END:
            state->roomname_cursor = strlen(state->room_name);
            break;
        case KEY_DELETE:
            for (size_t c = state->roomname_cursor; c < C_ARRAY_LEN(state->room_name) - 1; c ++) {
                state->room_name[c] = state->room_name[c + 1];
                state->room_name[c + 1] = '\0';
            }
            if (state->roomname_cursor) {
                state->roomname_cursor--;
            }
            break;
        case KEY_RIGHT:
            if (state->roomname_cursor < C_ARRAY_LEN(state->room_name) - 1 && state->room_name[state->roomname_cursor]) {
                state->roomname_cursor ++;
            }
            break;
        case KEY_LEFT:
            if (state->roomname_cursor > 0) {
                state->roomname_cursor --;
            }
            break;
        case KEY_ESC:
            if (state->help) {
                state->help = false;
            } else {
                state->current_state = state->previous_state;
                state->current_switch = 0;
                state->switch_on = false;
                state->current_chunk = 0;
                state->previous_state = NORMAL;
            }
            break;
        case KEY_BS:
            if (state->roomname_cursor) {
                for (size_t c = state->roomname_cursor - 1; c < C_ARRAY_LEN(state->room_name) - 1; c ++) {
                    state->room_name[c] = state->room_name[c + 1];
                    state->room_name[c + 1] = '\0';
                }
                if (state->roomname_cursor) {
                    state->roomname_cursor--;
                }
            }
            break;
        default: break;
    }
    return true;
}

static bool handle_toggle_display(Key key) {
    uint8_t changed = 1;
    switch (key.code) {
        case KEY_CHAR:
            switch (key.ch) {
                case 'a': debugalltoggle(); break;
                case 'c': state->debug.spidey = !state->debug.spidey; break;
                case 'd': state->debug.data = !state->debug.data; break;
                case 'h': state->debug.hurt = !state->debug.hurt; break;
                case 'l': state->debug.switch_links = !state->debug.switch_links; break;
                case 'n': state->debug.neighbours = !state->debug.neighbours; break;
                case 'o': state->debug.objects = !state->debug.objects; break;
                case 'p': state->debug.pos = !state->debug.pos; break;
                case 's': state->debug.switches = !state->debug.switches; break;
                case 't': state->debug.tiles = !state->debug.tiles; break;
                case 'q': break;
                case '?': state->help = !state->help; changed = 0; break;
                default: changed = 0; break;
            }
            break;
        case KEY_ESC:
            if (state->help) { state->help = false; changed = 0; }
            /* else: exit toggle display (changed stays 1) */
            break;
        case KEY_CTRL:
            switch (key.ch) {
                case 'H': state->debug.hex = !state->debug.hex; break;
                case '_': state->help = !state->help; changed = 0; break;
                case 'D': break; /* Ctrl-D closes the menu */
                default: changed = 0; break;
            }
            break;
        default:
            changed = 0;
            break;
    }
    if (changed) {
        state->current_state = state->previous_state;
        state->current_switch = 0;
        state->switch_on = false;
        state->current_chunk = 0;
        state->previous_state = NORMAL;
    }
    return true;
}

/* Exit the Ctrl-R submenu entirely back to TILE_EDIT */
static void exit_roomdetails(void) {
    state->current_state = state->previous_state;
    state->previous_state = TILE_EDIT;
    state->current_switch = 0;
    state->switch_on = false;
    state->current_chunk = 0;
    state->partial_byte = 0;
    state->room_detail = 0;
    memset(state->room_name, 0, state->roomname_cursor);
    state->roomname_cursor = 0;
}

/* Exit the Ctrl-S submenu entirely back to TILE_EDIT (preserves current_switch) */
static void exit_switchdetails(void) {
    state->current_state = state->previous_state;
    state->previous_state = NORMAL;
}

static bool handle_edit_roomdetails(Key key) {
    switch (key.code) {
        case KEY_UP:
            state->room_detail = 'k';
            state->current_state = EDIT_ROOMDETAILS_ROOM;
            state->current_switch = 0;
            state->switch_on = false;
            state->current_chunk = 0;
            break;
        case KEY_DOWN:
            state->room_detail = 'j';
            state->current_state = EDIT_ROOMDETAILS_ROOM;
            state->current_switch = 0;
            state->switch_on = false;
            state->current_chunk = 0;
            break;
        case KEY_LEFT:
            state->room_detail = 'h';
            state->current_state = EDIT_ROOMDETAILS_ROOM;
            state->current_switch = 0;
            state->switch_on = false;
            state->current_chunk = 0;
            break;
        case KEY_RIGHT:
            state->room_detail = 'l';
            state->current_state = EDIT_ROOMDETAILS_ROOM;
            state->current_switch = 0;
            state->switch_on = false;
            state->current_chunk = 0;
            break;
        case KEY_CHAR:
            switch (key.ch) {
                case 'h':
                case 'j':
                case 'k':
                case 'l':
                    state->room_detail = key.ch;
                    state->current_state = EDIT_ROOMDETAILS_ROOM;
                    state->current_switch = 0;
                    state->switch_on = false;
                    state->current_chunk = 0;
                    break;

                case 'g':
                case 't':
                case 'd':
                case '|':
                case '-':
                    state->room_detail = key.ch;
                    state->current_state = EDIT_ROOMDETAILS_NUM;
                    state->current_switch = 0;
                    state->switch_on = false;
                    state->current_chunk = 0;
                    break;

                case 'p':
                    fork_play();
                    break;

                case 'q':
                    if (state->help) {
                        state->help = false;
                    } else {
                        state->current_state = state->previous_state;
                        state->current_switch = 0;
                        state->switch_on = false;
                        state->current_chunk = 0;
                        state->previous_state = TILE_EDIT;
                    }
                    break;

                case '?':
                    state->help = !state->help;
                    break;

                default:
                    break;
            }
            break;
        case KEY_ESC:
            if (state->help) {
                state->help = false;
            } else {
                state->current_state = state->previous_state;
                state->current_switch = 0;
                state->switch_on = false;
                state->current_chunk = 0;
                state->previous_state = TILE_EDIT;
            }
            break;
        case KEY_CTRL:
            switch (key.ch) {
                case '_': state->help = !state->help; break;
                case 'H':
                    state->debug.hex = !state->debug.hex;
                    if (state->partial_byte) {
                        if (state->debug.hex) {
                            state->partial_byte = ((state->partial_byte & 0xFF) * 10 / 16) % 16 + 1;
                        } else {
                            state->partial_byte = ((state->partial_byte & 0xFF) * 16 / 10) % 10 + 1;
                        }
                    }
                    break;
                case 'R': exit_roomdetails(); break;
            }
            break;
        default: break;
    }
    return true;
}

static bool handle_edit_roomdetails_num(Key key) {
    switch (key.code) {
        case KEY_CHAR:
            if ((state->debug.hex && isxdigit(key.ch)) || (!state->debug.hex && isdigit(key.ch))) {
                int digit;
                if (key.ch > '9') {
                    digit = tolower(key.ch) - 'a' + 10;
                } else {
                    digit = key.ch - '0';
                }
                if (state->partial_byte) {
                    size_t value = digit;
                    if (state->debug.hex) {
                        value += 16 * (state->partial_byte & 0xFF);
                    } else {
                        value += 10 * (state->partial_byte & 0xFF);
                    }

                    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                    push_undo((uint8_t)state->current_level, false);
                    switch (state->room_detail) {
                        case 'g': room->background = value; break;
                        case 't': room->tile_offset = value; break;
                        case 'd': room->room_damage = value; break;
                        case '|': room->gravity_vertical = value; break;
                        case '-': room->gravity_horizontal = value; break;

                        // UNKNOWN_b is no longer edited here: every original room has it set to 1,
                        // and testing (disassembly + play) found no code path that ever reads it.
                        // Object/switch bases (c, e, f) are no longer edited here either: writeRooms()
                        // recomputes them fresh from the file's actual layout on every save.

                        default: UNREACHABLE();
                    }
                    state->current_state = EDIT_ROOMDETAILS;
                    state->current_switch = 0;
                    state->switch_on = false;
                    state->current_chunk = 0;
                    state->partial_byte = 0;
                    state->room_detail = 0;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                } else {
                    if (state->room_detail != 'e' || digit == 0) {
                        state->partial_byte = 0xFF00 | digit;
                    }
                }
            } else switch (key.ch) {
                case 'q':
                    if (state->help) {
                        state->help = !state->help;
                    } else {
                        state->current_state = EDIT_ROOMDETAILS;
                        state->current_switch = 0;
                        state->switch_on = false;
                        state->current_chunk = 0;
                        state->partial_byte = 0;
                        state->room_detail = 0;
                    }
                    break;
                case '?': state->help = !state->help; break;
                default: break;
            }
            break;
        case KEY_BS:
        case KEY_DELETE:
            state->partial_byte = 0;
            break;
        case KEY_ESC:
            if (state->help) {
                state->help = false;
            } else {
                state->current_state = EDIT_ROOMDETAILS;
                state->current_switch = 0;
                state->switch_on = false;
                state->current_chunk = 0;
                state->partial_byte = 0;
                state->room_detail = 0;
            }
            break;
        case KEY_CTRL:
            switch (key.ch) {
                case '_': state->help = !state->help; break;
                case 'H':
                    state->debug.hex = !state->debug.hex;
                    if (state->partial_byte) {
                        if (state->debug.hex) {
                            state->partial_byte = ((state->partial_byte & 0xFF) * 10 / 16) % 16 + 1;
                        } else {
                            state->partial_byte = ((state->partial_byte & 0xFF) * 16 / 10) % 10 + 1;
                        }
                    }
                    break;
                case 'P': fork_play(); break;
                case 'R': exit_roomdetails(); break;
            }
            break;
        default: break;
    }
    return true;
}

static bool handle_edit_roomdetails_room(Key key) {
    size_t *cursorlevel = &state->current_level;
    switch (key.code) {
        case KEY_CHAR:
            if (state->partial_byte &&
                    ((state->debug.hex && isxdigit(key.ch)) ||
                     (!state->debug.hex && isdigit(key.ch)))) {
                int digit;
                if (key.ch > '9') {
                    digit = tolower(key.ch) - 'a' + 10;
                } else {
                    digit = key.ch - '0';
                }
                size_t room_id = digit;
                if (state->debug.hex) {
                    room_id += 16 * (state->partial_byte & 0xFF);
                } else {
                    room_id += 10 * (state->partial_byte & 0xFF);
                }
                if (room_id < C_ARRAY_LEN(state->rooms.rooms)) {
                    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                    push_undo((uint8_t)state->current_level, false);
                    switch (state->room_detail) {
                        case 'k': room->room_north = room_id; break;
                        case 'j': room->room_south = room_id; break;
                        case 'l': room->room_east  = room_id; break;
                        case 'h': room->room_west  = room_id; break;
                        default: UNREACHABLE();
                    }
                    state->current_state = EDIT_ROOMDETAILS;
                    state->current_switch = 0;
                    state->switch_on = false;
                    state->current_chunk = 0;
                    state->partial_byte = 0;
                    state->room_detail = 0;
                    if (state->roomname_cursor) {
                        memset(state->room_name, 0, state->roomname_cursor);
                        state->roomname_cursor = 0;
                    }
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }
            } else if (state->roomname_cursor == 0 && state->partial_byte == 0 &&
                       key.ch >= '0' &&
                       key.ch <= (state->debug.hex ? '3' : '6')) {
                state->partial_byte = 0xFF00 | (key.ch - '0');
            } else if (state->partial_byte == 0) {
                if (state->roomname_cursor < C_ARRAY_LEN(state->room_name) - 1) {
                    state->room_name[state->roomname_cursor++] = key.ch;

                    bool possible = false;
                    bool duplicate = false;
                    size_t matching_room = 0;
                    if (state->roomname_cursor) {
                        for (size_t _room = 0; _room < C_ARRAY_LEN(state->rooms.rooms); _room ++) {
                            struct DecompressedRoom *room = &state->rooms.rooms[_room].data;
                            if (strncasecmp(room->name, state->room_name, state->roomname_cursor) == 0) {
                                if (possible) {
                                    duplicate = true;
                                    break;
                                } else {
                                    duplicate = false;
                                }
                                possible = true;
                                matching_room = _room;
                            }
                        }
                    } else {
                        possible = true;
                    }
                    if (!possible) {
                        state->room_name[--state->roomname_cursor] = 0;
                    } else if (!duplicate) {
                        struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                        push_undo((uint8_t)state->current_level, false);
                        switch (state->room_detail) {
                            case 'k': room->room_north = matching_room; break;
                            case 'j': room->room_south = matching_room; break;
                            case 'l': room->room_east  = matching_room; break;
                            case 'h': room->room_west  = matching_room; break;
                            default: UNREACHABLE();
                        }
                        state->current_state = EDIT_ROOMDETAILS;
                        state->current_switch = 0;
                        state->switch_on = false;
                        state->current_chunk = 0;
                        state->partial_byte = 0;
                        state->room_detail = 0;
                        if (state->roomname_cursor) {
                            memset(state->room_name, 0, state->roomname_cursor);
                            state->roomname_cursor = 0;
                        }
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        editor_save();
                    }
                }
            }
            break;
        case KEY_BS:
        case KEY_DELETE:
            if (state->roomname_cursor) {
                state->room_name[--state->roomname_cursor] = 0;
            } else {
                state->partial_byte = 0;
            }
            break;
        case KEY_ESC:
            if (state->help) {
                state->help = false;
            } else {
                state->current_state = EDIT_ROOMDETAILS;
                state->current_switch = 0;
                state->switch_on = false;
                state->current_chunk = 0;
                state->partial_byte = 0;
                if (state->roomname_cursor) {
                    memset(state->room_name, 0, state->roomname_cursor);
                    state->roomname_cursor = 0;
                }
            }
            break;
        case KEY_CTRL:
            switch (key.ch) {
                case '_': state->help = !state->help; break;
                case 'H':
                    state->debug.hex = !state->debug.hex;
                    if (state->partial_byte) {
                        if (state->debug.hex) {
                            state->partial_byte = ((state->partial_byte & 0xFF) * 10 / 16) % 16 + 1;
                        } else {
                            state->partial_byte = ((state->partial_byte & 0xFF) * 16 / 10) % 10 + 1;
                        }
                    }
                    break;
                case 'Q':
                    if (state->help) {
                        state->help = !state->help;
                    } else {
                        state->current_state = EDIT_ROOMDETAILS;
                        state->current_switch = 0;
                        state->switch_on = false;
                        state->current_chunk = 0;
                        state->partial_byte = 0;
                        if (state->roomname_cursor) {
                            memset(state->room_name, 0, state->roomname_cursor);
                            state->roomname_cursor = 0;
                        }
                    }
                    break;
                case 'R': exit_roomdetails(); break;
            }
            break;
        default:
            (void)cursorlevel;
            break;
    }
    return true;
}

// Plain one-tile cursor movement with room-edge crossing, matching handle_normal's hjkl/arrow
// case (but without that case's side effects of leaving switch/chunk selection) - used where a
// direction key is pressed but has nothing else to do (see side_locked in
// handle_edit_switchdetails), so it falls back to ordinary map navigation instead of a no-op.
static void move_cursor_plain(int dx, int dy) {
    size_t *cursorlevel = &state->current_level;
    if (dx < 0) {
        state->cursors[*cursorlevel].x--;
        if (state->cursors[*cursorlevel].x < 0) {
            state->cursors[*cursorlevel].x++;
            size_t level = state->rooms.rooms[*cursorlevel].data.room_west;
            state->cursors[level].y = state->cursors[*cursorlevel].y;
            state->cursors[level].x = WIDTH_TILES - 1;
            size_t old_level = *cursorlevel;
            *cursorlevel = level;
            room_prune_blank(old_level);
        }
    } else if (dx > 0) {
        state->cursors[*cursorlevel].x++;
        if (state->cursors[*cursorlevel].x >= WIDTH_TILES) {
            state->cursors[*cursorlevel].x--;
            size_t level = state->rooms.rooms[*cursorlevel].data.room_east;
            state->cursors[level].x = 0;
            state->cursors[level].y = state->cursors[*cursorlevel].y;
            size_t old_level = *cursorlevel;
            *cursorlevel = level;
            room_prune_blank(old_level);
        }
    }
    if (dy < 0) {
        state->cursors[*cursorlevel].y--;
        if (state->cursors[*cursorlevel].y < 0) {
            state->cursors[*cursorlevel].y++;
            size_t level = state->rooms.rooms[*cursorlevel].data.room_north;
            state->cursors[level].y = HEIGHT_TILES - 1;
            state->cursors[level].x = state->cursors[*cursorlevel].x;
            size_t old_level = *cursorlevel;
            *cursorlevel = level;
            room_prune_blank(old_level);
        }
    } else if (dy > 0) {
        state->cursors[*cursorlevel].y++;
        if (state->cursors[*cursorlevel].y >= HEIGHT_TILES) {
            state->cursors[*cursorlevel].y--;
            size_t level = state->rooms.rooms[*cursorlevel].data.room_south;
            state->cursors[level].y = 0;
            state->cursors[level].x = state->cursors[*cursorlevel].x;
            size_t old_level = *cursorlevel;
            *cursorlevel = level;
            room_prune_blank(old_level);
        }
    }
}

static bool handle_edit_switchdetails(Key key) {
    // "side" isn't shown or editable for a switch that's out of bounds, or sitting on a blank
    // tile (see room.c's switch_side_hidden for why). In that case direction keys fall back to
    // plain map cursor movement instead of doing nothing. "entry" is a separate, narrower case
    // (switch_entry_forced): only an in-bounds blank tile forces/hides it, not out of bounds.
    struct DecompressedRoom *side_room = &state->rooms.rooms[state->current_level].data;
    struct SwitchChunk *side_preamble = side_room->switches[state->current_switch - 1].chunks.data;
    bool side_locked = switch_side_hidden(side_room, side_preamble);
    bool entry_locked = switch_entry_forced(side_room, side_preamble);
    switch (key.code) {
        case KEY_UP:
        {
            if (side_locked) { move_cursor_plain(0, -1); break; }
            struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
            push_undo((uint8_t)state->current_level, false);
            room->switches[state->current_switch - 1].chunks.data[0].side = TOP;
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            editor_save();
        } break;
        case KEY_DOWN:
        {
            if (side_locked) { move_cursor_plain(0, 1); break; }
            struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
            push_undo((uint8_t)state->current_level, false);
            room->switches[state->current_switch - 1].chunks.data[0].side = BOTTOM;
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            editor_save();
        } break;
        case KEY_RIGHT:
        {
            if (side_locked) { move_cursor_plain(1, 0); break; }
            struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
            push_undo((uint8_t)state->current_level, false);
            room->switches[state->current_switch - 1].chunks.data[0].side = RIGHT;
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            editor_save();
        } break;
        case KEY_LEFT:
        {
            if (side_locked) { move_cursor_plain(-1, 0); break; }
            struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
            push_undo((uint8_t)state->current_level, false);
            room->switches[state->current_switch - 1].chunks.data[0].side = LEFT;
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            editor_save();
        } break;
        case KEY_BS:
        case KEY_DELETE:
            if (!state->current_switch) UNREACHABLE();
            yank_current_switch();
            delete_current_switch();
            break;
        case KEY_CHAR:
            switch (key.ch) {
                case '+':
                {
                    if (state->current_switch) {
                        struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                        size_t sw_i = state->current_switch - 1;
                        struct SwitchObject sw = room->switches[sw_i];
                        if (sw_i < (unsigned)room->num_switches - 1) {
                            push_undo((uint8_t)state->current_level, false);
                            room->switches[sw_i] = room->switches[sw_i+1];
                            room->switches[sw_i+1] = sw;
                            state->current_switch ++;
                            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                            editor_save();
                        }
                    }
                }; break;

                case '-':
                {
                    if (state->current_switch) {
                        struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                        size_t sw_i = state->current_switch - 1;
                        struct SwitchObject sw = room->switches[sw_i];
                        if (sw_i) {
                            push_undo((uint8_t)state->current_level, false);
                            room->switches[sw_i] = room->switches[sw_i-1];
                            room->switches[sw_i-1] = sw;
                            state->current_switch --;
                            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                            editor_save();
                        }
                    }
                }; break;

                case 'h':
                {
                    if (side_locked) { move_cursor_plain(-1, 0); break; }
                    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                    push_undo((uint8_t)state->current_level, false);
                    room->switches[state->current_switch - 1].chunks.data[0].side = LEFT;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;

                case 'j':
                {
                    if (side_locked) { move_cursor_plain(0, 1); break; }
                    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                    push_undo((uint8_t)state->current_level, false);
                    room->switches[state->current_switch - 1].chunks.data[0].side = BOTTOM;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;

                case 'k':
                {
                    if (side_locked) { move_cursor_plain(0, -1); break; }
                    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                    push_undo((uint8_t)state->current_level, false);
                    room->switches[state->current_switch - 1].chunks.data[0].side = TOP;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;

                case 'l':
                {
                    if (side_locked) { move_cursor_plain(1, 0); break; }
                    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                    push_undo((uint8_t)state->current_level, false);
                    room->switches[state->current_switch - 1].chunks.data[0].side = RIGHT;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;

                case 'o':
                {
                    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                    push_undo((uint8_t)state->current_level, false);
                    room->switches[state->current_switch - 1].chunks.data[0].one_time_use = !room->switches[state->current_switch - 1].chunks.data[0].one_time_use;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;

                case ' ':
                    // Preview the whole switch as active/idle on the map, same as 'o' one
                    // level down while editing a specific chunk (see editor_hot_block_preview)
                    // - every TOGGLE_BLOCK chunk this switch owns previews together, matching
                    // the real game's single state bit per switch (Hugh: "when you are on a
                    // switch, a key to toggle it and see the effects in the room map").
                    state->switch_on = !state->switch_on;
                    break;

                case 'y':
                    if (!state->current_switch) break;
                    yank_current_switch();
                    break;

                case 'x':
                    if (!state->current_switch) break;
                    yank_current_switch();
                    delete_current_switch();
                    break;

                case 'p':
                {
                    if (!state->current_switch) break;
                    if (state->clipboard.type != CLIP_CHUNK) break;
                    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                    struct SwitchObject *sw = room->switches + state->current_switch - 1;
                    push_undo((uint8_t)state->current_level, false);
                    ARRAY_ADD(sw->chunks, state->clipboard.chunk);
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;

                case 'e':
                {
                    // Forced true (and hidden) only for a switch on an in-bounds blank tile -
                    // see switch_entry_forced (room.c). An out-of-bounds switch's entry keeps
                    // its ordinary meaning and stays editable.
                    if (entry_locked) break;
                    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                    push_undo((uint8_t)state->current_level, false);
                    room->switches[state->current_switch - 1].chunks.data[0].room_entry = !room->switches[state->current_switch - 1].chunks.data[0].room_entry;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;

                case 's': case 'S':
                {
                    if (side_locked) { snprintf(state->status_message, sizeof(state->status_message), "This switch has no side (out of bounds or blank tile)"); break; }
                    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                    push_undo((uint8_t)state->current_level, false);
                    struct SwitchChunk *preamble = room->switches[state->current_switch - 1].chunks.data;
                    preamble->side = (preamble->side + (key.ch == 's' ? 1 : NUM_SIDES - 1)) % NUM_SIDES;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;

                case 'c':
                {
                    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                    struct SwitchObject *sw = room->switches + state->current_switch - 1;
                    if (sw->chunks.length == 1) {
                        state->current_chunk = sw->chunks.length;
                        push_undo((uint8_t)state->current_level, false);
                        ARRAY_ADD(sw->chunks, ((struct SwitchChunk){ .type = TOGGLE_BLOCK, .size = 8 }));
                        state->current_state = EDIT_SWITCHDETAILS_CHUNK_BLOCK_DETAILS;
                        state->switch_on = false;
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        editor_save();
                    } else if (sw->chunks.length == 2) {
                        state->current_chunk = 1;
                        assert(sw->chunks.data[0].type == PREAMBLE);
                        struct SwitchChunk *chunk = sw->chunks.data + 1;
                        switch (chunk->type) {
                            case PREAMBLE: UNREACHABLE();
                            case TOGGLE_BLOCK:
                            {
                                size_t overflow = WIDTH_TILES * HEIGHT_TILES;
                                size_t point = chunk->y * WIDTH_TILES + chunk->x;
                                if (point >= overflow) {
                                    state->current_state = EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS;
                                } else {
                                    state->current_state = EDIT_SWITCHDETAILS_CHUNK_BLOCK_DETAILS;
                                }
                            }; break;
                            case TOGGLE_BIT:
                                state->current_state = EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS;
                                break;
                            case TOGGLE_OBJECT:
                                state->current_state = EDIT_SWITCHDETAILS_CHUNK_OBJECT_DETAILS;
                                break;
                            default: UNREACHABLE();
                        }
                    } else {
                        state->current_state = EDIT_SWITCHDETAILS_SELECT_CHUNK;
                    }
                }; break;

                case 'C':
                {
                    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                    struct SwitchObject *sw = room->switches + state->current_switch - 1;
                    if (sw->chunks.length == 16) {
                        break;
                    }
                    state->current_chunk = sw->chunks.length;
                    push_undo((uint8_t)state->current_level, false);
                    ARRAY_ADD(sw->chunks, ((struct SwitchChunk){ .type = TOGGLE_BLOCK, .size = 8 }));
                    state->current_state = EDIT_SWITCHDETAILS_CHUNK_BLOCK_DETAILS;
                    state->switch_on = false;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;

                case 'X':
                    state->room_detail = 'X';
                    state->current_state = EDIT_SWITCHDETAILS_XY;
                    break;

                case 'Y':
                    state->room_detail = 'Y';
                    state->current_state = EDIT_SWITCHDETAILS_XY;
                    break;

                case 'q':
                    if (state->help) {
                        state->help = false;
                    } else {
                        state->current_state = state->previous_state;
                        state->current_switch = 0;
                        state->switch_on = false;
                        state->previous_state = TILE_EDIT;
                    }
                    break;

                case '?':
                    state->help = !state->help;
                    break;

                default:
                    break;
            }
            break;
        case KEY_ESC:
            if (state->help) {
                state->help = false;
            } else {
                state->current_state = state->previous_state;
                state->current_switch = 0;
                state->switch_on = false;
                state->previous_state = TILE_EDIT;
            }
            break;
        case KEY_CTRL:
            switch (key.ch) {
                case '_': state->help = !state->help; break;
                case 'H':
                    state->debug.hex = !state->debug.hex;
                    if (state->partial_byte) {
                        if (state->debug.hex) {
                            state->partial_byte = ((state->partial_byte & 0xFF) * 10 / 16) % 16 + 1;
                        } else {
                            state->partial_byte = ((state->partial_byte & 0xFF) * 16 / 10) % 10 + 1;
                        }
                    }
                    break;
                case 'P': fork_play(); break;
                case 'S': exit_switchdetails(); break;
            }
            break;
        default: break;
    }
    return true;
}

// X/Y move an existing switch's own position (chunks.data[0], the PREAMBLE):
// digits accumulate exactly like handle_edit_roomdetails_num, but write into
// the switch's x or y (masked to 5 bits - the on-disk field is that wide). X
// keeps the full 0-31 range; visible rooms only use cols 0-31, rows 0-21, so
// x above 21 (like y) already reads as "off grid" to the rest of the editor
// and the game, with no separate flag needed - but y is further capped to
// HEIGHT_TILES (22): one OOB row is enough to place a switch out of bounds,
// and a deeper row broke the world map's switch link lines (see below).
static bool handle_edit_switchdetails_xy(Key key) {
    switch (key.code) {
        case KEY_CHAR:
            if ((state->debug.hex && isxdigit(key.ch)) || (!state->debug.hex && isdigit(key.ch))) {
                int digit;
                if (key.ch > '9') {
                    digit = tolower(key.ch) - 'a' + 10;
                } else {
                    digit = key.ch - '0';
                }
                if (state->partial_byte) {
                    size_t value = digit;
                    if (state->debug.hex) {
                        value += 16 * (state->partial_byte & 0xFF);
                    } else {
                        value += 10 * (state->partial_byte & 0xFF);
                    }
                    value &= 0x1f;
                    // A single OOB row (y == HEIGHT_TILES) is all the out-of-bounds switch
                    // machinery needs - X (still the full 5 bits) tells OOB switches on that row
                    // apart. A second, deeper OOB row was the cause of the world map's link
                    // lines pointing at the wrong switches, so Y is capped here.
                    if (state->room_detail == 'Y' && value > HEIGHT_TILES) value = HEIGHT_TILES;

                    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                    struct SwitchChunk *preamble = room->switches[state->current_switch - 1].chunks.data;
                    push_undo((uint8_t)state->current_level, false);
                    if (state->room_detail == 'X') preamble->x = (uint8_t)value;
                    else preamble->y = (uint8_t)value;
                    state->current_state = EDIT_SWITCHDETAILS;
                    state->partial_byte = 0;
                    state->room_detail = 0;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                } else {
                    state->partial_byte = 0xFF00 | digit;
                }
            } else switch (key.ch) {
                case 'q':
                    if (state->help) {
                        state->help = !state->help;
                    } else {
                        state->current_state = EDIT_SWITCHDETAILS;
                        state->partial_byte = 0;
                        state->room_detail = 0;
                    }
                    break;
                case '?': state->help = !state->help; break;
                default: break;
            }
            break;
        case KEY_BS:
        case KEY_DELETE:
            state->partial_byte = 0;
            break;
        case KEY_ESC:
            if (state->help) {
                state->help = false;
            } else {
                state->current_state = EDIT_SWITCHDETAILS;
                state->partial_byte = 0;
                state->room_detail = 0;
            }
            break;
        case KEY_CTRL:
            switch (key.ch) {
                case '_': state->help = !state->help; break;
                case 'H':
                    state->debug.hex = !state->debug.hex;
                    if (state->partial_byte) {
                        if (state->debug.hex) {
                            state->partial_byte = ((state->partial_byte & 0xFF) * 10 / 16) % 16 + 1;
                        } else {
                            state->partial_byte = ((state->partial_byte & 0xFF) * 16 / 10) % 10 + 1;
                        }
                    }
                    break;
                case 'P': fork_play(); break;
                case 'S': exit_switchdetails(); break;
            }
            break;
        default: break;
    }
    return true;
}

static bool handle_edit_switchdetails_select_chunk(Key key) {
    size_t *cursorlevel = &state->current_level;
    switch (key.code) {
        case KEY_CHAR:
            if (isxdigit(key.ch)) {
                struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                if (sw->chunks.length > 16) {
                    fprintf(stderr, "%s:%d: UNIMPLEMENTED: support two digit numbers", __FILE__, __LINE__);
                    UNREACHABLE();
                }
                size_t id;
                if (key.ch > '9') {
                    id = tolower(key.ch) - 'a' + 10;
                } else {
                    id = key.ch - '0';
                }
                if (id < sw->chunks.length) {
                    state->current_chunk = id;
                    struct SwitchChunk *chunk = sw->chunks.data + id;
                    switch (chunk->type) {
                        case PREAMBLE: UNREACHABLE();
                        case TOGGLE_BLOCK:
                        {
                            size_t overflow = WIDTH_TILES * HEIGHT_TILES;
                            size_t point = chunk->y * WIDTH_TILES + chunk->x;
                            if (point >= overflow) {
                                state->current_state = EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS;
                            } else {
                                state->current_state = EDIT_SWITCHDETAILS_CHUNK_BLOCK_DETAILS;
                            }
                        }; break;
                        case TOGGLE_BIT:
                            state->current_state = EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS;
                            break;
                        case TOGGLE_OBJECT:
                            state->current_state = EDIT_SWITCHDETAILS_CHUNK_OBJECT_DETAILS;
                            break;
                        default: UNREACHABLE();
                    }
                } else {
                    state->current_state = EDIT_SWITCHDETAILS;
                }
            } else switch (key.ch) {
                case 'q':
                    if (state->help) {
                        state->help = !state->help;
                    } else {
                        state->current_state = EDIT_SWITCHDETAILS;
                    }
                    break;
                case '?':
                    state->help = !state->help;
                    break;
                case '+': {
                    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                    struct SwitchObject *sw = room->switches + state->current_switch - 1;
                    if (sw->chunks.length == 16) {
                        return true;
                    }
                    push_undo((uint8_t)state->current_level, false);
                    ARRAY_ADD(sw->chunks, ((struct SwitchChunk){ .type = TOGGLE_BLOCK, .size = 8 }));
                    state->switch_on = false;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;
                default:
                    state->current_state = EDIT_SWITCHDETAILS;
                    break;
            }
            break;
        case KEY_BS:
        case KEY_DELETE:
            state->partial_byte = 0;
            break;
        case KEY_ESC:
            if (state->help) {
                state->help = false;
            } else {
                state->current_state = EDIT_SWITCHDETAILS;
            }
            break;
        case KEY_CTRL:
            switch (key.ch) {
                case '_': state->help = !state->help; break;
                case 'H': state->debug.hex = !state->debug.hex; break;
                case 'P': fork_play(); break;
                case 'S': state->current_state = EDIT_SWITCHDETAILS; break;
            }
            break;
        default: break;
    }
    return true;
}

void clipboard_clear(void) {
    switch (state->clipboard.type) {
        case CLIP_OBJECT: free(state->clipboard.object.tiles); state->clipboard.object.tiles = NULL; break;
        case CLIP_SWITCH: ARRAY_FREE(state->clipboard.sw.chunks); break;
        case CLIP_ROOM:   room_snapshot_free(&state->clipboard.room.data); ARRAY_FREE(state->clipboard.room.rest); break;
        case CLIP_CHUNK: case CLIP_TILE: case CLIP_NONE: break;
    }
    state->clipboard.type = CLIP_NONE;
}

static void yank_chunk(void) {
    if (!state->current_switch || !state->current_chunk) return;
    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
    struct SwitchObject *sw = room->switches + state->current_switch - 1;
    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
    clipboard_clear();
    state->clipboard.type = CLIP_CHUNK;
    state->clipboard.chunk = *chunk;
}

static void yank_at_cursor(void) {
    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
    int x = state->cursors[state->current_level].x;
    int y = state->cursors[state->current_level].y;
    clipboard_clear();
    state->clipboard.type = CLIP_NONE;
    for (size_t j = 0; j < room->num_objects; j++) {
        struct RoomObject *obj = room->objects + j;
        if ((obj->type == BLOCK && x >= obj->x && x < obj->x + obj->block.width &&
                y >= obj->y && y < obj->y + obj->block.height) ||
            (obj->type == SPRITE && x == obj->x && y == obj->y)) {
            state->clipboard.type = CLIP_OBJECT;
            state->clipboard.object = *obj;
            if (obj->type == BLOCK && obj->tiles) {
                size_t tile_n = obj->block.width * obj->block.height;
                state->clipboard.object.tiles = malloc(tile_n);
                assert(state->clipboard.object.tiles != NULL);
                memcpy(state->clipboard.object.tiles, obj->tiles, tile_n);
            } else {
                state->clipboard.object.tiles = NULL;
            }
            break;
        }
    }
    if (state->clipboard.type == CLIP_NONE) {
        for (size_t j = 0; j < room->num_switches; j++) {
            struct SwitchObject *sw = room->switches + j;
            if (sw->chunks.length > 0 && sw->chunks.data[0].type == PREAMBLE &&
                    x == sw->chunks.data[0].x && y == sw->chunks.data[0].y) {
                state->clipboard.type = CLIP_SWITCH;
                state->clipboard.tile = room->tiles[TILE_IDX(x, y)];
                state->clipboard.sw.chunks.length = sw->chunks.length;
                state->clipboard.sw.chunks.capacity = sw->chunks.length;
                state->clipboard.sw.chunks.data = malloc(sw->chunks.length * sizeof(struct SwitchChunk));
                assert(state->clipboard.sw.chunks.data != NULL);
                memcpy(state->clipboard.sw.chunks.data, sw->chunks.data,
                       sw->chunks.length * sizeof(struct SwitchChunk));
                break;
            }
        }
    }
    if (state->clipboard.type == CLIP_NONE) {
        for (size_t j = 0; j < room->num_switches; j++) {
            struct SwitchObject *sw = room->switches + j;
            for (size_t c = 1; c < sw->chunks.length; c++) {
                struct SwitchChunk *chunk = sw->chunks.data + c;
                if (chunk->type == TOGGLE_BLOCK &&
                        ((chunk->dir == VERTICAL && x == chunk->x && y >= chunk->y && y < chunk->y + chunk->size) ||
                         (chunk->dir == HORIZONTAL && y == chunk->y && x >= chunk->x && x < chunk->x + chunk->size))) {
                    state->clipboard.type = CLIP_CHUNK;
                    state->clipboard.chunk = *chunk;
                    break;
                }
            }
            if (state->clipboard.type == CLIP_CHUNK) break;
        }
    }
    if (state->clipboard.type == CLIP_NONE) {
        uint8_t tile = room->tiles[TILE_IDX(x, y)];
        if (tile != BLANK_TILE) {
            state->clipboard.type = CLIP_TILE;
            state->clipboard.tile = tile;
        }
    }
    if (state->clipboard.type == CLIP_NONE) {          // an empty cell: yank the whole room
        Room *r = &state->rooms.rooms[state->current_level];
        state->clipboard.type = CLIP_ROOM;
        room_snapshot_copy(&state->clipboard.room.data, room);
        state->clipboard.room.rest = (uint8_array){0};
        for (size_t i = 0; i < r->rest.length; i++) ARRAY_ADD(state->clipboard.room.rest, r->rest.data[i]);
        state->clipboard.room.index = (uint8_t)state->current_level;
        snprintf(state->status_message, sizeof(state->status_message),
                 "Yanked whole room %02zx: p in a room replaces it", state->current_level);
    }
}

static void yank_current_switch(void) {
    if (!state->current_switch) return;
    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
    struct SwitchObject *sw = room->switches + state->current_switch - 1;
    clipboard_clear();
    state->clipboard.type = CLIP_SWITCH;
    state->clipboard.sw.chunks.length = sw->chunks.length;
    state->clipboard.sw.chunks.capacity = sw->chunks.length;
    state->clipboard.sw.chunks.data = malloc(sw->chunks.length * sizeof(struct SwitchChunk));
    assert(state->clipboard.sw.chunks.data != NULL);
    memcpy(state->clipboard.sw.chunks.data, sw->chunks.data,
           sw->chunks.length * sizeof(struct SwitchChunk));
}

// Blank the base tilemap under a switch's own TOGGLE_BLOCK chunks (not memory/exit-rewrite
// chunks at y >= HEIGHT_TILES, which don't cover real tiles at all) - so deleting a switch,
// or pasting one, doesn't leave whatever tile happened to be underneath as a stray leftover
// (Hugh: "so that there is just an empty tile left").
static void clear_switch_block_tiles(struct DecompressedRoom *room, struct SwitchObject *sw) {
    for (size_t c = 1; c < sw->chunks.length; c++) {
        struct SwitchChunk *chunk = sw->chunks.data + c;
        if (chunk->type != TOGGLE_BLOCK || chunk->y >= HEIGHT_TILES) continue;
        if (chunk->dir == HORIZONTAL) {
            for (int x = chunk->x; x < chunk->x + (int)chunk->size && x < WIDTH_TILES; x++)
                room->tiles[TILE_IDX(x, chunk->y)] = 0;
        } else {
            for (int y = chunk->y; y < chunk->y + (int)chunk->size && y < HEIGHT_TILES; y++)
                room->tiles[TILE_IDX(chunk->x, y)] = 0;
        }
    }
}

// Whether (x,y) is on switch `sw`'s own marker, or on a tile any of its TOGGLE_BLOCK chunks
// covers - "switch under spidey, or chunk of any kind under spidey" (Hugh), used to let Space
// preview a switch just by standing near it, without opening it for editing.
static bool switch_covers_tile(struct SwitchObject *sw, int x, int y) {
    struct SwitchChunk *pre = sw->chunks.data;
    if (x == pre->x && y == pre->y) return true;
    for (size_t c = 1; c < sw->chunks.length; c++) {
        struct SwitchChunk *chunk = sw->chunks.data + c;
        if (chunk->type != TOGGLE_BLOCK || chunk->y >= HEIGHT_TILES) continue;
        bool hit = (chunk->dir == HORIZONTAL)
            ? (y == chunk->y && x >= chunk->x && x < chunk->x + (int)chunk->size)
            : (x == chunk->x && y >= chunk->y && y < chunk->y + (int)chunk->size);
        if (hit) return true;
    }
    return false;
}
// 0-based index of the switch under (x,y) in `room`, or -1.
static int find_switch_under_cursor(struct DecompressedRoom *room, int x, int y) {
    for (int s = 0; s < room->num_switches; s++) {
        if (switch_covers_tile(room->switches + s, x, y)) return s;
    }
    return -1;
}
// Space outside of switch editing: toggle-preview whatever switch is under the cursor right
// now, sticking to it (via hot_preview_switch) even as the cursor moves away afterward, so
// you can walk over to see the effect elsewhere in the room (Hugh: "toggle it and see the
// effects in the room map"). No-op with a status message if nothing is there.
static void toggle_hover_switch_preview(void) {
    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
    int x = state->cursors[state->current_level].x, y = state->cursors[state->current_level].y;
    int s = find_switch_under_cursor(room, x, y);
    if (s < 0) {
        snprintf(state->status_message, sizeof(state->status_message), "No switch here to preview");
        return;
    }
    if (state->hot_preview_switch == (size_t)s + 1) {
        state->switch_on = !state->switch_on;
    } else {
        state->hot_preview_switch = (size_t)s + 1;
        state->switch_on = true;
    }
}

static void delete_current_switch(void) {
    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
    size_t sw_i = state->current_switch - 1;
    push_undo((uint8_t)state->current_level, false);
    state->hot_preview_switch = 0;   // indices shift below - don't leave a hover preview pointing at the wrong switch
    clear_switch_block_tiles(room, room->switches + sw_i);
    // The switch's own marker tile isn't a TOGGLE_BLOCK chunk, so clear_switch_block_tiles
    // above doesn't touch it - unlike paste, which deliberately restores whatever was there
    // before the switch existed (state->clipboard.tile), a plain delete has nothing worth
    // keeping there, so it goes blank too (Hugh: "just an empty tile left").
    struct SwitchChunk *preamble = room->switches[sw_i].chunks.data;
    if (preamble->x < WIDTH_TILES && preamble->y < HEIGHT_TILES)
        room->tiles[TILE_IDX(preamble->x, preamble->y)] = 0;
    fixup_toggle_bit_after_switch_removed((uint8_t)state->current_level, sw_i);
    while (sw_i < (size_t)(room->num_switches - 1)) {
        room->switches[sw_i] = room->switches[sw_i+1];
        sw_i++;
    }
    memset(room->switches + sw_i, 0, sizeof(struct SwitchObject));
    room->num_switches--;
    state->current_state = state->previous_state;
    state->current_switch = 0;
    state->switch_on = false;
    state->current_chunk = 0;
    state->previous_state = NORMAL;
    state->partial_byte = 0;
    if (state->roomname_cursor) {
        memset(state->room_name, 0, state->roomname_cursor);
        state->roomname_cursor = 0;
    }
    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
    editor_save();
}

// Cycles a switch chunk's type/sub-mode and re-initializes it for wherever it lands, mirroring
// 't's four logical steps (a TOGGLE_BLOCK chunk has two: fresh/in-bounds, then "memory" mode off
// the tile grid) both ways - dir +1/-1, same pattern as sprite_cycle_mode (Hugh: "let uppercase
// cycle backwards"). Caller still does its own push_undo/ARRAY_FREE/editor_save around this.
static void cycle_chunk_type(struct SwitchChunk *chunk, size_t level, int dir) {
    bool has_objects = state->rooms.rooms[level].data.num_objects > 0;
    int nsteps = has_objects ? 4 : 3;
    int step;
    if (chunk->type == TOGGLE_BLOCK)
        step = (chunk->y * WIDTH_TILES + chunk->x) < WIDTH_TILES * HEIGHT_TILES ? 0 : 1;
    else if (chunk->type == TOGGLE_BIT) step = 2;
    else step = 3;   // TOGGLE_OBJECT (or PREAMBLE, impossible here)
    if (step >= nsteps) step = 0;   // e.g. an existing TOGGLE_OBJECT chunk after its objects were deleted
    step = ((step + dir) % nsteps + nsteps) % nsteps;

    memset(chunk, 0, sizeof(*chunk));
    switch (step) {
        case 0:
            chunk->type = TOGGLE_BLOCK;
            // A fresh chunk (memset to 0 above) needs an explicit size - 0 isn't a valid
            // width/height (Hugh: "that width=0 should be 8").
            chunk->size = 8;
            state->current_state = EDIT_SWITCHDETAILS_CHUNK_BLOCK_DETAILS;
            break;
        case 1:
            chunk->type = TOGGLE_BLOCK;
            chunk->x = 0;
            chunk->y = HEIGHT_TILES;
            chunk->size = 1;
            state->current_state = EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS;
            break;
        case 2:
            chunk->type = TOGGLE_BIT;
            chunk->room_idx = (uint16_t)level;   // same room by default
            state->current_state = EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS;
            chunk->bitmask = 0x1;
            chunk->on = 1;    // turn on
            chunk->off = 2;   // turn off
            break;
        case 3:
            chunk->type = TOGGLE_OBJECT;
            chunk->test = 0x20;   // test=2: fires on every press, not just every second one
            state->current_state = EDIT_SWITCHDETAILS_CHUNK_OBJECT_DETAILS;
            break;
    }
}

static bool handle_edit_switchdetails_chunk_switch(Key key) {
    size_t *cursorlevel = &state->current_level;
    switch (key.code) {
        case KEY_CHAR:
            // No raw hex entry for switch_idx - it can only point at something real via 's'
            // now (Hugh: "disable the raw hex edit for the switch id"), since a typed number
            // could point past the target room's actual switches.
            switch (key.ch) {
                case 'r':
                    state->current_state = EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS_ROOM;
                    break;
                // 'n'/'f' match "on"/"off"'s second letter, since both words start with 'o'
                // (Hugh: "n and f (currently o, but change to f)").
                case 'f': case 'F': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    assert(chunk->type == TOGGLE_BIT);
                    push_undo((uint8_t)state->current_level, false);
                    chunk->off = (uint8_t)((chunk->off + (key.ch == 'f' ? 1 : 3)) % 4);
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;
                case 'n': case 'N': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    assert(chunk->type == TOGGLE_BIT);
                    push_undo((uint8_t)state->current_level, false);
                    chunk->on = (uint8_t)((chunk->on + (key.ch == 'n' ? 1 : 3)) % 4);
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;
                case 's': case 'S': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    assert(chunk->type == TOGGLE_BIT);
                    // Cycle within the target room's actual switches, not a blind hex digit
                    // entry that can point at nothing there (Hugh: "make switch cycle through
                    // available switches in the target room").
                    size_t n = state->rooms.rooms[chunk->room_idx].data.num_switches;
                    if (n > 0) {
                        push_undo((uint8_t)state->current_level, false);
                        chunk->switch_idx = (uint16_t)((chunk->switch_idx + (key.ch == 's' ? 1 : n - 1)) % n);
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        editor_save();
                    }
                }; break;
                case 't': case 'T': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    push_undo((uint8_t)state->current_level, false);
                    cycle_chunk_type(chunk, *cursorlevel, key.ch == 't' ? 1 : -1);
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;
                case 'q':
                    if (state->help) {
                        state->help = !state->help;
                    } else {
                        if (state->partial_byte) {
                            state->partial_byte = 0;
                        } else {
                            pop_chunk_state();
                        }
                    }
                    break;
                case '?': state->help = !state->help; break;
                case '+': chunk_swap_next(); break;
                case '-': chunk_swap_prev(); break;
                case 'y': yank_chunk(); break;
                case 'x': if (state->partial_byte) { state->partial_byte = 0; break; } yank_chunk(); delete_current_chunk(); break;
                default: break;
            }
            break;
        case KEY_BS:
        case KEY_DELETE:
            if (state->partial_byte) {
                state->partial_byte = 0;
            } else {
                yank_chunk();
                delete_current_chunk();
            }
            break;
        case KEY_ESC:
            if (state->help) {
                state->help = false;
            } else {
                if (state->partial_byte) {
                    state->partial_byte = 0;
                } else {
                    pop_chunk_state();
                }
            }
            break;
        case KEY_CTRL:
            switch (key.ch) {
                case '_': state->help = !state->help; break;
                case 'H': state->debug.hex = !state->debug.hex; break;
                case 'P': fork_play(); break;
                case 'S': pop_chunk_state(); break;
            }
            break;
        default: break;
    }
    return true;
}

static bool handle_edit_switchdetails_chunk_switch_room(Key key) {
    size_t *cursorlevel = &state->current_level;

    /* helper: restore room_name to current room's name */
#define RESTORE_ROOM_NAME() do { \
    strncpy(state->room_name, state->rooms.rooms[*cursorlevel].data.name, C_ARRAY_LEN(state->room_name)); \
    for (size_t _i = 0; _i < C_ARRAY_LEN(state->room_name); _i ++) { \
        if (state->room_name[C_ARRAY_LEN(state->room_name) - _i] == '\0') continue; \
        if (state->room_name[C_ARRAY_LEN(state->room_name) - _i] == ' ') { \
            state->room_name[C_ARRAY_LEN(state->room_name) - _i] = '\0'; \
        } else { break; } \
    } \
} while (0)

    switch (key.code) {
        case KEY_CHAR:
            if (state->partial_byte &&
                    ((state->debug.hex && isxdigit(key.ch)) ||
                     (!state->debug.hex && isdigit(key.ch)))) {
                int digit;
                if (key.ch > '9') {
                    digit = tolower(key.ch) - 'a' + 10;
                } else {
                    digit = key.ch - '0';
                }
                size_t room_id = digit;
                if (state->debug.hex) {
                    room_id += 16 * (state->partial_byte & 0xFF);
                } else {
                    room_id += 10 * (state->partial_byte & 0xFF);
                }
                if (room_id < C_ARRAY_LEN(state->rooms.rooms)) {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    assert(chunk->type == TOGGLE_BIT);
                    push_undo((uint8_t)state->current_level, false);
                    chunk->room_idx = room_id;
                    chunk->switch_idx = 0;
                    state->current_state = EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS;
                    state->partial_byte = 0;
                    state->roomname_cursor = 0;
                    RESTORE_ROOM_NAME();
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }
            } else if (state->roomname_cursor == 0 && state->partial_byte == 0 &&
                       key.ch >= '0' &&
                       key.ch <= (state->debug.hex ? '3' : '6')) {
                state->partial_byte = 0xFF00 | (key.ch - '0');
            } else if (state->partial_byte == 0) {
                if (state->roomname_cursor < C_ARRAY_LEN(state->room_name) - 1) {
                    state->room_name[state->roomname_cursor++] = key.ch;

                    bool possible = false;
                    bool duplicate = false;
                    size_t matching_room = 0;
                    if (state->roomname_cursor) {
                        for (size_t _room = 0; _room < C_ARRAY_LEN(state->rooms.rooms); _room ++) {
                            struct DecompressedRoom *room = &state->rooms.rooms[_room].data;
                            if (strncasecmp(room->name, state->room_name, state->roomname_cursor) == 0) {
                                if (possible) {
                                    duplicate = true;
                                    break;
                                } else {
                                    duplicate = false;
                                }
                                possible = true;
                                matching_room = _room;
                            }
                        }
                    } else {
                        possible = true;
                    }
                    if (!possible) {
                        state->room_name[--state->roomname_cursor] = 0;
                    } else if (!duplicate) {
                        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                        assert(chunk->type == TOGGLE_BIT);
                        push_undo((uint8_t)state->current_level, false);
                        chunk->room_idx = matching_room;
                        chunk->switch_idx = 0;
                        state->current_state = EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS;
                        state->partial_byte = 0;
                        state->roomname_cursor = 0;
                        RESTORE_ROOM_NAME();
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        editor_save();
                    }
                }
            }
            break;
        case KEY_BS:
        case KEY_DELETE:
            if (state->roomname_cursor) {
                state->room_name[--state->roomname_cursor] = 0;
            } else {
                state->partial_byte = 0;
            }
            break;
        case KEY_ESC:
            if (state->help) {
                state->help = false;
            } else {
                state->current_state = EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS;
                state->partial_byte = 0;
                state->roomname_cursor = 0;
                RESTORE_ROOM_NAME();
            }
            break;
        case KEY_CTRL:
            switch (key.ch) {
                case '_': state->help = !state->help; break;
                case 'H':
                    state->debug.hex = !state->debug.hex;
                    if (state->partial_byte) {
                        if (state->debug.hex) {
                            state->partial_byte = ((state->partial_byte & 0xFF) * 10 / 16) % 16 + 1;
                        } else {
                            state->partial_byte = ((state->partial_byte & 0xFF) * 16 / 10) % 10 + 1;
                        }
                    }
                    break;
                case 'Q':
                    if (state->help) {
                        state->help = !state->help;
                    } else {
                        state->current_state = EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS;
                        state->partial_byte = 0;
                        state->roomname_cursor = 0;
                        RESTORE_ROOM_NAME();
                    }
                    break;
                case 'S': pop_chunk_state(); break;
            }
            break;
        default: break;
    }
#undef RESTORE_ROOM_NAME
    return true;
}

static bool handle_edit_switchdetails_chunk_memory(Key key) {
    size_t *cursorlevel = &state->current_level;
    switch (key.code) {
        case KEY_CHAR:
            if (isxdigit(key.ch)) {
                uint8_t b = 0;
                if (isdigit(key.ch)) {
                    b = key.ch - '0';
                } else {
                    b = 10 + tolower(key.ch) - 'a';
                }
                if ((state->partial_byte & 0xFF00) != 0) {
                    size_t value = b;
                    if (state->debug.hex) {
                        value += 16 * (state->partial_byte & 0xFF);
                    } else {
                        value += 10 * (state->partial_byte & 0xFF);
                    }
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    assert(chunk->type == TOGGLE_BLOCK);
                    push_undo((uint8_t)state->current_level, false);
                    if (state->switch_on) {
                        chunk->on = value;
                    } else {
                        chunk->off = value;
                    }
                    state->partial_byte = 0;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                } else {
                    state->partial_byte = 0xFF00 | b;
                }
            } else switch (key.ch) {
                case 'h':
                case 'k': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    assert(chunk->type == TOGGLE_BLOCK);
                    push_undo((uint8_t)state->current_level, false);
                    if (chunk->x) {
                        chunk->x --;
                    } else {
                        if (chunk->y == HEIGHT_TILES) {
                            chunk->y = (offsetof(struct DecompressedRoom, end_marker) - 1) / WIDTH_TILES;
                            chunk->x = (offsetof(struct DecompressedRoom, end_marker) - 1) % WIDTH_TILES;
                        } else {
                            chunk->y --;
                            chunk->x = WIDTH_TILES - 1;
                        }
                    }
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;
                case 'j':
                case 'l': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    assert(chunk->type == TOGGLE_BLOCK);
                    push_undo((uint8_t)state->current_level, false);
                    chunk->x ++;
                    if (chunk->x == WIDTH_TILES) {
                        chunk->x = 0;
                        chunk->y ++;
                    }
                    size_t overflow = WIDTH_TILES * HEIGHT_TILES;
                    size_t point = chunk->y * WIDTH_TILES + chunk->x;
                    size_t offset = point - overflow;
                    size_t end = offsetof(struct DecompressedRoom, end_marker) - overflow;
                    if (offset >= end) {
                        chunk->y = HEIGHT_TILES;
                        chunk->x = 0;
                    }
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;
                case 'o':
                    state->switch_on = !state->switch_on;
                    break;
                case ' ': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    assert(chunk->type == TOGGLE_BLOCK);
                    push_undo((uint8_t)state->current_level, false);
                    chunk->x ++;
                    if (chunk->x == WIDTH_TILES) {
                        chunk->x = 0;
                        chunk->y ++;
                    }
                    size_t overflow = WIDTH_TILES * HEIGHT_TILES;
                    size_t point = chunk->y * WIDTH_TILES + chunk->x;
                    size_t offset = point - overflow;
                    size_t end = offsetof(struct DecompressedRoom, end_marker) - overflow;
                    if (offset >= end) {
                        chunk->y = HEIGHT_TILES;
                        chunk->x = 0;
                    }
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;
                case '^': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    assert(chunk->type == TOGGLE_BLOCK);
                    push_undo((uint8_t)state->current_level, false);
                    if (chunk->size < 8) chunk->size ++;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;
                case 'v': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    assert(chunk->type == TOGGLE_BLOCK);
                    push_undo((uint8_t)state->current_level, false);
                    if (chunk->size > 1) chunk->size --;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;
                case 'r': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    assert(chunk->type == TOGGLE_BLOCK);
                    push_undo((uint8_t)state->current_level, false);
                    chunk->dir = (chunk->dir + 1) % NUM_DIRECTIONS;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;
                case 't': case 'T': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    push_undo((uint8_t)state->current_level, false);
                    cycle_chunk_type(chunk, *cursorlevel, key.ch == 't' ? 1 : -1);
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;
                case 'q':
                    if (state->help) {
                        state->help = !state->help;
                    } else {
                        if (state->partial_byte) {
                            state->partial_byte = 0;
                        } else {
                            pop_chunk_state();
                        }
                    }
                    break;
                case '?': state->help = !state->help; break;
                case '+': chunk_swap_next(); break;
                case '-': chunk_swap_prev(); break;
                case 'x': if (state->partial_byte) { state->partial_byte = 0; break; } yank_chunk(); delete_current_chunk(); break;
                case 'y': yank_chunk(); break;
                default: break;
            }
            break;
        case KEY_UP:
        case KEY_LEFT:
        {
            struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
            struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
            assert(chunk->type == TOGGLE_BLOCK);
            push_undo((uint8_t)state->current_level, false);
            if (chunk->x) {
                chunk->x --;
            } else {
                if (chunk->y == HEIGHT_TILES) {
                    chunk->y = (offsetof(struct DecompressedRoom, end_marker) - 1) / WIDTH_TILES;
                    chunk->x = (offsetof(struct DecompressedRoom, end_marker) - 1) % WIDTH_TILES;
                } else {
                    chunk->y --;
                    chunk->x = WIDTH_TILES - 1;
                }
            }
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            editor_save();
        } break;
        case KEY_DOWN:
        case KEY_RIGHT:
        {
            struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
            struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
            assert(chunk->type == TOGGLE_BLOCK);
            push_undo((uint8_t)state->current_level, false);
            chunk->x ++;
            if (chunk->x == WIDTH_TILES) {
                chunk->x = 0;
                chunk->y ++;
            }
            size_t overflow = WIDTH_TILES * HEIGHT_TILES;
            size_t point = chunk->y * WIDTH_TILES + chunk->x;
            size_t offset = point - overflow;
            size_t end = offsetof(struct DecompressedRoom, end_marker) - overflow;
            if (offset >= end) {
                chunk->y = HEIGHT_TILES;
                chunk->x = 0;
            }
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            editor_save();
        } break;
        case KEY_BS:
        case KEY_DELETE:
            if (state->partial_byte) {
                state->partial_byte = 0;
            } else {
                yank_chunk();
                delete_current_chunk();
            }
            break;
        case KEY_ESC:
            if (state->help) {
                state->help = false;
            } else {
                if (state->partial_byte) {
                    state->partial_byte = 0;
                } else {
                    pop_chunk_state();
                }
            }
            break;
        case KEY_CTRL:
            switch (key.ch) {
                case '_': state->help = !state->help; break;
                case 'H': state->debug.hex = !state->debug.hex; break;
                case 'P': fork_play(); break;
                case 'S': pop_chunk_state(); break;
            }
            break;
        default: break;
    }
    return true;
}

static bool handle_edit_switchdetails_chunk_block(Key key) {
    size_t *cursorlevel = &state->current_level;
    switch (key.code) {
        case KEY_CHAR:
            if (isxdigit(key.ch)) {
                uint8_t b = 0;
                if (isdigit(key.ch)) {
                    b = key.ch - '0';
                } else {
                    b = 10 + tolower(key.ch) - 'a';
                }
                if ((state->partial_byte & 0xFF00) != 0) {
                    size_t value = b;
                    if (state->debug.hex) {
                        value += 16 * (state->partial_byte & 0xFF);
                    } else {
                        value += 10 * (state->partial_byte & 0xFF);
                    }
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    assert(chunk->type == TOGGLE_BLOCK);
                    push_undo((uint8_t)state->current_level, false);
                    if (state->switch_on) {
                        chunk->on = value;
                    } else {
                        chunk->off = value;
                    }
                    state->partial_byte = 0;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                } else {
                    state->partial_byte = 0xFF00 | b;
                }
            } else switch (key.ch) {
                case 'o':
                    state->switch_on = !state->switch_on;
                    state->partial_byte = 0;
                    break;
                case ' ': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    assert(chunk->type == TOGGLE_BLOCK);
                    push_undo((uint8_t)state->current_level, false);
                    chunk->dir = (chunk->dir + 1) % NUM_DIRECTIONS;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;
                case '^': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    assert(chunk->type == TOGGLE_BLOCK);
                    push_undo((uint8_t)state->current_level, false);
                    if (chunk->size < 8) chunk->size ++;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;
                case 'v': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    assert(chunk->type == TOGGLE_BLOCK);
                    push_undo((uint8_t)state->current_level, false);
                    if (chunk->size > 1) chunk->size --;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;
                case 'h': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    assert(chunk->type == TOGGLE_BLOCK);
                    push_undo((uint8_t)state->current_level, false);
                    if (chunk->x) chunk->x --;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;
                case 'j': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    assert(chunk->type == TOGGLE_BLOCK);
                    push_undo((uint8_t)state->current_level, false);
                    chunk->y ++;
                    size_t overflow = WIDTH_TILES * HEIGHT_TILES;
                    size_t point = chunk->y * WIDTH_TILES + chunk->x;
                    if (point >= overflow) {
                        state->current_state = EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS;
                    }
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;
                case 'k': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    assert(chunk->type == TOGGLE_BLOCK);
                    push_undo((uint8_t)state->current_level, false);
                    if (chunk->y) chunk->y --;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;
                case 'l': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    assert(chunk->type == TOGGLE_BLOCK);
                    push_undo((uint8_t)state->current_level, false);
                    chunk->x ++;
                    size_t overflow = WIDTH_TILES * HEIGHT_TILES;
                    size_t point = chunk->y * WIDTH_TILES + chunk->x;
                    if (point >= overflow) {
                        state->current_state = EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS;
                    }
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;
                case 't': case 'T': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    push_undo((uint8_t)state->current_level, false);
                    cycle_chunk_type(chunk, *cursorlevel, key.ch == 't' ? 1 : -1);
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;
                case 'q':
                    if (state->help) {
                        state->help = !state->help;
                    } else {
                        if (state->partial_byte) {
                            state->partial_byte = 0;
                        } else {
                            pop_chunk_state();
                        }
                    }
                    break;
                case '?': state->help = !state->help; break;
                case '+': chunk_swap_next(); break;
                case 'x': if (state->partial_byte) { state->partial_byte = 0; break; } yank_chunk(); delete_current_chunk(); break;
                case '-': chunk_swap_prev(); break;
                case 'y': yank_chunk(); break;
                default: break;
            }
            break;
        case KEY_UP:
        {
            struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
            struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
            assert(chunk->type == TOGGLE_BLOCK);
            push_undo((uint8_t)state->current_level, false);
            if (chunk->y) chunk->y --;
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            editor_save();
        } break;
        case KEY_DOWN:
        {
            struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
            struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
            assert(chunk->type == TOGGLE_BLOCK);
            push_undo((uint8_t)state->current_level, false);
            chunk->y ++;
            size_t overflow = WIDTH_TILES * HEIGHT_TILES;
            size_t point = chunk->y * WIDTH_TILES + chunk->x;
            if (point >= overflow) {
                state->current_state = EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS;
            }
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            editor_save();
        } break;
        case KEY_LEFT:
        {
            struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
            struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
            assert(chunk->type == TOGGLE_BLOCK);
            push_undo((uint8_t)state->current_level, false);
            if (chunk->x) chunk->x --;
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            editor_save();
        } break;
        case KEY_RIGHT:
        {
            struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
            struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
            assert(chunk->type == TOGGLE_BLOCK);
            push_undo((uint8_t)state->current_level, false);
            chunk->x ++;
            size_t overflow = WIDTH_TILES * HEIGHT_TILES;
            size_t point = chunk->y * WIDTH_TILES + chunk->x;
            if (point >= overflow) {
                state->current_state = EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS;
            }
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            editor_save();
        } break;
        case KEY_BS:
        case KEY_DELETE:
            if (state->partial_byte) {
                state->partial_byte = 0;
            } else {
                yank_chunk();
                delete_current_chunk();
            }
            break;
        case KEY_ESC:
            if (state->help) {
                state->help = false;
            } else {
                if (state->partial_byte) {
                    state->partial_byte = 0;
                } else {
                    pop_chunk_state();
                }
            }
            break;
        case KEY_CTRL:
            switch (key.ch) {
                case '_': state->help = !state->help; break;
                case 'H': state->debug.hex = !state->debug.hex; break;
                case 'P': fork_play(); break;
                case 'S': pop_chunk_state(); break;
            }
            break;
        default: break;
    }
    return true;
}

// Whether a TOGGLE_OBJECT chunk's index currently names a sprite in this room (false, including
// out of bounds, means treat it as a block) - decides which attribute scheme this chunk's value
// byte gets edited/shown with (Hugh: "it should show sprite controls for sprite objects and
// block controls for block objects").
bool chunk_target_is_sprite(struct SwitchChunk *chunk) {
    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
    return chunk->index < room->num_objects && room->objects[chunk->index].type == SPRITE;
}

// A TOGGLE_OBJECT chunk's value byte targeting a BLOCK is the same scheme as a block's MOVERTAB
// entry (see the "heli" table comment above handle_edit_movertab_object) - independent
// horizontal/vertical axes, each speed 0-3 + direction + wall stop/bounce bit - so it gets the
// identical attribute editor: 's'/'d'/'w' arm speed/direction/walls focus (Hugh: "change the
// menu of object chunks so that you can set a precise value based on the attributes...
// consistent language and editing" with the MOVERTAB block editor), then hjkl/arrows adjust it
// (see block_speed_ratchet/block_direction_set). Targeting a SPRITE instead, there's no
// ratchet/direction/focus concept at all - d/f/m/M/s/w/e each act immediately, same as the
// MOVERTAB sprite editor (see the KEY_CHAR cases below) - so hjkl is a no-op for a sprite here,
// same as it is for one there once wall/edge stopped needing a focus.
static void chunk_value_dir_key(uint8_t *value, bool is_sprite, int dx, int dy) {
    if (is_sprite) {
        return;
    }
    if (state->chunk_value_focus == 's' || state->chunk_value_focus == 'd') {
        bool speed = state->chunk_value_focus == 's';
        if (dx) {
            if (speed) block_speed_ratchet(value, 0, 0x04, dx < 0);
            else block_direction_set(value, 0x04, dx < 0);
        }
        if (dy) {
            if (speed) block_speed_ratchet(value, 4, 0x40, dy < 0);
            else block_direction_set(value, 0x40, dy < 0);
        }
    } else if (state->chunk_value_focus == 'w') {
        if (dx) *value ^= 0x08;
        if (dy) *value ^= 0x80;
    }
}

static bool handle_edit_switchdetails_chunk_object(Key key) {
    size_t *cursorlevel = &state->current_level;
    switch (key.code) {
        case KEY_CHAR:
            // Raw hex entry for chunk->value only while explicitly armed with 'r' (same
            // convention as movertab_raw) - otherwise d/e/f would be swallowed as hex digits
            // instead of reaching the attribute commands below (Hugh: "only let you edit raw if
            // you push r... that way hex keys aren't swallowed, and e still works for test").
            if (state->chunk_value_raw && isxdigit(key.ch)) {
                uint8_t b = 0;
                if (isdigit(key.ch)) {
                    b = key.ch - '0';
                } else {
                    b = 10 + tolower(key.ch) - 'a';
                }
                if ((state->partial_byte & 0xFF00) != 0) {
                    size_t value = b;
                    if (state->debug.hex) {
                        value += 16 * (state->partial_byte & 0xFF);
                    } else {
                        value += 10 * (state->partial_byte & 0xFF);
                    }
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    assert(chunk->type == TOGGLE_OBJECT);
                    push_undo((uint8_t)state->current_level, false);
                    chunk->value = value;
                    state->partial_byte = 0;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                } else {
                    state->partial_byte = 0xFF00 | b;
                }
            } else switch (key.ch) {
                case 'r':
                    state->chunk_value_raw = !state->chunk_value_raw;
                    state->chunk_value_focus = 0;
                    state->partial_byte = 0;
                    break;
                case 't': case 'T': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    push_undo((uint8_t)state->current_level, false);
                    cycle_chunk_type(chunk, *cursorlevel, key.ch == 't' ? 1 : -1);
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;
                case 'i': case 'I': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    // Cycle within the room's actual object count, not a blind 0-15 - an index
                    // past num_objects doesn't reference anything (Hugh: "the idx field should
                    // disallow going out of bound").
                    size_t n = state->rooms.rooms[*cursorlevel].data.num_objects;
                    if (n > 0) {
                        push_undo((uint8_t)state->current_level, false);
                        chunk->index = (uint8_t)((chunk->index + (key.ch == 'i' ? 1 : n - 1)) % n);
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        editor_save();
                    }
                }; break;
                // Whether this chunk fires when the switch turns off or on (Hugh: "i think
                // only press=0 and press=2 exist, for off and on respectively" - so this is a
                // plain toggle between the two, not a cycle through all four raw bit patterns).
                case 'o': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    push_undo((uint8_t)state->current_level, false);
                    chunk->test = (chunk->test & 0x20) ? 0x00 : 0x20;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;
                // Attribute editor for chunk->value: block controls (s/d/w focus, see
                // chunk_value_dir_key's comment above) or the sprite MOVERTAB scheme
                // (sprite_toggle_dead/sprite_cycle_flip/etc.), whichever chunk->index actually
                // names (Hugh: "it should show sprite controls for sprite objects and block
                // controls for block objects").
                case 'd': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    if (chunk_target_is_sprite(chunk)) {
                        push_undo((uint8_t)state->current_level, false);
                        sprite_toggle_dead(&chunk->value);
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        editor_save();
                    } else {
                        state->chunk_value_focus = (state->chunk_value_focus == 'd') ? 0 : 'd';
                    }
                }; break;
                case 'f': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    if (chunk_target_is_sprite(chunk)) {
                        // No flip here - a switch command can't flip a sprite in the real game,
                        // only MOVERTAB.DAT at room load can (Hugh: "switches can't flip, that
                        // is only on room load via movertab") - the MOVERTAB sprite editor
                        // (handle_edit_movertab_object) keeps its own 'f'.
                        snprintf(state->status_message, sizeof(state->status_message), "Flip is fixed at room load by MOVERTAB.DAT - not editable from a switch");
                    } else {
                        snprintf(state->status_message, sizeof(state->status_message), "Blocks: s/d/w for speed/direction/walls, then arrows or |/-");
                    }
                }; break;
                case 'm':
                case 'M': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    if (chunk_target_is_sprite(chunk)) {
                        if (chunk->value == 0x00) {
                            snprintf(state->status_message, sizeof(state->status_message), "Dead - press d to make it alive first");
                        } else {
                            push_undo((uint8_t)state->current_level, false);
                            sprite_cycle_mode(&chunk->value, key.ch == 'm' ? 1 : -1);
                            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                            editor_save();
                        }
                    } else {
                        snprintf(state->status_message, sizeof(state->status_message), "Blocks: s/d/w for speed/direction/walls, then arrows or |/-");
                    }
                }; break;
                case 's': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    if (chunk_target_is_sprite(chunk)) {
                        if (chunk->value == 0x00) {
                            snprintf(state->status_message, sizeof(state->status_message), "Dead - press d to make it alive first");
                        } else if (!(chunk->value & 0x40)) {
                            snprintf(state->status_message, sizeof(state->status_message), "Only a chasing enemy can be stopped - press m to chase first");
                        } else {
                            push_undo((uint8_t)state->current_level, false);
                            sprite_toggle_stopped(&chunk->value);
                            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                            editor_save();
                        }
                    } else {
                        state->chunk_value_focus = (state->chunk_value_focus == 's') ? 0 : 's';
                    }
                }; break;
                case 'w': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    if (chunk_target_is_sprite(chunk)) {
                        // Direct toggle, same as the MOVERTAB sprite editor's 'w' - no focus to
                        // arm, since wall-turning is a single independent bit.
                        if (chunk->value == 0x00) {
                            snprintf(state->status_message, sizeof(state->status_message), "Dead - press d to make it alive first");
                        } else if (!(chunk->value & 0x48)) {
                            snprintf(state->status_message, sizeof(state->status_message), "Not moving - press m first");
                        } else {
                            push_undo((uint8_t)state->current_level, false);
                            sprite_toggle_wall(&chunk->value);
                            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                            editor_save();
                        }
                        break;
                    }
                    // Only one axis moving means only one wall option exists - toggle it
                    // directly rather than making the user disambiguate (same as the MOVERTAB
                    // block editor's 'w').
                    bool v_moving = ((chunk->value >> 4) & 0x03) != 0, h_moving = (chunk->value & 0x03) != 0;
                    if (v_moving && !h_moving) {
                        push_undo((uint8_t)state->current_level, false);
                        chunk->value ^= 0x80;
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        editor_save();
                    } else if (h_moving && !v_moving) {
                        push_undo((uint8_t)state->current_level, false);
                        chunk->value ^= 0x08;
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        editor_save();
                    } else {
                        state->chunk_value_focus = (state->chunk_value_focus == 'w') ? 0 : 'w';
                    }
                }; break;
                case 'e': {
                    // Direct toggle for edge-turning, sprite-only, mirroring 'w' (same as the
                    // MOVERTAB sprite editor's 'e'). Blocks have no edge concept.
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    if (chunk_target_is_sprite(chunk)) {
                        if (chunk->value == 0x00) {
                            snprintf(state->status_message, sizeof(state->status_message), "Dead - press d to make it alive first");
                        } else if (!(chunk->value & 0x48)) {
                            snprintf(state->status_message, sizeof(state->status_message), "Not moving - press m first");
                        } else {
                            push_undo((uint8_t)state->current_level, false);
                            sprite_toggle_edge(&chunk->value);
                            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                            editor_save();
                        }
                    } else {
                        snprintf(state->status_message, sizeof(state->status_message), "Blocks: s/d/w for speed/direction/walls, then arrows or |/-");
                    }
                }; break;
                case 'h': case 'l': case 'j': case 'k':
                    if (state->chunk_value_focus) {
                        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                        push_undo((uint8_t)state->current_level, false);
                        chunk_value_dir_key(&chunk->value, chunk_target_is_sprite(chunk),
                                key.ch == 'h' ? -1 : key.ch == 'l' ? 1 : 0,
                                key.ch == 'k' ? -1 : key.ch == 'j' ? 1 : 0);
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        editor_save();
                    }
                    break;
                case 'q':
                    if (state->help) {
                        state->help = !state->help;
                    } else if (state->partial_byte) {
                        state->partial_byte = 0;
                    } else if (state->chunk_value_focus) {
                        state->chunk_value_focus = 0;
                    } else {
                        pop_chunk_state();
                    }
                    break;
                case '?': state->help = !state->help; break;
                case 'x': if (state->partial_byte) { state->partial_byte = 0; break; } yank_chunk(); delete_current_chunk(); break;
                case '+': chunk_swap_next(); break;
                case '-': chunk_swap_prev(); break;
                case 'y': yank_chunk(); break;
                default: break;
            }
            break;
        case KEY_LEFT:  if (state->chunk_value_focus) { struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1; struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk; push_undo((uint8_t)state->current_level, false); chunk_value_dir_key(&chunk->value, chunk_target_is_sprite(chunk), -1, 0); ARRAY_FREE(state->rooms.rooms[state->current_level].compressed); editor_save(); } break;
        case KEY_RIGHT: if (state->chunk_value_focus) { struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1; struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk; push_undo((uint8_t)state->current_level, false); chunk_value_dir_key(&chunk->value, chunk_target_is_sprite(chunk), 1, 0); ARRAY_FREE(state->rooms.rooms[state->current_level].compressed); editor_save(); } break;
        case KEY_UP:    if (state->chunk_value_focus) { struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1; struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk; push_undo((uint8_t)state->current_level, false); chunk_value_dir_key(&chunk->value, chunk_target_is_sprite(chunk), 0, -1); ARRAY_FREE(state->rooms.rooms[state->current_level].compressed); editor_save(); } break;
        case KEY_DOWN:  if (state->chunk_value_focus) { struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1; struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk; push_undo((uint8_t)state->current_level, false); chunk_value_dir_key(&chunk->value, chunk_target_is_sprite(chunk), 0, 1); ARRAY_FREE(state->rooms.rooms[state->current_level].compressed); editor_save(); } break;
        case KEY_BS:
        case KEY_DELETE:
            if (state->partial_byte) {
                state->partial_byte = 0;
            } else {
                yank_chunk();
                delete_current_chunk();
            }
            break;
        // Layered, matching the MOVERTAB block editor: close help, else drop the speed/
        // direction/walls focus back to plain view, else fall through to the usual escape.
        case KEY_ESC:
            if (state->help) {
                state->help = false;
            } else if (state->partial_byte) {
                state->partial_byte = 0;
            } else if (state->chunk_value_focus) {
                state->chunk_value_focus = 0;
            } else {
                pop_chunk_state();
            }
            break;
        case KEY_CTRL:
            switch (key.ch) {
                case '_': state->help = !state->help; break;
                case 'H': state->debug.hex = !state->debug.hex; break;
                case 'P': fork_play(); break;
                case 'J':
                case 'M':
                    if (state->help) state->help = false;
                    else if (state->chunk_value_focus) state->chunk_value_focus = 0;
                    break;
                case 'S': pop_chunk_state(); break;
            }
            break;
        default: break;
    }
    return true;
}

static bool handle_normal(Key key) {
    v2 *cursor = &state->cursors[state->current_level];
    size_t *cursorlevel = &state->current_level;
    bool is_tile = (state->current_state == TILE_EDIT);

    switch (key.code) {
        case KEY_CHAR:
            switch (key.ch) {
                case 'q':
                    if (state->help) {
                        state->help = false;
                    } else {
                        if (state->partial_byte) {
                            state->partial_byte = 0;
                        } else {
                            end();
                            UNREACHABLE();
                        }
                    }
                    break;
                case '?':
                    state->help = !state->help;
                    break;
                case 'h':
                    if (key.mod == MOD_NONE) {
                        cursor->x --;
                        state->partial_byte = 0;
                        state->current_switch = 0;
                        state->switch_on = false;
                        state->current_chunk = 0;
                        if (cursor->x < 0) {
                            cursor->x ++;
                            size_t level = state->rooms.rooms[*cursorlevel].data.room_west;
                            state->cursors[level].y = state->cursors[*cursorlevel].y;
                            state->cursors[level].x = WIDTH_TILES - 1;
                            size_t old_level = *cursorlevel;
                            *cursorlevel = level;
                            room_prune_blank(old_level);
                        }
                    }
                    break;
                case 'j':
                    if (key.mod == MOD_NONE) {
                        cursor->y ++;
                        state->partial_byte = 0;
                        state->current_switch = 0;
                        state->switch_on = false;
                        state->current_chunk = 0;
                        if (cursor->y >= HEIGHT_TILES) {
                            cursor->y --;
                            size_t level = state->rooms.rooms[*cursorlevel].data.room_south;
                            state->cursors[level].y = 0;
                            state->cursors[level].x = state->cursors[*cursorlevel].x;
                            size_t old_level = *cursorlevel;
                            *cursorlevel = level;
                            room_prune_blank(old_level);
                        }
                    }
                    break;
                case 'k':
                    if (key.mod == MOD_NONE) {
                        cursor->y --;
                        state->partial_byte = 0;
                        state->current_switch = 0;
                        state->switch_on = false;
                        state->current_chunk = 0;
                        if (cursor->y < 0) {
                            cursor->y ++;
                            size_t level = state->rooms.rooms[*cursorlevel].data.room_north;
                            state->cursors[level].y = HEIGHT_TILES - 1;
                            state->cursors[level].x = state->cursors[*cursorlevel].x;
                            size_t old_level = *cursorlevel;
                            *cursorlevel = level;
                            room_prune_blank(old_level);
                        }
                    }
                    break;
                case 'l':
                    if (key.mod == MOD_NONE) {
                        cursor->x ++;
                        state->partial_byte = 0;
                        state->current_switch = 0;
                        state->switch_on = false;
                        state->current_chunk = 0;
                        if (cursor->x >= WIDTH_TILES) {
                            cursor->x --;
                            size_t level = state->rooms.rooms[*cursorlevel].data.room_east;
                            state->cursors[level].x = 0;
                            state->cursors[level].y = state->cursors[*cursorlevel].y;
                            size_t old_level = *cursorlevel;
                            *cursorlevel = level;
                            room_prune_blank(old_level);
                        }
                    }
                    break;
                case 'u': {
                    uint8_t r = do_undo();
                    if (r == 255) {
                        snprintf(state->status_message, sizeof(state->status_message), "Nothing to undo");
                    } else {
                        state->current_level = r;
                        snprintf(state->status_message, sizeof(state->status_message), "Undo");
                    }
                    return true;
                }
                case 'U': {
                    uint8_t r = do_redo();
                    if (r == 255) {
                        snprintf(state->status_message, sizeof(state->status_message), "Nothing to redo");
                    } else {
                        state->current_level = r;
                        snprintf(state->status_message, sizeof(state->status_message), "Redo");
                    }
                    return true;
                }
                case 'r':
                    state->previous_state = state->current_state;
                    state->current_state = GOTO_ROOM;
                    state->current_switch = 0;
                    state->switch_on = false;
                    state->current_chunk = 0;
                    state->partial_byte = 0;
                    break;
                case 's':
                    state->previous_state = state->current_state;
                    state->current_state = GOTO_SWITCH;
                    state->current_switch = 0;
                    state->switch_on = false;
                    state->current_chunk = 0;
                    state->partial_byte = 0;
                    break;
                case 'o':
                    state->previous_state = state->current_state;
                    state->current_state = GOTO_OBJECT;
                    state->current_switch = 0;
                    state->switch_on = false;
                    state->current_chunk = 0;
                    state->partial_byte = 0;
                    break;
                case ' ':
                    toggle_hover_switch_preview();
                    break;
                case 'g':
                    state->goto_coord_x = -1;
                    state->goto_coord_move = false;
                    state->partial_byte = 0;
                    state->previous_state = state->current_state;
                    state->current_state = GOTO_COORD;
                    break;
                case 'm':
                    if (state->macro_recording) {
                        /* Stop recording: transfer buf ownership to slot */
                        KeyArray *slot = &state->macro_slots[state->macro_slot - 'a'];
                        ARRAY_FREE(*slot);
                        *slot = state->macro_buf;
                        state->macro_buf = (KeyArray){0};
                        state->macro_last_saved = state->macro_slot;
                        state->macro_recording = false;
                        snprintf(state->status_message, sizeof(state->status_message),
                                 "Macro '%c' saved (%zu keys)", state->macro_slot,
                                 slot->length);
                    } else if (state->macro_replay.length == 0) {
                        state->macro_meta = true;
                    }
                    break;
                case ',':
                    if (!state->macro_recording && state->macro_replay.length == 0) {
                        state->macro_replay_pending = true;
                    }
                    break;
                default: break;
            }
            break;
        case KEY_LEFT:
            if (key.mod == MOD_NONE) {
                cursor->x --;
                state->partial_byte = 0;
                state->current_switch = 0;
                state->switch_on = false;
                state->current_chunk = 0;
                if (cursor->x < 0) {
                    cursor->x ++;
                    size_t level = state->rooms.rooms[*cursorlevel].data.room_west;
                    state->cursors[level].y = state->cursors[*cursorlevel].y;
                    state->cursors[level].x = WIDTH_TILES - 1;
                    size_t old_level = *cursorlevel;
                    *cursorlevel = level;
                    room_prune_blank(old_level);
                }
            }
            break;
        case KEY_DOWN:
            if (key.mod == MOD_NONE) {
                cursor->y ++;
                state->partial_byte = 0;
                state->current_switch = 0;
                state->switch_on = false;
                state->current_chunk = 0;
                if (cursor->y >= HEIGHT_TILES) {
                    cursor->y --;
                    size_t level = state->rooms.rooms[*cursorlevel].data.room_south;
                    state->cursors[level].y = 0;
                    state->cursors[level].x = state->cursors[*cursorlevel].x;
                    size_t old_level = *cursorlevel;
                    *cursorlevel = level;
                    room_prune_blank(old_level);
                }
            }
            break;
        case KEY_UP:
            if (key.mod == MOD_NONE) {
                cursor->y --;
                state->partial_byte = 0;
                state->current_switch = 0;
                state->switch_on = false;
                state->current_chunk = 0;
                if (cursor->y < 0) {
                    cursor->y ++;
                    size_t level = state->rooms.rooms[*cursorlevel].data.room_north;
                    state->cursors[level].y = HEIGHT_TILES - 1;
                    state->cursors[level].x = state->cursors[*cursorlevel].x;
                    size_t old_level = *cursorlevel;
                    *cursorlevel = level;
                    room_prune_blank(old_level);
                }
            }
            break;
        case KEY_RIGHT:
            if (key.mod == MOD_NONE) {
                cursor->x ++;
                state->partial_byte = 0;
                state->current_switch = 0;
                state->switch_on = false;
                state->current_chunk = 0;
                if (cursor->x >= WIDTH_TILES) {
                    cursor->x --;
                    size_t level = state->rooms.rooms[*cursorlevel].data.room_east;
                    state->cursors[level].x = 0;
                    state->cursors[level].y = state->cursors[*cursorlevel].y;
                    size_t old_level = *cursorlevel;
                    *cursorlevel = level;
                    room_prune_blank(old_level);
                }
            }
            break;
        default: break;
    }

    if (is_tile) {
        if (key.code == KEY_LEFT || key.code == KEY_DOWN || key.code == KEY_UP || key.code == KEY_RIGHT) {
            int dx = (key.code == KEY_LEFT) ? -1 : (key.code == KEY_RIGHT) ? 1 : 0;
            int dy = (key.code == KEY_UP)   ? -1 : (key.code == KEY_DOWN)  ? 1 : 0;
            if      (key.mod == MOD_SHIFT)             { move(dx, dy); }
            else if (key.mod == MOD_ALT)               { stretch(dx, dy); }
            else if (key.mod == (MOD_ALT | MOD_SHIFT)) { shrink(dx, dy); }
        } else if (key.code == KEY_CHAR && key.mod == MOD_NONE) {
            /* SHIFT+hjkl (uppercase): move(); G: warp item to typed coord */
            switch (key.ch) {
                case 'H': move(-1,  0); break;
                case 'J': move( 0, +1); break;
                case 'K': move( 0, -1); break;
                case 'L': move(+1,  0); break;
                case 'G':
                    state->goto_coord_x = -1;
                    state->goto_coord_move = true;
                    state->partial_byte = 0;
                    state->previous_state = state->current_state;
                    state->current_state = GOTO_COORD;
                    break;
            }
        } else if (key.code == KEY_CHAR && key.mod == MOD_ALT) {
            /* Alt+hjkl: stretch(); Alt+HJKL: shrink() */
            switch (key.ch) {
                case 'h': stretch(-1,  0); break;
                case 'j': stretch( 0, +1); break;
                case 'k': stretch( 0, -1); break;
                case 'l': stretch(+1,  0); break;
                case 'H': shrink(-1,  0); break;
                case 'J': shrink( 0, +1); break;
                case 'K': shrink( 0, -1); break;
                case 'L': shrink(+1,  0); break;
            }
        }
    }

    switch (key.code) {
        case KEY_CTRL:
            switch (key.ch) {
                case '_': state->help = !state->help; break;
                case 'D':
                    state->previous_state = state->current_state;
                    state->current_state = TOGGLE_DISPLAY;
                    state->current_switch = 0;
                    state->switch_on = false;
                    state->current_chunk = 0;
                    break;
                case 'H': state->debug.hex = !state->debug.hex; break;
                case 'P': fork_play(); break;
                case 'T': {
                    switch (state->current_state) {
                        case NORMAL:
                            state->previous_state = state->current_state;
                            state->current_state = TILE_EDIT;
                            state->current_switch = 0;
                            state->switch_on = false;
                            state->current_chunk = 0;
                            break;
                        case TILE_EDIT:
                            state->previous_state = state->current_state;
                            state->current_state = NORMAL;
                            state->current_switch = 0;
                            state->switch_on = false;
                            state->current_chunk = 0;
                            break;
                        default: UNREACHABLE();
                    }
                }; break;
            }
            break;
        case KEY_ESC:
            if (state->help) {
                state->help = false;
            } else {
                if ((state->partial_byte & 0xFF00) != 0) {
                    state->partial_byte = 0;
                }
            }
            break;
        default: break;
    }

    return true;
}

/* Returns true if there is an object/switch/chunk at (x,y) that move() would act on. */
static bool has_draggable_at(int x, int y) {
    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
    if (state->debug.switches) {
        for (size_t i = 0; i < room->num_switches; i++) {
            struct SwitchObject *sw = room->switches + i;
            if (sw->chunks.length > 0 && sw->chunks.data[0].type == PREAMBLE &&
                    sw->chunks.data[0].x == (uint8_t)x && sw->chunks.data[0].y == (uint8_t)y)
                return true;
            for (size_t c = 1; c < sw->chunks.length; c++) {
                struct SwitchChunk *chunk = sw->chunks.data + c;
                if (chunk->type != TOGGLE_BLOCK) continue;
                if (chunk->dir == HORIZONTAL && y == chunk->y &&
                        x >= chunk->x && x < chunk->x + (int)chunk->size) return true;
                if (chunk->dir == VERTICAL   && x == chunk->x &&
                        y >= chunk->y && y < chunk->y + (int)chunk->size) return true;
            }
        }
    }
    if (state->debug.objects) {
        for (size_t i = 0; i < room->num_objects; i++) {
            struct RoomObject *o = room->objects + i;
            if (o->type == SPRITE && o->x == (uint8_t)x && o->y == (uint8_t)y) return true;
            if (o->type == BLOCK && x >= o->x && x < o->x + o->block.width &&
                    y >= o->y && y < o->y + o->block.height) return true;
        }
    }
    return false;
}

static bool handle_tile_edit(Key key) {
    size_t *cursorlevel = &state->current_level;
    switch (key.code) {
    case KEY_CHAR:
        if (!state->partial_byte && key.ch >= '1' && key.ch <= '4') {
            struct DecompressedRoom *room = &state->rooms.rooms[*cursorlevel].data;
            int x = state->cursors[*cursorlevel].x;
            int y = state->cursors[*cursorlevel].y;
            for (size_t j = 0; j < room->num_objects; j++) {
                struct RoomObject *obj = room->objects + j;
                if (obj->type == SPRITE && x == obj->x && y == obj->y) {
                    push_undo((uint8_t)*cursorlevel, false);
                    obj->sprite.damage = (uint8_t)(key.ch - '0');
                    ARRAY_FREE(state->rooms.rooms[*cursorlevel].compressed);
                    editor_save();
                    return true;
                }
            }
        }
        if (isxdigit(key.ch)) {
            uint8_t b = 0;
            if (isdigit(key.ch)) {
                b = key.ch - '0';
            } else {
                b = 10 + tolower(key.ch) - 'a';
            }
            if ((state->partial_byte & 0xFF00) != 0) {
                size_t value = b;
                if (state->debug.hex) {
                    value += 16 * (state->partial_byte & 0xFF);
                } else {
                    value += 10 * (state->partial_byte & 0xFF);
                }
                bool obj = false;
                bool ch = false;
                int x = state->cursors[state->current_level].x;
                int y = state->cursors[state->current_level].y;
                struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                push_undo((uint8_t)state->current_level, false);
                for (size_t j = 0; j < room->num_objects; j ++) {
                    struct RoomObject *object = room->objects + j;
                    if (object->type == BLOCK &&
                            x >= object->x && x < object->x + object->block.width &&
                            y >= object->y && y < object->y + object->block.height) {
                        obj = true;
                        object->tiles[(y - object->y) * object->block.width + (x - object->x)] = value;
                        break;
                    } else if (object->type == SPRITE && x == object->x && y == object->y) {
                        obj = true;
                        object->sprite.type = value >> 4;
                        object->sprite.damage = value & 0xF;
                        break;
                    }
                }
                if (!obj) {
                    struct SwitchObject *switcch = NULL;
                    struct SwitchChunk *chunk = NULL;
                    for (size_t j = 0; j < room->num_switches; j ++) {
                        switcch = room->switches + j;
                        for (size_t c = 1; c < switcch->chunks.length; c ++) {
                            chunk = switcch->chunks.data + c;
                            if (chunk->type == TOGGLE_BLOCK &&
                                    ((chunk->dir == VERTICAL && x == chunk->x && y >= chunk->y && y < chunk->y + chunk->size) ||
                                     (chunk->dir == HORIZONTAL && y == chunk->y && x >= chunk->x && x < chunk->x + chunk->size))) {
                                ch = true;
                                break;
                            }
                        }
                        if (ch) {
                            chunk->on = value;
                            break;
                        }
                    }
                }
                if (!obj && !ch) room->tiles[TILE_IDX(x, y)] = value;
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                editor_save();
                state->partial_byte = 0;
            } else {
                state->partial_byte = 0xFF00 | b;
            }
            return true;
        } else switch (key.ch) {
        case 'R':
            state->previous_state = state->current_state;
            state->current_state = EDIT_ROOMNAME;
            state->current_switch = 0;
            state->switch_on = false;
            state->current_chunk = 0;
            state->roomname_cursor = 0;
            strncpy(state->room_name, state->rooms.rooms[*cursorlevel].data.name, C_ARRAY_LEN(state->room_name));
            for (size_t j = 0; j < C_ARRAY_LEN(state->room_name); j ++) {
                if (state->room_name[C_ARRAY_LEN(state->room_name) - j] == '\0') continue;
                if (state->room_name[C_ARRAY_LEN(state->room_name) - j] == ' ') {
                    state->room_name[C_ARRAY_LEN(state->room_name) - j] = '\0';
                } else {
                    break;
                }
            }
            break;
        case '-': {
            struct RoomObject *object_underneath = NULL;
            struct SwitchObject *switch_underneath = NULL;
            struct SwitchChunk *chunk_underneath = NULL;
            struct SwitchObject *chunk_switch_underneath = NULL;

            struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
            int x = state->cursors[state->current_level].x;
            int y = state->cursors[state->current_level].y;
            int i;
            for (i = 0; i < room->num_objects; i ++) {
                struct RoomObject *object = room->objects + i;
                if (object->type == BLOCK &&
                        x >= object->x && x < object->x + object->block.width &&
                        y >= object->y && y < object->y + object->block.height) {
                    object_underneath = object;
                    break;
                } else if (object->type == SPRITE && x == object->x && y == object->y) {
                    object_underneath = object;
                    break;
                }
            }
            int obj_i = i;
            for (i = 0; i < room->num_switches; i ++) {
                struct SwitchObject *switcch = room->switches + i;
                if (switcch->chunks.length > 0 && switcch->chunks.data[0].type == PREAMBLE &&
                        x == switcch->chunks.data[0].x && y == switcch->chunks.data[0].y) {
                    switch_underneath = switcch;
                    break;
                }
            }
            int sw_i = i;
            int c;
            for (i = 0; i < room->num_switches; i ++) {
                struct SwitchObject *switcch = room->switches + i;
                for (c = 1; c < (signed) switcch->chunks.length; c ++) {
                    struct SwitchChunk *chunk = switcch->chunks.data + c;
                    if (chunk->type == TOGGLE_BLOCK) {
                        if (chunk->dir == HORIZONTAL) {
                            if (y == chunk->y && x >= chunk->x && x < chunk->x + chunk->size) {
                                chunk_switch_underneath = switcch;
                                chunk_underneath = chunk;
                                break;
                            }
                        } else if (x == chunk->x && y >= chunk->y && y < chunk->y + chunk->size) {
                            chunk_switch_underneath = switcch;
                            chunk_underneath = chunk;
                            break;
                        }
                    }
                }
                if (chunk_underneath) break;
            }

            push_undo((uint8_t)state->current_level, false);
            if (object_underneath) {
                struct RoomObject object = *object_underneath;
                if (obj_i) {
                    room->objects[obj_i] = room->objects[obj_i-1];
                    room->objects[obj_i-1] = object;
                }
            } else if (switch_underneath) {
                struct SwitchObject sw = *switch_underneath;
                if (sw_i) {
                    room->switches[sw_i] = room->switches[sw_i-1];
                    room->switches[sw_i-1] = sw;
                }
            } else if (chunk_underneath) {
                struct SwitchChunk chk = *chunk_underneath;
                if (c) {
                    chunk_switch_underneath->chunks.data[c] = chunk_switch_underneath->chunks.data[c-1];
                    chunk_switch_underneath->chunks.data[c-1] = chk;
                }
            }
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            editor_save();
        }; break;
        case '+': {
            struct RoomObject *object_underneath = NULL;
            struct SwitchObject *switch_underneath = NULL;
            struct SwitchChunk *chunk_underneath = NULL;
            struct SwitchObject *chunk_switch_underneath = NULL;

            struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
            int x = state->cursors[state->current_level].x;
            int y = state->cursors[state->current_level].y;
            int i;
            for (i = 0; i < room->num_objects; i ++) {
                struct RoomObject *object = room->objects + i;
                if (object->type == BLOCK &&
                        x >= object->x && x < object->x + object->block.width &&
                        y >= object->y && y < object->y + object->block.height) {
                    object_underneath = object;
                    break;
                } else if (object->type == SPRITE && x == object->x && y == object->y) {
                    object_underneath = object;
                    break;
                }
            }
            int obj_i = i;
            for (i = 0; i < room->num_switches; i ++) {
                struct SwitchObject *switcch = room->switches + i;
                if (switcch->chunks.length > 0 && switcch->chunks.data[0].type == PREAMBLE &&
                        x == switcch->chunks.data[0].x && y == switcch->chunks.data[0].y) {
                    switch_underneath = switcch;
                    break;
                }
            }
            int sw_i = i;
            int c;
            for (i = 0; i < room->num_switches; i ++) {
                struct SwitchObject *switcch = room->switches + i;
                for (c = 1; c < (signed) switcch->chunks.length; c ++) {
                    struct SwitchChunk *chunk = switcch->chunks.data + c;
                    if (chunk->type == TOGGLE_BLOCK) {
                        if (chunk->dir == HORIZONTAL) {
                            if (y == chunk->y && x >= chunk->x && x < chunk->x + chunk->size) {
                                chunk_switch_underneath = switcch;
                                chunk_underneath = chunk;
                                break;
                            }
                        } else if (x == chunk->x && y >= chunk->y && y < chunk->y + chunk->size) {
                            chunk_switch_underneath = switcch;
                            chunk_underneath = chunk;
                            break;
                        }
                    }
                }
                if (chunk_underneath) break;
            }

            push_undo((uint8_t)state->current_level, false);
            if (object_underneath) {
                struct RoomObject object = *object_underneath;
                if (obj_i < room->num_objects - 1) {
                    room->objects[obj_i] = room->objects[obj_i+1];
                    room->objects[obj_i+1] = object;
                }
            } else if (switch_underneath) {
                struct SwitchObject sw = *switch_underneath;
                if (sw_i < room->num_switches - 1) {
                    room->switches[sw_i] = room->switches[sw_i+1];
                    room->switches[sw_i+1] = sw;
                }
            } else if (chunk_underneath) {
                struct SwitchChunk chk = *chunk_underneath;
                if (c < (signed) chunk_switch_underneath->chunks.length - 1) {
                    chunk_switch_underneath->chunks.data[c] = chunk_switch_underneath->chunks.data[c+1];
                    chunk_switch_underneath->chunks.data[c+1] = chk;
                }
            }
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            editor_save();
        }; break;
        case 'u': {
            uint8_t r = do_undo();
            if (r == 255) {
                snprintf(state->status_message, sizeof(state->status_message), "Nothing to undo");
            } else {
                state->current_level = r;
                snprintf(state->status_message, sizeof(state->status_message), "Undo");
            }
            return true;
        }
        case 'U': {
            uint8_t r = do_redo();
            if (r == 255) {
                snprintf(state->status_message, sizeof(state->status_message), "Nothing to redo");
            } else {
                state->current_level = r;
                snprintf(state->status_message, sizeof(state->status_message), "Redo");
            }
            return true;
        }
        case 'y': yank_at_cursor(); break;
        case 'x': yank_at_cursor(); delete_item_at_cursor(); break;
        case 'p': {
            struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
            size_t x = state->cursors[state->current_level].x;
            size_t y = state->cursors[state->current_level].y;
            if (state->clipboard.type == CLIP_OBJECT) {
                if (room->num_objects >= 7) {
                    snprintf(state->status_message, sizeof(state->status_message), "Max 7 objects");
                    return true;
                }
                push_undo((uint8_t)state->current_level, false);
                room->objects = realloc(room->objects, (room->num_objects + 1) * sizeof(struct RoomObject));
                assert(room->objects != NULL);
                size_t idx = room->num_objects;
                room->objects[idx] = state->clipboard.object;
                room->objects[idx].x = x;
                room->objects[idx].y = y;
                if (room->objects[idx].type == BLOCK && state->clipboard.object.tiles) {
                    size_t tile_n = room->objects[idx].block.width * room->objects[idx].block.height;
                    room->objects[idx].tiles = malloc(tile_n);
                    assert(room->objects[idx].tiles != NULL);
                    memcpy(room->objects[idx].tiles, state->clipboard.object.tiles, tile_n);
                } else {
                    room->objects[idx].tiles = NULL;
                }
                room->num_objects++;
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                editor_save();
            } else if (state->clipboard.type == CLIP_SWITCH) {
                if (room->num_switches >= 15) {
                    snprintf(state->status_message, sizeof(state->status_message), "Max 15 switches");
                    return true;
                }
                push_undo((uint8_t)state->current_level, false);
                room->switches = realloc(room->switches, (room->num_switches + 1) * sizeof(struct SwitchObject));
                assert(room->switches != NULL);
                size_t idx = room->num_switches;
                size_t chunk_n = state->clipboard.sw.chunks.length;
                room->switches[idx].chunks.length = chunk_n;
                room->switches[idx].chunks.capacity = chunk_n;
                room->switches[idx].chunks.data = malloc(chunk_n * sizeof(struct SwitchChunk));
                assert(room->switches[idx].chunks.data != NULL);
                memcpy(room->switches[idx].chunks.data, state->clipboard.sw.chunks.data,
                       chunk_n * sizeof(struct SwitchChunk));
                if (chunk_n > 0) {
                    room->switches[idx].chunks.data[0].x = x;
                    room->switches[idx].chunks.data[0].y = y;
                    room->tiles[TILE_IDX(x, y)] = state->clipboard.tile;
                }
                clear_switch_block_tiles(room, room->switches + idx);
                room->num_switches++;
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                editor_save();
            } else if (state->clipboard.type == CLIP_CHUNK) {
                /* Append chunk to switch preamble at cursor */
                bool found_sw = false;
                for (size_t sw_i = 0; sw_i < room->num_switches; sw_i++) {
                    struct SwitchObject *sw = room->switches + sw_i;
                    if (sw->chunks.length > 0 && sw->chunks.data[0].type == PREAMBLE &&
                            sw->chunks.data[0].x == x && sw->chunks.data[0].y == y) {
                        push_undo((uint8_t)state->current_level, false);
                        ARRAY_ADD(sw->chunks, state->clipboard.chunk);
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        editor_save();
                        found_sw = true;
                        break;
                    }
                }
                if (!found_sw) {
                    snprintf(state->status_message, sizeof(state->status_message),
                             "No switch here - move cursor to a switch to paste chunk");
                }
            } else if (state->clipboard.type == CLIP_TILE) {
                push_undo((uint8_t)state->current_level, false);
                room->tiles[TILE_IDX(x, y)] = state->clipboard.tile;
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                editor_save();
            } else if (state->clipboard.type == CLIP_ROOM) {
                /* Replace this room with the yanked one: tiles, objects, switches,
                 * settings and name. This room's exits are kept, so its place on
                 * the map does not change. */
                Room *r = &state->rooms.rooms[state->current_level];
                uint8_t n = room->room_north, e = room->room_east, s = room->room_south, w = room->room_west;
                push_undo((uint8_t)state->current_level, false);
                room_snapshot_free(room);
                room_snapshot_copy(room, &state->clipboard.room.data);
                room->room_north = n; room->room_east = e; room->room_south = s; room->room_west = w;
                r->rest.length = 0;
                for (size_t i = 0; i < state->clipboard.room.rest.length; i++) ARRAY_ADD(r->rest, state->clipboard.room.rest.data[i]);
                state->current_switch = 0;
                state->current_chunk = 0;
                ARRAY_FREE(r->compressed);
                if (editor_save())
                    snprintf(state->status_message, sizeof(state->status_message),
                             "Room %02x pasted over this room (exits kept)", state->clipboard.room.index);
            } else {
                snprintf(state->status_message, sizeof(state->status_message), "Nothing to paste");
            }
        }; break;
        default:
            state->partial_byte = 0;
            break;
        }
        break;
    case KEY_BS: case KEY_DELETE:
        if (state->partial_byte) {
            state->partial_byte = 0;
        } else {
            yank_at_cursor();
            delete_item_at_cursor();
        }
        break;
    case KEY_CTRL:
        switch (key.ch) {
            case 'R':
                state->previous_state = state->current_state;
                state->current_state = EDIT_ROOMDETAILS;
                state->current_switch = 0;
                state->switch_on = false;
                state->current_chunk = 0;
                return true;

            case 'S':
            {
                state->previous_state = state->current_state;
                struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                size_t num_switches = room->num_switches;
                if (state->current_switch) {
                    state->current_state = EDIT_SWITCHDETAILS;
                    return true;
                }
                size_t x = state->cursors[state->current_level].x;
                size_t y = state->cursors[state->current_level].y;
                size_t sw_idx = 0;
                while (sw_idx < num_switches) {
                    struct SwitchObject *sw = room->switches + sw_idx;
                    if (sw->chunks.length && sw->chunks.data[0].x == x && sw->chunks.data[0].y == y) {
                        break;
                    }
                    sw_idx ++;
                }
                if (sw_idx == room->num_switches) {
                    /* chunk fallback: find a TOGGLE_BLOCK chunk covering cursor */
                    for (size_t i = 0; i < num_switches && sw_idx == room->num_switches; i++) {
                        struct SwitchObject *sw = room->switches + i;
                        for (size_t c = 1; c < sw->chunks.length; c++) {
                            struct SwitchChunk *chunk = sw->chunks.data + c;
                            if (chunk->type != TOGGLE_BLOCK) continue;
                            bool hit = chunk->dir == HORIZONTAL
                                ? y == (size_t)chunk->y && x >= (size_t)chunk->x && x < (size_t)(chunk->x + chunk->size)
                                : x == (size_t)chunk->x && y >= (size_t)chunk->y && y < (size_t)(chunk->y + chunk->size);
                            if (hit) { sw_idx = i; break; }
                        }
                    }
                }
                if (sw_idx == room->num_switches && room->num_switches >= 15) {
                    snprintf(state->status_message, sizeof(state->status_message), "Max 15 switches");
                    return true;
                }
                push_undo((uint8_t)state->current_level, false);
                if (sw_idx == room->num_switches) {
                    room->switches = realloc(room->switches, (sw_idx + 1) * sizeof(struct SwitchObject));
                    assert(room->switches != NULL);
                    memset(room->switches + sw_idx, 0, sizeof(struct SwitchObject));
                    room->num_switches ++;
                    struct SwitchObject *sw = room->switches + sw_idx;
                    ARRAY_ADD(sw->chunks, ((struct SwitchChunk){ .type = PREAMBLE, .x = x, .y = y }));
                }
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                editor_save();
                state->current_switch = sw_idx + 1;
                state->current_state = EDIT_SWITCHDETAILS;
                return true;
            }

            case 'O':
            {
                struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                size_t x = state->cursors[state->current_level].x;
                size_t y = state->cursors[state->current_level].y;
                // A switch under the cursor (its own marker, or any tile one of its
                // TOGGLE_BLOCK chunks covers) takes priority over object create/cycle here -
                // "switch under spidey, or chunk of any kind under spidey" (Hugh).
                if (find_switch_under_cursor(room, (int)x, (int)y) >= 0) {
                    toggle_hover_switch_preview();
                    return true;
                }
                struct RoomObject *found_obj = NULL;
                for (size_t obj_j = 0; obj_j < room->num_objects; obj_j++) {
                    struct RoomObject *obj = room->objects + obj_j;
                    if ((obj->type == BLOCK && (int)x >= obj->x && (int)x < obj->x + obj->block.width &&
                            (int)y >= obj->y && (int)y < obj->y + obj->block.height) ||
                        (obj->type == SPRITE && (int)x == obj->x && (int)y == obj->y)) {
                        found_obj = obj;
                        break;
                    }
                }
                if (found_obj == NULL) {
                    if (room->num_objects >= 7) {
                        snprintf(state->status_message, sizeof(state->status_message), "Max 7 objects");
                        return true;
                    }
                    push_undo((uint8_t)state->current_level, false);
                    room->objects = realloc(room->objects, (room->num_objects + 1) * sizeof(struct RoomObject));
                    assert(room->objects != NULL);
                    size_t idx = room->num_objects;
                    memset(room->objects + idx, 0, sizeof(struct RoomObject));
                    room->objects[idx].type = BLOCK;
                    room->objects[idx].x = x;
                    room->objects[idx].y = y;
                    room->objects[idx].block.width = 1;
                    room->objects[idx].block.height = 1;
                    room->objects[idx].tiles = malloc(1);
                    assert(room->objects[idx].tiles != NULL);
                    room->objects[idx].tiles[0] = room->tiles[TILE_IDX(x, y)];
                    room->num_objects++;
                    movertab_reset_entry((unsigned)room->UNKNOWN_c + (unsigned)idx, false);
                } else {
                    if (found_obj->type == BLOCK) {
                        for (size_t k = 0; k < room->num_objects; k++) {
                            struct RoomObject *other = room->objects + k;
                            if (other != found_obj && other->type == SPRITE &&
                                    other->x == found_obj->x && other->y == found_obj->y) {
                                snprintf(state->status_message, sizeof(state->status_message),
                                         "Sprite would overlap");
                                goto done_cycle;
                            }
                        }
                    }
                    push_undo((uint8_t)state->current_level, false);
                    unsigned found_global = (unsigned)room->UNKNOWN_c + (unsigned)(found_obj - room->objects);
                    if (found_obj->type == BLOCK) {
                        free(found_obj->tiles);
                        found_obj->tiles = NULL;
                        found_obj->type = SPRITE;
                        found_obj->sprite.type = SHARK;
                        found_obj->sprite.damage = 1;
                        movertab_reset_entry(found_global, true);
                        snprintf(state->status_message, sizeof(state->status_message),
                                 "Sprite: %s", sprite_display_name(SHARK, 1));
                    } else if (sprite_cycle_forward(found_obj)) {
                        found_obj->type = BLOCK;
                        found_obj->block.width = 1;
                        found_obj->block.height = 1;
                        found_obj->tiles = malloc(1);
                        assert(found_obj->tiles != NULL);
                        found_obj->tiles[0] = room->tiles[TILE_IDX(found_obj->x, found_obj->y)];
                        movertab_reset_entry(found_global, false);
                        snprintf(state->status_message, sizeof(state->status_message), "Block");
                    } else {
                        snprintf(state->status_message, sizeof(state->status_message),
                                 "Sprite: %s", sprite_display_name(found_obj->sprite.type, found_obj->sprite.damage));
                    }
                }
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                editor_save();
                done_cycle:
                return true;
            }

            // Enter, over an object: edit its MOVERTAB.DAT entry. A raw \r or \n both
            // decode to KEY_CTRL 'M'/'J' (see the ENTER handling elsewhere in this file) -
            // there is no separate "Ctrl-M" a terminal can send that isn't just Enter.
            case 'J':
            case 'M':
            {
                struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                size_t x = state->cursors[state->current_level].x;
                size_t y = state->cursors[state->current_level].y;
                // Lazy, not just at editor startup: a long-running hot-reloaded session never
                // re-runs the startup init that first called movertab_load(), so without this it
                // would still show an all-zero table from before this feature existed. Needed
                // before the hit-test below too, not just after it: a sprite's x/y tile offset
                // (which shifts where it's actually drawn - see room.c's doc comment above
                // move_desc()) has to be known to match the cursor against where it now sits,
                // not just its logical tile (Hugh: "when the spidey cursor also selects over the
                // sprite, it should allow edits").
                if (!state->movertab_loaded) movertab_load();
                size_t obj_idx;
                struct RoomObject *found_obj = NULL;
                for (obj_idx = 0; obj_idx < room->num_objects; obj_idx++) {
                    struct RoomObject *obj = room->objects + obj_idx;
                    // Its logical (x,y) - the definition - always counts as "on" a sprite,
                    // regardless of how far an x/y offset has since moved it away from there
                    // (Hugh: "when i go to where it is defined in the level... it is as if i
                    // were on an empty tile"). The plain offset tile counts too, in the TUI as
                    // well as the GUI - just a tile coordinate, no picture needed (Hugh: "it
                    // doesn't show up when the spidey cursor... i go to where a sprite would be
                    // drawn, and no info shows up in tui"). Only the *wider* GUI rendered box
                    // (SPRITE_X_PAD widens and shifts it even with no offset at all - see
                    // render.c) stays GUI-only, matching what a click there already can do
                    // (ged_sprite_hit in gui.c, same math) - that's a picture-pixel detail with
                    // nothing to correspond to in the TUI.
                    bool sprite_hit = obj->type == SPRITE && (int)x == obj->x && (int)y == obj->y;
                    if (obj->type == SPRITE && !sprite_hit) {
                        unsigned global = (unsigned)room->UNKNOWN_c + (unsigned)obj_idx;
                        if (global < C_ARRAY_LEN(state->movertab) / 3) {
                            const uint8_t *oe = state->movertab + global * 3;
                            int ox = obj->x + movertab_offset_x(oe);
                            int oy = obj->y + movertab_offset_y(oe, true);
                            sprite_hit = (int)x == ox && (int)y == oy;
                        }
                    }
                    if (obj->type == SPRITE && !sprite_hit && screen_headless) {
                        int dx, dy, w, h; bool fh, fv;
                        unsigned global = (unsigned)room->UNKNOWN_c + (unsigned)obj_idx;
                        render_sprite_box(obj, state->movertab, global, &dx, &dy, &w, &h, &fh, &fv);
                        int px = (int)x * 8, py = (int)y * 8;
                        sprite_hit = px < dx + w && px + 8 > dx && py < dy + h && py + 8 > dy;
                    }
                    // A block's definition rectangle always counts, same as a sprite's logical
                    // (x,y) - plus, unlike a sprite's rendered box, a block's x/y-offset
                    // rectangle is a plain, well-defined set of tiles in the TUI too (no picture
                    // needed to know where it is), so it applies regardless of screen_headless.
                    bool block_hit = obj->type == BLOCK &&
                        (int)x >= obj->x && (int)x < obj->x + obj->block.width &&
                        (int)y >= obj->y && (int)y < obj->y + obj->block.height;
                    if (obj->type == BLOCK && !block_hit) {
                        int ox, oy;
                        unsigned global = (unsigned)room->UNKNOWN_c + (unsigned)obj_idx;
                        render_block_offset(obj, state->movertab, global, &ox, &oy);
                        block_hit = (int)x >= ox && (int)x < ox + obj->block.width &&
                                    (int)y >= oy && (int)y < oy + obj->block.height;
                    }
                    if (block_hit || sprite_hit) {
                        found_obj = obj;
                        break;
                    }
                }
                if (found_obj == NULL) {
                    snprintf(state->status_message, sizeof(state->status_message),
                             "No object here to edit a MOVERTAB entry for");
                    return true;
                }
                state->previous_state = state->current_state;
                state->current_state = EDIT_MOVERTAB_OBJECT;
                state->current_object = obj_idx;
                state->movertab_raw = false;   // always open in the attribute picker, not raw hex
                state->movertab_block_focus = 0;
                return true;
            }
        }
        break;
    case KEY_MOUSE: {
        int tx = key.mouse.x / 2;
        int ty = key.mouse.y - 1;
        if (MOUSE_IS_SCROLL(key.mouse.button)) break;
        bool in_bounds = (tx >= 0 && tx < WIDTH_TILES && ty >= 0 && ty < HEIGHT_TILES);
        uint8_t btn = MOUSE_BTN(key.mouse.button);
        bool ctrl = (key.mod & MOD_CTRL) != 0;

        if (btn == 0 && !ctrl) {
            if (!key.mouse.release) {
                /* Press: move cursor to tile, start drag tracking */
                if (!in_bounds) break;
                state->cursors[state->current_level].x = tx;
                state->cursors[state->current_level].y = ty;
                state->mouse_dragging = true;
                state->mouse_drag_tile_x = (int16_t)tx;
                state->mouse_drag_tile_y = (int16_t)ty;
                state->mouse_drag_has_obj = has_draggable_at(tx, ty);
            } else {
                /* Release */
                if (!state->mouse_dragging) break;
                state->mouse_dragging = false;
                if (!in_bounds) break;
                int sx = state->mouse_drag_tile_x;
                int sy = state->mouse_drag_tile_y;
                if (tx == sx && ty == sy) {
                    /* Same tile: cursor already there, nothing more to do */
                } else {
                    move(tx - sx, ty - sy);
                }
            }
        } else if (btn == 2 || (btn == 0 && ctrl)) {
            /* Right click or Ctrl+click: move active item to clicked tile (on press) */
            if (key.mouse.release || !in_bounds) break;
            int cx = state->cursors[state->current_level].x;
            int cy = state->cursors[state->current_level].y;
            int dx = tx - cx, dy = ty - cy;
            if (dx != 0 || dy != 0) move(dx, dy);
        }
        break;
    }
    default:
        break;
    }
    return handle_normal(key);
}

static bool handle_goto_coord(Key key) {
    bool entering_x = (state->goto_coord_x < 0);
    switch (key.code) {
        case KEY_CHAR:
            if (key.mod != MOD_NONE) break;
            if ((state->debug.hex && isxdigit(key.ch)) ||
                    (!state->debug.hex && isdigit(key.ch))) {
                if (state->partial_byte) {
                    /* Second digit: compute value and advance */
                    int d = (key.ch > '9') ? tolower(key.ch) - 'a' + 10 : key.ch - '0';
                    int val = d + (state->debug.hex ? 16 : 10) * (state->partial_byte & 0xFF);
                    int limit = entering_x ? WIDTH_TILES : HEIGHT_TILES;
                    if (val < limit) {
                        if (entering_x) {
                            state->goto_coord_x = val;
                        } else {
                            /* Both coords confirmed: apply */
                            int tx = state->goto_coord_x;
                            int ty = val;
                            if (state->goto_coord_move) {
                                warp_to(tx, ty);
                            } else {
                                state->cursors[state->current_level].x = tx;
                                state->cursors[state->current_level].y = ty;
                            }
                            state->goto_coord_x = -1;
                            state->current_state = state->previous_state;
                        }
                        state->partial_byte = 0;
                    }
                    /* else: out of range, ignore */
                } else {
                    /* First digit: constrain to what can yield a valid coord */
                    char max_first = state->debug.hex ? '1'
                                   : (entering_x ? '3' : '2');
                    if (key.ch >= '0' && key.ch <= max_first) {
                        state->partial_byte = 0xFF00 | (key.ch - '0');
                    }
                }
            }
            break;
        case KEY_BS:
        case KEY_DELETE:
            if (state->partial_byte) {
                state->partial_byte = 0;
            } else if (!entering_x) {
                state->goto_coord_x = -1;
            } else {
                state->goto_coord_x = -1;
                state->current_state = state->previous_state;
            }
            break;
        case KEY_ESC:
            state->goto_coord_x = -1;
            state->partial_byte = 0;
            state->current_state = state->previous_state;
            break;
        case KEY_CTRL:
            switch (key.ch) {
                case '_': state->help = !state->help; break;
                case 'H':
                    state->debug.hex = !state->debug.hex;
                    if (state->partial_byte) {
                        if (state->debug.hex) {
                            state->partial_byte = ((state->partial_byte & 0xFF) * 10 / 16) % 16 + 1;
                        } else {
                            state->partial_byte = ((state->partial_byte & 0xFF) * 16 / 10) % 10 + 1;
                        }
                    }
                    break;
            }
            break;
        default: break;
    }
    return true;
}

// Every bit of a sprite's MOVERTAB byte 2 the attribute picker below assigns a meaning to,
// per movertab.txt (Hugh's own notes: facing 0x02, upside-down 0x04, moving 0x08, wall-turn
// 0x10, edge-turn 0x20, chase-Spider-Man 0x40) - i.e. everything except bit 0x01 (glitch) and
// bit 0x80 (handled separately: 0x00 exactly is "dead", not part of this mask at all).
#define MOVERTAB_ATTR_MASK 0x7e

// 0x08 is not simply "patrol mode" on its own - live testing (2026-09-18) found 0xc2 (0x40 set,
// 0x08 clear) is a chaser standing STOPPED, not patrolling, while 0xf8 (0x40 and 0x08 both set)
// is one actively chasing. So once 0x40 (chase) is set, 0x08 becomes chase's own "currently
// moving" sub-flag rather than the alternative-to-chase "patrol a fixed direction" mode it is
// when 0x40 is clear. Also noted: a chaser only stops once it reaches Spider-Man if it's facing
// the same direction he's on - a gameplay nuance, not something this picker exposes.

// Bit manipulation for a sprite's behaviour byte, shared between MOVERTAB byte 2
// (handle_edit_movertab_object) and a TOGGLE_OBJECT chunk's value byte when it targets a sprite
// (handle_edit_switchdetails_chunk_object) - same scheme, same "consistent language and
// editing" reasoning as the block functions just below. Callers still do their own dead/mode
// guard messages and (MOVERTAB only) the offset re-clamp after a flip; this is just the byte
// math.
static void sprite_toggle_dead(uint8_t *v) {
    // Dead is exactly 0x00, nothing else - remembering the other bits and restoring them on
    // revive let a live-tested-glitchy combination survive a round trip through dead (Hugh:
    // "it goes glitchy on other bits set").
    *v = (*v == 0x00) ? 0x80 : 0x00;
}
// Ctrl-O's sprite-type cycle. Shark carries two extra named stops via its damage byte
// (room.h: dmg 3 = Mysterio, dmg 4 = Mary Jane, else a plain shark) - Hugh wanted these
// directly selectable rather than an incidental damage value on an ordinary shark.
// Returns true if the cycle ran off the end (Shotgun Lady) - the caller turns the object
// back into a BLOCK in that case, exactly as it already did for the non-shark types.
static bool sprite_cycle_forward(struct RoomObject *obj) {
    if (obj->sprite.type == SHARK && obj->sprite.damage < 3) {
        obj->sprite.damage = 3;   // Mysterio
        return false;
    }
    if (obj->sprite.type == SHARK && obj->sprite.damage == 3) {
        obj->sprite.damage = 4;   // Mary Jane
        return false;
    }
    if ((int)obj->sprite.type + 1 >= NUM_SPRITE_TYPES) return true;
    obj->sprite.type = (SpriteType)(obj->sprite.type + 1);
    obj->sprite.damage = 1;
    return false;
}
static const char *sprite_display_name(SpriteType type, uint8_t damage) {
    static const char *names[] = {
        "Shark", "Mummy", "Blue man", "Wolf",
        "R2D2", "Dinosaur", "Rat", "Shotgun lady",
    };
    if (type == SHARK) {
        if (damage == 3) return "Mysterio";
        if (damage == 4) return "Mary Jane";
    }
    return names[type];
}
static void sprite_normalize(uint8_t *v) {
    // Drop bit 0x01 and anything outside this picker's model, force the 0x80 base: a value raw
    // mode left behind - or one an older editor version wrote - must not silently keep a glitch
    // bit alive just because attribute mode only ever *toggles* relative to what was already
    // there.
    *v = (*v & MOVERTAB_ATTR_MASK) | 0x80;
}
static void sprite_cycle_flip(uint8_t *v, int dir) {
    sprite_normalize(v);
    // One cycle through both flip bits together (facing 0x02, upside-down 0x04) instead of two
    // separate toggle keys: none -> horizontal -> vertical -> both -> none (dir -1: backwards).
    int step = ((*v & 0x06) / 2 + dir) % 4;
    if (step < 0) step += 4;
    uint8_t flip = (uint8_t)(step * 2);
    *v = (*v & ~(uint8_t)0x06) | flip;
}
static void sprite_toggle_stopped(uint8_t *v) { sprite_normalize(v); *v ^= 0x08; }
static void sprite_toggle_wall(uint8_t *v)    { sprite_normalize(v); *v ^= 0x10; }
static void sprite_toggle_edge(uint8_t *v)    { sprite_normalize(v); *v ^= 0x20; }
static void sprite_cycle_mode(uint8_t *v, int dir) {
    sprite_normalize(v);
    // 3 movement modes: stopped (neither bit), patrol (0x08 alone, per movertab.txt's
    // 0x8X/0x9X/0xaX/0xbX families), chase (0x40 set, the 0xcX-0xfX families) - checked
    // chase-first because 0x40 and 0x08 can coexist (see the comment above): within chase, 0x08
    // is its own "currently moving, not stopped" sub-flag (sprite_toggle_stopped), not an
    // alternative mode.
    int mode = (*v & 0x40) ? 2 : (*v & 0x08) ? 1 : 0;
    mode = ((mode + dir) % 3 + 3) % 3;
    *v &= ~(uint8_t)0x48;
    if (mode == 0) *v &= ~(uint8_t)0x30;   // stopped: wall/edge modifiers are meaningless too
    else if (mode == 1) *v |= 0x08;
    else *v |= 0x48;   // chase defaults to actively moving, not stopped - sprite_toggle_stopped can stop it
}

// A block's MOVERTAB byte 2, per movertab.txt's "heli" table (Hugh's own notes, confirmed
// against the whole 0x00-0xf0 range): the low nibble is horizontal, the high nibble vertical,
// each independently 0 (stationary) or [speed 1-3][direction bit][bounce-at-wall bit] - e.g.
// 0x09-0x0b is "goes right, bounces off walls" at speed 1-3 (bit 0x08 set), vs 0x01-0x03 "moves
// right, stops at wall" (bit 0x08 clear) at the same speeds; 0x10-0x30 down/stop, 0x90-0xb0
// down/bounce, mirrored for up with bits 0x40 (direction) and 0x80 (bounce) in the high nibble.
// Both nibbles are independent, so e.g. 0x11 (right + down together) should work too, though
// that specific combination isn't itself in movertab.txt's table. No bit here is ever "always
// glitchy" the way sprite byte 2's 0x01 is - every value 0x00-0xff decodes to *some* meaning.
// 's' focus: a directional ratchet, not a plain +-1 (Hugh: "when pressing down on 0 should
// change dir to v and then speed to 1... if speed > 0 and dir is v then speed++, otherwise
// speed--... pressing up should do the opposite; vice versa for the other axis"). At speed 0,
// the key pressed sets the direction and jumps to speed 1; pressing the same direction again
// increases speed (capped at 3); pressing the opposite direction decreases it (down to 0, where
// the next same-or-opposite press starts the cycle over). `shift`/`dir_bit` locate that axis's
// 2-bit speed field and direction bit within byte 2 (horizontal: bits 0-1, 0x04; vertical: bits
// 4-5, 0x40 - see the "heli" table comment above handle_edit_movertab_object). `pressed_dir` is
// which way of that axis was pressed, as that axis's direction bit would represent it.
static void block_speed_ratchet(uint8_t *entry, int shift, uint8_t dir_bit, bool pressed_dir) {
    int speed = (*entry >> shift) & 0x03;
    if (speed == 0) {
        *entry = pressed_dir ? (*entry | dir_bit) : (uint8_t)(*entry & ~dir_bit);
        speed = 1;
    } else if (((*entry & dir_bit) != 0) == pressed_dir) {
        speed = speed < 3 ? speed + 1 : 3;
    } else {
        speed--;
    }
    *entry = (uint8_t)((*entry & ~(0x03 << shift)) | (speed << shift));
}
// 'd' focus: hjkl/arrows set that axis's direction outright (Hugh: "swapping direction for dir"),
// independent of its current speed.
static void block_direction_set(uint8_t *entry, uint8_t dir_bit, bool set) {
    *entry = set ? (*entry | dir_bit) : (uint8_t)(*entry & ~dir_bit);
}

// MOVERTAB bytes 0/1 (y/x offset in whole tiles - see room.c's doc comment above move_desc()) as
// signed ints, clamped well clear of the only known-bad spots rather than to a proven safe
// boundary (Hugh: "just limit those two bytes to where it makes sense" rather than chase the
// exact glitch edges by hand). X (byte 1) and a block's Y (byte 0) are ordinary two's complement;
// a sprite's Y (byte 0) is signed in the low 7 bits only, with bit 0x80 untested and left alone.
#define MOVERTAB_OFFSET_STD_LIMIT 100
#define MOVERTAB_OFFSET_SPRITE_Y_LIMIT 32
static int movertab_offset_get_std(uint8_t b) { return (int8_t)b; }
static uint8_t movertab_offset_set_std(int v) {
    if (v < -MOVERTAB_OFFSET_STD_LIMIT) v = -MOVERTAB_OFFSET_STD_LIMIT;
    if (v > MOVERTAB_OFFSET_STD_LIMIT) v = MOVERTAB_OFFSET_STD_LIMIT;
    return (uint8_t)(int8_t)v;
}
static int movertab_offset_get_sprite_y(uint8_t b) {
    int v = b & 0x7f;
    if (v & 0x40) v -= 0x80;
    return v;
}
static uint8_t movertab_offset_set_sprite_y(uint8_t b, int v) {
    if (v < -MOVERTAB_OFFSET_SPRITE_Y_LIMIT) v = -MOVERTAB_OFFSET_SPRITE_Y_LIMIT;
    if (v > MOVERTAB_OFFSET_SPRITE_Y_LIMIT) v = MOVERTAB_OFFSET_SPRITE_Y_LIMIT;
    return (uint8_t)((b & 0x80) | ((uint8_t)v & 0x7f));
}
// Reset a newly-created or type-cycled object's MOVERTAB entry to each type's baseline
// (Hugh, 2026-09-19): a block gets 0,0 offset and byte 2 = 0x88 (stationary, confirmed in
// room.c's byte-2 survey); a sprite gets -1,-1 offset and byte 2 = 0xb8 (patrolling, the
// original game's own default for a live sprite). Applies on Ctrl-O create-new-block,
// block->sprite, and sprite(last type)->block - not the plain sprite-type cycle in between.
static void movertab_reset_entry(unsigned global, bool is_sprite) {
    if (!state->movertab_loaded) movertab_load();
    if (global >= C_ARRAY_LEN(state->movertab) / 3) return;
    uint8_t *entry = state->movertab + global * 3;
    entry[1] = movertab_offset_set_std(is_sprite ? -1 : 0);
    entry[0] = is_sprite ? movertab_offset_set_sprite_y(0, -1) : movertab_offset_set_std(0);
    entry[2] = is_sprite ? 0xb8 : 0x88;
    movertab_save();
}
int movertab_offset_x(const uint8_t *entry) { return movertab_offset_get_std(entry[1]); }
int movertab_offset_y(const uint8_t *entry, bool is_sprite) {
    return is_sprite ? movertab_offset_get_sprite_y(entry[0]) : movertab_offset_get_std(entry[0]);
}
// However far `off` already is from `base`, pull it back in if base+off would land outside the
// room (0..room_tiles-1) - the offset can push a sprite out of its logical tile, but not off the
// map (Hugh: "that offset should not allow going out of bounds"). One tile of leeway past the
// low edge (Hugh: "we are allowed -1 more" for y, then "i still can't set offset to make target
// x = -1... gets clamped too early" - x gets the same -1 leeway as y after all). `extra` shifts
// both bounds the same way when flip mirrors the axis being clamped (tightens the low/left or
// top side, loosens the high/right or bottom side - Hugh confirmed the direction, then measured
// the exact magnitude live per axis: see movertab_clamp_offset).
static int movertab_offset_in_room(int base, int off, int room_tiles, int extra) {
    int lo = -1 + extra, hi = room_tiles - 2 + extra;
    if (base + off < lo) off = lo - base;
    if (base + off > hi) off = hi - base;
    return off;
}
// Re-clamp an object's stored offset to its current (possibly just-changed) tile position,
// without otherwise altering it - call this whenever obj->x/obj->y change (see move() in
// editor_actions.c; Hugh: "if a sprite is moved, it should check its offset and clamp to wall").
void movertab_clamp_offset(uint8_t *entry, bool is_sprite, int x, int y) {
    int extra_x = (is_sprite && (entry[2] & 0x02)) ? 2 : 0;   // flip: < (horizontal)
    int extra_y = (is_sprite && (entry[2] & 0x04)) ? 1 : 0;   // flip: ^ (vertical) - same shift, smaller magnitude (Hugh measured 3 off with -2: "3 more than i should be clamped at [top], then 3 short... at bottom" - both bounds needed +3 from that, landing on +1 overall, not -2)
    entry[1] = movertab_offset_set_std(movertab_offset_in_room(x, movertab_offset_get_std(entry[1]), WIDTH_TILES, extra_x));
    int y_off = is_sprite ? movertab_offset_get_sprite_y(entry[0]) : movertab_offset_get_std(entry[0]);
    y_off = movertab_offset_in_room(y, y_off, HEIGHT_TILES, extra_y);
    entry[0] = is_sprite ? movertab_offset_set_sprite_y(entry[0], y_off) : movertab_offset_set_std(y_off);
}
static void movertab_offset_adjust(uint8_t *entry, bool is_sprite, int x, int y, int dx, int dy) {
    if (dx) entry[1] = movertab_offset_set_std(movertab_offset_get_std(entry[1]) + dx);
    if (dy) entry[0] = is_sprite
        ? movertab_offset_set_sprite_y(entry[0], movertab_offset_get_sprite_y(entry[0]) + dy)
        : movertab_offset_set_std(movertab_offset_get_std(entry[0]) + dy);
    movertab_clamp_offset(entry, is_sprite, x, y);
}
// hjkl/arrows: adjust the x/y tile offset by default; for a block with 's' or 'd' focus armed,
// adjust the movement byte instead (up/right = +1 speed or set up/right; down/left = -1 speed or
// set down/left - see block_speed_adjust/block_direction_set above).
static void movertab_dir_key(uint8_t *entry, bool is_sprite, int x, int y, int dx, int dy) {
    if (!is_sprite && (state->movertab_block_focus == 's' || state->movertab_block_focus == 'd')) {
        bool speed = state->movertab_block_focus == 's';
        if (dx) {
            if (speed) block_speed_ratchet(entry + 2, 0, 0x04, dx < 0);   // left = bit set
            else block_direction_set(entry + 2, 0x04, dx < 0);
        }
        if (dy) {
            if (speed) block_speed_ratchet(entry + 2, 4, 0x40, dy < 0);   // up = bit set
            else block_direction_set(entry + 2, 0x40, dy < 0);
        }
        return;
    }
    if (!is_sprite && state->movertab_block_focus == 'w') {
        // Either direction of an axis toggles that axis's wall bit here - up and down both mean
        // "|", left and right both mean "-" (Hugh: "allow arrows to toggle their axis... as if
        // they were both | in this context").
        if (dx) entry[2] ^= 0x08;
        if (dy) entry[2] ^= 0x80;
        return;
    }
    movertab_offset_adjust(entry, is_sprite, x, y, dx, dy);
}

// Enter over an object (handle_tile_edit) enters this to edit that object's MOVERTAB.DAT
// entry. Opens in the attribute picker, not raw hex entry: every byte-2 value it can produce
// there is one that's been confirmed to actually work (see room.c's movertab_behavior_desc/
// MOVERTAB_KNOWN_BEHAVIORS comment) - picking attributes instead of typing hex makes it
// impossible to land on a glitchy value by accident.
//   sprite: d toggles dead/alive; f cycles flip (none -> horizontal -> vertical -> both); m/M
//   cycle movement mode (stopped -> patrol -> chase -> stopped); s toggles chase's own
//   currently-stopped/moving sub-flag (chase mode only); w/e toggle wall-turning/edge-turning
//   (patrol or chase only) - all while alive only. See MOVERTAB_ATTR_MASK and the 0x08/0x40
//   comment just above for where each bit comes from.
//   block: two independent axes, per movertab.txt's "heli" table (see block_axis_cycle's
//   comment) - f cycles horizontal stopped/right/left + speed 1-3, w toggles its stop-at-wall/
//   bounce bit (only while moving horizontally); u/e do the same for vertical (down/up); d/s/m/M
//   don't apply to a block and are a no-op with a message.
// hjkl/arrows nudge bytes 1/0 (x/y tile offset - see room.c's doc comment above move_desc() and
// movertab_offset_adjust above) by 1, for sprite and block alike; the same for both entity types.
// r toggles into raw hex entry of byte 2 for a combination the attribute picker can't reach yet.
// No validation once every bit is understood this could refuse a save on a value known to be
// bad, but that day isn't here yet.
static bool handle_edit_movertab_object(Key key) {
    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
    if (state->current_object >= room->num_objects) {
        state->current_state = state->previous_state;
        state->previous_state = NORMAL;
        return true;
    }
    struct RoomObject *obj = room->objects + state->current_object;
    bool is_sprite = obj->type == SPRITE;
    unsigned global = (unsigned)room->UNKNOWN_c + (unsigned)state->current_object;
    if (global >= C_ARRAY_LEN(state->movertab) / 3) global = C_ARRAY_LEN(state->movertab) / 3 - 1;
    uint8_t *entry = state->movertab + global * 3;
#define SAVE_MOVERTAB() snprintf(state->status_message, sizeof(state->status_message), \
                                 movertab_save() ? "Saved MOVERTAB.DAT" : "Could not write MOVERTAB.DAT")

    if (state->movertab_raw) {
        // Just byte 2 now - bytes 0/1 (the x/y offsets) have their own hjkl/arrow controls
        // below instead of raw hex entry, so there's no second or third byte to navigate to.
        switch (key.code) {
            case KEY_CHAR:
                if (isxdigit(key.ch)) {
                    int digit = key.ch > '9' ? tolower(key.ch) - 'a' + 10 : key.ch - '0';
                    if (state->partial_byte & 0xFF00) {
                        entry[2] = (uint8_t)(((state->partial_byte & 0xFF) << 4) | digit);
                        state->partial_byte = 0;
                        SAVE_MOVERTAB();
                    } else {
                        state->partial_byte = 0xFF00 | (unsigned)digit;
                    }
                } else switch (key.ch) {
                    case 'r':
                        state->movertab_raw = false;
                        state->partial_byte = 0;
                        break;
                    case 'q':
                        if (state->help) { state->help = false; }
                        else { state->current_state = state->previous_state; state->previous_state = NORMAL; state->partial_byte = 0; }
                        break;
                    case '?': state->help = !state->help; break;
                    default: break;
                }
                break;
            case KEY_BS:
            case KEY_DELETE:
                state->partial_byte = 0;
                break;
            case KEY_ESC:
                if (state->help) { state->help = false; }
                else { state->current_state = state->previous_state; state->previous_state = NORMAL; state->partial_byte = 0; }
                break;
            case KEY_CTRL:
                switch (key.ch) {
                    case '_': state->help = !state->help; break;
                    case 'H': state->debug.hex = !state->debug.hex; break;
                    case 'P': fork_play(); break;
                }
                break;
            default: break;
        }
        return true;
    }

    switch (key.code) {
        case KEY_CHAR:
            switch (key.ch) {
                case 'r':
                    state->movertab_raw = true;
                    break;
                case 'd':
                    if (!is_sprite) {
                        state->movertab_block_focus = (state->movertab_block_focus == 'd') ? 0 : 'd';
                    } else {
                        sprite_toggle_dead(&entry[2]);
                        SAVE_MOVERTAB();
                    }
                    break;
                case 'f': case 'F':
                    if (!is_sprite) {
                        snprintf(state->status_message, sizeof(state->status_message), "Blocks: s/d/w for speed/direction/walls, then arrows or |/-");
                    } else if (entry[2] == 0x00) {
                        snprintf(state->status_message, sizeof(state->status_message), "Dead - press d to make it alive first");
                    } else {
                        sprite_cycle_flip(&entry[2], key.ch == 'f' ? 1 : -1);
                        // The flip bits change which side of the room the offset can safely
                        // reach (see movertab_clamp_offset) - an offset that was fine before this
                        // flip might not be any more, so re-check it now rather than only on the
                        // next hjkl/arrow press (Hugh: "when you do a flip action, it should also
                        // ensure any clamp necessary").
                        movertab_clamp_offset(entry, is_sprite, obj->x, obj->y);
                        SAVE_MOVERTAB();
                    }
                    break;
                case 's':
                    if (!is_sprite) {
                        state->movertab_block_focus = (state->movertab_block_focus == 's') ? 0 : 's';
                    } else if (entry[2] == 0x00) {
                        snprintf(state->status_message, sizeof(state->status_message), "Dead - press d to make it alive first");
                    } else if (!(entry[2] & 0x40)) {
                        snprintf(state->status_message, sizeof(state->status_message), "Only a chasing enemy can be stopped - press m to chase first");
                    } else {
                        sprite_toggle_stopped(&entry[2]);
                        SAVE_MOVERTAB();
                    }
                    break;
                case 'm':
                case 'M':
                    if (!is_sprite) {
                        snprintf(state->status_message, sizeof(state->status_message), "Blocks: f/w for horizontal, u/e for vertical");
                    } else if (entry[2] == 0x00) {
                        snprintf(state->status_message, sizeof(state->status_message), "Dead - press d to make it alive first");
                    } else {
                        sprite_cycle_mode(&entry[2], key.ch == 'm' ? 1 : -1);
                        SAVE_MOVERTAB();
                    }
                    break;
                case 'w':
                    if (!is_sprite) {
                        // Only one axis moving means only one wall option exists - toggle it
                        // directly rather than making the user disambiguate with |/- (Hugh: "if
                        // there is only one wall option, w should just toggle it... but if two,
                        // then | -"). Both, or neither, moving: arm focus as before.
                        bool v_moving = ((entry[2] >> 4) & 0x03) != 0, h_moving = (entry[2] & 0x03) != 0;
                        if (v_moving && !h_moving) { entry[2] ^= 0x80; SAVE_MOVERTAB(); }
                        else if (h_moving && !v_moving) { entry[2] ^= 0x08; SAVE_MOVERTAB(); }
                        else state->movertab_block_focus = (state->movertab_block_focus == 'w') ? 0 : 'w';
                    } else if (entry[2] == 0x00) {
                        snprintf(state->status_message, sizeof(state->status_message), "Dead - press d to make it alive first");
                    } else if (!(entry[2] & 0x48)) {
                        snprintf(state->status_message, sizeof(state->status_message), "Not moving - press m first");
                    } else {
                        sprite_toggle_wall(&entry[2]);
                        SAVE_MOVERTAB();
                    }
                    break;
                case 'u':
                    if (!is_sprite) {
                        snprintf(state->status_message, sizeof(state->status_message), "Blocks: s/d/w for speed/direction/walls, then arrows or |/-");
                    } else {
                        snprintf(state->status_message, sizeof(state->status_message), "Sprites don't have a vertical movement axis");
                    }
                    break;
                case '|':
                case '-':
                    if (!is_sprite && state->movertab_block_focus) {
                        // | acts on the vertical axis, - the horizontal - same convention as
                        // "gravity (|)"/"gravity (-)" in EDIT_ROOMDETAILS. In s/d focus this
                        // negates that axis's direction (Hugh: "s also does speed = -speed" -
                        // flips the sign, keeps the magnitude); in w focus it toggles that
                        // axis's stop-at-wall/bounce-back bit.
                        uint8_t dir_bit = key.ch == '|' ? 0x40 : 0x04;
                        uint8_t wall_bit = key.ch == '|' ? 0x80 : 0x08;
                        if (state->movertab_block_focus == 'w') entry[2] ^= wall_bit;
                        else entry[2] ^= dir_bit;
                        SAVE_MOVERTAB();
                    }
                    break;
                case 'e':
                    if (!is_sprite) {
                        snprintf(state->status_message, sizeof(state->status_message), "Blocks: s/d/w for speed/direction/walls, then arrows or |/-");
                    } else if (entry[2] == 0x00) {
                        snprintf(state->status_message, sizeof(state->status_message), "Dead - press d to make it alive first");
                    } else if (!(entry[2] & 0x48)) {
                        snprintf(state->status_message, sizeof(state->status_message), "Not moving - press m first");
                    } else {
                        sprite_toggle_edge(&entry[2]);
                        SAVE_MOVERTAB();
                    }
                    break;
                case 'g': case 'G':
                    if (!is_sprite) {
                        snprintf(state->status_message, sizeof(state->status_message), "Blocks don't have damage");
                    } else if (obj->sprite.type == SHARK && obj->sprite.damage >= 3) {
                        // Mysterio/Mary Jane: 'g' is a no-op here, not a way back to a plain
                        // shark by accident - that identity only changes via Ctrl-O's cycle.
                        snprintf(state->status_message, sizeof(state->status_message),
                                 "%s - change via Ctrl-O's sprite cycle instead",
                                 obj->sprite.damage == 3 ? "Mysterio" : "Mary Jane");
                    } else {
                        push_undo((uint8_t)state->current_level, false);
                        // Shark alone stays within 1-2: 3/4 are the Mysterio/Mary Jane identities,
                        // selected via Ctrl-O's type cycle, not something 'g'/'G' should wander
                        // into by accident (Hugh: "for shark (and only shark) only cycle 1 and 2").
                        if (obj->sprite.type == SHARK) {
                            obj->sprite.damage = (obj->sprite.damage == 1) ? 2 : 1;
                        } else if (key.ch == 'g') {
                            obj->sprite.damage = (uint8_t)((obj->sprite.damage % 4) + 1);
                        } else {
                            obj->sprite.damage = (uint8_t)(obj->sprite.damage == 1 ? 4 : obj->sprite.damage - 1);
                        }
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        editor_save();
                        snprintf(state->status_message, sizeof(state->status_message), "Damage: %d", obj->sprite.damage);
                    }
                    break;
                case 'q':
                    if (state->help) {
                        state->help = false;
                    } else {
                        state->current_state = state->previous_state;
                        state->previous_state = NORMAL;
                    }
                    break;
                case '?': state->help = !state->help; break;
                case 'h': movertab_dir_key(entry, is_sprite, obj->x, obj->y, -1, 0); SAVE_MOVERTAB(); break;
                case 'l': movertab_dir_key(entry, is_sprite, obj->x, obj->y, 1, 0); SAVE_MOVERTAB(); break;
                case 'k': movertab_dir_key(entry, is_sprite, obj->x, obj->y, 0, -1); SAVE_MOVERTAB(); break;
                case 'j': movertab_dir_key(entry, is_sprite, obj->x, obj->y, 0, 1); SAVE_MOVERTAB(); break;
                default: break;
            }
            break;
        case KEY_LEFT:  movertab_dir_key(entry, is_sprite, obj->x, obj->y, -1, 0); SAVE_MOVERTAB(); break;
        case KEY_RIGHT: movertab_dir_key(entry, is_sprite, obj->x, obj->y, 1, 0); SAVE_MOVERTAB(); break;
        case KEY_UP:    movertab_dir_key(entry, is_sprite, obj->x, obj->y, 0, -1); SAVE_MOVERTAB(); break;
        case KEY_DOWN:  movertab_dir_key(entry, is_sprite, obj->x, obj->y, 0, 1); SAVE_MOVERTAB(); break;
        // Layered: close the help overlay if it's up; else drop a block's s/d/w focus back to
        // the plain attribute view if one's armed; else leave the picker entirely, back to view
        // only (Hugh: "enter goes back to view only not edit... enter in [an] s/etc mode... as
        // if enter was escape in these contexts" - so Enter (Ctrl-J/Ctrl-M) mirrors Escape here).
        case KEY_ESC:
            if (state->help) state->help = false;
            else if (state->movertab_block_focus) state->movertab_block_focus = 0;
            else { state->current_state = state->previous_state; state->previous_state = NORMAL; }
            break;
        case KEY_CTRL:
            switch (key.ch) {
                case '_': state->help = !state->help; break;
                case 'H': state->debug.hex = !state->debug.hex; break;
                case 'P': fork_play(); break;
                case 'J':
                case 'M':
                    if (state->help) state->help = false;
                    else if (state->movertab_block_focus) state->movertab_block_focus = 0;
                    else { state->current_state = state->previous_state; state->previous_state = NORMAL; }
                    break;
            }
            break;
        default: break;
    }
#undef SAVE_MOVERTAB
    return true;
}

static state_handler_fn state_handlers[NUM_STATES] = {
    [GOTO_OBJECT]      = handle_goto_object,
    [GOTO_SWITCH]      = handle_goto_switch,
    [GOTO_ROOM]        = handle_goto_room,
    [GOTO_COORD]       = handle_goto_coord,
    [EDIT_ROOMNAME]    = handle_edit_roomname,
    [TOGGLE_DISPLAY]   = handle_toggle_display,
    [EDIT_ROOMDETAILS]                             = handle_edit_roomdetails,
    [EDIT_ROOMDETAILS_NUM]                         = handle_edit_roomdetails_num,
    [EDIT_ROOMDETAILS_ROOM]                        = handle_edit_roomdetails_room,
    [EDIT_SWITCHDETAILS]                           = handle_edit_switchdetails,
    [EDIT_SWITCHDETAILS_XY]                        = handle_edit_switchdetails_xy,
    [EDIT_SWITCHDETAILS_SELECT_CHUNK]              = handle_edit_switchdetails_select_chunk,
    [EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS]      = handle_edit_switchdetails_chunk_switch,
    [EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS_ROOM] = handle_edit_switchdetails_chunk_switch_room,
    [EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS]      = handle_edit_switchdetails_chunk_memory,
    [EDIT_SWITCHDETAILS_CHUNK_BLOCK_DETAILS]       = handle_edit_switchdetails_chunk_block,
    [EDIT_SWITCHDETAILS_CHUNK_OBJECT_DETAILS]      = handle_edit_switchdetails_chunk_object,
    [EDIT_MOVERTAB_OBJECT]                         = handle_edit_movertab_object,
    [TILE_EDIT]        = handle_tile_edit,
    [NORMAL]           = handle_normal,
};

/* --- clicks on the text below the map ---------------------------------------
 * The info rows are parsed back from the screen the editor just drew, so a
 * click on "switch 03: ..." opens that switch, on a "(02) type block" chunk
 * line selects that chunk, on "sprite 1:" goes to that object, and on a room
 * field ("dmg: 01", "left: 00 - ...") edits that field. Works the same from a
 * terminal mouse and from the GUI. */

static bool plain_state(void) { return state->current_state == NORMAL || state->current_state == TILE_EDIT; }

static bool room_list_state(void) {
    return state->current_state == GOTO_ROOM || state->current_state == EDIT_ROOMDETAILS_ROOM ||
           state->current_state == EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS_ROOM;
}

static void feed(Key k) { state_handlers[state->current_state](k); }
static void feed_char(char c) { feed((Key){ .code = KEY_CHAR, .ch = c }); }
static void feed_ctrl(char c) { feed((Key){ .code = KEY_CTRL, .ch = c }); }
static char hex_digit(int v) { return (char)(v < 10 ? '0' + v : 'a' + v - 10); }

/* Row of the last drawn screen as text, trailing blanks trimmed. */
static void screen_row_text(int row, char *out, size_t n) {
    size_t len = 0;
    if (row < 0 || row >= SCREEN_MAX_HEIGHT) { out[0] = '\0'; return; }
    for (int x = 0; x < SCREEN_MAX_WIDTH && len + 1 < n; x++) {
        char c = state->screen_prev[row * SCREEN_MAX_WIDTH + x].ch;
        out[len++] = c ? c : ' ';
    }
    out[len] = '\0';
    while (len && out[len - 1] == ' ') out[--len] = '\0';
}

/* Number as PRINTF_DATA printed it (hex when debug.hex). Chars consumed, 0 if none. */
static int parse_num(const char *s, int *val) {
    char *end;
    long v = strtol(s, &end, state->debug.hex ? 16 : 10);
    if (end == s) return 0;
    *val = (int)v;
    return (int)(end - s);
}

/* Press Escape until back in NORMAL/TILE_EDIT (closing help first). */
void editor_cancel_to_plain(void) {
    if (state->help) state->help = false;
    for (int i = 0; i < 8 && !plain_state(); i++) feed((Key){ .code = KEY_ESC });
    state->partial_byte = 0;
}

/* Type a room number the way the current room-choosing state expects it. */
void editor_feed_room_number(int room) {
    char buf[8];
    snprintf(buf, sizeof buf, state->debug.hex ? "%02x" : "%02d", room);
    feed_char(buf[0]);
    feed_char(buf[1]);
}

/* `s` + id, then Ctrl-S if the switch was in bounds (goto-switch opens
 * out-of-bounds ones itself). */
static void open_switch(int id) {
    if (id < 0 || id > 15) return;
    editor_cancel_to_plain();
    feed_char('s');
    feed_char(hex_digit(id));
    if (plain_state()) feed_ctrl('S');
}

static const struct { const char *label; char key; } room_fields[] = {
    {"bkgrnd", 'g'}, {"tiles", 't'}, {"dmg", 'd'}, {"gravity (|)", '|'}, {"gravity (-)", '-'},
    {"left", 'h'}, {"down", 'j'}, {"up", 'k'}, {"right", 'l'},
};

static bool panel_click(int row, int col) {
    char text[SCREEN_MAX_WIDTH + 1];
    screen_row_text(row, text, sizeof text);
    const char *t = text;
    while (*t == ' ') t++;
    int v;

    if (strncmp(t, "switch id ", 10) == 0 || strncmp(t, "switch ", 7) == 0) {
        if (!parse_num(t + (strncmp(t, "switch id ", 10) == 0 ? 10 : 7), &v)) return false;
        open_switch(v);
        /* a click on the (entry)=, (once)= or (side)= token flips that field */
        if (state->current_state == EDIT_SWITCHDETAILS) {
            static const char *labels[] = { "(entry)=", "(once)=", "(side)=" };
            for (int i = 0; i < 3; i++) {
                const char *hit = strstr(text, labels[i]);
                if (hit == NULL) continue;
                int pos = (int)(hit - text), end = pos + (int)strlen(labels[i]);
                while (text[end] && text[end] != ' ') end++;            /* ... through its value */
                if (col < pos || col >= end) continue;
                const char *val = hit + strlen(labels[i]);
                if (i == 0) feed_char('e');
                else if (i == 1) feed_char('o');
                else feed_char(strncmp(val, "TOP", 3) == 0 ? 'l' : strncmp(val, "RIGHT", 5) == 0 ? 'j'   /* clockwise */
                               : strncmp(val, "BOTTOM", 6) == 0 ? 'h' : 'k');
                break;
            }
        }
        return true;
    }
    if (t[0] == '(' && parse_num(t + 1, &v) && strstr(t, ") type") ) {   /* chunk line: find its switch header above */
        for (int r = row - 1; r > HEIGHT_TILES; r--) {
            char h[SCREEN_MAX_WIDTH + 1], *hp;
            screen_row_text(r, h, sizeof h);
            hp = h;
            while (*hp == ' ') hp++;
            int id;
            if (strncmp(hp, "switch ", 7) == 0 && parse_num(hp + (strncmp(hp, "switch id ", 10) == 0 ? 10 : 7), &id)) {
                open_switch(id);
                if (state->current_state == EDIT_SWITCHDETAILS && v >= 0 && v <= 15) { feed_char('c'); feed_char(hex_digit(v)); }
                return true;
            }
        }
        return false;
    }
    if (strncmp(t, "sprite ", 7) == 0 || strncmp(t, "block ", 6) == 0) {
        if (!parse_num(t + (t[0] == 's' ? 7 : 6), &v) || v < 0 || v > 15) return false;
        editor_cancel_to_plain();
        feed_char('o');
        feed_char(hex_digit(v));
        return true;
    }
    /* room fields: the field whose label starts furthest left of the click column */
    int best = -1; char key = 0;
    for (size_t i = 0; i < C_ARRAY_LEN(room_fields); i++) {
        const char *hit = text;
        while ((hit = strcasestr(hit, room_fields[i].label)) != NULL) {
            int pos = (int)(hit - text);
            bool word_start = pos == 0 || text[pos - 1] == ' ' || text[pos - 1] == ',';
            if (word_start && pos <= col && pos > best) { best = pos; key = room_fields[i].key; }
            hit++;
        }
    }
    if (key) {
        editor_cancel_to_plain();
        if (state->current_state == NORMAL) feed_ctrl('T');   /* Ctrl-R lives in tile-edit mode */
        feed_ctrl('R');
        if (state->current_state == EDIT_ROOMDETAILS) feed_char(key);
        return true;
    }
    return false;
}

/* Click on the room list (goto room / link editing): pick that room. */
static bool room_list_click(int row, int col) {
    char text[SCREEN_MAX_WIDTH + 1];
    screen_row_text(row, text, sizeof text);
    int half = col >= WIDTH_TILES ? WIDTH_TILES : 0, v;
    if ((int)strlen(text) <= half) return false;
    if (!parse_num(text + half, &v)) return false;
    editor_feed_room_number(v);
    return true;
}

/* Pop one queued macro-replay key and run it through the current state's
 * handler. Returns false when the replay queue is empty. */
bool editor_replay_step(void) {
    if (state->macro_replay.length == 0) return false;
    Key k = state->macro_replay.data[0];
    memmove(state->macro_replay.data,
            state->macro_replay.data + 1,
            (state->macro_replay.length - 1) * sizeof(Key));
    state->macro_replay.length--;
    assert(state->current_state < NUM_STATES);
    assert(state_handlers[state->current_state] != NULL);
    state_handlers[state->current_state](k);
    return true;
}

/* Run one decoded key through the macro layer (record / slot selection) and
 * then the current state's handler. Shared by the terminal editor and the
 * GUI, which decodes raylib events into the same Key struct. */
void editor_handle_key(Key key) {
    /* Macro meta: awaiting slot key after 'm' — consume key, start or cancel */
    if (state->macro_meta) {
        state->macro_meta = false;
        if (key.code == KEY_CHAR && key.mod == MOD_NONE &&
                key.ch >= 'a' && key.ch <= 'z') {
            state->macro_recording = true;
            state->macro_slot = key.ch;
            state->macro_buf.length = 0;
        }
        /* ESC or any other key: cancel, key consumed */
        return;
    }

    /* Replay pending: awaiting slot key after ',' — consume key, queue or cancel */
    if (state->macro_replay_pending) {
        state->macro_replay_pending = false;
        if (key.code == KEY_CHAR && key.mod == MOD_NONE) {
            char ch = key.ch;
            char slot = '\0';
            if      (ch >= 'a' && ch <= 'z') slot = ch;
            else if (ch == ',')              slot = state->macro_last_replayed;
            else if (ch == '.')              slot = state->macro_last_saved;
            if (slot >= 'a' && slot <= 'z') {
                KeyArray *m = &state->macro_slots[slot - 'a'];
                if (m->length > 0) {
                    for (size_t mi = 0; mi < m->length; mi++)
                        ARRAY_ADD(state->macro_replay, m->data[mi]);
                    state->macro_last_replayed = slot;
                }
            }
        }
        /* any other key or empty slot: cancel, key consumed */
        return;
    }

    /* Record key into macro buf (skip plain 'm' — it is the stop delimiter) */
    if (state->macro_recording &&
            !(key.code == KEY_CHAR && key.mod == MOD_NONE && key.ch == 'm')) {
        ARRAY_ADD(state->macro_buf, key);
    }

    /* Plain left click below the map: act on the info line clicked (any state);
     * on the room list: pick that room. */
    if (key.code == KEY_MOUSE && !key.mouse.release && !MOUSE_IS_SCROLL(key.mouse.button) &&
            MOUSE_BTN(key.mouse.button) == 0 && !(key.mod & MOD_CTRL)) {
        if (room_list_state() && key.mouse.y >= 3 && key.mouse.x < 2 * WIDTH_TILES) {
            if (room_list_click(key.mouse.y, key.mouse.x)) return;
        } else if (key.mouse.y > HEIGHT_TILES) {
            if (panel_click(key.mouse.y, key.mouse.x)) return;
        }
    }

    assert(state->current_state < NUM_STATES);
    assert(state_handlers[state->current_state] != NULL);
    state_handlers[state->current_state](key);
}

bool process_input(void) {
    /* Drain one queued replay key before blocking on stdin */
    if (editor_replay_step()) return true;

    state->status_message[0] = '\0';  /* clear previous status on each keypress */
    struct pollfd fd = { .fd = STDIN_FILENO, .events = POLLIN };
    int ret = poll(&fd, 1, 1000);
    assert(ret >= 0 || errno == EINTR);
    if (!(fd.revents & (POLLIN | POLLHUP))) return false;
    char buf[64];
    ssize_t n = read(fd.fd, buf, sizeof(buf));
    assert(n >= 0);
    if (n == 0) {
        /* EOF on stdin: exit when running non-interactively (piped input) */
        if (!isatty(STDIN_FILENO)) { end(); UNREACHABLE(); }
        return false;
    }

    /* If buffer ends with a bare ESC (or ESC+[), the terminal has split the
     * sequence across two writes.  Wait up to 50 ms for the rest to arrive. */
    while (n < (ssize_t)sizeof(buf)) {
        uint8_t last = (uint8_t)buf[n - 1];
        bool incomplete = (last == 0x1b) ||
                          (n >= 2 && (uint8_t)buf[n - 2] == 0x1b && last == '[');
        if (!incomplete) break;
        struct pollfd fd2 = { .fd = STDIN_FILENO, .events = POLLIN };
        if (poll(&fd2, 1, 50) <= 0 || !(fd2.revents & POLLIN)) break;
        ssize_t extra = read(STDIN_FILENO, buf + n, (ssize_t)sizeof(buf) - n);
        if (extra <= 0) break;
        n += extra;
    }
    int i = 0;
    while (i < n) {
        editor_handle_key(parse_key(buf, &i, (int)n));
    }
    return true;
}

bool update(void) { return false; }

