#include "editor_types.h"
#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <string.h>

typedef bool (*state_handler_fn)(Key key);

static bool handle_goto_object(Key key);
static bool handle_goto_switch(Key key);
static bool handle_goto_room(Key key);
static bool handle_edit_roomname(Key key);
static bool handle_toggle_display(Key key);
static bool handle_edit_roomdetails(Key key);
static bool handle_edit_roomdetails_num(Key key);
static bool handle_edit_roomdetails_room(Key key);
static bool handle_edit_switchdetails(Key key);
static bool handle_edit_switchdetails_select_chunk(Key key);
static bool handle_edit_switchdetails_chunk_switch(Key key);
static bool handle_edit_switchdetails_chunk_switch_room(Key key);
static bool handle_edit_switchdetails_chunk_memory(Key key);
static bool handle_edit_switchdetails_chunk_block(Key key);
static bool handle_edit_switchdetails_chunk_object(Key key);
static bool handle_normal(Key key);
static bool handle_tile_edit(Key key);

static void fork_play(void) {
    signal(SIGCHLD, SIG_IGN);
    pid_t child = fork();
    if (child == 0) {
        if (state != NULL) {
            freeRoomFile(&state->rooms);
            free(state);
        }
        pid_t sid = fork();
        if (sid == -1) exit(EXIT_FAILURE);
        if (sid > 0) exit(EXIT_SUCCESS);
        if (setsid() == -1) exit(EXIT_FAILURE);
        execv("./play.sh", (char**){0});
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
    while (ch_i < sw->chunks.length - 1) {
        sw->chunks.data[ch_i] = sw->chunks.data[ch_i+1];
        ch_i++;
    }
    memset(sw->chunks.data + ch_i, 0, sizeof(struct SwitchChunk));
    sw->chunks.length --;
    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
    assert(writeRooms(&state->rooms));
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
            sw->chunks.data[state->current_chunk] = sw->chunks.data[state->current_chunk+1];
            sw->chunks.data[state->current_chunk+1] = ch;
            state->current_chunk ++;
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            assert(writeRooms(&state->rooms));
        }
    }
}

static void chunk_swap_prev(void) {
    size_t *cursorlevel = &state->current_level;
    if (state->current_switch) {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk ch = sw->chunks.data[state->current_chunk];
        if (state->current_chunk > 1) {
            sw->chunks.data[state->current_chunk] = sw->chunks.data[state->current_chunk-1];
            sw->chunks.data[state->current_chunk-1] = ch;
            state->current_chunk --;
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            assert(writeRooms(&state->rooms));
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
    } else {
        room->tiles[TILE_IDX(x, y)] = 0;
    }
    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
    assert(writeRooms(&state->rooms));
}

static bool handle_goto_object(Key key) {
    v2 *cursor = &state->cursors[state->current_level];
    size_t *cursorlevel = &state->current_level;
    if (key.code == KEY_CHAR && isxdigit(key.ch)) {
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
                state->current_state = state->previous_state;
                state->previous_state = NORMAL;
            }
        }
    } else if (key.code == KEY_ESC) {
        if (state->help) {
            state->help = false;
        } else {
            state->current_state = state->previous_state;
            state->current_switch = 0;
            state->switch_on = false;
            state->current_chunk = 0;
            state->previous_state = NORMAL;
        }
    } else if (key.code == KEY_CTRL) {
        switch (key.ch) {
            case '_': state->help = !state->help; break;
            case 'H': state->debug.hex = !state->debug.hex; break;
        }
    } else if (key.code == KEY_CHAR && key.ch == 'q') {
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
    } else if (key.code == KEY_CHAR && key.ch == '?') {
        state->help = !state->help;
    }
    return true;
}

static bool handle_goto_switch(Key key) {
    v2 *cursor = &state->cursors[state->current_level];
    size_t *cursorlevel = &state->current_level;
    if (key.code == KEY_CHAR && isxdigit(key.ch)) {
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
        }
    } else if (key.code == KEY_ESC) {
        if (state->help) {
            state->help = false;
        } else {
            state->current_state = state->previous_state;
            state->current_switch = 0;
            state->switch_on = false;
            state->current_chunk = 0;
            state->previous_state = NORMAL;
        }
    } else if (key.code == KEY_CTRL) {
        switch (key.ch) {
            case '_': state->help = !state->help; break;
            case 'H': state->debug.hex = !state->debug.hex; break;
        }
    } else if (key.code == KEY_CHAR && key.ch == 'q') {
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
    } else if (key.code == KEY_CHAR && key.ch == '?') {
        state->help = !state->help;
    }
    return true;
}

/* Helper macro for goto_room navigation */
#define GOTO_ROOM_NAVIGATE(dest_level) do { \
    *cursorlevel = (dest_level); \
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
} while (0)

static bool handle_goto_room(Key key) {
    size_t *cursorlevel = &state->current_level;
    if (key.code == KEY_CHAR && isxdigit(key.ch) &&
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
               key.code == KEY_CHAR && key.ch >= '0' &&
               key.ch <= (state->debug.hex ? '3' : '6')) {
        state->partial_byte = 0xFF00 | (key.ch - '0');
    } else if (key.code == KEY_BS || key.code == KEY_DELETE) {
        if (state->roomname_cursor) {
            state->room_name[--state->roomname_cursor] = 0;
        } else {
            state->partial_byte = 0;
        }
    } else if (key.code == KEY_UP) {
        GOTO_ROOM_NAVIGATE(state->rooms.rooms[*cursorlevel].data.room_north);
    } else if (key.code == KEY_DOWN) {
        GOTO_ROOM_NAVIGATE(state->rooms.rooms[*cursorlevel].data.room_south);
    } else if (key.code == KEY_RIGHT) {
        GOTO_ROOM_NAVIGATE(state->rooms.rooms[*cursorlevel].data.room_east);
    } else if (key.code == KEY_LEFT) {
        GOTO_ROOM_NAVIGATE(state->rooms.rooms[*cursorlevel].data.room_west);
    } else if (key.code == KEY_CHAR && key.mod == MOD_ALT && key.ch == 'h') {
        GOTO_ROOM_NAVIGATE(state->rooms.rooms[*cursorlevel].data.room_west);
    } else if (key.code == KEY_CHAR && key.mod == MOD_ALT && key.ch == 'j') {
        GOTO_ROOM_NAVIGATE(state->rooms.rooms[*cursorlevel].data.room_south);
    } else if (key.code == KEY_CHAR && key.mod == MOD_ALT && key.ch == 'k') {
        GOTO_ROOM_NAVIGATE(state->rooms.rooms[*cursorlevel].data.room_north);
    } else if (key.code == KEY_CHAR && key.mod == MOD_ALT && key.ch == 'l') {
        GOTO_ROOM_NAVIGATE(state->rooms.rooms[*cursorlevel].data.room_east);
    } else if (key.code == KEY_ESC) {
        if (state->help) {
            state->help = false;
        } else {
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
        }
    } else if (key.code == KEY_CTRL) {
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
                }
                break;
        }
    } else if (key.code == KEY_CHAR && state->partial_byte == 0) {
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
    return true;
}

