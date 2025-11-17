#include <assert.h>

#include <termios.h>

#include <ctype.h>

#include <poll.h>

#include <signal.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#include <string.h>

#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/stat.h>

#include <dlfcn.h>

#include <time.h>
#define TIME_NEWER(t1, t2) (((t1).tv_sec > (t2).tv_sec) || (((t1).tv_sec == (t2).tv_sec) && (t1).tv_nsec > (t2).tv_nsec))

#define UNREACHABLE() do { \
    fprintf(stderr, "%s:%d: UNREACHABLE\n", __FILE__, __LINE__); \
    if (state != NULL) { \
        assert(tcsetattr(STDIN_FILENO, TCSANOW, &state->original_termios) == 0); \
        freeRoomFile(&state->rooms); \
        free(state); \
    } \
    exit(1); \
} while (false)

#if __has_attribute(__fallthrough__)
# define fallthrough                    __attribute__((__fallthrough__))
#else
# define fallthrough                    do {} while (0)  /* fallthrough */
#endif

#ifdef __TINYC__
#define LIBRARY_BUILD_CMD "tcc -g -ggdb -Werror -Wall -Wpedantic -fsanitize=address -fpic -shared room.c -o"
#else
#define LIBRARY_BUILD_CMD "cc -g -ggdb -Werror -Wall -Wpedantic -fsanitize=address -fpic -shared room.c -o"
#endif

#include "room.h"

bool any_source_newer(struct stat *library_stat) {
    struct stat file_stat;
    char *files[] = {
        __FILE__,
        "room.c",
        "room.h",
        "array.h"
    };
    for (size_t i = 0; i < C_ARRAY_LEN(files); i ++) {
        if (stat(files[i], &file_stat) == 0) {
            if (TIME_NEWER(file_stat.st_mtim, library_stat->st_mtim)) {
                return true;
            }
        }
    }
    return false;
}


#define SAVE_CURSOR "\x1b" "7"
#define RESTORE_CURSOR "\x1b" "8"
#define HIDE_CURSOR "\x1b[?25l"
#define SHOW_CURSOR "\x1b[?25h"

#define SAVE_SCREEN    "\x1b[?47h"
#define RESTORE_SCREEN "\x1b[?47l"

#define ENABLE_ALT_BUFFER  "\x1b[1049h"
#define DISABLE_ALT_BUFFER "\x1b[1049l"

#define RESET_GFX_MODE "\x1b[0m"
#define CLEAR_SCREEN   "\x1b[0J"
#define GOTO(_x, _y)     do { \
    assert((_x) < state->screen_dimensions.x && "width out of bounds"); \
    assert((_y) < state->screen_dimensions.y && "height out of bounds"); \
    printf("\033[%d;%dH", (_y) + 1, (_x) + 1); \
} while (false)

#define KEY_UP    "\x1b[A"
#define KEY_DOWN  "\x1b[B"
#define KEY_RIGHT "\x1b[C"
#define KEY_LEFT  "\x1b[D"

#define SHIFT_UP    "\x1b[1;2A"
#define SHIFT_DOWN  "\x1b[1;2B"
#define SHIFT_RIGHT "\x1b[1;2C"
#define SHIFT_LEFT  "\x1b[1;2D"

#define ALT_UP    "\x1b[1;3A"
#define ALT_DOWN  "\x1b[1;3B"
#define ALT_RIGHT "\x1b[1;3C"
#define ALT_LEFT  "\x1b[1;3D"

#define ALT_SHIFT_UP    "\x1b[1;4A"
#define ALT_SHIFT_DOWN  "\x1b[1;4B"
#define ALT_SHIFT_RIGHT "\x1b[1;4C"
#define ALT_SHIFT_LEFT  "\x1b[1;4D"

#define HOME      "\x1b[1~"
#define INSERT    "\x1b[2~"
#define DELETE    "\x1b[3~"
#define END       "\x1b[4~"
#define PAGE_UP   "\x1b[5~"
#define PAGE_DOWN "\x1b[6~"

#define KEYPAD_1  END
#define KEYPAD_2  "2"
#define KEYPAD_3  PAGE_DOWN
#define KEYPAD_4  "4"
#define KEYPAD_5  "\x1b[E"
#define KEYPAD_6  "6"
#define KEYPAD_7  HOME
#define KEYPAD_8  "8"
#define KEYPAD_9  PAGE_UP
#define KEYPAD_0  INSERT

#define F1  "\x1b[OP"
#define F2  "\x1b[OQ"
#define F3  "\x1b[OR"
#define F4  "\x1b[OS"
#define F5  "\x1b[15~"
#define F6  "\x1b[17~"
#define F7  "\x1b[18~"
#define F8  "\x1b[19~"
#define F9  "\x1b[20~"
#define F10  "\x1b[21~"
#define F11  "\x1b[23~"
#define F12  "\x1b[24~"

#define ESCAPE '\033'

#define MIN_HEIGHT (HEIGHT_TILES + 1)
#define MIN_WIDTH (2 * WIDTH_TILES)

typedef struct {
    int x;
    int y;
} v2;

struct debug {
    bool hex;
    bool pos;
    bool data;
    bool neighbours;
    bool unknowns;
    bool objects;
    bool switches;
    bool all;
};

typedef enum {
    NORMAL,
    TILE_EDIT,
    GOTO_SWITCH,
    GOTO_ROOM,
    EDIT_ROOMNAME,
    EDIT_ROOMDETAILS,
    EDIT_ROOMDETAILS_NUM,
    TOGGLE_DISPLAY,
    NUM_STATES
} game_state_state;

typedef struct {
    game_state_state current_state;
    game_state_state previous_state;
    size_t size;
    v2 cursors[64];
    size_t current_level;
    v2 screen_dimensions;
    struct termios original_termios;
    RoomFile rooms;
    bool resized;
    bool help;
    struct debug debug;
    // FIXME have a pos for each room now that there is a goto

    uint16_t partial_byte;

    char room_detail;
    char room_name[24];
    size_t roomname_length;
    size_t roomname_cursor;
} game_state;
game_state *state = NULL;

void end() {
    printf(RESTORE_CURSOR SHOW_CURSOR RESTORE_SCREEN DISABLE_ALT_BUFFER);
    if (state != NULL) {
        assert(tcsetattr(STDIN_FILENO, TCSANOW, &state->original_termios) == 0);
        freeRoomFile(&state->rooms);
        free(state);
    }
    exit(0);
    UNREACHABLE();
}

void debugalltoggle() {
    state->debug.data = !state->debug.all;
    state->debug.objects = !state->debug.all;
    state->debug.switches = !state->debug.all;
    state->debug.neighbours = !state->debug.all;
    state->debug.unknowns = !state->debug.all;
    state->debug.pos = !state->debug.all;
    state->debug.all = !state->debug.all;
}

bool debugany() {
    return state->debug.data ||
        state->debug.objects ||
        state->debug.switches ||
        state->debug.neighbours ||
        state->debug.unknowns ||
        state->debug.pos;
}

void move(int dx, int dy) {
    int x = state->cursors[state->current_level].x;
    int y = state->cursors[state->current_level].y;
    if (x + dx < 0 || x + dx >= WIDTH_TILES || y + dy < 0 || y + dy >= HEIGHT_TILES) return;
    struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
    bool moved = false;
    if (state->debug.objects) {
        struct RoomObject *object = NULL;
        bool obj = false;
        for (size_t i = 0; i < room->num_objects; i ++) {
            object = room->objects + i;
            if (object->type == BLOCK &&
                    x >= object->x && x < object->x + object->block.width &&
                    y >= object->y && y < object->y + object->block.height &&
                    (int)object->x + dx >= 0 && (int)object->x + object->block.width + dx <= WIDTH_TILES &&
                    (int)object->y + dy >= 0 && (int)object->y + object->block.height + dy <= HEIGHT_TILES) {
                obj = true;
                break;
            } else if (object->type == SPRITE && x == object->x && y == object->y) {
                obj = true;
                break;
            }
        }
        if (obj) {
            object->x += dx;
            object->y += dy;
            state->cursors[state->current_level].x += dx;
            state->cursors[state->current_level].y += dy;
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            assert(writeRooms(&state->rooms));
            moved = true;
        }
    }
    if (!moved && state->debug.switches) {
        struct SwitchObject *switcch = NULL;
        bool sw = false;
        for (size_t i = 0; i < room->num_switches; i ++) {
            switcch = room->switches + i;
            if (switcch->chunks.length > 0 && switcch->chunks.data[0].type == PREAMBLE &&
                    x == switcch->chunks.data[0].x && y == switcch->chunks.data[0].y) {
                sw = true;
                break;
            }
        }
        if (sw) {
            room->tiles[TILE_IDX(x + dx, y + dy)] = room->tiles[TILE_IDX(x, y)];
            room->tiles[TILE_IDX(x, y)] = 0;
            switcch->chunks.data[0].x += dx;
            switcch->chunks.data[0].y += dy;
            state->cursors[state->current_level].x += dx;
            state->cursors[state->current_level].y += dy;
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            assert(writeRooms(&state->rooms));
            moved = true;
        } else {
            struct SwitchChunk *chunk = NULL;
            bool ch = false;
            for (size_t i = 0; i < room->num_switches; i ++) {
                switcch = room->switches + i;
                for (size_t c = 1; c < switcch->chunks.length; c ++) {
                    chunk = switcch->chunks.data + c;
                    if (chunk->type == TOGGLE_BLOCK &&
                            (chunk->dir == HORIZONTAL ?
                                 y == chunk->y && x >= chunk->x && x < chunk->x + chunk->size :
                                 x == chunk->x && y >= chunk->y && y < chunk->y + chunk->size) &&
                             chunk->x + dx >= 0 && chunk->y + dy >= 0 &&
                            (chunk->dir == HORIZONTAL ?
                                 chunk->x + chunk->size + dx <= WIDTH_TILES && chunk->y + dy <= HEIGHT_TILES :
                                 chunk->y + chunk->size + dy <= HEIGHT_TILES && chunk->x + dx <= WIDTH_TILES)) {
                        ch = true;
                        break;
                    }
                }
                if (ch) {
                    chunk->x += dx;
                    chunk->y += dy;
                    state->cursors[state->current_level].x += dx;
                    state->cursors[state->current_level].y += dy;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    assert(writeRooms(&state->rooms));
                    moved = true;
                    break;
                }
            }
        }
    }
    if (!moved && x + dx >= 0 && x + dx < WIDTH_TILES && y + dy >= 0 && y + dy < HEIGHT_TILES) {
        room->tiles[TILE_IDX(x + dx, y + dy)] = room->tiles[TILE_IDX(x, y)];
        room->tiles[TILE_IDX(x, y)] = 0;
        state->cursors[state->current_level].x += dx;
        state->cursors[state->current_level].y += dy;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    }
}

