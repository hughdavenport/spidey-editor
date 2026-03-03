#include "editor_types.h"
#include <ctype.h>
#include <poll.h>
#include <signal.h>
#include <string.h>

typedef bool (*state_handler_fn)(char *buf, int *i, int n);

static bool handle_goto_object(char *buf, int *i, int n);
static bool handle_goto_switch(char *buf, int *i, int n);
static bool handle_goto_room(char *buf, int *i, int n);
static bool handle_edit_roomname(char *buf, int *i, int n);
static bool handle_toggle_display(char *buf, int *i, int n);
static bool handle_edit_roomdetails(char *buf, int *i, int n);
static bool handle_edit_roomdetails_num(char *buf, int *i, int n);
static bool handle_edit_roomdetails_room(char *buf, int *i, int n);
static bool handle_edit_switchdetails(char *buf, int *i, int n);
static bool handle_edit_switchdetails_select_chunk(char *buf, int *i, int n);
static bool handle_edit_switchdetails_chunk_switch(char *buf, int *i, int n);
static bool handle_edit_switchdetails_chunk_switch_room(char *buf, int *i, int n);
static bool handle_edit_switchdetails_chunk_memory(char *buf, int *i, int n);
static bool handle_edit_switchdetails_chunk_block(char *buf, int *i, int n);
static bool handle_edit_switchdetails_chunk_object(char *buf, int *i, int n);
static bool handle_normal(char *buf, int *i, int n);
static bool handle_tile_edit(char *buf, int *i, int n);