#undef GOTO_ROOM_NAVIGATE

static bool handle_edit_roomname(Key key) {
    size_t *cursorlevel = &state->current_level;

    if (key.code == KEY_CTRL && key.ch == 'J') {
        /* ENTER (Ctrl-J = \n) */
        struct DecompressedRoom *room = &state->rooms.rooms[*cursorlevel].data;
        assert(C_ARRAY_LEN(state->room_name) <= C_ARRAY_LEN(room->name));
        strncpy(room->name, state->room_name, C_ARRAY_LEN(room->name));
        state->current_state = state->previous_state;
        state->current_switch = 0;
        state->switch_on = false;
        state->current_chunk = 0;
        state->previous_state = NORMAL;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_CHAR) {
        if (state->roomname_cursor < C_ARRAY_LEN(state->room_name) - 1) {
            state->room_name[state->roomname_cursor++] = key.ch;
        }
    } else if (key.code == KEY_HOME) {
        state->roomname_cursor = 0;
    } else if (key.code == KEY_END) {
        state->roomname_cursor = strlen(state->room_name);
    } else if (key.code == KEY_DELETE) {
        for (size_t c = state->roomname_cursor; c < C_ARRAY_LEN(state->room_name) - 1; c ++) {
            state->room_name[c] = state->room_name[c + 1];
            state->room_name[c + 1] = '\0';
        }
        if (state->roomname_cursor) {
            state->roomname_cursor--;
        }
    } else if (key.code == KEY_RIGHT) {
        if (state->roomname_cursor < C_ARRAY_LEN(state->room_name) - 1 && state->room_name[state->roomname_cursor]) {
            state->roomname_cursor ++;
        }
    } else if (key.code == KEY_LEFT) {
        if (state->roomname_cursor > 0) {
            state->roomname_cursor --;
        }
    } else if (key.code == KEY_ESC) {
        if (state->help) {
            state->help = false;
        } else {
            state->current_state = state->previous_state;
            state->current_switch = 0;
            state->switch_on = false;
            state->current_chunk = 0;
            state->previous_state = NORMAL;
        }
    } else if (key.code == KEY_BS) {
        if (state->roomname_cursor) {
            for (size_t c = state->roomname_cursor - 1; c < C_ARRAY_LEN(state->room_name) - 1; c ++) {
                state->room_name[c] = state->room_name[c + 1];
                state->room_name[c + 1] = '\0';
            }
            if (state->roomname_cursor) {
                state->roomname_cursor--;
            }
        }
    } else if (key.code == KEY_CTRL) {
        switch (key.ch) {
            case 'H': state->debug.hex = !state->debug.hex; break;
            case '_': state->help = !state->help; break;
        }
    }
    return true;
}

static bool handle_toggle_display(Key key) {
    uint8_t changed = 1;
    if (key.code == KEY_CHAR) {
        switch (key.ch) {
            case 'a': debugalltoggle(); break;
            case 'd': state->debug.data = !state->debug.data; break;
            case 'n': state->debug.neighbours = !state->debug.neighbours; break;
            case 'o': state->debug.objects = !state->debug.objects; break;
            case 'p': state->debug.pos = !state->debug.pos; break;
            case 's': state->debug.switches = !state->debug.switches; break;
            case 'u': state->debug.unknowns = !state->debug.unknowns; break;
            case 'q': break;
            case '?': state->help = !state->help; changed = 0; break;
            default: changed = 0; break;
        }
    } else if (key.code == KEY_ESC) {
        if (state->help) { state->help = false; changed = 0; }
        /* else: exit toggle display (changed stays 1) */
    } else if (key.code == KEY_CTRL) {
        switch (key.ch) {
            case 'H': state->debug.hex = !state->debug.hex; break;
            case '_': state->help = !state->help; changed = 0; break;
            default: changed = 0; break;
        }
    } else {
        changed = 0;
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

static bool handle_edit_roomdetails(Key key) {
    if (key.code == KEY_UP) {
        state->room_detail = 'k';
        state->current_state = EDIT_ROOMDETAILS_ROOM;
        state->current_switch = 0;
        state->switch_on = false;
        state->current_chunk = 0;
    } else if (key.code == KEY_DOWN) {
        state->room_detail = 'j';
        state->current_state = EDIT_ROOMDETAILS_ROOM;
        state->current_switch = 0;
        state->switch_on = false;
        state->current_chunk = 0;
    } else if (key.code == KEY_LEFT) {
        state->room_detail = 'h';
        state->current_state = EDIT_ROOMDETAILS_ROOM;
        state->current_switch = 0;
        state->switch_on = false;
        state->current_chunk = 0;
    } else if (key.code == KEY_RIGHT) {
        state->room_detail = 'l';
        state->current_state = EDIT_ROOMDETAILS_ROOM;
        state->current_switch = 0;
        state->switch_on = false;
        state->current_chunk = 0;
    } else if (key.code == KEY_CHAR) {
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
            case 'b':
            case 'c':
            case 'e':
            case 'f':
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
    } else if (key.code == KEY_ESC) {
        if (state->help) {
            state->help = false;
        } else {
            state->current_state = state->previous_state;
            state->current_switch = 0;
            state->switch_on = false;
            state->current_chunk = 0;
            state->previous_state = TILE_EDIT;
        }
    } else if (key.code == KEY_CTRL) {
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
    }
    return true;
}

static bool handle_edit_roomdetails_num(Key key) {
    if (key.code == KEY_CHAR && ((state->debug.hex && isxdigit(key.ch)) || (!state->debug.hex && isdigit(key.ch)))) {
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
            switch (state->room_detail) {
                case 'g': room->background = value; break;
                case 't': room->tile_offset = value; break;
                case 'd': room->room_damage = value; break;
                case '|': room->gravity_vertical = value; break;
                case '-': room->gravity_horizontal = value; break;

                case 'b': room->UNKNOWN_b = value; break;
                case 'c': room->UNKNOWN_c = value; break;
                case 'e':
                    if ((value & ~0x3) == 0) {
                        room->_num_switches |= value;
                    }
                    break;

                case 'f': room->UNKNOWN_f = value; break;

                default: UNREACHABLE();
            }
            state->current_state = EDIT_ROOMDETAILS;
            state->current_switch = 0;
            state->switch_on = false;
            state->current_chunk = 0;
            state->partial_byte = 0;
            state->room_detail = 0;
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            assert(writeRooms(&state->rooms));
        } else {
            if (state->room_detail != 'e' || digit == 0) {
                state->partial_byte = 0xFF00 | digit;
            }
        }
    } else if (key.code == KEY_BS || key.code == KEY_DELETE) {
        state->partial_byte = 0;
    } else if (key.code == KEY_ESC) {
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
    } else if (key.code == KEY_CTRL) {
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
        }
    } else if (key.code == KEY_CHAR && key.ch == 'q') {
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
    } else if (key.code == KEY_CHAR && key.ch == '?') {
        state->help = !state->help;
    }
    return true;
}