void stretch(int dx, int dy) {
    int x = state->cursors[state->current_level].x;
    int y = state->cursors[state->current_level].y;
    struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
    bool stretched = false;
    if (state->debug.objects) {
        struct RoomObject *object = NULL;
        bool obj = false;
        for (size_t i = 0; i < room->num_objects; i ++) {
            object = room->objects + i;
            if (object->type == BLOCK &&
                    x >= object->x && x < object->x + object->block.width &&
                    y >= object->y && y < object->y + object->block.height &&
                    (int)object->x + dx >= 0 && (int)object->x + object->block.width + dx <= WIDTH_TILES &&
                    (int)object->y + dy >= 0 && (int)object->y + object->block.height + dy <= HEIGHT_TILES) {
                if ((dx && object->block.width < 8) || (dy && object->block.height < 4)) {
                    obj = true;
                    break;
                }
            }
        }
        if (obj) {
            assert(object->type == BLOCK);
            assert(object->tiles);
            if (dx) {
                // copy column
                uint8_t *tiles = malloc((object->block.width + 1) * object->block.height);
                assert(tiles != NULL);
                for (size_t _y = 0; _y < object->block.height; _y ++) {
                    if (x - object->x > 0) {
                        memcpy(tiles + _y * (object->block.width + 1),
                                object->tiles + _y * object->block.width,
                                x - object->x);
                    }
                    tiles[_y * (object->block.width + 1) + (x - object->x)] = object->tiles[_y * object->block.width + (x - object->x)];
                    if (dx < 0) {
                        memcpy(tiles + _y * (object->block.width + 1) + (x - object->x) - dx,
                                object->tiles + _y * object->block.width + (x - object->x),
                                object->block.width - (x - object->x));
                    } else {
                        memcpy(tiles + _y * (object->block.width + 1) + (x - object->x) + 1,
                                object->tiles + _y * object->block.width + (x - object->x),
                                object->block.width - (x - object->x));
                    }
                }
                object->block.width ++;
                if (dx < 0) object->x += dx;
                free(object->tiles);
                object->tiles = tiles;
            } else {
                // copy row
                uint8_t *tiles = malloc(object->block.width * (object->block.height + 1));
                memset(tiles, 1, object->block.width * (object->block.height + 1));
                assert(tiles != NULL);
                memcpy(tiles, object->tiles, (y - object->y + 1) * object->block.width);
                memcpy(tiles + (y - object->y + 1) * object->block.width, object->tiles + (y - object->y) * object->block.width, (object->block.height - (y - object->y)) * object->block.width);
                object->block.height ++;
                if (dy < 0) object->y += dy;
                free(object->tiles);
                object->tiles = tiles;
            }
            state->cursors[state->current_level].x += dx;
            state->cursors[state->current_level].y += dy;
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            assert(writeRooms(&state->rooms));

            stretched = true;
        }
    }
    if (!stretched && state->debug.switches) {
        struct SwitchObject *switcch = NULL;
        struct SwitchChunk *chunk = NULL;
        bool ch = false;
        for (size_t i = 0; i < room->num_switches; i ++) {
            switcch = room->switches + i;
            for (size_t c = 1; c < switcch->chunks.length; c ++) {
                chunk = switcch->chunks.data + c;
                if (chunk->type == TOGGLE_BLOCK &&
                        y >= chunk->y && x >= chunk->x &&
                        chunk->x + dx >= 0 && chunk->y + dy >= 0) {
                    if ((dy && chunk->dir == VERTICAL) || (dx && chunk->dir == HORIZONTAL)) {
                        // expand this one
                        if (chunk->size < 8 && 
                                ((dy && x == chunk->x && y < chunk->y + chunk->size && chunk->y + chunk->size + dy <= HEIGHT_TILES) ||
                                 (dx && y == chunk->y && x < chunk->x + chunk->size && chunk->x + chunk->size + dx <= WIDTH_TILES))) {
                            ch = true;
                            break;
                        }
                    } else if ((dy && y == chunk->y && x < chunk->x + chunk->size) ||
                            (dx && x == chunk->x && y < chunk->y + chunk->size)) {
                        // create new chunk
                        ch = true;
                        break;
                    }
                }
            }
            if (ch) {
                if ((dy && chunk->dir == VERTICAL) || (dx && chunk->dir == HORIZONTAL)) {
                    // Expand other blocks if next to this?
                    chunk->size ++;
                    if (dy < 0) chunk->y --;
                    if (dx < 0) chunk->x --;
                } else {
                    // expand to the next row/column, making a new chunk
                    // there may already be one here so find the edge of the "block"
                    int _x = chunk->x + dx;
                    int _y = chunk->y + dy;
                    bool conflict = true;
                    while (conflict) {
                        conflict = false;
                        for (size_t i = 1; i < switcch->chunks.length; i ++) {
                            if (switcch->chunks.data[i].x == _x && switcch->chunks.data[i].y == _y &&
                                    switcch->chunks.data[i].type == TOGGLE_BLOCK &&
                                    switcch->chunks.data[i].on == chunk->on &&
                                    switcch->chunks.data[i].off == chunk->off &&
                                    switcch->chunks.data[i].size == chunk->size) {
                                _x += dx;
                                _y += dy;
                                conflict = true;
                                break;
                            }
                        }
                    }
                    ARRAY_ADD(switcch->chunks, ((struct SwitchChunk){ .type = TOGGLE_BLOCK,
                                .x = _x, .y = _y, .size = chunk->size,
                                .on = chunk->on, .off = chunk->off, .dir = chunk->dir }));
                }
                state->cursors[state->current_level].x += dx;
                state->cursors[state->current_level].y += dy;
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                assert(writeRooms(&state->rooms));
                stretched = true;
                break;
            }
        }
    }
    if (!stretched && x + dx >= 0 && x + dx < WIDTH_TILES && y + dy >= 0 && y + dy < HEIGHT_TILES) {
        room->tiles[TILE_IDX(x + dx, y + dy)] = room->tiles[TILE_IDX(x, y)];
        state->cursors[state->current_level].x += dx;
        state->cursors[state->current_level].y += dy;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    }
}