static bool handle_goto_object(char *buf, int *i, int n) {
    (void)n;
    v2 *cursor = &state->cursors[state->current_level];
    size_t *cursorlevel = &state->current_level;
    if (isxdigit(buf[*i])) {
        struct DecompresssedRoom *room = &state->rooms.rooms[*cursorlevel].data;
        if (room->num_objects > 15) {
            // FIXME support 2 digit nums
            UNREACHABLE();
        }
        size_t id;
        if (buf[*i] > '9') {
            id = tolower(buf[*i]) - 'a' + 10;
        } else {
            id = buf[*i] - '0';
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
    } else if (buf[*i] == ESCAPE) {
        if (state->help) {
            state->help = false;
        } else {
            state->current_state = state->previous_state;
            state->current_switch = 0;
            state->switch_on = false;
            state->current_chunk = 0;
            state->previous_state = NORMAL;
        }
    } else if (iscntrl(buf[*i])) {
        switch (buf[*i] + 'A' - 1) {
            case '_': state->help = !state->help; break;
            case 'H': state->debug.hex = !state->debug.hex; break;
        }
    } else if (buf[*i] == 'q') {
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
    } else if (buf[*i] == '?') {
        state->help = !state->help;
    }
    (*i)++;
    return true;
}

static bool handle_goto_switch(char *buf, int *i, int n) {
    (void)n;
    v2 *cursor = &state->cursors[state->current_level];
    size_t *cursorlevel = &state->current_level;
    if (isxdigit(buf[*i])) {
        struct DecompresssedRoom *room = &state->rooms.rooms[*cursorlevel].data;
        if (room->num_switches > 15) {
            // FIXME support 2 digit nums
            UNREACHABLE();
        }
        size_t id;
        if (buf[*i] > '9') {
            id = tolower(buf[*i]) - 'a' + 10;
        } else {
            id = buf[*i] - '0';
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
    } else if (buf[*i] == ESCAPE) {
        if (state->help) {
            state->help = false;
        } else {
            state->current_state = state->previous_state;
            state->current_switch = 0;
            state->switch_on = false;
            state->current_chunk = 0;
            state->previous_state = NORMAL;
        }
    } else if (iscntrl(buf[*i])) {
        switch (buf[*i] + 'A' - 1) {
            case '_': state->help = !state->help; break;
            case 'H': state->debug.hex = !state->debug.hex; break;
        }
    } else if (buf[*i] == 'q') {
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
    } else if (buf[*i] == '?') {
        state->help = !state->help;
    }
    (*i)++;
    return true;
}

static bool handle_goto_room(char *buf, int *i, int n) {
    size_t *cursorlevel = &state->current_level;
    if (state->partial_byte && ((state->debug.hex && isxdigit(buf[*i])) || (!state->debug.hex && isdigit(buf[*i])))) {
        int digit;
        if (buf[*i] > '9') {
            digit = tolower(buf[*i]) - 'a' + 10;
        } else {
            digit = buf[*i] - '0';
        }
        size_t room = digit;
        if (state->debug.hex) {
            room += 16 * (state->partial_byte & 0xFF);
        } else {
            room += 10 * (state->partial_byte & 0xFF);
        }
        if (room < C_ARRAY_LEN(state->rooms.rooms)) {
            *cursorlevel = room;
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
    } else if (state->roomname_cursor == 0 && state->partial_byte == 0 && buf[*i] >= '0' &&
                buf[*i] <= (state->debug.hex ? '3' : '6')) {
        state->partial_byte = 0xFF00 | (buf[*i] - '0');
    } else if (buf[*i] == 0x7f) {
        if (state->roomname_cursor) {
            state->room_name[--state->roomname_cursor] = 0;
        } else {
            state->partial_byte = 0;
        }
    } else if (buf[*i] == ESCAPE) {
        if (*i + 1 < n) {
            switch (buf[*i + 1]) {
                case '[':
                    if (*i + 2 < n) {
                        // CSI sequence
                        *i += 2;
                        uint8_t arg = 0;
                        while (*i < n) {
                            if (isdigit(buf[*i])) {
                                arg = 10 * arg + buf[*i] - '0';
                            } else if (buf[*i] == ';') {
                                arg = 0;
                            } else {
                                switch (buf[*i]) {
                                    case '~':
                                    {
                                        switch (arg) {
                                            case 3:
                                                if (state->roomname_cursor) {
                                                    state->room_name[--state->roomname_cursor] = 0;
                                                } else {
                                                    state->partial_byte = 0;
                                                }
                                                break;

                                            default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[*i], arg);
                                        }
                                    }; break;

                                    case 'A':
                                    {
                                        *cursorlevel = state->rooms.rooms[*cursorlevel].data.room_north;
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
                                    }; break;

                                    case 'B':
                                    {
                                        *cursorlevel = state->rooms.rooms[*cursorlevel].data.room_south;
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
                                    }; break;

                                    case 'C':
                                    {
                                        *cursorlevel = state->rooms.rooms[*cursorlevel].data.room_east;
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
                                    }; break;

                                    case 'D':
                                    {
                                        *cursorlevel = state->rooms.rooms[*cursorlevel].data.room_west;
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
                                    }; break;

                                    default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[*i], arg);
                                }
                                break;
                            }
                            (*i)++;
                        }
                    }
                    break;

                case 'h':
                {
                    *cursorlevel = state->rooms.rooms[*cursorlevel].data.room_west;
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
                }; break;

                case 'j':
                {
                    *cursorlevel = state->rooms.rooms[*cursorlevel].data.room_south;
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
                }; break;

                case 'k':
                {
                    *cursorlevel = state->rooms.rooms[*cursorlevel].data.room_north;
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
                }; break;

                case 'l':
                {
                    *cursorlevel = state->rooms.rooms[*cursorlevel].data.room_east;
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
                }; break;

            }
        } else if (state->help) {
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
    } else if (iscntrl(buf[*i])) {
        switch (buf[*i] + 'A' - 1) {
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
            {
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
            }; break;

        }
    } else {
        if (buf[*i] == '[') {
            if (*i + 1 < n) {
                // CSI sequence
                *i += 1;
                uint8_t arg = 0;
                while (*i < n) {
                    if (isdigit(buf[*i])) {
                        arg = 10 * arg + buf[*i] - '0';
                    } else if (buf[*i] == ';') {
                        arg = 0;
                    } else {
                        if (arg == 3 && buf[*i] == '~') {
                            state->partial_byte = 0;
                        } else {
                            fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[*i], arg);
                        }
                        break;
                    }
                    (*i)++;
                }
            } else {
                UNREACHABLE();
            }
        } else if (state->partial_byte == 0 && isprint(buf[*i])) {
            if (state->roomname_cursor < C_ARRAY_LEN(state->room_name) - 1) {
                state->room_name[state->roomname_cursor++] = buf[*i];

                bool possible = false;
                bool duplicate = false;
                size_t matching_room = 0;
                if (state->roomname_cursor) {
                    for (size_t _room = 0; _room < C_ARRAY_LEN(state->rooms.rooms); _room ++) {
                        struct DecompresssedRoom *room = &state->rooms.rooms[_room].data;
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
                    *cursorlevel = matching_room;
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
            }
        }
    }
    (*i)++;
    return true;
}

static bool handle_edit_roomname(char *buf, int *i, int n) {
    size_t *cursorlevel = &state->current_level;
    if (state->help) {
        switch (buf[*i]) {
            case ESCAPE:
            case 'q':
            case 0x1f:
                state->help = false;
                break;
        }
        (*i)++;
        return true;
    }

    if (buf[*i] == '\n') {
        // ENTER
        struct DecompresssedRoom *room = &state->rooms.rooms[*cursorlevel].data;
        assert(C_ARRAY_LEN(state->room_name) <= C_ARRAY_LEN(room->name));
        strncpy(room->name, state->room_name, C_ARRAY_LEN(room->name));
        state->current_state = state->previous_state;
        state->current_switch = 0;
        state->switch_on = false;
        state->current_chunk = 0;
        state->previous_state = NORMAL;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (isprint(buf[*i])) {
        if (state->roomname_cursor < C_ARRAY_LEN(state->room_name) - 1) {
            state->room_name[state->roomname_cursor++] = buf[*i];
        }
    } else if (buf[*i] == ESCAPE) {
        if (*i + 1 < n) {
            switch (buf[*i + 1]) {
                case '[':
                    if (*i + 2 < n) {
                        // CSI sequence, with leading esc
                        *i += 2;
                        uint8_t arg = 0;
                        while (*i < n) {
                            if (isdigit(buf[*i])) {
                                arg = 10 * arg + buf[*i] - '0';
                            } else if (buf[*i] == ';') {
                                fprintf(stderr, "%s:%d: UNIMPLEMENTED: multiple csi args", __FILE__, __LINE__);
                                arg = 0;
                            } else {
                                switch (buf[*i]) {
                                    case '~':
                                    {
                                        switch (arg) {
                                            case 1:
                                            {
                                                // HOME
                                                state->roomname_cursor = 0;
                                            }; break;

                                            case 3:
                                            {
                                                // DELETE
                                                for (size_t c = state->roomname_cursor; c < C_ARRAY_LEN(state->room_name) - 1; c ++) {
                                                    state->room_name[c] = state->room_name[c + 1];
                                                    state->room_name[c + 1] = '\0';
                                                }
                                                if (state->roomname_cursor) {
                                                    state->roomname_cursor--;
                                                }
                                            }; break;

                                            case 4:
                                            {
                                                // END
                                                state->roomname_cursor = strlen(state->room_name);
                                            }; break;

                                            default:
                                                fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator ~ arg %d", __FILE__, __LINE__, arg);
                                        }
                                    }; break;

                                    case 'C':
                                    {
                                        // RIGHT ARROW
                                        if (state->roomname_cursor < C_ARRAY_LEN(state->room_name) - 1 && state->room_name[state->roomname_cursor]) {
                                            state->roomname_cursor ++;
                                        }
                                    }; break;

                                    case 'D':
                                    {
                                        // LEFT ARROW
                                        if (state->roomname_cursor > 0) {
                                            state->roomname_cursor --;
                                        }
                                    }; break;

                                    default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[*i], arg);
                                }
                                (*i)++;
                                break;
                            }
                            (*i)++;
                        }
                    } else {
                        UNREACHABLE();
                    }
                    break;
            }
            return true;
        } else {
            state->current_state = state->previous_state;
            state->current_switch = 0;
            state->switch_on = false;
            state->current_chunk = 0;
            state->previous_state = NORMAL;
        }
    } else if (buf[*i] == 0x7f) {
        if (state->roomname_cursor) {
            for (size_t c = state->roomname_cursor - 1; c < C_ARRAY_LEN(state->room_name) - 1; c ++) {
                state->room_name[c] = state->room_name[c + 1];
                state->room_name[c + 1] = '\0';
            }
            if (state->roomname_cursor) {
                state->roomname_cursor--;
            }
        }
    } else if (iscntrl(buf[*i])) {
        switch (buf[*i] + 'A' - 1) {
            case 'H': state->debug.hex = !state->debug.hex; break;
            case '_': state->help = !state->help; break;
        }
    }
    (*i)++;
    return true;
}

static bool handle_toggle_display(char *buf, int *i, int n) {
    (void)n;
    if (state->help) {
        switch (buf[*i]) {
            case 0x08:
                state->debug.hex = !state->debug.hex;
                break;
            case ESCAPE:
            case 'q':
            case 0x1f:
            case '?':
                state->help = false;
                break;
        }
        (*i)++;
        return true;
    }

    uint8_t changed = 1;
    switch (buf[*i]) {
        case 'a': debugalltoggle(); break;
        case 'd': state->debug.data = !state->debug.data; break;
        case 'n': state->debug.neighbours = !state->debug.neighbours; break;
        case 'o': state->debug.objects = !state->debug.objects; break;
        case 'p': state->debug.pos = !state->debug.pos; break;
        case 's': state->debug.switches = !state->debug.switches; break;
        case 'u': state->debug.unknowns = !state->debug.unknowns; break;

        case 'q':
        case ESCAPE:
            break;

        case 0x08:
            state->debug.hex = !state->debug.hex;
            break;

        case '?':
        case 0x1F:
            state->help = !state->help;
            fallthrough;
        default: fprintf(stderr, "buf[*i] = %02x\n", buf[*i]); UNREACHABLE();changed = 0;
    }
    (*i)++;
    if (changed) {
        state->current_state = state->previous_state;
        state->current_switch = 0;
        state->switch_on = false;
        state->current_chunk = 0;
        state->previous_state = NORMAL;
    }
    return true;
}

static bool handle_edit_roomdetails(char *buf, int *i, int n) {
    if (buf[*i] == ESCAPE && *i + 1 < n && buf[*i + 1] == '[') {
        // CSI sequence
        *i += 2;
        uint8_t arg = 0;
        while (*i < n) {
            if (isdigit(buf[*i])) {
                arg = 10 * arg + buf[*i] - '0';
            } else if (buf[*i] == ';') {
                arg = 0;
            } else {
                switch (buf[*i]) {
                    case 'A':
                    case 'B':
                    case 'C':
                    case 'D':
                        state->current_state = EDIT_ROOMDETAILS_ROOM;
                        state->current_switch = 0;
                        state->switch_on = false;
                        state->current_chunk = 0;
                        state->room_detail = buf[*i];
                        break;
                }
                break;
            }
            (*i)++;
        }
    } else {
        switch (buf[*i]) {
            case 'h':
            case 'j':
            case 'k':
            case 'l':
                state->room_detail = buf[*i];
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
            {
                state->room_detail = buf[*i];
                state->current_state = EDIT_ROOMDETAILS_NUM;
                state->current_switch = 0;
                state->switch_on = false;
                state->current_chunk = 0;
            }; break;

            case 'p':
            {
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
            }; break;

            case 'q':
            case ESCAPE:
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

            default:
                if (buf[*i] == '?') {
                    state->help = !state->help;
                } else if (iscntrl(buf[*i])) {
                    switch (buf[*i] + 'A' - 1) {
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
                    }
                }
        }
    }
    (*i)++;
    return true;
}

static bool handle_edit_roomdetails_num(char *buf, int *i, int n) {
    if ((state->debug.hex && isxdigit(buf[*i])) || (!state->debug.hex && isdigit(buf[*i]))) {
        int digit;
        if (buf[*i] > '9') {
            digit = tolower(buf[*i]) - 'a' + 10;
        } else {
            digit = buf[*i] - '0';
        }
        if (state->partial_byte) {
            size_t value = digit;
            if (state->debug.hex) {
                value += 16 * (state->partial_byte & 0xFF);
            } else {
                value += 10 * (state->partial_byte & 0xFF);
            }

            struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
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
    } else if (buf[*i] == 0x7f) {
        state->partial_byte = 0;
    } else if (buf[*i] == ESCAPE) {
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
    } else if (iscntrl(buf[*i])) {
        switch (buf[*i] + 'A' - 1) {
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
        }
    } else if (buf[*i] == 'q') {
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
    } else if (buf[*i] == '?') {
        state->help = !state->help;
    } else if (buf[*i] == 'p') {
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
    } else {
        switch (buf[*i]) {
            case '[':
                if (*i + 1 < n) {
                    // CSI sequence
                    *i += 1;
                    uint8_t arg = 0;
                    while (*i < n) {
                        if (isdigit(buf[*i])) {
                            arg = 10 * arg + buf[*i] - '0';
                        } else if (buf[*i] == ';') {
                            arg = 0;
                        } else {
                            if (arg == 3 && buf[*i] == '~') {
                                state->partial_byte = 0;
                            } else {
                                fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[*i], arg);
                            }
                            break;
                        }
                        (*i)++;
                    }
                } else {
                    UNREACHABLE();
                }
                break;
        }
    }
    (*i)++;
    return true;
}

static bool handle_edit_roomdetails_room(char *buf, int *i, int n) {
    if (state->partial_byte && ((state->debug.hex && isxdigit(buf[*i])) || (!state->debug.hex && isdigit(buf[*i])))) {
        int digit;
        if (buf[*i] > '9') {
            digit = tolower(buf[*i]) - 'a' + 10;
        } else {
            digit = buf[*i] - '0';
        }
        size_t room_id = digit;
        if (state->debug.hex) {
            room_id += 16 * (state->partial_byte & 0xFF);
        } else {
            room_id += 10 * (state->partial_byte & 0xFF);
        }
        if (room_id < C_ARRAY_LEN(state->rooms.rooms)) {
            struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
            switch (state->room_detail) {
                case 'A':
                case 'k':
                    room->room_north = room_id;
                    break;

                case 'B':
                case 'j':
                    room->room_south = room_id;
                    break;

                case 'C':
                case 'l':
                    room->room_east = room_id;
                    break;

                case 'D':
                case 'h':
                    room->room_west = room_id;
                    break;

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
    } else if (state->roomname_cursor == 0 && state->partial_byte == 0 && buf[*i] >= '0' &&
                buf[*i] <= (state->debug.hex ? '3' : '6')) {
        state->partial_byte = 0xFF00 | (buf[*i] - '0');
    } else if (buf[*i] == 0x7f) {
        if (state->roomname_cursor) {
            state->room_name[--state->roomname_cursor] = 0;
        } else {
            state->partial_byte = 0;
        }
    } else if (buf[*i] == ESCAPE) {
        if (*i + 1 < n) {
            if (buf[*i + 1] == '[') {
                if (*i + 2 < n) {
                    // CSI sequence
                    *i += 2;
                    uint8_t arg = 0;
                    while (*i < n) {
                        if (isdigit(buf[*i])) {
                            arg = 10 * arg + buf[*i] - '0';
                        } else if (buf[*i] == ';') {
                            arg = 0;
                        } else {
                            switch (buf[*i]) {
                                case '~':
                                {
                                    switch (arg) {
                                        case 3:
                                            if (state->roomname_cursor) {
                                                state->room_name[--state->roomname_cursor] = 0;
                                            } else {
                                                state->partial_byte = 0;
                                            }
                                            break;

                                        default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[*i], arg);
                                    }
                                }; break;

                                default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[*i], arg);
                            }
                            break;
                        }
                        (*i)++;
                    }
                }
            } else {

            }
        } else if (state->help) {
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
    } else if (iscntrl(buf[*i])) {
        switch (buf[*i] + 'A' - 1) {
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
    } else {
        if (buf[*i] == '[') {
            if (*i + 1 < n) {
                // CSI sequence
                *i += 1;
                uint8_t arg = 0;
                while (*i < n) {
                    if (isdigit(buf[*i])) {
                        arg = 10 * arg + buf[*i] - '0';
                    } else if (buf[*i] == ';') {
                        arg = 0;
                    } else {
                        if (arg == 3 && buf[*i] == '~') {
                            if (state->roomname_cursor) {
                                state->room_name[--state->roomname_cursor] = 0;
                            } else {
                                state->partial_byte = 0;
                            }
                        } else {
                            fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[*i], arg);
                        }
                        break;
                    }
                    (*i)++;
                }
            } else {
                UNREACHABLE();
            }
        } else if (state->partial_byte == 0 && isprint(buf[*i])) {
            if (state->roomname_cursor < C_ARRAY_LEN(state->room_name) - 1) {
                state->room_name[state->roomname_cursor++] = buf[*i];

                bool possible = false;
                bool duplicate = false;
                size_t matching_room = 0;
                if (state->roomname_cursor) {
                    for (size_t _room = 0; _room < C_ARRAY_LEN(state->rooms.rooms); _room ++) {
                        struct DecompresssedRoom *room = &state->rooms.rooms[_room].data;
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
                    struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
                    switch (state->room_detail) {
                        case 'A':
                        case 'k':
                            room->room_north = matching_room;
                            break;

                        case 'B':
                        case 'j':
                            room->room_south = matching_room;
                            break;

                        case 'C':
                        case 'l':
                            room->room_east = matching_room;
                            break;

                        case 'D':
                        case 'h':
                            room->room_west = matching_room;
                            break;

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
        }
    }
    (*i)++;
    return true;
}

static bool handle_edit_switchdetails(char *buf, int *i, int n) {
    if (buf[*i] == ESCAPE && *i + 1 < n && buf[*i + 1] == '[') {
        // CSI sequence
        *i += 2;
        uint8_t arg = 0;
        while (*i < n) {
            if (isdigit(buf[*i])) {
                arg = 10 * arg + buf[*i] - '0';
            } else if (buf[*i] == ';') {
                arg = 0;
            } else {
                switch (buf[*i]) {
                    case '~':
                    {
                        switch (arg) {
                            case 3:
                            {
                                if (!state->current_switch) UNREACHABLE();
                                struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
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
                            }; break;

                            default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[*i], arg);
                        }
                    }; break;

                    case 'A':
                    {
                        struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
                        room->switches[state->current_switch - 1].chunks.data[0].side = TOP;
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        assert(writeRooms(&state->rooms));
                    }; break;

                    case 'B':
                    {
                        struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
                        room->switches[state->current_switch - 1].chunks.data[0].side = BOTTOM;
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        assert(writeRooms(&state->rooms));
                    }; break;

                    case 'C':
                    {
                        struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
                        room->switches[state->current_switch - 1].chunks.data[0].side = RIGHT;
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        assert(writeRooms(&state->rooms));
                    }; break;

                    case 'D':
                    {
                        struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
                        room->switches[state->current_switch - 1].chunks.data[0].side = LEFT;
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        assert(writeRooms(&state->rooms));
                    }; break;

                    default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[*i], arg);
                }
                break;
            }
            (*i)++;
        }
    } else if (buf[*i] == 'p') {
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
    } else {
        switch (buf[*i]) {
            case 0x7f:
            {
                if (!state->current_switch) UNREACHABLE();
                struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
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
            }; break;

            case '+':
            {
                if (state->current_switch) {
                    struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
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
                    struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
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
                struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
                room->switches[state->current_switch - 1].chunks.data[0].side = LEFT;
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                assert(writeRooms(&state->rooms));
            }; break;

            case 'j':
            {
                struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
                room->switches[state->current_switch - 1].chunks.data[0].side = BOTTOM;
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                assert(writeRooms(&state->rooms));
            }; break;

            case 'k':
            {
                struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
                room->switches[state->current_switch - 1].chunks.data[0].side = TOP;
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                assert(writeRooms(&state->rooms));
            }; break;

            case 'l':
            {
                struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
                room->switches[state->current_switch - 1].chunks.data[0].side = RIGHT;
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                assert(writeRooms(&state->rooms));
            }; break;

            case 'o':
            {
                struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
                room->switches[state->current_switch - 1].chunks.data[0].one_time_use = !room->switches[state->current_switch - 1].chunks.data[0].one_time_use;
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                assert(writeRooms(&state->rooms));
            }; break;

            case 'e':
            {
                struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
                room->switches[state->current_switch - 1].chunks.data[0].room_entry = !room->switches[state->current_switch - 1].chunks.data[0].room_entry;
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                assert(writeRooms(&state->rooms));
            }; break;

            case 's':
            {
                struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
                room->switches[state->current_switch - 1].chunks.data[0].side = (room->switches[state->current_switch - 1].chunks.data[0].side + 1) % NUM_SIDES;
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                assert(writeRooms(&state->rooms));
            }; break;

            case 'c':
            {

                struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
                struct SwitchObject *sw = room->switches + state->current_switch - 1;
                if (sw->chunks.length == 1) {
                    // create new one
                    struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
                    struct SwitchObject *sw = room->switches + state->current_switch - 1;
                    state->current_chunk = sw->chunks.length;
                    ARRAY_ADD(sw->chunks, ((struct SwitchChunk){ .type = TOGGLE_BLOCK }));
                    state->current_state = EDIT_SWITCHDETAILS_CHUNK_BLOCK_DETAILS;
                    state->switch_on = false;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    assert(writeRooms(&state->rooms));
                } else if (sw->chunks.length == 2) {
                    // The first chunk is the preamble, uneditable as a chunk, only as a switch
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
                struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
                struct SwitchObject *sw = room->switches + state->current_switch - 1;
                if (sw->chunks.length == 16) {
                    (*i)++;
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
            case ESCAPE:
                if (state->help) {
                    state->help = false;
                } else {
                    state->current_state = state->previous_state;
                    state->current_switch = 0;
                    state->switch_on = false;
                    state->previous_state = TILE_EDIT;
                }
                break;

            default:
                if (buf[*i] == '?') {
                    state->help = !state->help;
                } else if (iscntrl(buf[*i])) {
                    switch (buf[*i] + 'A' - 1) {
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
                    }
                }
        }
    }
    (*i)++;
    return true;
}

static bool handle_edit_switchdetails_select_chunk(char *buf, int *i, int n) {
    (void)n;
    size_t *cursorlevel = &state->current_level;
    if (isxdigit(buf[*i])) {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        if (sw->chunks.length > 16) {
            // FIXME support 2 digit nums, remove define above when done (used in staticdefines)
            fprintf(stderr, "%s:%d: UNIMPLEMENTED: support two digit numbers", __FILE__, __LINE__);
            UNREACHABLE();
        }
        size_t id;
        if (buf[*i] > '9') {
            id = tolower(buf[*i]) - 'a' + 10;
        } else {
            id = buf[*i] - '0';
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
    } else if (buf[*i] == 0x7f) {
        state->partial_byte = 0;
    } else if (buf[*i] == ESCAPE) {
        if (state->help) {
            state->help = false;
        } else {
            state->current_state = EDIT_SWITCHDETAILS;
        }
    } else if (iscntrl(buf[*i])) {
        switch (buf[*i] + 'A' - 1) {
            case '_': state->help = !state->help; break;
            case 'H': state->debug.hex = !state->debug.hex; break;
        }
    } else if (buf[*i] == 'q') {
        if (state->help) {
            state->help = !state->help;
        } else {
            state->current_state = EDIT_SWITCHDETAILS;
        }
    } else if (buf[*i] == '?') {
        state->help = !state->help;
    } else if (buf[*i] == '+') {
        // create new one
        struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
        struct SwitchObject *sw = room->switches + state->current_switch - 1;
        if (sw->chunks.length == 16) {
            (*i)++;
            return true;
        }
        ARRAY_ADD(sw->chunks, ((struct SwitchChunk){ .type = TOGGLE_BLOCK }));
        state->switch_on = false;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (buf[*i] == 'p') {
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
    (*i)++;
    return true;
}

static bool handle_edit_switchdetails_chunk_switch(char *buf, int *i, int n) {
    size_t *cursorlevel = &state->current_level;
    if (isxdigit(buf[*i])) {
        uint8_t b = 0;
        if (isdigit(buf[*i])) {
            b = *buf - '0';
        } else {
            b = 10 + tolower(buf[*i]) - 'a';
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
    } else if (buf[*i] == 'r') {
        state->current_state = EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS_ROOM;
    } else if (buf[*i] == 'o') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BIT);
        chunk->off = (chunk->off + 1) % 4;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (buf[*i] == 'n') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BIT);
        chunk->on = (chunk->on + 1) % 4;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (buf[*i] == 0x7f) {
        if (state->partial_byte) {
            state->partial_byte = 0;
        } else {
            if (!state->current_switch) UNREACHABLE();
            if (!state->current_chunk) UNREACHABLE();
            struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
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
    } else if (buf[*i] == ESCAPE) {
        if (*i + 1 < n && buf[*i + 1] == '[' && *i + 2 < n) {
            // CSI sequence
            *i += 2;
            uint8_t arg = 0;
            while (*i < n) {
                if (isdigit(buf[*i])) {
                    arg = 10 * arg + buf[*i] - '0';
                } else if (buf[*i] == ';') {
                    arg = 0;
                } else {
                    switch (buf[*i]) {
                        case '~':
                        {
                            switch (arg) {
                                case 3:
                                {
                                    if (state->partial_byte) {
                                        state->partial_byte = 0;
                                    } else {
                                        if (!state->current_switch) UNREACHABLE();
                                        if (!state->current_chunk) UNREACHABLE();
                                        struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
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
                                }; break;

                                default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[*i], arg);
                            }
                        }; break;

                        default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[*i], arg);
                    }
                    break;
                }
                (*i)++;
            }
        } else if (state->help) {
            state->help = false;
        } else {
            if (state->partial_byte) {
                state->partial_byte = 0;
            } else {
                struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                if (sw->chunks.length <= 2) {
                    state->current_state = EDIT_SWITCHDETAILS;
                } else {
                    state->current_state = EDIT_SWITCHDETAILS_SELECT_CHUNK;
                }
                state->current_chunk = 0;
            }
        }
    } else if (buf[*i] == 't') {
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
    } else if (iscntrl(buf[*i])) {
        switch (buf[*i] + 'A' - 1) {
            case '_': state->help = !state->help; break;
            case 'H': state->debug.hex = !state->debug.hex; break;
        }
    } else if (buf[*i] == 'q') {
        if (state->help) {
            state->help = !state->help;
        } else {
            if (state->partial_byte) {
                state->partial_byte = 0;
            } else {
                struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                if (sw->chunks.length <= 2) {
                    state->current_state = EDIT_SWITCHDETAILS;
                } else {
                    state->current_state = EDIT_SWITCHDETAILS_SELECT_CHUNK;
                }
                state->current_chunk = 0;
            }
        }
    } else if (buf[*i] == '?') {
        state->help = !state->help;
    } else if (buf[*i] == '+') {
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
    } else if (buf[*i] == '-') {
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
    } else if (buf[*i] == 'p') {
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
    (*i)++;
    return true;
}

static bool handle_edit_switchdetails_chunk_switch_room(char *buf, int *i, int n) {
    size_t *cursorlevel = &state->current_level;
    if (state->partial_byte && ((state->debug.hex && isxdigit(buf[*i])) || (!state->debug.hex && isdigit(buf[*i])))) {
        int digit;
        if (buf[*i] > '9') {
            digit = tolower(buf[*i]) - 'a' + 10;
        } else {
            digit = buf[*i] - '0';
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
            strncpy(state->room_name, state->rooms.rooms[*cursorlevel].data.name, C_ARRAY_LEN(state->room_name));
            for (size_t i = 0; i < C_ARRAY_LEN(state->room_name); i ++) {
                if (state->room_name[C_ARRAY_LEN(state->room_name) - i] == '\0') continue;
                if (state->room_name[C_ARRAY_LEN(state->room_name) - i] == ' ') {
                    state->room_name[C_ARRAY_LEN(state->room_name) - i] = '\0';
                } else {
                    break;
                }
            }
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            assert(writeRooms(&state->rooms));
        }
    } else if (state->roomname_cursor == 0 && state->partial_byte == 0 && buf[*i] >= '0' &&
                buf[*i] <= (state->debug.hex ? '3' : '6')) {
        state->partial_byte = 0xFF00 | (buf[*i] - '0');
    } else if (buf[*i] == 0x7f) {
        if (state->roomname_cursor) {
            state->room_name[--state->roomname_cursor] = 0;
        } else {
            state->partial_byte = 0;
        }
    } else if (buf[*i] == ESCAPE) {
        if (*i + 1 < n) {
            if (buf[*i + 1] == '[') {
                if (*i + 2 < n) {
                    // CSI sequence
                    *i += 2;
                    uint8_t arg = 0;
                    while (*i < n) {
                        if (isdigit(buf[*i])) {
                            arg = 10 * arg + buf[*i] - '0';
                        } else if (buf[*i] == ';') {
                            arg = 0;
                        } else {
                            switch (buf[*i]) {
                                case '~':
                                {
                                    switch (arg) {
                                        case 3:
                                            if (state->roomname_cursor) {
                                                state->room_name[--state->roomname_cursor] = 0;
                                            } else {
                                                state->partial_byte = 0;
                                            }
                                            break;

                                        default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[*i], arg);
                                    }
                                }; break;

                                default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[*i], arg);
                            }
                            break;
                        }
                        (*i)++;
                    }
                }
            } else {

            }
        } else if (state->help) {
            state->help = false;
        } else {
            state->current_state = EDIT_SWITCHDETAILS_CHUNK_SWITCH_DETAILS;
            state->partial_byte = 0;
            state->roomname_cursor = 0;
            strncpy(state->room_name, state->rooms.rooms[*cursorlevel].data.name, C_ARRAY_LEN(state->room_name));
            for (size_t i = 0; i < C_ARRAY_LEN(state->room_name); i ++) {
                if (state->room_name[C_ARRAY_LEN(state->room_name) - i] == '\0') continue;
                if (state->room_name[C_ARRAY_LEN(state->room_name) - i] == ' ') {
                    state->room_name[C_ARRAY_LEN(state->room_name) - i] = '\0';
                } else {
                    break;
                }
            }
        }
    } else if (iscntrl(buf[*i])) {
        switch (buf[*i] + 'A' - 1) {
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
                          strncpy(state->room_name, state->rooms.rooms[*cursorlevel].data.name, C_ARRAY_LEN(state->room_name));
                          for (size_t i = 0; i < C_ARRAY_LEN(state->room_name); i ++) {
                              if (state->room_name[C_ARRAY_LEN(state->room_name) - i] == '\0') continue;
                              if (state->room_name[C_ARRAY_LEN(state->room_name) - i] == ' ') {
                                  state->room_name[C_ARRAY_LEN(state->room_name) - i] = '\0';
                              } else {
                                  break;
                              }
                          }
                      }
                      break;
        }
    } else {
        if (buf[*i] == '[') {
            if (*i + 1 < n) {
                // CSI sequence
                *i += 1;
                uint8_t arg = 0;
                while (*i < n) {
                    if (isdigit(buf[*i])) {
                        arg = 10 * arg + buf[*i] - '0';
                    } else if (buf[*i] == ';') {
                        arg = 0;
                    } else {
                        if (arg == 3 && buf[*i] == '~') {
                            if (state->roomname_cursor) {
                                state->room_name[--state->roomname_cursor] = 0;
                            } else {
                                state->partial_byte = 0;
                            }
                        } else {
                            fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[*i], arg);
                        }
                        break;
                    }
                    (*i)++;
                }
            } else {
                UNREACHABLE();
            }
        } else if (state->partial_byte == 0 && isprint(buf[*i])) {
            if (state->roomname_cursor < C_ARRAY_LEN(state->room_name) - 1) {
                state->room_name[state->roomname_cursor++] = buf[*i];

                bool possible = false;
                bool duplicate = false;
                size_t matching_room = 0;
                if (state->roomname_cursor) {
                    for (size_t _room = 0; _room < C_ARRAY_LEN(state->rooms.rooms); _room ++) {
                        struct DecompresssedRoom *room = &state->rooms.rooms[_room].data;
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
                    strncpy(state->room_name, state->rooms.rooms[*cursorlevel].data.name, C_ARRAY_LEN(state->room_name));
                    for (size_t i = 0; i < C_ARRAY_LEN(state->room_name); i ++) {
                        if (state->room_name[C_ARRAY_LEN(state->room_name) - i] == '\0') continue;
                        if (state->room_name[C_ARRAY_LEN(state->room_name) - i] == ' ') {
                            state->room_name[C_ARRAY_LEN(state->room_name) - i] = '\0';
                        } else {
                            break;
                        }
                    }
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    assert(writeRooms(&state->rooms));
                }
            }
        }
    }
    (*i)++;
    return true;
}

static bool handle_edit_switchdetails_chunk_memory(char *buf, int *i, int n) {
    size_t *cursorlevel = &state->current_level;
    if (isxdigit(buf[*i])) {
        uint8_t b = 0;
        if (isdigit(buf[*i])) {
            b = *buf - '0';
        } else {
            b = 10 + tolower(buf[*i]) - 'a';
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
    } else if (buf[*i] == 'h') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BLOCK);
        if (chunk->x) {
            chunk->x --;
        } else {
            if (chunk->y == HEIGHT_TILES) {
                chunk->y = (offsetof(struct DecompresssedRoom, end_marker) - 1) / WIDTH_TILES;
                chunk->x = (offsetof(struct DecompresssedRoom, end_marker) - 1) % WIDTH_TILES;
            } else {
                chunk->y --;
                chunk->x = WIDTH_TILES - 1;
            }
        }
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (buf[*i] == 'j') {
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
        size_t end = offsetof(struct DecompresssedRoom, end_marker) - overflow;
        if (offset >= end) {
            chunk->y = HEIGHT_TILES;
            chunk->x = 0;
        }
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (buf[*i] == 'k') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BLOCK);
        if (chunk->x) {
            chunk->x --;
        } else {
            if (chunk->y == HEIGHT_TILES) {
                chunk->y = (offsetof(struct DecompresssedRoom, end_marker) - 1) / WIDTH_TILES;
                chunk->x = (offsetof(struct DecompresssedRoom, end_marker) - 1) % WIDTH_TILES;
            } else {
                chunk->y --;
                chunk->x = WIDTH_TILES - 1;
            }
        }
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (buf[*i] == 'l') {
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
        size_t end = offsetof(struct DecompresssedRoom, end_marker) - overflow;
        if (offset >= end) {
            chunk->y = HEIGHT_TILES;
            chunk->x = 0;
        }
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (buf[*i] == 'o') {
        state->switch_on = !state->switch_on;
    } else if (buf[*i] == ' ') {
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
        size_t end = offsetof(struct DecompresssedRoom, end_marker) - overflow;
        if (offset >= end) {
            chunk->y = HEIGHT_TILES;
            chunk->x = 0;
        }
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (buf[*i] == '^') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BLOCK);
        if (chunk->size < 8) chunk->size ++;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (buf[*i] == 'v') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BLOCK);
        if (chunk->size > 1) chunk->size --;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (buf[*i] == 'r') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BLOCK);
        chunk->dir = (chunk->dir + 1) % NUM_DIRECTIONS;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (buf[*i] == 0x7f) {
        if (state->partial_byte) {
            state->partial_byte = 0;
        } else {
            if (!state->current_switch) UNREACHABLE();
            if (!state->current_chunk) UNREACHABLE();
            struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
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
    } else if (buf[*i] == ESCAPE) {
        if (*i + 1 < n && buf[*i + 1] == '[' && *i + 2 < n) {
            // CSI sequence
            *i += 2;
            uint8_t arg = 0;
            while (*i < n) {
                if (isdigit(buf[*i])) {
                    arg = 10 * arg + buf[*i] - '0';
                } else if (buf[*i] == ';') {
                    arg = 0;
                } else {
                    switch (buf[*i]) {
                        case '~':
                        {
                            switch (arg) {
                                case 3:
                                {
                                    if (state->partial_byte) {
                                        state->partial_byte = 0;
                                    } else {
                                        if (!state->current_switch) UNREACHABLE();
                                        if (!state->current_chunk) UNREACHABLE();
                                        struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
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
                                }; break;

                                default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[*i], arg);
                            }
                        }; break;

                        case 'A':
                        {
                            struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                            struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                            assert(chunk->type == TOGGLE_BLOCK);
                            if (chunk->x) {
                                chunk->x --;
                            } else {
                                if (chunk->y == HEIGHT_TILES) {
                                    chunk->y = (offsetof(struct DecompresssedRoom, end_marker) - 1) / WIDTH_TILES;
                                    chunk->x = (offsetof(struct DecompresssedRoom, end_marker) - 1) % WIDTH_TILES;
                                } else {
                                    chunk->y --;
                                    chunk->x = WIDTH_TILES - 1;
                                }
                            }
                            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                            assert(writeRooms(&state->rooms));
                        }; break;

                        case 'B':
                        {
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
                            size_t end = offsetof(struct DecompresssedRoom, end_marker) - overflow;
                            if (offset >= end) {
                                chunk->y = HEIGHT_TILES;
                                chunk->x = 0;
                            }
                            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                            assert(writeRooms(&state->rooms));
                        }; break;

                        case 'C':
                        {
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
                            size_t end = offsetof(struct DecompresssedRoom, end_marker) - overflow;
                            if (offset >= end) {
                                chunk->y = HEIGHT_TILES;
                                chunk->x = 0;
                            }
                            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                            assert(writeRooms(&state->rooms));
                        }; break;

                        case 'D':
                        {
                            struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                            struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                            assert(chunk->type == TOGGLE_BLOCK);
                            if (chunk->x) {
                                chunk->x --;
                            } else {
                                if (chunk->y == HEIGHT_TILES) {
                                    chunk->y = (offsetof(struct DecompresssedRoom, end_marker) - 1) / WIDTH_TILES;
                                    chunk->x = (offsetof(struct DecompresssedRoom, end_marker) - 1) % WIDTH_TILES;
                                } else {
                                    chunk->y --;
                                    chunk->x = WIDTH_TILES - 1;
                                }
                            }
                            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                            assert(writeRooms(&state->rooms));
                        }; break;

                        default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[*i], arg);
                    }
                    break;
                }
                (*i)++;
            }
        } else if (state->help) {
            state->help = false;
        } else {
            if (state->partial_byte) {
                state->partial_byte = 0;
            } else {
                struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                if (sw->chunks.length <= 2) {
                    state->current_state = EDIT_SWITCHDETAILS;
                } else {
                    state->current_state = EDIT_SWITCHDETAILS_SELECT_CHUNK;
                }
                state->current_chunk = 0;
            }
        }
    } else if (buf[*i] == 't') {
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
    } else if (iscntrl(buf[*i])) {
        switch (buf[*i] + 'A' - 1) {
            case '_': state->help = !state->help; break;
            case 'H': state->debug.hex = !state->debug.hex; break;
        }
    } else if (buf[*i] == 'q') {
        if (state->help) {
            state->help = !state->help;
        } else {
            if (state->partial_byte) {
                state->partial_byte = 0;
            } else {
                struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                if (sw->chunks.length <= 2) {
                    state->current_state = EDIT_SWITCHDETAILS;
                } else {
                    state->current_state = EDIT_SWITCHDETAILS_SELECT_CHUNK;
                }
                state->current_chunk = 0;
            }
        }
    } else if (buf[*i] == '?') {
        state->help = !state->help;
    } else if (buf[*i] == '+') {
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
    } else if (buf[*i] == '-') {
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
    } else if (buf[*i] == 'p') {
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
    (*i)++;
    return true;
}

static bool handle_edit_switchdetails_chunk_block(char *buf, int *i, int n) {
    size_t *cursorlevel = &state->current_level;
    if (isxdigit(buf[*i])) {
        uint8_t b = 0;
        if (isdigit(buf[*i])) {
            b = *buf - '0';
        } else {
            b = 10 + tolower(buf[*i]) - 'a';
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
    } else if (buf[*i] == 'o') {
        state->switch_on = !state->switch_on;
        state->partial_byte = 0;
    } else if (buf[*i] == ' ') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BLOCK);
        chunk->dir = (chunk->dir + 1) % NUM_DIRECTIONS;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (buf[*i] == '^') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BLOCK);
        if (chunk->size < 8) chunk->size ++;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (buf[*i] == 'v') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BLOCK);
        if (chunk->size > 1) chunk->size --;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (buf[*i] == 'h') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BLOCK);
        if (chunk->x) chunk->x --;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (buf[*i] == 'j') {
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
    } else if (buf[*i] == 'k') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        assert(chunk->type == TOGGLE_BLOCK);
        if (chunk->y) chunk->y --;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (buf[*i] == 'l') {
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
    } else if (buf[*i] == 0x7f) {
        if (state->partial_byte) {
            state->partial_byte = 0;
        } else {
            if (!state->current_switch) UNREACHABLE();
            if (!state->current_chunk) UNREACHABLE();
            struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
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
    } else if (buf[*i] == ESCAPE) {
        if (*i + 1 < n && buf[*i + 1] == '[' && *i + 2 < n) {
            // CSI sequence
            *i += 2;
            uint8_t arg = 0;
            while (*i < n) {
                if (isdigit(buf[*i])) {
                    arg = 10 * arg + buf[*i] - '0';
                } else if (buf[*i] == ';') {
                    arg = 0;
                } else {
                    switch (buf[*i]) {
                        case '~':
                        {
                            switch (arg) {
                                case 3:
                                {
                                    if (state->partial_byte) {
                                        state->partial_byte = 0;
                                    } else {
                                        if (!state->current_switch) UNREACHABLE();
                                        if (!state->current_chunk) UNREACHABLE();
                                        struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
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
                                }; break;

                                default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[*i], arg);
                            }
                        }; break;

                        case 'A':
                        {
                            struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                            struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                            assert(chunk->type == TOGGLE_BLOCK);
                            if (chunk->y) chunk->y --;
                            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                            assert(writeRooms(&state->rooms));
                        }; break;

                        case 'B':
                        {
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
                        }; break;

                        case 'C':
                        {
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
                        }; break;

                        case 'D':
                        {
                            struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                            struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                            assert(chunk->type == TOGGLE_BLOCK);
                            if (chunk->x) chunk->x --;
                            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                            assert(writeRooms(&state->rooms));
                        }; break;

                        default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[*i], arg);
                    }
                    break;
                }
                (*i)++;
            }
        } else if (state->help) {
            state->help = false;
        } else {
            if (state->partial_byte) {
                state->partial_byte = 0;
            } else {
                struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                if (sw->chunks.length <= 2) {
                    state->current_state = EDIT_SWITCHDETAILS;
                } else {
                    state->current_state = EDIT_SWITCHDETAILS_SELECT_CHUNK;
                }
                state->current_chunk = 0;
            }
        }
    } else if (buf[*i] == 't') {
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
    } else if (iscntrl(buf[*i])) {
        switch (buf[*i] + 'A' - 1) {
            case '_': state->help = !state->help; break;
            case 'H': state->debug.hex = !state->debug.hex; break;
        }
    } else if (buf[*i] == 'q') {
        if (state->help) {
            state->help = !state->help;
        } else {
            if (state->partial_byte) {
                state->partial_byte = 0;
            } else {
                struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                if (sw->chunks.length <= 2) {
                    state->current_state = EDIT_SWITCHDETAILS;
                } else {
                    state->current_state = EDIT_SWITCHDETAILS_SELECT_CHUNK;
                }
                state->current_chunk = 0;
            }
        }
    } else if (buf[*i] == '?') {
        state->help = !state->help;
    } else if (buf[*i] == '+') {
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
    } else if (buf[*i] == '-') {
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
    } else if (buf[*i] == 'p') {
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
    (*i)++;
    return true;
}

static bool handle_edit_switchdetails_chunk_object(char *buf, int *i, int n) {
    size_t *cursorlevel = &state->current_level;
    if (isxdigit(buf[*i])) {
        uint8_t b = 0;
        if (isdigit(buf[*i])) {
            b = *buf - '0';
        } else {
            b = 10 + tolower(buf[*i]) - 'a';
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
    } else if (buf[*i] == 0x7f) {
        if (state->partial_byte) {
            state->partial_byte = 0;
        } else {
            if (!state->current_switch) UNREACHABLE();
            if (!state->current_chunk) UNREACHABLE();
            struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
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
    } else if (buf[*i] == ESCAPE) {
        if (*i + 1 < n && buf[*i + 1] == '[' && *i + 2 < n) {
            // CSI sequence
            *i += 2;
            uint8_t arg = 0;
            while (*i < n) {
                if (isdigit(buf[*i])) {
                    arg = 10 * arg + buf[*i] - '0';
                } else if (buf[*i] == ';') {
                    arg = 0;
                } else {
                    switch (buf[*i]) {
                        case '~':
                        {
                            switch (arg) {
                                case 3:
                                {
                                    if (state->partial_byte) {
                                        state->partial_byte = 0;
                                    } else {
                                        if (!state->current_switch) UNREACHABLE();
                                        if (!state->current_chunk) UNREACHABLE();
                                        struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
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
                                }; break;

                                default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[*i], arg);
                            }
                        }; break;

                        default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[*i], arg);
                    }
                    break;
                }
                (*i)++;
            }
        } else if (state->help) {
            state->help = false;
        } else {
            if (state->partial_byte) {
                state->partial_byte = 0;
            } else {
                struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                if (sw->chunks.length <= 2) {
                    state->current_state = EDIT_SWITCHDETAILS;
                } else {
                    state->current_state = EDIT_SWITCHDETAILS_SELECT_CHUNK;
                }
                state->current_chunk = 0;
            }
        }
    } else if (buf[*i] == 't') {
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
    } else if (buf[*i] == 'i') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        chunk->index = (chunk->index + 1) % 0x10;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (buf[*i] == 's') {
        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
        chunk->test = (((chunk->test >> 4) + 1) % 4) << 4;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (iscntrl(buf[*i])) {
        switch (buf[*i] + 'A' - 1) {
            case '_': state->help = !state->help; break;
            case 'H': state->debug.hex = !state->debug.hex; break;
        }
    } else if (buf[*i] == 'q') {
        if (state->help) {
            state->help = !state->help;
        } else {
            if (state->partial_byte) {
                state->partial_byte = 0;
            } else {
                struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                if (sw->chunks.length <= 2) {
                    state->current_state = EDIT_SWITCHDETAILS;
                } else {
                    state->current_state = EDIT_SWITCHDETAILS_SELECT_CHUNK;
                }
                state->current_chunk = 0;
            }
        }
    } else if (buf[*i] == '?') {
        state->help = !state->help;
    } else if (buf[*i] == '+') {
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
    } else if (buf[*i] == '-') {
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
    } else if (buf[*i] == 'p') {
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
    (*i)++;
    return true;
}

static bool handle_normal(char *buf, int *i, int n) {
    v2 *cursor = &state->cursors[state->current_level];
    size_t *cursorlevel = &state->current_level;
    const char *match = NULL;
#define KEY_MATCHES(key) ((strncmp(buf + *i, (key), strlen((key))) == 0) && (match = (key)))
    if (KEY_MATCHES("p")) {
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
    } else if (KEY_MATCHES("q")) {
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
    } else if (KEY_MATCHES("?")) {
        state->help = !state->help;
    } else if (KEY_MATCHES(KEY_LEFT) || KEY_MATCHES("h")) {
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
    } else if (KEY_MATCHES(KEY_DOWN) || KEY_MATCHES("j")) {
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
    } else if (KEY_MATCHES(KEY_UP) || KEY_MATCHES("k")) {
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
    } else if (KEY_MATCHES(KEY_RIGHT) || KEY_MATCHES("l")) {
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
    } else if (KEY_MATCHES("r")) {
        state->previous_state = state->current_state;
        state->current_state = GOTO_ROOM;
        state->current_switch = 0;
        state->switch_on = false;
        state->current_chunk = 0;
        state->partial_byte = 0;
    } else if (KEY_MATCHES("s")) {
        state->previous_state = state->current_state;
        state->current_state = GOTO_SWITCH;
        state->current_switch = 0;
        state->switch_on = false;
        state->current_chunk = 0;
        state->partial_byte = 0;
    } else if (KEY_MATCHES("o")) {
        state->previous_state = state->current_state;
        state->current_state = GOTO_OBJECT;
        state->current_switch = 0;
        state->switch_on = false;
        state->current_chunk = 0;
        state->partial_byte = 0;
    }
    if (state->current_state == TILE_EDIT) {
#define LEFT -1, 0
#define DOWN 0, +1
#define UP 0, -1
#define RIGHT +1, 0
        // FIXME Currently doesn't move objects through to neighbours
        if (KEY_MATCHES(SHIFT_LEFT) || KEY_MATCHES("H")) {
            move(LEFT);
        } else if (KEY_MATCHES(SHIFT_DOWN) || KEY_MATCHES("J")) {
            move(DOWN);
        } else if (KEY_MATCHES(SHIFT_UP) || KEY_MATCHES("K")) {
            move(UP);
        } else if (KEY_MATCHES(SHIFT_RIGHT) || KEY_MATCHES("L")) {
            move(RIGHT);
        }
        if (KEY_MATCHES(ALT_LEFT) || KEY_MATCHES("\033h")) {
            stretch(LEFT);
        } else if (KEY_MATCHES(ALT_DOWN) || KEY_MATCHES("\033j")) {
            stretch(DOWN);
        } else if (KEY_MATCHES(ALT_UP) || KEY_MATCHES("\033k")) {
            stretch(UP);
        } else if (KEY_MATCHES(ALT_RIGHT) || KEY_MATCHES("\033l")) {
            stretch(RIGHT);
        }
        if (KEY_MATCHES(ALT_SHIFT_LEFT) || KEY_MATCHES("\033H")) {
            shrink(LEFT);
        } else if (KEY_MATCHES(ALT_SHIFT_DOWN) || KEY_MATCHES("\033J")) {
            shrink(DOWN);
        } else if (KEY_MATCHES(ALT_SHIFT_UP) || KEY_MATCHES("\033K")) {
            shrink(UP);
        } else if (KEY_MATCHES(ALT_SHIFT_RIGHT) || KEY_MATCHES("\033L")) {
            shrink(RIGHT);
        }
    }
    if (iscntrl(buf[*i]) != 0) {
        switch (buf[*i] + 'A' - 1) {
            case '_': state->help = !state->help; break;
            case 'D':
                  state->previous_state = state->current_state;
                  state->current_state = TOGGLE_DISPLAY;
                  state->current_switch = 0;
                  state->switch_on = false;
                  state->current_chunk = 0;
                  break;

            case 'H': state->debug.hex = !state->debug.hex; break;
            case 'T': {
                switch (state->current_state) {
                    case NORMAL:
                    {
                        state->previous_state = state->current_state;
                        state->current_state = TILE_EDIT;
                        state->current_switch = 0;
                        state->switch_on = false;
                        state->current_chunk = 0;
                    }; break;
                    case TILE_EDIT:
                    {
                        state->previous_state = state->current_state;
                        state->current_state = NORMAL;
                        state->current_switch = 0;
                        state->switch_on = false;
                        state->current_chunk = 0;
                    }; break;

                    default: UNREACHABLE();
                }
            }; break;
        }
    }


    if (match != NULL) {
        *i += strlen(match);
    } else {
        // Swallow unused input
        switch (buf[*i]) {
            case ESCAPE:
                if (*i + 1 < n) {
                    // ESC sequence
                    switch (buf[*i + 1]) {
                        case '[':
                            if (*i + 2 < n) {
                                // CSI sequence
                                *i += 2;
                                uint8_t arg = 0;
                                while (*i < n) {
                                    if (isdigit(buf[*i])) {
                                        arg = 10 * arg + buf[*i] - '0';
                                    } else if (buf[*i] == ';') {
                                        arg = 0;
                                    } else {
                                        if (buf[*i] == '~') {
                                            switch (arg) {
                                                case 3:
                                                {
                                                    if (state->partial_byte) {
                                                        state->partial_byte = 0;
                                                    } else {
                                                        // FIXME find object or switch underneath
                                                        struct RoomObject *object_underneath = NULL;
                                                        struct SwitchObject *switch_underneath = NULL;
                                                        struct SwitchChunk *chunk_underneath = NULL;
                                                        struct SwitchObject *chunk_switch_underneath = NULL;

                                                        struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
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
                                                }; break;

                                                default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[*i], arg);
                                            }
                                        } else {
                                            fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[*i], arg);
                                        }
                                    }
                                    (*i)++;
                                }
                            } else {
                                UNREACHABLE();
                            }
                            break;

                        default:
                            *i += 2;
                            break;
                    }
                } else {
                    if (state->help) {
                        state->help = false;
                    } else {
                        if ((state->partial_byte & 0xFF00) != 0) {
                            state->partial_byte = 0;
                        }
                    }
                    *i += 1; // Single escape
                }
                break;

            default:
                *i += 1;
        }
    }
#undef KEY_MATCHES
    return true;
}

static bool handle_tile_edit(char *buf, int *i, int n) {
    size_t *cursorlevel = &state->current_level;
    const char *match = NULL;
#define KEY_MATCHES(key) ((strncmp(buf + *i, (key), strlen((key))) == 0) && (match = (key)))
    if (isxdigit(buf[*i])) {
        uint8_t b = 0;
        if (isdigit(buf[*i])) {
            b = *buf - '0';
        } else {
            b = 10 + tolower(buf[*i]) - 'a';
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
            struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
            for (size_t i = 0; i < room->num_objects; i ++) {
                struct RoomObject *object = room->objects + i;
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
                for (size_t i = 0; i < room->num_switches; i ++) {
                    switcch = room->switches + i;
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
                        // FIXME split up chunks, need one for this byte, and potentially 2 more for either end
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
        *i += 1;
        return true;
    } else if (KEY_MATCHES("R")) {
        state->previous_state = state->current_state;
        state->current_state = EDIT_ROOMNAME;
        state->current_switch = 0;
        state->switch_on = false;
        state->current_chunk = 0;
        state->roomname_cursor = 0;
        strncpy(state->room_name, state->rooms.rooms[*cursorlevel].data.name, C_ARRAY_LEN(state->room_name));
        for (size_t i = 0; i < C_ARRAY_LEN(state->room_name); i ++) {
            if (state->room_name[C_ARRAY_LEN(state->room_name) - i] == '\0') continue;
            if (state->room_name[C_ARRAY_LEN(state->room_name) - i] == ' ') {
                state->room_name[C_ARRAY_LEN(state->room_name) - i] = '\0';
            } else {
                break;
            }
        }
    } else if (buf[*i] == '-') {
        struct RoomObject *object_underneath = NULL;
        struct SwitchObject *switch_underneath = NULL;
        struct SwitchChunk *chunk_underneath = NULL;
        struct SwitchObject *chunk_switch_underneath = NULL;

        struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
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
            struct SwitchChunk ch= *chunk_underneath;
            if (c) {
                chunk_switch_underneath->chunks.data[c] = chunk_switch_underneath->chunks.data[c-1];
                chunk_switch_underneath->chunks.data[c-1] = ch;
            }
        }
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (buf[*i] == '+') {
        struct RoomObject *object_underneath = NULL;
        struct SwitchObject *switch_underneath = NULL;
        struct SwitchChunk *chunk_underneath = NULL;
        struct SwitchObject *chunk_switch_underneath = NULL;

        struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
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
            struct SwitchChunk ch= *chunk_underneath;
            if (c < (signed) chunk_switch_underneath->chunks.length - 1) {
                chunk_switch_underneath->chunks.data[c] = chunk_switch_underneath->chunks.data[c+1];
                chunk_switch_underneath->chunks.data[c+1] = ch;
            }
        }
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    } else if (buf[*i] == 0x7f) {
        if (state->partial_byte) {
            state->partial_byte = 0;
        } else {
            struct RoomObject *object_underneath = NULL;
            struct SwitchObject *switch_underneath = NULL;
            struct SwitchChunk *chunk_underneath = NULL;
            struct SwitchObject *chunk_switch_underneath = NULL;

            struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
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
    } else if (iscntrl(buf[*i])) {
        switch (buf[*i] + 'A' - 1) {
            case 'R':
                state->previous_state = state->current_state;
                state->current_state = EDIT_ROOMDETAILS;
                state->current_switch = 0;
                state->switch_on = false;
                state->current_chunk = 0;
                (*i)++;
                return true;

            case 'S':
            {
                state->previous_state = state->current_state;
                struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
                size_t num_switches = room->num_switches;
                if (state->current_switch) {
                    state->current_state = EDIT_SWITCHDETAILS;
                    (*i)++;
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
                sw_idx ++;
                return true;
            }
        }
    }
#undef KEY_MATCHES
    return handle_normal(buf, i, n);
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

bool process_input() {
    struct pollfd fd = { .fd = STDIN_FILENO, .events = POLLIN };
    assert(poll(&fd, 1, 1000) >= 0);
    if (!(fd.revents & POLLIN)) return false;

    char buf[64];
    ssize_t n = read(fd.fd, buf, sizeof(buf));
    assert(n >= 0);
    if (n == 0) return false;

    int i = 0;
    while (i < n) {
        assert(state->current_state < NUM_STATES);
        assert(state_handlers[state->current_state] != NULL);
        state_handlers[state->current_state](buf, &i, n);
    }
    return true;
}

bool update() { return false; }