static bool handle_edit_roomdetails_room(Key key) {
    size_t *cursorlevel = &state->current_level;
    if (state->partial_byte &&
            ((state->debug.hex && key.code == KEY_CHAR && isxdigit(key.ch)) ||
             (!state->debug.hex && key.code == KEY_CHAR && isdigit(key.ch)))) {
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
            assert(writeRooms(&state->rooms));
        }
    } else if (state->roomname_cursor == 0 && state->partial_byte == 0 &&
               key.code == KEY_CHAR && key.ch >= '0' &&
               key.ch <= (state->debug.hex ? '3' : '6')) {
        state->partial_byte = 0xFF00 | (key.ch - '0');
    } else if (key.code == KEY_BS || key.code == KEY_DELETE) {
        if (state->roomname_cursor) {
            state->room_name[--state->roomname_cursor] = 0;
        } else {
            state->partial_byte = 0;
        }
    } else if (key.code == KEY_ESC) {
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
    } else if (key.code == KEY_CTRL) {
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
        }
    } else if (key.code == KEY_CHAR && state->partial_byte == 0) {
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
                assert(writeRooms(&state->rooms));
            }
        }
    } else {
        (void)cursorlevel;
    }
    return true;
}

static bool handle_edit_switchdetails(Key key) {
    if (key.code == KEY_UP) {
        struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
        room->switches[state->current_switch - 1].chunks.data[0].side = TOP;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_DOWN) {
        struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
        room->switches[state->current_switch - 1].chunks.data[0].side = BOTTOM;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_RIGHT) {
        struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
        room->switches[state->current_switch - 1].chunks.data[0].side = RIGHT;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_LEFT) {
        struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
        room->switches[state->current_switch - 1].chunks.data[0].side = LEFT;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_BS || key.code == KEY_DELETE) {
        if (!state->current_switch) UNREACHABLE();
        struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
        size_t sw_i = state->current_switch - 1;
        while (sw_i < (size_t)(room->num_switches - 1)) {
            room->switches[sw_i] = room->switches[sw_i+1];
            sw_i++;
        }
        memset(room->switches + sw_i, 0, sizeof(struct SwitchObject));
        room->num_switches --;
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
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_CHAR) {
        switch (key.ch) {
            case '+':
            {
                if (state->current_switch) {
                    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                    size_t sw_i = state->current_switch - 1;
                    struct SwitchObject sw = room->switches[sw_i];
                    if (sw_i < (unsigned)room->num_switches - 1) {
                        room->switches[sw_i] = room->switches[sw_i+1];
                        room->switches[sw_i+1] = sw;
                        state->current_switch ++;
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        assert(writeRooms(&state->rooms));
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
                        room->switches[sw_i] = room->switches[sw_i-1];
                        room->switches[sw_i-1] = sw;
                        state->current_switch --;
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        assert(writeRooms(&state->rooms));
                    }
                }
            }; break;

            case 'h':
            {
                struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                room->switches[state->current_switch - 1].chunks.data[0].side = LEFT;
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                assert(writeRooms(&state->rooms));
            }; break;

            case 'j':
            {
                struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                room->switches[state->current_switch - 1].chunks.data[0].side = BOTTOM;
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                assert(writeRooms(&state->rooms));
            }; break;

            case 'k':
            {
                struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                room->switches[state->current_switch - 1].chunks.data[0].side = TOP;
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                assert(writeRooms(&state->rooms));
            }; break;

            case 'l':
            {
                struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                room->switches[state->current_switch - 1].chunks.data[0].side = RIGHT;
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                assert(writeRooms(&state->rooms));
            }; break;

            case 'o':
            {
                struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                room->switches[state->current_switch - 1].chunks.data[0].one_time_use = !room->switches[state->current_switch - 1].chunks.data[0].one_time_use;
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                assert(writeRooms(&state->rooms));
            }; break;

            case 'y':
            {
                if (!state->current_switch) break;
                struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                struct SwitchObject *sw = room->switches + state->current_switch - 1;
                if (state->clipboard.type == CLIP_OBJECT && state->clipboard.object.tiles) {
                    free(state->clipboard.object.tiles);
                    state->clipboard.object.tiles = NULL;
                } else if (state->clipboard.type == CLIP_SWITCH) {
                    ARRAY_FREE(state->clipboard.sw.chunks);
                }
                state->clipboard.type = CLIP_SWITCH;
                state->clipboard.sw.chunks.length = sw->chunks.length;
                state->clipboard.sw.chunks.capacity = sw->chunks.length;
                state->clipboard.sw.chunks.data = malloc(sw->chunks.length * sizeof(struct SwitchChunk));
                assert(state->clipboard.sw.chunks.data != NULL);
                memcpy(state->clipboard.sw.chunks.data, sw->chunks.data,
                       sw->chunks.length * sizeof(struct SwitchChunk));
            }; break;

            case 'p':
            {
                if (!state->current_switch) break;
                if (state->clipboard.type != CLIP_CHUNK) break;
                struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                struct SwitchObject *sw = room->switches + state->current_switch - 1;
                ARRAY_ADD(sw->chunks, state->clipboard.chunk);
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                assert(writeRooms(&state->rooms));
            }; break;

            case 'e':
            {
                struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                room->switches[state->current_switch - 1].chunks.data[0].room_entry = !room->switches[state->current_switch - 1].chunks.data[0].room_entry;
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                assert(writeRooms(&state->rooms));
            }; break;

            case 's':
            {
                struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                room->switches[state->current_switch - 1].chunks.data[0].side = (room->switches[state->current_switch - 1].chunks.data[0].side + 1) % NUM_SIDES;
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                assert(writeRooms(&state->rooms));
            }; break;

            case 'c':
            {
                struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                struct SwitchObject *sw = room->switches + state->current_switch - 1;
                if (sw->chunks.length == 1) {
                    state->current_chunk = sw->chunks.length;
                    ARRAY_ADD(sw->chunks, ((struct SwitchChunk){ .type = TOGGLE_BLOCK }));
                    state->current_state = EDIT_SWITCHDETAILS_CHUNK_BLOCK_DETAILS;
                    state->switch_on = false;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    assert(writeRooms(&state->rooms));
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
                ARRAY_ADD(sw->chunks, ((struct SwitchChunk){ .type = TOGGLE_BLOCK }));
                state->current_state = EDIT_SWITCHDETAILS_CHUNK_BLOCK_DETAILS;
                state->switch_on = false;
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                assert(writeRooms(&state->rooms));
            }; break;

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
    } else if (key.code == KEY_ESC) {
        if (state->help) {
            state->help = false;
        } else {
            state->current_state = state->previous_state;
            state->current_switch = 0;
            state->switch_on = false;
            state->previous_state = TILE_EDIT;
        }
    } else if (key.code == KEY_CTRL) {
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
        }
    }
    return true;
}

static bool handle_edit_switchdetails_select_chunk(Key key) {
    size_t *cursorlevel = &state->current_level;
    if (key.code == KEY_CHAR && isxdigit(key.ch)) {
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
        }
    } else if (key.code == KEY_BS || key.code == KEY_DELETE) {
        state->partial_byte = 0;
    } else if (key.code == KEY_ESC) {
        if (state->help) {
            state->help = false;
        } else {
            state->current_state = EDIT_SWITCHDETAILS;
        }
    } else if (key.code == KEY_CTRL) {
        switch (key.ch) {
            case '_': state->help = !state->help; break;
            case 'H': state->debug.hex = !state->debug.hex; break;
            case 'P': fork_play(); break;
        }
    } else if (key.code == KEY_CHAR && key.ch == 'q') {
        if (state->help) {
            state->help = !state->help;
        } else {
            state->current_state = EDIT_SWITCHDETAILS;
        }
    } else if (key.code == KEY_CHAR && key.ch == '?') {
        state->help = !state->help;
    } else if (key.code == KEY_CHAR && key.ch == '+') {
        struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
        struct SwitchObject *sw = room->switches + state->current_switch - 1;
        if (sw->chunks.length == 16) {
            return true;
        }
        ARRAY_ADD(sw->chunks, ((struct SwitchChunk){ .type = TOGGLE_BLOCK }));
        state->switch_on = false;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    }
    return true;
}