void shrink(int dx, int dy) {
    int x = state->cursors[state->current_level].x;
    int y = state->cursors[state->current_level].y;
    struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
    bool stretched = false;
    if (state->debug.objects) {
        struct RoomObject *object = NULL;
        bool obj = false;
        for (size_t i = 0; i < room->num_objects; i ++) {
            object = room->objects + i;
            if (object->type == BLOCK &&
                    x >= object->x && x < object->x + object->block.width &&
                    y >= object->y && y < object->y + object->block.height &&
                    (int)object->x + dx >= 0 && (int)object->x + object->block.width + dx <= WIDTH_TILES &&
                    (int)object->y + dy >= 0 && (int)object->y + object->block.height + dy <= HEIGHT_TILES) {
                if ((dx && object->block.width < 8) || (dy && object->block.height < 4)) {
                    obj = true;
                    break;
                }
            }
        }
        if (obj) {
            assert(object->type == BLOCK);
            assert(object->tiles);
            fprintf(stderr, "%s:%d: UNIMPLEMENTED: shrink object", __FILE__, __LINE__);
            abort();

            // FIXME I guess pull one end to the direction you are shrinking
            // so if row 0 and dy -1 noop, but row 1 and dy -1 shifts rows up by one
            // and row 2 and dy -1 will keep row 1 the same, then shift 3-N up by one
            // and row 3 will keep row 1&2 the same, then shift 4-N up by one
            // if dy +1, then keep bottom rows (if any), shift down by one to y
            // dx the same but in horizontal dir

            state->cursors[state->current_level].x += dx;
            state->cursors[state->current_level].y += dy;
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            assert(writeRooms(&state->rooms));

            stretched = true;
        }
    }
    if (!stretched && state->debug.switches) {
        struct SwitchObject *switcch = NULL;
        struct SwitchChunk *chunk = NULL;
        bool ch = false;
        for (size_t i = 0; i < room->num_switches; i ++) {
            switcch = room->switches + i;
            for (size_t c = 1; c < switcch->chunks.length; c ++) {
                chunk = switcch->chunks.data + c;
                if (chunk->type == TOGGLE_BLOCK &&
                        y >= chunk->y && x >= chunk->x &&
                        chunk->x + dx >= 0 && chunk->y + dy >= 0) {
                    if ((dy && chunk->dir == VERTICAL) || (dx && chunk->dir == HORIZONTAL)) {
                        // expand this one
                        if (chunk->size < 8 && 
                                ((dy && x == chunk->x && y < chunk->y + chunk->size && chunk->y + chunk->size + dy <= HEIGHT_TILES) ||
                                 (dx && y == chunk->y && x < chunk->x + chunk->size && chunk->x + chunk->size + dx <= WIDTH_TILES))) {
                            ch = true;
                            break;
                        }
                    } else if ((dy && y == chunk->y && x < chunk->x + chunk->size) ||
                            (dx && x == chunk->x && y < chunk->y + chunk->size)) {
                        // create new chunk
                        ch = true;
                        break;
                    }
                }
            }
            if (ch) {
                if ((dy && chunk->dir == VERTICAL) || (dx && chunk->dir == HORIZONTAL)) {
                    // Expand other blocks if next to this?
                    chunk->size ++;
                    if (dy < 0) chunk->y --;
                    if (dx < 0) chunk->x --;
                } else {
                    // expand to the next row/column, making a new chunk
                    // there may already be one here so find the edge of the "block"
                    int _x = chunk->x + dx;
                    int _y = chunk->y + dy;
                    bool conflict = true;
                    while (conflict) {
                        conflict = false;
                        for (size_t i = 1; i < switcch->chunks.length; i ++) {
                            if (switcch->chunks.data[i].x == _x && switcch->chunks.data[i].y == _y &&
                                    switcch->chunks.data[i].type == TOGGLE_BLOCK &&
                                    switcch->chunks.data[i].on == chunk->on &&
                                    switcch->chunks.data[i].off == chunk->off &&
                                    switcch->chunks.data[i].size == chunk->size) {
                                _x += dx;
                                _y += dy;
                                conflict = true;
                                break;
                            }
                        }
                    }
                    ARRAY_ADD(switcch->chunks, ((struct SwitchChunk){ .type = TOGGLE_BLOCK,
                                .x = _x, .y = _y, .size = chunk->size,
                                .on = chunk->on, .off = chunk->off, .dir = chunk->dir }));
                }
                state->cursors[state->current_level].x += dx;
                state->cursors[state->current_level].y += dy;
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                assert(writeRooms(&state->rooms));
                stretched = true;
                break;
            }
        }
    }
    if (!stretched && x + dx >= 0 && x + dx < WIDTH_TILES && y + dy >= 0 && y + dy < HEIGHT_TILES) {
        room->tiles[TILE_IDX(x + dx, y + dy)] = room->tiles[TILE_IDX(x, y)];
        state->cursors[state->current_level].x += dx;
        state->cursors[state->current_level].y += dy;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    }
}

