#include "gui.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Keep libraylib resident (RTLD_GLOBAL so gui.so resolves raylib symbols from
// it; RTLD_NODELETE so it survives gui.so reloads, keeping the window alive).
static bool load_raylib(void) {
    void *rl = dlopen("./" RAYLIB_DIR "/lib/libraylib.so",
                      RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE);
    if (rl == NULL) {
        fprintf(stderr, "gui: cannot load raylib: %s\n", dlerror());
        fprintf(stderr, "gui: expected " RAYLIB_DIR "/lib/libraylib.so (fetch the raylib 6.0 release)\n");
        return false;
    }
    return true;
}

static bool build_gui_lib(void) {
    fprintf(stderr, "gui: building gui.so: %s\n", GUI_BUILD_CMD);
    int ret = system(GUI_BUILD_CMD);
    if (ret != 0) {
        fprintf(stderr, "gui: build failed (%d)\n", ret);
        return false;
    }
    return true;
}

int gui_main(RoomFile *file, const char *data_dir) {
    if (!load_raylib()) return 1;

    struct stat st;
    if (stat("gui.so", &st) != 0 && !build_gui_lib()) return 1;

    void *state = NULL;
    for (;;) {
        void *mod = dlopen("./gui.so", RTLD_NOW);
        if (mod == NULL) {
            fprintf(stderr, "gui: dlopen gui.so: %s\n", dlerror());
            return 1;
        }
        void *(*loop)(const char *, RoomFile *, const char *, void *);
        *(void **)(&loop) = dlsym(mod, "gui_loop");
        if (loop == NULL) {
            fprintf(stderr, "gui: gui.so missing gui_loop: %s\n", dlerror());
            dlclose(mod);
            return 1;
        }
        state = loop("gui.so", file, data_dir, state);
        dlclose(mod);
        if (state == NULL) break;   // window closed
        // Source changed: gui_loop already rebuilt gui.so; reopen it.
    }
    return 0;
}