static void yank_chunk(void) {
    if (!state->current_switch || !state->current_chunk) return;
    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
    struct SwitchObject *sw = room->switches + state->current_switch - 1;
    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
    if (state->clipboard.type == CLIP_OBJECT && state->clipboard.object.tiles) {
        free(state->clipboard.object.tiles);
        state->clipboard.object.tiles = NULL;
    } else if (state->clipboard.type == CLIP_SWITCH) {
        ARRAY_FREE(state->clipboard.sw.chunks);
    }
    state->clipboard.type = CLIP_CHUNK;
    state->clipboard.chunk = *chunk;
}

static bool handle_edit_switchdetails_chunk_switch(Key key) {
    size_t *cursorlevel = &state->current_level;
    if (key.code == KEY_CHAR && isxdigit(key.ch)) {
        uint8_t b = 0;
        if (isdigit(key.ch)) {
            b = key.ch - '0';
        } else {
            b = 10 + tolower(key.ch) - 'a';
        }
        if ((state->partial_byte & 0xFF00) != 0) {
            size_t index = b;
            if (state->debug.hex) {
                index += 16 * (state->partial_byte & 0xFF);
            } else {
                index += 10 * (state->partial_byte & 0xFF);
            }
            struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
            struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
            assert(chunk->type == TOGGLE_BIT);
            chunk->switch_idx = index;
            state->partial_byte = 0;
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            assert(writeRooms(&state->rooms));
        } else {
            state->partial_byte = 0xFF00 | b;
        }
    } else if (key.code == KEY_CHAR && key.ch == 'r') {
        state->current_state = EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS_ROOM;
    } else if (key.code == KEY_CHAR && key.ch == 'o') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BIT);
        chunk->off = (chunk->off + 1) % 4;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_CHAR && key.ch == 'n') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BIT);
        chunk->on = (chunk->on + 1) % 4;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_BS || key.code == KEY_DELETE) {
        if (state->partial_byte) {
            state->partial_byte = 0;
        } else {
            delete_current_chunk();
        }
    } else if (key.code == KEY_ESC) {
        if (state->help) {
            state->help = false;
        } else {
            if (state->partial_byte) {
                state->partial_byte = 0;
            } else {
                pop_chunk_state();
            }
        }
    } else if (key.code == KEY_CHAR && key.ch == 't') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        enum SwitchChunkType typ = chunk->type;
        switch (typ) {
            case TOGGLE_BLOCK:
            {
                size_t overflow = WIDTH_TILES * HEIGHT_TILES;
                size_t point = chunk->y * WIDTH_TILES + chunk->x;
                if (point < overflow) {
                    memset(chunk, 0, sizeof(*chunk));
                    chunk->type = TOGGLE_BLOCK;
                    chunk->x = 0;
                    chunk->y = HEIGHT_TILES;
                    chunk->size = 1;
                    state->current_state = EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS;
                    break;
                }
            }; fallthrough;
            default:
                memset(chunk, 0, sizeof(*chunk));
                chunk->type = (typ + 1) % NUM_CHUNK_TYPES;
                if (chunk->type == PREAMBLE) chunk->type = (chunk->type + 1) % NUM_CHUNK_TYPES;
                switch (chunk->type) {
                    case TOGGLE_BIT:
                        state->current_state = EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS;
                        chunk->bitmask = 0x1;
                        break;
                    case TOGGLE_OBJECT:
                        state->current_state = EDIT_SWITCHDETAILS_CHUNK_OBJECT_DETAILS;
                        break;
                    case TOGGLE_BLOCK:
                        state->current_state = EDIT_SWITCHDETAILS_CHUNK_BLOCK_DETAILS;
                        break;
                    default: UNREACHABLE();
                }
        }
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_CTRL) {
        switch (key.ch) {
            case '_': state->help = !state->help; break;
            case 'H': state->debug.hex = !state->debug.hex; break;
            case 'P': fork_play(); break;
        }
    } else if (key.code == KEY_CHAR && key.ch == 'q') {
        if (state->help) {
            state->help = !state->help;
        } else {
            if (state->partial_byte) {
                state->partial_byte = 0;
            } else {
                pop_chunk_state();
            }
        }
    } else if (key.code == KEY_CHAR && key.ch == '?') {
        state->help = !state->help;
    } else if (key.code == KEY_CHAR && key.ch == '+') {
        chunk_swap_next();
    } else if (key.code == KEY_CHAR && key.ch == '-') {
        chunk_swap_prev();
    } else if (key.code == KEY_CHAR && key.ch == 'y') {
        yank_chunk();
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

    if (state->partial_byte &&
            ((state->debug.hex && key.code == KEY_CHAR && isxdigit(key.ch)) ||
             (!state->debug.hex && key.code == KEY_CHAR && isdigit(key.ch)))) {
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
            chunk->room_idx = room_id;
            chunk->switch_idx = 0;
            state->current_state = EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS;
            state->partial_byte = 0;
            state->roomname_cursor = 0;
            RESTORE_ROOM_NAME();
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            assert(writeRooms(&state->rooms));
        }
    } else if (state->roomname_cursor == 0 && state->partial_byte == 0 &&
               key.code == KEY_CHAR && key.ch >= '0' &&
               key.ch <= (state->debug.hex ? '3' : '6')) {
        state->partial_byte = 0xFF00 | (key.ch - '0');
    } else if (key.code == KEY_BS || key.code == KEY_DELETE) {
        if (state->roomname_cursor) {
            state->room_name[--state->roomname_cursor] = 0;
        } else {
            state->partial_byte = 0;
        }
    } else if (key.code == KEY_ESC) {
        if (state->help) {
            state->help = false;
        } else {
            state->current_state = EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS;
            state->partial_byte = 0;
            state->roomname_cursor = 0;
            RESTORE_ROOM_NAME();
        }
    } else if (key.code == KEY_CTRL) {
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
        }
    } else if (key.code == KEY_CHAR && state->partial_byte == 0) {
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
                chunk->room_idx = matching_room;
                chunk->switch_idx = 0;
                state->current_state = EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS;
                state->partial_byte = 0;
                state->roomname_cursor = 0;
                RESTORE_ROOM_NAME();
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                assert(writeRooms(&state->rooms));
            }
        }
    }
