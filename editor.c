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

#define MIN_HEIGHT HEIGHT_TILES
#define MIN_WIDTH 2 * WIDTH_TILES

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

typedef struct {
    size_t size;
    v2 player;
    v2 screen_dimensions;
    struct termios original_termios;
    RoomFile rooms;
    bool resized;
    size_t playerlevel;
    bool help;
    struct debug debug;
    bool tileedit;
    size_t editlevel;
    v2 tileeditpos;
    uint16_t editbyte;
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
    int x = state->tileeditpos.x;
    int y = state->tileeditpos.y;
    if (x + dx < 0 || x + dx >= WIDTH_TILES || y + dy < 0 || y + dy >= HEIGHT_TILES) return;
    struct DecompresssedRoom *room = &state->rooms.rooms[state->editlevel].data;
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
            state->tileeditpos.x += dx;
            state->tileeditpos.y += dy;
            ARRAY_FREE(state->rooms.rooms[state->editlevel].compressed);
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
            state->tileeditpos.x += dx;
            state->tileeditpos.y += dy;
            ARRAY_FREE(state->rooms.rooms[state->editlevel].compressed);
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
                    state->tileeditpos.x += dx;
                    state->tileeditpos.y += dy;
                    ARRAY_FREE(state->rooms.rooms[state->editlevel].compressed);
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
        state->tileeditpos.x += dx;
        state->tileeditpos.y += dy;
        ARRAY_FREE(state->rooms.rooms[state->editlevel].compressed);
        assert(writeRooms(&state->rooms));
    }
}

