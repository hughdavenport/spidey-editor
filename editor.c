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
#define LIBRARY_BUILD_CMD "tcc -g -ggdb -Werror -Wall -Wpedantic -fsanitize=address -fpic -shared render.c room.c spl.c editor_actions.c editor_draw.c editor_input.c editor_screen.c -o"
#else
#define LIBRARY_BUILD_CMD "cc -g -ggdb -Werror -Wall -Wpedantic -fsanitize=address -fpic -shared render.c room.c spl.c editor_actions.c editor_draw.c editor_input.c editor_screen.c -o"
#endif

#include "editor_types.h"

bool any_source_newer(struct timespec test_time) {
    struct stat file_stat;
    char *files[] = {
        __FILE__,
        "room.c",
        "room.h",
        "spl.c",
        "spl.h",
        "render.c",
        "render.h",
        "array.h",
        "editor_types.h",
        "editor_actions.c",
        "editor_draw.c",
        "editor_input.c",
        "editor_screen.c",
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

#include "gui_bridge.h"   /* GED_COLS / GED_ROWS: the GUI's text grid (release build) */

game_state *state = NULL;

void end() {
    /* screen_headless: the GUI owns the terminal, leave it alone */
    if (!screen_headless) printf("\033[?1000l\033[?1006l" RESTORE_CURSOR SHOW_CURSOR RESTORE_SCREEN DISABLE_ALT_BUFFER);
    if (state != NULL) {
        if (!screen_headless && isatty(STDIN_FILENO))
            assert(tcsetattr(STDIN_FILENO, TCSANOW, &state->original_termios) == 0);
        clipboard_clear();
        free_undo_stacks();
        ARRAY_FREE(state->macro_buf);
        for (size_t mi = 0; mi < 26; mi++)
            ARRAY_FREE(state->macro_slots[mi]);
        ARRAY_FREE(state->macro_replay);
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
    state->debug.pos = !state->debug.all;
    state->debug.all = !state->debug.all;
}

bool debugany() {
    return state->debug.data ||
        state->debug.objects ||
        state->debug.switches ||
        state->debug.neighbours ||
        state->debug.pos;
}

void get_screen_dimensions() {
    if (screen_headless) {               /* the GUI lays the text screen out on a fixed grid */
        state->screen_dimensions.x = GED_COLS;
        state->screen_dimensions.y = GED_ROWS;
        return;
    }
    struct winsize w = {0};
    int fds[] = { STDOUT_FILENO, STDERR_FILENO, STDIN_FILENO };
    for (size_t i = 0; i < C_ARRAY_LEN(fds); i++) {
        if (ioctl(fds[i], TIOCGWINSZ, &w) == 0 && w.ws_col && w.ws_row)
            break;
        w = (struct winsize){0};
    }

    /* Non-tty fallback: use a wide display suitable for full panel rendering */
    state->screen_dimensions.x = w.ws_col ? w.ws_col : 132;
    state->screen_dimensions.y = w.ws_row ? w.ws_row : 50;
}

static void sigint_handler(int sig) { fprintf(stderr, "SIGINT\n"); (void)sig; end(); }

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
    memset(state->screen_prev, 0xFF, sizeof(state->screen_prev));

    debugalltoggle();
    state->current_state = TILE_EDIT;
    state->partial_byte = 0;

    if (isatty(STDIN_FILENO)) {
        assert(tcgetattr(STDIN_FILENO, &state->original_termios) == 0);
        struct termios new = state->original_termios;
        new.c_lflag &= ~(ICANON | ECHO);
        new.c_iflag &= ~(IXON | IXOFF);
        new.c_iflag &= ~(ISIG);
        assert(tcsetattr(STDIN_FILENO, TCSANOW, &new) == 0);
    }

    get_screen_dimensions();

    assert(readRooms(&state->rooms) && "Check that you have ROOMS.SPL");
    movertab_load();

    for (size_t i = 0; i < C_ARRAY_LEN(state->cursors); i ++) {
        state->cursors[i].x = WIDTH_TILES / 2;
        state->cursors[i].y = HEIGHT_TILES / 2;
    }
    state->current_level = 1;
    state->cursors[state->current_level].x = 3;
    state->cursors[state->current_level].y = HEIGHT_TILES - 2;


    state->help = true;
    state->debug.hex = true;
    state->debug.tiles = true;
}

typedef game_state *(*main_fn)(char *library, game_state *call_state);
game_state *loop_main(char *library, game_state *call_state) {
    struct stat library_stat;
    assert(stat(library, &library_stat) == 0);
    struct stat start_rooms_stat;
    assert(stat("ROOMS.SPL", &start_rooms_stat) == 0);

    state = call_state;
    if (state != NULL && state->size != sizeof(game_state)) {
        free(state);
        state = NULL;
    }
    if (state == NULL) {
        setup();
    } else {
        snprintf(state->status_message, sizeof(state->status_message), "Loaded new library");
        redraw();
        get_screen_dimensions();
        process_input();   /* blocks up to 1 s */
        redraw();
    }

    printf("\033[?1000h\033[?1006h");  /* enable mouse (X10 + SGR) — also re-enables on hot-reload */
    fflush(stdout);

    signal(SIGWINCH, sigwinch_handler);
    signal(SIGINT, sigint_handler);

    struct timespec test_time = library_stat.st_mtim;

    redraw();

    while (true) {
        bool dirty = false;

        /* --release / SPIDEY_NO_HOTRELOAD: never watch or rebuild the sources */
        if (getenv("SPIDEY_NO_HOTRELOAD") == NULL && any_source_newer(test_time)) {
            // rebuild
            char *build_cmd = NULL;
            assert(asprintf(&build_cmd, "%s %s %s", LIBRARY_BUILD_CMD, library, __FILE__) > 0);
            snprintf(state->status_message, sizeof(state->status_message), "Rebuilding editor library");
            redraw();
            int ret = system(build_cmd);
            if (ret == 0) {
                snprintf(state->status_message, sizeof(state->status_message), "Reloading library");
                redraw();
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
            /* Our own last editor_save() already updated state->rooms in memory
             * to match exactly what it just wrote; free-and-reread only when
             * this mtime change isn't the one we made ourselves, else every
             * save eventually races this poll into freeing state mid-use by
             * whatever handler is still holding a pointer into it. */
            bool own_write = rooms_stat.st_mtim.tv_sec == state->last_own_write_mtim.tv_sec
                           && rooms_stat.st_mtim.tv_nsec == state->last_own_write_mtim.tv_nsec;
            start_rooms_stat = rooms_stat;
            if (!own_write) {
                snprintf(state->status_message, sizeof(state->status_message), "Reloading ROOMS.SPL");
                redraw();
                freeRoomFile(&state->rooms);
                assert(readRooms(&state->rooms) && "Check that you have ROOMS.SPL");
                dirty = true;
            }
        }

        dirty |= process_input();   /* blocks up to 1 s */

        if (state->resized) {
            get_screen_dimensions();
            state->resized = false;
            memset(state->screen_prev, 0xFF, sizeof(state->screen_prev));
            dirty = true;
        }

        dirty |= update();
        if (dirty) redraw();
    }
    UNREACHABLE();
    return NULL;
}

// If this function is edited, then you must rebuild the application
#ifdef SPIDEY_RELEASE
/* Release build: the editor is linked in, no editor.so, no rebuilding. */
int editor_main() {
    game_state *saved_state = NULL;
    do {
        saved_state = loop_main("/proc/self/exe", saved_state);
    } while (saved_state != NULL);
    return 0;
}
#else
int editor_main() {
    char *basename = strdup(__FILE__);
    char *dot = strrchr(basename, '.');
    if (dot != NULL) *dot = '\0';
    char *library = NULL;
    assert(asprintf(&library, "./%s.so", basename) > 0);
    free(basename);
    basename = NULL;
    game_state *saved_state = NULL;
    while (true) {
        void *module = dlopen(library, RTLD_NOW);
        if (module == NULL) {
            struct stat src_stat;
            if (stat(__FILE__, &src_stat) != 0) {
                fprintf(stderr, "Editor library %s not found and source not available to build it\n", library);
                free(library);
                return 1;
            }
            char *build_cmd = NULL;
            assert(asprintf(&build_cmd, "%s %s %s", LIBRARY_BUILD_CMD, library, __FILE__) > 0);
            fprintf(stderr, "Building editor library: %s\n", build_cmd);
            assert(system(build_cmd) == 0);
            module = dlopen(library, RTLD_NOW);
            if (!module) {
                fprintf(stderr, "dlopen(%s): %s\n", library, dlerror());
                free(library);
                return 1;
            }
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
#endif /* SPIDEY_RELEASE */