#undef RESTORE_ROOM_NAME
    return true;
}

static bool handle_edit_switchdetails_chunk_memory(Key key) {
    size_t *cursorlevel = &state->current_level;
    if (key.code == KEY_CHAR && isxdigit(key.ch)) {
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
            if (state->switch_on) {
                chunk->on = value;
            } else {
                chunk->off = value;
            }
            state->partial_byte = 0;
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            assert(writeRooms(&state->rooms));
        } else {
            state->partial_byte = 0xFF00 | b;
        }
    } else if (key.code == KEY_CHAR && (key.ch == 'h' || key.ch == 'k')) {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BLOCK);
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
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_CHAR && (key.ch == 'j' || key.ch == 'l')) {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BLOCK);
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
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_UP || key.code == KEY_LEFT) {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BLOCK);
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
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_DOWN || key.code == KEY_RIGHT) {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BLOCK);
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
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_CHAR && key.ch == 'o') {
        state->switch_on = !state->switch_on;
    } else if (key.code == KEY_CHAR && key.ch == ' ') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BLOCK);
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
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_CHAR && key.ch == '^') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BLOCK);
        if (chunk->size < 8) chunk->size ++;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_CHAR && key.ch == 'v') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BLOCK);
        if (chunk->size > 1) chunk->size --;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_CHAR && key.ch == 'r') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BLOCK);
        chunk->dir = (chunk->dir + 1) % NUM_DIRECTIONS;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_BS || key.code == KEY_DELETE) {
        if (state->partial_byte) {
            state->partial_byte = 0;
        } else {
            delete_current_chunk();
        }
    } else if (key.code == KEY_ESC) {
        if (state->help) {
            state->help = false;
        } else {
            if (state->partial_byte) {
                state->partial_byte = 0;
            } else {
                pop_chunk_state();
            }
        }
    } else if (key.code == KEY_CHAR && key.ch == 't') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        enum SwitchChunkType typ = chunk->type;
        switch (typ) {
            case TOGGLE_BLOCK:
            {
                size_t overflow = WIDTH_TILES * HEIGHT_TILES;
                size_t point = chunk->y * WIDTH_TILES + chunk->x;
                if (point < overflow) {
                    memset(chunk, 0, sizeof(*chunk));
                    chunk->type = TOGGLE_BLOCK;
                    chunk->x = 0;
                    chunk->y = HEIGHT_TILES;
                    chunk->size = 1;
                    state->current_state = EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS;
                    break;
                }
            }; fallthrough;
            default:
                memset(chunk, 0, sizeof(*chunk));
                chunk->type = (typ + 1) % NUM_CHUNK_TYPES;
                if (chunk->type == PREAMBLE) chunk->type = (chunk->type + 1) % NUM_CHUNK_TYPES;
                switch (chunk->type) {
                    case TOGGLE_BIT:
                        state->current_state = EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS;
                        chunk->bitmask = 0x1;
                        break;
                    case TOGGLE_OBJECT:
                        state->current_state = EDIT_SWITCHDETAILS_CHUNK_OBJECT_DETAILS;
                        break;
                    case TOGGLE_BLOCK:
                        state->current_state = EDIT_SWITCHDETAILS_CHUNK_BLOCK_DETAILS;
                        break;
                    default: UNREACHABLE();
                }
        }
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_CTRL) {
        switch (key.ch) {
            case '_': state->help = !state->help; break;
            case 'H': state->debug.hex = !state->debug.hex; break;
            case 'P': fork_play(); break;
        }
    } else if (key.code == KEY_CHAR && key.ch == 'q') {
        if (state->help) {
            state->help = !state->help;
        } else {
            if (state->partial_byte) {
                state->partial_byte = 0;
            } else {
                pop_chunk_state();
            }
        }
    } else if (key.code == KEY_CHAR && key.ch == '?') {
        state->help = !state->help;
    } else if (key.code == KEY_CHAR && key.ch == '+') {
        chunk_swap_next();
    } else if (key.code == KEY_CHAR && key.ch == '-') {
        chunk_swap_prev();
    } else if (key.code == KEY_CHAR && key.ch == 'y') {
        yank_chunk();
    }
    return true;
}