void stretch(int dx, int dy) {
    int x = state->tileeditpos.x;
    int y = state->tileeditpos.y;
    struct DecompresssedRoom *room = &state->rooms.rooms[state->editlevel].data;
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
            state->tileeditpos.x += dx;
            state->tileeditpos.y += dy;
            ARRAY_FREE(state->rooms.rooms[state->editlevel].compressed);
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
                state->tileeditpos.x += dx;
                state->tileeditpos.y += dy;
                ARRAY_FREE(state->rooms.rooms[state->editlevel].compressed);
                assert(writeRooms(&state->rooms));
                stretched = true;
                break;
            }
        }
    }
    if (!stretched && x + dx >= 0 && x + dx < WIDTH_TILES && y + dy >= 0 && y + dy < HEIGHT_TILES) {
        room->tiles[TILE_IDX(x + dx, y + dy)] = room->tiles[TILE_IDX(x, y)];
        state->tileeditpos.x += dx;
        state->tileeditpos.y += dy;
        ARRAY_FREE(state->rooms.rooms[state->editlevel].compressed);
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
            char *match = NULL;
#define KEY_MATCHES(key) ((strncmp(buf + i, (key), strlen((key))) == 0) && (match = key))
            v2 *cursor = NULL;
            size_t *cursorlevel = NULL;
            if (state->tileedit) {
                cursor = &state->tileeditpos;
                cursorlevel = &state->editlevel;
            } else {
                cursor = &state->player;
                cursorlevel = &state->playerlevel;
            }

            if (state->tileedit && isxdigit(buf[i])) {
                uint8_t b = 0;
                if (isdigit(buf[i])) {
                    b = *buf - '0';
                } else {
                    b = 10 + tolower(buf[i]) - 'a';
                }
                if ((state->editbyte & 0xFF00) != 0) {
                    b = (state->editbyte & 0xFF) | b;
                    bool obj = false;
                    bool ch = false;
                    int x = state->tileeditpos.x;
                    int y = state->tileeditpos.y;
                    struct DecompresssedRoom *room = &state->rooms.rooms[state->editlevel].data;
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
                    ARRAY_FREE(state->rooms.rooms[state->editlevel].compressed);
                    assert(writeRooms(&state->rooms));
                    state->editbyte = 0;
                } else {
                    state->editbyte = 0xFF00 | (b << 4);
                }
                i += 1;
                continue;
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
                state->editbyte = 0;
                if (cursor->x < 0) {
                    *cursorlevel = state->rooms.rooms[*cursorlevel].data.room_west;
                    cursor->x = WIDTH_TILES - 1;
                }
            } else if (KEY_MATCHES(KEY_DOWN) || KEY_MATCHES("j")) {
                cursor->y ++;
                state->editbyte = 0;
                if (cursor->y >= HEIGHT_TILES) {
                    *cursorlevel = state->rooms.rooms[*cursorlevel].data.room_south;
                    cursor->y = 0;
                }
            } else if (KEY_MATCHES(KEY_UP) || KEY_MATCHES("k")) {
                cursor->y --;
                state->editbyte = 0;
                if (cursor->y < 0) {
                    *cursorlevel = state->rooms.rooms[*cursorlevel].data.room_north;
                    cursor->y = HEIGHT_TILES - 1;
                }
            } else if (KEY_MATCHES(KEY_RIGHT) || KEY_MATCHES("l")) {
                cursor->x ++;
                state->editbyte = 0;
                if (cursor->x >= WIDTH_TILES) {
                    *cursorlevel = state->rooms.rooms[*cursorlevel].data.room_east;
                    cursor->x = 0;
                }
            }
            if (state->tileedit) {
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
            }
            if (iscntrl(buf[i]) != 0) {
                switch (buf[i] + 'A' - 1) {
                    case 'A': debugalltoggle(); break;
                    case 'D': state->debug.data = !state->debug.data; break;
                    case 'H': state->debug.hex = !state->debug.hex; break;
                    case 'N': state->debug.neighbours = !state->debug.neighbours; break;
                    case 'O': state->debug.objects = !state->debug.objects; break;
                    case 'P': state->debug.pos = !state->debug.pos; break;
                    case 'S': state->debug.switches = !state->debug.switches; break;
                    case 'T': {
                        if (state->tileedit) {
                            state->playerlevel = state->editlevel;
                            state->player = state->tileeditpos;
                        } else {
                            state->editlevel = state->playerlevel;
                            state->tileeditpos = state->player;
                        }
                        state->tileedit = !state->tileedit;
                        state->editbyte = 0;
                    }; break;
                    case 'U': state->debug.unknowns = !state->debug.unknowns; break;
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
                                if ((state->editbyte & 0xFF00) != 0) {
                                    state->editbyte = 0;
                                }
                            }
                            i += 1; // Single escape
                        }
                        break;

                    default:
                        i += 1;
                }
            }
#undef KEY_MATCHES
        }
    }
}

