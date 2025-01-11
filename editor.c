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

#define UNREACHABLE() do { fprintf(stderr, "%s:%d: UNREACHABLE\n", __FILE__, __LINE__); exit(1); } while (false)

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
#define GOTO(x, y)     printf("\x1b[%d;%dH", (y), (x))

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

#include "room.h"

typedef struct {
    int x;
    int y;
} v2;

typedef struct {
    v2 player;
    v2 screen_dimensions;
    struct termios original_termios;
    RoomFile rooms;
    bool resized;
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
            if (KEY_MATCHES("q")) {
                end();
                UNREACHABLE();
            } else if (KEY_MATCHES("a") || KEY_MATCHES(LEFT)) {
                state->player.x --;
                if (state->player.x <= 0) {
                    state->player.x = 1;
                }
            } else if (KEY_MATCHES("d") || KEY_MATCHES(RIGHT)) {
                state->player.x ++;
                if (state->player.x > state->screen_dimensions.x) {
                    state->player.x = state->screen_dimensions.x;
                }
            } else if (KEY_MATCHES("w") || KEY_MATCHES(UP)) {
                state->player.y --;
                if (state->player.y <= 0) {
                    state->player.y = 1;
                }
            } else if (KEY_MATCHES("s") || KEY_MATCHES(DOWN)) {
                state->player.y ++;
                if (state->player.y > state->screen_dimensions.y) {
                    state->player.y = state->screen_dimensions.y;
                }
            }

            if (match != NULL) {
                i += strlen(match);
            } else {
                // Swallow unused input
                if (buf[i] == '\x1b') {
                    if (i + 1 < n) {
                        // ESC sequence
                        switch (buf[i]) {
                            case '[':
                                // CSI sequence
                                i += 2;
                                while (i < n && (isdigit(buf[i]) || buf[i] == ';')) i ++;
                                i ++;
                                break;

                            default:
                                // Another sequence, or more likely ALT+char
                                i += 2;
                                break;
                        }
                    } else {
                        i += 1;
                    }
                } else {
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
    GOTO(1, 1);
    printf(RESET_GFX_MODE CLEAR_SCREEN);

    if (state->screen_dimensions.x < WIDTH_TILES || state->screen_dimensions.y < HEIGHT_TILES) {
        char *message = NULL;
        assert(asprintf(&message, "Required screen dimension is %dx%d", WIDTH_TILES, HEIGHT_TILES) >= 0);
        GOTO(state->screen_dimensions.x / 2 - (int)strlen(message) / 2, state->screen_dimensions.y / 2);
        printf("%s", message);
        fflush(stdout);
        return;
    }

    assert(state->player.x >= 1);
    assert(state->player.y >= 1);
    assert(state->player.x < state->screen_dimensions.x);
    assert(state->player.y < state->screen_dimensions.y);
    GOTO(state->player.x, state->player.y);
    printf("@");

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

    assert(tcgetattr(STDIN_FILENO, &state->original_termios) == 0);

    struct termios new = state->original_termios;
    new.c_lflag &= ~(ICANON | ECHO);
    assert(tcsetattr(STDIN_FILENO, TCSANOW, &new) == 0);

    signal(SIGWINCH, sigwinch_handler);

    get_screen_dimensions();

    state->player.x = state->screen_dimensions.x / 2;
    state->player.y = state->screen_dimensions.y - 2;

    assert(readRooms(&state->rooms) && "Check that you have ROOMS.SPL");
}

typedef void *(*main_fn)(char *library, void *call_state);
void *loop_main(char *library, void *call_state) {
    struct stat library_stat;
    assert(stat(library, &library_stat) == 0);

    state = call_state;
    if (state == NULL) setup();
    else get_screen_dimensions();
    while (true) {
        struct stat file_stat;
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

        if (state->resized) {
            get_screen_dimensions();
            state->resized = false;
        }
        process_input();
        update();
        redraw();
        usleep(1000);
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