static bool handle_edit_switchdetails_chunk_block(Key key) {
    size_t *cursorlevel = &state->current_level;
    if (key.code == KEY_CHAR && isxdigit(key.ch)) {
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
            if (state->switch_on) {
                chunk->on = value;
            } else {
                chunk->off = value;
            }
            state->partial_byte = 0;
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            assert(writeRooms(&state->rooms));
        } else {
            state->partial_byte = 0xFF00 | b;
        }
    } else if (key.code == KEY_CHAR && key.ch == 'o') {
        state->switch_on = !state->switch_on;
        state->partial_byte = 0;
    } else if (key.code == KEY_CHAR && key.ch == ' ') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BLOCK);
        chunk->dir = (chunk->dir + 1) % NUM_DIRECTIONS;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_CHAR && key.ch == '^') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BLOCK);
        if (chunk->size < 8) chunk->size ++;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_CHAR && key.ch == 'v') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BLOCK);
        if (chunk->size > 1) chunk->size --;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_CHAR && key.ch == 'h') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BLOCK);
        if (chunk->x) chunk->x --;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_CHAR && key.ch == 'j') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BLOCK);
        chunk->y ++;
        size_t overflow = WIDTH_TILES * HEIGHT_TILES;
        size_t point = chunk->y * WIDTH_TILES + chunk->x;
        if (point >= overflow) {
            state->current_state = EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS;
        }
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_CHAR && key.ch == 'k') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BLOCK);
        if (chunk->y) chunk->y --;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_CHAR && key.ch == 'l') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BLOCK);
        chunk->x ++;
        size_t overflow = WIDTH_TILES * HEIGHT_TILES;
        size_t point = chunk->y * WIDTH_TILES + chunk->x;
        if (point >= overflow) {
            state->current_state = EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS;
        }
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_UP) {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BLOCK);
        if (chunk->y) chunk->y --;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_DOWN) {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BLOCK);
        chunk->y ++;
        size_t overflow = WIDTH_TILES * HEIGHT_TILES;
        size_t point = chunk->y * WIDTH_TILES + chunk->x;
        if (point >= overflow) {
            state->current_state = EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS;
        }
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_LEFT) {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BLOCK);
        if (chunk->x) chunk->x --;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_RIGHT) {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BLOCK);
        chunk->x ++;
        size_t overflow = WIDTH_TILES * HEIGHT_TILES;
        size_t point = chunk->y * WIDTH_TILES + chunk->x;
        if (point >= overflow) {
            state->current_state = EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS;
        }
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_BS || key.code == KEY_DELETE) {
        if (state->partial_byte) {
            state->partial_byte = 0;
        } else {
            delete_current_chunk();
        }
    } else if (key.code == KEY_ESC) {
        if (state->help) {
            state->help = false;
        } else {
            if (state->partial_byte) {
                state->partial_byte = 0;
            } else {
                pop_chunk_state();
            }
        }
    } else if (key.code == KEY_CHAR && key.ch == 't') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        enum SwitchChunkType typ = chunk->type;
        switch (typ) {
            case TOGGLE_BLOCK:
            {
                size_t overflow = WIDTH_TILES * HEIGHT_TILES;
                size_t point = chunk->y * WIDTH_TILES + chunk->x;
                if (point < overflow) {
                    memset(chunk, 0, sizeof(*chunk));
                    chunk->type = TOGGLE_BLOCK;
                    chunk->x = 0;
                    chunk->y = HEIGHT_TILES;
                    chunk->size = 1;
                    state->current_state = EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS;
                    break;
                }
            }; fallthrough;
            default:
                memset(chunk, 0, sizeof(*chunk));
                chunk->type = (typ + 1) % NUM_CHUNK_TYPES;
                if (chunk->type == PREAMBLE) chunk->type = (chunk->type + 1) % NUM_CHUNK_TYPES;
                switch (chunk->type) {
                    case TOGGLE_BIT:
                        state->current_state = EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS;
                        chunk->bitmask = 0x1;
                        break;
                    case TOGGLE_OBJECT:
                        state->current_state = EDIT_SWITCHDETAILS_CHUNK_OBJECT_DETAILS;
                        break;
                    case TOGGLE_BLOCK:
                        state->current_state = EDIT_SWITCHDETAILS_CHUNK_BLOCK_DETAILS;
                        break;
                    default: UNREACHABLE();
                }
        }
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_CTRL) {
        switch (key.ch) {
            case '_': state->help = !state->help; break;
            case 'H': state->debug.hex = !state->debug.hex; break;
            case 'P': fork_play(); break;
        }
    } else if (key.code == KEY_CHAR && key.ch == 'q') {
        if (state->help) {
            state->help = !state->help;
        } else {
            if (state->partial_byte) {
                state->partial_byte = 0;
            } else {
                pop_chunk_state();
            }
        }
    } else if (key.code == KEY_CHAR && key.ch == '?') {
        state->help = !state->help;
    } else if (key.code == KEY_CHAR && key.ch == '+') {
        chunk_swap_next();
    } else if (key.code == KEY_CHAR && key.ch == '-') {
        chunk_swap_prev();
    } else if (key.code == KEY_CHAR && key.ch == 'y') {
        yank_chunk();
    }
    return true;
}

