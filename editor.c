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

bool any_source_newer(struct timespec test_time) {
    struct stat file_stat;
    char *files[] = {
        __FILE__,
        "room.c",
        "room.h",
        "array.h"
    };
    for (size_t i = 0; i < C_ARRAY_LEN(files); i ++) {
        if (stat(files[i], &file_stat) == 0) {
            if (TIME_NEWER(file_stat.st_mtim, test_time)) {
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

    GOTO_OBJECT,
    GOTO_SWITCH,
    GOTO_ROOM,

    EDIT_ROOMNAME,
    EDIT_ROOMDETAILS,
    EDIT_ROOMDETAILS_NUM,
    EDIT_ROOMDETAILS_ROOM,

    EDIT_SWITCHDETAILS,

    EDIT_SWITCHDETAILS_SELECT_CHUNK,
    EDIT_SWITCHDETAILS_CHUNK_BLOCK_DETAILS,
    EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS,
    EDIT_SWITCHDETAILS_CHUNK_BIT_DETAILS,
    EDIT_SWITCHDETAILS_CHUNK_OBJECT_DETAILS,

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

    size_t current_switch;
    size_t current_chunk;
    bool switch_on;
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
                case GOTO_OBJECT:
                {
                    if (isxdigit(buf[i])) {
                        struct DecompresssedRoom *room = &state->rooms.rooms[*cursorlevel].data;
                        if (room->num_objects > 15) {
                            // FIXME support 2 digit nums
                            UNREACHABLE();
                        }
                        size_t id;
                        if (buf[i] > '9') {
                            id = tolower(buf[i]) - 'a' + 10;
                        } else {
                            id = buf[i] - '0';
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
                    } else if (buf[i] == ESCAPE) {
                        if (state->help) {
                            state->help = false;
                        } else {
                            state->current_state = state->previous_state;
                            state->current_switch = 0;
                            state->switch_on = false;
                            state->current_chunk = 0;
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
                            state->current_switch = 0;
                            state->switch_on = false;
                            state->current_chunk = 0;
                            state->previous_state = NORMAL;
                        }
                    } else if (buf[i] == '?') {
                        state->help = !state->help;
                    }
                    i ++;
                }; break;

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
                    } else if (buf[i] == ESCAPE) {
                        if (state->help) {
                            state->help = false;
                        } else {
                            state->current_state = state->previous_state;
                            state->current_switch = 0;
                            state->switch_on = false;
                            state->current_chunk = 0;
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
                            state->current_switch = 0;
                            state->switch_on = false;
                            state->current_chunk = 0;
                            state->previous_state = NORMAL;
                        }
                    } else if (buf[i] == '?') {
                        state->help = !state->help;
                    }
                    i ++;
                }; break;

                case GOTO_ROOM:
                {
                    if (state->partial_byte && ((state->debug.hex && isxdigit(buf[i])) || (!state->debug.hex && isdigit(buf[i])))) {
                        int digit;
                        if (buf[i] > '9') {
                            digit = tolower(buf[i]) - 'a' + 10;
                        } else {
                            digit = buf[i] - '0';
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
                    } else if (state->roomname_cursor == 0 && state->partial_byte == 0 && buf[i] >= '0' &&
                                buf[i] <= (state->debug.hex ? '3' : '6')) {
                        state->partial_byte = 0xFF00 | (buf[i] - '0');
                    } else if (buf[i] == 0x7f) {
                        if (state->roomname_cursor) {
                            state->room_name[--state->roomname_cursor] = 0;
                        } else {
                            state->partial_byte = 0;
                        }
                    } else if (buf[i] == ESCAPE) {
                        if (i + 1 < n) {
                            switch (buf[i + 1]) {
                                case '[':
                                    if (i + 2 < n) {
                                        // CSI sequence
                                        i += 2;
                                        uint8_t arg = 0;
                                        while (i < n) {
                                            if (isdigit(buf[i])) {
                                                arg = 10 * arg + buf[i] - '0';
                                            } else if (buf[i] == ';') {
                                                arg = 0;
                                            } else {
                                                switch (buf[i]) {
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

                                                            default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[i], arg);
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

                                                    default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[i], arg);
                                                }
                                                break;
                                            }
                                            i ++;
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
                        if (buf[i] == '[') {
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
                                        } else {
                                            fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[i], arg);
                                        }
                                        break;
                                    }
                                    i ++;
                                }
                            } else {
                                UNREACHABLE();
                            }
                        } else if (state->partial_byte == 0 && isprint(buf[i])) {
                            if (state->roomname_cursor < C_ARRAY_LEN(state->room_name) - 1) {
                                state->room_name[state->roomname_cursor++] = buf[i];

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
                        state->current_switch = 0;
                        state->switch_on = false;
                        state->current_chunk = 0;
                        state->previous_state = NORMAL;
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        assert(writeRooms(&state->rooms));
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

                                                    default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[i], arg);
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
                            state->current_switch = 0;
                            state->switch_on = false;
                            state->current_chunk = 0;
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
                    if (state->help) {
                        switch (buf[i]) {
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
                        i ++;
                        continue;
                    }

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

                        case 0x08:
                            state->debug.hex = !state->debug.hex;
                            break;

                        case '?':
                        case 0x1F:
                            state->help = !state->help;
                            fallthrough;
                        default: fprintf(stderr, "buf[i] = %02x\n", buf[i]); UNREACHABLE();changed = 0;
                    }
                    i++;
                    if (changed) {
                        state->current_state = state->previous_state;
                        state->current_switch = 0;
                        state->switch_on = false;
                        state->current_chunk = 0;
                        state->previous_state = NORMAL;
                    }
                }; break;

                case EDIT_ROOMDETAILS:
                {
                    if (buf[i] == ESCAPE && i + 1 < n && buf[i + 1] == '[') {
                        // CSI sequence
                        i += 2;
                        uint8_t arg = 0;
                        while (i < n) {
                            if (isdigit(buf[i])) {
                                arg = 10 * arg + buf[i] - '0';
                            } else if (buf[i] == ';') {
                                arg = 0;
                            } else {
                                switch (buf[i]) {
                                    case 'A':
                                    case 'B':
                                    case 'C':
                                    case 'D':
                                        state->current_state = EDIT_ROOMDETAILS_ROOM;
                                        state->current_switch = 0;
                                        state->switch_on = false;
                                        state->current_chunk = 0;
                                        state->room_detail = buf[i];
                                        break;
                                }
                                break;
                            }
                            i ++;
                        }
                    } else {
                        switch (buf[i]) {
                            case 'h':
                            case 'j':
                            case 'k':
                            case 'l':
                                state->room_detail = buf[i];
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
                                state->room_detail = buf[i];
                                state->current_state = EDIT_ROOMDETAILS_NUM;
                                state->current_switch = 0;
                                state->switch_on = false;
                                state->current_chunk = 0;
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
                                if (buf[i] == '?') {
                                    state->help = !state->help;
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
                    } else if (buf[i] == 0x7f) {
                        state->partial_byte = 0;
                    } else if (buf[i] == ESCAPE) {
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
                            state->current_state = EDIT_ROOMDETAILS;
                            state->current_switch = 0;
                            state->switch_on = false;
                            state->current_chunk = 0;
                            state->partial_byte = 0;
                            state->room_detail = 0;
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
                                            } else {
                                                fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[i], arg);
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

                case EDIT_ROOMDETAILS_ROOM:
                {
                    if (state->partial_byte && ((state->debug.hex && isxdigit(buf[i])) || (!state->debug.hex && isdigit(buf[i])))) {
                        int digit;
                        if (buf[i] > '9') {
                            digit = tolower(buf[i]) - 'a' + 10;
                        } else {
                            digit = buf[i] - '0';
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
                    } else if (state->roomname_cursor == 0 && state->partial_byte == 0 && buf[i] >= '0' &&
                                buf[i] <= (state->debug.hex ? '3' : '6')) {
                        state->partial_byte = 0xFF00 | (buf[i] - '0');
                    } else if (buf[i] == 0x7f) {
                        if (state->roomname_cursor) {
                            state->room_name[--state->roomname_cursor] = 0;
                        } else {
                            state->partial_byte = 0;
                        }
                    } else if (buf[i] == ESCAPE) {
                        if (i + 1 < n) {
                            if (buf[i + 1] == '[') {
                                if (i + 2 < n) {
                                    // CSI sequence
                                    i += 2;
                                    uint8_t arg = 0;
                                    while (i < n) {
                                        if (isdigit(buf[i])) {
                                            arg = 10 * arg + buf[i] - '0';
                                        } else if (buf[i] == ';') {
                                            arg = 0;
                                        } else {
                                            switch (buf[i]) {
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

                                                        default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[i], arg);
                                                    }
                                                }; break;

                                                default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[i], arg);
                                            }
                                            break;
                                        }
                                        i ++;
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
                        if (buf[i] == '[') {
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
                                            if (state->roomname_cursor) {
                                                state->room_name[--state->roomname_cursor] = 0;
                                            } else {
                                                state->partial_byte = 0;
                                            }
                                        } else {
                                            fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[i], arg);
                                        }
                                        break;
                                    }
                                    i ++;
                                }
                            } else {
                                UNREACHABLE();
                            }
                        } else if (state->partial_byte == 0 && isprint(buf[i])) {
                            if (state->roomname_cursor < C_ARRAY_LEN(state->room_name) - 1) {
                                state->room_name[state->roomname_cursor++] = buf[i];

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
                    i ++;
                }; break;

                case EDIT_SWITCHDETAILS:
                {
                    if (buf[i] == ESCAPE && i + 1 < n && buf[i + 1] == '[') {
                        // CSI sequence
                        i += 2;
                        uint8_t arg = 0;
                        while (i < n) {
                            if (isdigit(buf[i])) {
                                arg = 10 * arg + buf[i] - '0';
                            } else if (buf[i] == ';') {
                                arg = 0;
                            } else {
                                switch (buf[i]) {
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
                                            }; break;

                                            default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[i], arg);
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

                                    default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[i], arg);
                                }
                                break;
                            }
                            i ++;
                        }
                    } else {
                        switch (buf[i]) {
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
                                            state->current_state = EDIT_SWITCHDETAILS_CHUNK_BIT_DETAILS;
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
                                    i++;
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
                                if (buf[i] == '?') {
                                    state->help = !state->help;
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
                                }
                        }
                    }
                    i++;
                }; break;

                case EDIT_SWITCHDETAILS_SELECT_CHUNK:
                {
                    if (isxdigit(buf[i])) {
                        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                        if (sw->chunks.length > 16) {
                            // FIXME support 2 digit nums, remove define above when done (used in staticdefines)
                            fprintf(stderr, "%s:%d: UNIMPLEMENTED: support two digit numbers", __FILE__, __LINE__);
                            UNREACHABLE();
                        }
                        size_t id;
                        if (buf[i] > '9') {
                            id = tolower(buf[i]) - 'a' + 10;
                        } else {
                            id = buf[i] - '0';
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
                                    state->current_state = EDIT_SWITCHDETAILS_CHUNK_BIT_DETAILS;
                                    break;

                                case TOGGLE_OBJECT:
                                    state->current_state = EDIT_SWITCHDETAILS_CHUNK_OBJECT_DETAILS;
                                    break;

                                default: UNREACHABLE();
                            }
                        }
                    } else if (buf[i] == 0x7f) {
                        state->partial_byte = 0;
                    } else if (buf[i] == ESCAPE) {
                        if (state->help) {
                            state->help = false;
                        } else {
                            state->current_state = EDIT_SWITCHDETAILS;
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
                            state->current_state = EDIT_SWITCHDETAILS;
                        }
                    } else if (buf[i] == '?') {
                        state->help = !state->help;
                    } else if (buf[i] == '+') {
                        // create new one
                        struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
                        struct SwitchObject *sw = room->switches + state->current_switch - 1;
                        if (sw->chunks.length == 16) {
                            i++;
                            break;
                        }
                        ARRAY_ADD(sw->chunks, ((struct SwitchChunk){ .type = TOGGLE_BLOCK }));
                        state->switch_on = false;
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        assert(writeRooms(&state->rooms));
                    }
                    i ++;
                }; break;

                case EDIT_SWITCHDETAILS_CHUNK_BIT_DETAILS:
                {
                    if (isxdigit(buf[i])) {
                        uint8_t b = 0;
                        if (isdigit(buf[i])) {
                            b = *buf - '0';
                        } else {
                            b = 10 + tolower(buf[i]) - 'a';
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
                            chunk->index = index;
                            state->partial_byte = 0;
                            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                            assert(writeRooms(&state->rooms));
                        } else {
                            state->partial_byte = 0xFF00 | b;
                        }
                    } else if (buf[i] == 'm') {
                        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                        assert(chunk->type == TOGGLE_BIT);
                        switch (chunk->bitmask) {
                            case 0x01: chunk->bitmask = 0x04; break;
                            case 0x04: chunk->bitmask = 0x10; break;
                            case 0x10: chunk->bitmask = 0x40; break;
                            case 0x40: chunk->bitmask = 0x01; break;
                            default: UNREACHABLE();
                        }
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        assert(writeRooms(&state->rooms));
                    } else if (buf[i] == 'o') {
                        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                        assert(chunk->type == TOGGLE_BIT);
                        chunk->off = (chunk->off + 1) % 4;
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        assert(writeRooms(&state->rooms));
                    } else if (buf[i] == 'n') {
                        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                        assert(chunk->type == TOGGLE_BIT);
                        chunk->on = (chunk->on + 1) % 4;
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        assert(writeRooms(&state->rooms));
                    } else if (buf[i] == 0x7f) {
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
                                            state->current_state = EDIT_SWITCHDETAILS_CHUNK_BIT_DETAILS;
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
                    } else if (buf[i] == ESCAPE) {
                        if (i + 1 < n && buf[i+1] == '[' && i + 2 < n) {
                            // CSI sequence
                            i += 2;
                            uint8_t arg = 0;
                            while (i < n) {
                                if (isdigit(buf[i])) {
                                    arg = 10 * arg + buf[i] - '0';
                                } else if (buf[i] == ';') {
                                    arg = 0;
                                } else {
                                    switch (buf[i]) {
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
                                                                                           state->current_state = EDIT_SWITCHDETAILS_CHUNK_BIT_DETAILS;
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

                                                default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[i], arg);
                                            }
                                        }; break;

                                        default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[i], arg);
                                    }
                                    break;
                                }
                                i ++;
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
                    } else if (buf[i] == 't') {
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
                                        state->current_state = EDIT_SWITCHDETAILS_CHUNK_BIT_DETAILS;
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
                    } else if (iscntrl(buf[i])) {
                        switch (buf[i] + 'A' - 1) {
                            case '_': state->help = !state->help; break;
                            case 'H': state->debug.hex = !state->debug.hex; break;
                        }
                    } else if (buf[i] == 'q') {
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
                    } else if (buf[i] == '?') {
                        state->help = !state->help;
                    } else if (buf[i] == '+') {
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
                    } else if (buf[i] == '-') {
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
                    } else if (buf[i] == 'p') {
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
                    i ++;
                }; break;

                case EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS:
                {
                    if (isxdigit(buf[i])) {
                        uint8_t b = 0;
                        if (isdigit(buf[i])) {
                            b = *buf - '0';
                        } else {
                            b = 10 + tolower(buf[i]) - 'a';
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
                    } else if (buf[i] == 'h') {
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
                    } else if (buf[i] == 'j') {
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
                    } else if (buf[i] == 'k') {
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
                    } else if (buf[i] == 'l') {
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
                    } else if (buf[i] == 'o') {
                        state->switch_on = !state->switch_on;
                    } else if (buf[i] == ' ') {
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
                    } else if (buf[i] == '^') {
                        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                        assert(chunk->type == TOGGLE_BLOCK);
                        if (chunk->size < 8) chunk->size ++;
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        assert(writeRooms(&state->rooms));
                    } else if (buf[i] == 'V') {
                        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                        assert(chunk->type == TOGGLE_BLOCK);
                        if (chunk->size > 1) chunk->size --;
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        assert(writeRooms(&state->rooms));
                    } else if (buf[i] == 'r') {
                        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                        assert(chunk->type == TOGGLE_BLOCK);
                        chunk->dir = (chunk->dir + 1) % NUM_DIRECTIONS;
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        assert(writeRooms(&state->rooms));
                    } else if (buf[i] == 0x7f) {
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
                                            state->current_state = EDIT_SWITCHDETAILS_CHUNK_BIT_DETAILS;
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
                    } else if (buf[i] == ESCAPE) {
                        if (i + 1 < n && buf[i+1] == '[' && i + 2 < n) {
                            // CSI sequence
                            i += 2;
                            uint8_t arg = 0;
                            while (i < n) {
                                if (isdigit(buf[i])) {
                                    arg = 10 * arg + buf[i] - '0';
                                } else if (buf[i] == ';') {
                                    arg = 0;
                                } else {
                                    switch (buf[i]) {
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
                                                                                           state->current_state = EDIT_SWITCHDETAILS_CHUNK_BIT_DETAILS;
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

                                                default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[i], arg);
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

                                        default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[i], arg);
                                    }
                                    break;
                                }
                                i ++;
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
                    } else if (buf[i] == 't') {
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
                                        state->current_state = EDIT_SWITCHDETAILS_CHUNK_BIT_DETAILS;
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
                    } else if (iscntrl(buf[i])) {
                        switch (buf[i] + 'A' - 1) {
                            case '_': state->help = !state->help; break;
                            case 'H': state->debug.hex = !state->debug.hex; break;
                        }
                    } else if (buf[i] == 'q') {
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
                    } else if (buf[i] == '?') {
                        state->help = !state->help;
                    } else if (buf[i] == '+') {
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
                    } else if (buf[i] == '-') {
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
                    i ++;
                }; break;

                case EDIT_SWITCHDETAILS_CHUNK_BLOCK_DETAILS:
                {
                    if (isxdigit(buf[i])) {
                        uint8_t b = 0;
                        if (isdigit(buf[i])) {
                            b = *buf - '0';
                        } else {
                            b = 10 + tolower(buf[i]) - 'a';
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
                    } else if (buf[i] == 'o') {
                        state->switch_on = !state->switch_on;
                        state->partial_byte = 0;
                    } else if (buf[i] == ' ') {
                        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                        assert(chunk->type == TOGGLE_BLOCK);
                        chunk->dir = (chunk->dir + 1) % NUM_DIRECTIONS;
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        assert(writeRooms(&state->rooms));
                    } else if (buf[i] == '^') {
                        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                        assert(chunk->type == TOGGLE_BLOCK);
                        if (chunk->size < 8) chunk->size ++;
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        assert(writeRooms(&state->rooms));
                    } else if (buf[i] == 'v') {
                        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                        assert(chunk->type == TOGGLE_BLOCK);
                        if (chunk->size > 1) chunk->size --;
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        assert(writeRooms(&state->rooms));
                    } else if (buf[i] == 'h') {
                        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                        assert(chunk->type == TOGGLE_BLOCK);
                        if (chunk->x) chunk->x --;
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        assert(writeRooms(&state->rooms));
                    } else if (buf[i] == 'j') {
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
                    } else if (buf[i] == 'k') {
                        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                        assert(chunk->type == TOGGLE_BLOCK);
                        if (chunk->y) chunk->y --;
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        assert(writeRooms(&state->rooms));
                    } else if (buf[i] == 'l') {
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
                    } else if (buf[i] == 0x7f) {
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
                                            state->current_state = EDIT_SWITCHDETAILS_CHUNK_BIT_DETAILS;
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
                    } else if (buf[i] == ESCAPE) {
                        if (i + 1 < n && buf[i+1] == '[' && i + 2 < n) {
                            // CSI sequence
                            i += 2;
                            uint8_t arg = 0;
                            while (i < n) {
                                if (isdigit(buf[i])) {
                                    arg = 10 * arg + buf[i] - '0';
                                } else if (buf[i] == ';') {
                                    arg = 0;
                                } else {
                                    switch (buf[i]) {
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
                                                                                           state->current_state = EDIT_SWITCHDETAILS_CHUNK_BIT_DETAILS;
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

                                                default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[i], arg);
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

                                        default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[i], arg);
                                    }
                                    break;
                                }
                                i ++;
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
                    } else if (buf[i] == 't') {
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
                                        state->current_state = EDIT_SWITCHDETAILS_CHUNK_BIT_DETAILS;
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
                    } else if (iscntrl(buf[i])) {
                        switch (buf[i] + 'A' - 1) {
                            case '_': state->help = !state->help; break;
                            case 'H': state->debug.hex = !state->debug.hex; break;
                        }
                    } else if (buf[i] == 'q') {
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
                    } else if (buf[i] == '?') {
                        state->help = !state->help;
                    } else if (buf[i] == '+') {
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
                    } else if (buf[i] == '-') {
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
                    i ++;
                }; break;

                case EDIT_SWITCHDETAILS_CHUNK_OBJECT_DETAILS:
                {
                    if (isxdigit(buf[i])) {
                        uint8_t b = 0;
                        if (isdigit(buf[i])) {
                            b = *buf - '0';
                        } else {
                            b = 10 + tolower(buf[i]) - 'a';
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
                    } else if (buf[i] == 0x7f) {
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
                                            state->current_state = EDIT_SWITCHDETAILS_CHUNK_BIT_DETAILS;
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
                    } else if (buf[i] == ESCAPE) {
                        if (i + 1 < n && buf[i+1] == '[' && i + 2 < n) {
                            // CSI sequence
                            i += 2;
                            uint8_t arg = 0;
                            while (i < n) {
                                if (isdigit(buf[i])) {
                                    arg = 10 * arg + buf[i] - '0';
                                } else if (buf[i] == ';') {
                                    arg = 0;
                                } else {
                                    switch (buf[i]) {
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
                                                                                           state->current_state = EDIT_SWITCHDETAILS_CHUNK_BIT_DETAILS;
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

                                                default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[i], arg);
                                            }
                                        }; break;

                                        default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[i], arg);
                                    }
                                    break;
                                }
                                i ++;
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
                    } else if (buf[i] == 't') {
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
                                        state->current_state = EDIT_SWITCHDETAILS_CHUNK_BIT_DETAILS;
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
                    } else if (buf[i] == 'i') {
                        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                        chunk->index = (chunk->index + 1) % 0x10;
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        assert(writeRooms(&state->rooms));
                    } else if (buf[i] == 's') {
                        struct SwitchObject *sw = state->rooms.rooms[*cursorlevel].data.switches + state->current_switch - 1;
                        struct SwitchChunk *chunk = sw->chunks.data + state->current_chunk;
                        chunk->test = (((chunk->test >> 4) + 1) % 4) << 4;
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        assert(writeRooms(&state->rooms));
                    } else if (iscntrl(buf[i])) {
                        switch (buf[i] + 'A' - 1) {
                            case '_': state->help = !state->help; break;
                            case 'H': state->debug.hex = !state->debug.hex; break;
                        }
                    } else if (buf[i] == 'q') {
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
                    } else if (buf[i] == '?') {
                        state->help = !state->help;
                    } else if (buf[i] == '+') {
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
                    } else if (buf[i] == '-') {
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
                    } else if (buf[i] == 'p') {
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
                        i += 1;
                        continue;
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
                    } else if (buf[i] == '-') {
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
                    } else if (buf[i] == '+') {
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
                    } else if (buf[i] == 0x7f) {
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
                    } else if (iscntrl(buf[i])) {
                        switch (buf[i] + 'A' - 1) {
                            case 'R':
                                state->previous_state = state->current_state;
                                state->current_state = EDIT_ROOMDETAILS;
                                state->current_switch = 0;
                                state->switch_on = false;
                                state->current_chunk = 0;
                                i ++;
                                continue;

                            case 'S':
                            {
                                state->previous_state = state->current_state;
                                struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
                                size_t num_switches = room->num_switches;
                                if (state->current_switch) {
                                    state->current_state = EDIT_SWITCHDETAILS;
                                    i ++;
                                    continue;
                                }
                                size_t x = state->cursors[state->current_level].x;
                                size_t y = state->cursors[state->current_level].y;
                                size_t i = 0;
                                while (i < num_switches) {
                                    struct SwitchObject *sw = room->switches + i;
                                    if (sw->chunks.length && sw->chunks.data[0].x == x && sw->chunks.data[0].y == y) {
                                        break;
                                    }
                                    i ++;
                                }
                                if (i == room->num_switches) {
                                    room->switches = realloc(room->switches, (i + 1) * sizeof(struct SwitchObject));
                                    assert(room->switches != NULL);
                                    memset(room->switches + i, 0, sizeof(struct SwitchObject));
                                    room->num_switches ++;
                                    struct SwitchObject *sw = room->switches + i;
                                    ARRAY_ADD(sw->chunks, ((struct SwitchChunk){ .type = PREAMBLE, .x = x, .y = y }));
                                }
                                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                                assert(writeRooms(&state->rooms));
                                state->current_switch = i + 1;
                                state->current_state = EDIT_SWITCHDETAILS;
                                i ++;
                                continue;
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
                    if (iscntrl(buf[i]) != 0) {
                        switch (buf[i] + 'A' - 1) {
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
                                                uint8_t arg = 0;
                                                while (i < n) {
                                                    if (isdigit(buf[i])) {
                                                        arg = 10 * arg + buf[i] - '0';
                                                    } else if (buf[i] == ';') {
                                                        arg = 0;
                                                    } else {
                                                        if (buf[i] == '~') {
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

                                                                default: fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[i], arg);
                                                            }
                                                        } else {
                                                            fprintf(stderr, "%s:%d: UNIMPLEMENTED: csi terminator %c arg %d", __FILE__, __LINE__, buf[i], arg);
                                                        }
                                                    }
                                                    i ++;
                                                }
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
        {"Alt+S+dir", "TODO Shrink thing under cursor"},
        {"Left/h", "Move cursor left"},
        {"Down/j", "Move cursor down"},
        {"Up/k", "Move cursor up"},
        {"Right/l", "Move cursor right"},
        {"R", "edit room name"},
        {"r[nn]", "goto room"},
        {"Ctrl-r[ktdbcef|-]", "edit room detail"},
        {"Ctrl-s[eorcs]", "create/edit switch"},
        {"Ctrl-o", "TODO create/edit object"},
        {"s[n]", "goto switch"},
        {"o[n]", "goto object"},
        {"Delete/Backspace", "remove nibble; delete thing; or clear tile"},
        {"y", "TODO copy thing"},
        {"+", "increase id of thing under cursor"},
        {"-", "decrease id of thing under cursor"},
        {"p", "play (runs play.sh)"},
        {"q", "quit"},
        {"Ctrl-?", "toggle help"},
        {"Escape", "close/cancel"},
        {"Ctrl-h", "toggle hex in debug info"},
        {"Ctrl-t", "toggle tile edit mode"},
        {"Ctrl-d[adnopsu]", "toggle display element"},
        {0},
    },

    [GOTO_OBJECT]={
        {"0-9a-fA-F", "object id (highlighted on map)"},
        {"ESC", "go back to main view"},
        {"q", "go back to main view"},
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
        {0},
    },

    [GOTO_SWITCH]={
        {"0-9a-fA-F", "switch id (highlighted on map)"},
        {"ESC", "go back to main view"},
        {"q", "go back to main view"},
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
        {0},
    },

    [GOTO_ROOM]={
        {"0-9a-fA-F", "room number"},
        {"any printable char", "change character under cursor"},
        {"UP/Alt+j", "Goto room above current room"},
        {"DOWN/Alt+k", "Goto room below current room"},
        {"LEFT/Alt+h", "Goto room to the left current room"},
        {"RIGHT/Alt+l", "Goto room to the right current room"},
        {"DEL", "delete character under cursor"},
        {"BACKSPACE", "delete character under cursor"},
        {"ESC", "go back to main view"},
        {"Ctrl+q", "go back to main view"},
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
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
        {0},
    },

    [EDIT_ROOMDETAILS]={
        {"g", "Edit room background"},
        {"t", "Edit room tileset"},
        {"d", "Edit room damage"},
        {"|", "Edit room vertical gravity"},
        {"-", "Edit room horizontal gravity"},

        {"b", "Edit room UNKNOWN_b"},
        {"c", "Edit room UNKNOWN_c"},
        {"e", "Edit room UNKNOWN_e (only bottom two bits)"},
        {"f", "Edit room UNKNOWN_f"},

        {"LEFT", "Edit room to left"},
        {"RIGHT", "Edit room to right"},
        {"UP", "Edit room above"},
        {"DOWN", "Edit room below"},

        {"ESC", "go back to main view"},
        {"q", "go back to main view"},
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
        {0},
    },

    [EDIT_ROOMDETAILS_NUM]={
        {"0-9a-fA-F", "value"},
        {"ESC", "go back to main view"},
        {"q", "go back to main view"},
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
        {0},
    },

    [EDIT_ROOMDETAILS_ROOM]={
        {"0-9a-fA-F", "room number"},
        {"any printable char", "change character under cursor"},
        {"DEL", "delete character under cursor"},
        {"BACKSPACE", "delete character under cursor"},
        {"ESC", "go back to main view"},
        {"Ctrl+q", "go back to main view"},
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
        {0},
    },

    [EDIT_SWITCHDETAILS]={
        {"e", "flip the switch on entry bit"},
        {"o", "flip the switch one time only bit"},
        {"s", "rotate which side is the switch"},
        {"Left/h", "set switch side to left"},
        {"Down/j", "set switch side to bottom"},
        {"Up/k", "set switch side to top"},
        {"Right/l", "set switch side to right"},
        {"+", "increase switch id (shuffles up)"},
        {"-", "decrease switch id (shuffles down)"},
        {"y", "TODO copy switch"},
        {"c[n]", "select chunk"},
        {"C", "create new chunk"},
        {"ESC", "go back to main view"},
        {"DEL", "delete switch"},
        {"BACKSPACE", "delete switch"},
        {"q", "go back to main view"},
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
        {0},
    },

    [EDIT_SWITCHDETAILS_SELECT_CHUNK]={
        {"0-9a-fA-F", "chunk number"},
        {"ESC", "go back to main view"},
        {"q", "go back to main view"},
        {"+", "add new chunk"},
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
        {0},
    },

    [EDIT_SWITCHDETAILS_CHUNK_BIT_DETAILS]={
        {"t", "change chunk type"},
        {"DEL", "delete chunk"},
        {"BACKSPACE", "delete chunk"},
        {"y", "TODO copy chunk"},
        {"+", "increase chunk id (shuffles up)"},
        {"-", "decrease chunk id (shuffles down)"},
        {"0-9a-fA-F", "change index value"},
        {"n", "cycle possible on values"},
        {"o", "cycle possible off values"},
        {"m", "cycle possible mask values"},
        {"ESC", "go back to main view"},
        {"q", "go back to main view"},
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
        {0},
    },

    [EDIT_SWITCHDETAILS_CHUNK_BLOCK_DETAILS]={
        {"t", "change chunk type"},
        {"DEL", "remove partial entry or delete chunk"},
        {"BACKSPACE", "remove partial entry or delete chunk"},
        {"y", "TODO copy chunk"},
        {"+", "increase chunk id (shuffles up)"},
        {"-", "decrease chunk id (shuffles down)"},
        {"0-9a-fA-F", "change selected block tile"},
        {"Left/h", "Move block left"},
        {"Down/j", "Move block down"},
        {"Up/k", "Move block up"},
        {"Right/l", "Move block right"},
        {"^", "increase block size (width/height)"},
        {"v", "decrease block size (width/height)"},
        {"Space", "toggle vertical/horizontal"},
        {"o", "toggle between editing on and off value"},
        {"ESC", "go back to main view"},
        {"q", "go back to main view"},
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
        {0},
    },

    [EDIT_SWITCHDETAILS_CHUNK_MEMORY_DETAILS]={
        {"t", "change chunk type"},
        {"DEL", "remove partial entry or delete chunk"},
        {"BACKSPACE", "remove partial entry or delete chunk"},
        {"y", "TODO copy chunk"},
        {"+", "increase chunk id (shuffles up)"},
        {"-", "decrease chunk id (shuffles down)"},
        {"0-9a-fA-F", "change selected block tile"},
        {"Space", "cycle through memory fields"},
        {"o", "toggle between editing on and off value"},
        {"^", "increase block size (advanced)"},
        {"v", "decrease block size (advanced)"},
        {"r", "toggle direction (advanced)"},
        {"ESC", "go back to main view"},
        {"q", "go back to main view"},
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
        {0},
    },

    [EDIT_SWITCHDETAILS_CHUNK_OBJECT_DETAILS]={
        {"t", "change chunk type"},
        {"DEL", "remove partial entry or delete chunk"},
        {"BACKSPACE", "remove partial entry or delete chunk"},
        {"y", "TODO copy chunk"},
        {"+", "increase chunk id (shuffles up)"},
        {"-", "decrease chunk id (shuffles down)"},
        {"i", "cycle through possible index values"},
        {"s", "cycle through possible test values"},
        {" TODO dir keys ?", "change value"},
        {"0-9a-fA-F", "change raw value (advanced)"},
        {"ESC", "go back to main view"},
        {"q", "go back to main view"},
        {"Ctrl-?", "toggle help"},
        {"Ctrl-h", "toggle hex in debug info"},
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
#define PRINTF_DATA(num) printf(state->debug.hex ? "%02X" : "%d", (num))

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
    if (x < WIDTH_TILES && y < HEIGHT_TILES) {
        GOTO(2 * x, y + 1);
    }
    uint8_t tile = x < WIDTH_TILES && y < HEIGHT_TILES ? room.tiles[TILE_IDX(x, y)] : 0;
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
                        if (i + 1 == state->current_switch && c == state->current_chunk) {
                            tile = state->switch_on ? chunk->on : chunk->off;
                        } else {
                            tile = chunk->on;
                        }
                        ch = true;
                        chunk_switch_underneath = switcch;
                        chunk_underneath = chunk;
                        break;
                    }
                } else if (x == chunk->x && y >= chunk->y && y < chunk->y + chunk->size) {
                    if (i + 1 == state->current_switch && c == state->current_chunk) {
                        tile = state->switch_on ? chunk->on : chunk->off;
                    } else {
                        tile = chunk->on;
                    }
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
    if (state->current_state == GOTO_ROOM || state->current_state == EDIT_ROOMDETAILS_ROOM) {
        if (state->current_state == EDIT_ROOMDETAILS_ROOM) {
            switch (state->room_detail) {
                case 'A':
                case 'k':
                    printf("up: ");
                    break;

                case 'B':
                case 'j':
                    printf("down: ");
                    break;

                case 'C':
                case 'l':
                    printf("right: ");
                    break;

                case 'D':
                case 'h':
                    printf("left: ");
                    break;

                default: UNREACHABLE();
            }
        }
        if (state->partial_byte) {
            if (state->debug.hex) {
                printf("%x", state->partial_byte & 0xFF);
            } else {
                printf("%u", state->partial_byte & 0xFF);
            }
        } else {
            printf("\033[4;5m_\033[m");
        }
        switch (state->current_state) {
            case GOTO_ROOM:
                printf("\033[4;5m_\033[m - \"%s???\"", state->room_name);
                break;

            case EDIT_ROOMDETAILS_ROOM:
            {
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
                struct DecompresssedRoom *room = &state->rooms.rooms[state->current_level].data;
                struct DecompresssedRoom neighbour_room = {0};
                char neighbour_name[25] = {0};
                switch (state->room_detail) {
                    case 'A':
                    case 'k':
                        READ_NEIGHBOUR(room->room_north);
                        break;

                    case 'B':
                    case 'j':
                        READ_NEIGHBOUR(room->room_south);
                        break;

                    case 'C':
                    case 'l':
                        READ_NEIGHBOUR(room->room_east);
                        break;

                    case 'D':
                    case 'h':
                        READ_NEIGHBOUR(room->room_west);
                        break;

                    default: UNREACHABLE();
                }
                printf("\033[4;5m_\033[m - \"%s\"", neighbour_name);
#undef READ_NEIGHBOUR
            }; break;

            default: UNREACHABLE();
        }
        for (int y = 0; y < HEIGHT_TILES; y ++) {
            for (int x = 0; x < WIDTH_TILES; x ++) {
                printf("  ");
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
            bool highlight = state->roomname_cursor && strncasecmp(room_name, state->room_name, state->roomname_cursor) == 0;
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
                if (highlight) printf("\033[4;1m");
                printed += printf(state->debug.hex ? "%02lx" : "%02ld", room_id);
            }
            printed += printf(" - \"");
            if (underline) {
                printf("\033[4;1m");
            }
            if (highlight) {
                char tmp = room_name[state->roomname_cursor];
                room_name[state->roomname_cursor] = 0;
                printed += printf("%s", room_name);
                room_name[state->roomname_cursor] = tmp;
                printf("\033[m");
                printed += printf("%s", room_name + state->roomname_cursor);
            } else {
                printed += printf("%s", room_name);
            }
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
            highlight = state->roomname_cursor && strncasecmp(room_name, state->room_name, state->roomname_cursor) == 0;
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
                if (highlight) printf("\033[4;1m");
                printf(state->debug.hex ? "%02lx" : "%02ld", room_id);
            }
            printf(" - \"");
            if (underline) {
                printf("\033[4;1m");
            }
            if (highlight) {
                char tmp = room_name[state->roomname_cursor];
                room_name[state->roomname_cursor] = 0;
                printed += printf("%s", room_name);
                room_name[state->roomname_cursor] = tmp;
                printf("\033[m");
                printed += printf("%s", room_name + state->roomname_cursor);
            } else {
                printed += printf("%s", room_name);
            }
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
                if (state->debug.switches && state->current_state != GOTO_OBJECT)
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
                                    if ((size_t)s + 1 == state->current_switch && c == state->current_chunk) {
                                        tile = state->switch_on ? chunk->on : chunk->off;
                                    } else {
                                        tile = chunk->on;
                                    }
                                }
                            } else if (x == chunk->x && y >= chunk->y && y < chunk->y + chunk->size) {
                                printf("\033[3%d;40m", (s % 3) + 4);
                                colored = true;
                                if ((size_t)s + 1 == state->current_switch && c == state->current_chunk) {
                                    tile = state->switch_on ? chunk->on : chunk->off;
                                } else {
                                    tile = chunk->on;
                                }
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
    if (state->debug.objects) {
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
        if (state->current_state == GOTO_OBJECT) {
            for (int y = 0; y < HEIGHT_TILES; y ++) {
                for (int x = 0; x < WIDTH_TILES; x ++) {
                    struct RoomObject *found_object = NULL;
                    int o;
                    for (o = 0; o < room.num_objects; o ++) {
                        struct RoomObject *obj = room.objects + o;
                        if (x == obj->x && y == obj->y) {
                            found_object = obj;
                            break;
                        }
                    }
                    GOTO(2 * x, y + 1);
                    if (found_object) {
                        printf("\033[4%d;30;1m", (o % 3) + 1);

                        if (o > 15) {
                            // FIXME support 2 digit nums
                            UNREACHABLE();
                        }
                        printf(" %x", o);
                        printf("\033[m");
                    }
                }
            }
        }
    }

    if (state->current_state == TILE_EDIT || (state->current_state != NORMAL)) {
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
        if (state->current_state != EDIT_ROOMDETAILS_NUM && state->current_state != EDIT_SWITCHDETAILS_CHUNK_BLOCK_DETAILS && state->partial_byte) {
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
            printf("%X\033[m", state->partial_byte & 0xFF);
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
                printf("bk\033[1;4mg\033[mrnd: ");PRINTF_DATA(room.background);
                printf(", \033[1;4mt\033[miles: ");PRINTF_DATA(room.tile_offset);
                printf(", \033[1;4md\033[mmg: ");PRINTF_DATA(room.room_damage);
                printf(", gravity (\033[1;4m|\033[m): ");PRINTF_DATA(room.gravity_vertical);
                printf(", gravity (\033[1;4m-\033[m): ");PRINTF_DATA(room.gravity_horizontal);
                break;

            case EDIT_ROOMDETAILS_NUM:
                if (state->room_detail == 'g') {
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
                if (state->room_detail == 't') {
                    printf(", \033[1;4mt\033[miles: ");
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
                    printf(", tiles: ");PRINTF_DATA(room.tile_offset);
                }
                if (state->room_detail == 'd') {
                    printf(", \033[1;4md\033[mmg: ");
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
                    printf(", dmg: ");PRINTF_DATA(room.room_damage);
                }
                if (state->room_detail == '|') {
                    printf(", gravity (\033[1;4m|\033[m): ");
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
                    printf(", gravity (|): ");PRINTF_DATA(room.gravity_vertical);
                }
                if (state->room_detail == '-') {
                    printf(", gravity (\033[1;4m-\033[m): ");
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
                    printf(", gravity (-): ");PRINTF_DATA(room.gravity_horizontal);
                }
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
        switch (state->current_state) {
            case EDIT_ROOMDETAILS:
                printf("UNKNOWN_\033[1;4mb\033[m: ");PRINTF_DATA(room.UNKNOWN_b);
                printf(", UNKNOWN_\033[1;4mc\033[m: ");PRINTF_DATA(room.UNKNOWN_c);
                printf(", UNKNOWN_\033[1;4me\033[m: ");PRINTF_DATA(room._num_switches & 0x3);
                printf(", UNKNOWN_\033[1;4mf\033[m: ");PRINTF_DATA(room.UNKNOWN_f);
                break;

            case EDIT_ROOMDETAILS_NUM:
                if (state->room_detail == 'b') {
                    printf("UNKNOWN_\033[1;4mb\033[m: ");
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
                    printf("UNKNOWN_b: ");PRINTF_DATA(room.UNKNOWN_b);
                }

                if (state->room_detail == 'c') {
                    printf(", UNKNOWN_\033[1;4mc\033[m: ");
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
                    printf(", UNKNOWN_c: ");PRINTF_DATA(room.UNKNOWN_c);
                }

                if (state->room_detail == 'e') {
                    printf(", UNKNOWN_\033[1;4me\033[m: ");
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
                    printf(", UNKNOWN_e: ");PRINTF_DATA(room._num_switches & 0x3);
                }

                if (state->room_detail == 'f') {
                    printf(", UNKNOWN_\033[1;4mf\033[m: ");
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
                    printf(", UNKNOWN_f: ");PRINTF_DATA(room.UNKNOWN_f);
                }

                break;

            default:
                printf("UNKNOWN_b: ");PRINTF_DATA(room.UNKNOWN_b);
                printf(", UNKNOWN_c: ");PRINTF_DATA(room.UNKNOWN_c);
                printf(", UNKNOWN_e: ");PRINTF_DATA(room._num_switches & 0x3);
                printf(", UNKNOWN_f: ");PRINTF_DATA(room.UNKNOWN_f);
        }
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

        if (state->current_state == EDIT_ROOMDETAILS) {
            GOTO(0, bottom); bottom ++;
            printf("\033[1;4mLEFT\033[m: ");PRINTF_DATA(room.room_west);
            READ_NEIGHBOUR(room.room_west);
            printf(" - \"%s\"", neighbour_name);
            GOTO(0, bottom); bottom ++;
            printf("\033[1;4mDOWN\033[m: ");PRINTF_DATA(room.room_south);
            READ_NEIGHBOUR(room.room_south);
            printf(" - \"%s\"", neighbour_name);
            GOTO(0, bottom); bottom ++;
            printf("\033[1;4mUP\033[m: ");PRINTF_DATA(room.room_north);
            READ_NEIGHBOUR(room.room_north);
            printf(" - \"%s\"", neighbour_name);
            GOTO(0, bottom); bottom ++;
            printf("\033[1;4mRIGHT\033[m: ");PRINTF_DATA(room.room_east);
            READ_NEIGHBOUR(room.room_east);
            printf(" - \"%s\"", neighbour_name);
        } else {
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
    }

    if (state->current_state == EDIT_ROOMNAME) {
        GOTO(0, bottom); bottom +=2;
        printf("\nEnter new room name in space highlighted at top of screen");
        goto show_help_if_needed;
    }

    if (state->current_state == GOTO_SWITCH) {
        GOTO(0, bottom);
        printf("\nEnter switch id (in hex) or q to go to main view");
    }

    if (state->current_state == GOTO_OBJECT) {
        GOTO(0, bottom);
        printf("\nEnter object id (in hex) or q to go to main view");
    }

    if (state->current_state == TOGGLE_DISPLAY) {
        GOTO(0, bottom); bottom +=2;
        printf("\nEnter type to toggle, a for all, Ctrl-? for help");
        goto show_help_if_needed;
    }

    if (state->current_state == EDIT_ROOMDETAILS) {
        GOTO(0, bottom); bottom +=2;
        printf("\nEnter element to edit, Ctrl-? for help");
        goto show_help_if_needed;
    }

    if (state->debug.objects) {
        GOTO(0, bottom); bottom ++;
        if (object_underneath != NULL) {
            for (size_t i = object_underneath - room.objects + 1; i < room.num_objects; i ++) {
                if (room.objects[i].x == x && room.objects[i].y == y) {
                    fprintf(stderr, "%s:%d: %s: UNIMPLEMENTED: multiple objects underneath\n", __FILE__, __LINE__, __func__);
                    break;
                }
            }
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
        struct SwitchObject *switcz = switch_underneath;
        if (switcz) {
            for (size_t i = switcz - room.switches + 1; i < room.num_switches; i ++) {
                if (room.switches[i].chunks.data[0].x == x && room.switches[i].chunks.data[0].y == y) {
                    fprintf(stderr, "%s:%d: %s: UNIMPLEMENTED: multiple switches underneath\n", __FILE__, __LINE__, __func__);
                    break;
                }
            }
        }
        if (!switcz && chunk_underneath) switcz = chunk_switch_underneath;
        if (state->current_state != GOTO_SWITCH && switcz != NULL) {
#define BOOL_S(b) ((b) ? "true" : "false")
            assert(switcz->chunks.length >= 1);
            struct SwitchChunk *preamble = switcz->chunks.data;
            if (state->current_state == EDIT_SWITCHDETAILS) {
                printf("switch ");
                if (switcz == switch_underneath) printf("\033[4;1m");
                PRINTF_DATA((uint16_t)(switcz - room.switches));
                if (switcz == switch_underneath) printf("\033[m");
                printf(": (x,y)=%d,%d (\033[1;4me\033[mn\033[mtry)=%s (\033[1;4mo\033[mnce)=%s (\033[1;4ms\033[mide)=%s\n",
                        preamble->x, preamble->y,
                        BOOL_S(preamble->room_entry), BOOL_S(preamble->one_time_use),
                        SWITCH_SIDE(preamble->side));
            } else {
                printf("switch ");
                if (state->current_state == TILE_EDIT && switcz == switch_underneath) printf("\033[4;1m");
                PRINTF_DATA((uint16_t)(switcz - room.switches));
                if (state->current_state == TILE_EDIT && switcz == switch_underneath) printf("\033[m");
                printf(": (x,y)=%d,%d (entry)=%s (once)=%s (side)=%s\n",
                        preamble->x, preamble->y,
                        BOOL_S(preamble->room_entry), BOOL_S(preamble->one_time_use),
                        SWITCH_SIDE(preamble->side));
            }
            bottom ++;
            if (switcz->chunks.length > 1) {
                if (state->current_state == EDIT_SWITCHDETAILS) {
                    printf("  \033[1;4mc\033[mhunks:\n");
                } else {
                    printf("  chunks:\n");
                }
                bottom ++;
                for (size_t i = 1; i < switcz->chunks.length; i ++) {
                    printf("    ");
                    if ((state->current_state != EDIT_SWITCHDETAILS_SELECT_CHUNK && switcz == chunk_switch_underneath && (size_t)(chunk_underneath - switcz->chunks.data) == i) ||
                            (state->current_state == EDIT_SWITCHDETAILS_SELECT_CHUNK || state->current_chunk == i)) {
                        printf("\033[1;4m(");
                        PRINTF_DATA((uint16_t)i);
                        printf(")\033[m ");
                    } else {
                        printf("(");
                        PRINTF_DATA((uint16_t)i);
                        printf(") ");
                    }
                    if (state->current_chunk == i) {
                        printf("\033[1;4mt\033[mype ");
                    } else {
                        printf("type ");
                    }
                    struct SwitchChunk *chunk = switcz->chunks.data + i;
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
                                    default: printf(" name[%lu]", offset-14); break;
                                }
                                if (state->current_chunk == i) {
                                    printf(" (\033[4;1mo\033[mn/\033[4;1mo\033[mff)=");
                                    if (state->switch_on) {
                                        if (state->partial_byte) {
                                            if (state->debug.hex) {
                                                printf("%x", state->partial_byte & 0xFF);
                                            } else {
                                                printf("%u", state->partial_byte & 0xFF);
                                            }
                                        } else {
                                            printf("\033[4;5m%x\033[m", chunk->on / (state->debug.hex ? 16 : 10));
                                        }
                                        printf("\033[4;5m%x\033[m", chunk->on % (state->debug.hex ? 16 : 10));
                                    } else {
                                        PRINTF_DATA(chunk->on);
                                    }
                                    printf("/");
                                    if (state->switch_on) {
                                        PRINTF_DATA(chunk->off);
                                    } else {
                                        if (state->partial_byte) {
                                            if (state->debug.hex) {
                                                printf("%x", state->partial_byte & 0xFF);
                                            } else {
                                                printf("%u", state->partial_byte & 0xFF);
                                            }
                                        } else {
                                            printf("\033[4;5m%x\033[m", chunk->off / (state->debug.hex ? 16 : 10));
                                        }
                                        printf("\033[4;5m%x\033[m", chunk->off % (state->debug.hex ? 16 : 10));
                                    }
                                    printf(" (size)=%d (di\033[4;1mr\033[m)=%s", chunk->size, chunk->dir == HORIZONTAL ? "horiz" : "vert");
                                } else {
                                    printf(" (on/off)=");
                                    PRINTF_DATA(chunk->on);
                                    printf("/");
                                    PRINTF_DATA(chunk->off);
                                    printf(" (size)=%d (dir)=%s", chunk->size, chunk->dir == HORIZONTAL ? "horiz" : "vert");
                                }
                            } else {
                                printf("block: ");
                                if (state->current_chunk == i) {
                                    printf("\033[3%ld;40m", (i % 7));
                                }
                                printf("(x,y)=");
                                PRINTF_DATA(chunk->x);
                                printf(",");
                                PRINTF_DATA(chunk->y);
                                if (state->current_chunk == i) printf("\033[m");
                                if (state->current_chunk == i) {
                                    printf(" (%s)=%d (\033[4;1mo\033[mn/\033[4;1mo\033[mff)=", (chunk->dir == VERTICAL ? "height" : "width"), chunk->size);
                                    if (state->switch_on) {
                                        if (state->partial_byte) {
                                            if (state->debug.hex) {
                                                printf("%x", state->partial_byte & 0xFF);
                                            } else {
                                                printf("%u", state->partial_byte & 0xFF);
                                            }
                                        } else {
                                            printf("\033[4;5m%x\033[m", chunk->on / (state->debug.hex ? 16 : 10));
                                        }
                                        printf("\033[4;5m%x\033[m", chunk->on % (state->debug.hex ? 16 : 10));
                                    } else {
                                        PRINTF_DATA(chunk->on);
                                    }
                                    printf("/");
                                    if (state->switch_on) {
                                        PRINTF_DATA(chunk->off);
                                    } else {
                                        if (state->partial_byte) {
                                            if (state->debug.hex) {
                                                printf("%x", state->partial_byte & 0xFF);
                                            } else {
                                                printf("%u", state->partial_byte & 0xFF);
                                            }
                                        } else {
                                            printf("\033[4;5m%x\033[m", chunk->off / (state->debug.hex ? 16 : 10));
                                        }
                                        printf("\033[4;5m%x\033[m", chunk->off % (state->debug.hex ? 16 : 10));
                                    }
                                } else {
                                    printf(" (%s)=%d (on/off)=", (chunk->dir == VERTICAL ? "height" : "width"), chunk->size);
                                    PRINTF_DATA(chunk->on);
                                    printf("/");
                                    PRINTF_DATA(chunk->off);
                                }
                                if (state->current_chunk == i) {
                                    for (int offset = 0; offset < chunk->size; offset ++) {
                                        if (chunk->dir == HORIZONTAL) {
                                            GOTO(2 * (chunk->x + offset), chunk->y + 1);
                                        } else {
                                            GOTO(2 * chunk->x, chunk->y + offset + 1);
                                        }
                                        assert(state->current_switch);
                                        printf("\033[3%ld;40m", (i % 7));
                                        if (state->partial_byte) {
                                            if (state->debug.hex) {
                                                printf("%x", state->partial_byte & 0xFF);
                                            } else {
                                                printf("%u", state->partial_byte & 0xFF);
                                            }
                                        } else {
                                            printf("\033[4;1m%x\033[m", (state->switch_on ? chunk->on : chunk->off) / (state->debug.hex ? 16 : 10));
                                        }
                                        printf("\033[3%ld;40m", (i % 7));
                                        printf("\033[4;1m%x\033[m", (state->switch_on ? chunk->on : chunk->off) % (state->debug.hex ? 16 : 10));
                                    }
                                    GOTO(0, bottom - 1);
                                }
                            }
                        }; break;

                        case TOGGLE_BIT:
                        {
                            if (state->current_chunk == i) {
                                printf("bit: (idx)=");
                                if (state->partial_byte) {
                                    if (state->debug.hex) {
                                        printf("%x", state->partial_byte & 0xFF);
                                    } else {
                                        printf("%u", state->partial_byte & 0xFF);
                                    }
                                } else {

                                    printf("\033[4;5m%x\033[m", chunk->index / (state->debug.hex ? 16 : 10));
                                }
                                printf("\033[4;5m%x\033[m", chunk->index % (state->debug.hex ? 16 : 10));
                                printf(" (o\033[4;1mn\033[m/\033[4;1mo\033[mff)=%x/%x", chunk->on, chunk->off);
                                printf(" (\033[4;1mm\033[mask)=");
                                PRINTF_DATA(chunk->bitmask);
                            } else {
                                printf("bit: (idx)=");
                                PRINTF_DATA(chunk->index);
                                printf(" (on/off)=%d/%d (mask)=", chunk->on, chunk->off);
                                PRINTF_DATA(chunk->bitmask);
                            }
                        }; break;

                        case TOGGLE_OBJECT:
                        {
                            if (state->current_chunk == i) {
                                printf("object: (\033[4;1mi\033[mdx)=%d (te\033[4;1ms\033[mt)=%d", chunk->index, chunk->test >> 4);
                            } else {
                                printf("object: (idx)=%d (test)=%d", chunk->index, chunk->test >> 4);
                            }
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
                            printf(" (value_raw)=");
                            if (state->current_chunk == i) {
                                if (state->partial_byte) {
                                    if (state->debug.hex) {
                                        printf("%x", state->partial_byte & 0xFF);
                                    } else {
                                        printf("%u", state->partial_byte & 0xFF);
                                    }
                                } else {
                                    printf("\033[4;5m%x\033[m", chunk->value / (state->debug.hex ? 16 : 10));
                                }
                                printf("\033[4;5m%x\033[m", chunk->value % (state->debug.hex ? 16 : 10));
                            } else {
                                PRINTF_DATA(chunk->value);
                            }
                        }; break;

                        default: UNREACHABLE();
                    }
                    if (i < switcz->chunks.length - 1) {
                        printf("\n");
                        bottom ++;
                    }
                }
            }
        } else if (state->current_state == TILE_EDIT) {
            /* printf("No switch here, Ctrl-s to a create new one"); */
        }

        uint8_t found = 0;
        for (size_t s = 0; s < room.num_switches; s ++) {
            struct SwitchObject *sw = room.switches + s;
            int x = sw->chunks.data[0].x;
            int y = sw->chunks.data[0].y;
            if (x >= WIDTH_TILES || y >= HEIGHT_TILES) {
                found = 1;
                break;
            }
        }
        if (found) {
            printf("\n\nOut of bounds switches:");
            bottom +=2;
        }
        for (size_t s = 0; s < room.num_switches; s ++) {
            struct SwitchObject *sw = room.switches + s;
            int x = sw->chunks.data[0].x;
            int y = sw->chunks.data[0].y;
            if (x >= WIDTH_TILES || y >= HEIGHT_TILES) {
                GOTO(0, bottom); bottom ++;
#define BOOL_S(b) ((b) ? "true" : "false")
                if (state->current_state == EDIT_SWITCHDETAILS && state->current_switch == s) {
                    printf("\033[1;4mswitch id ");
                    PRINTF_DATA((uint16_t)s);
                    printf("\033[m: (x,y)=");
                    PRINTF_DATA(sw->chunks.data[0].x);
                    printf(",");
                    PRINTF_DATA(sw->chunks.data[0].y);
                    printf(" (\033[1;4me\033[mntry)=%s (\033[1;4mo\033[mnce)=%s (\033[1;4ms\033[mide)=%s\n",
                            BOOL_S(sw->chunks.data[0].room_entry), BOOL_S(sw->chunks.data[0].one_time_use),
                            SWITCH_SIDE(sw->chunks.data[0].side));

                } else {
                    printf("switch id ");
                    if (state->current_state == GOTO_SWITCH) {
                        printf("\033[4%ld;30;1m", (s % 4) + 4);
                        PRINTF_DATA((uint16_t)s);
                        printf("\033[m");
                    } else {
                        PRINTF_DATA((uint16_t)s);
                    }
                    printf(": (x,y)=");
                    PRINTF_DATA(sw->chunks.data[0].x);
                    printf(",");
                    PRINTF_DATA(sw->chunks.data[0].y);
                    printf(" (entry)=%s (once)=%s (side)=%s\n",
                            BOOL_S(sw->chunks.data[0].room_entry), BOOL_S(sw->chunks.data[0].one_time_use),
                            SWITCH_SIDE(sw->chunks.data[0].side));
                }

                bottom ++;
                if (sw->chunks.length > 1) {
                    if (state->current_state == EDIT_SWITCHDETAILS) {
                        printf("  \033[1;4mc\033[mhunks:\n");
                    } else {
                        printf("  chunks:\n");
                    }
                    bottom ++;
                    for (size_t i = 1; i < sw->chunks.length; i ++) {
                        if (state->current_chunk == i) {
                            fprintf(stderr, "%s:%d: %s: UNIMPLEMENTED: selected chunk\n", __FILE__, __LINE__, __func__);
                            UNREACHABLE();
                        }
                        printf("    ");
                        if (state->current_state == EDIT_SWITCHDETAILS_SELECT_CHUNK) {
                            printf("\033[1;4m(");
                            PRINTF_DATA((uint16_t)i);
                            printf(")\033[m ");
                        } else {
                            printf("(");
                            PRINTF_DATA((uint16_t)i);
                            printf(") ");
                        }
                        struct SwitchChunk *chunk = sw->chunks.data + i;
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
                                        default: printf(" name[%lu]", offset-14); break;
                                    }
                                    printf(" (on/off)=%02x/%02x (size)=%d", chunk->on, chunk->off, chunk->size);
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
                                printf("object - (idx)=%d (test)=", chunk->index >> 4);
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
                                printf(" (value_raw)=");
                                PRINTF_DATA(chunk->value);
                            }; break;

                            default: UNREACHABLE();
                        }
                        if (i < sw->chunks.length - 1) {
                            printf("\n");
                            bottom ++;
                        }
                    }
                }
#undef BOOL_S
            }
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

    struct timespec test_time = library_stat.st_mtim;
    while (true) {
        struct stat rooms_stat;
        if (any_source_newer(test_time)) {
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
            fprintf(stderr, "--- Recompile failed not reloading (%d)---\n", ret);
            free(build_cmd);
            if (clock_gettime(CLOCK_REALTIME, &test_time) == -1) {
                perror("clock_gettime");
            }
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
