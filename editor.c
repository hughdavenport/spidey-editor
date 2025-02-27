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

#define LIBRARY_BUILD_CMD "cc -ggdb -Werror -Wall -Wpedantic -fsanitize=address -fpic -shared room.c -o"

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
    printf("\x1b[%d;%dH", (_y) + 1, (_x) + 1); \
} while (false)

#define UP    "\x1b[A"
#define DOWN  "\x1b[B"
#define RIGHT "\x1b[C"
#define LEFT  "\x1b[D"

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
#define CTRL_A "\001"
#define CTRL_D "\004"
#define CTRL_O "\017"
#define CTRL_S "\023"
#define CTRL_T "\024"
#define CTRL_H "\010"

#define MIN_HEIGHT HEIGHT_TILES + 5
#define MIN_WIDTH 2 * WIDTH_TILES

#include "room.h"

typedef struct {
    int x;
    int y;
} v2;

typedef struct {
    size_t size;
    v2 player;
    v2 screen_dimensions;
    struct termios original_termios;
    RoomFile rooms;
    bool resized;
    size_t playerlevel;
    bool help;
    bool debug;
    bool debughex;
    bool debugdata;
    bool debugobjects;
    bool debugswitches;
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
            v2 *thing = NULL;
            size_t *thinglevel = NULL;
            if (state->tileedit) {
                thing = &state->tileeditpos;
                thinglevel = &state->editlevel;
            } else {
                thing = &state->player;
                thinglevel = &state->playerlevel;
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
                        }
                    }
                    if (!obj) room->tiles[y * WIDTH_TILES + x] = b;
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
            } else if (state->editbyte == 0 || !state->tileedit) {
                if (KEY_MATCHES("a") || KEY_MATCHES(LEFT) || KEY_MATCHES("h")) {
                    thing->x --;
                    if (thing->x < 0) {
                        *thinglevel = state->rooms.rooms[*thinglevel].data.room_west;
                        thing->x = WIDTH_TILES - 1;
                    }
                } else if (KEY_MATCHES("d") || KEY_MATCHES(RIGHT) || KEY_MATCHES("l")) {
                    thing->x ++;
                    if (thing->x >= WIDTH_TILES) {
                        *thinglevel = state->rooms.rooms[*thinglevel].data.room_east;
                        thing->x = 0;
                    }
                } else if (KEY_MATCHES("w") || KEY_MATCHES(UP) || KEY_MATCHES("k")) {
                    thing->y --;
                    if (thing->y < 0) {
                        *thinglevel = state->rooms.rooms[*thinglevel].data.room_north;
                        thing->y = HEIGHT_TILES - 1;
                    }
                } else if (KEY_MATCHES("s") || KEY_MATCHES(DOWN) || KEY_MATCHES("j")) {
                    thing->y ++;
                    if (thing->y >= HEIGHT_TILES) {
                        *thinglevel = state->rooms.rooms[*thinglevel].data.room_south;
                        thing->y = 0;
                    }
                } else if (KEY_MATCHES(CTRL_T)) {
                    state->tileedit = !state->tileedit;
                }
            }
            if (KEY_MATCHES(CTRL_H)) {
                state->debughex = !state->debughex;
                state->debug = true;
            } else if (KEY_MATCHES(CTRL_A)) {
                state->debugdata = !state->debugdata;
                state->debugobjects = !state->debugobjects;
                state->debugswitches = !state->debugswitches;
            } else if (KEY_MATCHES(CTRL_D)) {
                state->debugdata = !state->debugdata;
                state->debug = true;
            } else if (KEY_MATCHES(CTRL_O)) {
                state->debugobjects = !state->debugobjects;
                state->debug = true;
            } else if (KEY_MATCHES(CTRL_S)) {
                state->debugswitches = !state->debugswitches;
                state->debug = true;
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
                                } else {
                                    state->debug = !state->debug;
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

void dumpRoom(struct DecompresssedRoom *room);
void redraw() {
    GOTO(0, 0);
    printf(RESET_GFX_MODE CLEAR_SCREEN);

    assert(MIN_WIDTH >= 2 * WIDTH_TILES);
    assert(MIN_HEIGHT >= HEIGHT_TILES + 2);
    if (state->screen_dimensions.x < MIN_WIDTH || state->screen_dimensions.y < MIN_HEIGHT) {
        char *message = NULL;
        assert(asprintf(&message,
                    "Required screen dimension is %dx%d. Currently %dx%d",
                    MIN_WIDTH, MIN_HEIGHT,
                    state->screen_dimensions.x, state->screen_dimensions.y
        ) >= 0);
        int x = state->screen_dimensions.x / 2 - (int)strlen(message) / 2;
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
    GOTO(16, 0);
    /* char name[24]; */
    /* printf("%24s", room.name); */
    for (size_t i = 0; i < C_ARRAY_LEN(room.name); i ++) {
        printf("%c", room.name[i]);
    }

    for (int y = 0; y < HEIGHT_TILES; y ++) {
        for (int x = 0; x < WIDTH_TILES; x ++) {
            uint8_t tile = room.tiles[y * WIDTH_TILES + x];
            if (tile != BLANK_TILE) {
                bool colored = false;
                if (state->debugswitches)
                for (int s = 0; s < room.num_switches; s ++) {
                    struct SwitchObject *sw = room.switches + s;
                    assert(sw->chunks.length > 0 && sw->chunks.data[0].type == PREAMBLE);
                    if (x == sw->chunks.data[0].x && y == sw->chunks.data[0].y) {
                        colored = true;
                        printf("\033[4%d;30;1m", (s % 3) + 4);
                        break;
                    }
                }
                GOTO(2 * x, y + 1);
                printf("%02X", tile);
                if (colored) printf("\033[m");
            }
        }
    }

    if (state->debugobjects)
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
                printf("\033[3%ld;1m", (i % 8) + 1);
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
                printf("\033[4%ld;30;1m%d%d", (i % 3) + 1, object->sprite.type, object->sprite.damage);
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
        uint8_t tile = room.tiles[y * WIDTH_TILES + x];
        bool obj = false;
        size_t i;
        for (i = 0; i < room.num_objects; i ++) {
            struct RoomObject *object = room.objects + i;
            if (object->type == BLOCK &&
                    x >= object->x && x < object->x + object->block.width &&
                    y >= object->y && y < object->y + object->block.height) {
                obj = true;
                tile = object->tiles[(y - object->y) * object->block.width + (x - object->x)];
                printf("\033[30;4%ldm", (i % 8) + 1);
                break;
            }
        }
        if (obj) {
            printf("%02X\033[m", tile);
        } else {
            printf("\033[47;30m%02X\033[m", tile);
        }
        if (state->editbyte) {
            GOTO(2 * x, y + 1);
            if (obj) {
                printf("\033[3%ld;1m", (i % 8) + 1);
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

    if (state->debug) {
        GOTO(0, 0);
        v2 *thing = NULL;
        if (state->tileedit) thing = &state->tileeditpos;
        else thing = &state->player;
        printf(state->debughex ? "%02x,%02x" : "%d,%d", thing->x, thing->y);

        int bottom = HEIGHT_TILES + 1;
        if (state->debugdata) {
            GOTO(0, bottom); bottom ++;
            printf("UNIMPLEMENTED: debugdata");
        }
        if (state->debugobjects) {
            GOTO(0, bottom); bottom ++;
            printf("UNIMPLEMENTED: debugobjects");
        }
        if (state->debugswitches) {
            GOTO(0, bottom); bottom ++;
            printf("UNIMPLEMENTED: debugswitches");
        }
        GOTO(0, bottom); bottom ++;
        uint8_array rest = state->rooms.rooms[level].rest;
        int pre = printf("Rest (length=%zu):", rest.length);
        for (size_t i = 0; i < rest.length; i ++) {
            if (pre + 3 * (i + 2) > (size_t)state->screen_dimensions.x) {
                printf("...");
                break;
            }
            printf(state->debughex ? " %02x" : " %d", rest.data[i]);
        }
    }

    if (state->help) {
        // 35x10
        int y = 16;
        if (state->tileedit) y ++;
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
        }
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
        printf("|%-*s|", x - 2, "w/Up/k    - Move cursor up");
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
        printf("|%-*s|", x - 2, "a/Left/h  - Move cursor left");
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
        printf("|%-*s|", x - 2, "s/Down/j  - Move cursor down");
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
        printf("|%-*s|", x - 2, "d/Right/l - Move cursor right");
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
        printf("|%-*s|", x - 2, "q         - quit");
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
        printf("|%-*s|", x - 2, "?         - toggle help");
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
        if (state->tileedit && (state->editbyte & 0xFF00) != 0) {
            printf("|%-*s|", x - 2, "Escape    - cancel edit entry");
        } else {
            printf("|%-*s|", x - 2, "Escape    - toggle debug info");
        }
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
        printf("|%-*s|", x - 2, "Ctrl-h    - toggle hex in debug info");
        GOTO(state->screen_dimensions.x / 2 - x / 2, state->screen_dimensions.y / 2 - y / 2 + line); line ++;
        printf("|%-*s|", x - 2, "Ctrl-d    - toggle room data display");
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

    state->debug = true;
    state->debughex = true;

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
        struct stat file_stat;
        struct stat rooms_stat;
        if (stat(__FILE__, &file_stat) == 0) {
            if (TIME_NEWER(file_stat.st_mtim, library_stat.st_mtim)) {
                // rebuild
                char *build_cmd = NULL;
                assert(asprintf(&build_cmd, "%s %s %s 2>/dev/null", LIBRARY_BUILD_CMD, library, __FILE__) > 0);
                if (system(build_cmd) == 0) {
                    free(build_cmd);
                    return state;
                }
            }
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