static bool handle_edit_switchdetails_chunk_object(Key key) {
    size_t *cursorlevel = &state->current_level;
    if (key.code == KEY_CHAR && isxdigit(key.ch)) {
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
            chunk->value = value;
            state->partial_byte = 0;
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            assert(writeRooms(&state->rooms));
        } else {
            state->partial_byte = 0xFF00 | b;
        }
    } else if (key.code == KEY_BS || key.code == KEY_DELETE) {
        if (state->partial_byte) {
            state->partial_byte = 0;
        } else {
            delete_current_chunk();
        }
    } else if (key.code == KEY_ESC) {
        if (state->help) {
            state->help = false;
        } else {
            if (state->partial_byte) {
                state->partial_byte = 0;
            } else {
                pop_chunk_state();
            }
        }
    } else if (key.code == KEY_CHAR && key.ch == 't') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        enum SwitchChunkType typ = chunk->type;
        switch (typ) {
            case TOGGLE_BLOCK:
            {
                size_t overflow = WIDTH_TILES * HEIGHT_TILES;
                size_t point = chunk->y * WIDTH_TILES + chunk->x;
                if (point < overflow) {
                    memset(chunk, 0, sizeof(*chunk));
                    chunk->type = TOGGLE_BLOCK;
                    chunk->x = 0;
                    chunk->y = HEIGHT_TILES;
                    chunk->size = 1;
                    state->current_state = EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS;
                    break;
                }
            }; fallthrough;
            default:
                memset(chunk, 0, sizeof(*chunk));
                chunk->type = (typ + 1) % NUM_CHUNK_TYPES;
                if (chunk->type == PREAMBLE) chunk->type = (chunk->type + 1) % NUM_CHUNK_TYPES;
                switch (chunk->type) {
                    case TOGGLE_BIT:
                        state->current_state = EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS;
                        chunk->bitmask = 0x1;
                        break;
                    case TOGGLE_OBJECT:
                        state->current_state = EDIT_SWITCHDETAILS_CHUNK_OBJECT_DETAILS;
                        break;
                    case TOGGLE_BLOCK:
                        state->current_state = EDIT_SWITCHDETAILS_CHUNK_BLOCK_DETAILS;
                        break;
                    default: break;
                }
        }
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_CHAR && key.ch == 'i') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        chunk->index = (chunk->index + 1) % 0x10;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_CHAR && key.ch == 's') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        chunk->test = (((chunk->test >> 4) + 1) % 4) << 4;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_CTRL) {
        switch (key.ch) {
            case '_': state->help = !state->help; break;
            case 'H': state->debug.hex = !state->debug.hex; break;
            case 'P': fork_play(); break;
        }
    } else if (key.code == KEY_CHAR && key.ch == 'q') {
        if (state->help) {
            state->help = !state->help;
        } else {
            if (state->partial_byte) {
                state->partial_byte = 0;
            } else {
                pop_chunk_state();
            }
        }
    } else if (key.code == KEY_CHAR && key.ch == '?') {
        state->help = !state->help;
    } else if (key.code == KEY_CHAR && key.ch == '+') {
        chunk_swap_next();
    } else if (key.code == KEY_CHAR && key.ch == '-') {
        chunk_swap_prev();
    } else if (key.code == KEY_CHAR && key.ch == 'y') {
        yank_chunk();
    }
    return true;
}