void process_input() {
    struct pollfd fd = {
        .fd = STDIN_FILENO,
        .events = POLLIN,
    };
    assert(poll(&fd, 1, 0) >= 0);
    if (fd.revents != 0 && (fd.revents & POLLIN) != 0) {
        char buf[64];
        ssize_t n = read(fd.fd, buf, sizeof(buf));
        assert(n >= 0);

        int i = 0;
        while (i < n) {
            v2 *cursor = &state->cursors[state->current_level];
            size_t *cursorlevel = &state->current_level;

            /* FIXME make this more user friendly to add a key to action */

            char *match = NULL;
#define KEY_MATCHES(key) ((strncmp(buf + i, (key), strlen((key))) == 0) && (match = key))
            switch (state->current_state) {
                case GOTO_SWITCH:
                {
                    if (isxdigit(buf[i])) {
                        struct DecompresssedRoom *room = &state->rooms.rooms[*cursorlevel].data;
                        if (room->num_switches > 15) {
                            // FIXME support 2 digit nums
                            UNREACHABLE();
                        }
                        size_t id;
                        if (buf[i] > '9') {
                            id = tolower(buf[i]) - 'a' + 10;
                        } else {
                            id = buf[i] - '0';
                        }
                        if (id < room->num_switches) {
                            struct SwitchObject *sw = room->switches + id;
                            if (sw->chunks.length > 0 && sw->chunks.data[0].type == PREAMBLE) {
                                cursor->x = sw->chunks.data[0].x;
                                cursor->y = sw->chunks.data[0].y;
                            }
                            if (cursor->y >= HEIGHT_TILES) {
                                fprintf(stderr, "%s:%d: UNIMPLEMENTED: jump to switch out of bounds (%d,%d)", __FILE__, __LINE__, cursor->x, cursor->y);
                            }
                            state->current_state = state->previous_state;
                            state->previous_state = NORMAL;
                        }
                    } else if (buf[i] == ESCAPE) {
                        if (state->help) {
                            state->help = false;
                        } else {
                            state->current_state = state->previous_state;
                            state->previous_state = NORMAL;
                        }
                    } else if (iscntrl(buf[i])) {
                        switch (buf[i] + 'A' - 1) {
                            case '_': state->help = !state->help; break;
                            case 'H': state->debug.hex = !state->debug.hex; break;
                        }
                    } else if (buf[i] == 'q') {
                        if (state->help) {
                            state->help = !state->help;
                        } else {
                            state->current_state = state->previous_state;
                            state->previous_state = NORMAL;
                        }
                    } else if (buf[i] == '?') {
                        state->help = !state->help;
                    }
                    i ++;
                }; break;

                case GOTO_ROOM:
                {
                    if ((state->debug.hex && isxdigit(buf[i])) || (!state->debug.hex && isdigit(buf[i]))) {
                        int digit;
                        if (buf[i] > '9') {
                            digit = tolower(buf[i]) - 'a' + 10;
                        } else {
                            digit = buf[i] - '0';
                        }
                        if (state->partial_byte) {
                            size_t room = digit;
                            if (state->debug.hex) {
                                room += 16 * (state->partial_byte & 0xFF);
                            } else {
                                room += 10 * (state->partial_byte & 0xFF);
                            }
                            if (room < C_ARRAY_LEN(state->rooms.rooms)) {
                                *cursorlevel = room;
                                state->current_state = state->previous_state;
                                state->previous_state = NORMAL;
                                state->partial_byte = 0;
                            }
                        } else {
                            if ((unsigned)(digit * (state->debug.hex ? 16 : 10)) < C_ARRAY_LEN(state->rooms.rooms)) {
                                state->partial_byte = 0xFF00 | digit;
                            }
                        }
                    } else if (buf[i] == 0x7f) {
                        state->partial_byte = 0;
                    } else if (buf[i] == ESCAPE) {
                        if (state->help) {
                            state->help = false;
                        } else {
                            state->current_state = state->previous_state;
                            state->previous_state = NORMAL;
                        }
                    } else if (iscntrl(buf[i])) {
                        switch (buf[i] + 'A' - 1) {
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
                    } else if (buf[i] == 'q') {
                        if (state->help) {
                            state->help = !state->help;
                        } else {
                            state->current_state = state->previous_state;
                            state->previous_state = NORMAL;
                        }
                    } else if (buf[i] == '?') {
                        state->help = !state->help;
                    } else {
                        switch (buf[i]) {
                            case '[':
                                if (i + 1 < n) {
                                    // CSI sequence
                                    i += 1;
                                    uint8_t arg = 0;
                                    while (i < n) {
                                        if (isdigit(buf[i])) {
                                            arg = 10 * arg + buf[i] - '0';
                                        } else if (buf[i] == ';') {
                                            arg = 0;
                                        } else {
                                            if (arg == 3 && buf[i] == '~') {
                                                state->partial_byte = 0;
                                            }
                                            break;
                                        }
                                        i ++;
                                    }
                                } else {
                                    UNREACHABLE();
                                }
                                break;
                        }
                    }
                    i ++;
                }; break;

                case EDIT_ROOMNAME:
                {
                    if (state->help) {
                        switch (buf[i]) {
                            case ESCAPE:
                            case 'q':
                            case 0x1f:
                                state->help = false;
                                break;
                        }
                        i ++;
                        continue;
                    }

                    if (buf[i] == '\n') {
                        // ENTER
                        struct DecompresssedRoom *room = &state->rooms.rooms[*cursorlevel].data;
                        assert(C_ARRAY_LEN(state->room_name) <= C_ARRAY_LEN(room->name));
                        strncpy(room->name, state->room_name, C_ARRAY_LEN(room->name));
                        state->current_state = state->previous_state;
                        state->previous_state = NORMAL;
                    } else if (isprint(buf[i])) {
                        if (state->roomname_cursor < C_ARRAY_LEN(state->room_name) - 1) {
                            state->room_name[state->roomname_cursor++] = buf[i];
                        }
                    } else if (buf[i] == ESCAPE) {
                        if (i + 1 < n) {
                            switch (buf[i + 1]) {
                                case '[':
                                    if (i + 2 < n) {
                                        // CSI sequence, with leading esc
                                        i += 2;
                                        uint8_t arg = 0;
                                        while (i < n) {
                                            if (isdigit(buf[i])) {
                                                arg = 10 * arg + buf[i] - '0';
                                            } else if (buf[i] == ';') {
                                                fprintf(stderr, "%s:%d: UNIMPLEMENTED: multiple csi args", __FILE__, __LINE__);
                                                arg = 0;
                                            } else {
                                                switch (buf[i]) {
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

                                                    default:
                                                        fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[i], arg);
                                                }
                                                i ++;
                                                break;
                                            }
                                            i ++;
                                        }
                                    } else {
                                        UNREACHABLE();
                                    }
                                    break;
                            }
                            break;
                        } else {
                            state->current_state = state->previous_state;
                            state->previous_state = NORMAL;
                        }
                    } else if (buf[i] == 0x7f) {
                        if (state->roomname_cursor) {
                            for (size_t c = state->roomname_cursor - 1; c < C_ARRAY_LEN(state->room_name) - 1; c ++) {
                                state->room_name[c] = state->room_name[c + 1];
                                state->room_name[c + 1] = '\0';
                            }
                            if (state->roomname_cursor) {
                                state->roomname_cursor--;
                            }
                        }
                    } else if (iscntrl(buf[i])) {
                        switch (buf[i] + 'A' - 1) {
                            case 'H': state->debug.hex = !state->debug.hex; break;
                            case '_': state->help = !state->help; break;
                        }
                    }
                    i ++;
                }; break;

                case TOGGLE_DISPLAY:
                {
                    uint8_t changed = 1;
                    switch (buf[i]) {
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

                        default: changed = 0;
                    }
                    i++;
                    if (changed) {
                        state->current_state = state->previous_state;
                        state->previous_state = NORMAL;
                    }
                }; break;

                case EDIT_ROOMDETAILS:
                {
                    switch (buf[i]) {
                        case 'b':
                        {
                            state->room_detail = buf[i];
                            state->previous_state = state->current_state;
                            state->current_state = EDIT_ROOMDETAILS_NUM;
                        }; break;

                        case 'q':
                        case ESCAPE:
                            if (state->help) {
                                state->help = false;
                            } else {
                                state->current_state = state->previous_state;
                                state->previous_state = NORMAL;
                            }
                            break;

                        default:
                            if (iscntrl(buf[i])) {
                                switch (buf[i] + 'A' - 1) {
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
                    i++;
                }; break;

                case EDIT_ROOMDETAILS_NUM:
                {
                    if ((state->debug.hex && isxdigit(buf[i])) || (!state->debug.hex && isdigit(buf[i]))) {
                        int digit;
                        if (buf[i] > '9') {
                            digit = tolower(buf[i]) - 'a' + 10;
                        } else {
                            digit = buf[i] - '0';
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
                                case 'b': room->background = value; break;

                                default: UNREACHABLE();
                            }
                            state->current_state = state->previous_state;
                            state->previous_state = NORMAL;
                            state->partial_byte = 0;
                        } else {
                            state->partial_byte = 0xFF00 | digit;
                        }
                    } else if (buf[i] == 0x7f) {
                        state->partial_byte = 0;
                    } else if (buf[i] == ESCAPE) {
                        if (state->help) {
                            state->help = false;
                        } else {
                            state->current_state = state->previous_state;
                            state->previous_state = NORMAL;
                        }
                    } else if (iscntrl(buf[i])) {
                        switch (buf[i] + 'A' - 1) {
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
                    } else if (buf[i] == 'q') {
                        if (state->help) {
                            state->help = !state->help;
                        } else {
                            state->current_state = state->previous_state;
                            state->previous_state = NORMAL;
                        }
                    } else if (buf[i] == '?') {
                        state->help = !state->help;
                    } else {
                        switch (buf[i]) {
                            case '[':
                                if (i + 1 < n) {
                                    // CSI sequence
                                    i += 1;
                                    uint8_t arg = 0;
                                    while (i < n) {
                                        if (isdigit(buf[i])) {
                                            arg = 10 * arg + buf[i] - '0';
                                        } else if (buf[i] == ';') {
                                            arg = 0;
                                        } else {
                                            if (arg == 3 && buf[i] == '~') {
                                                state->partial_byte = 0;
                                            }
                                            break;
                                        }
                                        i ++;
                                    }
                                } else {
                                    UNREACHABLE();
                                }
                                break;
                        }
                    }
                    i ++;
                }; break;

                case TILE_EDIT:
                {
                    if (isxdigit(buf[i])) {
                        uint8_t b = 0;
                        if (isdigit(buf[i])) {
                            b = *buf - '0';
                        } else {
                            b = 10 + tolower(buf[i]) - 'a';
                        }
                        if ((state->partial_byte & 0xFF00) != 0) {
                            b = (state->partial_byte & 0xFF) | b;
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
                                    object->tiles[(y - object->y) * object->block.width + (x - object->x)] = b;
                                    break;
                                } else if (object->type == SPRITE && x == object->x && y == object->y) {
                                    obj = true;
                                    object->sprite.type = b >> 4;
                                    object->sprite.damage = b & 0xF;
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
                                        chunk->on = b;
                                        break;
                                    }
                                }
                            }
                            if (!obj && !ch) room->tiles[TILE_IDX(x, y)] = b;
                            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                            assert(writeRooms(&state->rooms));
                            state->partial_byte = 0;
                        } else {
                            state->partial_byte = 0xFF00 | (b << 4);
                        }
                        i += 1;
                        continue;
                    } else if (KEY_MATCHES("R")) {
                        state->previous_state = state->current_state;
                        state->current_state = EDIT_ROOMNAME;
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
                }; fallthrough;

                case NORMAL:
                {
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
                            end();
                            UNREACHABLE();
                        }
                    } else if (KEY_MATCHES("?")) {
                        state->help = !state->help;
                    } else if (KEY_MATCHES(KEY_LEFT) || KEY_MATCHES("h")) {
                        cursor->x --;
                        state->partial_byte = 0;
                        if (cursor->x < 0) {
                            *cursorlevel = state->rooms.rooms[*cursorlevel].data.room_west;
                            cursor->x = WIDTH_TILES - 1;
                        }
                    } else if (KEY_MATCHES(KEY_DOWN) || KEY_MATCHES("j")) {
                        cursor->y ++;
                        state->partial_byte = 0;
                        if (cursor->y >= HEIGHT_TILES) {
                            *cursorlevel = state->rooms.rooms[*cursorlevel].data.room_south;
                            cursor->y = 0;
                        }
                    } else if (KEY_MATCHES(KEY_UP) || KEY_MATCHES("k")) {
                        cursor->y --;
                        state->partial_byte = 0;
                        if (cursor->y < 0) {
                            *cursorlevel = state->rooms.rooms[*cursorlevel].data.room_north;
                            cursor->y = HEIGHT_TILES - 1;
                        }
                    } else if (KEY_MATCHES(KEY_RIGHT) || KEY_MATCHES("l")) {
                        cursor->x ++;
                        state->partial_byte = 0;
                        if (cursor->x >= WIDTH_TILES) {
                            *cursorlevel = state->rooms.rooms[*cursorlevel].data.room_east;
                            cursor->x = 0;
                        }
                    } else if (KEY_MATCHES("r")) {
                        state->previous_state = state->current_state;
                        state->current_state = GOTO_ROOM;
                        state->partial_byte = 0;
                    } else if (KEY_MATCHES("s")) {
                        state->previous_state = state->current_state;
                        state->current_state = GOTO_SWITCH;
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
                    if (iscntrl(buf[i]) != 0) {
                        switch (buf[i] + 'A' - 1) {
                            case '_': state->help = !state->help; break;
                            case 'D':
                                  state->previous_state = state->current_state;
                                  state->current_state = TOGGLE_DISPLAY;
                                  break;

                            case 'H': state->debug.hex = !state->debug.hex; break;
                            case 'R':
                                  state->previous_state = state->current_state;
                                  state->current_state = EDIT_ROOMDETAILS;
                                  break;

                            case 'T': {
                                switch (state->current_state) {
                                    case NORMAL:
                                    {
                                        state->previous_state = state->current_state;
                                        state->current_state = TILE_EDIT;
                                    }; break;
                                    case TILE_EDIT:
                                    {
                                        state->previous_state = state->current_state;
                                        state->current_state = NORMAL;
                                    }; break;

                                    default: UNREACHABLE();
                                }
                            }; break;
                        }
                    }


                    if (match != NULL) {
                        i += strlen(match);
                    } else {
                        // Swallow unused input
                        switch (buf[i]) {
                            case ESCAPE:
                                if (i + 1 < n) {
                                    // ESC sequence
                                    switch (buf[i + 1]) {
                                        case '[':
                                            if (i + 2 < n) {
                                                // CSI sequence
                                                i += 2;
                                                while (i < n && (isdigit(buf[i]) || buf[i] == ';')) i ++;
                                                i ++;
                                            } else {
                                                UNREACHABLE();
                                            }
                                            break;

                                        default:
                                            i += 2;
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
                                    i += 1; // Single escape
                                }
                                break;

                            default:
                                i += 1;
                        }
                    }
                }; break;

                default: UNREACHABLE();
            }
#undef KEY_MATCHES

        }
    }
}

void update() {

}

typedef struct {
    const char *key;
    const char *action;
} help_keys;

help_keys help[][100] = {
    [NORMAL]={
        {"Left/h", "Move cursor left"},
        {"Down/j", "Move cursor down"},
        {"Up/k", "Move cursor up"},
        {"Right/l", "Move cursor right"},
        {"r[nn]", "goto room"},
        {"s[n]", "goto switch"},
        {"p", "play (runs play.sh)"},
        {"q", "quit"},
        {"Ctrl-?", "toggle help"},
        {"Escape", "close/cancel"},
        {"Ctrl-h", "toggle hex in debug info"},
        {"Ctrl-t", "toggle tile edit mode"},
        {"Ctrl-d[adnopsu]", "toggle display element"},
        {0},
    },

    [TILE_EDIT]={
        {"0-9a-fA-F", "Enter hex nibble"},
        {"Shift+dir", "Move thing under cursor"},
        {"Alt+dir", "Stretch thing under cursor"},
        {"Alt+S+dir", "Shrink thing under cursor"},
        {"Left/h", "Move cursor left"},
        {"Down/j", "Move cursor down"},
        {"Up/k", "Move cursor up"},
        {"Right/l", "Move cursor right"},
        {"R", "edit room name"},
        {"r[nn]", "goto room"},
        {"Ctrl-r[b]", "edit room detail"},
        {"s[n]", "goto switch"},
        {"p", "play (runs play.sh)"},
        {"q", "quit"},
        {"Ctrl-?", "toggle help"},
        {"Escape", "close/cancel"},
        {"Ctrl-h", "toggle hex in debug info"},
        {"Ctrl-t", "toggle tile edit mode"},
        {"Ctrl-d[adnopsu]", "toggle display element"},
        {0},
    },

    [GOTO_SWITCH]={
        {"0-9a-fA-F", "switch id (highlighted on map)"},
        {"q", "go back to main view"},
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
        {0},
    },

    [GOTO_ROOM]={
        {"0-9a-fA-F", "room number"},
        {"DEL", "delete character under cursor"},
        {"BACKSPACE", "delete character under cursor"},
        {"ESC", "go back to main view"},
        {"q", "go back to main view"},
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
        {0},
    },

    [EDIT_ROOMNAME]={
        {"any printable char", "change character under cursor"},
        {"DEL", "delete character under cursor"},
        {"BACKSPACE", "delete character under cursor"},
        {"ENTER", "save room name"},
        {"LEFT", "move cursor left"},
        {"RIGHT", "move cursor right"},
        {"ESC", "go back to main view"},
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
        {0},
    },

    [TOGGLE_DISPLAY]={
        {"a", "toggle all debug info"},
        {"d", "toggle room data display"},
        {"n", "toggle neighbour display"},
        {"o", "toggle room object display"},
        {"p", "toggle position display"},
        {"s", "toggle room switch display"},
        {"u", "toggle unknown display"},
        {"ESC", "go back to main view"},
        {"q", "go back to main view"},
        {0},
    },

    [EDIT_ROOMDETAILS]={
        {"b", "Edit room background"},
        {"ESC", "go back to main view"},
        {"q", "go back to main view"},
        {0},
    },

    [EDIT_ROOMDETAILS_NUM]={
        {"0-9a-fA-F", "value"},
        {"ESC", "go back to main view"},
        {"q", "go back to main view"},
        {0},
    },

};
_Static_assert(C_ARRAY_LEN(help) == NUM_STATES, "Unhandled for all states");

void show_help()
{
    int x = 0, x1 = 0, x2 = 0, y = 0;
    for (size_t i = 0; i < C_ARRAY_LEN(help[state->current_state]); i ++, y ++) {
        if (help[state->current_state][i].key == NULL) break;
        int len = strlen(help[state->current_state][i].key);
        if (len > x1) x1 = len;
        if (help[state->current_state][i].action == NULL) {
            fprintf(stderr, "here\n");
        }
        len = strlen(help[state->current_state][i].action);
        if (len > x2) x2 = len;
    }
    x = x1 + x2 + 5;
    if (x % 2) x++;
    y += 3;
    GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2);
    printf("\033[47;30;1m");
    printf("+");
    for (int i = 0; i < x / 2 - 3; i ++) printf("-");
    printf("help");
    for (int i = 0; i < x / 2 - 3; i ++) printf("-");
    printf("+\033[m ");
    for (int _y = 1; _y < y - 1; _y ++) {
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + _y);
        printf("\033[47;30;1;m|%*s|\033[40;37;1m ", x - 2, "");
    }
    GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + (y - 2));
    printf("\033[47;30;1m");
    printf("+");
    for (int i = 0; i < x - 2; i ++) printf("-");
    printf("+\033[40;37;1m ");
    GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + (y - 1));
    printf("\033[m \033[40;37;1m%*s", x, "");
    printf("\033[47;30;1m");

    int line = 1;
    for (size_t i = 0; i < C_ARRAY_LEN(help[state->current_state]); i ++) {
        if (help[state->current_state][i].key == NULL) break;
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
        printf("|%-*s - %-*s|", x1, help[state->current_state][i].key, x - x1 - 5, help[state->current_state][i].action);
    }
}

