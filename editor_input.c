#include "editor_types.h"
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

/* Run play.sh detached. PLAY_ROOM tells it which room to start in (it swaps
 * that room into slot 1, the game's start room, in the copy the game runs). */
static void fork_play(void) {
    signal(SIGCHLD, SIG_IGN);
    pid_t child = fork();
    if (child == 0) {
        if (state != NULL) {
            char room[8];
            snprintf(room, sizeof room, "%zu", state->current_level);
            setenv("PLAY_ROOM", room, 1);
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

/* Helper macro for goto_room navigation */
#define GOTO_ROOM_NAVIGATE(dest_level) do { \
    room_create(dest_level); /* an absent room is created on the way in, so edits there are saved */ \
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
            if (key.ch == 'J') {
                /* ENTER (Ctrl-J = \n) */
                struct DecompressedRoom *room = &state->rooms.rooms[*cursorlevel].data;
                assert(C_ARRAY_LEN(state->room_name) <= C_ARRAY_LEN(room->name));
                push_undo((uint8_t)state->current_level, false);
                strncpy(room->name, state->room_name, C_ARRAY_LEN(room->name));
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

static bool handle_edit_switchdetails(Key key) {
    switch (key.code) {
        case KEY_UP:
        {
            struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
            push_undo((uint8_t)state->current_level, false);
            room->switches[state->current_switch - 1].chunks.data[0].side = TOP;
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            editor_save();
        } break;
        case KEY_DOWN:
        {
            struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
            push_undo((uint8_t)state->current_level, false);
            room->switches[state->current_switch - 1].chunks.data[0].side = BOTTOM;
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            editor_save();
        } break;
        case KEY_RIGHT:
        {
            struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
            push_undo((uint8_t)state->current_level, false);
            room->switches[state->current_switch - 1].chunks.data[0].side = RIGHT;
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            editor_save();
        } break;
        case KEY_LEFT:
        {
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
                    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                    push_undo((uint8_t)state->current_level, false);
                    room->switches[state->current_switch - 1].chunks.data[0].side = LEFT;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;

                case 'j':
                {
                    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                    push_undo((uint8_t)state->current_level, false);
                    room->switches[state->current_switch - 1].chunks.data[0].side = BOTTOM;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;

                case 'k':
                {
                    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                    push_undo((uint8_t)state->current_level, false);
                    room->switches[state->current_switch - 1].chunks.data[0].side = TOP;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;

                case 'l':
                {
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
                    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                    push_undo((uint8_t)state->current_level, false);
                    room->switches[state->current_switch - 1].chunks.data[0].room_entry = !room->switches[state->current_switch - 1].chunks.data[0].room_entry;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;

                case 's':
                {
                    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
                    push_undo((uint8_t)state->current_level, false);
                    room->switches[state->current_switch - 1].chunks.data[0].side = (room->switches[state->current_switch - 1].chunks.data[0].side + 1) % NUM_SIDES;
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
                        ARRAY_ADD(sw->chunks, ((struct SwitchChunk){ .type = TOGGLE_BLOCK }));
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
                    ARRAY_ADD(sw->chunks, ((struct SwitchChunk){ .type = TOGGLE_BLOCK }));
                    state->current_state = EDIT_SWITCHDETAILS_CHUNK_BLOCK_DETAILS;
                    state->switch_on = false;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
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
                    ARRAY_ADD(sw->chunks, ((struct SwitchChunk){ .type = TOGGLE_BLOCK }));
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

static void yank_at_cursor(void) {
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
    if (state->clipboard.type == CLIP_NONE) {
        uint8_t tile = room->tiles[TILE_IDX(x, y)];
        if (tile != BLANK_TILE) {
            state->clipboard.type = CLIP_TILE;
            state->clipboard.tile = tile;
        }
    }
}

static void yank_current_switch(void) {
    if (!state->current_switch) return;
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
}

static void delete_current_switch(void) {
    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
    size_t sw_i = state->current_switch - 1;
    push_undo((uint8_t)state->current_level, false);
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

static bool handle_edit_switchdetails_chunk_switch(Key key) {
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
                    size_t index = b;
                    if (state->debug.hex) {
                        index += 16 * (state->partial_byte & 0xFF);
                    } else {
                        index += 10 * (state->partial_byte & 0xFF);
                    }
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    assert(chunk->type == TOGGLE_BIT);
                    push_undo((uint8_t)state->current_level, false);
                    chunk->switch_idx = index;
                    state->partial_byte = 0;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                } else {
                    state->partial_byte = 0xFF00 | b;
                }
            } else switch (key.ch) {
                case 'r':
                    state->current_state = EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS_ROOM;
                    break;
                case 'o': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    assert(chunk->type == TOGGLE_BIT);
                    push_undo((uint8_t)state->current_level, false);
                    chunk->off = (chunk->off + 1) % 4;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;
                case 'n': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    assert(chunk->type == TOGGLE_BIT);
                    push_undo((uint8_t)state->current_level, false);
                    chunk->on = (chunk->on + 1) % 4;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;
                case 't': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    enum SwitchChunkType typ = chunk->type;
                    push_undo((uint8_t)state->current_level, false);
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
                case 't': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    enum SwitchChunkType typ = chunk->type;
                    push_undo((uint8_t)state->current_level, false);
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
                case 't': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    enum SwitchChunkType typ = chunk->type;
                    push_undo((uint8_t)state->current_level, false);
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

static bool handle_edit_switchdetails_chunk_object(Key key) {
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
                case 't': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    enum SwitchChunkType typ = chunk->type;
                    push_undo((uint8_t)state->current_level, false);
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
                    editor_save();
                }; break;
                case 'i': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    push_undo((uint8_t)state->current_level, false);
                    chunk->index = (chunk->index + 1) % 0x10;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    editor_save();
                }; break;
                case 's': {
                    struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                    struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                    push_undo((uint8_t)state->current_level, false);
                    chunk->test = (((chunk->test >> 4) + 1) % 4) << 4;
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
                case 'x': if (state->partial_byte) { state->partial_byte = 0; break; } yank_chunk(); delete_current_chunk(); break;
                case '+': chunk_swap_next(); break;
                case '-': chunk_swap_prev(); break;
                case 'y': yank_chunk(); break;
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
                            *cursorlevel = level;
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
                            *cursorlevel = level;
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
                            *cursorlevel = level;
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
                            *cursorlevel = level;
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
                    *cursorlevel = level;
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
                    *cursorlevel = level;
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
                    *cursorlevel = level;
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
                    *cursorlevel = level;
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
                } else {
                    static const char *sprite_names[] = {
                        "Shark", "Mummy", "Blue man", "Wolf",
                        "R2D2", "Dinosaur", "Rat", "Shotgun lady",
                    };
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
                    if (found_obj->type == BLOCK) {
                        free(found_obj->tiles);
                        found_obj->tiles = NULL;
                        found_obj->type = SPRITE;
                        found_obj->sprite.type = SHARK;
                        found_obj->sprite.damage = 1;
                        snprintf(state->status_message, sizeof(state->status_message),
                                 "Sprite: %s", sprite_names[SHARK]);
                    } else if ((int)found_obj->sprite.type + 1 >= NUM_SPRITE_TYPES) {
                        found_obj->type = BLOCK;
                        found_obj->block.width = 1;
                        found_obj->block.height = 1;
                        found_obj->tiles = malloc(1);
                        assert(found_obj->tiles != NULL);
                        found_obj->tiles[0] = room->tiles[TILE_IDX(found_obj->x, found_obj->y)];
                        snprintf(state->status_message, sizeof(state->status_message), "Block");
                    } else {
                        found_obj->sprite.type = (SpriteType)(found_obj->sprite.type + 1);
                        snprintf(state->status_message, sizeof(state->status_message),
                                 "Sprite: %s", sprite_names[found_obj->sprite.type]);
                    }
                }
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                editor_save();
                done_cycle:
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
    [EDIT_SWITCHDETAILS_SELECT_CHUNK]              = handle_edit_switchdetails_select_chunk,
    [EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS]      = handle_edit_switchdetails_chunk_switch,
    [EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS_ROOM] = handle_edit_switchdetails_chunk_switch_room,
    [EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS]      = handle_edit_switchdetails_chunk_memory,
    [EDIT_SWITCHDETAILS_CHUNK_BLOCK_DETAILS]       = handle_edit_switchdetails_chunk_block,
    [EDIT_SWITCHDETAILS_CHUNK_OBJECT_DETAILS]      = handle_edit_switchdetails_chunk_object,
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
    {"UNKNOWN_b", 'b'}, {"UNKNOWN_c", 'c'}, {"UNKNOWN_e", 'e'}, {"UNKNOWN_f", 'f'},
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