static bool handle_normal(Key key) {
    v2 *cursor = &state->cursors[state->current_level];
    size_t *cursorlevel = &state->current_level;
    bool is_tile = (state->current_state == TILE_EDIT);

    if (key.code == KEY_CHAR && key.ch == 'q') {
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
    } else if (key.code == KEY_CHAR && key.ch == '?') {
        state->help = !state->help;
    } else if ((key.code == KEY_LEFT && key.mod == MOD_NONE) ||
               (key.code == KEY_CHAR && key.mod == MOD_NONE && key.ch == 'h')) {
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
            *cursorlevel = level;
        }
    } else if ((key.code == KEY_DOWN && key.mod == MOD_NONE) ||
               (key.code == KEY_CHAR && key.mod == MOD_NONE && key.ch == 'j')) {
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
            *cursorlevel = level;
        }
    } else if ((key.code == KEY_UP && key.mod == MOD_NONE) ||
               (key.code == KEY_CHAR && key.mod == MOD_NONE && key.ch == 'k')) {
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
            *cursorlevel = level;
        }
    } else if ((key.code == KEY_RIGHT && key.mod == MOD_NONE) ||
               (key.code == KEY_CHAR && key.mod == MOD_NONE && key.ch == 'l')) {
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
            *cursorlevel = level;
        }
    } else if (key.code == KEY_CHAR && key.ch == 'r') {
        state->previous_state = state->current_state;
        state->current_state = GOTO_ROOM;
        state->current_switch = 0;
        state->switch_on = false;
        state->current_chunk = 0;
        state->partial_byte = 0;
    } else if (key.code == KEY_CHAR && key.ch == 's') {
        state->previous_state = state->current_state;
        state->current_state = GOTO_SWITCH;
        state->current_switch = 0;
        state->switch_on = false;
        state->current_chunk = 0;
        state->partial_byte = 0;
    } else if (key.code == KEY_CHAR && key.ch == 'o') {
        state->previous_state = state->current_state;
        state->current_state = GOTO_OBJECT;
        state->current_switch = 0;
        state->switch_on = false;
        state->current_chunk = 0;
        state->partial_byte = 0;
    }

    if (is_tile) {
        if (key.code == KEY_LEFT || key.code == KEY_DOWN || key.code == KEY_UP || key.code == KEY_RIGHT) {
            int dx = (key.code == KEY_LEFT) ? -1 : (key.code == KEY_RIGHT) ? 1 : 0;
            int dy = (key.code == KEY_UP)   ? -1 : (key.code == KEY_DOWN)  ? 1 : 0;
            if      (key.mod == MOD_SHIFT)             { move(dx, dy); }
            else if (key.mod == MOD_ALT)               { stretch(dx, dy); }
            else if (key.mod == (MOD_ALT | MOD_SHIFT)) { shrink(dx, dy); }
        } else if (key.code == KEY_CHAR && key.mod == MOD_NONE) {
            /* SHIFT+hjkl (uppercase): move() */
            switch (key.ch) {
                case 'H': move(-1,  0); break;
                case 'J': move( 0, +1); break;
                case 'K': move( 0, -1); break;
                case 'L': move(+1,  0); break;
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

    if (key.code == KEY_CTRL) {
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
    } else if (key.code == KEY_ESC) {
        if (state->help) {
            state->help = false;
        } else {
            if ((state->partial_byte & 0xFF00) != 0) {
                state->partial_byte = 0;
            }
        }
    }

    return true;
}

static bool handle_tile_edit(Key key) {
    size_t *cursorlevel = &state->current_level;
    if (key.code == KEY_CHAR && isxdigit(key.ch)) {
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
            assert(writeRooms(&state->rooms));
            state->partial_byte = 0;
        } else {
            state->partial_byte = 0xFF00 | b;
        }
        return true;
    } else if (key.code == KEY_CHAR && key.ch == 'R') {
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
    } else if (key.code == KEY_CHAR && key.ch == '-') {
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
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_CHAR && key.ch == '+') {
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
        assert(writeRooms(&state->rooms));
    } else if (key.code == KEY_BS || key.code == KEY_DELETE) {
        if (state->partial_byte) {
            state->partial_byte = 0;
        } else {
            delete_item_at_cursor();
        }
    } else if (key.code == KEY_CHAR && key.ch == 'y') {
        struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
        int x = state->cursors[state->current_level].x;
        int y = state->cursors[state->current_level].y;
        if (state->clipboard.type == CLIP_OBJECT && state->clipboard.object.tiles) {
            free(state->clipboard.object.tiles);
            state->clipboard.object.tiles = NULL;
        } else if (state->clipboard.type == CLIP_SWITCH) {
            ARRAY_FREE(state->clipboard.sw.chunks);
        }
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
    } else if (key.code == KEY_CHAR && key.ch == 'p') {
        struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
        size_t x = state->cursors[state->current_level].x;
        size_t y = state->cursors[state->current_level].y;
        if (state->clipboard.type == CLIP_OBJECT) {
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
            assert(writeRooms(&state->rooms));
        } else if (state->clipboard.type == CLIP_SWITCH) {
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
            room->num_switches++;
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            assert(writeRooms(&state->rooms));
        }
    } else if (key.code == KEY_CTRL) {
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
                    room->switches = realloc(room->switches, (sw_idx + 1) * sizeof(struct SwitchObject));
                    assert(room->switches != NULL);
                    memset(room->switches + sw_idx, 0, sizeof(struct SwitchObject));
                    room->num_switches ++;
                    struct SwitchObject *sw = room->switches + sw_idx;
                    ARRAY_ADD(sw->chunks, ((struct SwitchChunk){ .type = PREAMBLE, .x = x, .y = y }));
                }
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                assert(writeRooms(&state->rooms));
                state->current_switch = sw_idx + 1;
                state->current_state = EDIT_SWITCHDETAILS;
                return true;
            }

            case 'O':
            {
                struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                size_t x = state->cursors[state->current_level].x;
                size_t y = state->cursors[state->current_level].y;
                bool found = false;
                for (size_t obj_j = 0; obj_j < room->num_objects; obj_j++) {
                    struct RoomObject *obj = room->objects + obj_j;
                    if ((obj->type == BLOCK && (int)x >= obj->x && (int)x < obj->x + obj->block.width &&
                            (int)y >= obj->y && (int)y < obj->y + obj->block.height) ||
                        (obj->type == SPRITE && (int)x == obj->x && (int)y == obj->y)) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    room->objects = realloc(room->objects, (room->num_objects + 1) * sizeof(struct RoomObject));
                    assert(room->objects != NULL);
                    size_t idx = room->num_objects;
                    memset(room->objects + idx, 0, sizeof(struct RoomObject));
                    room->objects[idx].type = BLOCK;
                    room->objects[idx].x = x;
                    room->objects[idx].y = y;
                    room->objects[idx].block.width = 1;
                    room->objects[idx].block.height = 1;
                    room->objects[idx].tiles = calloc(1, 1);
                    assert(room->objects[idx].tiles != NULL);
                    room->num_objects++;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    assert(writeRooms(&state->rooms));
                }
                return true;
            }
        }
    }
    return handle_normal(key);
}

static state_handler_fn state_handlers[NUM_STATES] = {
    [GOTO_OBJECT]      = handle_goto_object,
    [GOTO_SWITCH]      = handle_goto_switch,
    [GOTO_ROOM]        = handle_goto_room,
    [EDIT_ROOMNAME]    = handle_edit_roomname,
    [TOGGLE_DISPLAY]   = handle_toggle_display,
    [EDIT_ROOMDETAILS]                             = handle_edit_roomdetails,
    [EDIT_ROOMDETAILS_NUM]                         = handle_edit_roomdetails_num,
    [EDIT_ROOMDETAILS_ROOM]                        = handle_edit_roomdetails_room,
    [EDIT_SWITCHDETAILS]                           = handle_edit_switchdetails,
    [EDIT_SWITCHDETAILS_SELECT_CHUNK]              = handle_edit_switchdetails_select_chunk,
    [EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS]      = handle_edit_switchdetails_chunk_switch,
    [EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS_ROOM] = handle_edit_switchdetails_chunk_switch_room,
    [EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS]      = handle_edit_switchdetails_chunk_memory,
    [EDIT_SWITCHDETAILS_CHUNK_BLOCK_DETAILS]       = handle_edit_switchdetails_chunk_block,
    [EDIT_SWITCHDETAILS_CHUNK_OBJECT_DETAILS]      = handle_edit_switchdetails_chunk_object,
    [TILE_EDIT]        = handle_tile_edit,
    [NORMAL]           = handle_normal,
};

bool process_input(void) {
    struct pollfd fd = { .fd = STDIN_FILENO, .events = POLLIN };
    int ret = poll(&fd, 1, 1000);
    assert(ret >= 0 || errno == EINTR);
    if (!(fd.revents & POLLIN)) return false;
    char buf[64];
    ssize_t n = read(fd.fd, buf, sizeof(buf));
    assert(n >= 0);
    if (n == 0) return false;
    int i = 0;
    while (i < n) {
        Key key = parse_key(buf, &i, (int)n);
        assert(state->current_state < NUM_STATES);
        assert(state_handlers[state->current_state] != NULL);
        state_handlers[state->current_state](key);
    }
    return true;
}

bool update(void) { return false; }

