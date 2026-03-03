#include <assert.h>
#include <dlfcn.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#define TIME_NEWER(t1, t2) (((t1).tv_sec > (t2).tv_sec) || (((t1).tv_sec == (t2).tv_sec) && (t1).tv_nsec > (t2).tv_nsec))

#ifdef __TINYC__
#define LIBRARY_BUILD_CMD "tcc -g -ggdb -Werror -Wall -Wpedantic -fsanitize=address -fpic -shared room.c editor_actions.c editor_draw.c editor_input.c -o"
#else
#define LIBRARY_BUILD_CMD "cc -g -ggdb -Werror -Wall -Wpedantic -fsanitize=address -fpic -shared room.c editor_actions.c editor_draw.c editor_input.c -o"
#endif

#include "editor_types.h"

bool any_source_newer(struct timespec test_time) {
    struct stat file_stat;
    char *files[] = {
        __FILE__,
        "room.c",
        "room.h",
        "array.h",
        "editor_types.h",
        "editor_actions.c",
        "editor_draw.c",
        "editor_input.c",
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

void get_screen_dimensions() {
    struct winsize w = {0};
    int fds[] = { STDOUT_FILENO, STDERR_FILENO, STDIN_FILENO };
    for (size_t i = 0; i < C_ARRAY_LEN(fds); i++) {
        if (ioctl(fds[i], TIOCGWINSZ, &w) == 0 && w.ws_col && w.ws_row)
            break;
        w = (struct winsize){0};
    }

    /* Non-tty fallback: use the minimum dimensions the editor can work with */
    state->screen_dimensions.x = w.ws_col ? w.ws_col : MIN_WIDTH;
    state->screen_dimensions.y = w.ws_row ? w.ws_row : MIN_HEIGHT;
}

static void sigint_handler(int sig) { (void)sig; end(); }

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

    debugalltoggle();
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
    signal(SIGINT, sigint_handler);

    struct timespec test_time = library_stat.st_mtim;

    redraw();

    while (true) {
        bool dirty = false;

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

        struct stat rooms_stat;
        if (stat("ROOMS.SPL", &rooms_stat) == 0
                && TIME_NEWER(rooms_stat.st_mtim, start_rooms_stat.st_mtim)) {
            fprintf(stderr, "Reloading ROOMS.SPL\n");
            freeRoomFile(&state->rooms);
            assert(readRooms(&state->rooms) && "Check that you have ROOMS.SPL");
            start_rooms_stat = rooms_stat;
            dirty = true;
        }

        dirty |= process_input();   /* blocks up to 1 s */

        if (state->resized) {
            get_screen_dimensions();
            state->resized = false;
            dirty = true;
        }

        dirty |= update();
        if (dirty) redraw();
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