void redraw() {
    GOTO(0, 0);
    printf(RESET_GFX_MODE CLEAR_SCREEN);
#define PRINTF_DATA(num) printf(state->debug.hex ? "%02x" : "%d", (num))

    int offset_y = 0;
    int offset_x = 0;
    if (debugany()) offset_y ++;
    if (state->debug.data) offset_y ++;
    if (state->debug.unknowns) offset_y ++;
    if (state->debug.neighbours) offset_y += 4;
    if (state->debug.objects) offset_y ++;

    size_t level = state->current_level;
    struct DecompresssedRoom room = state->rooms.rooms[level].data;
    char room_name[25] = {0};
    assert(C_ARRAY_LEN(room_name) >= C_ARRAY_LEN(room.name));
    strncpy(room_name, room.name, C_ARRAY_LEN(room.name));
    int room_name_len;
    for (room_name_len = (int)C_ARRAY_LEN(room_name) - 1; room_name_len >= 0; room_name_len --) {
        if (room_name[room_name_len] == '\0' || isspace(room_name[room_name_len])) {
            room_name[room_name_len] = '\0';
        } else {
            room_name_len ++;
            break;
        }
    }
    struct RoomObject *object_underneath = NULL;
    struct SwitchObject *switch_underneath = NULL;
    struct SwitchChunk *chunk_underneath = NULL;
    struct SwitchObject *chunk_switch_underneath = NULL;

    int x = state->cursors[state->current_level].x;
    int y = state->cursors[state->current_level].y;
    assert(x >= 0);
    assert(y >= 0);
    assert(x < WIDTH_TILES);
    assert(y < HEIGHT_TILES);
    GOTO(2 * x, y + 1);
    uint8_t tile = room.tiles[TILE_IDX(x, y)];
    bool obj = false;
    bool sw = false;
    bool ch = false;
    bool sprite = false;
    size_t i;
    for (i = 0; i < room.num_objects; i ++) {
        struct RoomObject *object = room.objects + i;
        if (object->type == BLOCK &&
                x >= object->x && x < object->x + object->block.width &&
                y >= object->y && y < object->y + object->block.height) {
            obj = true;
            tile = object->tiles[(y - object->y) * object->block.width + (x - object->x)];
            object_underneath = object;
            break;
        } else if (object->type == SPRITE && x == object->x && y == object->y) {
            obj = true;
            sprite = true;
            tile = (object->sprite.type << 4) | object->sprite.damage;
            object_underneath = object;
            break;
        }
    }
    size_t obj_i = i;
    for (i = 0; i < room.num_switches; i ++) {
        struct SwitchObject *switcch = room.switches + i;
        if (switcch->chunks.length > 0 && switcch->chunks.data[0].type == PREAMBLE &&
                x == switcch->chunks.data[0].x && y == switcch->chunks.data[0].y) {
            sw = true;
            switch_underneath = switcch;
            break;
        }
    }
    size_t sw_i = i;
    for (i = 0; i < room.num_switches; i ++) {
        struct SwitchObject *switcch = room.switches + i;
        size_t c;
        for (c = 1; c < switcch->chunks.length; c ++) {
            struct SwitchChunk *chunk = switcch->chunks.data + c;
            if (chunk->type == TOGGLE_BLOCK) {
                if (chunk->dir == HORIZONTAL) {
                    if (y == chunk->y && x >= chunk->x && x < chunk->x + chunk->size) {
                        tile = chunk->on;
                        ch = true;
                        chunk_switch_underneath = switcch;
                        chunk_underneath = chunk;
                        break;
                    }
                } else if (x == chunk->x && y >= chunk->y && y < chunk->y + chunk->size) {
                    tile = chunk->on;
                    ch = true;
                    chunk_switch_underneath = switcch;
                    chunk_underneath = chunk;
                    break;
                }
            }
        }
    }
    if (state->debug.switches) {
        if (switch_underneath != NULL) {
            offset_y += 2 + switch_underneath->chunks.length;
        } else if (state->current_state == TILE_EDIT) {
            offset_y ++;
        }
    }

    if (state->current_state == GOTO_ROOM) {
        offset_y = 36 - MIN_HEIGHT;
    }

    assert(MIN_WIDTH + offset_x >= 2 * WIDTH_TILES);
    assert(MIN_HEIGHT + offset_y >= HEIGHT_TILES);
    if (state->screen_dimensions.x < MIN_WIDTH + offset_x || state->screen_dimensions.y < MIN_HEIGHT + offset_y) {
        char *message = NULL;
        assert(asprintf(&message,
                    "Required screen dimension is %dx%d. Currently %s%d\033[mx%s%d",
                    MIN_WIDTH + offset_x + 1, MIN_HEIGHT + offset_y + 1,
                    (state->screen_dimensions.x < MIN_WIDTH + offset_x + 1 ? "\033[31;1m" : "\033[32m"),
                    state->screen_dimensions.x,
                    (state->screen_dimensions.y < MIN_HEIGHT + offset_y + 1 ? "\033[31;1m" : "\033[32m"),
                    state->screen_dimensions.y
        ) >= 0);
        int x = state->screen_dimensions.x / 2 - (int)(strlen(message) - 7) / 2;
        int y = state->screen_dimensions.y / 2;
        if (x < 0) x = 0;
        if (x >= MIN_WIDTH) x = MIN_WIDTH - 1;
        if (y < 0) y = 0;
        if (y >= MIN_HEIGHT) y = MIN_HEIGHT - 1;
        GOTO(x, y);
        printf("%s", message);
        free(message);
        fflush(stdout);
        return;
    }

    GOTO(6, 0);
    printf("%s", state->debug.hex ? "[HEX]" : "[DEC]");

    GOTO(MIN_WIDTH / 2 - ((room_name_len + 7) / 2), 0);
    if (state->current_state == GOTO_ROOM) {
        if (state->partial_byte) {
            if (state->debug.hex) {
                printf("%x", state->partial_byte & 0xFF);
            } else {
                printf("%u", state->partial_byte & 0xFF);
            }
        } else {
            printf("\033[4;5m_\033[m");
        }
        printf("\033[4;5m_\033[m - \"???\"");
        for (int y = 0; y < HEIGHT_TILES; y ++) {
            for (int x = 0; x < WIDTH_TILES; x ++) {
                if (state->current_state == GOTO_ROOM) {
                    printf("  ");
                }
            }
            printf("\n");
        }

        GOTO(0,1);
        printf("\nEnter room number or q to go to main view\n\n");
        for (size_t i = 0; i < C_ARRAY_LEN(state->rooms.rooms) / 2; i ++) {
            size_t room_id = i;
            struct DecompresssedRoom *room = &state->rooms.rooms[room_id].data;
            assert(C_ARRAY_LEN(room_name) >= C_ARRAY_LEN(room->name));
            strncpy(room_name, room->name, C_ARRAY_LEN(room->name));
            int room_name_len;
            for (room_name_len = (int)C_ARRAY_LEN(room_name) - 1; room_name_len >= 0; room_name_len --) {
                if (room_name[room_name_len] == '\0' || isspace(room_name[room_name_len])) {
                    room_name[room_name_len] = '\0';
                } else {
                    room_name_len ++;
                    break;
                }
            }
            int printed = 0;
            uint8_t underline = 0;
            if (state->partial_byte && ((room_id / (state->debug.hex ? 16 : 10)) == (state->partial_byte & 0xFF))) {
                printf("\033[4;1m");
                underline = 1;
                printed += printf(state->debug.hex ? "%01lx" : "%01ld", room_id / (state->debug.hex ? 16 : 10));
                printf("\033[m");
                printed += printf(state->debug.hex ? "%01lx" : "%01ld", room_id % (state->debug.hex ? 16 : 10));
            } else {
                printed += printf(state->debug.hex ? "%02lx" : "%02ld", room_id);
            }
            printed += printf(" - \"");
            if (underline) {
                printf("\033[4;1m");
            }
            printed += printf("%s", room_name);
            if (underline) {
                printf("\033[m");
                underline = 0;
            }
            printed += printf("\"");
            for (int j = printed; j < WIDTH_TILES; j ++) {
                printed += printf(" ");
            }

            room_id = C_ARRAY_LEN(state->rooms.rooms) / 2 + i;
            room = &state->rooms.rooms[room_id].data;
            assert(C_ARRAY_LEN(room_name) >= C_ARRAY_LEN(room->name));
            strncpy(room_name, room->name, C_ARRAY_LEN(room->name));
            for (room_name_len = (int)C_ARRAY_LEN(room_name) - 1; room_name_len >= 0; room_name_len --) {
                if (room_name[room_name_len] == '\0' || isspace(room_name[room_name_len])) {
                    room_name[room_name_len] = '\0';
                } else {
                    room_name_len ++;
                    break;
                }
            }
            if (state->partial_byte && ((room_id / (state->debug.hex ? 16 : 10)) == (state->partial_byte & 0xFF))) {
                printf("\033[4;1m");
                underline = 1;
                printf(state->debug.hex ? "%01lx" : "%01ld", room_id / (state->debug.hex ? 16 : 10));
                printf("\033[m");
                printf(state->debug.hex ? "%01lx" : "%01ld", room_id % (state->debug.hex ? 16 : 10));
            } else {
                printf(state->debug.hex ? "%02lx" : "%02ld", room_id);
            }
            printf(" - \"");
            if (underline) {
                printf("\033[4;1m");
            }
            printf("%s", room_name);
            if (underline) {
                printf("\033[m");
                underline = 0;
            }
            printf("\"");

            if (i != C_ARRAY_LEN(state->rooms.rooms) / 2 - 1) printf("\n");
        }
        goto show_help_if_needed;
    } else {
        PRINTF_DATA((int)level);
        printf(" - \"");
        if (state->current_state == EDIT_ROOMNAME) {
            for (size_t i = 0; i < state->roomname_cursor; i ++) {
                printf("%c", state->room_name[i]);
            }
            printf("\033[40;4;5m%c\033[m", state->room_name[state->roomname_cursor] ? state->room_name[state->roomname_cursor] : '_');
            for (size_t i = state->roomname_cursor + 1; i < C_ARRAY_LEN(state->room_name); i ++) {
                if (state->room_name[i] == '\0') break;
                printf("%c", state->room_name[i]);
            }
        } else {
            printf("%s", room_name);
        }
        printf("\"");
    }


    if (state->current_state == GOTO_SWITCH) {
        for (int y = 0; y < HEIGHT_TILES; y ++) {
            for (int x = 0; x < WIDTH_TILES; x ++) {
                uint8_t tile = room.tiles[TILE_IDX(x, y)];
                struct SwitchObject *found_switch = NULL;
                int s;
                for (s = 0; s < room.num_switches; s ++) {
                    struct SwitchObject *switcch = room.switches + s;
                    if (switcch->chunks.length > 0 && switcch->chunks.data[0].type == PREAMBLE &&
                            x == switcch->chunks.data[0].x && y == switcch->chunks.data[0].y) {
                        found_switch = switcch;
                        break;
                    }
                }
                GOTO(2 * x, y + 1);
                if (found_switch) {
                    printf("\033[4%d;30;1m", (s % 3) + 4);
                    if (s > 15) {
                        // FIXME support 2 digit nums
                        UNREACHABLE();
                    }
                    printf(" %x", s);
                    printf("\033[m");
                } else {
                    if (tile == BLANK_TILE) {
                        printf("  ");
                    } else {
                        printf("%02X", tile);
                    }
                }
            }
        }
    } else {
        for (int y = 0; y < HEIGHT_TILES; y ++) {
            for (int x = 0; x < WIDTH_TILES; x ++) {
                uint8_t tile = room.tiles[TILE_IDX(x, y)];
                bool colored = false;
                if (state->debug.switches)
                for (int s = 0; s < room.num_switches; s ++) {
                    struct SwitchObject *sw = room.switches + s;
                    assert(sw->chunks.length > 0 && sw->chunks.data[0].type == PREAMBLE);
                    if (x == sw->chunks.data[0].x && y == sw->chunks.data[0].y) {
                        colored = true;
                        printf("\033[4%d;30m", (s % 3) + 4);
                        break;
                    }
                    for (size_t c = 1; !colored && c < sw->chunks.length; c ++) {
                        struct SwitchChunk *chunk = sw->chunks.data + c;
                        if (chunk->type == TOGGLE_BLOCK) {
                            if (chunk->dir == HORIZONTAL) {
                                if (y == chunk->y && x >= chunk->x && x < chunk->x + chunk->size) {
                                    printf("\033[3%d;40m", (s % 3) + 4);
                                    colored = true;
                                    tile = chunk->on;
                                }
                            } else if (x == chunk->x && y >= chunk->y && y < chunk->y + chunk->size) {
                                printf("\033[3%d;40m", (s % 3) + 4);
                                colored = true;
                                tile = chunk->on;
                            }
                        }
                    }
                }
                if (colored || tile != BLANK_TILE) {
                    GOTO(2 * x, y + 1);
                    printf("%02X", tile);
                    if (colored) printf("\033[m");
                }
            }
        }
    }

    uint8_t dirty[MIN_HEIGHT * MIN_WIDTH] = {0};
    if (state->debug.objects)
    for (size_t i = 0; i < room.num_objects; i ++) {
        struct RoomObject *object = room.objects + i;
        assert(object->x >= 0);
        assert(object->y >= 0);
        assert(object->x < WIDTH_TILES);
        assert(object->y < HEIGHT_TILES);
        switch (object->type) {
            case BLOCK:
                assert(object->x + object->block.width <= WIDTH_TILES);
                assert(object->y + object->block.height <= HEIGHT_TILES);
                printf("\033[3%ldm", (i % 3) + 1);
                for (int y = object->y; y < object->y + object->block.height; y ++) {
                    GOTO(2 * object->x, y + 1);
                    for (int x = object->x; x < object->x + object->block.width; x ++) {
                        if (dirty[y * MIN_WIDTH + x] == 1) continue;
                        dirty[y * MIN_WIDTH + x] = 1;
                        uint8_t tile = object->tiles[(y - object->y) * object->block.width + (x - object->x)];
                        if (tile == BLANK_TILE) {
                            printf("  ");
                        } else {
                            printf("%02X", tile);
                        }
                    }
                }
                break;

            case SPRITE:
                if (state->current_state == GOTO_SWITCH) break;
                GOTO(2 * object->x, object->y + 1);
                assert((unsigned)(object->y * MIN_WIDTH + object->x) < sizeof(dirty));
                if (dirty[object->y * MIN_WIDTH + object->x] == 1) continue;
                dirty[object->y * MIN_WIDTH + object->x] = 1;
                printf("\033[4%ld;30m%d%d", (i % 3) + 1, object->sprite.type, object->sprite.damage);
                break;
        }
        printf("\033[m");
    }

    if (state->current_state != NORMAL) {
        GOTO(2 * x, y + 1);
        size_t ch_i = i;
        if (obj) {
            if (sprite) {
                printf("\033[3%ld;40;1m", (obj_i % 3) + 1);
            } else {
                printf("\033[30;4%ld;1m", (obj_i % 8) + 1);
            }
        }
        if (!obj && sw) {
            printf("\033[3%ld;1m", (sw_i % 3) + 4);
        }
        if (!obj && !sw && ch) {
            printf("\033[4%ld;30;1m", (ch_i % 3) + 4);
        }
        if (obj || sw || ch) {
            if (state->current_state == GOTO_SWITCH) {
                printf("@");
            } else {
                printf("%02X", tile);
            }
            printf("\033[m");
        } else {
            printf("\033[47;30;1m%02X\033[m", tile);
        }
        if (state->current_state != EDIT_ROOMDETAILS_NUM && state->partial_byte) {
            GOTO(2 * x, y + 1);
            if (obj) {
                if (sprite) {
                    printf("\033[30;4%ld;1m", (obj_i % 3) + 1);
                } else {
                    printf("\033[3%ld;1m", (obj_i % 8) + 1);
                }
            } else if (sw) {
                printf("\033[30;4%ld;1m", (sw_i % 3) + 4);
            } else if (ch) {
                printf("\033[40;3%ld;1m", (ch_i % 3) + 4);
            } else {
                printf("\033[1m");
            }
            printf("%X\033[m", (state->partial_byte & 0xFF) >> 4);
        }
    } else {
        assert(state->cursors[state->current_level].x >= 0);
        assert(state->cursors[state->current_level].y >= 0);
        assert(state->cursors[state->current_level].x < WIDTH_TILES);
        assert(state->cursors[state->current_level].y < HEIGHT_TILES);
        if (state->cursors[state->current_level].y - 2 >= 0) {
            GOTO(2 * state->cursors[state->current_level].x, state->cursors[state->current_level].y - 1);
            printf("\033[41;30;1m@@\033[m");
        }
        if (state->cursors[state->current_level].y - 1 >= 0) {
            GOTO(2 * state->cursors[state->current_level].x, state->cursors[state->current_level].y);
            printf("\033[41;30;1m@@\033[m");
        }
        if (state->cursors[state->current_level].y >= 0) {
            GOTO(2 * state->cursors[state->current_level].x, state->cursors[state->current_level].y + 1);
            printf("\033[41;30;1m@@\033[m");
        }
    }

    if (state->debug.pos) {
        GOTO(0, 0);
        PRINTF_DATA(state->cursors[state->current_level].x);
        printf(",");
        PRINTF_DATA(state->cursors[state->current_level].y);
    }

    int bottom = HEIGHT_TILES + 1;
    if (debugany()) bottom ++;
    if (state->debug.data) {
        GOTO(0, bottom); bottom ++;
        switch (state->current_state) {
            case EDIT_ROOMDETAILS:
                printf("\033[1;4mb\033[mkgrnd: ");PRINTF_DATA(room.background);
                printf(", tiles: ");PRINTF_DATA(room.tile_offset);
                printf(", dmg: ");PRINTF_DATA(room.room_damage);
                printf(", gravity (|): ");PRINTF_DATA(room.gravity_vertical);
                printf(", gravity (-): ");PRINTF_DATA(room.gravity_horizontal);
                break;

            case EDIT_ROOMDETAILS_NUM:
                if (state->room_detail == 'b') {
                    printf("\033[1;4mbkgrnd\033[m: ");
                    if (state->partial_byte) {
                        if (state->debug.hex) {
                            printf("%x", state->partial_byte & 0xFF);
                        } else {
                            printf("%u", state->partial_byte & 0xFF);
                        }
                    } else {
                        printf("\033[4;5m_\033[m");
                    }
                    printf("\033[4;5m_\033[m");
                } else {
                    printf("bkgrnd: ");PRINTF_DATA(room.background);
                }
                printf(", tiles: ");PRINTF_DATA(room.tile_offset);
                printf(", dmg: ");PRINTF_DATA(room.room_damage);
                printf(", gravity (|): ");PRINTF_DATA(room.gravity_vertical);
                printf(", gravity (-): ");PRINTF_DATA(room.gravity_horizontal);
                break;

            default:
                printf("bkgrnd: ");PRINTF_DATA(room.background);
                printf(", tiles: ");PRINTF_DATA(room.tile_offset);
                printf(", dmg: ");PRINTF_DATA(room.room_damage);
                printf(", gravity (|): ");PRINTF_DATA(room.gravity_vertical);
                printf(", gravity (-): ");PRINTF_DATA(room.gravity_horizontal);
        }
    }
    if (state->debug.unknowns) {
        GOTO(0, bottom); bottom ++;
        printf("UNKNOWN_b: ");PRINTF_DATA(room.UNKNOWN_b);
        printf(", UNKNOWN_c: ");PRINTF_DATA(room.UNKNOWN_c);
        printf(", UNKNOWN_e: ");PRINTF_DATA(room._num_switches & 0x3);
        printf(", UNKNOWN_f: ");PRINTF_DATA(room.UNKNOWN_f);
    }
    if (state->debug.neighbours) {
        char neighbour_name[25] = {0};
        struct DecompresssedRoom neighbour_room = {0};
#define READ_NEIGHBOUR(id) do { \
neighbour_room = state->rooms.rooms[(id)].data; \
assert(C_ARRAY_LEN(neighbour_name) >= C_ARRAY_LEN(neighbour_room.name)); \
strncpy(neighbour_name, neighbour_room.name, C_ARRAY_LEN(neighbour_room.name)); \
for (int i = C_ARRAY_LEN(neighbour_name) - 1; i >= 0; i --) { \
    if (neighbour_name[i] == '\0' || isspace(neighbour_name[i])) { \
        neighbour_name[i] = '\0'; \
    } else { \
        break; \
    } \
} \
} while (0)

        GOTO(0, bottom); bottom ++;
        printf("left: ");PRINTF_DATA(room.room_west);
        READ_NEIGHBOUR(room.room_west);
        printf(" - \"%s\"", neighbour_name);
        GOTO(0, bottom); bottom ++;
        printf("down: ");PRINTF_DATA(room.room_south);
        READ_NEIGHBOUR(room.room_south);
        printf(" - \"%s\"", neighbour_name);
        GOTO(0, bottom); bottom ++;
        printf("up: ");PRINTF_DATA(room.room_north);
        READ_NEIGHBOUR(room.room_north);
        printf(" - \"%s\"", neighbour_name);
        GOTO(0, bottom); bottom ++;
        printf("right: ");PRINTF_DATA(room.room_east);
        READ_NEIGHBOUR(room.room_east);
        printf(" - \"%s\"", neighbour_name);
    }

    if (state->current_state == EDIT_ROOMNAME) {
        GOTO(0, bottom); bottom +=2;
        printf("\nEnter new room name in space highlighted at top of screen");
        goto show_help_if_needed;
    }

    if (state->current_state == GOTO_SWITCH) {
        GOTO(0, bottom); bottom +=2;
        printf("\nEnter switch id or q to go to main view");
        goto show_help_if_needed;
    }

    if (state->debug.objects) {
        GOTO(0, bottom); bottom ++;
        if (object_underneath != NULL) {
            switch (object_underneath->type) {
                case BLOCK:
                    printf("block: (x,y)=");
                    PRINTF_DATA(object_underneath->x);
                    printf(",");
                    PRINTF_DATA(object_underneath->y);
                    printf(" (w,h)=");
                    PRINTF_DATA(object_underneath->block.width);
                    printf(",");
                    PRINTF_DATA(object_underneath->block.height);
                    break;

                case SPRITE:
                    printf("sprite: ");
                    switch(object_underneath->sprite.type) {
                        case SHARK: 
                            switch (object_underneath->sprite.damage) {
                                case 1:
                                case 2:
                                    printf("Shark");
                                    break;

                                case 3: printf("Mysterio"); break;
                                case 4: printf("Mary Jane"); break;

                                default: UNREACHABLE();
                            }
                            break;

                        case MUMMY: printf("Mummy"); break;
                        case BLUE_MAN: printf("Blue man"); break;
                        case WOLF: printf("Wolf"); break;
                        case R2D2: printf("R2D2"); break;
                        case DINOSAUR: printf("Dinosaur"); break;
                        case RAT: printf("Rat"); break;
                        case SHOTGUN_LADY: printf("Shotgun_lady"); break;

                        default: UNREACHABLE();
                    }
                    printf(" (x,y)=");
                    PRINTF_DATA(object_underneath->x);
                    printf(",");
                    PRINTF_DATA(object_underneath->y);
                    printf(" (dmg)=");
                    PRINTF_DATA(object_underneath->sprite.damage);
                    break;
            }
        } else if (state->current_state == TILE_EDIT) {
            printf("No object here, TODO create new?");
        }
    }

    if (state->debug.switches) {
        GOTO(0, bottom); bottom ++;
    (void)chunk_switch_underneath;
    (void)chunk_underneath;
        if (switch_underneath != NULL) {
#define BOOL_S(b) ((b) ? "true" : "false")
            assert(switch_underneath->chunks.length >= 1);
            struct SwitchChunk *preamble = switch_underneath->chunks.data;
            printf("switch: (x,y)=%d,%d (entry)=%s (once)=%s (side)=%s\n",
                    preamble->x, preamble->y,
                    BOOL_S(preamble->room_entry), BOOL_S(preamble->one_time_use),
                    SWITCH_SIDE(preamble->side));
            if (switch_underneath->chunks.length > 1) {
                printf("  chunks:\n");
                for (size_t i = 1; i < switch_underneath->chunks.length; i ++) {
                    printf("    ");
                    struct SwitchChunk *chunk = switch_underneath->chunks.data + i;
                    switch (chunk->type) {
                        case PREAMBLE: UNREACHABLE();
                        case TOGGLE_BLOCK: {
                            size_t overflow = WIDTH_TILES * HEIGHT_TILES;
                            size_t point = chunk->y * WIDTH_TILES + chunk->x;
                            if (point >= overflow) {
                                printf("memory:");
                                size_t offset = point - overflow;
                                size_t end = offsetof(struct DecompresssedRoom, end_marker) - overflow;
                                if (offset >= end) {
                                    printf("way out of bounds");
                                    assert(false);
                                }
                                switch (offset) {
                                    case 0: printf(" tile_offset"); break;
                                    case 1: printf(" background"); break;
                                    case 2: printf(" room_north"); break;
                                    case 3: printf(" room_east"); break;
                                    case 4: printf(" room_south"); break;
                                    case 5: printf(" room_west"); break;
                                    case 6: printf(" room_damage"); break;
                                    case 7: printf(" gravity_vertical"); break;
                                    case 8: printf(" gravity_horizontal"); break;
                                    case 9: printf(" UNKNOWN_b"); break;
                                    case 10: printf(" UNKNOWN_c"); break;
                                    case 11: printf(" num_objects"); break;
                                    case 12: printf(" _num_switches"); break;
                                    case 13: printf(" UNKNOWN_f"); break;
                                    default: printf(" somewhere inside name"); break;
                                }
                                printf(" (on/off)=%02x/%02x", chunk->on, chunk->off);
                            } else {
                                printf("block - (x,y)=");
                                PRINTF_DATA(chunk->x);
                                printf(",");
                                PRINTF_DATA(chunk->y);
                                printf(" (%s)=%d (on/off)=", (chunk->dir == VERTICAL ? "height" : "width"), chunk->size);
                                PRINTF_DATA(chunk->on);
                                printf("/");
                                PRINTF_DATA(chunk->off);
                            }
                        }; break;

                        case TOGGLE_BIT:
                        {
                            printf("bit - (idx)=%d (on/off)=%d/%d (mask)=", chunk->index, chunk->on, chunk->off);
                            PRINTF_DATA(chunk->bitmask);
                        }; break;

                        case TOGGLE_OBJECT:
                        {
                            printf("object - (idx)=%d (test)=", chunk->index);
                            PRINTF_DATA(chunk->test);
                            printf(" (value)=");
                            switch (chunk->value & MOVE_LEFT) {
                                case MOVE_LEFT:
                                    switch (chunk->value & MOVE_UP) {
                                        case MOVE_UP: printf("up+left"); break;
                                        case MOVE_DOWN: printf("down+left"); break;
                                        case '\0': printf("left"); break;
                                    }
                                    break;

                                case MOVE_RIGHT:
                                    switch (chunk->value & MOVE_UP) {
                                        case MOVE_UP: printf("up+right"); break;
                                        case MOVE_DOWN: printf("down+right"); break;
                                        case '\0': printf("right"); break;
                                    }
                                    break;

                                case '\0':
                                    switch (chunk->value & MOVE_UP) {
                                        case MOVE_UP: printf("up"); break;
                                        case MOVE_DOWN: printf("down"); break;
                                        case '\0': printf("stop"); break;
                                    }
                                    break;
                            }

                            printf(" (value_without_direction)=");
                            PRINTF_DATA(chunk->value & ~(MOVE_UP | MOVE_DOWN | MOVE_LEFT | MOVE_RIGHT));
                        }; break;

                        default: UNREACHABLE();
                    }
                    if (i < switch_underneath->chunks.length - 1) printf("\n");
                }
            }
        } else if (state->current_state == TILE_EDIT) {
            printf("No switch here, TODO create new?");
        }
    }

    /* GOTO(0, bottom); bottom ++; */
    /* uint8_array rest = state->rooms.rooms[level].rest; */
    /* int pre = printf("Rest (length=%zu):", rest.length); */
    /* for (size_t i = 0; i < rest.length; i ++) { */
    /*     if (pre + 3 * (i + 2) > (size_t)state->screen_dimensions.x) { */
    /*         printf("..."); */
    /*         break; */
    /*     } */
    /*     printf(" "); */
    /*     PRINTF_DATA(rest.data[i]); */
    /* } */