void update() {

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
    if (state->debug.switches) offset_y ++;
    assert(MIN_WIDTH + offset_x >= 2 * WIDTH_TILES);
    assert(MIN_HEIGHT + offset_y >= HEIGHT_TILES);
    if (state->screen_dimensions.x <= MIN_WIDTH + offset_x || state->screen_dimensions.y <= MIN_HEIGHT + offset_y) {
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

    size_t level = state->tileedit ? state->editlevel : state->playerlevel;
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
    GOTO(MIN_WIDTH / 2 - ((room_name_len + 7) / 2), 0);
    PRINTF_DATA((int)level);
    printf(" - \"%s\"", room_name);

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
                GOTO(2 * object->x, object->y + 1);
                printf("\033[4%ld;30m%d%d", (i % 3) + 1, object->sprite.type, object->sprite.damage);
                break;
        }
        printf("\033[m");
    }

    if (state->tileedit) {
        int x = state->tileeditpos.x;
        int y = state->tileeditpos.y;
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
                printf("\033[30;4%ld;1m", (i % 8) + 1);
                break;
            } else if (object->type == SPRITE && x == object->x && y == object->y) {
                obj = true;
                sprite = true;
                tile = (object->sprite.type << 4) | object->sprite.damage;
                printf("\033[3%ld;40;1m", (i % 3) + 1);
                break;
            }
        }
        if (!obj) {
            for (i = 0; i < room.num_switches; i ++) {
                struct SwitchObject *switcch = room.switches + i;
                if (switcch->chunks.length > 0 && switcch->chunks.data[0].type == PREAMBLE &&
                        x == switcch->chunks.data[0].x && y == switcch->chunks.data[0].y) {
                    printf("\033[3%ld;1m", (i % 3) + 4);
                    sw = true;
                    break;
                }
                size_t c;
                for (c = 1; c < switcch->chunks.length; c ++) {
                    struct SwitchChunk *chunk = switcch->chunks.data + c;
                    if (chunk->type == TOGGLE_BLOCK) {
                        if (chunk->dir == HORIZONTAL) {
                            if (y == chunk->y && x >= chunk->x && x < chunk->x + chunk->size) {
                                printf("\033[4%d;30;1m", (i % 3) + 4);
                                tile = chunk->on;
                                ch = true;
                                break;
                            }
                        } else if (x == chunk->x && y >= chunk->y && y < chunk->y + chunk->size) {
                            printf("\033[4%d;30;1m", (i % 3) + 4);
                            tile = chunk->on;
                            ch = true;
                            break;
                        }
                    }
                }
                if (ch) break;
            }
        }
        if (obj || sw || ch) {
            printf("%02X\033[m", tile);
        } else {
            printf("\033[47;30;1m%02X\033[m", tile);
        }
        if (state->editbyte) {
            GOTO(2 * x, y + 1);
            if (obj) {
                if (sprite) {
                    printf("\033[30;4%ld;1m", (i % 3) + 1);
                } else {
                    printf("\033[3%ld;1m", (i % 8) + 1);
                }
            } else if (sw) {
                printf("\033[30;4%ld;1m", (i % 3) + 4);
            } else if (ch) {
                printf("\033[40;3%ld;1m", (i % 3) + 4);
            } else {
                printf("\033[1m");
            }
            printf("%X\033[m", (state->editbyte & 0xFF) >> 4);
        }
    } else {
        assert(state->player.x >= 0);
        assert(state->player.y >= 0);
        assert(state->player.x < WIDTH_TILES);
        assert(state->player.y < HEIGHT_TILES);
        if (state->player.y - 2 >= 0) {
            GOTO(2 * state->player.x, state->player.y - 1);
            printf("\033[41;30;1m@@\033[m");
        }
        if (state->player.y - 1 >= 0) {
            GOTO(2 * state->player.x, state->player.y);
            printf("\033[41;30;1m@@\033[m");
        }
        if (state->player.y >= 0) {
            GOTO(2 * state->player.x, state->player.y + 1);
            printf("\033[41;30;1m@@\033[m");
        }
    }

    if (state->debug.pos) {
        GOTO(0, 0);
        v2 *thing = NULL;
        if (state->tileedit) thing = &state->tileeditpos;
        else thing = &state->player;
        PRINTF_DATA(thing->x);
        printf(",");
        PRINTF_DATA(thing->y);
    }

    int bottom = HEIGHT_TILES + 1;
    if (debugany()) bottom ++;
    if (state->debug.data) {
        GOTO(0, bottom); bottom ++;
        printf("back: ");PRINTF_DATA(room.background);
        printf(", tileset: ");PRINTF_DATA(room.tile_offset);
        printf(", dmg: ");PRINTF_DATA(room.room_damage);
        printf(", gravity (|): ");PRINTF_DATA(room.gravity_vertical);
        printf(", gravity (-): ");PRINTF_DATA(room.gravity_horizontal);
    }
    if (state->debug.unknowns) {
        GOTO(0, bottom); bottom ++;
        printf("UNKNOWN_b: ");PRINTF_DATA(room.UNKNOWN_b);
        printf(", UNKNOWN_c: ");PRINTF_DATA(room.UNKNOWN_c);
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
    if (state->debug.objects) {
        GOTO(0, bottom); bottom ++;
        printf("UNIMPLEMENTED: debugobjects");
    }
    if (state->debug.switches) {
        GOTO(0, bottom); bottom ++;
        printf("UNIMPLEMENTED: debugswitches");
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

    if (state->help) {
        // 35x10
        int y = 19;
        if (state->tileedit) y +=3;
        int x = 40;
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2);
        printf("\033[47;30;1m");
        printf("+-----------------help-----------------+\033[m ");
        for (int _y = 1; _y < y - 1; _y ++) {
            GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + _y);
            printf("\033[47;30;1;m|%*s|\033[40;37;1m ", x - 2, "");
        }
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + (y - 2));
        printf("\033[47;30;1m");
        printf("+--------------------------------------+\033[40;37;1m ");
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + (y - 1));
        printf("\033[m \033[40;37;1m%*s", x, "");
        printf("\033[47;30;1m");

        int line = 1;
        if (state->tileedit) {
            GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
            printf("|%-*s|", x - 2, "0-9a-fA-F - Enter hex nibble");
            GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
            printf("|%-*s|", x - 2, "Shift+dir - Move thing under cursor");
            GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
            printf("|%-*s|", x - 2, "Alt+dir   - Stretch thing under cursor");
        }
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
        printf("|%-*s|", x - 2, "Left/h    - Move cursor left");
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
        printf("|%-*s|", x - 2, "Down/j    - Move cursor down");
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
        printf("|%-*s|", x - 2, "Up/k      - Move cursor up");
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
        printf("|%-*s|", x - 2, "Right/l   - Move cursor right");
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
        printf("|%-*s|", x - 2, "q         - quit");
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
        printf("|%-*s|", x - 2, "?         - toggle help");
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
        printf("|%-*s|", x - 2, "Escape    - close/cancel");
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
        printf("|%-*s|", x - 2, "Ctrl-h    - toggle hex in debug info");
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
        printf("|%-*s|", x - 2, "Ctrl-p    - toggle position display");
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
        printf("|%-*s|", x - 2, "Ctrl-d    - toggle room data display");
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
        printf("|%-*s|", x - 2, "Ctrl-u    - toggle unknown display");
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
        printf("|%-*s|", x - 2, "Ctrl-n    - toggle neighbour display");
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
        printf("|%-*s|", x - 2, "Ctrl-o    - toggle room object display");
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
        printf("|%-*s|", x - 2, "Ctrl-s    - toggle room switch display");
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
        printf("|%-*s|", x - 2, "Ctrl-t    - toggle tile edit mode");
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
        printf("|%-*s|", x - 2, "Ctrl-a    - toggle all debug info");
    }

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
    state->tileedit = true;
    state->editbyte = 0;

    assert(tcgetattr(STDIN_FILENO, &state->original_termios) == 0);

    struct termios new = state->original_termios;
    new.c_lflag &= ~(ICANON | ECHO);
    new.c_iflag &= ~(IXON | IXOFF);
    assert(tcsetattr(STDIN_FILENO, TCSANOW, &new) == 0);

    get_screen_dimensions();

    state->player.x = 3;
    state->player.y = HEIGHT_TILES - 2;

    assert(readRooms(&state->rooms) && "Check that you have ROOMS.SPL");

    state->playerlevel = 1;
    state->editlevel = 1;

    state->tileeditpos = state->player;
    state->help = true;
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
            assert(asprintf(&build_cmd, "%s %s %s 2>/dev/null", LIBRARY_BUILD_CMD, library, __FILE__) > 0);
            if (system(build_cmd) == 0) {
                free(build_cmd);
                return state;
            }
            free(build_cmd);
        }
        if (stat("ROOMS.SPL", &rooms_stat) == 0) {
            if (TIME_NEWER(rooms_stat.st_mtim, start_rooms_stat.st_mtim)) {
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
            printf("%s\n", build_cmd);
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