show_help_if_needed:
    if (state->help) show_help();

    fflush(stdout);
}

void get_screen_dimensions() {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

    state->screen_dimensions.x = w.ws_col;
    state->screen_dimensions.y = w.ws_row;
}

void sigwinch_handler(int arg) {
    assert(arg == SIGWINCH);
    if (state != NULL) {
        state->resized = true;
    }
}

void setup() {
    assert(state == NULL && "Already setup");
    printf(SAVE_CURSOR HIDE_CURSOR SAVE_SCREEN ENABLE_ALT_BUFFER);

    state = calloc(1, sizeof(game_state));
    assert(state != NULL && "Not enough memory");
    state->size = sizeof(game_state);

    debugalltoggle(state);
    state->current_state = TILE_EDIT;
    state->partial_byte = 0;

    assert(tcgetattr(STDIN_FILENO, &state->original_termios) == 0);

    struct termios new = state->original_termios;
    new.c_lflag &= ~(ICANON | ECHO);
    new.c_iflag &= ~(IXON | IXOFF);
    assert(tcsetattr(STDIN_FILENO, TCSANOW, &new) == 0);

    get_screen_dimensions();

    assert(readRooms(&state->rooms) && "Check that you have ROOMS.SPL");

    for (size_t i = 0; i < C_ARRAY_LEN(state->cursors); i ++) {
        state->cursors[i].x = WIDTH_TILES / 2;
        state->cursors[i].y = HEIGHT_TILES / 2;
    }
    state->current_level = 1;
    state->cursors[state->current_level].x = 3;
    state->cursors[state->current_level].y = HEIGHT_TILES - 2;


    state->help = true;
    state->debug.hex = true;
}

typedef void *(*main_fn)(char *library, void *call_state);
void *loop_main(char *library, void *call_state) {
    struct stat library_stat;
    assert(stat(library, &library_stat) == 0);
    struct stat start_rooms_stat;
    assert(stat("ROOMS.SPL", &start_rooms_stat) == 0);

    state = call_state;
    if (state != NULL && state->size != sizeof(game_state)) {
        free(state);
        state = NULL;
    }
    if (state == NULL) setup();
    else get_screen_dimensions();

    signal(SIGWINCH, sigwinch_handler);
    signal(SIGINT, end);

    while (true) {
        struct stat rooms_stat;
        if (any_source_newer(&library_stat)) {
            // rebuild
            char *build_cmd = NULL;
            assert(asprintf(&build_cmd, "%s %s %s", LIBRARY_BUILD_CMD, library, __FILE__) > 0);
            fprintf(stderr, "Rebuilding editor library: %s\n", build_cmd);
            int ret = system(build_cmd);
            if (ret == 0) {
                fprintf(stderr, "Reloading library\n");
                free(build_cmd);
                return state;
            }
            if (ret == -1) perror("system");
            fprintf(stderr, "Recompile failed\n");
            free(build_cmd);
        }
        if (stat("ROOMS.SPL", &rooms_stat) == 0) {
            if (TIME_NEWER(rooms_stat.st_mtim, start_rooms_stat.st_mtim)) {
                fprintf(stderr, "Reloading ROOMS.SPL\n");
                freeRoomFile(&state->rooms);
                assert(readRooms(&state->rooms) && "Check that you have ROOMS.SPL");
                start_rooms_stat = rooms_stat;
            }
        }

        if (state->resized) {
            get_screen_dimensions();
            state->resized = false;
        }
        process_input();
        update();
        redraw();
        usleep(50000);
    }
    UNREACHABLE();
    return NULL;
}

// If this function is edited, then you must rebuild the application
int editor_main() {
    char *basename = strdup(__FILE__);
    char *dot = strrchr(basename, '.');
    if (dot != NULL) *dot = '\0';
    char *library = NULL;
    assert(asprintf(&library, "./%s.so", basename) > 0);
    free(basename);
    basename = NULL;
    void *saved_state = NULL;
    while (true) {
        void *module = dlopen(library, RTLD_NOW);
        if (module == NULL) {
            char *build_cmd = NULL;
            assert(asprintf(&build_cmd, "%s %s %s", LIBRARY_BUILD_CMD, library, __FILE__) > 0);
            fprintf(stderr, "Building editor library: %s\n", build_cmd);
            assert(system(build_cmd) == 0);
            module = dlopen(library, RTLD_NOW);
            assert(module != NULL);
        }
        void *(*function)(char *library, void *state);
        *(void **) (&function) = dlsym(module, "loop_main");
        assert(function != NULL && "Could not find loop_main");
        saved_state = function(library, saved_state);
        dlclose(module);
        if (saved_state == NULL) break;
    }

    free(library);
    return 0;
}
